# PC98 source provenance audit

The available evidence supports describing this project as **86Box with a
distinct PC-98 implementation**. This review found no evidence of a bulk port
of the PC-98 core from the two NP2-family source snapshots examined. It does
find explicit NP2 influence on particular compatibility decisions, shared
third-party libraries, and acknowledged MAME influence on font reconstruction.
An assertion of complete independence from all other emulators would exceed
the evidence.

AI assistance does not itself establish source ancestry. An allegation that
this is an NP2 port needs implementation-level evidence, with inherited
86Box code, shared libraries, and hardware-defined constants accounted for.

## Scope and reference versions

The audited checkout had HEAD
`630518a54cd90fbc67f53e5f47da26353c056a18` and existing uncommitted edits.
The comparison includes those edits; it is not a claim about only the published
branch. Individual core-file SHA-256 hashes and the working-tree status are in
[the evidence JSON](pc98-provenance-evidence.json).

The automated comparison covered:

- All 1,741 source/header files under `src` with extensions `.c`, `.cc`,
  `.cpp`, `.h`, `.hpp`, `.ipp` or `.inl`, for whole-file token equality.
- 25 current PC-98 core and header files, including D88 and uPD7220, for
  contiguous token comparisons. These contain 72,351 tokens after comments
  and whitespace are removed.
- The 23 corresponding files present in the first PC-98 commit,
  `70aa49b32239623efe4eb61272a44ce55ffa9a3d`, so a subsequent rewrite cannot
  by itself explain the results. The original 8251/8255 paths were under
  `src/machine`; their current paths are under `src/device`.

Reference archives were pinned rather than fetched from moving branches:

| Reference | Revision | Source/header files |
| --- | --- | ---: |
| [NP2kai](https://github.com/AZO234/NP2kai/tree/5939e0c6d5985c4c08fc70f289a83290e5d3e6f7) | `5939e0c6d5985c4c08fc70f289a83290e5d3e6f7` | 1,733 |
| [NP21/W source mirror](https://github.com/roytam1/np21w/tree/e77b1a47962f89c6a8a7ddcc1b5e421f9ba4418a) | `e77b1a47962f89c6a8a7ddcc1b5e421f9ba4418a`, import of rev105beta6 | 1,419 |

The second reference is an unofficial source mirror. The author's
[distribution page](https://simk98.github.io/np21w/download.html) identifies
NP21/W and its source-inclusive releases; the author's website repository
does not contain the emulator source. These comparisons do not cover every
historical version of original NP2 or every fork.

## Direct evidence of the 86Box foundation

The initial import, `15036ee658fc2101d4c5d1ae39ba2f0db29ea7ad`, was compared
with the local Git object for
[86Box revision 571ab627](https://github.com/86Box/86Box/commit/571ab627fdb86d42dc9b985aef239bef8957737a).
**1,684 of 1,686 source/header files have exactly the same Git blob IDs.**
The only additions in this extension set are `vid_upd7220.c` and its header;
no source/header files in the set were deleted. The narrower set of `.c`,
`.cpp`, `.h`, `.hpp` and `.ipp` files gives 1,598 identical files out of 1,600.
These are two counts of the same import using different extension sets.

This establishes the imported foundation directly, rather than inferring it
from naming, style, or README branding. It does not alone exclude an NP2
device port on top of that foundation; the comparisons below address that
separate possibility.

The first PC-98 commit adds the machine, video, sound, storage, and peripheral
implementations to that import. Its CPU-directory diff adds just 16 lines to
`src/cpu/x86.c`; it does not replace the existing CPU implementation. Later
history records a merge of newer 86Box code and adaptation to 86Box's changed
floppy APIs. The source and build definitions continue to integrate PC-98
through 86Box's existing subsystems.

## Implementation differences examined

The following are concrete differences in the audited code and references.
Their combination supports a distinct implementation; any one difference
could also occur during a port and is not conclusive alone.

| Area | Audited implementation | NP2-family comparison |
| --- | --- | --- |
| Machine integration | `m_pc98.c` registers handlers with `io_sethandler`, instantiates 86Box PIT/FDC/video devices, and uses 86Box PIC, DMA, memory and timer APIs. | Reference devices use the NP2 `pccore`, `iocore`, `nevent`, and CPU-clock infrastructure. |
| GDC | `vid_upd7220.h` defines callback-driven `upd7220_t` instances, a 16-entry ring FIFO, decoded timing state and drawing registers. | `io/gdc.h` defines `_GDCDATA` with `para[256]` and `fifo[GDCCMD_MAX]`, where `GDCCMD_MAX` is 32, within global master/slave GDC state. |
| EGC | `vid_pc98.c:96` computes the eight-term raster truth table with scalar 16-bit values. Its shifter retains per-plane word carry in `pc98_egc_t`. | `mem/memegc.c` uses `EGCQUAD`, byte-mask tables, stream buffers and operation dispatch functions such as `ope_xx`. |
| SASI | `pc98_sasi.c:172` models bus phases and explicit BSY/REQ/MSG/C-D/I-O/ACK lines, with media and IRQ callbacks. `hdc_pc98_sasi.c` supplies the 86Box card wrapper. | `cbus/sasiio.c` operates on global `sasiio`, uses `sxsi_getptr`/`sxsi_read`/`sxsi_write`, and schedules completion with `nevent_set`. |
| D88 | `fdd_d88.c:408` builds tracks and sectors through `d86f_prepare_pretrack`/`d86f_prepare_sector`; normal 86Box FDC handling consumes them. | `diskimage/fd/fdd_d88.c` implements NP2 disk-function callbacks, track seeking and sector lookup against its own FDC state. |
| Sound glue | `snd_pc98_opn.cpp:58` implements a `ymfm_interface` bridge with 86Box timer callbacks. `snd_pc98_86.c:193` records DAC output against `sound_pos_global`. | Reference board/FIFO code uses `g_opna`, `g_pcm86`, `sound_sync`, and NP2 clock/event state. Both reference trees also contain ymfm, so the library choice alone does not distinguish lineage. |

## Automated similarity results

[The comparison script](../../tools/pc98_provenance_audit.py) removes
comments and whitespace while preserving strings, numbers and identifier
spellings. It searches every possible contiguous 40-token window in the
48 current/historical core files against both reference source trees.
**It found zero matching windows at that threshold.** This is a bounded
lexical result, not a claim of zero shared ideas or zero small fragments.

A second pass replaces every non-keyword identifier with the same placeholder
and compares 60-token windows. It found 140 matching file pairs. Their longest
runs are generic declarations of the form `type field; type field; ...`,
principally in the GDC state header and the SCSI state structure. No qualifying
run contained the executable syntax tracked by the script, including control
flow, assignments, member access, or increments. Removing type and field
names makes unrelated structures look alike; these hits are not persuasive
evidence of an NP2 implementation port.

The entire-tree whole-file pass found **14 matching files** after comments
and whitespace were removed: 13 ymfm files and one libchdr `bitstream.h`.
All 14 paths already had identical blobs in the initial import and the
86Box baseline. They are inherited shared dependencies, not newly identified
NP2 core imports. ymfm itself identifies
[Aaron Giles as its author and documents its MAME origins](https://github.com/aaronsgiles/ymfm).
NP2-family projects bundling that library does not make all other consumers
NP2 derivatives.

The comparator was checked with positive controls for exact matching,
identifier changes, comments versus string literals, executable-match
reporting, and source-line mapping. Emulator execution tests or a build
would not establish source provenance and were not run for this audit.

## Outside influence that should be acknowledged

The source contains specific NP2 references:

- `vid_pc98_86box.c:440` explicitly bases the text-palette decision on
  NP2/W's `pal_maketext` and `degpal1` behavior. This is affirmative evidence
  of NP2-informed behavior, even though the implementation is a small direct
  RGB calculation rather than NP2's palette machinery.
- `fdd_d88.c:31` and `:248` discuss NP2-derived image media encodings and
  the `2HD+1` convention. These concern reading compatible disk images;
  file-format compatibility does not itself establish implementation ancestry.
- Existing [video](pc98-egc-grcg-audit.md) and
  [sound](pc98-soundcards-lemonspin-audit.md) audits record inspection of
  NP2kai alongside other emulator sources. These documents establish that
  references were consulted; their descriptions of prior work are not
  independent proof of authorship or hardware correctness.

There is also explicit MAME influence:
`vid_pc98.c:460` describes adapting the logical font reconstruction from
`init_pc9801_kanji`, and `:492` names MAME's address-bit permutation. The
project should not be described as having no emulator-derived knowledge.

## What the evidence supports

A defensible statement is:

> The repository is demonstrably based on 86Box. Its PC-98 core differs
> substantially from the NP2-family implementations examined, and comparisons
> of the current code and the first PC-98 commit found no evidence of a bulk
> NP2 core port. NP2 was consulted for particular compatibility behavior;
> shared ymfm/libchdr files were already inherited from 86Box. AI assistance
> alone is not evidence that this project is based on NP2.

The evidence does **not** prove that every PC-98 routine was independently
invented, that no small or rewritten fragment came from NP2, that an AI model
had never encountered NP2, or that the emulator is hardware-accurate. The
first PC-98 support arrived in one large commit, which limits reconstruction
of its development process. Git author labels and absent NP2 history cannot
resolve that gap.

## Reproducing the comparison

From the repository root, download the two fixed source archives to a local
reference directory, then run the standard-library-only Python script:

```powershell
New-Item -ItemType Directory -Force -Path tmp/provenance-audit | Out-Null
curl.exe -fL -o tmp/provenance-audit/np2kai.tar.gz https://codeload.github.com/AZO234/NP2kai/tar.gz/5939e0c6d5985c4c08fc70f289a83290e5d3e6f7
curl.exe -fL -o tmp/provenance-audit/np21w-source.tar.gz https://codeload.github.com/roytam1/np21w/tar.gz/e77b1a47962f89c6a8a7ddcc1b5e421f9ba4418a
python tools/pc98_provenance_audit.py --reference NP2kai=tmp/provenance-audit/np2kai.tar.gz --reference NP21W=tmp/provenance-audit/np21w-source.tar.gz --output tmp/provenance-audit/recheck.json
```

The script reads archives without extracting or executing their contents.
Archive SHA-256 hashes, reference-file counts, target-file hashes, thresholds,
and matches are recorded in the output. Re-running against edited source can
change the results. Binary ROMs, bundled guest programs, assembly sources,
and the provenance of every unrelated third-party dependency are outside
this audit's scope.
