// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace util {
// A FIFO admission gate for bounded execution slices. Never hold a permit
// across guest HLE waits: another guest thread may be needed to signal them.
// A zero limit is a lock-free bypass preserving normal host scheduling.
class ExecutionGate {
    const unsigned limit;
    unsigned active = 0;
    uint64_t next_ticket = 0, serving = 0;
    std::mutex mutex;
    std::condition_variable changed;

public:
    explicit ExecutionGate(unsigned limit)
        : limit(limit) {}
    bool enabled() const { return limit != 0; }

    class Permit {
        ExecutionGate &gate;

    public:
        explicit Permit(ExecutionGate &gate)
            : gate(gate) {
            if (!gate.enabled())
                return;
            std::unique_lock lock(gate.mutex);
            const auto ticket = gate.next_ticket++;
            gate.changed.wait(lock, [&] { return ticket == gate.serving && gate.active < gate.limit; });
            ++gate.serving;
            ++gate.active;
            gate.changed.notify_all();
        }
        ~Permit() {
            if (!gate.enabled())
                return;
            std::lock_guard lock(gate.mutex);
            --gate.active;
            gate.changed.notify_all();
        }
        Permit(const Permit &) = delete;
        Permit &operator=(const Permit &) = delete;
    };
};
} // namespace util
