#!/usr/bin/env python3

import argparse
import subprocess
from pathlib import Path


def git_output(*arguments: str) -> str:
    return subprocess.run(
        ["git", *arguments], check=True, capture_output=True, text=True
    ).stdout.strip()


def firmware_version() -> str:
    commit = git_output("rev-parse", "--short", "HEAD")
    if git_output("status", "--porcelain"):
        return f"{commit} [DIRTY]"

    try:
        return git_output("describe", "--tags", "--exact-match", "HEAD")
    except subprocess.CalledProcessError:
        return commit


def cpp_string(value: str) -> str:
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=Path)
    arguments = parser.parse_args()

    version = firmware_version()
    contents = "\n".join(
        (
            "#ifndef REFLOWCTRL_FIRMWARE_VERSION_HPP",
            "#define REFLOWCTRL_FIRMWARE_VERSION_HPP",
            "",
            "namespace reflowCtrl {",
            f"inline constexpr char FIRMWARE_VERSION[] = {cpp_string(version)};",
            "}  // namespace reflowCtrl",
            "",
            "#endif  // REFLOWCTRL_FIRMWARE_VERSION_HPP",
            "",
        )
    )
    if not arguments.output.exists() or arguments.output.read_text(encoding="utf-8") != contents:
        arguments.output.write_text(contents, encoding="utf-8")
    print(f"Firmware version: {version}")


if __name__ == "__main__":
    main()
