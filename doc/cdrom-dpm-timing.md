# CD-ROM timing with Data Position Measurement

86Box mounts BlindWrite 5 and 6 images and reads their sectors and subchannels. Embedded DPM measurements and `.bwa` sidecars currently have no effect on drive timing.

## Image metadata

Represent DPM as a starting LBA, a nonzero sample interval in sectors, and cumulative angles in units of 1/256 of a revolution. Sample zero is implicit at the starting LBA. The first stored angle belongs to the sector one sample interval later.

The rust-dos reference implementation reads the sample header 32 bytes into the embedded DPM block or 12 bytes into a `.bwa` sidecar. A valid same-stem sidecar takes precedence over the embedded measurements. Check the sample count against the available bytes, reject decreasing angles, and calculate the final sampled LBA with checked arithmetic. Missing or invalid measurements should leave the disc mountable.

Let the image backend own the table and expose it through an optional callback in [`cdrom_ops_t`](../src/include/86box/cdrom.h). The drive should consume the same representation regardless of image format. Alcohol 120% images can supply it later through the same interface.

## Drive model

Use the nominal CD spiral before the first measurement. Within the measured range, linearly interpolate the cumulative angle between samples. After the final measurement, resume the nominal spiral with an offset that makes the angle continuous at the boundary.

Keep spindle phase and read-ahead state per drive. Advance phase using emulated elapsed time and the active drive speed. For an uncached read, use [`cdrom_seek_time`](../src/cdrom/cdrom.c) for the radial movement. At seek completion, the forward difference between the spindle phase and the requested sector angle, modulo one revolution, gives the rotational wait. The difference in cumulative angle from the first requested sector to the sector after the request gives the media transfer duration. Convert both angular differences to time with the spindle rate.

For sequential reads, use the emulated time at which the last required sector enters the read-ahead buffer. Reset the buffer on an explicit seek and clear timing state on eject, mount, or drive reset. Preserve phase continuity when the drive speed changes.

## Command scheduling

Start with ATAPI and SCSI `READ` and `READ CD` for CDs with valid DPM. Capture the request's first LBA and sector count before the read loop advances the position. In [`scsi_cdrom_command_common`](../src/scsi/scsi_cdrom.c), use the modeled delay in microseconds in place of the current media seek and throughput estimate. Keep bus transfer accounting in the controller. Check the ATAPI PIO chunk boundaries and the SCSI controller-timed path so each requested sector incurs the media delay once.

Exercise the model with two synthetic discs containing identical sector data and different DPM curves. Random reads should yield different, repeatable guest-visible completion times. Also check interpolation boundaries, cached and sequential reads, explicit seeks, media changes, and reads with DPM absent or malformed. The reference implementation is in `src/cdrom/dpm.rs` and `src/cdrom/timing.rs` in rust-dos.
