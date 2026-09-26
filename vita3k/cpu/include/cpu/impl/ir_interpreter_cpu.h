// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
#pragma once
#include <atomic>
#include <cpu/impl/interface.h>
#include <cpu/ir_interpreter.h>

class IRInterpreterCPU final : public CPUInterface {
    CPUState *parent;
    std::size_t core_id;
    cpu::IRInterpreter core;
    std::array<float, 64> fpu{};
    uint32_t tpidruro = 0;
    std::atomic<bool> stopped{ false }, breakpoint{ false };
    bool log_code = false, log_mem = false;
    uint64_t read_memory(uint32_t address, unsigned size);
    void write_memory(uint32_t address, uint64_t value, unsigned size);

public:
    IRInterpreterCPU(CPUState *state, std::size_t processor_id);
    int run() override;
    int step() override;
    void stop() override { stopped.store(true, std::memory_order_relaxed); }
    uint32_t get_reg(uint8_t idx) override { return core.state.regs.at(idx); }
    void set_reg(uint8_t idx, uint32_t val) override { core.state.regs.at(idx) = val; }
    uint32_t get_sp() override { return get_reg(13); }
    void set_sp(uint32_t val) override { set_reg(13, val); }
    uint32_t get_lr() override { return get_reg(14); }
    void set_lr(uint32_t val) override { set_reg(14, val); }
    uint32_t get_pc() override { return get_reg(15); }
    void set_pc(uint32_t val) override {
        core.state.cpsr = (core.state.cpsr & ~0x20u) | ((val & 1) << 5);
        set_reg(15, val & ((val & 1) ? ~1u : ~3u));
    }
    uint32_t get_cpsr() override { return core.state.cpsr; }
    void set_cpsr(uint32_t val) override { core.state.cpsr = val; }
    uint32_t get_fpscr() override { return core.state.fpscr; }
    void set_fpscr(uint32_t val) override { core.state.fpscr = val; }
    uint32_t get_tpidruro() override { return tpidruro; }
    void set_tpidruro(uint32_t val) override { tpidruro = val; }
    float get_float_reg(uint8_t idx) override { return fpu.at(idx); }
    void set_float_reg(uint8_t idx, float val) override { fpu.at(idx) = val; }
    CPUContext save_context() override;
    void load_context(const CPUContext &ctx) override;
    // No code cache: every step translates current guest bytes.
    void invalidate_jit_cache(Address, size_t) override {}
    bool is_thumb_mode() override { return core.state.cpsr & 0x20; }
    bool hit_breakpoint() override { return breakpoint.load(std::memory_order_relaxed); }
    void trigger_breakpoint() override {
        breakpoint.store(true, std::memory_order_relaxed);
        stop();
    }
    void set_log_code(bool val) override { log_code = val; }
    void set_log_mem(bool val) override { log_mem = val; }
    bool get_log_code() override { return log_code; }
    bool get_log_mem() override { return log_mem; }
    // Exclusive instructions are explicitly rejected by the IR interpreter.
    void clear_exclusive() override {}
    std::size_t processor_id() const override { return core_id; }
};
