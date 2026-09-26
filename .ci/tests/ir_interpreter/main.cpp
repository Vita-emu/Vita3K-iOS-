#include <array>
#include <cassert>
#include <cpu/ir_interpreter.h>
#include <cstring>
#include <iostream>
#include <stdexcept>

int main() {
    std::array<uint8_t, 4096> memory{};
    const auto put = [&](uint32_t address, uint64_t value, unsigned size) {
        if (address > memory.size() - size)
            throw std::runtime_error("write out of range");
        std::memcpy(memory.data() + address, &value, size);
    };
    cpu::IRInterpreter core([&](uint32_t address, unsigned size) {
        if (address > memory.size() - size)
            throw std::runtime_error("read out of range");
        uint64_t value = 0;
        std::memcpy(&value, memory.data() + address, size);
        return value;
    },
        put);
    const auto step = [&] {
        if (!core.step()) {
            std::cerr << core.error() << '\n';
            std::abort();
        }
    };
    put(0, 0xe3a0002a, 4); // mov r0, #42
    put(4, 0xe2801001, 4); // add r1, r0, #1
    put(8, 0xe3520000, 4); // cmp r2, #0
    put(12, 0x12811001, 4); // addne r1, r1, #1 (must skip)
    put(16, 0xe5831000, 4); // str r1, [r3]
    put(20, 0xe5934000, 4); // ldr r4, [r3]
    put(24, 0xef000007, 4); // svc #7
    core.state.regs[3] = 1024;
    for (int i = 0; i < 7; ++i)
        step();
    assert(core.state.regs[0] == 42 && core.state.regs[1] == 43);
    assert(core.state.regs[4] == 43 && memory[1024] == 43);
    assert(core.state.svc_called && core.state.svc == 7 && core.state.regs[15] == 28);

    core.state.regs[15] = 64;
    core.state.cpsr = 0x20;
    put(64, 0x2001, 2); // movs r0, #1
    put(66, 0x3002, 2); // adds r0, #2
    put(68, 0x2803, 2); // cmp r0, #3
    put(70, 0xd101, 2); // bne +2 (must skip)
    put(72, 0xdf03, 2); // svc #3
    for (int i = 0; i < 5; ++i)
        step();
    assert(core.state.regs[0] == 3 && core.state.svc == 3 && core.state.regs[15] == 74);
    assert((core.state.cpsr & 0x60000000) == 0x60000000);

    core.state.cpsr = 0x20;
    core.state.regs[15] = 96;
    put(96, 0x2034f241, 4); // Thumb-2 movw r0, #0x1234
    step();
    assert(core.state.regs[0] == 0x1234 && core.state.regs[15] == 100);
    // No persistent decoded cache: self-modifying code takes effect immediately.
    put(96, 0x2056f241, 4);
    core.state.regs[15] = 96;
    step();
    assert(core.state.regs[0] == 0x1256);

    core.state.cpsr = 0;
    core.state.regs[15] = 128;
    put(128, 0xee300a00, 4); // vadd.f32 s0, s0, s0: explicitly unsupported
    const auto before = core.state.regs;
    assert(!core.step());
    assert(core.error().find("Unsupported IR opcode") != std::string::npos);
    assert(core.state.regs == before);
    core.state.regs[15] = 4096;
    assert(!core.step());
    assert(core.error() == "read out of range");
}
