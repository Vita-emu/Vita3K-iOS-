import importlib.util
from pathlib import Path
import plistlib
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


if __name__ == "__main__":
    unittest.main()
