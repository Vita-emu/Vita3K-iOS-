// Production lifecycle functions are injected by test_avplayer_lifecycle.py.
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <vector>

using SceUID = int;
using Address = uint32_t;
constexpr uint32_t KiB(uint32_t size) { return size * 1024; }
struct MemState {
    std::map<Address, uint32_t> allocations;
    std::unique_ptr<uint8_t[]> memory = std::make_unique<uint8_t[]>(KiB(256));
    bool use_page_table = false;
    std::vector<uint8_t *> page_table;
    Address next = 4;
    int remaining = -1;
};
template <typename T>
bool atomic_compare_and_swap(volatile T *, T, T);
static bool is_valid_addr(const MemState &mem, Address address) {
    return mem.allocations.count(address) != 0;
}
// INSERT_PTR
static Address alloc(MemState &mem, uint32_t size, const char *) {
    if (mem.remaining == 0)
        return 0;
    if (mem.remaining > 0)
        --mem.remaining;
    const Address address = mem.next;
    mem.next += size;
    assert(mem.next <= KiB(256));
    mem.allocations.emplace(address, size);
    return address;
}
static void free(MemState &mem, Ptr<uint8_t> ptr) {
    assert(ptr.valid(mem));
    assert(mem.allocations.erase(ptr.address()) == 1);
}
namespace fmt {
template <typename... Args>
std::string format(const char *, Args...) { return "fixture"; }
} // namespace fmt
struct DecoderSize {
    uint32_t width = 2, height = 2;
};
struct AVRational {
    int num, den;
};
struct AVStream {
    AVRational avg_frame_rate{ 30, 1 };
};
struct AVFormatContext {
    unsigned nb_streams = 1;
    AVStream **streams;
};
struct PlayerState {
    AVStream stream;
    AVStream *streams[1] = { &stream };
    AVFormatContext context{ 1, streams };
    AVFormatContext *format = &context;
    void *video_context = this;
    int video_stream_id = 0;
    std::string video_playing = "trailer";
    uint64_t last_timestamp = 0;
    uint32_t last_channels = 2, last_sample_rate = 48000, last_sample_count = 2;
    DecoderSize dimensions;
    std::function<void()> on_decode;
    bool eof = false;
    uint64_t get_framerate_microseconds();
    DecoderSize get_size() { return dimensions; }
    void free_video() {
        video_playing.clear();
        format = nullptr;
        video_context = nullptr;
    }
    std::vector<uint8_t> receive_video() {
        assert(format);
        if (on_decode)
            on_decode();
        assert(format); // Stop/Close must wait for decoding to finish.
        return eof ? std::vector<uint8_t>{} : std::vector<uint8_t>(dimensions.width * dimensions.height * 3 / 2, 42);
    }
    std::vector<int16_t> receive_audio() {
        if (video_playing.empty())
            return {};
        assert(format);
        return { 1, 2, 3, 4 };
    }
};
struct H264DecoderState {
    static uint32_t buffer_size(DecoderSize size) { return size.width * size.height * 3 / 2; }
};
// INSERT_FRAMERATE
// INSERT_DECLARATIONS
struct Store {
    std::shared_ptr<AvPlayerState> state = std::make_shared<AvPlayerState>();
    template <typename T>
    std::shared_ptr<T> get() { return state; }
};
struct Kernel {
    Store obj_store;
    int get_thread(int) { return 1; }
};
struct EmuEnvState {
    Kernel kernel;
    MemState mem;
};
template <typename Key, typename Map>
auto lock_and_find(Key key, Map &map, std::mutex &mutex) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = map.find(key);
    return it == map.end() ? typename Map::mapped_type{} : it->second;
}
static std::function<void(int)> callback;
static void run_event_callback(EmuEnvState &, int, const PlayerPtr &, int event, int, Ptr<void>) {
    if (callback)
        callback(event);
}
#define EXPORT(result, name, ...) result name(EmuEnvState &emuenv, int thread_id, __VA_ARGS__)
#define RET_ERROR(value) static_cast<int32_t>(value)
constexpr bool REJECT_DATA_ON_PAUSE = true;
constexpr bool CATCHUP_VIDEO_PLAYBACK = true;
// INSERT_FUNCTIONS

int main() {
    EmuEnvState env;
    auto add = [&] {
        auto player = std::make_shared<PlayerInfoState>();
        env.kernel.obj_store.state->players[1] = player;
        return player;
    };
    SceAvPlayerFrameInfo frame{};
    auto player = add();
    assert(player->player.get_framerate_microseconds() == 33333);
    player->player.stream.avg_frame_rate.num = 0;
    assert(player->player.get_framerate_microseconds() == 0);
    player->player.stream.avg_frame_rate = { 30, 0 };
    assert(player->player.get_framerate_microseconds() == 0);
    player->player.stream.avg_frame_rate = { 30, 1 };
    player->player.video_stream_id = 1;
    assert(player->player.get_framerate_microseconds() == 0);
    player->player.video_stream_id = 0;
    assert(sceAvPlayerGetVideoData(env, 0, 1, &frame));
    assert(sceAvPlayerGetAudioData(env, 0, 1, &frame));
    assert(env.mem.allocations.size() == 8);
    int events = 0;
    callback = [&](int event) {
        assert(event == SCE_AVPLAYER_STATE_STOP);
        ++events;
        assert(sceAvPlayerStop(env, 0, 1) == 0);
    };
    assert(sceAvPlayerStop(env, 0, 1) == 0);
    assert(events == 1);
    assert(player->player.get_framerate_microseconds() == 0);
    assert(!sceAvPlayerGetVideoData(env, 0, 1, &frame));
    assert(!sceAvPlayerGetAudioData(env, 0, 1, &frame));
    callback = [&](int) {
        ++events;
        assert(sceAvPlayerClose(env, 0, 1) != 0);
        assert(!sceAvPlayerGetVideoData(env, 0, 1, &frame));
    };
    assert(sceAvPlayerClose(env, 0, 1) == 0);
    assert(events == 2 && env.mem.allocations.empty());
    for (auto buffer : player->video_buffer)
        assert(!buffer);
    for (auto buffer : player->audio_buffer)
        assert(!buffer);
    callback = {};
    assert(sceAvPlayerStop(env, 0, 99) != 0);
    assert(!sceAvPlayerGetAudioData(env, 0, 99, &frame));

    player = add();
    assert(!sceAvPlayerGetVideoData(env, 0, 1, nullptr));
    player->player.eof = true;
    assert(!sceAvPlayerGetVideoData(env, 0, 1, &frame));
    assert(env.mem.allocations.empty());
    player->player.eof = false;
    assert(sceAvPlayerGetVideoData(env, 0, 1, &frame));
    const auto old_buffer = player->video_buffer[0].address();
    env.mem.remaining = 2;
    player->player.on_decode = [&] { player->player.dimensions = { 64, 64 }; };
    assert(!sceAvPlayerGetVideoData(env, 0, 1, &frame));
    assert(env.mem.allocations.size() == 4 && player->video_buffer[0].address() == old_buffer);
    env.mem.remaining = -1;
    assert(sceAvPlayerGetVideoData(env, 0, 1, &frame));
    assert(frame.stream_details.video.width == 64 && frame.data.get(env.mem)[6143] == 42);
    int play_events = 0;
    callback = [&](int event) { if (event == SCE_AVPLAYER_STATE_PLAY) ++play_events; };
    assert(sceAvPlayerPause(env, 0, 1) == 0);
    assert(sceAvPlayerResume(env, 0, 1) == 0);
    assert(sceAvPlayerResume(env, 0, 1) == 0 && play_events == 1);
    callback = {};
    assert(sceAvPlayerClose(env, 0, 1) == 0);

    for (bool close : { false, true }) {
        player = add();
        std::promise<void> entered, release, stopping;
        auto released = release.get_future();
        player->player.on_decode = [&] { entered.set_value(); released.wait(); };
        auto decode = std::async(std::launch::async, [&] { return sceAvPlayerGetVideoData(env, 0, 1, &frame); });
        entered.get_future().wait();
        auto stop = std::async(std::launch::async, [&] {
            stopping.set_value();
            return close ? sceAvPlayerClose(env, 0, 1) : sceAvPlayerStop(env, 0, 1);
        });
        stopping.get_future().wait();
        assert(stop.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout);
        release.set_value();
        assert(decode.get() && stop.get() == 0);
        assert(!sceAvPlayerGetVideoData(env, 0, 1, &frame));
        if (!close)
            assert(sceAvPlayerClose(env, 0, 1) == 0);
        assert(env.mem.allocations.empty());
    }
}
