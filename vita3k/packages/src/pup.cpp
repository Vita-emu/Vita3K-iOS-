// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

/**
 * @file pup.cpp
 * @brief PlayStation Update Package (`.pup`) handling
 *
 * On the PlayStation Vita and just like any other PlayStation console, PUP packages
 * contain firmware updates
 */

#include <openssl/evp.h>
#include <packages/exfat.h>
#include <packages/sce_types.h>
#include <packages/stream_copy.h>
#include <packages/stream_decrypt.h>
#include <util/fs.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>

// Credits to TeamMolecule for their original work on this https://github.com/TeamMolecule/sceutils

static const std::map<int, std::string> PUP_TYPES = {
    { 0x100, "version.txt" },
    { 0x101, "license.xml" },
    { 0x200, "psp2swu.self" },
    { 0x204, "cui_setupper.self" },
    { 0x400, "package_scewm.wm" },
    { 0x401, "package_sceas.as" },
    { 0x2005, "UpdaterES1.CpUp" },
    { 0x2006, "UpdaterES2.CpUp" },
};

static const char *FSTYPE[] = {
    "unknown0",
    "os0",
    "unknown2",
    "unknown3",
    "vs0_chmod",
    "unknown5",
    "unknown6",
    "unknown7",
    "pervasive8",
    "boot_slb2",
    "vs0",
    "devkit_cp",
    "motionC",
    "bbmc",
    "unknownE",
    "motionF",
    "touch10",
    "touch11",
    "syscon12",
    "syscon13",
    "pervasive14",
    "unknown15",
    "vs0_tarpatch",
    "sa0",
    "pd0",
    "pervasive19",
    "unknown1A",
    "psp_emulist",
};

static std::string make_filename(const unsigned char *hdr, size_t header_length, int64_t filetype, uint32_t index) {
    if (header_length < 24)
        throw std::runtime_error("Truncated PUP package header");
    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t flags = 0;
    uint32_t moffs = 0;
    uint64_t metaoffs = 0;
    memcpy(&magic, &hdr[0], 4);
    memcpy(&version, &hdr[4], 4);
    memcpy(&flags, &hdr[8], 4);
    memcpy(&moffs, &hdr[12], 4);
    memcpy(&metaoffs, &hdr[16], 8);

    if (magic == SCE_MAGIC && version == 3 && flags == 0x30040) {
        if (metaoffs > header_length - 5)
            throw std::runtime_error("Invalid PUP package metadata offset");
        const unsigned char t = hdr[metaoffs + 4];

        if (t < std::size(FSTYPE)) {
            // Stable per-package order, including repeated installs and >99
            // parts. A process-global counter changed ordering across imports.
            return fmt::format("{}-{:010}.pkg", FSTYPE[t], index);
        }
    }
    return fmt::format("unknown-0x{:X}-{:010}.pkg", filetype, index);
}

static void extract_pup_files(const fs::path &pup, const fs::path &output) {
    constexpr uint64_t header_size = 0x80;
    constexpr uint64_t record_size = 0x20;
    fs::ifstream infile(pup, std::ios::binary | std::ios::ate);
    const auto end = infile.tellg();
    if (!infile || end < static_cast<std::streamoff>(header_size))
        throw std::runtime_error("PUP file is missing or truncated");
    const uint64_t file_size = static_cast<uint64_t>(end);
    infile.seekg(0);
    char header[header_size];
    if (!infile.read(header, sizeof(header)) || std::memcmp(header, "SCEUF", 5) != 0)
        throw std::runtime_error("Not a PS Vita PUP file");
    uint32_t count = 0;
    std::memcpy(&count, header + 0x18, sizeof(count));
    if (count > (file_size - header_size) / record_size)
        throw std::runtime_error("Truncated PUP file table");

    for (uint32_t index = 0; index < count; ++index) {
        infile.seekg(header_size + uint64_t(index) * record_size);
        uint64_t record[4]{};
        if (!infile.read(reinterpret_cast<char *>(record), sizeof(record)))
            throw std::runtime_error("Could not read PUP file table");
        const auto [filetype, offset, length, flags] = record;
        (void)flags;
        if (offset > file_size || length > file_size - offset)
            throw std::runtime_error("PUP payload extends beyond the selected file");
        std::string filename;
        if (PUP_TYPES.contains(filetype)) {
            filename = PUP_TYPES.at(filetype);
        } else {
            unsigned char hdr[HEADER_LENGTH]{};
            infile.seekg(offset);
            // A valid small package need not fill the 4 KiB inspection buffer.
            // Read only this entry's bytes; make_filename checks the fields it
            // actually accesses against that length, including the metadata tag.
            const size_t header_length = static_cast<size_t>(std::min<uint64_t>(length, sizeof(hdr)));
            if (!infile.read(reinterpret_cast<char *>(hdr), header_length))
                throw std::runtime_error("Could not read PUP package header");
            filename = make_filename(hdr, header_length, filetype, index);
        }
        fs::ofstream outfile(output / filename, std::ios::binary);
        infile.seekg(offset);
        if (!outfile || !packages::copy_stream_exact(infile, outfile, length))
            throw std::runtime_error("Could not extract PUP payload; check free storage");
        outfile.close();
        if (!outfile)
            throw std::runtime_error("Could not finish writing PUP payload");
    }
}

static void decrypt_segments(std::ifstream &infile, const fs::path &outdir, const fs::path &filename, KeyStore &SCE_KEYS) {
    char sceheaderbuffer[SceHeader::Size];
    if (!infile.read(sceheaderbuffer, SceHeader::Size))
        throw std::runtime_error("Truncated SCE header");
    const SceHeader sce_hdr = SceHeader(sceheaderbuffer);

    const auto [sysver, selftype] = get_key_type(infile, sce_hdr);

    const std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    const std::unique_ptr<EVP_CIPHER, decltype(&EVP_CIPHER_free)> algorithm(EVP_CIPHER_fetch(nullptr, "AES-128-CTR", nullptr), EVP_CIPHER_free);
    auto *cipher_ctx = context.get();
    auto *cipher = algorithm.get();
    if (!cipher_ctx || !cipher)
        throw std::runtime_error("Could not initialize firmware decryption");

    // Reset the offset to the beginning of the file
    infile.seekg(0, std::ios::beg);

    // Segment metadata lives in the SCE header, not the payload. Do not keep
    // a second copy of the entire package alive while decrypting segments.
    infile.seekg(0, std::ios::end);
    const auto file_size = infile.tellg();
    if (file_size < 0 || sce_hdr.header_length > static_cast<uint64_t>(file_size)
        || sce_hdr.header_length > 16 * 1024 * 1024
        || sce_hdr.metadata_offset > sce_hdr.header_length
        || sce_hdr.header_length - sce_hdr.metadata_offset < 48 + MetadataInfo::Size + MetadataHeader::Size)
        throw std::runtime_error("Invalid SCE package header");
    const auto scesegs = [&] {
        std::vector<uint8_t> header(sce_hdr.header_length);
        infile.seekg(0);
        if (!infile.read(reinterpret_cast<char *>(header.data()), header.size()))
            throw std::runtime_error("Truncated SCE package metadata");
        return get_segments(header.data(), sce_hdr, SCE_KEYS, sysver, selftype);
    }();
    for (const auto &sceseg : scesegs) {
        if (sceseg.offset > static_cast<uint64_t>(file_size)
            || sceseg.size > static_cast<uint64_t>(file_size) - sceseg.offset
            || sceseg.key.size() != 16 || sceseg.iv.size() != 16)
            throw std::runtime_error("Invalid SCE segment size or key");
        fs::ofstream outfile(outdir / fs_utils::path_concat(filename, ".seg02"), std::ios::binary);
        infile.seekg(sceseg.offset);
        if (!outfile || !infile)
            throw std::runtime_error("Could not open firmware segment streams");
        if (EVP_DecryptInit_ex(cipher_ctx, cipher, nullptr, reinterpret_cast<const unsigned char *>(sceseg.key.data()), reinterpret_cast<const unsigned char *>(sceseg.iv.data())) != 1
            || EVP_CIPHER_CTX_set_padding(cipher_ctx, 0) != 1)
            throw std::runtime_error("Firmware segment decryption failed");
        packages::decrypt_stream_exact(infile, outfile, cipher_ctx, sceseg.size, sceseg.compressed);
        outfile.close();
        if (!outfile)
            throw std::runtime_error("Could not write decrypted firmware segment");
    }
}

static void join_files(const fs::path &path, const std::string &filename, const fs::path &output) {
    std::vector<fs::path> files;

    for (auto &p : fs::directory_iterator(path)) {
        if (p.path().filename().string().substr(0, 4) == filename) {
            files.push_back(p.path());
        }
    }

    std::sort(files.begin(), files.end());

    fs::ofstream fileout(output, std::ios::binary);
    for (const auto &file : files) {
        fs::ifstream input(file, std::ios::binary);
        if (!input || !packages::copy_stream_exact(input, fileout, fs::file_size(file)))
            throw std::runtime_error("Could not assemble firmware image; check free storage");
        input.close();
        fs::remove(file);
    }
    fileout.close();
    if (!fileout)
        throw std::runtime_error("Could not finish writing firmware image");
}

static void decrypt_pup_packages(const fs::path &src, const fs::path &dest, KeyStore &SCE_KEYS) {
    std::vector<fs::path> pkgfiles;

    for (const auto &p : fs::directory_iterator(src)) {
        if (p.path().filename().extension().string() == ".pkg")
            pkgfiles.push_back(p.path().filename());
    }

    for (const auto &filename : pkgfiles) {
        const fs::path &filepath = src / filename;
        fs::ifstream infile(filepath, std::ios::binary);
        decrypt_segments(infile, dest, filename, SCE_KEYS);
        infile.close();
    }

    join_files(dest, "os0-", dest / "os0.img");
    join_files(dest, "pd0-", dest / "pd0.img");
    join_files(dest, "vs0-", dest / "vs0.img");
    join_files(dest, "sa0-", dest / "sa0.img");
}

std::string install_pup(const fs::path &vita_fs_path, const fs::path &pup_path, const std::function<void(uint32_t)> &progress_callback) {
    fs::path pup_dec_root = vita_fs_path / "PUP_DEC";
    try {
        if (fs::exists(pup_dec_root)) {
            LOG_WARN("Path already exists, deleting it and reinstalling");
            fs::remove_all(pup_dec_root);
        }

        const auto update_progress = [&](const uint32_t progress) {
            if (progress_callback)
                progress_callback(progress);
        };

        LOG_INFO("Extracting {} to {}", pup_path, pup_dec_root);

        fs::create_directory(pup_dec_root);
        const auto pup_dest = pup_dec_root / "PUP";
        fs::create_directory(pup_dest);
        update_progress(10);

        extract_pup_files(pup_path, pup_dest);
        update_progress(20);

        const auto pup_dec = pup_dec_root / "PUP_dec";
        fs::create_directory(pup_dec);

        KeyStore SCE_KEYS;
        register_keys(SCE_KEYS, 0);

        update_progress(30);
        decrypt_pup_packages(pup_dest, pup_dec, SCE_KEYS);

        update_progress(70);
        if (fs::file_size(pup_dec / "os0.img") > 0)
            extract_fat(pup_dec, "os0.img", vita_fs_path);
        if (fs::file_size(pup_dec / "pd0.img") > 0)
            exfat::extract_exfat(pup_dec, "pd0.img", vita_fs_path);
        if (fs::file_size(pup_dec / "sa0.img") > 0)
            extract_fat(pup_dec, "sa0.img", vita_fs_path);
        if (fs::file_size(pup_dec / "vs0.img") > 0)
            extract_fat(pup_dec, "vs0.img", vita_fs_path);
        update_progress(100);

        // get firmware version
        std::string fw_version;
        fs::ifstream versionFile(pup_dest / "version.txt");
        if (versionFile.is_open()) {
            std::getline(versionFile, fw_version);
            versionFile.close();
        } else
            LOG_WARN("Firmware Version file not found!");

        fs::remove_all(pup_dec_root);

        return fw_version;
    } catch (const std::exception &error) {
        LOG_ERROR("Firmware installation failed: {}", error.what());
        boost::system::error_code cleanup_error;
        fs::remove_all(pup_dec_root, cleanup_error);
        return {};
    }
}
