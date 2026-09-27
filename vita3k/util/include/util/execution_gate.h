// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
#pragma once

#include <condition_variable>
#include <mutex>

namespace util {
// A FIFO admission gate for bounded execution slices. Never hold a permit
// across guest HLE waits: another guest thread may be needed to signal them.
// A zero limit is a lock-free bypass preserving normal host scheduling.
class ExecutionGate {
    const unsigned limit;
    unsigned active = 0;
    std::mutex mutex;
    // Each blocked caller owns its waiter on its stack. A released slot is
    // handed directly to the oldest waiter, without waking all guest threads
    // at every JIT slice (especially costly with many game worker threads).
    struct Waiter {
        std::condition_variable ready;
        Waiter *next = nullptr;
        bool admitted = false;
    };
    Waiter *first = nullptr;
    Waiter *last = nullptr;

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
            if (gate.active < gate.limit && !gate.first) {
                ++gate.active;
                return;
            }
            Waiter waiter;
            if (gate.last)
                gate.last->next = &waiter;
            else
                gate.first = &waiter;
            gate.last = &waiter;
            waiter.ready.wait(lock, [&] { return waiter.admitted; });
        }
        ~Permit() {
            if (!gate.enabled())
                return;
            std::lock_guard lock(gate.mutex);
            if (gate.first) {
                Waiter *waiter = gate.first;
                gate.first = waiter->next;
                if (!gate.first)
                    gate.last = nullptr;
                // Keep this slot reserved: a new arrival cannot steal it
                // while the selected thread is waking up.
                waiter->admitted = true;
                waiter->ready.notify_one();
            } else {
                --gate.active;
            }
        }
        Permit(const Permit &) = delete;
        Permit &operator=(const Permit &) = delete;
    };
};
} // namespace util
