import importlib.util
from pathlib import Path
import plistlib
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "ios_minimum", Path(__file__).resolve().parents[1] / "verify-ios-minimum.py")
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class IOSMinimumTests(unittest.TestCase):
    def test_new_sdk_does_not_raise_runtime_minimum(self):
        checker.verify_build_info("platform IOS\nminos 16.7\nsdk 26.0", "16.7.16")

    def test_legacy_command(self):
        checker.verify_build_info("cmd LC_VERSION_MIN_IPHONEOS\nversion 14.0\nsdk 18.0", "16.7")

    def test_incompatible_or_missing_load_commands(self):
        for info in ("platform IOS\nminos 26.0", "platform IOSSIMULATOR\nminos 16.0",
                     "platform MACOS\nminos 13.0", "platform IOS", "", "platform IOS\nminos bad"):
            with self.subTest(info=info), self.assertRaises(ValueError):
                checker.verify_build_info(info, "16.7")

    def test_patch_versions_are_compared_numerically(self):
        self.assertGreater(checker.version("16.7.16"), checker.version("16.7.9"))
        self.assertEqual(checker.version("16.7"), checker.version("16.7.0"))

    def test_checks_embedded_binary_not_only_app_plist(self):
        with tempfile.TemporaryDirectory() as directory:
            app = Path(directory)
            (app / "Info.plist").write_bytes(plistlib.dumps({
                "MinimumOSVersion": "16.7", "CFBundleExecutable": "App"}))
            (app / "App").write_bytes(bytes.fromhex("cffaedfe"))
            library = app / "Frameworks" / "New.framework" / "New"
            library.parent.mkdir(parents=True)
            library.write_bytes(bytes.fromhex("cffaedfe"))
            def build_info(command, **kwargs):
                minimum = "26.0" if Path(command[-1]) == library else "16.7"
                return f"platform IOS\nminos {minimum}\nsdk 26.0"
            with patch.object(checker.subprocess, "run"), patch.object(
                    checker.subprocess, "check_output", side_effect=build_info):
                with self.assertRaisesRegex(ValueError, "New.framework"):
                    checker.verify_bundle(app, "16.7")

    def test_rejects_newer_plist_before_running_apple_tools(self):
        with tempfile.TemporaryDirectory() as directory:
            app = Path(directory)
            (app / "Info.plist").write_bytes(plistlib.dumps({"MinimumOSVersion": "26.0"}))
            with self.assertRaisesRegex(ValueError, "Info.plist requires"):
                checker.verify_bundle(app, "16.7")

    def test_lipo_receives_only_architectures_after_verify_arch(self):
        with tempfile.TemporaryDirectory(prefix="ios bundle ") as directory:
            app = Path(directory)
            (app / "Info.plist").write_bytes(plistlib.dumps({
                "MinimumOSVersion": "16.7", "CFBundleExecutable": "App"}))
            executable = app / "App"
            executable.write_bytes(bytes.fromhex("cffaedfe"))

            def verify_arch(command, *, check):
                # lipo consumes every argument after -verify_arch as an architecture.
                index = command.index("-verify_arch")
                self.assertEqual(command[index + 1:], ["arm64"])
                self.assertIn(str(executable), command[2:index])
                self.assertTrue(check)

            with patch.object(checker.subprocess, "run", side_effect=verify_arch) as run, \
                    patch.object(checker.subprocess, "check_output",
                                 return_value="platform IOS\nminos 16.7\nsdk 26.2"):
                checker.verify_bundle(app, "16.7")
                run.assert_called_once()

    @unittest.skipUnless(sys.platform == "darwin", "Requires Xcode and the iPhoneOS SDK")
    def test_real_apple_tools_accept_ios_arm64_bundle(self):
        with tempfile.TemporaryDirectory(prefix="ios bundle ") as directory:
            root = Path(directory)
            app = root / "Probe.app"
            app.mkdir()
            (app / "Info.plist").write_bytes(plistlib.dumps({
                "MinimumOSVersion": "16.7", "CFBundleExecutable": "Probe"}))
            source = root / "probe.c"
            source.write_text("int main(void) { return 0; }\n")
            subprocess.run([
                "xcrun", "--sdk", "iphoneos", "clang", "-target", "arm64-apple-ios16.7",
                str(source), "-o", str(app / "Probe"),
            ], check=True)
            checker.verify_bundle(app, "16.7")


if __name__ == "__main__":
    unittest.main()
