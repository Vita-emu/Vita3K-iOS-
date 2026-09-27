#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <thread>
#include <util/execution_gate.h>
#include <util/ios_runtime_tuning.h>
#include <vector>
#define LOG_INFO(...) ((void)0)
#define LOG_CRITICAL(...) ((void)0)
// INSERT_GATE
struct TickBase {
    virtual uint64_t GetTicksRemaining() = 0;
};
struct Callbacks : TickBase {
    // INSERT_TICKS
};
namespace Dynarmic {
enum class HaltReason { None,
    Step,
    CacheInvalidation };
}
struct FakeJit {
    std::function<Dynarmic::HaltReason()> execute;
    auto Run() { return execute(); }
    void Step() { execute(); }
};
struct Parent {
    int thread_id = 1;
    bool svc_called = false;
};
struct DynarmicCPU {
    Parent data;
    Parent *parent = &data;
    bool halted = false, break_ = false, fail_init = false;
    std::unique_ptr<FakeJit> jit = std::make_unique<FakeJit>();
    void ensure_jit() {
        if (fail_init)
            throw std::runtime_error("allocation failed");
    }
    int run();
    int step();
};
// INSERT_RUN
int main(int argc, char **argv) {
    assert(argc == 2);
    ios_runtime::tuning.cpu_execution_threads = std::stoi(argv[1]);
    const int limit = ios_runtime::cpu_execution_threads(ios_runtime::tuning.cpu_execution_threads,
        static_cast<int>(std::thread::hardware_concurrency()));
    assert(ios_runtime::cpu_execution_threads(-1, 6) == 0);
    assert(ios_runtime::cpu_execution_threads(9, 6) == 0);
    assert(ios_runtime::cpu_execution_threads(8, 6) == 6);
    assert(ios_runtime::cpu_execution_threads(4, 0) == 1);
    struct {
        bool enable_cycle_counting;
    } config;
    // INSERT_CYCLE_CONFIG
    assert(config.enable_cycle_counting == (limit != 0));
    Callbacks callbacks;
    assert(callbacks.GetTicksRemaining() == (limit ? 10000 : 1ull << 60));

    std::atomic<int> active{ 0 }, peak{ 0 }, completed{ 0 };
    std::vector<std::future<void>> jobs;
    // Attack on Titan creates many guest workers. Exercise a much larger
    // waiting queue than the admitted set, including direct slot handoffs.
    constexpr int workers = 32;
    for (int worker = 0; worker < workers; ++worker) {
        jobs.push_back(std::async(std::launch::async, [&] {
            DynarmicCPU cpu;
            cpu.jit->execute = [&] {
                const int running = ++active;
                int old = peak.load();
                while (old < running && !peak.compare_exchange_weak(old, running)) {
                }
                if (limit)
                    assert(running <= limit);
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                --active;
                return Dynarmic::HaltReason::None;
            };
            for (int slice = 0; slice < 100; ++slice) {
                assert(cpu.run() == 0);
                assert(cpu.step() == 0);
            }
            ++completed;
        }));
    }
    for (auto &job : jobs)
        job.get();
    assert(completed == workers && active == 0 && peak > 0);

    // A failed runner must not leak an admission slot, including single-step.
    DynarmicCPU cpu;
    cpu.jit->execute = []() -> Dynarmic::HaltReason { throw std::runtime_error("runner failed"); };
    for (bool step : { false, true }) {
        try {
            step ? cpu.step() : cpu.run();
            assert(false);
        } catch (const std::runtime_error &) {
        }
    }
    int runs = 0;
    cpu.jit->execute = [&] {
        ++runs;
        if (runs == 1)
            return Dynarmic::HaltReason::CacheInvalidation;
        if (runs == 2)
            return Dynarmic::HaltReason::Step;
        cpu.parent->svc_called = true;
        return Dynarmic::HaltReason::None;
    };
    assert(cpu.run() == 0 && runs == 3 && cpu.parent->svc_called);
    // HLE runs after run() returns and can wait for another admitted thread.
    auto signal = std::async(std::launch::async, [] {
        util::ExecutionGate::Permit permit(ios_cpu_execution_gate());
    });
    assert(signal.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    signal.get();
    cpu.fail_init = true;
    assert(cpu.run() == -1 && cpu.step() == -1);
}
