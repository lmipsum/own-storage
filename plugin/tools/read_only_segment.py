# SPDX-License-Identifier: GPL-3.0-or-later
"""Makes a module's only segment read-only and executable, as the PS3 loads no writable and
executable one; prx-gen writes one segment with all three rights.

Usage: read_only_segment.py <module.prx> <output.prx>
"""

import struct
import sys

PT_LOAD = 1
# Read and execute, for the ELF and for the PPU, as the system's own modules mark their code.
READ_EXECUTE = 0x400005
# The ELF flags the system's own modules carry.
MODULE_FLAGS = 0x1000000


def read_only(module: bytes) -> bytes:
    """The module with its first program header read-only; it must be its one loadable segment."""
    elf = bytearray(module)
    if elf[:4] != b"\x7fELF" or elf[4] != 2 or elf[5] != 2:
        raise ValueError("not a big-endian 64-bit ELF")
    (phoff,) = struct.unpack_from(">Q", elf, 32)
    (phnum,) = struct.unpack_from(">H", elf, 56)
    loads = [i for i in range(phnum) if struct.unpack_from(">I", elf, phoff + i * 56)[0] == PT_LOAD]
    if loads != [0]:
        raise ValueError("expected one loadable segment, first among the program headers")
    struct.pack_into(">I", elf, 48, MODULE_FLAGS)
    struct.pack_into(">I", elf, phoff + 4, READ_EXECUTE)
    return bytes(elf)


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2
    with open(sys.argv[1], "rb") as source:
        module = source.read()
    with open(sys.argv[2], "wb") as target:
        target.write(read_only(module))
    return 0


if __name__ == "__main__":
    sys.exit(main())
