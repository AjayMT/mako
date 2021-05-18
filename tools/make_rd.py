#!/usr/bin/env python3

"""
Ramdisk builder script.
"""

import os
import sys

DIR_ENTRIES = 16
DIR_ENTRY_NAME = 128
DIR_ENTRY_LEN = 4
DIR_ENTRY_FLAG = 1
DIR_ENTRY_SIZE = DIR_ENTRY_NAME + DIR_ENTRY_LEN + DIR_ENTRY_FLAG
DIR_HEADER_SIZE = DIR_ENTRIES * DIR_ENTRY_SIZE


dirlens = {}


def dirlen(path):
    if os.path.isfile(path):
        return os.path.getsize(path)
    if path in dirlens:
        return dirlens[path]

    size = DIR_HEADER_SIZE
    for file in os.listdir(path):
        size += dirlen(os.path.join(path, file))

    dirlens[path] = size
    return size


def dir_header(path):
    header = list(bytes(DIR_HEADER_SIZE))
    entry_idx = 0
    for file in os.listdir(path):
        relpath = os.path.join(path, file)
        fflag = 1 if os.path.isdir(relpath) else 0
        flen = dirlen(relpath)
        flenbytes = flen.to_bytes(4, "little")
        len_start = entry_idx + DIR_ENTRY_NAME
        len_end = len_start + DIR_ENTRY_LEN
        flag_start = len_end
        flag_end = flag_start + DIR_ENTRY_FLAG
        header[entry_idx : entry_idx + len(file)] = bytes(file, "utf-8")
        header[len_start:len_end] = flenbytes
        header[flag_start:flag_end] = [fflag]
        entry_idx += DIR_ENTRY_SIZE

    return header


def walk_dir(path):
    if os.path.isfile(path):
        f = open(path, "rb")
        return f.read()

    out = dir_header(path)
    for file in os.listdir(path):
        out += walk_dir(os.path.join(path, file))

    return out


def main():
    rdfile = open("iso/modules/rd", "wb")
    rdfile.write(bytes(walk_dir("rdroot")))


if __name__ == "__main__":
    main()
