# Hard disk image sizing regression

Build and run the tests in a build configured with `BUILD_TESTING=ON`:

```sh
cmake --build build/tests --target hdd_image_tests
ctest --test-dir build/tests --output-on-failure -R '^HddImage\.'
```

The tests compile the production `src/disk/hdd_image.c` as C, link the real
MiniVHD library, and use temporary image files. Platform services are adapted;
the loader, file operations, and file sizing are real. They do not boot a guest
or automate the Qt dialog.

## Fixed bug: raw image truncation on Unix

A user can select **Add Existing Hard Disk**, choose a raw `.img` image, and
increase **Cylinders** before starting the VM. When the configured capacity
exceeds the file size, `prepare_new_hard_disk()` must extend the image while
preserving its contents. Its Unix branch used to pass the number of missing
bytes to `ftruncate()` as the final file size, which truncated the existing
filesystem and erased user data simply by loading the VM. Any build that
defines `__unix__` was affected; the non-Unix branch appends zero-filled
blocks and was not.

`HddImage.IncreasingRawImageGeometryPreservesDataAndZeroFillsAddedCapacity`
covers this with a patterned three-cylinder image and a four-cylinder
configuration, both with four heads and 17 sectors per track:

| Quantity | Bytes |
| --- | ---: |
| Original image | 104448 |
| Expected image after loading | 139264 |
| Image after loading on Linux, before the fix | 34816 |
| Original bytes erased before the fix | 69632 |

The regression expects the configured capacity, unchanged original bytes, and
zero-filled added capacity. The two controls cover creating a new raw image
and loading an existing image whose geometry already matches.

The Unix branch now passes the absolute length, `full_size + base`, to
`ftruncate()`, and all three tests pass. Note that the regression only
exercises the fixed code on Unix builds; on Windows it runs the append path.

## Write-behind of file images (Linux)

Guest writes to raw, HDI and HDX images are queued and written by a thread
of their own (`hdd_image.c`, POSIX hosts): it wakes for the first write of a
batch, lets more gather for up to 10 ms, and a write carrying on from the
newest queued one is added to it. The `HddImageWriteBehind` tests replace the
host's `pwrite()` with one that can hold every write, as a filesystem commit
would, or fail one:

| Test | Checks |
| --- | --- |
| `ReadsSeeWritesStillQueuedBehindAStalledHostWrite` | reads return the newest queued data (write being made, writes and a zero behind it, overlapping), the file is untouched until the host lets the writes through, and a flush leaves file and reads equal |
| `ARunOfSectorsWrittenOneAtATimeIsOneHostWrite` | 100 single-sector writes in a row (as ATA WRITE MULTIPLE hands them over) reach the host as at most two writes, and read back right before that |
| `AFailedHostWriteIsReportedByTheNextWriteOrFlush` | a failed host write returns -1 from the next flush or write, once |
| `ClosingWritesEverythingQueued` | closing the image waits for every queued write |
| `RandomWritesAndReadsMatchTheDisk` | 4000 random writes, zeroes and reads with the host holding writes at random agree with a model, and so does the file after closing |

Breaking the copy from the queue (left out, or oldest last) fails the first
and the last test. The tests are compiled on Linux only.
