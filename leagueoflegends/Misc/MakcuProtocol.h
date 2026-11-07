#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Makcu {
// Firmware frames are "km." followed by a binary five-button mask. Text
// replies share the stream; never interpret an echoed command as a button.
struct Parser {
    std::array<char, 256> line{};
    size_t size = 0;
    bool overflow = false;
    uint8_t mask = 0;
    enum class Event { None, Buttons, Version };
    Event Feed(uint8_t byte) {
        if (!overflow && size == 3 && line[0] == 'k' && line[1] == 'm'
            && line[2] == '.' && byte < 32) {
            mask = byte; size = 0;
            return Event::Buttons;
        }
        if (byte == '\r' || byte == '\n') {
            line[size] = 0;
            const bool version = !overflow && std::strstr(line.data(), "km.MAKCU") != nullptr;
            size = 0; overflow = false;
            return version ? Event::Version : Event::None;
        }
        if (size + 1 < line.size()) line[size++] = static_cast<char>(byte);
        else overflow = true;
        return Event::None;
    }
};
inline std::array<uint8_t, 9> BaudCommand(uint32_t baud) {
    return {0xDE, 0xAD, 5, 0, 0xA5, static_cast<uint8_t>(baud),
        static_cast<uint8_t>(baud >> 8), static_cast<uint8_t>(baud >> 16), static_cast<uint8_t>(baud >> 24)};
}
struct Command {
    std::array<char, 64> text{};
    size_t size = 0;
    uint64_t created = 0;
    bool test = false;
};
// Fixed storage, oldest control command first, newest movement only.
// Owner serializes access; no serial I/O may happen under that lock.
struct Outbox {
    std::array<Command, 32> controls{};
    size_t first = 0, count = 0;
    Command movement{}, recoilMovement{};
    bool hasMovement = false, hasRecoilMovement = false;
    bool preferRecoil = false;
    bool Push(const Command& c, bool move, bool recoil = false) {
        if (move) {
            if (recoil) { recoilMovement = c; hasRecoilMovement = true; }
            else { movement = c; hasMovement = true; }
            return true;
        }
        if (count == controls.size()) return false;
        controls[(first + count++) % controls.size()] = c;
        return true;
    }
    bool Pop(Command& out, uint64_t now) {
        if (count) {
            out = controls[first]; first = (first + 1) % controls.size(); --count;
            return true;
        }
        if (hasMovement && now - movement.created > 50) hasMovement = false;
        if (hasRecoilMovement && now - recoilMovement.created > 50) hasRecoilMovement = false;
        if (!hasMovement && !hasRecoilMovement) return false;
        if (hasRecoilMovement && (preferRecoil || !hasMovement)) {
            out = recoilMovement; hasRecoilMovement = false; preferRecoil = false;
        } else {
            out = movement; hasMovement = false; preferRecoil = true;
        }
        return true;
    }
    void Clear() { first = count = 0; hasMovement = hasRecoilMovement = false; preferRecoil = false; }
};
}
