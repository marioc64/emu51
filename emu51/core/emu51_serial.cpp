// SPDX-License-Identifier: GPL-2.0-only
#include "emu51.h"

#include <algorithm>

// Byte/frame model of the classic 12-clock 8051 serial port. See the Intel
// MCS-51 User's Manual, chapter 3, Serial Interface. Register effects and flag
// delivery are observed at instruction boundaries, as with the timers.
void emu51::sync_serial_mode()
{
    if (serialMode_ == serialMode()) return;
    serialMode_ = serialMode();
    serialTx_.reset(); serialRx_.reset(); serialNextTx_.reset();
}

void emu51::writeDirect(std::uint8_t address, std::uint8_t value)
{
    if (address < 0x80) { IRAM[address] = value; return; }
    if (!hasSfr(address)) return;
    if (address != 0x99) {
        SFR[address] = value;
        if (portIo_ && address >= 0x80 && address <= 0xb0 && !(address & 0x0f))
            portIo_->writePort((address - 0x80) >> 4, value);
        if (address == 0x98) sync_serial_mode();
        return;
    }
    sync_serial_mode();
    const SerialByte byte{value, serialMode_ >= 2 && (SCON & bit3) != 0, true};
    if (serialTx_ && serialTx_->flagged) {
        // Firmware may write the next byte after TI, while the stop bit is
        // still being sent. Preserve that stop bit before starting its frame.
        if (serialNextTx_) ++serialTxOverwrites_;
        serialNextTx_ = byte;
    } else {
        // A premature SBUF write restarts the current frame. Such writes are
        // not hardware-conformance behavior; expose the loss to the caller.
        if (serialTx_) ++serialTxOverwrites_;
        serialTx_ = SerialFrame{byte};
    }
}

bool emu51::queueSerialReceive(std::uint8_t value, bool ninth, bool stop)
{
    if (serialInput_.size() >= kSerialQueueCapacity) return false;
    serialInput_.push_back({value, ninth, stop});
    return true;
}

std::optional<emu51::SerialByte> emu51::takeSerialTransmit()
{
    if (serialOutput_.empty()) return std::nullopt;
    const auto byte = serialOutput_.front();
    serialOutput_.pop_front();
    return byte;
}

void emu51::clearSerialReceiveQueue()
{
    serialInput_.clear();
    serialRx_.reset();
}

void emu51::advance_serial(std::uint8_t cycles, unsigned timer1Overflows, unsigned timer2Overflows)
{
    sync_serial_mode();
    if (!serialTx_ && !serialRx_ && serialInput_.empty()) return;
    const unsigned mode = serialMode_;
    // Phase units use twice the clock source frequency, allowing the receiver
    // to sample midway through the stop bit without floating-point time.
    const bool timer2Tx = (model_ == Emu51Model::i8052) && (mode == 1 || mode == 3) && (SFR[0xc8] & bit4);
    const bool timer2Rx = (model_ == Emu51Model::i8052) && (mode == 1 || mode == 3) && (SFR[0xc8] & bit5);
    const unsigned divider = mode == 0 ? 1 : (mode == 2 ? ((PCON & bit7) ? 32 : 64) : ((PCON & bit7) ? 16 : 32));
    const unsigned clocks = mode == 0 ? cycles : (mode == 2 ? cycles * 12u : timer1Overflows);
    const unsigned txDivider = timer2Tx ? 16 : divider;
    const unsigned rxDivider = timer2Rx ? 16 : divider;
    const unsigned txClocks = timer2Tx ? timer2Overflows : clocks;
    const unsigned rxClocks = timer2Rx ? timer2Overflows : clocks;
    const unsigned fullFrame = (mode == 0 ? 16 : (mode == 1 ? 20 : 22)) * txDivider;
    const unsigned transmitFlag = (mode == 0 ? 16 : (mode == 1 ? 18 : 20)) * txDivider;
    const unsigned receiveFlag = (mode == 0 ? 16 : (mode == 1 ? 19 : 21)) * rxDivider;

    const unsigned receiveFullFrame = (mode == 0 ? 16 : (mode == 1 ? 20 : 22)) * rxDivider;
    unsigned remaining = txClocks * 2;
    while (serialTx_ && remaining) {
        auto& frame = *serialTx_;
        const auto step = std::min<std::uint64_t>(remaining, frame.phase < fullFrame ? fullFrame - frame.phase : 0);
        frame.phase += step;
        remaining -= static_cast<unsigned>(step);
        if (!frame.flagged && frame.phase >= transmitFlag) {
            SCON |= bit1;
            frame.flagged = true;
        }
        if (frame.phase < fullFrame) break;
        if (serialOutput_.size() == kSerialQueueCapacity) {
            serialOutput_.pop_front();
            ++serialOutputDrops_;
        }
        serialOutput_.push_back(frame.byte);
        serialTx_.reset();
        if (serialNextTx_) {
            serialTx_ = SerialFrame{*serialNextTx_};
            serialNextTx_.reset();
        }
    }

    if (!(SCON & bit4)) { serialRx_.reset(); return; } // REN
    remaining = rxClocks * 2;
    while (remaining) {
        if (!serialRx_) {
            if (serialInput_.empty() || (mode == 0 && (SCON & bit0))) break;
            serialRx_ = SerialFrame{serialInput_.front()};
            serialInput_.pop_front();
        }
        auto& frame = *serialRx_;
        const auto step = std::min<std::uint64_t>(remaining, frame.phase < receiveFullFrame ? receiveFullFrame - frame.phase : 0);
        frame.phase += step;
        remaining -= static_cast<unsigned>(step);
        if (!frame.flagged && frame.phase >= receiveFlag) {
            frame.flagged = true;
            const bool accepted = mode == 0 || !(SCON & bit5) || (mode == 1 ? frame.byte.stop : frame.byte.ninth);
            if (SCON & bit0) ++serialRxOverruns_;
            else if (accepted) {
                SBUF = frame.byte.value;
                if (mode != 0) {
                    const bool rb8 = mode == 1 ? frame.byte.stop : frame.byte.ninth;
                    SCON = static_cast<std::uint8_t>((SCON & ~bit2) | (rb8 ? bit2 : 0));
                }
                SCON |= bit0;
            }
        }
        if (frame.phase < receiveFullFrame) break;
        serialRx_.reset();
    }
}
