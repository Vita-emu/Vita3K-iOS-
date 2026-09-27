#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iterator>
#include <string>
#include <vector>
#define LOG_INFO(...) ((void)0)
using Uint64 = uint64_t;
static Uint64 now = 0, end = 0;
static std::atomic_bool stop_guest_watchdog{ false };
struct Environment {
    struct {
        std::atomic<uint64_t> last_setframe_vblank_count{ 0 };
    } display;
} environment;
static auto *emuenv = &environment;
static std::function<bool(Uint64)> presenting;
static Uint64 SDL_GetTicks() { return now; }
static void advance_clock(std::chrono::milliseconds interval) {
    now += interval.count();
    if (presenting(now))
        ++emuenv->display.last_setframe_vblank_count;
    if (now >= end)
        stop_guest_watchdog = true;
}
namespace app {
static std::vector<std::pair<Uint64, std::string>> dumps;
static void dump_guest_state(Environment &, const char *reason) {
    dumps.emplace_back(now, reason);
}
} // namespace app
static void run() {
    // INSERT_WATCHDOG
}
static void reset(Uint64 duration, std::function<bool(Uint64)> frames) {
    now = 0;
    end = duration;
    stop_guest_watchdog = false;
    emuenv->display.last_setframe_vblank_count = 0;
    presenting = frames;
    app::dumps.clear();
}
int main() {
    // Healthy gameplay consumes all scheduled slots and retires at 180s,
    // without inspecting guest registers or locking the kernel for a dump.
    reset(200000, [](Uint64) { return true; });
    run();
    assert(app::dumps.empty());
    assert(now == 180000 && !stop_guest_watchdog);

    // Before the first frame, retain early boot snapshots and the 8s alarm.
    reset(12000, [](Uint64) { return false; });
    run();
    assert(app::dumps.size() == 4);
    assert(app::dumps[0].first == 2000);
    assert(app::dumps[1].first == 3000);
    assert(app::dumps[2].first == 8000);
    assert(app::dumps[2].second == "no sceDisplaySetFrameBuf progress for 8s");
    assert(app::dumps[3].first == 10000);

    // Starting successfully must not suppress a later boot stall. When
    // frames resume, healthy scheduled slots are skipped again.
    reset(200000, [](Uint64 tick) { return tick <= 1000 || tick >= 20000; });
    run();
    assert(app::dumps.size() == 3);
    assert(app::dumps[0].first == 3000);
    assert(app::dumps[1].first == 9000);
    assert(app::dumps[1].second == "no sceDisplaySetFrameBuf progress for 8s");
    assert(app::dumps[2].first == 10000);
    assert(now == 180000);
}
