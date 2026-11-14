// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_imgui_app.h"
#include "portable-file-dialogs.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <sstream>

void Emu51ImguiApp::showSerialTerminal() { serialTerminalOpen_ = true; }

void Emu51ImguiApp::collectSerialOutput()
{
    while (const auto byte = cpu_.takeSerialTransmit()) {
        if (serialTerminalOutput_.size() == emu51::kSerialQueueCapacity) serialTerminalOutput_.pop_front();
        serialTerminalOutput_.push_back(*byte);
        ++serialOutputGeneration_;
    }
}

std::string Emu51ImguiApp::serialTerminalDisplay() const
{
    std::string text;
    std::size_t count = 0;
    for (const auto& byte : serialTerminalOutput_) {
        char hex[12];
        if (serialHexOutput_) {
            std::snprintf(hex, sizeof(hex), "%02X%s ", byte.value, byte.ninth ? "/1" : "");
            text += hex;
            if (++count % 16 == 0) text += '\n';
        } else if (byte.value == '\r') {
            continue;
        } else if (byte.value == '\n' || byte.value == '\t' || (byte.value >= 32 && byte.value < 127)) {
            text += static_cast<char>(byte.value);
        } else {
            std::snprintf(hex, sizeof(hex), "\\x%02X", byte.value);
            text += hex;
        }
    }
    return text;
}

bool Emu51ImguiApp::sendSerialTerminalInput()
{
    if (serialFileStream_.is_open()) {
        serialTerminalStatus_ = "A file transfer is active; wait or cancel before sending text.";
        return false;
    }
    std::vector<std::uint8_t> bytes;
    if (serialHexInput_) {
        std::istringstream input(serialTerminalInput_);
        std::string token;
        while (input >> token) {
            if (token.size() == 4 && token[0] == '0' && (token[1] == 'x' || token[1] == 'X')) token.erase(0, 2);
            unsigned value = 0;
            const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value, 16);
            if (token.size() != 2 || parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size()) {
                serialTerminalStatus_ = "Enter hexadecimal bytes separated by spaces, for example: 41 00 FF.";
                return false;
            }
            bytes.push_back(static_cast<std::uint8_t>(value));
        }
    } else {
        for (const unsigned char byte : std::string(serialTerminalInput_)) bytes.push_back(byte);
    }
    if (serialLineEnding_ == 2 || serialLineEnding_ == 3) bytes.push_back('\r');
    if (serialLineEnding_ == 1 || serialLineEnding_ == 3) bytes.push_back('\n');
    if (bytes.empty()) { serialTerminalStatus_ = "Enter data to send."; return false; }
    if (bytes.size() + cpu_.pendingSerialReceiveCount() > emu51::kSerialQueueCapacity) {
        serialTerminalStatus_ = "Input queue is full; run the program or clear pending input.";
        return false;
    }
    for (const auto byte : bytes) cpu_.queueSerialReceive(byte, serialRxNinth_);
    serialTerminalStatus_ = "Queued " + std::to_string(bytes.size()) + " bytes for the receiver.";
    serialTerminalInput_[0] = '\0';
    return true;
}

void Emu51ImguiApp::requestSerialFileSend()
{
    if (serialFileStream_.is_open()) return;
    std::filesystem::path selected;
    if (serialFilePathPicker_) selected = serialFilePathPicker_();
    else if (pfd::settings::available()) {
        const auto files = pfd::open_file("Send file to serial receiver", serialFilePath_.parent_path().string(),
            {"All files", "*"}).result();
        if (!files.empty()) selected = files.front();
    } else {
        serialTerminalStatus_ = "File dialog unavailable.";
    }
    cycleTiming_.reset();
    if (!selected.empty()) startSerialFileSend(selected);
}

bool Emu51ImguiApp::startSerialFileSend(const std::filesystem::path& path)
{
    if (path.empty()) return false;
    if (serialFileStream_.is_open()) {
        serialTerminalStatus_ = "A file transfer is already active; cancel it before choosing another file.";
        return false;
    }
    if (serialFileLineEnding_ < 0 || serialFileLineEnding_ > 3) {
        serialTerminalStatus_ = "Invalid file line-ending selection.";
        return false;
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        serialTerminalStatus_ = "Unable to open serial input file: " + path.string();
        return false;
    }
    const auto size = std::filesystem::file_size(path, error);
    std::ifstream input(path, std::ios::binary);
    if (error || !input) {
        serialTerminalStatus_ = "Unable to read serial input file: " + path.string();
        return false;
    }
    serialFileStream_ = std::move(input);
    serialFilePath_ = path;
    serialFileSize_ = size;
    serialFileQueued_ = 0;
    serialFileNinth_ = serialRxNinth_;
    serialFileActiveLineEnding_ = serialFileLineEnding_;
    serialFileSkipLf_ = false;
    serialTerminalStatus_ = "Sending file bytes to the serial receiver.";
    serviceSerialFileSend();
    return serialFileStream_.is_open() || serialFileQueued_ == serialFileSize_;
}

void Emu51ImguiApp::serviceSerialFileSend()
{
    if (!serialFileStream_.is_open()) return;
    if (serialFileQueued_ == serialFileSize_) {
        serialFileStream_.close();
        serialTerminalStatus_ = "File queued: " + std::to_string(serialFileQueued_) +
            " source bytes. Pending UART input may still be receiving.";
        return;
    }
    const auto pending = cpu_.pendingSerialReceiveCount();
    // Refill in chunks rather than performing a filesystem read for every
    // consumed byte. Input remains bounded even for files larger than RAM.
    if (pending > emu51::kSerialQueueCapacity / 2) return;
    std::array<char, emu51::kSerialQueueCapacity> buffer;
    // CRLF normalization can expand every source byte to two UART bytes.
    const auto capacity = (buffer.size() - pending) / (serialFileActiveLineEnding_ == 3 ? 2 : 1);
    const auto count = static_cast<std::size_t>(std::min<std::uintmax_t>(
        serialFileSize_ - serialFileQueued_, capacity));
    serialFileStream_.read(buffer.data(), static_cast<std::streamsize>(count));
    if (serialFileStream_.gcount() != static_cast<std::streamsize>(count) || serialFileStream_.bad()) {
        serialFileStream_.close();
        serialTerminalStatus_ = "File read failed after " + std::to_string(serialFileQueued_) + " bytes queued. Transfer stopped.";
        return;
    }
    for (std::size_t i = 0; i < count; ++i) {
        const auto byte = static_cast<std::uint8_t>(static_cast<unsigned char>(buffer[i]));
        if (serialFileActiveLineEnding_ != 0) {
            // Emit a CR immediately, then coalesce its following LF even if
            // the pair straddles file chunks. Lone CR and LF are also endings.
            if (serialFileSkipLf_) {
                serialFileSkipLf_ = false;
                if (byte == '\n') continue;
            }
            if (byte == '\r' || byte == '\n') {
                serialFileSkipLf_ = byte == '\r';
                if (serialFileActiveLineEnding_ >= 2) cpu_.queueSerialReceive('\r', serialFileNinth_);
                if (serialFileActiveLineEnding_ != 2) cpu_.queueSerialReceive('\n', serialFileNinth_);
                continue;
            }
        }
        cpu_.queueSerialReceive(byte, serialFileNinth_);
    }
    serialFileQueued_ += count;
    if (serialFileQueued_ == serialFileSize_) serviceSerialFileSend();
}

void Emu51ImguiApp::cancelSerialFileSend()
{
    if (!serialFileStream_.is_open()) return;
    serialFileStream_.close();
    cpu_.clearSerialReceiveQueue();
    serialTerminalStatus_ = "File transfer canceled; pending input cleared.";
}

void Emu51ImguiApp::drawSerialTerminal()
{
    if (!serialTerminalOpen_) return;
    ImGui::SetNextWindowSize(ImVec2(680, 480), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Serial Terminal", &serialTerminalOpen_)) { ImGui::End(); return; }
    const unsigned mode = cpu_.serialMode();
    ImGui::Text("Mode %u (%s)  SCON %02X  SBUF RX %02X  RI %u  TI %u", mode,
        mode == 0 ? "shift register" : (mode == 1 ? "8-bit UART" : "9-bit UART"),
        cpu_.SCON, cpu_.SBUF, cpu_.SCON & 1u, (cpu_.SCON >> 1) & 1u);
    const double crystal = crystalMHz_ * 1'000'000.0;
    const unsigned timerMode = (cpu_.TMOD >> 4) & 3;
    const unsigned t2con = cpu_.modelInfo().timer2 ? cpu_.readDirect(0xc8) : 0;
    const bool txTimer2 = (mode == 1 || mode == 3) && (t2con & bit4);
    const bool rxTimer2 = (mode == 1 || mode == 3) && (t2con & bit5);
    if (mode == 0 || mode == 2)
        ImGui::Text("Nominal baud: %.2f", crystal / (mode == 0 ? 12 : ((cpu_.PCON & bit7) ? 32 : 64)));
    else {
        for (const bool transmit : {true, false}) {
            const bool timer2 = transmit ? txTimer2 : rxTimer2;
            const char* direction = transmit ? "TX" : "RX";
            if (timer2) {
                const unsigned reload = (cpu_.readDirect(0xcb) << 8) | cpu_.readDirect(0xca);
                if (t2con & bit1) ImGui::TextDisabled("%s baud clock: Timer 2 external counter", direction);
                else ImGui::Text("%s nominal baud: %.2f (Timer 2)", direction, crystal / 32 / (65536 - reload));
            } else if (timerMode == 2)
                ImGui::Text("%s nominal baud: %.2f (Timer 1 mode 2)", direction,
                    crystal / 12 / (256 - cpu_.TH1) / ((cpu_.PCON & bit7) ? 16 : 32));
            else ImGui::TextDisabled("%s baud clock: Timer 1 overflow (timer mode %u)", direction, timerMode);
        }
    }
    ImGui::TextDisabled("%s | %zu pending input bytes | TX %s", running_ ? "Running" : "Paused",
        cpu_.pendingSerialReceiveCount(), cpu_.serialTransmitBusy() ? "busy" : "idle");
    ImGui::TextDisabled("Output retains the latest %zu bytes.", emu51::kSerialQueueCapacity);
    if (!(cpu_.SCON & bit4)) ImGui::TextWrapped("Receiver disabled (REN=0). Queued input waits until firmware enables it.");
    if ((mode == 1 || mode == 3) && (!txTimer2 || !rxTimer2) && (!(cpu_.TCON & bit6) || (cpu_.TMOD & bit6) || (cpu_.TMOD & 3) == 3 ||
        timerMode == 3 || ((cpu_.TMOD & bit7) && !(cpu_.P3 & bit3))))
        ImGui::TextWrapped("Timer 1 baud clock is stopped or uses unsupported counter/split mode.");
    if ((txTimer2 || rxTimer2) && !(t2con & bit2))
        ImGui::TextWrapped("Timer 2 baud clock is stopped (TR2=0).");
    ImGui::Checkbox("Hex output", &serialHexOutput_);
    ImGui::SameLine();
    if (ImGui::Button("Clear output")) serialTerminalOutput_.clear();
    ImGui::SameLine();
    const auto display = serialTerminalDisplay();
    if (ImGui::Button("Copy output")) ImGui::SetClipboardText(display.c_str());
    const float footer = ImGui::GetFrameHeightWithSpacing() * 4 + ImGui::GetTextLineHeightWithSpacing() * 2 +
        (serialFilePath_.empty() ? 0 : ImGui::GetTextLineHeightWithSpacing() + ImGui::GetFrameHeightWithSpacing());
    ImGui::BeginChild("serial_output", ImVec2(0, -footer), true, ImGuiWindowFlags_HorizontalScrollbar);
    const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY();
    ImGui::TextUnformatted(display.c_str());
    if (atBottom && serialDrawnGeneration_ != serialOutputGeneration_) ImGui::SetScrollHereY(1.0f);
    serialDrawnGeneration_ = serialOutputGeneration_;
    ImGui::EndChild();
    ImGui::Checkbox("Hex input", &serialHexInput_);
    ImGui::SameLine(); ImGui::SetNextItemWidth(100);
    ImGui::Combo("Ending", &serialLineEnding_, "None\0LF\0CR\0CRLF\0");
    if (mode >= 2) { ImGui::SameLine(); ImGui::Checkbox("RX ninth bit", &serialRxNinth_); }
    ImGui::SetNextItemWidth(-80);
    ImGui::BeginDisabled(serialFileStream_.is_open());
    const bool submitted = ImGui::InputText("##serial_input", serialTerminalInput_, sizeof(serialTerminalInput_), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    const bool clicked = ImGui::Button("Send");
    if (submitted || clicked) sendSerialTerminalInput();
    if (ImGui::Button("Send file...")) requestSerialFileSend();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(115);
    ImGui::Combo("File endings", &serialFileLineEnding_, "Unchanged\0LF\0CR\0CRLF\0");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Unchanged preserves every byte. Text modes normalize CRLF, CR and LF to the selected ending.");
    ImGui::EndDisabled();
    if (serialFileStream_.is_open()) {
        ImGui::SameLine();
        if (ImGui::Button("Cancel file")) cancelSerialFileSend();
    }
    if (!serialFilePath_.empty()) {
        ImGui::Text("File: %s", serialFilePath_.filename().string().c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", serialFilePath_.string().c_str());
        const std::string progress = std::to_string(serialFileQueued_) + " / " + std::to_string(serialFileSize_) + " source bytes queued";
        ImGui::ProgressBar(serialFileSize_ ? static_cast<float>(static_cast<double>(serialFileQueued_) / serialFileSize_) : 1.0f,
            ImVec2(-1, 0), progress.c_str());
    }
    if (ImGui::Button("Clear pending input")) {
        cancelSerialFileSend(); cpu_.clearSerialReceiveQueue(); serialTerminalStatus_ = "Pending input cleared.";
    }
    ImGui::SameLine(); ImGui::TextDisabled("RX overruns: %llu | TX overwrites: %llu | core output drops: %llu",
        static_cast<unsigned long long>(cpu_.serialReceiveOverruns()), static_cast<unsigned long long>(cpu_.serialTransmitOverwrites()),
        static_cast<unsigned long long>(cpu_.serialOutputDrops()));
    if (!serialTerminalStatus_.empty()) ImGui::TextWrapped("%s", serialTerminalStatus_.c_str());
    ImGui::End();
}
