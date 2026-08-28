#!/usr/bin/env python3
import argparse
import binascii
import pathlib
import struct

MAGIC = b"EXWPAY1\0"
HEADER_SIZE = 32
LOAD_ADDRESS = 0xAC800000
FLAG_EXECUTABLE = 1
FLAG_RETURNS = 2


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--returns", action="store_true")
    parser.add_argument("--append-blob", type=pathlib.Path)
    parser.add_argument("--append-offset", type=lambda value: int(value, 0))
    parser.add_argument("--append-capacity", type=lambda value: int(value, 0))
    parser.add_argument("input", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()

    payload = args.input.read_bytes()
    append_options = (args.append_blob, args.append_offset,
                      args.append_capacity)
    if any(option is not None for option in append_options):
        if not all(option is not None for option in append_options):
            parser.error("--append-blob, --append-offset and "
                         "--append-capacity must be used together")
        blob = args.append_blob.read_bytes()
        if len(payload) > args.append_offset:
            parser.error(
                f"kernel is {len(payload)} bytes; it overlaps blob offset "
                f"{args.append_offset:#x}"
            )
        if len(blob) > args.append_capacity:
            parser.error(
                f"blob is {len(blob)} bytes; capacity is "
                f"{args.append_capacity:#x}"
            )
        payload += bytes(args.append_offset - len(payload)) + blob
    flags = FLAG_EXECUTABLE | (FLAG_RETURNS if args.returns else 0)
    checksum = binascii.crc32(payload) & 0xFFFFFFFF
    header = struct.pack(
        ">8s6I",
        MAGIC,
        HEADER_SIZE,
        LOAD_ADDRESS,
        LOAD_ADDRESS,
        len(payload),
        checksum,
        flags,
    )
    args.output.write_bytes(header + payload)
    print(f"{args.output}: {len(payload)} payload bytes, CRC32 {checksum:08x}")


if __name__ == "__main__":
    main()
