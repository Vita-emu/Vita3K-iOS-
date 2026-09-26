#include <packages/archive.h>

#include <packages/license_file.h>
#include <packages/sfo.h>

#include <util/log.h>

#include <miniz.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace packages {
namespace {

constexpr std::size_t maximum_archive_entries = 100000;
constexpr std::size_t maximum_sfo_size = 16 * 1024 * 1024;
constexpr mz_uint maximum_archive_path_size = 4096;
constexpr std::uint64_t maximum_archive_install_size = 32ULL * 1024 * 1024 * 1024;
constexpr std::uint64_t install_free_space_margin = 64ULL * 1024 * 1024;
constexpr std::string_view sfo_suffix = "sce_sys/param.sfo";

struct SfoBuffer {
    std::vector<std::uint8_t> bytes;
    bool overflow{};
};

struct InstallOutput {
    std::ofstream stream;
    std::uint64_t written{};
    std::function<void(std::uint64_t)> progress;
};

bool read_archive_path(mz_zip_archive &zip, mz_uint index, std::string &name) {
    const auto name_size = mz_zip_reader_get_filename(&zip, index, nullptr, 0);
    if (name_size <= 1 || name_size > maximum_archive_path_size)
        return false;
    std::vector<char> storage(name_size);
    if (mz_zip_reader_get_filename(&zip, index, storage.data(), name_size) != name_size)
        return false;
    const std::string_view view(storage.data(), name_size - 1);
    if (view.find('\0') != std::string_view::npos)
        return false;
    name.assign(view);
    return true;
}

bool safe_title_id(std::string_view title_id) {
    return title_id.size() == 9 && std::ranges::all_of(title_id, [](unsigned char character) {
        return (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9');
    });
}

bool existing_parent_has_symlink(const std::filesystem::path &root,
    const std::filesystem::path &relative_parent, std::error_code &error) {
    error.clear();
    auto current = root;
    for (const auto &component : relative_parent) {
        current /= component;
        std::error_code status_error;
        const auto status = std::filesystem::symlink_status(current, status_error);
        // A component that does not exist yet is fine — it will be created as a
        // normal directory (e.g. ux0/patch on the first patch install). Only a
        // path that definitively IS a symlink, or a real error querying an
        // existing path, should reject. The previous code treated *any* error
        // (including not-found) as a symlink, which wrongly rejected clean zips.
        if (status_error) {
            if (status.type() == std::filesystem::file_type::not_found
                || status_error == std::errc::no_such_file_or_directory)
                continue;
            LOG_ERROR("Archive install: cannot stat destination component '{}': {}",
                current.string(), status_error.message());
            error = status_error;
            return true;
        }
        if (std::filesystem::is_symlink(status)) {
            LOG_ERROR("Archive install: destination component is a symlink: '{}'", current.string());
            return true;
        }
    }
    return false;
}

bool add_without_overflow(std::uint64_t &total, std::uint64_t value) {
    if (value > std::numeric_limits<std::uint64_t>::max() - total)
        return false;
    total += value;
    return true;
}

bool safe_archive_path(std::string_view path) {
    if (path.empty() || path.front() == '/' || path.front() == '\\' || path.find('\\') != std::string_view::npos || path.find(':') != std::string_view::npos)
        return false;

    std::size_t offset = 0;
    while (offset < path.size()) {
        const auto separator = path.find('/', offset);
        const auto end = separator == std::string_view::npos ? path.size() : separator;
        const auto component = path.substr(offset, end - offset);
        if (component.empty() || component == "." || component == "..")
            return false;
        if (separator == std::string_view::npos)
            break;
        offset = separator + 1;
        if (offset == path.size())
            break; // A single trailing slash denotes a directory.
    }
    return true;
}

bool find_content_root(std::string_view name, std::string &root) {
    if (!name.ends_with(sfo_suffix))
        return false;
    const auto prefix_size = name.size() - sfo_suffix.size();
    if (prefix_size != 0 && name[prefix_size - 1] != '/')
        return false;
    root.assign(name.substr(0, prefix_size));
    return true;
}

size_t append_sfo(void *opaque, mz_uint64 file_offset, const void *buffer, size_t size) {
    auto &output = *static_cast<SfoBuffer *>(opaque);
    if (file_offset != output.bytes.size() || size > maximum_sfo_size - output.bytes.size()) {
        output.overflow = true;
        return 0;
    }
    const auto *first = static_cast<const std::uint8_t *>(buffer);
    output.bytes.insert(output.bytes.end(), first, first + size);
    return size;
}

size_t write_install_file(void *opaque, mz_uint64 file_offset, const void *buffer, size_t size) {
    auto &output = *static_cast<InstallOutput *>(opaque);
    if (file_offset != output.written || size > std::numeric_limits<std::uint64_t>::max() - output.written)
        return 0;
    output.stream.write(static_cast<const char *>(buffer), static_cast<std::streamsize>(size));
    if (!output.stream)
        return 0;
    output.written += size;
    if (output.progress)
        output.progress(output.written);
    return size;
}

std::string install_target(const sfo::SfoAppInfo &app) {
    if (app.app_category.find("gp") != std::string::npos)
        return "ux0/patch/" + app.app_title_id;
    if (app.app_category == "ac")
        return valid_content_id(app.app_content_id) && app.app_content_id.substr(7, 9) == app.app_title_id
            ? "ux0/addcont/" + app.app_title_id + "/" + app.app_content_id.substr(20)
            : std::string{};
    return "ux0/app/" + app.app_title_id;
}

ArchiveInspection inspect_open_archive(mz_zip_archive &zip) {
    ArchiveInspection result{ .inspected = true };
    const auto entry_count = static_cast<std::size_t>(mz_zip_reader_get_num_files(&zip));
    if (entry_count == 0 || entry_count > maximum_archive_entries) {
        result.detail = entry_count == 0 ? "Archive contains no entries."
                                         : "Archive entry count exceeds the inspection limit.";
        return result;
    }

    struct SfoEntry {
        mz_uint index;
        std::string root;
    };
    std::vector<SfoEntry> sfo_entries;
    std::set<std::string> roots;
    std::set<std::string> paths;
    for (mz_uint index = 0; index < entry_count; ++index) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip, index, &stat)) {
            result.detail = "Could not read an archive directory entry.";
            return result;
        }
        std::string name;
        if (!read_archive_path(zip, index, name)) {
            ++result.unsafe_path_count;
            continue;
        }
        if (!safe_archive_path(name) || !paths.insert(name).second) {
            ++result.unsafe_path_count;
            continue;
        }
        if (!add_without_overflow(result.compressed_size, stat.m_comp_size) || !add_without_overflow(result.uncompressed_size, stat.m_uncomp_size)) {
            result.detail = "Archive size totals overflowed the supported range.";
            return result;
        }
        if (mz_zip_reader_is_file_a_directory(&zip, index)) {
            ++result.directory_count;
            continue;
        }
        ++result.file_count;
        std::string root;
        if (find_content_root(name, root) && roots.insert(root).second)
            sfo_entries.push_back({ index, std::move(root) });
    }

    if (result.unsafe_path_count != 0) {
        result.detail = "Archive contains unsafe absolute, traversal, or malformed paths.";
        return result;
    }
    if (sfo_entries.empty()) {
        result.detail = "Archive contains no sce_sys/param.sfo application metadata.";
        return result;
    }

    for (const auto &entry : sfo_entries) {
        SfoBuffer buffer;
        if (!mz_zip_reader_extract_to_callback(&zip, entry.index, append_sfo, &buffer, 0) || buffer.overflow) {
            result.detail = "Could not extract bounded PARAM.SFO metadata from the archive.";
            return result;
        }
        sfo::SfoAppInfo app;
        sfo::get_param_info(app, buffer.bytes, 1);
        if (!safe_title_id(app.app_title_id) || app.app_title.empty() || install_target(app).empty()) {
            result.detail = "Archive PARAM.SFO is malformed or has an unsafe title identity.";
            return result;
        }
        result.applications.push_back({ .content_root = entry.root,
            .title_id = app.app_title_id,
            .title = app.app_title,
            .category = app.app_category,
            .app_version = app.app_version,
            .content_id = app.app_content_id,
            .install_target = install_target(app) });
    }

    result.valid = true;
    std::ostringstream detail;
    detail << "Vita3K package archive inspector: " << result.file_count << " files; "
           << result.directory_count << " directories; " << result.applications.size()
           << " application" << (result.applications.size() == 1 ? "" : "s") << "; "
           << result.uncompressed_size << " uncompressed bytes";
    for (const auto &app : result.applications) {
        detail << "; title ID " << app.title_id << "; title " << app.title
               << "; planned target " << app.install_target;
    }
    detail << ". Inspection only; extraction and decryption are not active.";
    result.detail = detail.str();
    return result;
}

} // namespace

ArchiveInspection inspect_archive(std::span<const std::uint8_t> content) {
    ArchiveInspection result{ .inspected = true };
    if (content.empty()) {
        result.detail = "Archive is empty.";
        return result;
    }
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, content.data(), content.size(), 0)) {
        result.detail = "miniz rejected the archive container.";
        return result;
    }
    result = inspect_open_archive(zip);
    mz_zip_reader_end(&zip);
    return result;
}

ArchiveInspection inspect_archive(const std::filesystem::path &path) {
    ArchiveInspection result{ .inspected = true };
    const auto path_text = path.string();
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, path_text.c_str(), 0)) {
        result.detail = "miniz could not open the archive container.";
        return result;
    }
    result = inspect_open_archive(zip);
    mz_zip_reader_end(&zip);
    return result;
}

ArchiveInstallResult install_archive_transactionally(const std::filesystem::path &archive_path,
    const std::filesystem::path &vfs_root, const std::function<void(uint32_t)> &progress, const ArchivePrepare &prepare) {
    ArchiveInstallResult result{ .attempted = true };
    mz_zip_archive zip{};
    const auto path_text = archive_path.string();
    if (!mz_zip_reader_init_file(&zip, path_text.c_str(), 0)) {
        result.detail = "Could not open the selected ZIP/VPK archive.";
        return result;
    }

    const auto inspection = inspect_open_archive(zip);
    if (!inspection.valid) {
        result.detail = "Installation rejected: " + inspection.detail;
        mz_zip_reader_end(&zip);
        return result;
    }
    if (inspection.uncompressed_size > maximum_archive_install_size) {
        result.detail = "Installation rejected: archive exceeds the 32 GiB safety limit.";
        mz_zip_reader_end(&zip);
        return result;
    }

    std::set<std::string> unique_targets;
    for (const auto &application : inspection.applications) {
        if (!unique_targets.insert(application.install_target).second) {
            result.detail = "Installation rejected: multiple archive roots resolve to the same Vita target.";
            mz_zip_reader_end(&zip);
            return result;
        }
    }

    std::error_code error;
    std::filesystem::create_directories(vfs_root, error);
    const auto staging_parent = vfs_root / ".install-staging";
    if (!error)
        std::filesystem::create_directories(staging_parent, error);
    const auto staging_status = std::filesystem::symlink_status(staging_parent, error);
    if (error || std::filesystem::is_symlink(staging_status)) {
        result.detail = "Could not create a safe installation staging directory: " + error.message();
        mz_zip_reader_end(&zip);
        return result;
    }
    const auto available = std::filesystem::space(vfs_root, error).available;
    if (error || inspection.uncompressed_size > available || install_free_space_margin > available - inspection.uncompressed_size) {
        result.detail = error ? "Could not query free storage: " + error.message()
                              : "Installation rejected: insufficient free storage for transactional extraction.";
        mz_zip_reader_end(&zip);
        return result;
    }

    static std::atomic_uint64_t transaction_counter{};
    const auto transaction_id = static_cast<std::uint64_t>(
                                    std::chrono::steady_clock::now().time_since_epoch().count())
        + transaction_counter.fetch_add(1, std::memory_order_relaxed);
    const auto transaction_root = staging_parent / ("txn-" + std::to_string(transaction_id));
    const auto payload_root = transaction_root / "payload";
    const auto backup_root = transaction_root / "backup";
    std::filesystem::create_directories(payload_root, error);
    if (error) {
        result.detail = "Could not create the installation transaction: " + error.message();
        mz_zip_reader_end(&zip);
        return result;
    }

    const auto cleanup = [&]() {
        std::error_code ignored;
        std::filesystem::remove_all(transaction_root, ignored);
    };
    const auto fail = [&](std::string detail) {
        result.detail = std::move(detail);
        cleanup();
        mz_zip_reader_end(&zip);
        return result;
    };

    const auto entry_count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint index = 0; index < entry_count; ++index) {
        if (mz_zip_reader_is_file_a_directory(&zip, index))
            continue;
        std::string name;
        if (!read_archive_path(zip, index, name) || !safe_archive_path(name))
            return fail("Installation rejected an unsafe archive path during extraction.");

        const ArchiveApplicationInfo *owner = nullptr;
        std::string_view relative;
        for (const auto &application : inspection.applications) {
            if (name.starts_with(application.content_root) && (!owner || application.content_root.size() > owner->content_root.size())) {
                owner = &application;
                relative = std::string_view(name).substr(application.content_root.size());
            }
        }
        if (!owner || relative.empty())
            continue;
        if (!safe_archive_path(relative))
            return fail("Installation rejected an unsafe application-relative path.");

        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip, index, &stat) || !mz_zip_reader_is_file_supported(&zip, index))
            return fail("Installation encountered an unsupported or encrypted ZIP entry.");

        const auto output_path = payload_root / owner->install_target / std::filesystem::path(std::string(relative));
        std::filesystem::create_directories(output_path.parent_path(), error);
        if (error)
            return fail("Could not create a staged application directory: " + error.message());
        InstallOutput output{ .stream = std::ofstream(output_path, std::ios::binary),
            .progress = [&](std::uint64_t written) {
                if (progress && inspection.uncompressed_size != 0)
                    progress(static_cast<uint32_t>(std::min<uint64_t>(99,
                        (result.bytes_written + written) * 100 / inspection.uncompressed_size)));
            } };
        if (!output.stream)
            return fail("Could not create a staged application file.");
        if (!mz_zip_reader_extract_to_callback(&zip, index, write_install_file, &output, 0) || output.written != stat.m_uncomp_size)
            return fail("A staged ZIP entry failed decompression or size verification.");
        output.stream.close();
        if (!output.stream)
            return fail("Could not finish writing a staged file; check free storage.");
        ++result.file_count;
        result.bytes_written += output.written;
    }
    // A full NoNpDrm dump may keep licenses beside app/patch/addcont instead
    // of embedding work.bin. Import only licenses named for discovered content.
    for (mz_uint index = 0; index < entry_count; ++index) {
        std::string name;
        if (!read_archive_path(zip, index, name) || !name.ends_with(".rif")
            || !(name.starts_with("license/") || name.find("/license/") != std::string::npos))
            continue;
        for (const auto &application : inspection.applications) {
            if (!valid_content_id(application.content_id)
                || std::filesystem::path(name).filename() != application.content_id + ".rif")
                continue;
            mz_zip_archive_file_stat stat{};
            SfoBuffer buffer;
            if (!mz_zip_reader_file_stat(&zip, index, &stat) || stat.m_uncomp_size != 512
                || !mz_zip_reader_extract_to_callback(&zip, index, append_sfo, &buffer, 0)
                || buffer.bytes.size() != 512)
                return fail("Invalid bundled RIF license for " + application.title_id);
            const auto work = payload_root / application.install_target / "sce_sys/package/work.bin";
            if (std::filesystem::exists(work, error)) {
                std::array<std::uint8_t, 512> existing{};
                std::string id;
                if (!read_license_file(work, existing, id)
                    || !std::equal(existing.begin(), existing.end(), buffer.bytes.begin()))
                    return fail("Conflicting bundled licenses for " + application.title_id);
            } else {
                std::filesystem::create_directories(work.parent_path(), error);
                if (error)
                    return fail("Could not stage bundled license: " + error.message());
                std::ofstream output(work, std::ios::binary);
                output.write(reinterpret_cast<const char *>(buffer.bytes.data()), buffer.bytes.size());
                output.close();
                if (!output)
                    return fail("Could not write bundled license");
            }
        }
    }
    mz_zip_reader_end(&zip);

    if (result.file_count == 0) {
        cleanup();
        result.detail = "Installation rejected: no application files matched the discovered roots.";
        return result;
    }

    std::set<std::string> license_targets;
    try {
        for (const auto &application : inspection.applications) {
            const auto source = payload_root / application.install_target / "sce_sys/package/work.bin";
            if (!std::filesystem::exists(source))
                continue;
            std::array<std::uint8_t, 512> bytes{};
            std::string content_id;
            if (!read_license_file(source, bytes, content_id) || content_id != application.content_id || content_id.substr(7, 9) != application.title_id) {
                cleanup();
                result.detail = "Invalid or mismatched work.bin for " + application.title_id;
                return result;
            }
            const auto target = "ux0/license/" + application.title_id + "/" + content_id + ".rif";
            const auto license = payload_root / target;
            if (license_targets.insert(target).second) {
                std::filesystem::create_directories(license.parent_path());
                std::filesystem::copy_file(source, license);
            } else {
                std::array<std::uint8_t, 512> existing{};
                std::string existing_id;
                if (!read_license_file(license, existing, existing_id) || bytes != existing)
                    throw std::runtime_error("Conflicting bundled licenses for " + content_id);
            }
        }
        for (const auto &application : inspection.applications) {
            const auto base = "ux0/app/" + application.title_id;
            if (application.category.find("gp") != std::string::npos
                && !unique_targets.contains(base) && !std::filesystem::is_directory(vfs_root / base))
                throw std::runtime_error("Install the base game before its update: " + application.title_id);
            std::string preparation_error;
            if (prepare) {
                if (!prepare(application, payload_root, preparation_error))
                    throw std::runtime_error(preparation_error.empty() ? "Content preparation failed" : preparation_error);
            } else if (std::filesystem::exists(payload_root / application.install_target / "sce_pfs")) {
                throw std::runtime_error("NoNpDrm content requires PFS decryption support");
            }
            const auto staged = payload_root / application.install_target;
            const auto metadata = staged / "sce_sys/param.sfo";
            if (!std::filesystem::is_regular_file(metadata) || std::filesystem::file_size(metadata) > maximum_sfo_size)
                throw std::runtime_error("Prepared content is missing bounded PARAM.SFO metadata");
            std::ifstream input(metadata, std::ios::binary);
            const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
            sfo::SfoAppInfo prepared;
            sfo::get_param_info(prepared, bytes, 1);
            if (prepared.app_title_id != application.title_id || prepared.app_category != application.category
                || prepared.app_content_id != application.content_id)
                throw std::runtime_error("Prepared content identity does not match the selected package");
            if (application.category == "gd" && !std::filesystem::is_regular_file(staged / EBOOT_PATH))
                throw std::runtime_error("Game package is missing eboot.bin");
        }
    } catch (const std::exception &exception) {
        cleanup();
        result.detail = "Installation rejected: " + std::string(exception.what());
        return result;
    }

    struct TargetMove {
        std::filesystem::path staged;
        std::filesystem::path destination;
        std::filesystem::path backup;
        bool backed_up{};
        bool installed{};
    };
    std::vector<TargetMove> moves;
    auto commit_targets = unique_targets;
    commit_targets.insert(license_targets.begin(), license_targets.end());
    moves.reserve(commit_targets.size());
    for (const auto &target : commit_targets) {
        const auto relative_target = std::filesystem::path(target);
        if (existing_parent_has_symlink(vfs_root, relative_target.parent_path(), error)) {
            cleanup();
            result.detail = "Installation rejected a symlinked Vita destination path.";
            return result;
        }
        moves.push_back({ .staged = payload_root / target,
            .destination = vfs_root / target,
            .backup = backup_root / target });
        if (!std::filesystem::exists(moves.back().staged, error) || error) {
            cleanup();
            result.detail = "Installation transaction is missing a staged application root.";
            return result;
        }
    }

    const auto rollback = [&]() {
        bool restored = true;
        result.installed_targets.clear();
        for (auto iterator = moves.rbegin(); iterator != moves.rend(); ++iterator) {
            std::error_code ignored;
            if (iterator->installed)
                std::filesystem::remove_all(iterator->destination, ignored);
            if (iterator->backed_up) {
                std::filesystem::create_directories(iterator->destination.parent_path(), ignored);
                std::filesystem::rename(iterator->backup, iterator->destination, ignored);
            }
            if (ignored)
                restored = false;
        }
        if (restored)
            cleanup();
        else
            LOG_ERROR("Installation rollback incomplete; recovery files retained at {}", transaction_root.string());
    };

    for (auto &move : moves) {
        std::filesystem::create_directories(move.destination.parent_path(), error);
        if (error) {
            rollback();
            result.detail = "Could not prepare an emulated Vita destination: " + error.message();
            return result;
        }
        if (std::filesystem::exists(move.destination, error) && !error) {
            std::filesystem::create_directories(move.backup.parent_path(), error);
            if (!error)
                std::filesystem::rename(move.destination, move.backup, error);
            if (error) {
                rollback();
                result.detail = "Could not back up the previous installed title: " + error.message();
                return result;
            }
            move.backed_up = true;
        }
    }
    for (auto &move : moves) {
        std::filesystem::rename(move.staged, move.destination, error);
        if (error) {
            rollback();
            result.detail = "Could not commit the staged Vita application: " + error.message();
            return result;
        }
        move.installed = true;
        const auto target = std::filesystem::relative(move.destination, vfs_root).generic_string();
        if (unique_targets.contains(target))
            result.installed_targets.push_back(target);
    }

    cleanup();
    result.success = true;
    if (progress)
        progress(100);
    result.application_count = inspection.applications.size();
    result.installed_applications = inspection.applications;
    std::ostringstream detail;
    detail << "Installed " << result.application_count << " application root"
           << (result.application_count == 1 ? "" : "s") << " transactionally; "
           << result.file_count << " files; " << result.bytes_written << " bytes; targets ";
    for (std::size_t index = 0; index < result.installed_targets.size(); ++index) {
        if (index != 0)
            detail << ", ";
        detail << result.installed_targets[index];
    }
    detail << ".";
    result.detail = detail.str();
    return result;
}

ArchiveInstallResult install_directory_transactionally(const std::filesystem::path &directory,
    const std::filesystem::path &vfs_root, const std::function<void(uint32_t)> &progress, const ArchivePrepare &prepare) {
    ArchiveInstallResult result{ .attempted = true };
    struct TemporaryArchive {
        std::filesystem::path root;
        mz_zip_archive zip{};
        ~TemporaryArchive() {
            if (zip.m_pState)
                mz_zip_writer_end(&zip);
            std::error_code ignored;
            if (!root.empty())
                std::filesystem::remove_all(root, ignored);
        }
    } temporary;
    try {
        if (!std::filesystem::is_directory(directory) || std::filesystem::is_symlink(directory))
            throw std::runtime_error("Select a regular game directory");
        static std::atomic_uint64_t counter{};
        for (;;) {
            const auto candidate = std::filesystem::temp_directory_path() / ("vita3k-import-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(counter.fetch_add(1)));
            if (std::filesystem::create_directory(candidate)) {
                temporary.root = candidate;
                break;
            }
        }
        const auto archive = temporary.root / "content.zip";
        if (!mz_zip_writer_init_file_v2(&temporary.zip, archive.string().c_str(), 0, MZ_ZIP_FLAG_WRITE_ZIP64))
            throw std::runtime_error("Could not create temporary directory import archive");
        std::uint64_t total = 0;
        std::size_t entries = 0;
        // Store files without compression. miniz reads from disk in bounded chunks;
        // the temporary archive lets directory imports share the same transaction.
        for (const auto &entry : std::filesystem::recursive_directory_iterator(directory)) {
            if (++entries > maximum_archive_entries || entry.is_symlink())
                throw std::runtime_error("Directory contains a symlink or too many entries");
            if (entry.is_directory())
                continue;
            if (!entry.is_regular_file() || !add_without_overflow(total, entry.file_size())
                || total > maximum_archive_install_size)
                throw std::runtime_error("Directory contains unsupported files or exceeds 32 GiB");
            const auto relative = entry.path().lexically_relative(directory).generic_string();
            if (!safe_archive_path(relative) || relative.size() >= maximum_archive_path_size)
                throw std::runtime_error("Directory contains an unsafe path");
            if (!mz_zip_writer_add_file(&temporary.zip, relative.c_str(), entry.path().string().c_str(), nullptr, 0, 0))
                throw std::runtime_error("Could not read game directory or write temporary archive; check free storage");
        }
        if (!mz_zip_writer_finalize_archive(&temporary.zip))
            throw std::runtime_error("Could not finish directory import archive");
        mz_zip_writer_end(&temporary.zip);
        result = install_archive_transactionally(archive, vfs_root, progress, prepare);
    } catch (const std::exception &error) {
        result.detail = error.what();
    }
    return result;
}

} // namespace packages
