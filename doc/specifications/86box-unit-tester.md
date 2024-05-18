# 86Box Unit Tester device specification v1.0.0

By GreaseMonkey + other 86Box contributors, 2024.
This specification, including any code samples included, has been released into the Public Domain under the Creative Commons CC0 licence version 1.0 or later, as described here: <http://creativecommons.org/publicdomain/zero/1.0>

The 86Box Unit Tester is a facility for allowing one to unit-test various parts of 86Box's emulation which would otherwise not be exposed to the emulated system. 

The original purpose of this was to make it possible to analyse and verify aspects of the monitor framebuffers in order to detect and prevent regressions in certain pieces of video hardware.

----------------------------------------------------------------------------

## Versioning

This specification follows the rules of Semantic Versioning 2.0.0 as documented here: <https://semver.org/spec/v2.0.0.html>

The format is `major.minor.patch`.

- Before you mess with this specification, talk to the other contributors first!
- Any changes need to be tracked in the Version History below, mostly in the event that this document escapes into the wild and doesn't have the Git history attached to it.
- If it clarifies something without introducing any behaviour changes (e.g. formatting changes, spelling fixes), increment the patch version.
- If it introduces a backwards-compatible change, increment the minor version and reset the patch version to 0.
- If it introduces a backwards-incompatible change, increment the major version and reset the minor and patch versions to 0.
  - If you make a mistake and accidentally introduce a backward-incompatible change, fix the mistake and increment the minor version.
  - To clarify, modifications to *this* section are to be classified as a *patch* version update.
- If you understand SemVer 2.0.0, you may also do other things to the version number according to the specification.

And lastly, the 3 golden rules of protocol specifications:

1. If it's not documented, it doesn't exist.
2. If it's not documented but somehow exists, it's a bug.
3. If it's a bug, it needs to be fixed. (Yes, I'm talking to you. You who introduced the bug. Go fix it.)

The checklist:

- Work out what kind of version number this document needs.
- Update the version number at the top of the file.
- Add an entry to the "Version History" section below describing roughly what was changed.

----------------------------------------------------------------------------

## Version History

Dates are based on what day it was in UTC at the time of publication.

New entries are placed at the top. That is, immediately following this paragraph.

### v1.0.0 (2024-01-08)
Initial release. Authored by GreaseMonkey.

----------------------------------------------------------------------------

## Conventions

### Integer types

- `i8` denotes a signed 8-bit value.
- `u8` denotes an unsigned 8-bit value.
- `w8` denotes an 8-bit value which wraps around.
- `x8` denotes an 8-bit value where the signedness is irrelevant.
- `e8` ("either") denotes an 8-bit value where the most significant bit is clear - in effect, this is a 7-bit unsigned value, and can be interepreted identically as a signed 8-bit value.
- `u16L` denotes a little-endian unsigned 16-bit value.
- `u16B` would denote a big-endian unsigned 16-bit value if we had any big-endian values.
- `[N]T` denotes an array of `N` values of type `T`, whatever `N` and `T` are.

----------------------------------------------------------------------------

## Usage

### Accessing the device and configuring the I/O base address

Find an area in I/O space where 2 addresses are confirmed (or assumed) to be unused.
There is no need for the 2 addresses to be 2-byte-aligned.

Send the following sequence of bytes to port 0x80 with INTERRUPTS DISABLED:

    '8', '6', 'B', 'o', 'x', (IOBASE & 0xFF), (IOBASE >> 8)

Alternatively denoted in hex:

    38 36 42 6F 78 yy xx

There are no timing constraints. This is an emulator, after all.

To confirm that this has happened, read the status port at IOBASE+0x00.
If it's 0xFF, then the device is most likely not present.
Otherwise, one can potentially assume that it exists and has been configured successfully.
(You *did* make sure that the space was unused *before* doing this, right?)

IOBASE is allowed to overlap the trigger port, but please don't do this!

### Hiding the device

Set the I/O base address to 0xFFFF using the above method.

### Executing commands

The ports at IOBASE+0x00 and IOBASE+0x01 are all 8 bits wide.

Writing to IOBASE+0x00 cancels any in-flight commands and sends a new command.

Reading from IOBASE+0x00 reads the status:

- bit         0: There is data to be read from this device
  - If one reads with this bit clear, the returned data will be 0xFF.
- bit         1: The device is expecting data to be sent to it
  - If one writes with this bit clear, the data will be ignored.
- bit         2: There is no command in flight
  - If this is set, then bits 0 and 1 will be clear.
- bit         3: The previously-sent command does not exist.
- bits  4 ..  7: Reserved, should be 0.

Writing to IOBASE+0x01 provides data to the device if said data is needed.

Reading from IOBASE+0x01 fetches the next byte data to the device if said data is needed.

### General flow of executing a command:

This is how most commands will work.

- Write the command to IOBASE+0x00.
- If data needs to be written or read:
  - Read the status from IOBASE+0x00 and confirm that bit 2 is clear.
    If it is set, then the command may not exist.
    Check bit 3 if that's the case.
- If data needs to be written:
  - Write all the data one needs to write.
- If data needs to be read:
  - Read the status from IOBASE+0x00 and wait until bit 0 is set.
    If it is set, then the command may not exist.
    Check bit 3 if that's the case.
  - Keep reading bytes until one is satisfied.
- Otherwise:
  - Read the status from IOBASE+0x00 and wait until any of the bottom 3 bits are set.

----------------------------------------------------------------------------

## Command reference

### 0x00: No-op

This does nothing, takes no input, and gives no output.

This is an easy way to reset the status to 0x04 (no command in flight, not waiting for reads or writes, and no errors).

### 0x01: Capture Screen Snapshot

Captures a snapshot of the current screen state and stores it in the current snapshot buffer.

The initial state of the screen snapshot buffer has an image area of 0x0, an overscanned area of 0x0, and an image start offset of (0,0).

Input:

* u8 monitor
  - 0x00 = no monitor - clear the screen snapshot
  - 0x01 = primary monitor
  - 0x02 = secondary monitor
  - Any monitor which is not available is treated as 0x00, and clears the screen snapshot.

Output:

* `e16L` image width in pixels
* `e16L` image height in pixels
* `e16L` overscanned width in pixels
* `e16L` overscanned height in pixels
* `e16L` X offset of image start
* `e16L` Y offset of image start

If there is no screen snapshot, then all values will be 0 as per the initial screen snapshot buffer state.

### 0x02: Read Screen Snapshot Rectangle

Returns a rectangular snapshot of the screen snapshot buffer as an array of 32bpp 8:8:8:8 B:G:R:X pixels.

Input:

* `e16L` w: rectangle width in pixels
* `e16L` h: rectangle height in pixels
* `i16L` x: X offset relative to image start
* `i16L` y: Y offset relative to image start

Output:

* `[h][w][4]u8`: image data
  - `[y][x][0]` is the blue component, or 0x00 if the pixel is outside the snapshot area.
  - `[y][x][1]` is the green component, or 0x00 if the pixel is outside the snapshot area.
  - `[y][x][2]` is the red component, or 0x00 if the pixel is outside the snapshot area.
  - `[y][x][3]` is 0x00, or 0xFF if the pixel is outside the snapshot area.

### 0x03: Verify Screen Snapshot Rectangle

As per 0x02 "Read Screen Snapshot Rectangle", except instead of returning the pixel data, it returns a CRC-32 of the data.

The CRC is as per zlib's `crc32()` function. Specifically, one uses a right-shifting Galois LFSR with a polynomial of 0xEDB88320, bytes XORed against the least significant byte, the initial seed is 0xFFFFFFFF, and all bits of the output are inverted.

(Rationale: There are better CRCs, but this one is ubiquitous and still really good... and we don't need to protect against deliberate tampering.)

Input:

* `e16L` w: rectangle width in pixels
* `e16L` h: rectangle height in pixels
* `i16L` x: X offset relative to image start
* `i16L` y: Y offset relative to image start

Output:

* `u32L` crc: CRC-32 of rectangle data

### 0x04: Exit 86Box

Exits 86Box, unless this command is disabled.

- If the command is enabled, then program execution terminates immediately.
- If the command is disabled, it still counts as having executed correctly, but program execution continues. This makes it useful to show a "results" screen for a unit test.

Input:

* u8 exit code:
  - The actual exit code is clamped to no greater than the maximum valid exit code.
    - In practice, this is probably going to be 0x7F.

----------------------------------------------------------------------------

## Implementation notes

### Port 0x80 sequence detection

In order to ensure that one can always trigger the activation sequence, there are effectively two finite state machines in action.

FSM1:
- Wait for 8.
- Wait for 6.
- Wait for B.
- Wait for o.
- Wait for x.
    Once received, set FSM2 to "Wait for low byte",
    then go back to "Wait for 8".

If at any point an 8 arrives, jump to the "Wait for 6" step.

Otherwise, if any other unexpected byte arrives, jump to the "Wait for 8" step.

FSM2:
- Idle.
- Wait for low byte. Once received, store this in a temporary location.
- Wait for high byte.
    Once received, replace IOBASE with this byte in the high byte and the temporary value in the low byte,
    then go back to "Idle".

----------------------------------------------------------------------------

## Extending the protocol

### Adding new commands

Commands 0x01 through 0x7F accept a single command byte.

Command bytes 0x80 through 0xFB are reserved for 16-bit command IDs, to be written in a similar way to this:

- Write the first command byte (0x80 through 0xFF) to the command register.
- If this block of commands does not exist, then the command is cancelled and the status is set to 0x0C.
- Otherwise, the status is set to 0x0
- Write the next command byte (0x00 through 0xFF) to the data register.
- If this block of commands does not exist, then the command is cancelled and the status is set to 0x0C.
- Otherwise, the command exists and the status is set according to the command.

Command bytes 0xFC through 0xFF are reserved for if we somehow need more than 16 bits worth of command ID.

