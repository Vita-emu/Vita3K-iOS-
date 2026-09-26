#include <cassert>
#include <chrono>
#include <cpu/impl/ir_interpreter_cpu.h>
#include <cpu/state.h>
#include <cstring>
#include <future>
#include <string>
#include <thread>

static std::string reported;
static std::atomic<unsigned> reads{ 0 };
void report_cpu_backend_error(std::string error) { reported = std::move(error); }
bool is_valid_addr(const MemState &state, Address addr) {
    ++reads;
    return addr && state.allocator.free_slot_count(addr / 4096, addr / 4096 + 1) == 0;
}
int main() {
    MemState memory;
    memory.host_page_size = 4096;
    memory.memory = Memory(new uint8_t[16384]{}, [](uint8_t *p) { delete[] p; });
    memory.allocator.set_maximum(4);
    memory.allocator.allocate_at(1, 2);
    CPUState state;
    state.mem = &memory;
    IRInterpreterCPU cpu(&state, 3);
    const auto put = [&](uint32_t address, uint32_t instruction) {
        std::memcpy(&memory.memory[address], &instruction, 4);
    };
    put(4096, 0xe3a0002a); // mov r0, #42
    put(4100, 0xe12fff1e); // bx lr
    put(4104, 0xe320f003); // wfi = emulator halt sentinel
    cpu.set_pc(4096);
    cpu.set_lr(4104);
    assert(cpu.ensure_code_cache());
    assert(cpu.run() == 1 && cpu.get_reg(0) == 42);
    assert(cpu.processor_id() == 3);
    cpu.set_tpidruro(0x1234);
    assert(cpu.get_tpidruro() == 0x1234);
    cpu.set_fpscr(0x01000000);
    cpu.set_float_reg(3, 1.5f);
    const auto saved = cpu.save_context();
    cpu.set_reg(0, 99);
    cpu.set_float_reg(3, 0);
    cpu.load_context(saved);
    assert(cpu.get_reg(0) == 42 && cpu.get_float_reg(3) == 1.5f);
    cpu.release_code_cache();
    assert(cpu.get_reg(0) == 42);
    put(4096, 0xef000007);
    cpu.set_pc(4096);
    assert(cpu.run() == 0 && state.svc_called && state.svc == 7 && cpu.get_pc() == 4100);
    put(4096, 0xe3a0000b); // modified code is immediately visible
    cpu.set_pc(4096);
    assert(cpu.step() == 0 && cpu.get_reg(0) == 11);
    put(4096, 0xee300a00); // unsupported floating point
    cpu.set_pc(4096);
    assert(cpu.step() == -1);
    assert(!state.svc_called && reported.find("Unsupported IR") != std::string::npos);
    // Bad loads are explicit errors, not fabricated zero values.
    put(4096, 0xe5910000);
    cpu.set_reg(1, 12288);
    cpu.set_pc(4096);
    assert(cpu.step() == -1 && reported.find("outside allocated") != std::string::npos);
    cpu.set_pc(4097);
    assert(cpu.is_thumb_mode() && cpu.get_pc() == 4096);
    cpu.trigger_breakpoint();
    assert(cpu.hit_breakpoint());
    // A host suspension must stop the loop without marking a guest return.
    put(4096, 0xeafffffe);
    cpu.set_pc(4096);
    reads = 0;
    auto running = std::async(std::launch::async, [&] { return cpu.run(); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (reads < 10 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    assert(reads >= 10);
    cpu.stop();
    assert(running.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
    assert(running.get() == 0);
}
