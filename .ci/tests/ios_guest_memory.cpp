#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <mem/allocator.h>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

using Address = uint32_t;
constexpr uint32_t KiB(uint32_t size) { return size * 1024; }
constexpr uint32_t STANDARD_PAGE_SIZE = KiB(4);
constexpr bool PAGE_NAME_TRACKING = true;
constexpr uint32_t arena_size = KiB(256);
constexpr int PROT_NONE = 0, PROT_READ = 1, PROT_WRITE = 2;
#ifdef VITA3K_PLATFORM_IOS
constexpr int MADV_FREE = 10;
#else
constexpr int MADV_DONTNEED = 11;
#endif
#define LOG_CRITICAL_IF(condition, ...) assert(!(condition))
#define LOG_CRITICAL(...) assert(false)
static Address align_down(Address address, uint32_t alignment) { return address & ~(alignment - 1); }
static Address align(Address address, uint32_t alignment) { return align_down(address + alignment - 1, alignment); }
struct AllocMemPage {
    uint32_t allocated = 0, size = 0;
};
struct MemState {
    std::mutex generation_mutex;
    uint64_t guest_bytes_used = 0, guest_bytes_limit = 0;
    uint32_t host_page_size;
    std::unique_ptr<uint8_t[]> memory = std::make_unique<uint8_t[]>(arena_size);
    std::vector<AllocMemPage> alloc_table = std::vector<AllocMemPage>(arena_size / STANDARD_PAGE_SIZE);
    BitmapAllocator allocator{ arena_size / STANDARD_PAGE_SIZE };
    std::map<int, std::string> page_name_map;
    bool use_page_table = false;
    std::vector<uint8_t *> page_table;
};
struct Range {
    uint8_t *address;
    uint32_t size;
    int protection;
};
static std::vector<Range> protections, released;
static bool fail_commit = false;
static int mprotect(void *address, uint32_t size, int protection) {
    if (fail_commit && protection == (PROT_READ | PROT_WRITE))
        return -1;
    protections.push_back({ static_cast<uint8_t *>(address), size, protection });
    return 0;
}
static int madvise(void *address, uint32_t size, int advice) {
#ifdef VITA3K_PLATFORM_IOS
    assert(advice == MADV_FREE);
#else
    assert(advice == MADV_DONTNEED);
#endif
    const auto &last = protections.back();
    assert(last.address == address && last.size == size && last.protection == PROT_NONE);
    released.push_back(last);
    // Model discarded contents, not actual Darwin kernel memory accounting.
    std::memset(address, 0xDD, size);
    return 0;
}
Address alloc(MemState &, uint32_t, const char *, Address);
// INSERT_ALLOCATE
// INSERT_ALLOC_WRAPPERS
// INSERT_FREE
static Address allocate(MemState &state, uint32_t start, uint32_t count) {
    const auto address = alloc_inner(state, start, count, "test", true);
    assert(address == start * STANDARD_PAGE_SIZE);
    for (uint32_t i = 0; i < count * STANDARD_PAGE_SIZE; ++i)
        assert(state.memory[address + i] == 0);
    std::memset(&state.memory[address], 0xAB, count * STANDARD_PAGE_SIZE);
    return address;
}
static void assert_released(MemState &state, uint32_t start, uint32_t size) {
    assert(released.size() == 1);
    assert(released[0].address == &state.memory[start] && released[0].size == size);
    assert(start % state.host_page_size == 0 && size % state.host_page_size == 0);
    released.clear();
}
int main() {
    // A small budget exercises the same byte accounting as the 768 MiB limit
    // without physically allocating hundreds of MiB in the test process.
    {
        MemState bounded;
        bounded.host_page_size = KiB(16);
        bounded.allocator.allocate_at(0, 1); // init() reserves the null page.
        bounded.guest_bytes_limit = 3 * STANDARD_PAGE_SIZE;
        const auto a = alloc(bounded, 1, "budget", STANDARD_PAGE_SIZE);
        const auto b = alloc(bounded, STANDARD_PAGE_SIZE + 1, "budget", STANDARD_PAGE_SIZE);
        assert(a && b && bounded.guest_bytes_used == bounded.guest_bytes_limit);
        assert(alloc(bounded, 1, "full", STANDARD_PAGE_SIZE) == 0);
        assert(try_alloc_at(bounded, 8 * STANDARD_PAGE_SIZE, 1, "fixed full") == 0);
        assert(alloc_aligned(bounded, 1, "aligned full", STANDARD_PAGE_SIZE, STANDARD_PAGE_SIZE) == 0);
        free(bounded, a);
        assert(bounded.guest_bytes_used == 2 * STANDARD_PAGE_SIZE);
        assert(alloc(bounded, 1, "reuse", STANDARD_PAGE_SIZE) == a);
        free(bounded, a);
        free(bounded, b);
        assert(bounded.guest_bytes_used == 0);
        fail_commit = true;
        assert(try_alloc_at(bounded, 4 * STANDARD_PAGE_SIZE, 1, "host failure") == 0);
        assert(bounded.guest_bytes_used == 0);
        fail_commit = false;
        const auto fixed = try_alloc_at(bounded, 4 * STANDARD_PAGE_SIZE, 1, "rollback reused");
        assert(fixed);
        free(bounded, fixed);
        const auto aligned = alloc_aligned(bounded, 1, "aligned", 2 * STANDARD_PAGE_SIZE, STANDARD_PAGE_SIZE);
        assert(aligned == 2 * STANDARD_PAGE_SIZE);
        assert(bounded.guest_bytes_used == 2 * STANDARD_PAGE_SIZE);
        free(bounded, aligned);
        assert(bounded.guest_bytes_used == 0);
        assert(alloc(bounded, UINT32_MAX, "overflow", STANDARD_PAGE_SIZE) == 0);
        assert(alloc_aligned(bounded, UINT32_MAX, "overflow", STANDARD_PAGE_SIZE, STANDARD_PAGE_SIZE) == 0);
        assert(try_alloc_at(bounded, STANDARD_PAGE_SIZE + 1, UINT32_MAX, "overflow") == 0);
        assert(alloc(bounded, 0, "zero", STANDARD_PAGE_SIZE) == 0);
        protections.clear();
        released.clear();
    }
    MemState state;
    state.host_page_size = KiB(16);
    // Two guest allocations share one host page. Neither live contents nor
    // protection may be discarded when just the first guest allocation dies.
    const auto first = allocate(state, 1, 1);
    const auto neighbor = allocate(state, 2, 1);
    protections.clear();
    free(state, first);
    assert(released.empty() && protections.empty());
    assert(state.memory[neighbor] == 0xAB);
    // Reallocating the freed guest subpage must zero it, keeping its neighbor.
    allocate(state, 1, 1);
    assert(state.memory[neighbor] == 0xAB);
    free(state, first);
    free(state, neighbor);
    assert_released(state, 0, KiB(16));
    // Batch flush inside the scan: first host page is free, second still has
    // a live guest neighbor. This exercises the non-trailing decommit path.
    const auto span = allocate(state, 1, 6);
    const auto tail = allocate(state, 7, 1);
    free(state, span);
    assert_released(state, 0, KiB(16));
    assert(state.memory[tail] == 0xAB);
    free(state, tail);
    assert_released(state, KiB(16), KiB(16));
    // Several entire host pages coalesce into a single decommit, repeatedly.
    for (int i = 0; i < 20; ++i) {
        const auto large = allocate(state, 8, 12);
        free(state, large);
        assert_released(state, KiB(32), KiB(48));
        assert(state.page_name_map.empty());
    }
    // Same allocation/free path also works on 4 KiB hosts.
    state.host_page_size = KiB(4);
    const auto small = allocate(state, 1, 1);
    free(state, small);
    assert_released(state, KiB(4), KiB(4));
}
