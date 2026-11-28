#!/usr/bin/env python3
"""One line to build, flash and watch any ICVSP firmware. Run it in an ESP-IDF terminal (VS Code:
"ESP-IDF: Open ESP-IDF Terminal"), from this folder (1_XAIO_SW/code):

    python build.py prod                          build the production firmware (the one for the car)
    python build.py prod flash monitor -p COM5    build, flash and watch it (port: Windows COM5,
                                                  macOS /dev/cu.usbmodem1101, Linux /dev/ttyACM0)
    python build.py camera flash monitor -p COM5  a test firmware
    python build.py all                           build every firmware (checks that everything compiles)
    python build.py                               list the build types

After the type, any idf.py actions can follow (build, flash, monitor, size, menuconfig, fullclean). With no action
it builds. Each type builds in its own folder (build_prod, build_dev, build_camera, ...). The types and their
settings come from CMakePresets.json, the same list VS Code shows. See 1_XAIO_SW/doc/BUILD_AND_FLASH.md.
"""
import json
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
LOCAL = "sdkconfig.local"


def presets() -> dict:
    data = json.loads((HERE / "CMakePresets.json").read_text())
    return {p["name"]: p for p in data["configurePresets"]}


def idf_command(name: str, preset: dict, actions: list[str], compile_check: bool = False) -> list[str]:
    cache = preset["cacheVariables"]
    layers = cache["SDKCONFIG_DEFAULTS"].split(";")
    if LOCAL in layers and not (HERE / LOCAL).exists():
        if compile_check:
            # 'all' only checks that everything compiles: the example's placeholder values are enough
            layers[layers.index(LOCAL)] = "sdkconfig.local.example"
        elif name == "report":
            sys.exit(f"{LOCAL} is missing: copy sdkconfig.local.example to {LOCAL} and put the Wi-Fi name, "
                     "password and backend URL in it (the report firmware needs them).")
        else:
            print(f"note: no {LOCAL}, so this build has no Wi-Fi: reports go to the phone only")
            layers.remove(LOCAL)
    build_dir, sdkconfig = preset["binaryDir"], cache["SDKCONFIG"]
    if compile_check:
        # 'all' builds in its own folders (build_check_prod, ...), so its placeholder Wi-Fi values never end up in
        # the real build folders: ESP-IDF keeps the settings of an existing build folder.
        build_dir = build_dir.replace("build", "build_check", 1)
        sdkconfig = sdkconfig.replace("build", "build_check", 1)
    idf = Path(os.environ["IDF_PATH"]) / "tools" / "idf.py"
    return [sys.executable, str(idf), "-B", build_dir, "-D", f"SDKCONFIG={sdkconfig}",
            "-D", f"SDKCONFIG_DEFAULTS={';'.join(layers)}", *actions]


def main() -> int:
    table = presets()
    args = sys.argv[1:]
    if not args or args[0] in ("-h", "--help", "help", "list"):
        print(__doc__)
        print("Build types:")
        for name, p in table.items():
            print(f"  {name:8} {p['displayName']}")
        return 0
    if "IDF_PATH" not in os.environ:
        sys.exit("ESP-IDF is not set up in this terminal. In VS Code run \"ESP-IDF: Open ESP-IDF Terminal\" "
                 "(or run ESP-IDF's export.sh / export.ps1 first), then try again.")
    name, actions = args[0], args[1:] or ["build"]
    names = list(table) if name == "all" else [name]
    for n in names:
        if n not in table:
            sys.exit(f"unknown build type '{n}'. Types: {', '.join(table)}, all")
    if name == "all" and set(actions) - {"build"}:
        sys.exit("'all' only builds; flash one type at a time")
    failed = []
    for n in names:
        print(f"\n=== {n}: {table[n]['displayName']}", flush=True)
        if subprocess.run(idf_command(n, table[n], actions, compile_check=name == "all"), cwd=HERE).returncode != 0:
            failed.append(n)
    if len(names) > 1:
        print("\n=== result: " + ("all built" if not failed else f"FAILED: {', '.join(failed)}"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
