#!/usr/bin/env python3
"""Reject bundles whose metadata or Mach-O files require a newer iOS release."""
import argparse
import plistlib
from pathlib import Path
import re
import subprocess


def version(value):
    if not re.fullmatch(r"\d+\.\d+(?:\.\d+)?", value):
        raise ValueError(f"Invalid OS version: {value!r}")
    parts = tuple(map(int, value.split(".")))
    return parts + (0,) * (3 - len(parts))


def verify_build_info(info, maximum):
    # vtool prints LC_BUILD_VERSION for current binaries and the legacy load
    # command for older libraries. The SDK version is deliberately irrelevant.
    platforms = re.findall(r"^\s*platform\s+(\S+)", info, re.MULTILINE)
    if platforms:
        if any(platform != "IOS" for platform in platforms):
            raise ValueError("Expected an iOS device binary")
        minimums = re.findall(r"^\s*minos\s+(\S+)", info, re.MULTILINE)
    elif "LC_VERSION_MIN_IPHONEOS" in info:
        minimums = re.findall(r"^\s*version\s+(\S+)", info, re.MULTILINE)
    else:
        raise ValueError("Missing iOS deployment load command")
    if not minimums or any(version(item) > version(maximum) for item in minimums):
        raise ValueError(f"Binary requires {minimums or 'unknown iOS'}; maximum is {maximum}")


def verify_bundle(app, maximum):
    with (app / "Info.plist").open("rb") as stream:
        plist = plistlib.load(stream)
    minimum = plist.get("MinimumOSVersion", "")
    if version(minimum) > version(maximum):
        raise ValueError(f"Info.plist requires iOS {minimum}; maximum is {maximum}")
    executable = app / plist["CFBundleExecutable"]
    if not executable.is_file():
        raise ValueError(f"Missing executable: {executable}")

    # Check embedded frameworks/dylibs as well as the main executable. A lower
    # app minimum alone cannot make a library built for iOS 26 load on iOS 16.
    magic_numbers = {bytes.fromhex(value) for value in (
        "cffaedfe", "feedfacf", "cefaedfe", "feedface",
        "cafebabe", "bebafeca", "cafebabf", "bfbafeca",
    )}
    binaries = {executable}
    for path in app.rglob("*"):
        if path.is_file():
            with path.open("rb") as stream:
                if stream.read(4) in magic_numbers:
                    binaries.add(path)
    for path in sorted(binaries):
        subprocess.run(["xcrun", "lipo", str(path), "-verify_arch", "arm64"], check=True)
        info = subprocess.check_output(
            ["xcrun", "vtool", "-arch", "arm64", "-show-build", str(path)], text=True)
        try:
            verify_build_info(info, maximum)
        except ValueError as error:
            raise ValueError(f"{path}: {error}") from error
    print(f"Verified {len(binaries)} arm64 binaries and Info.plist for iOS {maximum}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path)
    parser.add_argument("--maximum", default="16.7")
    args = parser.parse_args()
    verify_bundle(args.app, args.maximum)
