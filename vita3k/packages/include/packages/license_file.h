// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace packages {
inline bool valid_content_id(std::string_view id) {
    if (id.size() != 36 || id[6] != '-' || id[16] != '_' || id[19] != '-')
        return false;
    for (std::size_t i = 0; i < id.size(); ++i) {
        if (i == 6 || i == 16 || i == 19)
            continue;
        const auto c = id[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (i >= 20 && c == '_')))
            return false;
    }
    return true;
}

// RIF/work.bin has a fixed 512-byte layout. Validate before forming paths or
// passing a license to the PFS decoder; never treat content_id as an unbounded C string.
inline bool read_license_file(const std::filesystem::path &path,
    std::array<std::uint8_t, 512> &bytes, std::string &content_id) {
    content_id.clear();
    std::ifstream input(path, std::ios::binary);
    if (!input.read(reinterpret_cast<char *>(bytes.data()), bytes.size()) || input.peek() != std::char_traits<char>::eof())
        return false;
    const auto first = bytes.begin() + 0x10;
    const auto end = std::find(first, first + 0x30, 0);
    content_id.assign(first, end);
    if (!valid_content_id(content_id)) {
        content_id.clear();
        return false;
    }
    return true;
}
} // namespace packages
