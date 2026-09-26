// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace cpu {
// Experimental scalar A32/Thumb IR interpreter. No executable allocations or
// persistent translated-block cache. Unsupported operations fail explicitly.
class IRInterpreter {
public:
    struct State {
        std::array<uint32_t, 16> regs{};
        uint32_t cpsr = 0, fpscr = 0;
        bool svc_called = false, halted = false, breakpoint = false;
        uint32_t svc = 0;
    } state;
    using Read = std::function<uint64_t(uint32_t, unsigned)>;
    using Write = std::function<void(uint32_t, uint64_t, unsigned)>;
    IRInterpreter(Read read, Write write);
    ~IRInterpreter();
    IRInterpreter(const IRInterpreter &) = delete;
    IRInterpreter &operator=(const IRInterpreter &) = delete;
    // Executes one guest instruction. On unsupported IR, validates the whole
    // instruction before changing registers or memory. error describes failure.
    bool step();
    const std::string &error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace cpu
