#!/usr/bin/env python3

import os
import shutil
from pathlib import Path

SDK_NAME = "PocketDarwin01.sdk"
ARCH = "arm64"
TARGET = "arm64-apple-darwin"

ROOT = Path(SDK_NAME)
SRC = Path("sdk_sources")

def mkdir(p):
    p.mkdir(parents=True, exist_ok=True)

def write_plist():
    plist = f"""<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
 "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CanonicalName</key>
    <string>pocketdarwin01</string>

    <key>Version</key>
    <string>0.1</string>

    <key>DefaultArchitecture</key>
    <string>{ARCH}</string>

    <key>SupportedArchitectures</key>
    <array>
        <string>{ARCH}</string>
    </array>

    <key>PlatformName</key>
    <string>PocketDarwin</string>
</dict>
</plist>
"""
    (ROOT / "SDKSettings.plist").write_text(plist)

def write_tbd(libname, symbols):
    tbd = f"""--- !tapi-tbd
tbd-version:     4
targets:         [ {TARGET} ]
install-name:    /usr/lib/{libname}.dylib
exports:
  - archs:       [ {ARCH} ]
    symbols:
"""
    for s in symbols:
        tbd += f"      - {s}\n"

    tbd += "...\n"

    out = ROOT / "System/usr/lib" / f"{libname}.tbd"
    out.write_text(tbd)

def main():
    print(f"[+] Creating {SDK_NAME}")

    mkdir(ROOT)
    mkdir(ROOT / "System/Library/Frameworks")
    mkdir(ROOT / "System/usr/include")
    mkdir(ROOT / "System/usr/lib")

    # Copy headers
    hdr_src = SRC / "headers"
    if hdr_src.exists():
        print("[+] Copying headers")
        shutil.copytree(hdr_src, ROOT / "System/usr/include", dirs_exist_ok=True)

    # Copy frameworks
    fw_src = SRC / "frameworks"
    if fw_src.exists():
        for fw in fw_src.iterdir():
            print(f"[+] Framework {fw.name}")
            dst = ROOT / "System/Library/Frameworks" / f"{fw.name}.framework"
            mkdir(dst)
            shutil.copytree(fw, dst / "Headers", dirs_exist_ok=True)

    # Write SDKSettings.plist
    write_plist()

    # Generate stub libraries
    sym_src = SRC / "symbols"
    if sym_src.exists():
        for symfile in sym_src.iterdir():
            lib = symfile.stem
            symbols = [
                line.strip()
                for line in symfile.read_text().splitlines()
                if line.strip()
            ]
            print(f"[+] Stub {lib}")
            write_tbd(lib, symbols)

    print("[✓] SDK created successfully")

if __name__ == "__main__":
    main()
