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
