#!/usr/bin/env python3
"""Count statically declared MessageBus subscriptions."""

from __future__ import annotations

import argparse
import re
import sys
from collections import Counter
from pathlib import Path

CPP_FILE_SUFFIXES = {".cpp", ".hpp", ".cc", ".h"}
SUBSCRIPTION_PATTERN = re.compile(r"\bsubscribe\s*<\s*([A-Za-z_]\w*(?:::\w+)*)\s*>")
CAPACITY_PATTERN = re.compile(r"MAX_SUBSCRIPTIONS\s*=\s*(\d+)")


def read_capacity(message_bus_header: Path) -> int:
    match = CAPACITY_PATTERN.search(message_bus_header.read_text(encoding="utf-8"))
    if match is None:
        raise ValueError(f"Could not read MAX_SUBSCRIPTIONS from {message_bus_header}")
    return int(match.group(1))


def find_subscriptions(source_directory: Path) -> Counter[str]:
    subscriptions: Counter[str] = Counter()
    for source_file in source_directory.rglob("*"):
        if source_file.suffix not in CPP_FILE_SUFFIXES:
            continue
        for message_type in SUBSCRIPTION_PATTERN.findall(source_file.read_text(encoding="utf-8")):
            subscriptions[message_type.rsplit("::", maxsplit=1)[-1]] += 1
    return subscriptions


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path("main"))
    parser.add_argument("--message-bus-header", type=Path, default=Path("main/message_bus.hpp"))
    arguments = parser.parse_args()

    try:
        capacity = read_capacity(arguments.message_bus_header)
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2

    subscriptions = find_subscriptions(arguments.source)
    total = sum(subscriptions.values())
    width = max([len(message_type) for message_type in subscriptions] + [len("Total")])

    print("Message Bus Subscriptions\n")
    for message_type, count in sorted(subscriptions.items()):
        print(f"{message_type:<{width}}  {count}")
    print("-" * (width + 10))
    print(f"{'Total':<{width}}  {total} / {capacity}")

    if total > capacity:
        print("\nERROR: MessageBus subscription capacity exceeded!", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
