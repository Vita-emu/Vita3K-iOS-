// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
#pragma once

#include <miniz.h>
#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <istream>
#include <memory>
#include <ostream>
#include <stdexcept>

namespace packages {
// The caller initializes an AES-CTR context for this segment. Working memory
// stays fixed even for large compressed firmware images; buffers live off-stack.
inline void decrypt_stream_exact(std::istream &input, std::ostream &output,
    EVP_CIPHER_CTX *cipher, std::uint64_t remaining, bool compressed) {
    struct Buffers {
        std::array<unsigned char, 64 * 1024 + EVP_MAX_BLOCK_LENGTH> decrypted;
        std::array<unsigned char, 64 * 1024> expanded;
    };
    auto buffers = std::make_unique<Buffers>();
    struct Inflater {
        mz_stream stream{};
        bool initialized = false;
        ~Inflater() {
            if (initialized)
                mz_inflateEnd(&stream);
        }
    } inflater;
    if (compressed) {
        if (mz_inflateInit(&inflater.stream) != MZ_OK)
            throw std::runtime_error("Could not initialize firmware decompression");
        inflater.initialized = true;
    }
    bool ended = false;
    while (remaining != 0) {
        const int amount = static_cast<int>(std::min<std::uint64_t>(remaining, 64 * 1024));
        if (!input.read(reinterpret_cast<char *>(buffers->decrypted.data()), amount))
            throw std::runtime_error("Truncated SCE segment");
        int written = 0;
        if (EVP_DecryptUpdate(cipher, buffers->decrypted.data(), &written,
                buffers->decrypted.data(), amount)
                != 1
            || written != amount)
            throw std::runtime_error("Firmware segment decryption failed");
        remaining -= amount;
        if (!compressed) {
            output.write(reinterpret_cast<char *>(buffers->decrypted.data()), written);
        } else if (!ended) {
            auto &stream = inflater.stream;
            stream.next_in = buffers->decrypted.data();
            stream.avail_in = written;
            do {
                const auto before = stream.avail_in;
                stream.next_out = buffers->expanded.data();
                stream.avail_out = buffers->expanded.size();
                const int result = mz_inflate(&stream, MZ_NO_FLUSH);
                const auto produced = buffers->expanded.size() - stream.avail_out;
                if (result != MZ_OK && result != MZ_STREAM_END && result != MZ_BUF_ERROR)
                    throw std::runtime_error("Invalid compressed firmware segment");
                if (!output.write(reinterpret_cast<char *>(buffers->expanded.data()), produced))
                    throw std::runtime_error("Could not write decrypted firmware segment");
                if (result == MZ_STREAM_END) {
                    ended = true;
                    break; // SCE segment alignment may leave trailing padding.
                }
                if (before == stream.avail_in && produced == 0) {
                    if (stream.avail_in != 0)
                        throw std::runtime_error("Firmware decompression made no progress");
                    break; // Need more encrypted input (or the stream is truncated).
                }
            } while (stream.avail_in != 0 || stream.avail_out == 0);
        }
        if (!output)
            throw std::runtime_error("Could not write decrypted firmware segment");
    }
    unsigned char final_bytes[EVP_MAX_BLOCK_LENGTH];
    int final_size = 0;
    if (EVP_DecryptFinal_ex(cipher, final_bytes, &final_size) != 1 || final_size != 0)
        throw std::runtime_error("Incomplete firmware segment decryption");
    if (compressed && !ended)
        throw std::runtime_error("Truncated compressed firmware segment");
}
} // namespace packages
