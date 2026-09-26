// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <istream>
#include <ostream>

namespace packages {
// Copy exactly the declared payload with fixed working memory. A truncated
// source or a full destination must never be reported as a successful install.
inline bool copy_stream_exact(std::istream &input, std::ostream &output, std::uint64_t remaining) {
    std::array<char, 64 * 1024> buffer;
    while (remaining != 0) {
        const auto amount = static_cast<std::streamsize>(std::min<std::uint64_t>(remaining, buffer.size()));
        if (!input.read(buffer.data(), amount) || !output.write(buffer.data(), amount))
            return false;
        remaining -= static_cast<std::uint64_t>(amount);
    }
    return true;
}
} // namespace packages
