# PC-98 text partitions and cursor synchronization

## Partition addressing

Text GDC PRAM lengths count raster lines. Each area's start address (SAD)
is independent, and an area boundary can fall within a character row.
The renderer regression includes this configuration:

| Area | Start address (word) | Length (raster lines) |
| --- | --- | --- |
| 1 | `0300h` | 40 |
| 2 | `0600h` | 360 |

With a 16-line character height, the 40-line boundary falls halfway through
a character row. Additional checks use an eight-line smooth-scroll offset.

The wrapper previously treated LEN as a number of character rows, so the
first area covered the entire 400-line display. It also added the accumulated
screen row to each area's SAD and inserted a blank character row between
areas. These errors selected the wrong text memory for later areas.

`pc98_render_text()` counts raster lines, loads each area's independent SAD,
and places the next area immediately after it. The character-row clock keeps
running across a partition boundary. A 40-line split leaves an eight-line
phase at a 16-line character height; an eight-line smooth scroll then carries
into the row after the new SAD. Restarting this clock at each area displaced
the second area's text by eight pixels. Empty areas consume no display lines.

The line-count interpretation follows the SCROLL command description in
[*PC-9801 Programmer's Bible*, printed p.97](https://pc98.ne.jp/devdocs/pc-9801programmersbible.pdf):
SL specifies the display lines of each area, and a single full-screen area's
SL equals SYNC's active line count. NEC's PRAM character-mode diagram also
labels LEN as line count in
[*uPD7220: A Single-Chip Graphics Display Controller*, February 1981, p.5](https://www.bitsavers.org/components/nec/uPD7220/uPD7220_A_Single-Chip_Graphics_Display_Controller_Feb1981.pdf).

## Cursor BIOS synchronization

The PC-9801VX firmware waits for a vertical interrupt when configuring the
cursor with INT 18h/AH=10h. This behavior was confirmed by disassembling the
four local `nec_d27c256d-15_cpu_extboard_yll0*.bin` ROMs using the production
ROM loader's interleaving.

INT 18h dispatches AH=10h to FD80:122E, which jumps to FD80:0A76 with the
cursor-programming handler FD80:1242. That shared routine saves the IRQ2
vector at 0000:0028, installs its own handler, unmasks IRQ2, rearms CRTV at
FD80:0A97, and waits at FD80:0A99 until the handler runs. It then restores
the previous vector and interrupt mask. AH=11h at FD80:1295 and AH=13h at
FD80:12BF issue GDC commands without this sync wait.

Repeated cursor setup combined with an application's own vertical-sync wait
can halve its update rate while the display raster continues at its normal
rate. Cursor setup can be performed once, followed by position updates as
needed. Applications with their own IRQ2 handler must also account for the
CRTV request latch after BIOS calls, as described in
[the vertical-interrupt audit](pc98-vrtc-interrupt-audit.md).

## Validation and limits

Display memory is sampled at the start of each active scanline, after the
programmed vertical back porch. Frame presentation submits the completed
rows without reading VRAM again. This preserves rows already scanned when
software changes the display page or writes to visible memory later in the
frame. The raster regression switches between blue and red pages on adjacent
lines and checks that presentation cannot change the earlier line.

Sampling is currently per scanline, not per pixel; writes during a line are
therefore represented at line granularity.

The production wrapper regression covers the 40/360 split, independent SADs,
exact short-area clipping, an empty intermediate area, scroll carry, continuous
character scanline phase at a split, and a normal full-screen area with TVRAM
address wrapping. The split cases distinguish reloading SAD from resetting
the row clock, including a carry at the start of the next area. All 160 checks
passed, including clipped scanline rendering, a page switch between adjacent
rows, and preserving already scanned rows when the frame is presented.

The cursor synchronization behavior is supported by the firmware
disassembly. Physical-hardware timing has not been measured, and these
checks do not establish accuracy for other EGC, font, or bus-timing behavior.
