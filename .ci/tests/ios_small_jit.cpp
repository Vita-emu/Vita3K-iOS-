#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <dynarmic/backend/arm64/a32_jitstate.h>
#include <dynarmic/backend/arm64/abi.h>
#include <dynarmic/backend/arm64/devirtualize.h>
#include <dynarmic/backend/arm64/stack_layout.h>
#include <dynarmic/common/cast_util.h>
#include <dynarmic/interface/A32/config.h>
#include <dynarmic/interface/exclusive_monitor.h>
#include <dynarmic/interface/halt_reason.h>
#include <set>
#include <vector>

// Callbacks/locks are embedded as addresses, never executed by this host test.
namespace Dynarmic {
void SpinLock::Lock() { std::abort(); }
void SpinLock::Unlock() { std::abort(); }
} // namespace Dynarmic
struct Callbacks : Dynarmic::A32::UserCallbacks {
    uint8_t MemoryRead8(uint32_t) override { std::abort(); }
    uint16_t MemoryRead16(uint32_t) override { std::abort(); }
    uint32_t MemoryRead32(uint32_t) override { std::abort(); }
    uint64_t MemoryRead64(uint32_t) override { std::abort(); }
    void MemoryWrite8(uint32_t, uint8_t) override { std::abort(); }
    void MemoryWrite16(uint32_t, uint16_t) override { std::abort(); }
    void MemoryWrite32(uint32_t, uint32_t) override { std::abort(); }
    void MemoryWrite64(uint32_t, uint64_t) override { std::abort(); }
    void InterpreterFallback(uint32_t, size_t) override { std::abort(); }
    void CallSVC(uint32_t) override { std::abort(); }
    void ExceptionRaised(uint32_t, Dynarmic::A32::Exception) override { std::abort(); }
    void AddTicks(uint64_t) override { std::abort(); }
    uint64_t GetTicksRemaining() override { std::abort(); }
};
namespace Dynarmic::Backend::Arm64 {
using CodePtr = std::byte *;
struct MemoryAdapter {
    void invalidate_all() {}
};
struct Ranges {
    std::set<int> entries;
    void ClearCache() { entries.clear(); }
};
struct AddressSpace {
    explicit AddressSpace(size_t bytes)
        : words(bytes / 4 + 16, 0xDEADBEEF)
        , code(words.data(), words.data())
        , capacity(bytes) {}
    virtual ~AddressSpace() = default;
    virtual void ClearCache();
    size_t GetRemainingSize() {
        assert(static_cast<size_t>(code.offset()) <= capacity);
        return capacity - code.offset();
    }
    void ProtectCodeMemory() {}
    void UnprotectCodeMemory() {}
    std::vector<uint32_t> words;
    oaknut::CodeGenerator code;
    size_t capacity;
    MemoryAdapter mem;
    std::set<int> block_entries, reverse_block_entries, block_infos, block_references;
    // INSERT_PRELUDE_INFO
};
struct A32AddressSpace : AddressSpace {
    A32AddressSpace(size_t bytes, A32::UserConfig config)
        : AddressSpace(bytes)
        , conf(config) {}
    void EmitPrelude();
    void ClearCache() override;
    CodePtr GetOrEmit(IR::LocationDescriptor) { std::abort(); }
    A32::UserConfig conf;
    Ranges block_ranges;
};
struct A64AddressSpace : AddressSpace {
    explicit A64AddressSpace(size_t bytes)
        : AddressSpace(bytes) {}
    void ClearCache() override;
    Ranges block_ranges;
};
// INSERT_TRAMPOLINES
// INSERT_PRELUDE
// INSERT_CLEAR_BASE
// INSERT_CLEAR_A32
// INSERT_CLEAR_A64
} // namespace Dynarmic::Backend::Arm64
int main() {
    using namespace Dynarmic;
    using namespace Dynarmic::Backend::Arm64;
    constexpr size_t mib = 1024 * 1024;
    Callbacks callbacks;
    for (size_t bytes : { 4 * mib, 8 * mib, 12 * mib }) {
        for (bool optimized : { false, true }) {
            A32::UserConfig config;
            config.callbacks = &callbacks;
            config.optimizations = optimized ? all_safe_optimizations : no_optimizations;
            config.enable_cycle_counting = optimized;
            config.fastmem_pointer = optimized ? std::optional<uintptr_t>{ 0x123456780000 } : std::nullopt;
            A32AddressSpace space(bytes, config);
            space.EmitPrelude();
            assert(space.prelude_info.end_of_prelude > 0);
            assert(space.GetRemainingSize() >= mib);
            const auto prelude = std::vector<uint32_t>(space.words.begin(), space.words.begin() + space.code.offset() / 4);
            std::printf("ARM64 cache %zu MiB: prelude %td bytes, free %zu bytes\n", bytes / mib, space.prelude_info.end_of_prelude, space.GetRemainingSize());
            for (int round = 0; round < 100; ++round) {
                for (int block = 0; block < 500; ++block) {
                    space.block_entries.insert(round * 500 + block);
                    space.reverse_block_entries.insert(block);
                    space.block_infos.insert(block);
                    space.block_references.insert(block);
                    space.block_ranges.entries.insert(round * 500 + block);
                }
                // The emitter calls through the base class when the cache fills.
                space.code.set_offset(bytes - mib / 2);
                AddressSpace &base = space;
                base.ClearCache();
                assert(space.block_ranges.entries.empty());
                assert(space.block_entries.empty() && space.reverse_block_entries.empty());
                assert(space.block_infos.empty() && space.block_references.empty());
                assert(space.code.offset() == space.prelude_info.end_of_prelude);
                assert(std::equal(prelude.begin(), prelude.end(), space.words.begin()));
            }
            for (size_t i = bytes / 4; i < space.words.size(); ++i)
                assert(space.words[i] == 0xDEADBEEF);
        }
    }
    A64AddressSpace a64(4 * mib);
    a64.prelude_info.end_of_prelude = 256;
    a64.block_ranges.entries.insert(1);
    static_cast<AddressSpace &>(a64).ClearCache();
    assert(a64.block_ranges.entries.empty() && a64.code.offset() == 256);
}
