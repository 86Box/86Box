# IBM 5550 Font Card ROMs

The IBM 5550 Multistation renders CJK glyphs from an optional **font card**, a
512 KB page ROM exposed behind a 32 KB window at `F0000h`. The card images that
86Box loads from `roms/machines/ibm5550/` are **reconstructions**, not dumps of
IBM parts: they are rebuilt from the font files shipped on the DOS-K / DOS/V /
HANZI DOS diskettes. This document describes the card layout and how each image
is generated.

## Card layout (512 KB = 16 pages × 32 KB)

| Region | Offset | Size | Meaning |
| --- | --- | --- | --- |
| Pointer table | `0x0000` | `0x5820` (22560 B) | 11280 entries, 2 bytes each, big-endian page-offset |
| Attribute table | `0x5820` | `0x1608` (5640 B) | 11280 entries, 4 bits each (page number) |
| T2 table | `0x6E32` | `0x800` (2048 B) | Codepoint mapping table (second decode stage) |
| T1 table | `0x763C` | `0x108` (264 B) | Bit-length prefix table (first decode stage) |
| Glyph pages | page 1..10 | — | 32 KB pages holding the glyph records |

A pointer-table entry for cell `n` is the page-internal offset of the glyph
record; the matching attribute nibble is the page number. The cell for a font
codepoint `key` is `key − 0x100`. Cells `0..11279` span the full map; the fonts
below use only a prefix of it (largest cell `0x2B5F`).

## Source file formats

The diskette fonts come in two layouts:

**DOS/V "family A"** — self-contained; the tables live in the file:

| Region | Offset | Size | Meaning |
| --- | --- | --- | --- |
| T1 table | `0x00C0` | `0x108` (264 B) | Bit-length prefix table |
| T2 table | `0x0200` | `0x800` (2048 B) | Codepoint mapping table |
| Glyph record stream | varies | — | `[key: u16 LE][len: u8][payload: len]` runs |

**Raw (IBM DOS-K 3.44 / HANZI DOS P2.61)** — starts directly with T2 and has
**no T1 table**: the operating system supplies T1 from its `IBMBIO`.

| Region | Offset | Size | Meaning |
| --- | --- | --- | --- |
| T2 table | `0x0000` | `0x800` (2048 B) | Codepoint mapping table |
| Glyph record stream | varies | — | `[key: u16 LE][len: u8][payload: len]` runs |

`rebuild_card.py` classifies the file automatically: if a T1-shaped 264-byte
block sits at `0x00C0` the font is treated as family A; otherwise it is treated
as raw and **`--ibmbio` is required**, so T1 can be taken from the DOS. The T1
table is located inside the IBMBIO by its leading bytes (`008a8c8b` for 16-dot,
`008b7a7a` for 24-dot); pass `--t1-offset HEX` if that search is not unique.

The record stream start differs per source and is found by probing:

| Source font file | Layout | Dot | Stream start | Records |
| --- | --- | --- | --- | --- |
| DOS-K 3.44 16-dot (`$SYS1Z16.FNT`) | raw | 16 | `0x1800` | 3900 |
| DOS-K 3.44 24-dot (`$SYS1Z24.FNT`) | raw | 24 | `0x1800` | 3516 |
| DOS/V J7.0 24-dot (`_JPNZN24.FNT`) | family A | 24 | `0x1800` | 7267 |
| HANZI DOS P2.61 24-dot (`$SYS1Z24.FNT`) | raw | 24 | `0x2800` | 7445 |

A record's payload is not a raw bitmap: it is decoded in two stages. Stage 1
expands a variable-bit-length stream using the T1 table; stage 2 applies the T2
codepoint mapping. Only after both stages is the result a `32`-byte (16-dot) or
`72`-byte (24-dot) glyph. T2 must come from the same font file as the records;
T1 may come from that file (family A) or from the DOS's IBMBIO (raw layout).

## Generation recipe

The generator is `tests/machine/rebuild_card.py`. For a given source file it:

1. Starts from the real 5535-M ROM `roms/machines/ibm5535/65X1460.BIN` — passed
   as the final `build` argument — as the base (its header layout and
   unused-page fill are preserved).
2. Zeroes the pointer table `0x0000..0x5820` and the attribute table
   `0x5820..0x6E28`.
3. Copies T2 to `0x6E32` and T1 to `0x763C`. For a family-A font both come from
   the file (`0x0200` and `0x00C0`); for a raw font T2 comes from file offset
   `0x0000` and T1 is taken from the IBMBIO passed with `--ibmbio`.
4. Walks the record stream from the given start, placing each glyph payload
   consecutively into the glyph pages. 16-dot images use pages `1,2,3,4,6`;
   24-dot images use pages `1,2,3,4,5,6,7,8,10`. The 16-dot ANK page (5) and
   the 24-dot ANK page (9) are skipped.
5. For each cell writes the page-internal offset into the pointer table and the
   page number into the attribute nibble.
6. Re-decodes every cell through stage1/stage2 and compares it byte-for-byte
   against the source record (a 100 % self-check before the image is written).

```
python rebuild_card.py probe <src>                          # find the stream start
python rebuild_card.py build <src> <16|24> <start_hex> <out> <base_rom> \
                            [--ibmbio IBMBIO] [--t1-offset HEX] [--patch-7ff7 HEX]
```

`FONTP24.BIN` also passes `--patch-7ff7 0AA00031` (see below).

## Generated images

`Occupied cells` is the number of non-zero pointer-table entries measured from
the built card; the source `Records` above are the raw codepoints in the font
file, which differ slightly (a few codepoints get page-offset 0 and so count as
empty).

| File | Source | Dot | Records | Occupied cells | md5 (12 hex) | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| `FONTK16.BIN` | DOS-K 3.44 `$SYS1Z16.FNT` | 16 | 3900 | 3897 | `155340fb369c` | Japanese, level-1+α |
| `FONTK24.BIN` | DOS-K 3.44 `$SYS1Z24.FNT` | 24 | 3516 | 3512 | `27360ce9e7ad` | Japanese, level-1+α |
| `FONTK52.BIN` | DOS/V J7.0 `_JPNZN24.FNT` | 24 | 7267 | 7258 | `18fd4a1810c2` | Japanese, full (level-1+2), 5250 emulation |
| `FONTP24.BIN` | HANZI DOS P2.61 `$SYS1Z24.FNT` | 24 | 7445 | 7436 | `e620c584c384` | Simplified Chinese (ShiftGB) |

The Japanese fonts use the IBMJ codepoint space, shared by the 5550 and DOS/V,
so their font files differ mainly by the header layout. The Chinese font uses
the 5550-specific **ShiftGB** space, so it must be built from the P2.61 disk's
own font file and cannot reuse a DOS/V Chinese font.

### Card/DOS pairing constant

The HANZI DOS P2.61 `IBMBIO` verifies that the card's page 0 at `0x7FF7`
matches a constant taken from its own code. The Chinese card therefore has
`0AA00031` written at `F000:7FF7`; the Japanese cards leave `065F1460`
unchanged (the Japanese DOS performs no such check).

## Validation

The self-check inside `rebuild_card.py` confirms every glyph round-trips
(`3900/3900`, `3516/3516`, `7267/7267`, `7445/7445` for the four cards above).

An emulator boot check (boot the DOS, then compare the decoded DBCS slot region
at physical `A0000` byte-for-byte against the reference state where the DOS
loads its font from floppy) was performed for the earlier revision of the
Japanese cards (`font` selections `1`, `2`, `3`) and for the Chinese card
(`6`). The Japanese 16- and 24-dot cards have since been regenerated from the
DOS-K 3.44 fonts and have not yet been re-checked this way.
