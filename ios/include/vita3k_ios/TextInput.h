#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>

// Vita limits are UTF-16 code units, not UTF-8 bytes. Never split a surrogate pair.
inline void vita3k_ios_limit_text(std::u16string &text, std::size_t maximum) {
    if (text.size() > maximum)
        text.resize(maximum);
    if (!text.empty() && text.back() >= 0xD800 && text.back() <= 0xDBFF)
        text.pop_back();
}

struct Vita3KIOSTextRequest {
    std::uint64_t id = 0;
    std::string title;
    std::u16string text;
    std::size_t maximum = 0;
    bool multiline = false;
    bool cancelable = true;
};
struct Vita3KIOSTextResult {
    std::uint64_t id = 0;
    std::u16string text;
    bool cancelled = false;
};
void vita3k_ios_update_text_input(const std::optional<Vita3KIOSTextRequest> &request);
std::optional<Vita3KIOSTextResult> vita3k_ios_take_text_result();
