# Known test failures

The Tests workflow (`.github/workflows/tests.yml`) builds and runs the tests on
every push and pull request, on the systems and architectures the CMake
workflows build for. A pull request should only be held up by the failures it
causes, so the tests that already fail on master are listed here:

- A listed test that fails is reported as a warning.
- A failing test that isn't listed fails the job.
- A listed test that passes, or that isn't a test in the job, is pointed out so
  its line can be removed or moved.

A list is not a place to park a failure a pull request introduces: fix the
test or the code instead. Lines here are for failures that are already on
master, and should name what is wrong, so they can be fixed and removed, or
why the test can't pass in CI, so they stay.

## Files

Each job reads the files that match it, if they exist:

| File | Jobs |
|---|---|
| `all.txt` | every job |
| `<os>.txt` | the jobs on one system: `linux`, `windows`, `macos` |
| `<arch>.txt` | the jobs on one architecture: `x86_64`, `arm64` |
| `<os>-<arch>.txt` | one job, e.g. `windows-arm64.txt` |

Put a failure in the narrowest file that covers every job it happens in, and
only create a file once it has an entry.

## Format

One CTest test name per line, as `ctest -N` lists it (for GoogleTest tests,
`Suite.Test`). `#` starts a comment, on its own line or after a name; blank
lines are ignored. Give each entry, or group of entries, a comment saying why
it fails:

```
# Need a Vulkan GPU backend, which the runners don't have. These stay.
Rage128.Registers.GpuVerify

Foo.Bar   # Off by one in the CRC since 1234abcd; to fix
```

A GoogleTest target that doesn't compile shows up as `<target>_NOT_BUILT`
instead of its tests; a target added with `add_test()` keeps its test names.

## Running it locally

`.ci/run_tests.sh <build directory> <os> <arch>` runs CTest the way the workflow
does and prints the same summary. Configure without Qt as the workflow does,
or the Qt tests show up without being built:

```
cmake --preset development -D QT=OFF -D BUILD_TESTING=ON
cmake --build build/development --target tests/all
.ci/run_tests.sh build/development linux x86_64
```
