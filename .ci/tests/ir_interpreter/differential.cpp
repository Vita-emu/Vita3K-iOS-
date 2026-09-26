// Compare scalar IR semantics against the existing JIT on identical inputs.
#include <cassert>
#include <cpu/ir_interpreter.h>
#include <iostream>
#include <random>
#include <testenv.h>

int main() {
    ArmTestEnv env;
    env.code_mem = { 0, 0xeafffffe };
    Dynarmic::A32::UserConfig config{};
    config.callbacks = &env;
    config.arch_version = Dynarmic::A32::ArchVersion::v7;
    config.code_cache_size = 16 * 1024 * 1024;
    Dynarmic::A32::Jit jit(config);
    cpu::IRInterpreter interpreter([&](uint32_t address, unsigned size) {
        uint64_t value = 0;
        for (unsigned i = 0; i < size; ++i) value |= uint64_t(env.MemoryRead8(address + i)) << (8 * i);
        return value; }, [&](uint32_t address, uint64_t value, unsigned size) {
        for (unsigned i = 0; i < size; ++i) env.MemoryWrite8(address + i, value >> (8 * i)); });
    std::mt19937 random(0x768);
    for (uint32_t instruction : { 0xe0902001u, 0xe0b02001u, 0xe0502001u, 0xe0d02001u,
             0xe0102001u, 0xe0302001u, 0xe1902001u, 0xe1f02000u, 0xe16f2f10u,
             0xe1b02110u, 0xe1b02130u, 0xe1b02150u, 0xe1b02170u, 0xe1b02060u }) {
        env.code_mem[0] = instruction;
        jit.ClearCache();
        for (uint32_t shift : { 0u, 1u, 31u, 32u, 33u, 255u }) {
            for (int trial = 0; trial < 16; ++trial) {
                for (auto &reg : interpreter.state.regs)
                    reg = random();
                if (trial < 4)
                    interpreter.state.regs[0] = std::array<uint32_t, 4>{ 0, 0x7fffffff, 0x80000000, 0xffffffff }[trial];
                interpreter.state.regs[1] = shift;
                interpreter.state.regs[15] = 0;
                interpreter.state.cpsr = (random() & 0xf0000000) | 0x10;
                jit.Regs() = interpreter.state.regs;
                jit.SetCpsr(interpreter.state.cpsr);
                env.ticks_left = 1;
                jit.Step();
                if (!interpreter.step()) {
                    std::cerr << interpreter.error() << '\n';
                    std::abort();
                }
                assert(interpreter.state.regs == jit.Regs());
                assert(interpreter.state.cpsr == jit.Cpsr());
            }
        }
    }
}
