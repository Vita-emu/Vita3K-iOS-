#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

// Structural validation catches partial writes after forced termination before
// handing cached bytes to a GPU driver. Semantic validation remains the driver's.
inline bool valid_spirv_cache(std::span<const std::uint32_t> words) {
    if (words.size() <= 5 || words[0] != 0x07230203 || words[4] != 0)
        return false;
    std::size_t offset = 5;
    while (offset < words.size()) {
        const std::size_t count = words[offset] >> 16;
        if (count == 0 || count > words.size() - offset)
            return false;
        offset += count;
    }
    return offset == words.size();
}

inline bool valid_pipeline_hash_count(std::size_t file_size, std::size_t count) {
    constexpr std::size_t header_size = sizeof(std::uint32_t) + sizeof(std::size_t);
    return file_size >= header_size && count <= (file_size - header_size) / sizeof(std::uint64_t);
}
