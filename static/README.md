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
application are all compiled with `-m32 -Os -fno-pie`. Musl targets i686;
ncurses and the application use `-march=i386` because it produced smaller packed
code with this compiler. The complete executable still requires i686.
Explicitly disabling PIE code generation reduces the size of the static binary;
the linker also uses `-no-pie`.
Ncurses and the application use LTO and `-fwhole-program`. The pinned static
toolchain cannot load a linker plugin for LTO archives, so the script extracts
the ncurses objects and passes them directly to GCC. This enables optimization
across the library/application boundary; unreachable code is still removed.
Musl retains its normal allocator and compilation settings.
It builds [musl](https://musl.libc.org/) 1.2.6 and
[ncurses](https://invisible-island.net/ncurses/) 6.6 from the original source
archives in `static/sources/`, links everything
statically with Unicode-capable ncurses (`ncursesw`), strips unused code and
symbols, and compresses the executable with
[UPX](https://upx.github.io/) 5.2.1. Downloads have pinned SHA-256 checksums.
Archives missing from `static/sources/` are downloaded into the temporary build
directory. Local archives are checked against the same pinned hashes.
The UPX download matches the build host (x86-64 or 32-bit x86), while the packed
application remains 32-bit. GCC and UPX are build tools, not runtime dependencies.

Ncurses omits unused extended color, screen-pointer, opaque-accessor, and extended
screen-dump APIs. Wide characters, mouse support, scrolling optimizations, and
all eight terminal profiles remain enabled. A small
[patch](ncurses-integer-colors.patch) replaces two integer-only `sscanf` calls
with `strtol`, avoiding the general floating-point input parser. The upstream
archives remain unchanged; the patch is applied only in the temporary build tree.
See [size measurements and tradeoffs](SIZE.md).

No root access is needed, and nothing is installed system-wide. Temporary
downloads, libraries, and build tools are removed after success, failure, or
interruption; local source archives are retained. The existing `static/mc`
is replaced only after the new binary
passes the ELF32/x86 and static-link checks and runs successfully. Avoid spaces
in the checkout path, since the upstream library build systems do not reliably
support them.

## Prerequisites

An x86 or x86-64 Linux system with GNU make, curl, tar, gzip, xz, patch, and the usual
shell utilities (including awk and SHA-256 checksum tools). On Debian/Ubuntu:

```sh
sudo apt-get install make curl ca-certificates xz-utils patch
```

The build host must be able to run 32-bit x86 Linux executables, including the
downloaded compiler and generated ncurses build tools.

The script builds its own libraries; installed ncurses development packages,
musl packages, and UPX are not required. Native Windows builds are not supported;
run it on Linux or inside WSL.

## Options

```sh
JOBS=2 sh static/build.sh   # Limit parallel compilation and memory use.
PACK=0 sh static/build.sh  # Keep an unpacked static ELF; do not download UPX.
```

By default, compilation uses all available CPU cores and UPX compression is on.
UPX uses `--ultra-brute` to select the smallest packed result; this takes longer
than the previous `--best --lzma` setting.
`PACK=0` only disables compression; the target remains 32-bit x86.
The script prints both the unpacked and final binary sizes.

## Inspecting what is linked

Static archives are not included wholesale: the linker selects needed object
files, and `-ffunction-sections -fdata-sections` with `--gc-sections` removes
unreachable sections from those objects. This applies to musl and ncurses as
well as the application. Copying the same functions into separate source files
does not by itself remove more code; their internal dependencies still remain.

Each successful build also saves three reports beside `static/mc`:

- `mc.map`: selected archive members (notably musl), the references that pulled
  them in, discarded sections, and a symbol cross-reference table. Ncurses is
  optimized with the application, so much of its code appears in LTO-generated
  objects rather than individual archive members.
- `mc.symbols`: retained symbols before stripping, sorted by size in ascending
  order. The second column is the symbol's size in decimal bytes; this includes
  data symbols as well as functions.
- `mc.sections`: section sizes of the stripped ELF before UPX compression.
  `.bss` represents memory reserved at runtime, not bytes stored in the file.

For example, `tail -n 30 static/mc.symbols` shows the largest retained symbols.
These reports are ignored by Git and add no data to the final executable.
Use them to identify expensive dependencies before replacing functionality or
specializing library code. A smaller unpacked ELF does not always compress to
a smaller UPX binary, so compare both sizes.

The bundled archives are unmodified upstream sources and include their original
license and copyright files. To inspect them without building:

```sh
tar -xf static/sources/musl-1.2.6.tar.gz -C /tmp
tar -xf static/sources/ncurses-6.6.tar.gz -C /tmp
```

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
