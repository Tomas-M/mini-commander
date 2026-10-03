# Small static 32-bit Linux build

From the repository root:

```sh
sh static/build.sh
./static/mc
```

The script always builds a 32-bit x86 executable for i686-compatible CPUs,
including when run on a 64-bit x86 Linux host. It downloads a self-contained
32-bit GCC toolchain from [musl.cc](https://musl.cc/), so no system compiler or
additional 32-bit development packages are needed. Musl, ncurses, and the
application are all compiled with `-m32 -march=i686 -Os -fno-pie`.
Explicitly disabling PIE code generation reduces the size of the static binary;
the linker also uses `-no-pie`.
The application uses LTO with inlining disabled, which produced a smaller packed
executable with the pinned compiler. Libraries retain their normal inlining.
It downloads and builds [musl](https://musl.libc.org/) 1.2.6 and
[ncurses](https://invisible-island.net/ncurses/) 6.6 locally, links everything
statically with Unicode-capable ncurses (`ncursesw`), strips unused code and
symbols, and compresses the executable with
[UPX](https://upx.github.io/) 5.2.1. Downloads have pinned SHA-256 checksums.
The UPX download matches the build host (x86-64 or 32-bit x86), while the packed
application remains 32-bit.

No root access is needed, and nothing is installed system-wide. Temporary
downloads, libraries, and build tools are removed after success, failure, or
interruption. The existing `static/mc` is replaced only after the new binary
passes the ELF32/x86 and static-link checks and runs successfully. Avoid spaces
in the checkout path, since the upstream library build systems do not reliably
support them.

## Prerequisites

An x86 or x86-64 Linux system with GNU make, curl, tar, gzip, xz, and the usual
shell utilities (including awk and SHA-256 checksum tools). On Debian/Ubuntu:

```sh
sudo apt-get install make curl ca-certificates xz-utils
```

The build host must be able to run 32-bit x86 Linux executables, including the
downloaded compiler and generated ncurses build tools.

The script downloads its own libraries; installed ncurses development packages,
musl packages, and UPX are not required. Native Windows builds are not supported;
run it on Linux or inside WSL.

## Options

```sh
JOBS=2 sh static/build.sh   # Limit parallel compilation and memory use.
PACK=0 sh static/build.sh  # Keep an unpacked static ELF; do not download UPX.
```

By default, compilation uses all available CPU cores and UPX compression is on.
`PACK=0` only disables compression; the target remains 32-bit x86.
The script prints both the unpacked and final binary sizes.

## Running the binary

The binary runs on 32-bit x86 Linux and on x86-64 Linux with 32-bit executable
support enabled in the kernel. It needs no shared libc/ncurses libraries or
external terminfo files.
Use a UTF-8 terminal and locale (for example `LC_ALL=C.UTF-8 ./static/mc`) to
display international file and directory names correctly.
It embeds descriptions for `linux`, `vt100`, `xterm`, `xterm-256color`, `screen`,
`screen-256color`, `tmux`, and `tmux-256color`. The application's existing
terminal initialization falls back to `xterm` for unrecognized terminal types.
As in the normal build, typed shell commands still require `/bin/sh` and any
programs they invoke on the destination system.
