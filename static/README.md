# Small static Linux build

From the repository root:

```sh
sh static/build.sh
./static/mc
```

The script builds for the native architecture of your Linux GCC toolchain.
It downloads and builds [musl](https://musl.libc.org/) 1.2.6 and
[ncurses](https://invisible-island.net/ncurses/) 6.6 locally, links everything
statically with Unicode-capable ncurses (`ncursesw`), strips unused code and
symbols, and compresses the executable with
[UPX](https://upx.github.io/) 5.2.1. Downloads have pinned SHA-256 checksums.
UPX downloads are provided for x86-64, 32-bit x86, and AArch64 hosts.

No root access is needed, and nothing is installed system-wide. Temporary
downloads, libraries, and build tools are removed after success, failure, or
interruption. The existing `static/mc` is replaced only after the new binary
passes the static-link check and runs successfully. Avoid spaces in the checkout
path, since the upstream library build systems do not reliably support them.

## Prerequisites

A Linux system with GCC, GNU make, binutils, curl, tar, gzip, xz, and the usual
shell utilities (including awk and SHA-256 checksum tools). On Debian/Ubuntu:

```sh
sudo apt-get install build-essential curl ca-certificates xz-utils
```

The script downloads its own libraries; installed ncurses development packages,
musl packages, and UPX are not required. Native Windows builds are not supported;
run it on Linux or inside WSL.

## Options

```sh
JOBS=2 sh static/build.sh   # Limit parallel compilation and memory use.
PACK=0 sh static/build.sh  # Keep an unpacked static ELF; do not download UPX.
```

By default, compilation uses all available CPU cores and UPX compression is on.
For other CPU architectures, use `PACK=0` with a native GCC supported by musl.
Binary size depends on the architecture and compiler; the script prints both
the unpacked and final sizes.

## Running the binary

The binary needs no shared libc/ncurses libraries or external terminfo files.
Use a UTF-8 terminal and locale (for example `LC_ALL=C.UTF-8 ./static/mc`) to
display international file and directory names correctly.
It embeds descriptions for `linux`, `vt100`, `xterm`, `xterm-256color`, `screen`,
`screen-256color`, `tmux`, and `tmux-256color`. The application's existing
terminal initialization falls back to `xterm` for unrecognized terminal types.
As in the normal build, typed shell commands still require `/bin/sh` and any
programs they invoke on the destination system.
