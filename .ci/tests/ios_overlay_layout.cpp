#include <atomic>
#include <cassert>
#include <deque>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <vita3k_ios/OverlayLayout.h>
// INSERT_RING
int main() {
    for (const auto &screen : { std::pair{ 375.0, 667.0 }, std::pair{ 667.0, 375.0 }, std::pair{ 200.0, 120.0 } }) {
        for (double x : { -1.0, 0.0, 0.3, 1.0, 2.0 }) {
            for (double y : { -1.0, 0.0, 0.7, 1.0, 2.0 }) {
                for (bool collapsed : { false, true }) {
                    const auto r = overlay_layout::panel(screen.first, screen.second, 10, 20, 10, 20, x, y, collapsed);
                    assert(r.x >= 18 && r.y >= 28);
                    assert(r.x + r.width <= screen.first - 18);
                    assert(r.y + r.height <= screen.second - 28);
                    assert(r.width <= 300 && r.height <= (collapsed ? 32 : 116));
                }
            }
        }
    }
    assert(overlay_layout::normalized(std::numeric_limits<double>::quiet_NaN()) == 0);
    assert(overlay_layout::normalized(std::numeric_limits<double>::infinity()) == 0);
    assert(vita3k_ios_recent_log_lines(8).empty());
    for (unsigned i = 0; i < 600; ++i)
        push_recent_log_line(std::to_string(i));
    auto tail = vita3k_ios_recent_log_lines(8);
    assert(tail.size() == 8 && tail.front() == "592" && tail.back() == "599");
    assert(vita3k_ios_recent_log_lines(0).empty());
    assert(vita3k_ios_recent_log_lines(10000).size() == 500);
    std::atomic<bool> done{ false };
    std::thread producer([&] {
        for (unsigned i = 600; i < 10600; ++i)
            push_recent_log_line(std::to_string(i));
        done = true;
    });
    do {
        tail = vita3k_ios_recent_log_lines(8);
        assert(tail.size() == 8);
        for (unsigned i = 1; i < tail.size(); ++i)
            assert(std::stoi(tail[i]) == std::stoi(tail[i - 1]) + 1);
    } while (!done);
    producer.join();
    assert(vita3k_ios_recent_log_lines(1).front() == "10599");
}
