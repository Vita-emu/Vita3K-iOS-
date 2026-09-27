"""Compile the frontend's actual queue and import deferral blocks."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest


class SettingsQueueTests(unittest.TestCase):
    def test_save_before_launch_and_import_deferral(self):
        root = Path(__file__).resolve().parents[2]
        native = (root / "ios/src/NativeFrontend.mm").read_text()
        start = native.index("std::mutex g_action_mutex;")
        queue = native[start:native.index("// Last snapshot", start)]
        start = native.index("std::optional<Vita3KIOSFrontendAction> vita3k_ios_take_frontend_action()")
        queue += native[start:native.index("\nint vita3k_ios_load_fps_limit", start)]
        main = (root / "ios/src/UpstreamMain.cpp").read_text()
        start = main.index("                if (g_import_job && action->kind == Vita3KIOSFrontendActionKind::ApplySettings)")
        defer = main[start:main.index("                if (g_import_job && !g_import_job->done.load()", start)]
        start = main.index("                if (deferred_settings) {")
        apply = main[start:main.index("                if (rescan_apps", start)]
        fixture = r'''
#include <atomic>
#include <cassert>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
enum class Vita3KIOSFrontendActionKind { ApplySettings, Launch };
struct Vita3KIOSSettings { int value; };
struct Vita3KIOSFrontendAction { Vita3KIOSFrontendActionKind kind; Vita3KIOSSettings settings; };
// QUEUE
struct Job { std::atomic<bool> done{false}; };
static std::unique_ptr<Job> g_import_job;
static std::optional<Vita3KIOSSettings> deferred_settings;
static int emuenv=0, games=0;
static void vita3k_ios_report_import_result(const char*,bool) {}
static void apply_native_settings(int &env,Vita3KIOSSettings s) {env=s.value;}
static int native_settings(int env) {return env;}
static void vita3k_ios_update_library(int,int) {}
static void save(Vita3KIOSSettings settings) {
    auto action=std::make_optional(Vita3KIOSFrontendAction{Vita3KIOSFrontendActionKind::ApplySettings,settings});
    // DEFER
    apply_native_settings(emuenv,settings);
}
static void complete() {
    g_import_job.reset();
    // APPLY
}
int main() {
    using Kind=Vita3KIOSFrontendActionKind;
    queue_action({Kind::ApplySettings,{1}});
    queue_action({Kind::ApplySettings,{2}});
    queue_action({Kind::Launch,{0}});
    queue_action({Kind::ApplySettings,{3}});
    auto action=vita3k_ios_take_frontend_action();
    assert(action && action->kind==Kind::ApplySettings && action->settings.value==2);
    action=vita3k_ios_take_frontend_action();assert(action && action->kind==Kind::Launch);
    action=vita3k_ios_take_frontend_action();assert(action && action->settings.value==3);
    assert(!vita3k_ios_take_frontend_action());
    g_import_job=std::make_unique<Job>();
    save({4});save({5});assert(emuenv==0 && deferred_settings->value==5);
    g_import_job->done=true; // Completes between the completion check and taking an action.
    save({6});assert(emuenv==0 && deferred_settings->value==6);
    complete();assert(emuenv==6 && !deferred_settings);
    save({7});assert(emuenv==7);
}
'''.replace("// QUEUE", queue).replace("// DEFER", defer).replace("// APPLY", apply)
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "settings.cpp"
            source.write_text(fixture)
            binary = Path(directory) / "settings"
            subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
                "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pthread", str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
