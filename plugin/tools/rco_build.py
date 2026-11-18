# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds a VSH plugin's resource file (RCO) from a JSON description: pages of objects, and texts
that are the same in every language the system has.

The layout is the one the system's own RCOs have, as RCOMage reads and writes them: a header, the
zlib-packed tables (entries, label hash tables, labels, event names), then one zlib-packed text
block per language. The description names the RCO, its texts and its pages; an object gives its
kind, its name, the attributes that differ from its kind's defaults below, and its children.

Usage: rco_build.py <description.json> <output.rco>
"""

import json
import struct
import sys
import zlib

SIGNATURE = 0x46525000
VERSION = 0x130
ZLIB_PACKED = 0x10
NONE = 0xFFFFFFFF
HEADER_BYTES = 0xA4
# The tables' offsets count from where they start once unpacked: after the header.
TABLES_START = HEADER_BYTES
# The packed tables follow the header and three words: their packed and unpacked sizes, and the
# longest text block.
PACKED_TABLES_START = HEADER_BYTES + 12
ENTRY_BYTES = 0x28
ENTRY = ">BBHIIIIIIIII"
NEXT_FIELD = 0x14
MAIN_TABLE = 1
TEXT_TABLE = 3
OBJECT_TABLE = 8
# A text table's entry for a language: its id, the texts' format (UTF-16), their count.
LANGUAGE_ENTRY = ">HHI"
UTF16 = 1
# The system's languages, in the order explore_plugin's RCO lists them.
LANGUAGES = [6, 1, 2, 4, 5, 0, 9, 7, 8, 3, 11, 10, 14, 12, 15, 13, 16, 17, 18, 19]
# An event's reference names it in the event table; "native:/" events go to the module's handlers.
EVENT_REFERENCE = 0x04000000
NO_REFERENCE = 0xFFFF0000
EVENT_PREFIX = "native:/"

# The attributes every visible object starts with: position, colour, size, scale and layout.
_POSITION = [
    ("posX", "float", 0.0),
    ("posY", "float", 0.0),
    ("posZ", "float", 0.0),
    ("redScale", "float", 1.0),
    ("greenScale", "float", 1.0),
    ("blueScale", "float", 1.0),
    ("alphaScale", "float", 1.0),
    ("width", "float", 0.0),
    ("height", "float", 0.0),
    ("depth", "float", 0.0),
    ("scaleWidth", "float", 1.0),
    ("scaleHeight", "float", 1.0),
    ("scaleDepth", "float", 1.0),
    ("iconOffset", "word", 0x10000),
    ("onLoad", "event", None),
    ("unknown17", "word", 0x10000),
    ("unknown18", "word", 0x10000),
    ("unknown19", "word", 0x10000),
    ("unknownInt20", "word", 0),
    ("unknownInt21", "word", 0),
    ("unknown22", "word", 0),
]

# A text's attributes with the defaults of the Saved Data Utility's delete page (ps3_savedata_plugin,
# page_plugin_delete, text_delete_title): its position, colour, size and layout, the text and its
# font (set in code), and the type's own words.
_TEXT = [
    ("posX", "float", 0.0),
    ("posY", "float", 0.0),
    ("posZ", "float", 0.0),
    ("redScale", "float", 1.0),
    ("greenScale", "float", 1.0),
    ("blueScale", "float", 1.0),
    ("alphaScale", "float", 1.0),
    ("width", "float", 0.0),
    ("height", "float", 0.0),
    ("depth", "float", 0.0),
    ("scaleWidth", "float", 1.0),
    ("scaleHeight", "float", 1.0),
    ("scaleDepth", "float", 1.0),
    ("iconOffset", "word", 0x430000),
    ("onLoad", "event", None),
    ("unknown17", "word", 0x10CF7),
    ("unknown18", "word", 0x410CF9),
    ("unknown19", "word", 0),
    ("unknownInt20", "word", 0x10CFA),
    ("unknownInt21", "word", 0x10CFB),
    ("unknown22", "word", 0x10000),
    ("text", "reference", None),
    ("font", "reference", None),
    ("unknownInt27", "word", 0x1000101),
    ("unknownInt29", "word", 0x1000000),
    ("size", "float", 0.0),
    ("topRed", "float", 1.0),
    ("topGreen", "float", 1.0),
    ("topBlue", "float", 1.0),
    ("bottomRed", "float", 1.0),
    ("bottomGreen", "float", 1.0),
    ("bottomBlue", "float", 1.0),
    ("spacingHorizontal", "float", 0.0),
    ("unknown37", "word", 0),
    ("unknown38", "word", 0),
    ("unknown39", "word", 0),
    ("spacingVertical", "float", 0.0),
    ("shadowX", "float", 4.0),
    ("shadowY", "float", -5.0),
    ("shadowPerspective", "float", 0.0),
    ("shadowRed", "float", 0.25),
    ("shadowGreen", "float", 0.25),
    ("shadowBlue", "float", 0.25),
    ("shadowAlpha", "float", 1.0),
    ("unknown48", "word", 0),
    ("unknown49", "word", 0),
    ("unknown50", "word", 0),
    ("unknownFloat51", "float", 1.0),
    ("unknownFloat52", "float", 1.0),
    ("unknownFloat53", "float", 1.0),
    ("unknownFloat54", "float", 1.0),
    ("unknown55", "word", 0),
    ("unknownInt56", "word", 0x15),
    ("unknownInt57", "word", 3),
    ("unknown58", "word", 0),
]

# Each kind's object type, and its attributes in order with their defaults: the values most
# objects of the system's option menu page (explore_plugin's expage_menu) have, and the texts'
# above.
KINDS = {
    "page": (
        1,
        [
            ("unknownInt0", "word", 0x01110000),
            ("onInit", "event", None),
            ("onCancel", "event", None),
            ("onContextMenu", "event", None),
            ("onActivate", "event", None),
        ],
    ),
    "plane": (2, _POSITION + [("image", "reference", None), ("unknownInt24", "word", 0)]),
    "text": (13, _TEXT),
    "mlist": (
        9,
        _POSITION
        + [
            ("unknown23", "word", 6),
            ("unknown24", "word", 0),
            ("unknown25", "word", NONE),
            ("unknown26", "word", 0),
            ("unknown27", "word", 0),
            ("unknown28", "word", 0),
            ("unknown29", "word", 0),
            ("unknown30", "word", 0),
            ("unknownRef32", "event", None),
            ("onPush", "event", None),
            ("unknownRef35", "event", None),
            ("unknownRef37", "event", None),
            ("unknownRef39", "event", None),
            ("onFocusLeft", "event", None),
            ("onFocusRight", "event", None),
            ("onFocusUp", "event", None),
            ("onFocusDown", "event", None),
            ("onScrollIn", "event", None),
            ("onScrollOut", "event", None),
        ],
    ),
}


def align4(size: int) -> int:
    """The size rounded up to a multiple of four."""
    return (size + 3) & ~3


def padded(data: bytes) -> bytes:
    """The data with zeros up to a multiple of four bytes."""
    return data + b"\0" * (align4(len(data)) - len(data))


def entry(kind: int, subtype: int, label: int, header_size: int, size: int, children: int,
          previous: int, parent: int) -> bytes:
    """An entry's header; its next sibling is set once that is written. A table's own entry has
    no header size and the entry size; an item's has the header size, and its size only when it
    has children."""
    return struct.pack(ENTRY, kind, subtype, 0, label, header_size, size, children, 0, previous,
                       parent, 0, 0)


def label_hash(label: str) -> int:
    """The hash the system looks a label up by: the sum of its bytes."""
    return sum(label.encode("ascii"))


def hash_table(size: int, entries: list) -> list:
    """An open-addressing table of (label, value) pairs, as the system probes it."""
    table = [0] * size
    for label, value in entries:
        slot = label_hash(label) % size
        while table[slot]:
            slot = (slot + 1) % size
        table[slot] = value
    return table


def next_prime(number: int) -> int:
    """The smallest prime that is at least the number, and at least 2."""
    candidate = max(number, 2)
    while any(candidate % divisor == 0 for divisor in range(2, int(candidate**0.5) + 1)):
        candidate += 1
    return candidate


class NameTable:
    """Labels or event names: NUL-ended, each padded to four bytes, by first use."""

    def __init__(self, start: bytes = b""):
        self.data = bytearray(start)
        self.offsets = {}

    def offset(self, name: str) -> int:
        """The name's offset, added when it is new."""
        if name not in self.offsets:
            self.offsets[name] = len(self.data)
            self.data += padded(name.encode("ascii") + b"\0")
        return self.offsets[name]


class RcoWriter:
    """Writes the tables of one RCO; offsets are those of the unpacked file."""

    def __init__(self):
        self.tables = bytearray()
        self.labels = NameTable()
        # Offset 0 is the empty name, which no reference uses.
        self.events = NameTable(b"\0" * 4)
        self.objects = []

    def position(self) -> int:
        """The offset the next table entry gets."""
        return TABLES_START + len(self.tables)

    def set_next(self, at: int, distance: int) -> None:
        """Sets an entry's distance to its next sibling."""
        struct.pack_into(">I", self.tables, at - TABLES_START + NEXT_FIELD, distance)

    def attributes(self, node: dict) -> bytes:
        """An object's attribute words, from its kind's defaults and the description's values."""
        attributes = KINDS[node["kind"]][1]
        values = node.get("attributes", {})
        unknown = set(values) - {name for name, _, _ in attributes}
        if unknown:
            raise ValueError(f"{node['name']}: no such attributes: {sorted(unknown)}")
        body = bytearray()
        for name, kind, default in attributes:
            value = values.get(name, default)
            if kind == "float":
                body += struct.pack(">f", float(value))
            elif kind == "word":
                body += struct.pack(">I", int(value, 0) if isinstance(value, str) else value)
            elif value is None:
                body += struct.pack(">II", NO_REFERENCE, NONE)
            elif kind == "reference":
                raise ValueError(f"{node['name']}: {name} can only be null")
            else:
                body += struct.pack(">II", EVENT_REFERENCE,
                                    self.events.offset(EVENT_PREFIX + value))
        return bytes(body)

    def write_texts(self, main: int, texts: list) -> tuple:
        """The text table: the same texts for each language.

        Returns the table's offset, each language's entry, and each language's text block."""
        table = self.position()
        self.tables += entry(TEXT_TABLE, 0, NONE, 0, ENTRY_BYTES, len(LANGUAGES), 0, table - main)
        languages = []
        blocks = []
        previous = 0
        for index, language in enumerate(LANGUAGES):
            at = self.position()
            data = bytearray(entry(TEXT_TABLE, 1, NONE, ENTRY_BYTES, 0, 0, previous, at - table))
            data += struct.pack(LANGUAGE_ENTRY, language, UTF16, len(texts))
            block = bytearray()
            for label, text in texts:
                units = text.encode("utf-16-be") + b"\0\0"
                data += struct.pack(">III", self.labels.offset(label), len(units), len(block))
                block += padded(units)
            self.tables += data
            languages.append((language, at))
            blocks.append((language, bytes(block)))
            if index < len(LANGUAGES) - 1:
                self.set_next(at, len(data))
                previous = len(data)
        self.set_next(table, self.position() - table)
        return table, languages, blocks

    def write_object(self, node: dict, parent: int, previous: int, last: bool) -> int:
        """An object and its children, depth first.

        Returns its distance to its next sibling: 0 for the last one."""
        kind = KINDS[node["kind"]][0]
        children = node.get("children", [])
        body = self.attributes(node)
        at = self.position()
        size = ENTRY_BYTES + len(body) if children else 0
        self.tables += entry(OBJECT_TABLE, kind, self.labels.offset(node["name"]), ENTRY_BYTES,
                             size, len(children), previous, at - parent)
        self.tables += body
        self.objects.append((node["name"], at))
        child_previous = 0
        for index, child in enumerate(children):
            child_previous = self.write_object(child, at, child_previous,
                                               index == len(children) - 1)
        distance = 0 if last else self.position() - at
        self.set_next(at, distance)
        return distance

    def build(self, description: dict) -> bytes:
        """The whole RCO."""
        texts = list(description["texts"].items())
        main = self.position()
        label = self.labels.offset(description["name"])
        self.tables += entry(MAIN_TABLE, 1, label, 0, ENTRY_BYTES, 2, 0, 0)
        text_table, languages, blocks = self.write_texts(main, texts)
        object_table = self.position()
        pages = description["pages"]
        self.tables += entry(OBJECT_TABLE, 0, NONE, 0, ENTRY_BYTES, len(pages), 0,
                             object_table - main)
        previous = 0
        for index, page in enumerate(pages):
            previous = self.write_object(page, object_table, previous, index == len(pages) - 1)

        text_pointers = self.position()
        for _, at in sorted(languages):
            pointers = [(label, at + ENTRY_BYTES + 8 + 12 * index)
                        for index, (label, _) in enumerate(texts)]
            self.tables += struct.pack(f">{len(texts)}I", *hash_table(len(texts), pointers))
        text_pointer_bytes = 4 * len(texts) * len(languages)
        object_pointers = self.position()
        table = hash_table(next_prime(len(self.objects)), self.objects)
        self.tables += struct.pack(f">{len(table)}I", *table)
        label_start = self.position()
        self.tables += self.labels.data
        event_start = self.position()
        self.tables += self.events.data

        packed = zlib.compress(bytes(self.tables), 9)
        text_data = bytearray()
        for index, (language, block) in enumerate(blocks):
            packed_block = zlib.compress(block, 9)
            following = 0 if index == len(blocks) - 1 else align4(16 + len(packed_block))
            text_data += struct.pack(">HHIII", language, UTF16, following, len(packed_block),
                                     len(block))
            text_data += padded(packed_block)
        header = [
            SIGNATURE, VERSION, 0, ZLIB_PACKED,
            main, NONE, text_table, NONE, NONE, NONE, NONE, NONE, object_table, NONE,
            PACKED_TABLES_START + align4(len(packed)), len(text_data),
            label_start, len(self.labels.data),
            event_start, len(self.events.data),
            text_pointers, text_pointer_bytes,
            NONE, 0, NONE, 0, NONE, 0,
            object_pointers, 4 * len(table),
            NONE, 0, NONE, 0, NONE, 0, NONE, 0,
            NONE, NONE, NONE,
        ]
        longest = max(len(block) for _, block in blocks)
        return (struct.pack(f">{len(header)}I", *header)
                + struct.pack(">III", len(packed), len(self.tables), longest)
                + padded(packed) + bytes(text_data))


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2
    with open(sys.argv[1], encoding="utf-8") as source:
        description = json.load(source)
    with open(sys.argv[2], "wb") as target:
        target.write(RcoWriter().build(description))
    return 0


if __name__ == "__main__":
    sys.exit(main())
