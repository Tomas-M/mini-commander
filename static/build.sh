#!/bin/sh
set -eu
export LC_ALL=C

if [ "$(uname -s)" != Linux ]; then
    echo "Run this script on Linux (or inside WSL)." >&2
    exit 1
fi
for tool in make curl tar gzip xz sha256sum awk sed; do
    command -v "$tool" >/dev/null || { echo "Missing build tool: $tool" >&2; exit 1; }
done

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_dir=$(dirname -- "$script_dir")
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN)}
pack=${PACK:-1}
case "$jobs" in ''|0|*[!0-9]*) echo "JOBS must be a positive integer." >&2; exit 1;; esac
case "$pack" in 0|1) ;; *) echo "PACK must be 0 or 1." >&2; exit 1;; esac

musl_version=1.2.6
ncurses_version=6.6
upx_version=5.2.1
# UPX runs on the build host; the executable it packs is always 32-bit x86.
case "$(uname -m)" in
    x86_64) upx_arch=amd64; upx_hash=402162aad30af47e60dbd767fb2e64ca394ace9727ba1f40283641f1d1b91657;;
    i?86) upx_arch=i386; upx_hash=44b505d881337ef17ad03f03fd80f2ecdebc3a36077e1dd260ae1b359c713ba5;;
    *) echo "This 32-bit x86 build requires an x86 or x86-64 Linux host." >&2; exit 1;;
esac

build_dir=$(mktemp -d "$script_dir/.build.XXXXXX")
# Keep the old binary until every build/verification step succeeds, and clean on failure too.
trap 'status=$?; if [ "$status" -ne 0 ] && [ -f "$build_dir/build.log" ]; then tail -n 60 "$build_dir/build.log" >&2; fi; rm -rf -- "$build_dir"' 0
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
cd "$build_dir"

# Verify pinned upstream archives before unpacking or executing their contents.
download() {
    archive=${1##*/}
    curl --fail --location --retry 3 --connect-timeout 30 --output "$archive" "$1"
    printf '%s  %s\n' "$2" "$archive" | sha256sum -c -
    tar -xf "$archive"
}

# Verify the target architecture before running both unpacked and packed binaries.
check_binary() {
    readelf -h mc >header.txt
    if ! grep -Eq 'Class:[[:space:]]+ELF32' header.txt || ! grep -Eq 'Machine:[[:space:]]+Intel 80386' header.txt; then
        echo "The resulting binary is not 32-bit x86." >&2
        exit 1
    fi
    # The application's --version prints a version but exits with status 1.
    ./mc --version >version.txt 2>&1 || test "$?" -eq 1
    grep '^Version ' version.txt
}

# Use a self-contained 32-bit compiler instead of requiring host GCC multilib packages.
download "https://musl.cc/i686-linux-musl-native.tgz" \
    978471bf7b8111dfd8c5559a23ef18b80bcd85936872f00424f1b7a5300580ee
export PATH="$build_dir/i686-linux-musl-native/bin:$PATH"

download "https://musl.libc.org/releases/musl-$musl_version.tar.gz" \
    d585fd3b613c66151fc3249e8ed44f77020cb5e6c1e635a616d3f9f82460512a
download "https://ftp.gnu.org/gnu/ncurses/ncurses-$ncurses_version.tar.gz" \
    355b4cbbed880b0381a04c46617b7656e362585d52e9cf84a67e2009b749ff11
if [ "$pack" = 1 ]; then
    download "https://github.com/upx/upx/releases/download/v$upx_version/upx-$upx_version-${upx_arch}_linux.tar.xz" "$upx_hash"
fi

prefix="$build_dir/local"
export CFLAGS='-m32 -march=i686 -Os -g0 -ffunction-sections -fdata-sections -fno-unwind-tables -fno-asynchronous-unwind-tables'
# The downloaded GCC defaults to PIE, but musl-gcc's startup objects cannot relocate static PIE.
export LDFLAGS='-m32 -static -no-pie -Wl,--gc-sections -Wl,--build-id=none'
echo "Building 32-bit x86 musl $musl_version..."
(
    cd "musl-$musl_version"
    ./configure --prefix="$prefix" --target=i686-linux-musl \
        --disable-shared --disable-optimize --enable-wrapper=gcc CC=gcc AR=ar RANLIB=ranlib
    make -j "$jobs"
    make install
) >build.log 2>&1

export CC="$prefix/bin/musl-gcc"
ncurses_source="$build_dir/ncurses-$ncurses_version"
mkdir tools curses
# Build matching tic/infocmp locally so embedded terminal entries need no host ncurses tools.
echo "Building terminal description tools..."
(
    cd tools
    "$ncurses_source/configure" --prefix="$prefix" \
        --without-shared --without-debug --without-cxx --without-cxx-binding \
        --without-ada --without-tests --without-manpages --without-gpm \
        --without-dlsym --enable-widec
    make -j "$jobs" -C include
    make -j "$jobs" -C ncurses
    make -j "$jobs" -C progs tic infocmp
) >>build.log 2>&1

echo "Building minimal ncurses $ncurses_version with embedded terminal descriptions..."
(
    cd curses
    # Install headers and the ncurses.h alias directly in our private prefix/include.
    "$ncurses_source/configure" --prefix="$prefix" --enable-overwrite \
        --without-shared --without-debug --without-cxx --without-cxx-binding \
        --without-ada --without-tests --without-manpages --without-progs \
        --without-gpm --without-dlsym \
        --enable-widec --disable-database --disable-db-install \
        --with-fallbacks=linux,vt100,xterm,xterm-256color,screen,screen-256color,tmux,tmux-256color \
        --with-tic-path="$build_dir/tools/progs/tic" \
        --with-infocmp-path="$build_dir/tools/progs/infocmp"
    make -j "$jobs" -C include
    make -j "$jobs" -C ncurses
    make -C include install
    make -C ncurses install
) >>build.log 2>&1

echo "Linking 32-bit x86 Mini Commander..."
# Use the normal build's source files, bypassing its optional host UPX step.
(
    cd "$project_dir"
    "$CC" $CFLAGS -std=gnu99 -flto -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE \
        -D_FILE_OFFSET_BITS=64 -I"$prefix/include" \
        mc.c cmd.c operations.c dialog.c filelist.c init.c panel.c ui.c view_edit.c progress.c \
        "$prefix/lib/libncursesw.a" $LDFLAGS -o "$build_dir/mc"
) >>build.log 2>&1
strip --strip-all mc

# Check the ELF before packing: a static executable has neither a loader nor shared dependencies.
readelf -l mc >elf.txt
readelf -d mc >>elf.txt
if grep -Eq 'INTERP|\(NEEDED\)' elf.txt; then
    echo "The resulting binary is not fully static." >&2
    exit 1
fi
check_binary
echo "Unpacked size: $(wc -c < mc) bytes"
if [ "$pack" = 1 ]; then
    "./upx-$upx_version-${upx_arch}_linux/upx" --best --lzma mc
    "./upx-$upx_version-${upx_arch}_linux/upx" -t mc
    check_binary
fi
chmod 755 mc
mv -f mc "$script_dir/mc"
echo "Built $script_dir/mc ($(wc -c < "$script_dir/mc") bytes)"
