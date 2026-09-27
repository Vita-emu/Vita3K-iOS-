#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <miniz.h>
#include <optional>
#include <packages/archive.h>
#include <packages/license_file.h>
#include <packages/sfo.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <util/log.h>
#include <vector>
#ifdef __APPLE__
#include <pthread/qos.h>
#endif
namespace fs {
using namespace std::filesystem;
using std::ifstream;
using std::ofstream;
} // namespace fs
namespace boost::system {
using error_code = std::error_code;
}
namespace fs_utils {
fs::path path_concat(const fs::path &path, const char *suffix) { return path.string() + suffix; }
} // namespace fs_utils
namespace fmt {
std::string format(const char *, const std::string &id) { return id + ".rif"; }
} // namespace fmt
// INSERT_LICENSE_STRUCT
struct EmuEnvState {
    fs::path vita_fs_path;
    std::string license_title_id, license_content_id;
    struct {
        std::map<std::string, SceNpDrmLicense> rif;
    } license;
};
// INSERT_LICENSE_READ_COPY
static bool decoder_fail = false, decoder_throw = false, prevent_commit = false;
enum class F00DEncryptorTypes { native };
static std::string rif2zrif(std::ifstream &) { return "synthetic license"; }
// Synthetic decoder exercises the real staging/replacement contract. It does
// not model PFS cryptography or claim compatibility with encrypted retail data.
static int execute(std::string &, fs::path &src, fs::path &dst, F00DEncryptorTypes, std::string &) {
    fs::create_directories(dst);
    if (decoder_fail)
        return -1;
    if (decoder_throw)
        throw std::runtime_error("synthetic decoder failure");
    fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
    fs::remove_all(dst / "sce_pfs");
    if (prevent_commit) {
        const auto backup = fs_utils::path_concat(src, "_encrypted_backup");
        fs::create_directory(backup);
        std::ofstream(backup / "keep") << "keep";
    }
    return 0;
}
// INSERT_DECRYPT
struct Job {
    std::atomic<int> progress{ 0 };
    std::atomic<bool> done{ false };
    bool success = false, apps_rescanned = false;
    std::optional<int> games_snapshot;
    std::string message;
};
using ImportJob = Job;
static std::shared_ptr<ImportJob> g_import_job;
#ifndef __APPLE__
// Apple provides these in pthread/qos.h; only stub QoS on other hosts.
static constexpr int QOS_CLASS_UTILITY = 0;
static void pthread_set_qos_class_self_np(int, int) {}
#endif
static void vita3k_ios_report_import_result(const std::string &, bool) {}
namespace app {
static bool scan_apps(EmuEnvState &) { return true; }
} // namespace app
static int native_games(EmuEnvState &) { return 1; }
// INSERT_LICENSE_WORKER
static void wait_license_job() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!g_import_job->done.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(g_import_job->done.load());
}
static auto make_preparer(EmuEnvState &emuenv) {
    auto job = std::make_shared<Job>();
    // INSERT_PREPARE
    return prepare;
}
using Entries = std::vector<std::pair<std::string, std::string>>;
static const std::string title_id = "PCSE00001";
static const std::string cid = "UP0000-PCSE00001_00-ABCDEFGHIJKLMNOP";
static std::string license(const std::string &id) {
    std::string bytes(512, '\0');
    bytes.replace(0x10, id.size(), id);
    return bytes;
}
static std::string make_sfo(const std::string &category, const std::string &id = cid) {
    const Entries fields{ { "TITLE_ID", title_id }, { "TITLE", "Synthetic game" }, { "CATEGORY", category }, { "CONTENT_ID", id }, { "APP_VER", "01.00" } };
    std::string keys, data;
    std::vector<SfoIndexTableEntry> indexes;
    for (auto &[key, value] : fields) {
        indexes.push_back({ static_cast<uint16_t>(keys.size()), UTF8_NULL,
            static_cast<uint32_t>(value.size() + 1), static_cast<uint32_t>(value.size() + 1), static_cast<uint32_t>(data.size()) });
        keys += key + '\0';
        data += value + '\0';
    }
    SfoHeader header{ 0x46535000, 0x101, static_cast<uint32_t>(sizeof(SfoHeader) + indexes.size() * sizeof(SfoIndexTableEntry)), 0, static_cast<uint32_t>(indexes.size()) };
    header.data_table_start = header.key_table_start + keys.size();
    std::string bytes(reinterpret_cast<char *>(&header), sizeof(header));
    bytes.append(reinterpret_cast<char *>(indexes.data()), indexes.size() * sizeof(SfoIndexTableEntry));
    return bytes + keys + data;
}
static void write_file(const fs::path &path, const std::string &bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output.write(bytes.data(), bytes.size());
    output.close();
    assert(output);
}
static std::string read_file(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    return { (std::istreambuf_iterator<char>(input)), {} };
}
static void archive(const fs::path &path, const Entries &entries) {
    mz_zip_archive zip{};
    assert(mz_zip_writer_init_file(&zip, path.string().c_str(), 0));
    for (const auto &[name, bytes] : entries)
        assert(mz_zip_writer_add_mem(&zip, name.c_str(), bytes.data(), bytes.size(), MZ_BEST_SPEED));
    assert(mz_zip_writer_finalize_archive(&zip));
    mz_zip_writer_end(&zip);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    const fs::path root = argv[1];
    const auto vfs = root / "vfs";
    const auto zip = root / "game.vpk";
    const auto app = vfs / "ux0/app" / title_id;
    EmuEnvState emuenv;
    emuenv.vita_fs_path = vfs;
    auto prepare = make_preparer(emuenv);
    Entries plain{ { "sce_sys/param.sfo", make_sfo("gd") }, { "eboot.bin", "plain" } };
    archive(zip, plain);
    assert(packages::install_archive_transactionally(zip, vfs, {}, prepare).success);
    assert(read_file(app / "eboot.bin") == "plain");

    Entries encrypted = plain;
    encrypted.push_back({ "sce_pfs/files.db", "synthetic" });
    archive(zip, encrypted);
    assert(!packages::install_archive_transactionally(zip, vfs, {}, prepare).success);
    assert(read_file(app / "eboot.bin") == "plain");
    encrypted.push_back({ "sce_sys/package/work.bin", license(cid) });
    archive(zip, encrypted);
    decoder_fail = true;
    assert(!packages::install_archive_transactionally(zip, vfs, {}, prepare).success);
    decoder_fail = false;
    decoder_throw = true;
    assert(!packages::install_archive_transactionally(zip, vfs, {}, prepare).success);
    decoder_throw = false;
    assert(!fs::exists(vfs / "ux0/license" / title_id / (cid + ".rif")));
    assert(read_file(app / "eboot.bin") == "plain");
    assert(packages::install_archive_transactionally(zip, vfs, {}, prepare).success);
    assert(!fs::exists(app / "sce_pfs"));
    assert(read_file(vfs / "ux0/license" / title_id / (cid + ".rif")) == license(cid));

    // Already-imported license works for the app and its update without work.bin.
    encrypted.pop_back();
    archive(zip, encrypted);
    assert(packages::install_archive_transactionally(zip, vfs, {}, prepare).success);
    Entries patch{ { "update/sce_sys/param.sfo", make_sfo("gp") }, { "update/sce_pfs/files.db", "synthetic" }, { "update/assets/patch.dat", "patch" } };
    archive(zip, patch);
    assert(!packages::install_archive_transactionally(zip, root / "empty", {}, prepare).success);
    assert(packages::install_archive_transactionally(zip, vfs, {}, prepare).success);
    assert(read_file(vfs / "ux0/patch" / title_id / "assets/patch.dat") == "patch");

    // A full NoNpDrm tree may contain a sibling license/app directory. Its
    // base and patch share the staged license before either is committed.
    Entries tree;
    for (auto &[name, bytes] : plain)
        tree.push_back({ "ux0/app/" + title_id + "/" + name, bytes });
    tree.push_back({ "ux0/app/" + title_id + "/sce_pfs/files.db", "synthetic" });
    tree.push_back({ "ux0/patch/" + title_id + "/sce_sys/param.sfo", make_sfo("gp") });
    tree.push_back({ "ux0/patch/" + title_id + "/sce_pfs/files.db", "synthetic" });
    tree.push_back({ "ux0/license/app/" + title_id + "/" + cid + ".rif", license(cid) });
    archive(zip, tree);
    assert(packages::install_archive_transactionally(zip, root / "tree", {}, prepare).success);
    assert(read_file(root / "tree/ux0/license" / title_id / (cid + ".rif")) == license(cid));

    // Two DLC packages install to separate directories and preserve each other.
    const std::string dlc1 = "UP0000-PCSE00001_00-DLC0000000000001";
    const std::string dlc2 = "UP0000-PCSE00001_00-DLC0000000000002";
    Entries dlcs{ { "a/sce_sys/param.sfo", make_sfo("ac", dlc1) }, { "a/data", "one" },
        { "b/sce_sys/param.sfo", make_sfo("ac", dlc2) }, { "b/data", "two" } };
    archive(zip, dlcs);
    const auto result = packages::install_archive_transactionally(zip, vfs, {}, prepare);
    assert(result.success && result.application_count == 2);
    assert(read_file(vfs / "ux0/addcont" / title_id / dlc1.substr(20) / "data") == "one");
    assert(read_file(vfs / "ux0/addcont" / title_id / dlc2.substr(20) / "data") == "two");
    dlcs[0].second = make_sfo("ac", "../../unsafe");
    archive(zip, dlcs);
    assert(!packages::install_archive_transactionally(zip, vfs, {}, prepare).success);

    // Wrong-title and truncated bundled licenses must not replace any target.
    encrypted.push_back({ "sce_sys/package/work.bin", license(dlc1) });
    archive(zip, encrypted);
    assert(!packages::install_archive_transactionally(zip, vfs, {}, prepare).success);
    encrypted.back().second.resize(20);
    archive(zip, encrypted);
    assert(!packages::install_archive_transactionally(zip, vfs, {}, prepare).success);
    auto duplicate = plain;
    duplicate.push_back(plain.front());
    archive(zip, duplicate);
    assert(!packages::install_archive_transactionally(zip, vfs, {}, prepare).success);
    auto traversal = plain;
    traversal.push_back({ "../escape", "bad" });
    archive(zip, traversal);
    assert(!packages::install_archive_transactionally(zip, vfs, {}, prepare).success);

    // A later preparation failure cannot partially install a multi-root archive.
    Entries multiple{ { "app/sce_sys/param.sfo", make_sfo("gd") }, { "app/eboot.bin", "replacement" },
        { "patch/sce_sys/param.sfo", make_sfo("gp") } };
    archive(zip, multiple);
    assert(!packages::install_archive_transactionally(zip, vfs, {}, [](const auto &meta, const auto &, auto &error) {
        error = "synthetic preparation failure";
        return meta.category != "gp";
    }).success);
    assert(read_file(app / "eboot.bin") == "plain");

    // Metadata loss/change after preparation must not commit unusable content.
    archive(zip, plain);
    assert(!packages::install_archive_transactionally(zip, vfs, {}, [](const auto &meta, const auto &payload, auto &) {
        fs::remove(payload / meta.install_target / "sce_sys/param.sfo");
        return true;
    }).success);
    assert(read_file(app / "eboot.bin") == "plain");
    auto missing_eboot = plain;
    missing_eboot.pop_back();
    archive(zip, missing_eboot);
    assert(!packages::install_archive_transactionally(zip, vfs, {}, prepare).success);
    archive(zip, plain);
    fs::resize_file(zip, fs::file_size(zip) / 2);
    assert(!packages::install_archive_transactionally(zip, vfs, {}, prepare).success);

    const auto directory = root / "directory";
    for (auto &[name, bytes] : plain)
        write_file(directory / "wrapped" / name, bytes);
    assert(packages::install_directory_transactionally(directory, vfs, {}, prepare).success);
    fs::create_symlink(app / "eboot.bin", directory / "link");
    assert(!packages::install_directory_transactionally(directory, vfs, {}, prepare).success);
    fs::remove(directory / "link");

    const auto work = root / "work.bin";
    write_file(work, license(cid));
    assert(copy_license(emuenv, work));
    assert(copy_license(emuenv, vfs / "ux0/license" / title_id / (cid + ".rif")));
    write_file(work, std::string(512, 'X'));
    assert(!copy_license(emuenv, work) && emuenv.license_title_id.empty());
    write_file(work, license(cid) + "extra");
    assert(!copy_license(emuenv, work));
    write_file(work, license(cid));
    // A failed swap keeps the original directory; output is never reused.
    const auto original = root / "original";
    write_file(original / "eboot.bin", "keep");
    prevent_commit = true;
    assert(!decrypt_install_nonpdrm(emuenv, work, original, false));
    assert(read_file(original / "eboot.bin") == "keep");
    assert(!fs::exists(fs_utils::path_concat(original, "_dec")));
    // Compile and exercise the real asynchronous license-import worker too.
    prevent_commit = false;
    write_file(work, license(cid));
    write_file(app / "sce_pfs/files.db", "synthetic");
    write_file(vfs / "ux0/patch" / title_id / "sce_pfs/files.db", "synthetic");
    const auto encrypted_dlc = vfs / "ux0/addcont" / title_id / dlc1.substr(20);
    write_file(encrypted_dlc / "sce_pfs/files.db", "synthetic");
    start_license_import(emuenv, work.string());
    wait_license_job();
    assert(g_import_job->success && !fs::exists(work));
    assert(!fs::exists(app / "sce_pfs") && !fs::exists(vfs / "ux0/patch" / title_id / "sce_pfs"));
    assert(fs::exists(encrypted_dlc / "sce_pfs")); // Base license must not decrypt DLC.
    write_file(work, license(dlc1));
    start_license_import(emuenv, work.string());
    wait_license_job();
    assert(g_import_job->success && !fs::exists(encrypted_dlc / "sce_pfs"));
    write_file(work, "truncated");
    start_license_import(emuenv, work.string());
    wait_license_job();
    assert(!g_import_job->success && !fs::exists(work));
    assert(fs::is_empty(vfs / ".install-staging"));
}
