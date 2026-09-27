#include <cassert>
#include <thread>
#include <util/render_diagnostics.h>
#include <vector>
int main() {
    using namespace render_diagnostics;
    Reporter reporter(100);
    Snapshot snapshot;
    add(Draws);
    { CompileTimer disabled_timer; }
    assert(!take_shader_sample());
    assert(!reporter.poll(10000, snapshot));
    assert(counters[Draws] == 0 && counters[PipelineCompiles] == 0);
    mode = 1;
    assert(!take_shader_sample());
    mode = 2;
    std::atomic<unsigned> samples{ 0 };
    std::vector<std::thread> workers;
    for (unsigned i = 0; i < 8; ++i) {
        workers.emplace_back([&] {
            for (unsigned n = 0; n < 10000; ++n) {
                add(Draws);
                if (take_shader_sample())
                    ++samples;
            }
        });
    }
    for (auto &worker : workers)
        worker.join();
    assert(samples == 4);
    assert(!reporter.poll(5099, snapshot));
    assert(reporter.poll(5100, snapshot));
    assert(snapshot.interval_ms == 5000 && snapshot.values[Draws] == 80000);
    assert(counters[Draws] == 0);
    assert(!reporter.poll(5100, snapshot));
    assert(!reporter.poll(100, snapshot));
    for (unsigned i = 0; i < 4; ++i)
        assert(take_shader_sample());
    assert(!take_shader_sample());
    { CompileTimer timer; }
    assert(reporter.poll(10100, snapshot));
    assert(snapshot.values[Draws] == 0 && snapshot.values[PipelineCompiles] == 1);
    // Draining while producers run must not drop or double count increments.
    std::thread producer([] { for (unsigned i = 0; i < 100000; ++i) add(TextureUploads); });
    uint64_t total = 0;
    uint64_t now = 10100;
    for (unsigned i = 0; i < 10; ++i) {
        assert(reporter.poll(now += 5000, snapshot));
        total += snapshot.values[TextureUploads];
    }
    producer.join();
    assert(reporter.poll(now + 5000, snapshot));
    total += snapshot.values[TextureUploads];
    assert(total == 100000);
}
