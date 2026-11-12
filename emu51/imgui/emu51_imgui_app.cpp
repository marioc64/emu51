// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_imgui_app.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <fstream>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "assembler.h"
#include "emu51_editor_files.h"
#include "emu51_imgui_actions.h"
#include "emu51_imgui_app_internal.h"
#include "emu51_imgui_asm_editor.h"
#include "emu51_imgui_draw.h"
#include "emu51_imgui_input.h"
#include "emu51_imgui_menu.h"
#include "emu51_imgui_paths.h"
#include "emu51_imgui_seg.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "portable-file-dialogs.h"
#include "TextEditor.h"

namespace {

namespace fs = std::filesystem;
namespace SD = SegmentDisplay;
using namespace emu51_imgui_internal;

constexpr float kDisassemblyKeyRepeatDelay = 0.35f;
constexpr float kDisassemblyKeyRepeatRate = 0.06f;
constexpr float kDisassemblyKeyRepeatMaxFrameDelta = 0.05f;
constexpr float kRegisterBitRadius = 4.5f;
constexpr float kRegisterBitSpacing = 5.0f;
constexpr float kRegisterBitGroupGap = 8.0f;
constexpr float kPswFlagVerticalOffset = 0.5f;
constexpr float kRegisterSegmentScale = 1.0f;
constexpr ImU32 kRegisterBitOffColor = IM_COL32(90, 18, 18, 255);
constexpr ImU32 kRegisterBitOnColor = IM_COL32(230, 110, 110, 255);
constexpr ImU32 kRegisterBitOutlineColor = IM_COL32(125, 34, 34, 255);
constexpr ImU32 kAssemblyPcHighlightColor = IM_COL32(24, 56, 30, 255);
constexpr ImVec4 kPcHeaderColor = ImVec4(1.0f, 0.55f, 0.55f, 1.0f);
constexpr ImVec4 kDptrHeaderColor = ImVec4(0.55f, 0.8f, 1.0f, 1.0f);

const std::vector<std::string> kProgramFileFilters = {
	"Program files", "*.asm *.s03 *.hex *.ihx *.ihex *.bin",
	"Assembler files", "*.asm *.s03",
	"Intel HEX files", "*.hex *.ihx *.ihex",
	"Raw binary files", "*.bin",
	"All files", "*",
};

const std::vector<std::string> kAssemblyFileFilters = {
	"Assembler files", "*.asm *.s03",
	"All files", "*",
};

struct RegisterDescriptor {
	const char* name;
	const char* address;
	unsigned value;
	int bitCount;
};

struct RegisterTableOptions {
	bool showDec = true;
	bool showAscii = true;
	bool showBinary = true;
	bool showSegment = true;
};

bool isCallOpcode(std::uint8_t opcode)
{
	switch (opcode) {
	case 0x11:
	case 0x12:
	case 0x31:
	case 0x51:
	case 0x71:
	case 0x91:
	case 0xb1:
	case 0xd1:
	case 0xf1:
		return true;
	default:
		return false;
	}
}

bool isReturnOpcode(std::uint8_t opcode)
{
	return opcode == 0x22 || opcode == 0x32;
}

char monitorAscii(std::uint8_t value)
{
	return value >= 0x20 && value <= 0x7e ? static_cast<char>(value) : '.';
}

float registerBitWidth(int bitCount)
{
	if (bitCount <= 0)
		return 0.0f;

	float width = 0.0f;
	for (int bit = bitCount - 1; bit >= 0; --bit) {
		width += kRegisterBitRadius * 2.0f;
		if (bit != 0)
			width += kRegisterBitSpacing;
		if (bit != 0 && bit % 8 == 0)
			width += kRegisterBitGroupGap;
	}
	return width;
}

void formatRegisterHex(unsigned value, int bitCount, char* buffer, size_t bufferSize)
{
	if (bitCount > 8)
		std::snprintf(buffer, bufferSize, "%04X", value & 0xffffu);
	else
		std::snprintf(buffer, bufferSize, "%02X", value & 0xffu);
}

void formatRegisterAscii(unsigned value, int bitCount, char* buffer, size_t bufferSize)
{
	const int byteCount = std::max(1, bitCount / 8);
	size_t cursor = 0;
	if (bufferSize == 0)
		return;

	buffer[cursor++] = '\'';
	for (int byteIndex = byteCount - 1; byteIndex >= 0 && cursor + 2 < bufferSize; --byteIndex) {
		const std::uint8_t byteValue = static_cast<std::uint8_t>((value >> (byteIndex * 8)) & 0xffu);
		buffer[cursor++] = monitorAscii(byteValue);
	}
	if (cursor + 1 < bufferSize)
		buffer[cursor++] = '\'';
	buffer[cursor] = '\0';
}

float columnWidthForText(const char* text)
{
	return ImGui::CalcTextSize(text).x + ImGui::GetStyle().CellPadding.x * 2.0f;
}

template <size_t N>
float registerHexColumnWidth(const RegisterDescriptor (&registers)[N])
{
	float width = columnWidthForText("HEX");
	char buffer[8];
	for (const RegisterDescriptor& reg : registers) {
		formatRegisterHex(reg.value, reg.bitCount, buffer, sizeof(buffer));
		width = std::max(width, columnWidthForText(buffer));
	}
	return width;
}

template <size_t N>
float registerDecColumnWidth(const RegisterDescriptor (&registers)[N])
{
	float width = columnWidthForText("DEC");
	char buffer[16];
	for (const RegisterDescriptor& reg : registers) {
		std::snprintf(buffer, sizeof(buffer), "%u", reg.value);
		width = std::max(width, columnWidthForText(buffer));
	}
	return width;
}

template <size_t N>
float registerAsciiColumnWidth(const RegisterDescriptor (&registers)[N])
{
	float width = columnWidthForText("ASCII");
	char buffer[8];
	for (const RegisterDescriptor& reg : registers) {
		formatRegisterAscii(reg.value, reg.bitCount, buffer, sizeof(buffer));
		width = std::max(width, columnWidthForText(buffer));
	}
	return width;
}

template <size_t N>
float registerBinaryColumnWidth(const RegisterDescriptor (&registers)[N])
{
	int maxBitCount = 0;
	for (const RegisterDescriptor& reg : registers)
		maxBitCount = std::max(maxBitCount, reg.bitCount);
	return std::max(columnWidthForText("Binary"), registerBitWidth(maxBitCount) + ImGui::GetStyle().CellPadding.x * 2.0f);
}

float registerSegmentColumnWidth()
{
	return std::max(columnWidthForText("SEG"), SD::Size(kRegisterSegmentScale).x + ImGui::GetStyle().CellPadding.x * 2.0f);
}

std::string fileDialogStartPath(const std::string& loadedProgramPath)
{
	if (!loadedProgramPath.empty()) {
		const fs::path programPath(loadedProgramPath);
		if (fs::is_directory(programPath))
			return programPath.string();
		if (programPath.has_parent_path())
			return programPath.parent_path().string();
		return ".";
	}

	const fs::path bundledPrograms = "progs";
	if (fs::exists(bundledPrograms))
		return bundledPrograms.string();
	return std::string();
}

std::string chooseProgramPath(const std::string& dialogTitle, const std::string& loadedProgramPath)
{
	if (!pfd::settings::available())
		return std::string();

	std::vector<std::string> selection = pfd::open_file(
		dialogTitle,
		fileDialogStartPath(loadedProgramPath),
		kProgramFileFilters).result();
	if (selection.empty())
		return std::string();
	return selection.front();
}

fs::path withDefaultAssemblyExtension(fs::path path)
{
	if (!path.empty() && !path.has_extension())
		path += ".asm";
	return path;
}


bool isAssemblyProgramPath(const fs::path& path)
{
	return emu51_asm::detect_program_file_kind(path) == emu51_asm::ProgramFileKind::assembly_source;
}

std::string diagnosticText(const emu51_asm::Diagnostic& diagnostic)
{
	if (diagnostic.line != 0)
		return diagnostic.file + ":" + std::to_string(diagnostic.line) + ": " + diagnostic.message;
	return diagnostic.file + ": " + diagnostic.message;
}

std::string trimWhitespace(std::string_view text)
{
	size_t start = 0;
	while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0)
		++start;
	size_t end = text.size();
	while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0)
		--end;
	return std::string(text.substr(start, end - start));
}

std::string upperCopy(std::string text)
{
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
		return static_cast<char>(std::toupper(ch));
	});
	return text;
}

bool isDefinitionKeyword(const std::string& text)
{
	const std::string upper = upperCopy(text);
	return upper == "EQU" || upper == "DATA" || upper == "BIT";
}

std::string normalizeOperandSpacing(std::string_view text)
{
	std::string out;
	bool pendingSpace = false;
	for (char ch : text) {
		if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
			pendingSpace = !out.empty();
			continue;
		}
		if (ch == ',') {
			while (!out.empty() && out.back() == ' ')
				out.pop_back();
			out += ", ";
			pendingSpace = false;
			continue;
		}
		if (pendingSpace && !out.empty())
			out += ' ';
		out += ch;
		pendingSpace = false;
	}
	while (!out.empty() && out.back() == ' ')
		out.pop_back();
	return out;
}

std::string formatAsmLine(std::string_view line)
{
	constexpr size_t kLabelColumnWidth = 12;
	constexpr size_t kMnemonicColumn = 12;
	constexpr size_t kOperandColumn = 20;
	constexpr size_t kCommentColumn = 40;

	const size_t commentPos = line.find(';');
	std::string code = trimWhitespace(commentPos == std::string_view::npos ? line : line.substr(0, commentPos));
	const std::string comment = commentPos == std::string_view::npos ? std::string() : trimWhitespace(line.substr(commentPos));
	if (code.empty())
		return comment;

	std::string label;
	const size_t colon = code.find(':');
	if (colon != std::string::npos) {
		label = trimWhitespace(std::string_view(code).substr(0, colon)) + ":";
		code = trimWhitespace(std::string_view(code).substr(colon + 1));
	}

	std::string keyword;
	std::string operands;
	if (!code.empty()) {
		const size_t firstSpace = code.find_first_of(" \t");
		std::string first = firstSpace == std::string::npos ? code : code.substr(0, firstSpace);
		std::string rest = firstSpace == std::string::npos ? std::string() : trimWhitespace(std::string_view(code).substr(firstSpace + 1));

		if (label.empty()) {
			const size_t secondSpace = rest.find_first_of(" \t");
			const std::string second = secondSpace == std::string::npos ? rest : rest.substr(0, secondSpace);
			if (!first.empty() && isDefinitionKeyword(second)) {
				label = first;
				keyword = upperCopy(second);
				operands = secondSpace == std::string::npos ? std::string() : normalizeOperandSpacing(std::string_view(rest).substr(secondSpace + 1));
			}
		}
		if (keyword.empty()) {
			keyword = upperCopy(first);
			operands = normalizeOperandSpacing(rest);
		}
	}

	std::string out;
	if (!label.empty()) {
		out = label;
		if (!keyword.empty() && out.size() < kLabelColumnWidth)
			out.append(kLabelColumnWidth - out.size(), ' ');
	} else if (!keyword.empty()) {
		out.append(kMnemonicColumn, ' ');
	}

	if (!keyword.empty()) {
		out += keyword;
		if (!operands.empty() && out.size() < kOperandColumn)
			out.append(kOperandColumn - out.size(), ' ');
		if (!operands.empty())
			out += operands;
	}

	if (!comment.empty()) {
		if (!out.empty() && out.size() < kCommentColumn)
			out.append(kCommentColumn - out.size(), ' ');
		else if (!out.empty())
			out += ' ';
		out += comment;
	}
	return out;
}

void drawRegisterBinary(unsigned value, int bitCount)
{
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImVec2 cursor = ImGui::GetCursorScreenPos();
	const float totalWidth = registerBitWidth(bitCount);
	const float centerY = cursor.y + ImGui::GetTextLineHeight() * 0.5f;
	float centerX = cursor.x + kRegisterBitRadius;

	for (int bit = bitCount - 1; bit >= 0; --bit) {
		const bool enabled = ((value >> bit) & 0x1u) != 0;
		drawList->AddCircleFilled(ImVec2(centerX, centerY), kRegisterBitRadius, enabled ? kRegisterBitOnColor : kRegisterBitOffColor, 20);
		drawList->AddCircle(ImVec2(centerX, centerY), kRegisterBitRadius, kRegisterBitOutlineColor, 20, 1.0f);
		centerX += kRegisterBitRadius * 2.0f;
		if (bit != 0)
			centerX += kRegisterBitSpacing;
		if (bit != 0 && bit % 8 == 0)
			centerX += kRegisterBitGroupGap;
	}

	ImGui::Dummy(ImVec2(totalWidth, ImGui::GetTextLineHeight()));
}

void drawSingleBitIndicator(bool enabled, float lineHeight)
{
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImVec2 cursor = ImGui::GetCursorScreenPos();
	const float diameter = kRegisterBitRadius * 2.0f;
	const float topPadding = std::max(0.0f, (lineHeight - diameter) * 0.5f);
	const float centerY = cursor.y + topPadding + kRegisterBitRadius + kPswFlagVerticalOffset;
	const float centerX = cursor.x + kRegisterBitRadius;

	drawList->AddCircleFilled(ImVec2(centerX, centerY), kRegisterBitRadius, enabled ? kRegisterBitOnColor : kRegisterBitOffColor, 20);
	drawList->AddCircle(ImVec2(centerX, centerY), kRegisterBitRadius, kRegisterBitOutlineColor, 20, 1.0f);
	ImGui::Dummy(ImVec2(diameter, lineHeight));
}

void drawPswFlag(const char* label, bool enabled, bool sameLine)
{
	if (sameLine)
		ImGui::SameLine(0.0f, 14.0f);
	const float lineHeight = ImGui::GetTextLineHeight();
	ImGui::TextUnformatted(label);
	ImGui::SameLine(0.0f, 4.0f);
	drawSingleBitIndicator(enabled, lineHeight);
}

void drawPswFlagsRow(std::uint8_t psw)
{
	drawPswFlag("C", (psw & 0x80u) != 0, false);
	drawPswFlag("AC", (psw & 0x40u) != 0, true);
	drawPswFlag("F0", (psw & 0x20u) != 0, true);
	drawPswFlag("RS1", (psw & 0x10u) != 0, true);
	drawPswFlag("RS0", (psw & 0x08u) != 0, true);
	drawPswFlag("OV", (psw & 0x04u) != 0, true);
	drawPswFlag("P", (psw & 0x01u) != 0, true);
}

void drawRegisterTableRow(const RegisterDescriptor& reg, const RegisterTableOptions& options)
{
	char label[64];
	char hex[8];
	char ascii[8];
	int column = 0;

	std::snprintf(label, sizeof(label), "%s [%s]", reg.name, reg.address);
	formatRegisterHex(reg.value, reg.bitCount, hex, sizeof(hex));
	if (options.showAscii)
		formatRegisterAscii(reg.value, reg.bitCount, ascii, sizeof(ascii));

	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(column++);
	ImGui::TextUnformatted(label);
	ImGui::TableSetColumnIndex(column++);
	ImGui::TextUnformatted(hex);
	if (options.showDec) {
		ImGui::TableSetColumnIndex(column++);
		ImGui::Text("%u", reg.value);
	}
	if (options.showAscii) {
		ImGui::TableSetColumnIndex(column++);
		ImGui::TextUnformatted(ascii);
	}
	if (options.showBinary) {
		ImGui::TableSetColumnIndex(column++);
		drawRegisterBinary(reg.value, reg.bitCount);
	}
	if (options.showSegment) {
		ImGui::TableSetColumnIndex(column++);
		SD::Digit(static_cast<std::uint8_t>(reg.value & 0xffu), kRegisterSegmentScale);
	}
}

template <size_t N>
void drawRegisterTable(const char* id, const RegisterDescriptor (&registers)[N], const RegisterTableOptions& options = {})
{
	const ImGuiTableFlags flags =
		ImGuiTableFlags_BordersInnerV |
		ImGuiTableFlags_BordersOuterH |
		ImGuiTableFlags_RowBg |
		ImGuiTableFlags_SizingFixedFit;
	int columnCount = 2;
	if (options.showDec)
		++columnCount;
	if (options.showAscii)
		++columnCount;
	if (options.showBinary)
		++columnCount;
	if (options.showSegment)
		++columnCount;

	if (!ImGui::BeginTable(id, columnCount, flags))
		return;

	ImGui::TableSetupColumn("Register", ImGuiTableColumnFlags_WidthStretch, 0.0f, 0);
	ImGui::TableSetupColumn("HEX", ImGuiTableColumnFlags_WidthFixed, registerHexColumnWidth(registers), 1);
	if (options.showDec)
		ImGui::TableSetupColumn("DEC", ImGuiTableColumnFlags_WidthFixed, registerDecColumnWidth(registers));
	if (options.showAscii)
		ImGui::TableSetupColumn("ASCII", ImGuiTableColumnFlags_WidthFixed, registerAsciiColumnWidth(registers));
	if (options.showBinary)
		ImGui::TableSetupColumn("Binary", ImGuiTableColumnFlags_WidthFixed, registerBinaryColumnWidth(registers));
	if (options.showSegment)
		ImGui::TableSetupColumn("SEG", ImGuiTableColumnFlags_WidthFixed, registerSegmentColumnWidth());
	ImGui::TableHeadersRow();

	for (const RegisterDescriptor& reg : registers)
		drawRegisterTableRow(reg, options);

	ImGui::EndTable();
}

}  // namespace

Emu51ImguiApp::Emu51ImguiApp() : Emu51ImguiApp(true) {}

Emu51ImguiApp::Emu51ImguiApp(bool persistState)
	: persistState_(persistState), cpu_(true)
{
	assemblyEditor_ = std::make_unique<TextEditor>();
	emu51ConfigureAsmEditor(*assemblyEditor_);
	assemblyEditor_->SetTransactionCallback([this](std::vector<TextEditor::Change>&) {
		markAssemblyEditorChanged();
	});
	configureAddressSpace();
	resetMachine();
	if (persistState_) {
		loadExecutionSettings();
		loadRecentProgramPaths();
		editorRecovery_ = Emu51EditorRecovery::create(emu51GetEditorRecoveryDirectory());
		recoveryCheckPending_ = true;
	}
	addDisassemblyView(0, true);
	internalRamMonitor_.memoryKind = MonitorView::MemoryKind::InternalRam;
	internalRamMonitor_.open = true;
	internalRamMonitor_.id = 0;
	internalRamMonitor_.start = 0;
	addMonitorView(MonitorView::MemoryKind::ExternalData, 0);
	if (persistState_)
		loadLayoutState();
}

Emu51ImguiApp::~Emu51ImguiApp()
{
	if (!quitApproved_)
		saveEditorRecovery();
	if (persistState_)
		saveLayoutState();
}

void Emu51ImguiApp::tick()
{
	tickAt(Emu51CycleTiming::Clock::now());
}

void Emu51ImguiApp::tickAt(Emu51CycleTiming::Clock::time_point now)
{
	if (!running_)
		return;
	using Clock = Emu51CycleTiming::Clock;
	if (!stepTask_) {
		if (cycleExact_)
			cycleTiming_.update(now, crystalMHz_);
		else
			cycleTiming_.updateInstructions(now, static_cast<unsigned>(stepsPerFrame_));
	}
	const auto deadline = Clock::now() + std::chrono::milliseconds(4);
	for (int step = 0; running_ && step < 65536 &&
		(stepTask_ || cycleTiming_.cyclesDue()); ++step) {
		if (stopBeforeInstruction())
			break;
		const auto previousCycles = cycleCount_;
		const auto opcode = executeInstruction();
		if (!running_)
			break;
		if (stepTask_) {
			++stepTask_->instructions;
			if (isReturnOpcode(opcode) && cpu_.PC == stepTask_->returnAddress && cpu_.SP == stepTask_->returnSp)
				stopForDebugger("step complete");
			else if (stepTask_->instructions >= static_cast<unsigned>(kMaxExecutionStepsPerAction))
				stopForDebugger("step did not complete within the instruction limit");
		} else {
			cycleTiming_.consume(cycleExact_ ? static_cast<unsigned>(cycleCount_ - previousCycles) : 1);
		}
		if ((step & 63) == 63 && Clock::now() >= deadline)
			break;
	}
	updateDisassemblyFollowRange();
}

bool Emu51ImguiApp::shouldAnimate() const
{
	return running_ || virtualScrollActive_ || recoverySavePending_;
}

bool Emu51ImguiApp::consumeQuitRequested()
{
	const bool requested = quitRequested_;
	quitRequested_ = false;
	return requested;
}

void Emu51ImguiApp::requestOpenProgram()
{
	requestProgramLoad(true);
}

void Emu51ImguiApp::requestLoadProgram()
{
	requestProgramLoad(false);
}

bool Emu51ImguiApp::hasLoadedProgram() const
{
	return !loadedProgramPath_.empty();
}

void Emu51ImguiApp::reloadProgram()
{
	if (loadedProgramPath_.empty())
		return;

	if (loadProgram(loadedProgramPath_.c_str(), true, true, loadedProgramBinaryAddress_))
		statusMessage_ = "program reloaded";
}

int Emu51ImguiApp::recentProgramFileCount() const
{
	return static_cast<int>(recentProgramPaths_.size());
}

const std::string& Emu51ImguiApp::recentProgramFilePath(int index) const
{
	static const std::string empty;
	if (index < 0 || static_cast<size_t>(index) >= recentProgramPaths_.size())
		return empty;
	return recentProgramPaths_[static_cast<size_t>(index)];
}

void Emu51ImguiApp::openRecentProgramFile(int index)
{
	if (index < 0 || static_cast<size_t>(index) >= recentProgramPaths_.size())
		return;
	requestProgramLoadFromPath(fs::path(recentProgramPaths_[static_cast<size_t>(index)]), true);
}

void Emu51ImguiApp::clearRecentProgramFiles()
{
	recentProgramPaths_.clear();
	saveRecentProgramPaths();
}

bool Emu51ImguiApp::canSaveAssemblyEditorFile() const
{
	return assemblyEditorState_.open && assemblyEditor_ != nullptr;
}

void Emu51ImguiApp::saveAssemblyEditorFile()
{
	if (!canSaveAssemblyEditorFile())
		return;

	saveAssemblyEditorChanges();
}

void Emu51ImguiApp::saveAssemblyEditorFileAs()
{
	if (!canSaveAssemblyEditorFile())
		return;

	const fs::path path = chooseAssemblyEditorSavePath();
	cycleTiming_.reset();
	if (!path.empty())
		saveAssemblyEditorFileToPath(path);
}

void Emu51ImguiApp::requestQuit()
{
	if (prepareToQuit())
		quitRequested_ = true;
}

bool Emu51ImguiApp::prepareToQuit()
{
	if (quitApproved_)
		return true;
	if (!confirmAssemblyEditorChanges())
		return false;
	quitApproved_ = true;
	clearEditorRecovery();
	return true;
}

bool Emu51ImguiApp::saveAssemblyEditorChanges()
{
	const fs::path path = assemblyEditorState_.sourcePath.empty()
		? chooseAssemblyEditorSavePath() : fs::path(assemblyEditorState_.sourcePath);
	cycleTiming_.reset();
	return !path.empty() && saveAssemblyEditorFileToPath(path);
}

bool Emu51ImguiApp::confirmAssemblyEditorChanges()
{
	trackAssemblyEditorChanges();
	if (!assemblyEditorState_.dirty)
		return true;
	// Keep a recoverable copy even if a native save/confirmation dialog blocks.
	saveEditorRecovery();
	UnsavedChoice choice = UnsavedChoice::Cancel;
	if (unsavedChangesPrompt_) {
		choice = unsavedChangesPrompt_();
	} else if (pfd::settings::available()) {
		const auto button = pfd::message("Unsaved assembly source",
			"Save changes to " + assemblyEditorState_.sourceName +
			"?\n\nYes: Save changes\nNo: Discard changes\nCancel: Keep editing",
			pfd::choice::yes_no_cancel, pfd::icon::warning).result();
		if (button == pfd::button::yes)
			choice = UnsavedChoice::Save;
		else if (button == pfd::button::no)
			choice = UnsavedChoice::Discard;
	} else {
		statusMessage_ = "confirmation dialog unavailable; save the assembly source first";
	}
	cycleTiming_.reset();
	return choice == UnsavedChoice::Discard ||
		(choice == UnsavedChoice::Save && saveAssemblyEditorChanges());
}

void Emu51ImguiApp::markAssemblyEditorChanged()
{
	assemblyEditorState_.dirty = true;
	editorDiagnostics_.clear();
	assemblyEditorState_.compiledImage.reset();
	assemblyEditor_->ClearMarkers();
	if (!recoverySavePending_)
		recoverySaveDue_ = Emu51CycleTiming::Clock::now() + std::chrono::milliseconds(500);
	recoverySavePending_ = true;
}

void Emu51ImguiApp::trackAssemblyEditorChanges()
{
	if (assemblyEditor_ == nullptr)
		return;
	std::string text = assemblyEditor_->GetText();
	if (text != observedAssemblyEditorText_) {
		observedAssemblyEditorText_ = std::move(text);
		markAssemblyEditorChanged();
	}
}

void Emu51ImguiApp::saveEditorRecovery()
{
	trackAssemblyEditorChanges();
	recoverySavePending_ = false;
	if (!persistState_ || !assemblyEditorState_.dirty || assemblyEditor_ == nullptr)
		return;
	if (!editorRecovery_ || !editorRecovery_->save({assemblyEditorState_.sourcePath,
		assemblyEditorState_.sourceName, assemblyEditor_->GetText()}))
		statusMessage_ = "unable to write recovery draft; save the assembly source";
}

void Emu51ImguiApp::clearEditorRecovery()
{
	recoverySavePending_ = false;
	if (editorRecovery_)
		editorRecovery_->clear();
}

void Emu51ImguiApp::recoverAssemblyEditor(const fs::path& root)
{
	recoveryCheckPending_ = false;
	for (const auto& directory : Emu51EditorRecovery::candidates(root)) {
		auto recovery = Emu51EditorRecovery::claim(directory);
		if (!recovery)
			continue; // Another running editor owns this draft.
		const auto draft = recovery->load();
		if (!draft)
			continue;
		if (!recoveryPrompt_ && !pfd::settings::available()) {
			statusMessage_ = "recovery draft available in " + directory.string();
			return;
		}
		RecoveryChoice choice = RecoveryChoice::Keep;
		if (recoveryPrompt_) {
			choice = recoveryPrompt_(draft->sourceName);
		} else {
			const auto button = pfd::message("Recover assembly source",
				"Recover unsaved changes to " + draft->sourceName +
				"?\n\nYes: Recover draft\nNo: Delete draft\nCancel: Keep draft for later",
				pfd::choice::yes_no_cancel, pfd::icon::question).result();
			if (button == pfd::button::yes)
				choice = RecoveryChoice::Restore;
			else if (button == pfd::button::no)
				choice = RecoveryChoice::Discard;
		}
		cycleTiming_.reset();
		if (choice == RecoveryChoice::Discard) {
			recovery->clear();
			continue;
		}
		if (choice != RecoveryChoice::Restore)
			return;
		editorRecovery_ = std::move(recovery);
		assemblyEditor_->SetText(draft->text);
		observedAssemblyEditorText_ = assemblyEditor_->GetText();
		assemblyEditor_->ClearMarkers();
		assemblyEditorState_.sourcePath = draft->sourcePath;
		assemblyEditorState_.sourceName = draft->sourceName;
		assemblyEditorState_.dirty = true;
		assemblyEditorState_.compiledImage.reset();
		assemblyEditorState_.open = true;
		recoverySavePending_ = false;
		statusMessage_ = "recovered unsaved assembly source";
		return;
	}
}

std::string Emu51ImguiApp::windowTitle() const
{
	std::string path = loadedProgramPath_;
	if (path.empty())
		path = assemblyEditorState_.sourcePath;
	if (path.empty())
		return "emu51";

	const fs::path filePath(path);
	const std::string filename = filePath.filename().empty() ? path : filePath.filename().string();
	return "emu51: " + filename;
}

bool Emu51ImguiApp::canUseAssemblyEditorTextCommands() const
{
	return assemblyEditorState_.open &&
		assemblyEditor_ != nullptr &&
		hasActivePane_ &&
		activePane_.kind == PaneKey::Kind::AssemblyEditor;
}

bool Emu51ImguiApp::canUndoAssemblyEditorText() const
{
	return canUseAssemblyEditorTextCommands() && assemblyEditor_->CanUndo();
}

bool Emu51ImguiApp::canRedoAssemblyEditorText() const
{
	return canUseAssemblyEditorTextCommands() && assemblyEditor_->CanRedo();
}

bool Emu51ImguiApp::canFormatAssemblyEditorText() const
{
	return assemblyEditorState_.open && hasAssemblyEditorSource();
}

void Emu51ImguiApp::selectAllAssemblyEditorText()
{
	if (canUseAssemblyEditorTextCommands())
		assemblyEditor_->SelectAll();
}

void Emu51ImguiApp::undoAssemblyEditorText()
{
	if (canUndoAssemblyEditorText()) {
		assemblyEditor_->Undo();
		trackAssemblyEditorChanges();
	}
}

void Emu51ImguiApp::redoAssemblyEditorText()
{
	if (canRedoAssemblyEditorText()) {
		assemblyEditor_->Redo();
		trackAssemblyEditorChanges();
	}
}

void Emu51ImguiApp::copyAssemblyEditorText()
{
	if (canUseAssemblyEditorTextCommands())
		assemblyEditor_->Copy();
}

void Emu51ImguiApp::cutAssemblyEditorText()
{
	if (canUseAssemblyEditorTextCommands())
		assemblyEditor_->Cut();
}

void Emu51ImguiApp::pasteAssemblyEditorText()
{
	if (canUseAssemblyEditorTextCommands())
		assemblyEditor_->Paste();
}

void Emu51ImguiApp::formatAssemblyEditorText()
{
	if (canFormatAssemblyEditorText())
		formatAssemblyEditor();
}

bool Emu51ImguiApp::hasAssemblyEditorSource() const
{
	return assemblyEditor_ != nullptr && !assemblyEditor_->GetText().empty();
}

bool Emu51ImguiApp::hasCompiledAssemblyEditorOutput() const
{
	return assemblyEditorState_.compiledImage.has_value();
}

void Emu51ImguiApp::compileAssemblyEditor()
{
	compileAssemblyEditorImage(true);
	cycleTiming_.reset();
}

void Emu51ImguiApp::loadCompiledAssemblyEditorOutput()
{
	loadCompiledAssemblyEditorOutput(true, false);
	cycleTiming_.reset();
}

void Emu51ImguiApp::compileAndLoadAssemblyEditor()
{
	if (compileAssemblyEditorImage(false))
		loadCompiledAssemblyEditorOutput(false, true);
	cycleTiming_.reset();
}

bool Emu51ImguiApp::running() const
{
	return running_;
}

std::string Emu51ImguiApp::executionTimingStatus() const
{
	char buffer[160];
	if (!cycleExact_) {
		std::snprintf(buffer, sizeof(buffer), "%d instructions/frame", stepsPerFrame_);
	} else if (!running_) {
		std::snprintf(buffer, sizeof(buffer), "Cycle exact | %.9g MHz", crystalMHz_);
	} else {
		const double cyclesPerSecond = cycleTiming_.cyclesPerSecond();
		const double targetCyclesPerSecond = std::llround(crystalMHz_ * 1'000'000.0) / 12.0;
		std::snprintf(buffer, sizeof(buffer), "Cycle exact | %.9g MHz | %.3f Mcycles/s | %.1f%%",
			crystalMHz_, cyclesPerSecond / 1'000'000.0, 100.0 * cyclesPerSecond / targetCyclesPerSecond);
	}
	return buffer;
}

void Emu51ImguiApp::toggleRun()
{
	if (running_) {
		stopForDebugger("paused");
	} else {
		running_ = true;
		skipBreakpoint_ = cpu_.PC;
		cycleTiming_.reset();
	}
}

void Emu51ImguiApp::resetCpuState(bool stopRunning)
{
	const bool wasRunning = running_;
	runToAddress_.reset();
	skipBreakpoint_.reset();
	stepTask_.reset();
	running_ = false;
	instructionCount_ = 0;
	cycleCount_ = 0;
	statusMessage_ = stopRunning ? "cpu reset and stopped" : "cpu reset";
	cpu_.init();
	updateDisassemblyFollowRange();
	if (!stopRunning)
		running_ = wasRunning;
	cycleTiming_.reset();
}

void Emu51ImguiApp::stepIn()
{
	stopForDebugger("step in");
	executeInstruction();
	updateDisassemblyFollowRange();
}

void Emu51ImguiApp::stepOver()
{
	stopForDebugger("step over");
	if (!isCallOpcode(cpu_.readProg(cpu_.PC))) {
		stepIn();
		return;
	}
	stepTask_ = StepTask{nextInstructionAddress(cpu_.PC), cpu_.SP, 0};
	skipBreakpoint_ = cpu_.PC;
	running_ = true;
}

void Emu51ImguiApp::stepOut()
{
	stopForDebugger("step out");
	if (cpu_.SP == 0x07) {
		statusMessage_ = "cannot step out at top level";
		return;
	}
	const auto returnAddress = static_cast<std::uint16_t>((static_cast<unsigned>(cpu_.SFR[cpu_.SP]) << 8) |
		cpu_.SFR[static_cast<std::uint8_t>(cpu_.SP - 1)]);
	stepTask_ = StepTask{returnAddress, static_cast<std::uint8_t>(cpu_.SP - 2), 0};
	skipBreakpoint_ = cpu_.PC;
	running_ = true;
}

void Emu51ImguiApp::loadExecutionSettings()
{
	const auto settings = emu51LoadExecutionSettings(emu51GetSettingsPath());
	cycleExact_ = settings.cycleExact;
	crystalMHz_ = settings.crystalMHz;
	stepsPerFrame_ = settings.stepsPerFrame;
}

bool Emu51ImguiApp::saveExecutionSettings() const
{
	if (!persistState_)
		return true;
	return emu51SaveExecutionSettings(emu51GetSettingsPath(), Emu51ExecutionSettings{
		.cycleExact = cycleExact_, .crystalMHz = crystalMHz_, .stepsPerFrame = stepsPerFrame_});
}

void Emu51ImguiApp::requestExecutionSettings()
{
	executionSettingsDialog_.stepsPerFrame = stepsPerFrame_;
	executionSettingsDialog_.cycleExact = cycleExact_;
	executionSettingsDialog_.crystalMHz = crystalMHz_;
	executionSettingsDialog_.saveFailed = false;
	executionSettingsDialog_.open = true;
}

void Emu51ImguiApp::requestSetProgramCounter()
{
	if (running_)
		return;
	std::snprintf(setProgramCounterDialog_.value, sizeof(setProgramCounterDialog_.value), "%04X", cpu_.PC);
	setProgramCounterDialog_.open = true;
	setProgramCounterDialog_.focusInput = true;
}

bool Emu51ImguiApp::unifiedMode() const
{
	return unifiedMode_;
}

void Emu51ImguiApp::setUnifiedMode(bool unifiedMode)
{
	if (unifiedMode_ == unifiedMode)
		return;

	unifiedMode_ = unifiedMode;
	invalidateInstructionCache();
	configureAddressSpace();
	if (loadedProgramPath_.empty()) {
		resetMachine();
		statusMessage_ = unifiedMode_ ? "unified memory enabled" : "unified memory disabled";
		return;
	}

	loadProgram(loadedProgramPath_.c_str(), true, true, loadedProgramBinaryAddress_);
	statusMessage_ = unifiedMode_ ? "program reloaded in unified mode" : "program reloaded in split mode";
}

bool Emu51ImguiApp::internalRamMonitorVisible() const
{
	return internalRamMonitor_.open;
}

void Emu51ImguiApp::setInternalRamMonitorVisible(bool visible)
{
	internalRamMonitor_.open = visible;
}

bool Emu51ImguiApp::assemblyEditorVisible() const
{
	return assemblyEditorState_.open;
}

void Emu51ImguiApp::setAssemblyEditorVisible(bool visible)
{
	assemblyEditorState_.open = visible;
}

void Emu51ImguiApp::createDisassemblyView()
{
	addDisassemblyView(cpu_.PC, false);
}

void Emu51ImguiApp::createDataMemoryMonitor()
{
	addMonitorView(MonitorView::MemoryKind::ExternalData, 0);
}

void Emu51ImguiApp::createProgramMemoryMonitor()
{
	addMonitorView(MonitorView::MemoryKind::ExternalProgram, 0);
}

void Emu51ImguiApp::clearAllMemory()
{
	clearMemory();
}

void Emu51ImguiApp::clearRamOnly()
{
	clearRam();
}

void Emu51ImguiApp::clearRomOnly()
{
	clearRom();
}

void Emu51ImguiApp::requestFillRamRange()
{
	openFillRangeDialog(false);
}

void Emu51ImguiApp::requestFillRomRange()
{
	openFillRangeDialog(true);
}

bool Emu51ImguiApp::hasActivePane() const
{
	return hasActivePane_ && paneExists(activePane_);
}

void Emu51ImguiApp::snapActivePane(Emu51ImguiSnapTarget target)
{
	if (!hasActivePane())
		return;
	snapPane(activePane_, target);
}

void Emu51ImguiApp::resetSnappedPanes()
{
	paneSnapAssignments_.clear();
	snappedPaneRects_.clear();
	statusMessage_ = "reset snapped layout";
	markLayoutStateDirty();
}

void Emu51ImguiApp::draw()
{
	virtualScrollActive_ = false;
	if (recoveryCheckPending_)
		recoverAssemblyEditor(emu51GetEditorRecoveryDirectory());
#if !defined(__APPLE__)
	emu51HandleImguiShortcuts(*this);
#endif
	if (hasActivePane_ && !paneExists(activePane_))
		hasActivePane_ = false;
	updateDragSnapState();
	drawMainMenu();
	contentTop_ = emu51DrawImguiToolbar(*this);
	updateSnappedPaneLayout();
	drawExecutionSettingsDialog();
	drawBinaryLoadDialog();
	drawRegisters();
	drawDebugger();
	drawAssemblyEditor();
	drawDisassemblyViews();
	drawMonitorViews();
	drawSetProgramCounterDialog();
	drawFillRangeDialog();
	drawSnapSplitters();
	drawDragSnapOverlay();
	if (recoverySavePending_ && Emu51CycleTiming::Clock::now() >= recoverySaveDue_)
		saveEditorRecovery();
	if (persistState_ && layoutStateDirty_)
		saveLayoutState();
}

void Emu51ImguiApp::drawMainMenu()
{
	emu51DrawImguiMainMenuBar(*this);
}

void Emu51ImguiApp::drawAssemblyEditor()
{
	if (!assemblyEditorState_.open || assemblyEditor_ == nullptr)
		return;

	const PaneKey pane{PaneKey::Kind::AssemblyEditor, 0};
	std::string title = "Assembly Editor";
	if (!assemblyEditorState_.sourceName.empty()) {
		title += " - ";
		title += assemblyEditorState_.sourceName;
	}
	if (assemblyEditorState_.dirty)
		title += " *";
	title += "###assembly_editor";

	ImGuiWindowFlags flags = 0;
	applyWindowPlacement(pane, ImVec2(720.0f, 520.0f), &flags);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
	ImGui::Begin(title.c_str(), &assemblyEditorState_.open, flags);
	registerPaneWindow(pane);
	drawPaneSnapContextMenu(pane);

	const bool editorActive =
		ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
		ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
	notePaneActivity(pane, editorActive);

	ImVec2 editorSize = ImGui::GetContentRegionAvail();
	const bool showBuildStatus =
		assemblyEditorState_.buildStatusOpen &&
		(!assemblyEditorState_.buildStatusTitle.empty() || !assemblyEditorState_.buildStatusDetail.empty());
	const ImGuiStyle& style = ImGui::GetStyle();
	const float buildStatusHeight = showBuildStatus
		? std::max(82.0f, ImGui::GetTextLineHeightWithSpacing() * 4.0f + style.FramePadding.y * 2.0f)
		: 0.0f;
	if (showBuildStatus)
		editorSize.y = std::max(0.0f, editorSize.y - buildStatusHeight);
	updateAssemblyEditorDiagnostics(editorDiagnostics_);
	if (!assemblyEditorState_.dirty) {
		const auto source = sourceLines_.find(cpu_.PC);
		if (source != sourceLines_.end() && source->second.line > 0 &&
			(source->second.file == assemblyEditorState_.sourcePath || source->second.file == assemblyEditorState_.sourceName)) {
			assemblyEditor_->AddMarker(static_cast<int>(source->second.line - 1), kAssemblyPcHighlightColor,
				kAssemblyPcHighlightColor, "Program counter", "Program counter");
		}
	}
	assemblyEditor_->Render("##assembly_editor_text", editorSize, false);
	// The editor's transaction callback does not report undo/redo. Observe the
	// final text immediately, including shortcuts handled inside Render().
	trackAssemblyEditorChanges();

	if (showBuildStatus) {
		ImGui::BeginChild("##assembly_editor_build_status", ImVec2(0.0f, buildStatusHeight), true);
		const ImVec4 statusColor = assemblyEditorState_.buildStatusSuccess
			? ImVec4(0.45f, 0.85f, 0.55f, 1.0f)
			: ImVec4(1.0f, 0.45f, 0.45f, 1.0f);
		ImGui::TextColored(statusColor, "%s", assemblyEditorState_.buildStatusTitle.c_str());
		ImGui::SameLine();
		const float closeWidth = ImGui::CalcTextSize("Close").x + style.FramePadding.x * 2.0f;
		ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - closeWidth));
		if (ImGui::SmallButton("Close"))
			assemblyEditorState_.buildStatusOpen = false;
		if (!assemblyEditorState_.buildStatusDetail.empty()) {
			ImGui::PushTextWrapPos(ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x);
			ImGui::TextUnformatted(assemblyEditorState_.buildStatusDetail.c_str());
			ImGui::PopTextWrapPos();
		}
		ImGui::EndChild();
	}
	captureFloatingPaneState(pane);
	ImGui::End();
	ImGui::PopStyleVar(2);

	if (!assemblyEditorState_.open) {
		removePaneSnapTarget(pane);
		if (hasActivePane_ && activePane_ == pane)
			hasActivePane_ = false;
	}
}

void Emu51ImguiApp::drawDisassemblyViews()
{
	for (auto it = disassemblyViews_.begin(); it != disassemblyViews_.end(); ) {
		drawDisassemblyView(*it);
		if (!it->open) {
			const PaneKey pane{PaneKey::Kind::Disassembly, it->id};
			removePaneSnapTarget(pane);
			if (hasActivePane_ && activePane_ == pane)
				hasActivePane_ = false;
			it = disassemblyViews_.erase(it);
			markLayoutStateDirty();
			continue;
		}
		++it;
	}
}

void Emu51ImguiApp::drawDisassemblyView(DisassemblyView& view)
{
	char title[64];
	char popupId[64];
	const PaneKey pane{PaneKey::Kind::Disassembly, view.id};
	std::snprintf(title, sizeof(title), "Disassembly @ %04X###disassembly_%d", view.start, view.id);
	std::snprintf(popupId, sizeof(popupId), "Goto###goto_%d", view.id);
	ImGuiWindowFlags flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoNavInputs;
	applyWindowPlacement(pane, ImVec2(230.0f, 400.0f), &flags);
	ImGui::Begin(title, &view.open, flags);
	registerPaneWindow(pane);
	drawPaneSnapContextMenu(pane);
	if (ImGui::BeginMenuBar()) {
		if (ImGui::Checkbox("Follow PC", &view.followPc))
			markLayoutStateDirty();
		ImGui::EndMenuBar();
	}
	const float childHeight = ImGui::GetContentRegionAvail().y;
	const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
	const int visibleRows = std::max(1, static_cast<int>(std::ceil(childHeight / lineHeight)));
	bool rangeComputed = false;
	char buffer[64];
	ImGui::BeginChild(
		"disassembly_view",
		childRegionSizeWithResizeGripClearance(),
		false,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavInputs);
	ImGui::SetItemDefaultFocus();

	const bool disassemblyActive =
		ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
		ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
	notePaneActivity(pane, disassemblyActive);
	captureFloatingPaneState(pane);

	if (disassemblyActive && ImGui::IsKeyPressed(ImGuiKey_G, false)) {
		std::snprintf(view.gotoAddress, sizeof(view.gotoAddress), "%04X", view.start);
		view.openGotoPopup = true;
	}

	const bool canManualScrollDisassembly = disassemblyActive && (!view.followPc || !running_);
	if (canManualScrollDisassembly) {
		const ImGuiIO& io = ImGui::GetIO();
		const bool upPressed = ImGui::IsKeyPressed(ImGuiKey_UpArrow, false);
		const bool downPressed = ImGui::IsKeyPressed(ImGuiKey_DownArrow, false);
		const bool upHeld = ImGui::IsKeyDown(ImGuiKey_UpArrow);
		const bool downHeld = ImGui::IsKeyDown(ImGuiKey_DownArrow);
		int scrollDirection = 0;
		bool immediateStep = false;
		if (upPressed != downPressed) {
			scrollDirection = upPressed ? -1 : 1;
			immediateStep = true;
		} else if (upHeld != downHeld) {
			scrollDirection = upHeld ? -1 : 1;
		} else if (upHeld && downHeld) {
			scrollDirection = view.keyScrollDirection;
		}
		if (scrollDirection != 0) {
			virtualScrollActive_ = true;
			const auto scrollOneInstruction = [this, &view, scrollDirection]() {
				view.start = scrollDirection < 0
					? previousInstructionAddress(view.start)
					: nextInstructionAddress(view.start);
			};

			if (immediateStep || view.keyScrollDirection != scrollDirection) {
				view.keyScrollDirection = scrollDirection;
				view.keyScrollRepeatTimer = -kDisassemblyKeyRepeatDelay;
				scrollOneInstruction();
			} else {
				view.keyScrollRepeatTimer += std::min(io.DeltaTime, kDisassemblyKeyRepeatMaxFrameDelta);
				if (view.keyScrollRepeatTimer >= 0.0f) {
					scrollOneInstruction();
					view.keyScrollRepeatTimer = std::min(0.0f, view.keyScrollRepeatTimer - kDisassemblyKeyRepeatRate);
				}
			}
		} else {
			view.keyScrollDirection = 0;
			view.keyScrollRepeatTimer = 0.0f;
		}
	} else {
		view.keyScrollDirection = 0;
		view.keyScrollRepeatTimer = 0.0f;
	}

	if (view.openGotoPopup) {
		ImGui::OpenPopup(popupId);
		view.openGotoPopup = false;
	}
	if (ImGui::BeginPopupModal(popupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text("Go to address");
		ImGui::SetNextItemWidth(100.0f);
		ImGui::SetKeyboardFocusHere();
		if (ImGui::InputText("##goto_address", view.gotoAddress, sizeof(view.gotoAddress),
				ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
			if (const auto parsed = emu51ParseHexInput(view.gotoAddress, 0xffff)) {
				view.start = static_cast<std::uint16_t>(*parsed);
				view.followPc = false;
				markLayoutStateDirty();
				ImGui::CloseCurrentPopup();
			} else {
				statusMessage_ = "invalid address";
			}
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Escape))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	ImGui::PushTextWrapPos(ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x);

	std::uint16_t pc = view.start;
	view.end = view.start;
	const float rowWidth = ImGui::GetContentRegionAvail().x;
	const float spaceWidth = ImGui::CalcTextSize(" ").x;
	const float addressWidth = ImGui::CalcTextSize("*0000:").x;
	const float byteBlockWidth = ImGui::CalcTextSize(" 00 00 00  ").x;
	for (int row = 0; row < visibleRows; ++row) {
		const std::uint16_t rowAddress = pc;
		const code_51 opcode = asm51[cpu_.readProg(pc)];
		const std::uint8_t byte0 = cpu_.readProg(rowAddress);
		const std::uint8_t byte1 = cpu_.readProg(static_cast<std::uint16_t>(rowAddress + 1));
		const std::uint8_t byte2 = cpu_.readProg(static_cast<std::uint16_t>(rowAddress + 2));
		emu51DrawAlternatingContentRowBackground(row, rowWidth, lineHeight);
		const float rowStartX = ImGui::GetCursorPosX();
		ImGui::PushID(static_cast<int>(rowAddress));
		ImGui::Text("%s%04X:", breakpoints_.contains(rowAddress) ? "*" : " ", rowAddress);
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			toggleBreakpoint(rowAddress);
		if (ImGui::BeginPopupContextItem("debug_instruction")) {
			if (ImGui::MenuItem("Breakpoint", nullptr, breakpoints_.contains(rowAddress))) toggleBreakpoint(rowAddress);
			if (ImGui::MenuItem("Run to cursor")) runToAddress(rowAddress);
			if (ImGui::MenuItem("Show source", nullptr, false, sourceLines_.contains(rowAddress))) showSource(rowAddress);
			ImGui::EndPopup();
		}
		ImGui::PopID();
		ImGui::SameLine(rowStartX + addressWidth + spaceWidth);
		for (int byteIndex = 0; byteIndex < 3; ++byteIndex) {
			if (byteIndex > 0)
				ImGui::SameLine(0.0f, spaceWidth);
			if (byteIndex < opcode.lenght) {
				const std::uint16_t byteAddress = static_cast<std::uint16_t>(rowAddress + byteIndex);
				const std::uint8_t byteValue = byteIndex == 0 ? byte0 : (byteIndex == 1 ? byte1 : byte2);
				emu51DrawMarkedHexByte(byteValue, byteAddress, cpu_.PC, cpu_.DPTR());
			}
			else {
				ImGui::Dummy(ImVec2(ImGui::CalcTextSize("00").x, lineHeight));
			}
		}
		ImGui::SameLine(rowStartX + addressWidth + byteBlockWidth);
		ImGui::TextUnformatted(opcode.make_ds(cpu_, rowAddress, buffer));
		const auto source = sourceLines_.find(rowAddress);
		if (source != sourceLines_.end() && ImGui::IsItemHovered())
			ImGui::SetTooltip("%s:%zu", source->second.file.c_str(), source->second.line);
		const int advance = std::max<int>(1, opcode.lenght);
		pc = static_cast<std::uint16_t>(rowAddress + advance);
		view.end = pc;
		rangeComputed = true;
	}
	ImGui::PopTextWrapPos();
	ImGui::EndChild();

	if (!rangeComputed)
		view.end = view.start;
	ImGui::End();
}


void Emu51ImguiApp::drawRegisters()
{
	const PaneKey pane{PaneKey::Kind::Registers, 0};
	const int registerBank = (cpu_.PSW >> 3) & 0x03;
	const unsigned registerBankBase = static_cast<unsigned>(registerBank << 3);
	char r0Address[3];
	char r1Address[3];
	char r2Address[3];
	char r3Address[3];
	char r4Address[3];
	char r5Address[3];
	char r6Address[3];
	char r7Address[3];
	char rxHeader[48];

	std::snprintf(r0Address, sizeof(r0Address), "%02X", registerBankBase + 0u);
	std::snprintf(r1Address, sizeof(r1Address), "%02X", registerBankBase + 1u);
	std::snprintf(r2Address, sizeof(r2Address), "%02X", registerBankBase + 2u);
	std::snprintf(r3Address, sizeof(r3Address), "%02X", registerBankBase + 3u);
	std::snprintf(r4Address, sizeof(r4Address), "%02X", registerBankBase + 4u);
	std::snprintf(r5Address, sizeof(r5Address), "%02X", registerBankBase + 5u);
	std::snprintf(r6Address, sizeof(r6Address), "%02X", registerBankBase + 6u);
	std::snprintf(r7Address, sizeof(r7Address), "%02X", registerBankBase + 7u);
	std::snprintf(rxHeader, sizeof(rxHeader), "Rx Registers (Bank %d)###registers_rx", registerBank);

	const RegisterDescriptor cpuRegisters[] = {
		{"A", "E0", cpu_.Acc, 8},
		{"B", "F0", cpu_.B, 8},
		{"PSW", "D0", cpu_.PSW, 8},
		{"SP", "81", cpu_.SP, 8},
		{"PCON", "87", cpu_.PCON, 8},
	};
	const RegisterDescriptor portRegisters[] = {
		{"P0", "80", cpu_.P0, 8},
		{"P1", "90", cpu_.P1, 8},
		{"P2", "A0", cpu_.P2, 8},
		{"P3", "B0", cpu_.P3, 8},
	};
	const RegisterDescriptor timerRegisters[] = {
		{"TMOD", "89", cpu_.TMOD, 8},
		{"TCON", "88", cpu_.TCON, 8},
		{"TH0", "8C", cpu_.TH0, 8},
		{"TL0", "8A", cpu_.TL0, 8},
		{"TH1", "8D", cpu_.TH1, 8},
		{"TL1", "8B", cpu_.TL1, 8},
	};
	const RegisterDescriptor serialRegisters[] = {
		{"SCON", "98", cpu_.SCON, 8},
		{"SBUF", "99", cpu_.SBUF, 8},
		{"IE", "A8", cpu_.IE, 8},
		{"IP", "B8", cpu_.IP, 8},
	};
	const RegisterDescriptor rxRegisters[] = {
		{"R0", r0Address, cpu_.Ri(0), 8},
		{"R1", r1Address, cpu_.Ri(1), 8},
		{"R2", r2Address, cpu_.Ri(2), 8},
		{"R3", r3Address, cpu_.Ri(3), 8},
		{"R4", r4Address, cpu_.Ri(4), 8},
		{"R5", r5Address, cpu_.Ri(5), 8},
		{"R6", r6Address, cpu_.Ri(6), 8},
		{"R7", r7Address, cpu_.Ri(7), 8},
	};

	ImGuiWindowFlags flags = 0;
	applyWindowPlacement(pane, ImVec2(860.0f, 520.0f), &flags);
	if (!ImGui::Begin("Registers", nullptr, flags)) {
		ImGui::End();
		return;
	}
	registerPaneWindow(pane);
	drawPaneSnapContextMenu(pane);
	notePaneActivity(pane,
		ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
		ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem));
	captureFloatingPaneState(pane);

	const bool canEditPc = !running_;
	ImGui::TextColored(kPcHeaderColor, "PC");
	if (canEditPc && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
		ImGui::SetTooltip("Click to set PC.");
	if (canEditPc && ImGui::IsItemClicked())
		requestSetProgramCounter();
	ImGui::SameLine(0.0f, 6.0f);
	ImGui::TextColored(kPcHeaderColor, "%04X", cpu_.PC);
	if (canEditPc && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
		ImGui::SetTooltip("Click to set PC.");
	if (canEditPc && ImGui::IsItemClicked())
		requestSetProgramCounter();
	ImGui::SameLine(0.0f, 18.0f);
	ImGui::TextColored(kDptrHeaderColor, "DPTR %04X", cpu_.DPTR());
	drawPswFlagsRow(cpu_.PSW);
	ImGui::Separator();

	if (ImGui::CollapsingHeader("CPU###registers_cpu", ImGuiTreeNodeFlags_DefaultOpen))
		drawRegisterTable("registers_cpu_table", cpuRegisters);
	if (ImGui::CollapsingHeader("Ports###registers_ports", ImGuiTreeNodeFlags_DefaultOpen))
		drawRegisterTable("registers_ports_table", portRegisters);
	if (ImGui::CollapsingHeader("Timers###registers_timers"))
		drawRegisterTable("registers_timers_table", timerRegisters);
	if (ImGui::CollapsingHeader("Serial & Interrupts###registers_serial"))
		drawRegisterTable("registers_serial_table", serialRegisters);
	ImGui::SetNextItemOpen(false, ImGuiCond_FirstUseEver);
	if (ImGui::CollapsingHeader(rxHeader))
		drawRegisterTable("registers_rx_table", rxRegisters);

	ImGui::End();
}

void Emu51ImguiApp::resetMachine()
{
	stepTask_.reset();
	runToAddress_.reset();
	skipBreakpoint_.reset();
	sourceLines_.clear();
	trace_.clear();
	running_ = false;
	instructionCount_ = 0;
	cycleCount_ = 0;
	statusMessage_.clear();
	std::fill(ramMemory_.begin(), ramMemory_.end(), 0);
	std::fill(romMemory_.begin(), romMemory_.end(), 0);
	std::fill(unifiedMemory_.begin(), unifiedMemory_.end(), 0);
	invalidateInstructionCache();
	cpu_.init();
}

void Emu51ImguiApp::requestProgramLoad(bool resetCpuBeforeLoad)
{
	std::string path = chooseProgramPath(
		resetCpuBeforeLoad ? "Open program" : "Load program",
		loadedProgramPath_);
	cycleTiming_.reset();
	if (path.empty()) {
		if (!pfd::settings::available())
			statusMessage_ = "file dialog unavailable";
		return;
	}

	requestProgramLoadFromPath(fs::path(path), resetCpuBeforeLoad);
}

void Emu51ImguiApp::requestProgramLoadFromPath(const fs::path& path, bool resetCpuBeforeLoad)
{
	if (path.empty())
		return;

	if (emu51_asm::detect_program_file_kind(path) == emu51_asm::ProgramFileKind::raw_binary) {
		binaryLoadDialog_.path = normalizedProgramPath(path);
		binaryLoadDialog_.resetCpuBeforeLoad = resetCpuBeforeLoad;
		std::snprintf(binaryLoadDialog_.address, sizeof(binaryLoadDialog_.address), "0000");
		binaryLoadDialog_.open = true;
		return;
	}

	if (isAssemblyProgramPath(path)) {
		if (!confirmAssemblyEditorChanges() || !loadAssemblyFileIntoEditor(path))
			return;
		rememberRecentProgramPath(path);
	}

	const std::string pathText = path.string();
	loadProgram(pathText.c_str(), resetCpuBeforeLoad, resetCpuBeforeLoad);
}

bool Emu51ImguiApp::loadProgram(
	const char* path,
	bool resetCpuBeforeLoad,
	bool rememberForReload,
	std::uint16_t binaryLoadAddress,
	bool rememberRecent)
{
	const std::string requestedPath = path != nullptr ? path : "";
	const auto prepared = emu51_asm::prepare_program_file(
		fs::path(requestedPath), emu51_asm::ProgramLoadOptions{binaryLoadAddress});
	const auto& result = prepared.result;
	if (result.success) {
		if (resetCpuBeforeLoad)
			resetMachine();
		emu51_asm::apply_program_image(cpu_, prepared.image);
		setLoadedSourceMap(prepared.image);
		invalidateInstructionCache();
		if (rememberRecent)
			rememberRecentProgramPath(fs::path(requestedPath));
		if (rememberForReload) {
			loadedProgramPath_ = normalizedProgramPath(fs::path(requestedPath));
			loadedProgramBinaryAddress_ = binaryLoadAddress;
		}
		updateDisassemblyFollowRange();
	}
	statusMessage_ = result.message;
	cycleTiming_.reset();
	return result.success;
}

bool Emu51ImguiApp::loadAssemblyFileIntoEditor(const fs::path& path)
{
	if (assemblyEditor_ == nullptr)
		return false;
	std::error_code error;
	if (!fs::is_regular_file(path, error)) {
		statusMessage_ = "unable to open assembly source file";
		return false;
	}

	std::ifstream input(path, std::ios::binary);
	if (!input.is_open()) {
		statusMessage_ = "unable to open assembly source";
		return false;
	}

	std::string source;
	char chunk[4096];
	while (input.read(chunk, sizeof(chunk)) || input.gcount() != 0)
		source.append(chunk, static_cast<std::size_t>(input.gcount()));
	if (input.bad() || !input.eof()) {
		statusMessage_ = "unable to read assembly source";
		return false;
	}
	assemblyEditor_->SetText(source);
	// Loading normalizes CRLF, tabs and UTF-8 BOMs. Track the editor's
	// representation so that normalization alone never looks like an edit.
	observedAssemblyEditorText_ = assemblyEditor_->GetText();
	assemblyEditor_->ClearMarkers();
	assemblyEditorState_.sourcePath = normalizedProgramPath(path);
	assemblyEditorState_.sourceName = path.filename().empty() ? path.string() : path.filename().string();
	assemblyEditorState_.compiledImage.reset();
	assemblyEditorState_.dirty = false;
	assemblyEditorState_.open = true;
	clearEditorRecovery();
	return true;
}

bool Emu51ImguiApp::saveAssemblyEditorFileToPath(const fs::path& path)
{
	if (assemblyEditor_ == nullptr || path.empty())
		return false;

	const std::string text = assemblyEditor_->GetText();
	if (!emu51WriteTextFileAtomically(path, text)) {
		statusMessage_ = "failed to save assembly source; original file preserved";
		return false;
	}

	const bool pathChanged = assemblyEditorState_.sourcePath.empty() || fs::path(assemblyEditorState_.sourcePath) != path;
	assemblyEditorState_.sourcePath = path.string();
	assemblyEditorState_.sourceName = path.filename().empty() ? path.string() : path.filename().string();
	assemblyEditorState_.dirty = false;
	observedAssemblyEditorText_ = text;
	if (pathChanged)
		assemblyEditorState_.compiledImage.reset();
	clearEditorRecovery();
	statusMessage_ = "saved assembly source to " + assemblyEditorState_.sourcePath;
	return true;
}

fs::path Emu51ImguiApp::chooseAssemblyEditorSavePath()
{
	if (savePathPicker_)
		return savePathPicker_();
	if (!pfd::settings::available()) {
		statusMessage_ = "file dialog unavailable";
		return {};
	}

	fs::path defaultPath;
	if (!assemblyEditorState_.sourcePath.empty()) {
		defaultPath = assemblyEditorState_.sourcePath;
	} else {
		const std::string startPath = fileDialogStartPath(loadedProgramPath_);
		const std::string filename = assemblyEditorState_.sourceName.empty() ? "untitled.asm" : assemblyEditorState_.sourceName;
		defaultPath = startPath.empty() ? fs::path(filename) : fs::path(startPath) / filename;
	}

	const std::string selected = pfd::save_file(
		"Save assembly file",
		defaultPath.string(),
		kAssemblyFileFilters).result();
	if (selected.empty())
		return {};
	return withDefaultAssemblyExtension(fs::path(selected));
}

fs::path Emu51ImguiApp::assemblyEditorBaseDirectory() const
{
	if (!assemblyEditorState_.sourcePath.empty()) {
		const fs::path sourcePath(assemblyEditorState_.sourcePath);
		if (sourcePath.has_parent_path())
			return sourcePath.parent_path();
	}
	return {};
}

void Emu51ImguiApp::formatAssemblyEditor()
{
	if (assemblyEditor_ == nullptr || assemblyEditor_->GetText().empty()) {
		statusMessage_ = "no assembly source in editor";
		return;
	}

	assemblyEditor_->FilterLines(formatAsmLine);
	markAssemblyEditorChanged();
	statusMessage_ = "formatted assembly source";
}

void Emu51ImguiApp::updateAssemblyEditorDiagnostics(const std::vector<emu51_asm::Diagnostic>& diagnostics)
{
	editorDiagnostics_ = diagnostics;
	if (assemblyEditor_ == nullptr)
		return;

	assemblyEditor_->ClearMarkers();
	const std::string sourceDiagnosticName = assemblyEditorState_.sourcePath.empty()
		? assemblyEditorState_.sourceName
		: assemblyEditorState_.sourcePath;
	for (const emu51_asm::Diagnostic& diagnostic : diagnostics) {
		if (diagnostic.line == 0)
			continue;
		if (diagnostic.file != sourceDiagnosticName && diagnostic.file != assemblyEditorState_.sourceName)
			continue;
		const int line = static_cast<int>(diagnostic.line - 1);
		const std::string tooltip = diagnosticText(diagnostic);
		assemblyEditor_->AddMarker(
			line,
			IM_COL32(255, 95, 95, 255),
			IM_COL32(255, 205, 205, 255),
			tooltip,
			tooltip);
	}
}

void Emu51ImguiApp::setAssemblyEditorBuildStatus(bool success, std::string title, std::string detail)
{
	assemblyEditorState_.buildStatusSuccess = success;
	assemblyEditorState_.buildStatusTitle = std::move(title);
	assemblyEditorState_.buildStatusDetail = std::move(detail);
	assemblyEditorState_.buildStatusOpen = true;
}

bool Emu51ImguiApp::compileAssemblyEditorImage(bool updateStatus)
{
	trackAssemblyEditorChanges();
	assemblyEditorState_.compiledImage.reset();
	if (!hasAssemblyEditorSource()) {
		statusMessage_ = "no assembly source in editor";
		setAssemblyEditorBuildStatus(false, "Compile failed", statusMessage_);
		return false;
	}
	const std::string sourceName = assemblyEditorState_.sourcePath.empty()
		? assemblyEditorState_.sourceName : assemblyEditorState_.sourcePath;
	emu51_asm::Assembler assembler;
	auto result = assembler.assemble_string(sourceName, assemblyEditor_->GetText(), assemblyEditorBaseDirectory());
	updateAssemblyEditorDiagnostics(result.diagnostics);
	if (!result.success()) {
		statusMessage_ = result.diagnostics.empty() ? "assembly failed" : diagnosticText(result.diagnostics.front());
		setAssemblyEditorBuildStatus(false, "Compile failed", statusMessage_);
		return false;
	}
	assemblyEditorState_.compiledImage = std::move(result.image);
	if (updateStatus) {
		statusMessage_ = "compiled " + std::to_string(assemblyEditorState_.compiledImage->bytes.size()) + " bytes";
		setAssemblyEditorBuildStatus(true, "Compile succeeded", statusMessage_);
	}
	return true;
}

bool Emu51ImguiApp::loadCompiledAssemblyEditorOutput(bool updateStatus, bool resetBeforeLoad)
{
	trackAssemblyEditorChanges();
	if (!hasCompiledAssemblyEditorOutput()) {
		statusMessage_ = "compile assembly source first";
		setAssemblyEditorBuildStatus(false, "Load failed", statusMessage_);
		return false;
	}
	if (resetBeforeLoad)
		resetMachine();
	emu51_asm::apply_program_image(cpu_, *assemblyEditorState_.compiledImage);
	setLoadedSourceMap(*assemblyEditorState_.compiledImage);
	invalidateInstructionCache();
	updateDisassemblyFollowRange();
	cycleTiming_.reset();
	statusMessage_ = updateStatus ? "compiled output loaded" : "compiled and loaded";
	setAssemblyEditorBuildStatus(true, updateStatus ? "Load succeeded" : "Compile and load succeeded",
		resetBeforeLoad ? "Reset memory and CPU state, then loaded compiled program."
			: "Loaded compiled program into memory.");
	return true;
}

void Emu51ImguiApp::runSteps(int count)
{
	for (int i = 0; running_ && i < count; ++i) {
		if (stopBeforeInstruction())
			break;
		executeInstruction();
	}
	updateDisassemblyFollowRange();
}

void Emu51ImguiApp::updateDisassemblyFollowRange()
{
	for (DisassemblyView& view : disassemblyViews_) {
		if (!view.followPc)
			continue;
		if (cpu_.PC < view.start || cpu_.PC >= view.end)
			view.start = cpu_.PC;
	}
}

void Emu51ImguiApp::addDisassemblyView(std::uint16_t start, bool followPc)
{
	DisassemblyView view;
	view.id = nextDisassemblyViewId_++;
	view.followPc = followPc;
	view.start = start;
	view.end = start;
	disassemblyViews_.push_back(view);
	markLayoutStateDirty();
}

void Emu51ImguiApp::addMonitorView(MonitorView::MemoryKind memoryKind, std::uint16_t start)
{
	MonitorView view;
	view.id = nextMonitorViewId_++;
	view.memoryKind = memoryKind;
	view.start = clampMonitorAddress(view, start);
	monitorViews_.push_back(view);
}

void Emu51ImguiApp::configureAddressSpace()
{
	auto readData = [this](std::uint16_t addr) -> std::uint8_t {
		noteMemoryAccess(WatchSpace::Data, addr, false);
		return unifiedMode_ ? unifiedMemory_[addr] : ramMemory_[addr];
	};
	auto writeData = [this](std::uint16_t addr, std::uint8_t data) {
		noteMemoryAccess(WatchSpace::Data, addr, true);
		if (unifiedMode_) {
			sourceLines_.erase(addr);
			unifiedMemory_[addr] = data;
			prevInstructionCacheDirty_ = true;
		} else {
			ramMemory_[addr] = data;
		}
	};
	auto readProg = [this](std::uint16_t addr) -> std::uint8_t {
		noteMemoryAccess(WatchSpace::Program, addr, false);
		return unifiedMode_ ? unifiedMemory_[addr] : romMemory_[addr];
	};
	auto writeProg = [this](std::uint16_t addr, std::uint8_t data) {
		noteMemoryAccess(WatchSpace::Program, addr, true);
		sourceLines_.erase(addr);
		if (unifiedMode_)
			unifiedMemory_[addr] = data;
		else
			romMemory_[addr] = data;
		prevInstructionCacheDirty_ = true;
	};
	cpu_.setAddressSpace(std::make_shared<functional_address_space>(readData, writeData, readProg, writeProg));
}

void Emu51ImguiApp::clearMemory()
{
	stopForDebugger("memory cleared");
	sourceLines_.clear();
	running_ = false;
	instructionCount_ = 0;
	cycleCount_ = 0;
	loadedProgramPath_.clear();
	loadedProgramBinaryAddress_ = 0;
	statusMessage_ = unifiedMode_ ? "cleared memory" : "cleared data and program memory";
	std::fill(ramMemory_.begin(), ramMemory_.end(), 0);
	std::fill(romMemory_.begin(), romMemory_.end(), 0);
	std::fill(unifiedMemory_.begin(), unifiedMemory_.end(), 0);
	invalidateInstructionCache();
	cpu_.init();
}

void Emu51ImguiApp::clearRam()
{
	stopForDebugger("memory cleared");
	sourceLines_.clear();
	running_ = false;
	instructionCount_ = 0;
	cycleCount_ = 0;
	statusMessage_ = unifiedMode_ ? "cleared memory" : "cleared data memory";
	std::fill(ramMemory_.begin(), ramMemory_.end(), 0);
	std::fill(unifiedMemory_.begin(), unifiedMemory_.end(), 0);
	if (unifiedMode_)
		invalidateInstructionCache();
	cpu_.init();
}

void Emu51ImguiApp::clearRom()
{
	stopForDebugger("memory cleared");
	sourceLines_.clear();
	running_ = false;
	instructionCount_ = 0;
	cycleCount_ = 0;
	loadedProgramPath_.clear();
	loadedProgramBinaryAddress_ = 0;
	statusMessage_ = unifiedMode_ ? "cleared memory" : "cleared program memory";
	std::fill(romMemory_.begin(), romMemory_.end(), 0);
	std::fill(unifiedMemory_.begin(), unifiedMemory_.end(), 0);
	invalidateInstructionCache();
	if (unifiedMode_)
		cpu_.init();
}

void Emu51ImguiApp::openFillRangeDialog(bool fillRom)
{
	fillRangeDialog_.fillRom = fillRom;
	std::snprintf(fillRangeDialog_.start, sizeof(fillRangeDialog_.start), "%04X", 0);
	std::snprintf(fillRangeDialog_.end, sizeof(fillRangeDialog_.end), "%04X", 0);
	std::snprintf(fillRangeDialog_.value, sizeof(fillRangeDialog_.value), "%02X", 0);
	fillRangeDialog_.open = true;
}

void Emu51ImguiApp::fillRange(bool fillRom, std::uint16_t start, std::uint16_t end, std::uint8_t value)
{
	if (stepTask_)
		stopForDebugger("step canceled by memory edit");
	if (start > end)
		std::swap(start, end);
	auto& memory = unifiedMode_ ? unifiedMemory_ : (fillRom ? romMemory_ : ramMemory_);
	for (std::uint16_t address = start; ; ++address) {
		memory[address] = value;
		if (address == end)
			break;
	}
	if (unifiedMode_ || fillRom) {
		sourceLines_.clear();
		invalidateInstructionCache();
	}
	statusMessage_ = unifiedMode_ ? "filled memory range" : (fillRom ? "filled program memory range" : "filled data memory range");
	if (unifiedMode_ || !fillRom)
		cpu_.init();
	cycleTiming_.reset();
}

std::uint8_t Emu51ImguiApp::readProgramByte(std::uint16_t address) const
{
	return unifiedMode_ ? unifiedMemory_[address] : romMemory_[address];
}

std::uint16_t Emu51ImguiApp::nextInstructionAddress(std::uint16_t address)
{
	const std::uint8_t opcode = readProgramByte(address);
	const int advance = std::max<int>(1, asm51[opcode].lenght);
	return static_cast<std::uint16_t>(address + advance);
}

void Emu51ImguiApp::invalidateInstructionCache()
{
	prevInstructionCacheDirty_ = true;
}

std::uint16_t Emu51ImguiApp::previousInstructionAddress(std::uint16_t address)
{
	if (address == 0)
		return 0;

	if (prevInstructionCacheDirty_) {
		prevInstructionCacheDirty_ = false;
		if (++prevInstructionCacheVersion_ == 0) {
			prevInstructionCache_.fill(0);
			prevInstructionCacheVersion_ = 1;
		}
	}

	const std::uint32_t cached = prevInstructionCache_[address];
	if ((cached >> 16) == prevInstructionCacheVersion_)
		return static_cast<std::uint16_t>(cached & 0xffffu);

	std::uint16_t bestCandidate = static_cast<std::uint16_t>(address - 1);
	int bestScore = -1;

	for (int back = 3; back >= 1; --back) {
		if (back > address)
			continue;
		const std::uint16_t candidate = static_cast<std::uint16_t>(address - back);
		if (nextInstructionAddress(candidate) != address)
			continue;

		const int score = backwardCandidateScore(candidate, address) * 10 + back;
		if (score > bestScore) {
			bestScore = score;
			bestCandidate = candidate;
		}
	}

	prevInstructionCache_[address] =
		(static_cast<std::uint32_t>(prevInstructionCacheVersion_) << 16) |
		static_cast<std::uint32_t>(bestCandidate);
	return bestCandidate;
}

int Emu51ImguiApp::backwardCandidateScore(std::uint16_t start, std::uint16_t end)
{
	int score = 0;

	const int span = static_cast<int>(end - start);
	if (span >= 1 && span <= 3)
		score += span;

	for (int back = 3; back >= 1; --back) {
		if (back > start)
			continue;
		const std::uint16_t predecessor = static_cast<std::uint16_t>(start - back);
		if (nextInstructionAddress(predecessor) == start) {
			score += 10 + back;
			break;
		}
	}

	if (start >= 4) {
		const std::uint16_t overlap4 = static_cast<std::uint16_t>(end - 4);
		if (overlap4 < start && nextInstructionAddress(overlap4) == static_cast<std::uint16_t>(end - 1))
			score -= 3;
	}
	if (start >= 5) {
		const std::uint16_t overlap5 = static_cast<std::uint16_t>(end - 5);
		if (overlap5 < start && nextInstructionAddress(overlap5) == static_cast<std::uint16_t>(end - 2))
			score -= 2;
	}

	return score;
}

std::uint8_t Emu51ImguiApp::executeInstruction()
{
	const auto pc = cpu_.PC;
	const std::uint8_t opcode = cpu_.readProg(pc);
	TraceEntry entry;
	if (traceEnabled_) {
		entry.instruction = instructionCount_;
		entry.pc = pc;
		for (unsigned i = 0; i < 3; ++i)
			entry.bytes[i] = cpu_.readProg(static_cast<std::uint16_t>(pc + i));
	}
	std::array<std::uint8_t, 256> previousInternal;
	if (!watchpoints_.empty())
		std::copy(std::begin(cpu_.SFR), std::end(cpu_.SFR), previousInternal.begin());
	pendingWatchpointStop_.clear();
	instructionExecuting_ = true;
	const auto cycles = static_cast<std::uint8_t>(cpu_.process());
	instructionExecuting_ = false;
	cycleCount_ += cycles;
	++instructionCount_;
	for (const auto& watch : watchpoints_) {
		if (watch.enabled && watch.space == WatchSpace::Internal && watch.address < 256 &&
			previousInternal[watch.address] != cpu_.SFR[watch.address] && pendingWatchpointStop_.empty()) {
			char reason[64];
			std::snprintf(reason, sizeof(reason), "internal change watchpoint at %02X", watch.address);
			pendingWatchpointStop_ = reason;
		}
	}
	if (traceEnabled_) {
		entry.nextPc = cpu_.PC; entry.cycles = cycles;
		entry.acc = cpu_.Acc; entry.psw = cpu_.PSW; entry.sp = cpu_.SP;
		if (trace_.size() == kTraceCapacity)
			trace_.pop_front();
		trace_.push_back(entry);
	}
	if (!pendingWatchpointStop_.empty())
		stopForDebugger(pendingWatchpointStop_);
	return opcode;
}
