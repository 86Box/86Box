# 86Box in a container

This image runs one 86Box machine **headless**: it has no window, display
server or GPU. A program on the host, such as a test harness, a script or an
AI agent, drives it through the
[automation socket](../doc/specifications/automation.md). It's tested with
Docker. Podman should work the same way: use `podman` in place of `docker`,
and `--userns=keep-id` in place of `--user`.

## Build

From the repository root:

```sh
docker build -f container/Dockerfile -t 86box-headless .
```

The image is built with the SDL frontend and `AUTOMATION=ON`, so no Qt is
needed. These build arguments are available:

| Argument | Default | |
|---|---|---|
| `WITH_ROMS` | `0` | `1` clones the [ROM set](https://github.com/86Box/roms) into `/roms` in the image. Otherwise, mount one there. |
| `ROMS_REF` | `master` | The ROM repository's branch or tag. |
| `BUILD_PRESET` | `regular` | The CMake preset (`regular`, `optimized`, `debug`, ...). |
| `BASE_IMAGE` | `ubuntu:26.04` | A Debian-based image with SDL 3. |

## Run

The container needs a VM directory (with `86box.cfg`) at `/vm`, and a ROM set.
A VM made with 86Box on a desktop works as it is.

```sh
mkdir -p run
docker run -d --name dos \
    --user "$(id -u):$(id -g)" \
    -v "$PWD/myvm:/vm" \
    -v "$PWD/roms:/roms:ro" \
    -v "$PWD/run:/run/86box" \
    -e EMU_HOLD=1 \
    86box-headless

container/automation-client.py --wait 30 unix:run/86box.sock status "step 3000" "screenshot /run/86box/screen.png"
```

- The automation socket is `/run/86box/86box.sock`. Mounting `/run/86box`
  makes it reachable from the host as `run/86box.sock`. The socket is
  owner-only (mode 0600), so run the container with `--user` set to your own
  UID, as above.
- Paths in commands are paths **inside the container**. A screenshot saved to
  `/run/86box/screen.png` appears on the host as `run/screen.png`. A disk
  image to load must be under a mounted volume, for example under `/vm`.
  Alternatively, send it with `media put`.
- `docker stop` sends SIGTERM. 86Box then saves the machine's NVRAM and disks
  and exits cleanly. A second signal kills it.
- If 86Box can't start, it exits non-zero with the reason on stderr
  (`docker logs`). Causes include a missing machine, a hardware option this
  build doesn't have, and no usable ROMs.

### Settings

| Variable | Default | |
|---|---|---|
| `EMU_VM` | `/vm` | VM directory. |
| `EMU_AUTOMATION` | `unix:/run/86box/86box.sock` | Automation endpoint: `unix:<path>`, `tcp:[host:]port` or `none`. |
| `EMU_TOKEN_FILE` | `/run/86box/automation.token` | TCP: where the access token is written. |
| `EMU_HOLD` | `0` | `1`: start held, so no emulated time passes until a client asks for it. |
| `EMU_SEED` | | Makes random numbers (generated MACs, floppy weak bits) repeatable. |

Arguments after the image name go to 86Box, for example
`docker run ... 86box-headless --automation-insecure`.

### Over TCP

Use TCP when the client isn't on the same host, or can't share a directory
with the container:

```sh
docker run -d --name dos --user "$(id -u):$(id -g)" \
    -v "$PWD/myvm:/vm" -v "$PWD/roms:/roms:ro" -v "$PWD/run:/run/86box" \
    -e EMU_AUTOMATION=tcp:0.0.0.0:7086 -p 127.0.0.1:7086:7086 \
    86box-headless

container/automation-client.py --wait 30 --token-file run/automation.token tcp:127.0.0.1:7086 info
```

A published port arrives on the container's network interface, not on its
loopback. 86Box must therefore listen on all addresses, so the entry point
adds `--automation-allow-remote` for a non-loopback host. Every client must
send the token, which 86Box writes to `EMU_TOKEN_FILE` at startup. The
protocol is **unencrypted**. Publish the port on the host's loopback only
(`-p 127.0.0.1:...`), and read the
[security notes](../doc/specifications/automation.md#transports-and-security)
before exposing it further.

## A minimal machine

To try the image without a VM from a desktop, put this in `myvm/86box.cfg`:

```ini
[Machine]
machine = ibmat
cpu_family = ibmat
cpu_speed = 6000000
mem_size = 512

[Video]
gfxcard = cga
```

After `step 3000`, the `text` command shows the BIOS memory count.

## What works in a container

- **Networking:** SLiRP (user-mode networking) needs no privileges. TAP and
  pcap need `--cap-add NET_ADMIN` or `--network host`.
- **Sound:** there is no sound device, so the host output is silent. The
  automation `audio` commands still record every stream.
- **Serial ports:** use the `automation` backend to talk to COM ports over
  the socket. The named pipe and file backends also work, with their paths
  under a mounted volume.
- **Not available:** host CD drives, USB passthrough and VFIO.
