#!/usr/bin/env python3
"""Print the runtime's recipe hash for every record of a shaders.bin.

Same FNV-1a (32-bit, offset 2166136261, prime 16777619) over each record's bytes as
hash_recipe() in gx_recipes.c, so it matches the "recipe=<8 hex>" in "shader miss" logs.
Usage: recipe-hashes.py shaders.bin
"""
import struct
import sys


def fnv1a(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def main():
    if len(sys.argv) != 2 or sys.argv[1] in ("-h", "--help"):
        sys.exit(__doc__.strip())
    with open(sys.argv[1], "rb") as f:
        data = f.read()
    magic, version, record_bytes = struct.unpack_from("<4sII", data, 0)
    if magic != b"WRSR" or record_bytes == 0:
        sys.exit("not a shaders.bin")
    for at in range(32, len(data) - record_bytes + 1, record_bytes):
        print("%08x" % fnv1a(data[at:at + record_bytes]))


main()
