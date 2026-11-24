#!/usr/bin/env python3
"""Set the sdk field of LC_BUILD_VERSION / LC_VERSION_MIN_IPHONEOS in every slice of a Mach-O.
Usage: set_sdk.py <binary> <x.y.z>   (re-sign afterwards)"""
import struct, sys

def pack(s):
    p = [int(x) for x in (s.split('.') + ['0', '0'])[:3]]
    return (p[0] << 16) | (p[1] << 8) | p[2]

def main(path, sdk):
    data = bytearray(open(path, 'rb').read())
    magic = struct.unpack_from('>I', data, 0)[0]
    if magic in (0xcafebabe, 0xcafebabf):
        n = struct.unpack_from('>I', data, 4)[0]
        bases = [struct.unpack_from('>I', data, 8 + i * 20 + 8)[0] if magic == 0xcafebabe
                 else struct.unpack_from('>Q', data, 8 + i * 32 + 8)[0] for i in range(n)]
    elif struct.unpack_from('<I', data, 0)[0] == 0xfeedfacf:
        bases = [0]
    else:
        sys.exit("not a 64-bit Mach-O")
    for base in bases:
        ncmds = struct.unpack_from('<I', data, base + 16)[0]
        off, done = base + 32, False
        for _ in range(ncmds):
            cmd, size = struct.unpack_from('<II', data, off)
            if cmd == 0x32:
                struct.pack_into('<I', data, off + 16, pack(sdk)); done = True
            elif cmd == 0x25:
                struct.pack_into('<I', data, off + 12, pack(sdk)); done = True
            off += size
        if not done:
            sys.exit(f"slice at {base:#x}: no SDK load command")
    open(path, 'wb').write(data)
    print(f"{path}: sdk -> {sdk} ({len(bases)} slice(s))")

if __name__ == '__main__':
    main(*sys.argv[1:3])
