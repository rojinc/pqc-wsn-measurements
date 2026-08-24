#!/usr/bin/env python3
"""
elf2uf2.py — Convert ARM ELF to UF2 format for RP2040.

Reads the ELF file directly, extracts only LOAD segments in the flash
address range, and writes proper UF2 blocks. More reliable than
converting from flat .bin for firmwares with multiple sections.

Usage:
    python scripts/elf2uf2.py build/firmware.elf build/firmware.uf2
"""

import struct
import sys

# UF2 constants
UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END    = 0x0AB16F30
UF2_FLAG_FAMILY  = 0x00002000
RP2040_FAMILY_ID = 0xE48BFF56
PAYLOAD_SIZE     = 256

# RP2040 address ranges
FLASH_START = 0x10000000
FLASH_END   = 0x10200000  # 2MB flash
RAM_START   = 0x20000000
RAM_END     = 0x20042000


def read_elf(path):
    """Parse ELF file and return list of (paddr, data) for LOAD segments in flash."""
    with open(path, "rb") as f:
        elf = f.read()

    # ELF header
    if elf[:4] != b'\x7fELF':
        raise ValueError("Not an ELF file")

    ei_class = elf[4]  # 1=32-bit, 2=64-bit
    if ei_class != 1:
        raise ValueError("Expected 32-bit ELF")

    ei_data = elf[5]  # 1=little-endian
    if ei_data != 1:
        raise ValueError("Expected little-endian ELF")

    # Parse ELF32 header
    (e_type, e_machine, e_version, e_entry, e_phoff, e_shoff,
     e_flags, e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum,
     e_shstrndx) = struct.unpack_from("<HHIIIIIHHHHHH", elf, 16)

    print(f"ELF: entry=0x{e_entry:08x}, {e_phnum} program headers")

    segments = []
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        (p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz,
         p_flags, p_align) = struct.unpack_from("<IIIIIIII", elf, off)

        # PT_LOAD = 1
        if p_type != 1:
            continue

        if p_filesz == 0:
            continue

        # Only include segments with physical address in flash range
        if FLASH_START <= p_paddr < FLASH_END:
            data = elf[p_offset:p_offset + p_filesz]
            segments.append((p_paddr, data))
            print(f"  LOAD segment: paddr=0x{p_paddr:08x} vaddr=0x{p_vaddr:08x} "
                  f"filesz={p_filesz} memsz={p_memsz}")
        else:
            print(f"  Skip segment: paddr=0x{p_paddr:08x} vaddr=0x{p_vaddr:08x} "
                  f"filesz={p_filesz} (not in flash)")

    if not segments:
        raise ValueError("No LOAD segments found in flash range")

    return segments


def segments_to_uf2(segments, output_path):
    """Convert flash segments to UF2 file."""
    # Merge all segments into a flat address map
    # Sort by physical address
    segments.sort(key=lambda s: s[0])

    # Collect all (address, byte) pairs
    flash_data = {}
    for paddr, data in segments:
        for i, byte in enumerate(data):
            flash_data[paddr + i] = byte

    if not flash_data:
        raise ValueError("No flash data to write")

    min_addr = min(flash_data.keys())
    max_addr = max(flash_data.keys())

    print(f"Flash range: 0x{min_addr:08x} - 0x{max_addr:08x} "
          f"({max_addr - min_addr + 1} bytes)")

    # Generate UF2 blocks for each 256-byte page that has data
    # Align to PAYLOAD_SIZE boundaries
    start_page = (min_addr // PAYLOAD_SIZE) * PAYLOAD_SIZE
    end_page = ((max_addr // PAYLOAD_SIZE) + 1) * PAYLOAD_SIZE

    # First pass: count blocks
    pages = []
    for page_addr in range(start_page, end_page, PAYLOAD_SIZE):
        # Check if any byte in this page has data
        has_data = any(
            (page_addr + i) in flash_data
            for i in range(PAYLOAD_SIZE)
        )
        if has_data:
            pages.append(page_addr)

    num_blocks = len(pages)
    print(f"Writing {num_blocks} UF2 blocks")

    # Second pass: generate blocks
    blocks = []
    for block_no, page_addr in enumerate(pages):
        # Build 256-byte payload
        payload = bytearray(PAYLOAD_SIZE)
        for i in range(PAYLOAD_SIZE):
            addr = page_addr + i
            if addr in flash_data:
                payload[i] = flash_data[addr]

        header = struct.pack("<IIIIIIII",
            UF2_MAGIC_START0,
            UF2_MAGIC_START1,
            UF2_FLAG_FAMILY,
            page_addr,
            PAYLOAD_SIZE,
            block_no,
            num_blocks,
            RP2040_FAMILY_ID,
        )
        data_area = bytes(payload) + b'\x00' * (476 - PAYLOAD_SIZE)
        tail = struct.pack("<I", UF2_MAGIC_END)
        blocks.append(header + data_area + tail)

    with open(output_path, "wb") as f:
        for block in blocks:
            f.write(block)

    print(f"Output: {output_path} ({len(blocks)} blocks, "
          f"{len(blocks) * 512} bytes)")


def check_fresh(elf_path):
    """Refuse to convert an ELF older than the sources it was built from.

    This exists because of a real incident: a failed `make` left the previous
    build's ELF in place, this script happily converted it, and the resulting
    UF2 was a DIFFERENT benchmark firmware than the one intended. Flashing that
    would have produced a capture labelled as one scheme containing another
    scheme's data — the worst class of error in this project, because nothing
    downstream would flag it.

    Pass --force to override (e.g. deliberately re-converting an archived ELF).
    """
    import os, time, glob

    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    elf_mtime = os.path.getmtime(elf_path)

    sources = []
    for pat in ("pico/src/*.c", "pico/src/*.h", "pico/CMakeLists.txt",
                "pico/memmap_custom.ld"):
        sources.extend(glob.glob(os.path.join(root, pat)))

    newer = [s for s in sources if os.path.getmtime(s) > elf_mtime]
    if newer:
        age = time.strftime("%Y-%m-%d %H:%M", time.localtime(elf_mtime))
        print(f"\nERROR: {os.path.basename(elf_path)} (built {age}) is OLDER "
              f"than {len(newer)} source file(s):", file=sys.stderr)
        for s in sorted(newer)[:8]:
            print(f"    {os.path.relpath(s, root)}", file=sys.stderr)
        print("\nThe build almost certainly failed and this ELF is stale.\n"
              "Fix the build first — do NOT flash this. Override with --force.\n",
              file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if a != "--force"]
    force = "--force" in sys.argv

    if len(args) != 2:
        print(f"Usage: {sys.argv[0]} [--force] <input.elf> <output.uf2>")
        sys.exit(1)

    if not force:
        check_fresh(args[0])

    segments = read_elf(args[0])
    segments_to_uf2(segments, args[1])
