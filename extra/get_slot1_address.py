#!/usr/bin/env python3
import re
import sys


def extract_absolute_slot1_address(dts_file_path):
    try:
        with open(dts_file_path, "r") as f:
            content = f.read()
    except FileNotFoundError:
        sys.exit(1)

    # 1. Find the base address of flash0
    # Matches 'flash0:' and captures its first 'reg = < 0x...' value
    flash0_pattern = re.compile(r"flash0:.*?reg\s*=\s*<\s*(0x[0-9a-fA-F]+)", re.DOTALL)
    flash0_match = flash0_pattern.search(content)

    # 2. Find the relative offset of slot1_partition
    slot1_pattern = re.compile(
        r"slot0_partition[a-zA-Z0-9_@]*:.*?reg\s*=\s*<\s*(0x[0-9a-fA-F]+)", re.DOTALL
    )
    slot1_match = slot1_pattern.search(content)

    if flash0_match and slot1_match:
        # Convert both string hex values to integers
        flash0_addr = int(flash0_match.group(1), 16)
        slot1_offset = int(slot1_match.group(1), 16)

        # Calculate absolute address
        absolute_addr = flash0_addr + slot1_offset

        # Print formatted as uppercase hex (e.g., 0x8020000)
        print(f"0x{absolute_addr:X}")
        sys.exit(0)

    # Return error if either property is missing
    sys.exit(1)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(1)

    extract_absolute_slot1_address(sys.argv[1])
