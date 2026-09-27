#include <cassert>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <type_traits>
#define EXPORT(ret, name, ...) ret name(__VA_ARGS__)
#define TRACY_FUNC(...) ((void)0)
#define RET_ERROR(value) static_cast<int>(value)
#define LOG_INFO(...) ((void)0)
using SceUID = int;
template <class T>
struct Ptr {
    T *value = nullptr;
    T *get(int) const { return value; }
    explicit operator bool() const { return value != nullptr; }
    template <class U>
    Ptr<U> cast() const { return { reinterpret_cast<U *>(value) }; }
};
// INSERT_DECLARATIONS
struct H264DecoderState {
    bool is_stopped = true, planar = false;
    unsigned remaining = 0, pts = 0, flushes = 0, drains = 0;
    uint32_t width = 0, height = 0;
    void set_output_format(bool p) { planar = p; }
    void set_res(uint32_t w, uint32_t h) {
        width = w;
        height = h;
    }
    bool drain(uint8_t *output) {
        ++drains;
        if (!remaining)
            return false;
        --remaining;
        *output = ++pts;
        return true;
    }
    void flush() {
        ++flushes;
        remaining = 0;
    }
    void get_res(uint32_t &w, uint32_t &h) {
        w = width;
        h = height;
    }
    void get_pts(uint32_t &upper, uint32_t &lower) {
        upper = 0;
        lower = pts;
    }
};
using H264DecoderPtr = std::shared_ptr<H264DecoderState>;
struct VideodecState {
    std::mutex mutex;
    std::map<int, H264DecoderPtr> decoders;
} state;
struct {
    int mem = 0;
    struct {
        struct {
            template <class T>
            T *get() { return &state; }
        } obj_store;
    } kernel;
} emuenv;
static H264DecoderPtr lock_and_find(int handle, const std::map<int, H264DecoderPtr> &decoders, std::mutex &) {
    auto found = decoders.find(handle);
    return found == decoders.end() ? nullptr : found->second;
}
// INSERT_STOP
int main() {
    auto codec = std::make_shared<H264DecoderState>();
    state.decoders[1] = codec;
    SceAvcdecCtrl ctrl{};
    ctrl.handle = 1;
    uint8_t pixels[2]{};
    SceAvcdecPicture outputs[2]{};
    Ptr<SceAvcdecPicture> pointers[2]{ { &outputs[0] }, { &outputs[1] } };
    for (unsigned i = 0; i < 2; ++i) {
        outputs[i].frame.pixelType = SCE_AVCDEC_PIXEL_YUV420_PACKED_RASTER;
        outputs[i].frame.frameWidth = 960;
        outputs[i].frame.frameHeight = 544;
        outputs[i].frame.pPicture[0] = { &pixels[i] };
    }
    SceAvcdecArrayPicture picture{ 99, 2, { pointers } };
    assert(sceAvcdecDecodeStop(&ctrl, &picture) == 0 && picture.numOfOutput == 0);
    assert(codec->drains == 0);
    codec->is_stopped = false;
    codec->remaining = 3;
    assert(sceAvcdecDecodeStop(&ctrl, &picture) == 0 && picture.numOfOutput == 2);
    assert(pixels[0] == 1 && pixels[1] == 2 && !codec->is_stopped && codec->flushes == 0);
    assert(outputs[1].info.pts.lower == 2 && outputs[1].frame.horizontalSize == 960);
    assert(sceAvcdecDecodeStop(&ctrl, &picture) == 0 && picture.numOfOutput == 1);
    assert(pixels[0] == 3 && pixels[1] == 2 && codec->is_stopped && codec->flushes == 1);
    assert(outputs[0].info.pts.lower == 3);
    const auto calls = codec->drains;
    assert(sceAvcdecDecodeStop(&ctrl, &picture) == 0 && picture.numOfOutput == 0 && codec->drains == calls);
    // A new stream and capacity of one must also drain across repeated calls.
    codec->is_stopped = false;
    codec->remaining = 2;
    picture.numOfElm = 1;
    outputs[0].frame.pixelType = SCE_AVCDEC_PIXEL_YUV420_RASTER;
    for (unsigned pts : { 4U, 5U }) {
        assert(sceAvcdecDecodeStop(&ctrl, &picture) == 0 && picture.numOfOutput == 1);
        assert(pixels[0] == pts && outputs[0].info.pts.lower == pts && codec->planar);
    }
    assert(sceAvcdecDecodeStop(&ctrl, &picture) == 0 && picture.numOfOutput == 0 && codec->is_stopped);
    assert(codec->flushes == 2);
    codec->is_stopped = false;
    picture.numOfElm = 0;
    assert(sceAvcdecDecodeStop(&ctrl, &picture) != 0 && !codec->is_stopped);
    assert(sceAvcdecDecodeStop(nullptr, &picture) != 0);
    assert(sceAvcdecDecodeStop(&ctrl, nullptr) != 0);
    ctrl.handle = 99;
    assert(sceAvcdecDecodeStop(&ctrl, &picture) != 0);
}
