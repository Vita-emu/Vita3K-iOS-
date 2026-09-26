// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
#include <cpu/functions.h>
#include <cpu/impl/ir_interpreter_cpu.h>
#include <cpu/state.h>
#include <mem/ptr.h>
#include <stdexcept>
#include <util/log.h>

IRInterpreterCPU::IRInterpreterCPU(CPUState *state, std::size_t processor_id)
    : parent(state)
    , core_id(processor_id)
    , core(
          [this](uint32_t address, unsigned size) { return read_memory(address, size); },
          [this](uint32_t address, uint64_t value, unsigned size) { write_memory(address, value, size); }) {}

uint64_t IRInterpreterCPU::read_memory(uint32_t address, unsigned size) {
    if (address < parent->mem->host_page_size || uint64_t(address) + size > UINT32_MAX)
        throw std::runtime_error("IR interpreter read outside allocated guest memory");
    for (unsigned i = 0; i < size; ++i)
        if (!is_valid_addr(*parent->mem, address + i))
            throw std::runtime_error("IR interpreter read outside allocated guest memory");
    uint64_t value = 0;
    // Resolve each byte through the page table: an unaligned access may cross
    // separately mapped guest pages. Normal protection/readback traps still run.
    for (unsigned i = 0; i < size; ++i)
        value |= uint64_t(*Ptr<uint8_t>(address + i).get(*parent->mem)) << (i * 8);
    if (log_mem)
        LOG_TRACE("IR read {} bytes at 0x{:x}", size, address);
    return value;
}
void IRInterpreterCPU::write_memory(uint32_t address, uint64_t value, unsigned size) {
    if (address < parent->mem->host_page_size || uint64_t(address) + size > UINT32_MAX)
        throw std::runtime_error("IR interpreter write outside allocated guest memory");
    for (unsigned i = 0; i < size; ++i)
        if (!is_valid_addr(*parent->mem, address + i))
            throw std::runtime_error("IR interpreter write outside allocated guest memory");
    for (unsigned i = 0; i < size; ++i)
        *Ptr<uint8_t>(address + i).get(*parent->mem) = static_cast<uint8_t>(value >> (i * 8));
    if (log_mem)
        LOG_TRACE("IR write {} bytes at 0x{:x}", size, address);
}
int IRInterpreterCPU::step() {
    parent->svc_called = false;
    if (log_code)
        LOG_TRACE("IR step at PC 0x{:x}", get_pc());
    if (!core.step()) {
        const auto error = fmt::format("IR Interpreter stopped at PC 0x{:x}: {}. Select Dynarmic JIT in Settings > CPU & Execution, then restart Tsubomi for general games.", get_pc(), core.error());
        LOG_ERROR("{}", error);
        report_cpu_backend_error(error);
        return -1;
    }
    parent->svc_called = core.state.svc_called;
    parent->svc = core.state.svc;
    if (core.state.breakpoint)
        trigger_breakpoint();
    return core.state.halted ? 1 : 0;
}
int IRInterpreterCPU::run() {
    stopped.store(false, std::memory_order_relaxed);
    breakpoint.store(false, std::memory_order_relaxed);
    parent->svc_called = false;
    while (!stopped.load(std::memory_order_relaxed)) {
        const int result = step();
        if (result != 0)
            return result;
        if (parent->svc_called)
            return 0;
    }
    return 0;
}
CPUContext IRInterpreterCPU::save_context() {
    CPUContext ctx;
    ctx.cpu_registers = core.state.regs;
    ctx.fpu_registers = fpu;
    ctx.cpsr = core.state.cpsr;
    ctx.fpscr = core.state.fpscr;
    return ctx;
}
void IRInterpreterCPU::load_context(const CPUContext &ctx) {
    core.state.regs = ctx.cpu_registers;
    fpu = ctx.fpu_registers;
    core.state.cpsr = ctx.cpsr;
    core.state.fpscr = ctx.fpscr;
}
