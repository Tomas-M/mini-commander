# Static binary size measurements

Measured on 2026-10-04 in 32-bit Alpine Linux under QEMU, using the pinned
GCC 11.2.1 toolchain, musl 1.2.6, ncurses 6.6, and UPX 5.2.1.
The baseline is commit `18d8529` (Mini Commander 2.0). Sizes are bytes, not RAM
usage. More than 40 compiler, linker, library, and compression combinations
were compared. These are measured results, not a proof of an absolute minimum.

## Results

Unless stated otherwise, packing uses `--best --lzma`. Individual baseline
experiments are not cumulative; the combined rows say which changes they use.

| Configuration | Stripped ELF | Packed |
| --- | ---: | ---: |
| Original build | 280,180 | 123,548 |
| Original + gold with safe identical-code folding | 280,236 | 123,428 |
| Original + i386 code generation for application | 280,180 | 122,900 |
| Original + unused ncurses extensions disabled | 275,572 | 121,052 |
| Original + integer-only ncurses parsers | 271,988 | 119,524 |
| Reduced ncurses + integer parsers + i386 application + gold | 267,436 | 116,348 |
| Same, with i386 code generation for ncurses too | 267,436 | 115,260 |
| Reduced ncurses + integer parsers + direct-object LTO + i386 code | 250,996 | 110,308 |
| Same, with `--ultra-brute` (selected) | 250,996 | 110,244 |
| Same, with gold and `--ultra-brute` | 251,052 | 110,236 |

The selected configuration saves **13,304 bytes (10.8%)** over the original
packed binary. It retains UTF-8, syntax highlighting, mouse support, scrolling
optimizations, all eight embedded terminal descriptions, and musl's default
`mallocng` allocator. The application source is unchanged.

## Why these changes help

The original build already used `-Os`, section garbage collection, stripping,
and UPX. Static linking did not include entire libraries indiscriminately.
Manually copying individual library functions would not remove their internal
dependencies. See the [GNU ld options](https://sourceware.org/binutils/docs/ld/Options.html)
for archive selection and section garbage collection.

Two ncurses color parsers used `sscanf` only for integers. This pulled in musl's
general formatted-input machinery, including floating-point parsing. The small
`ncurses-integer-colors.patch` preserves the integer formats and partial-match
behavior using `strtol`. Neither `vfscanf` nor `__floatscan` remains in the
optimized executable. Original source archives are kept intact.

The pinned, statically built compiler has no loadable LTO linker plugin.
Compiling an archive with `-flto` alone therefore did not optimize ncurses
together with the application. Extracting its objects with `ar x` and passing
them directly to GCC, with `-flto -fwhole-program`, enables this optimization.
The [GCC optimization options](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html)
describe LTO and whole-program optimization.
For example, the unused termcap-name table disappears while terminfo names and
extended capabilities used by the terminal profiles remain available.

The `-march=i386` choice concerns code generation for ncurses and the application,
not the minimum CPU for the whole executable: musl still targets i686. UPX's
`--ultra-brute` saved 64 bytes over `--best --lzma` for the selected ELF and takes
longer to pack.

## Other experiments and tradeoffs

- Adding LTO flags only to archived libraries did not help with this toolchain;
  direct objects were necessary. Merely removing application LTO or enabling
  inlining in the original build also failed to improve the packed size.
- Disabling separate code segments had no effect. Sorting sections by alignment
  increased the packed baseline to 124,948 bytes. Compare packed sizes, not just
  ELF sizes: a smaller or differently ordered ELF can compress worse.
- Gold with `--icf=safe` saved another eight bytes, but a subsequent clean build
  failed while reading `libc.a` (`attempt to map 2147483648 bytes at offset 52`).
  The default BFD linker is retained instead of depending on this result.
- Using musl's older `oldmalloc` allocator produced 108,740 bytes with the
  optimized BFD-linked build and `--best --lzma`, versus 110,308 with `mallocng`.
  This changes allocation behavior and hardening; it is not the default build.
- A prototype reusing the application's fork/exec helper for shell commands
  produced 109,228 bytes with the same BFD configuration. It would require
  completing and testing `system()` signal semantics before adoption.
- A diagnostic baseline with regex highlighting stubbed out shrank from 123,548
  to 111,396 bytes. That binary loses highlighting and is not a release
  candidate. Replacing the general POSIX regex engine with a small lexer for
  the fixed C/shell rules is the next substantial opportunity; its eventual
  saving must be measured after preserving highlighting behavior.
- Disabling both ncurses hashmap scrolling and scroll hints failed to compile
  with wide characters in ncurses 6.6. Those optimizations remain enabled.

## Verification

The selected settings are reproduced by `sh static/build.sh`; it verifies archive
hashes, applies the patch to a fresh source tree, checks ELF32/static linking,
tests UPX integrity, and runs both unpacked and packed executables.
The fresh build matched the selected experimental binary byte for byte
(SHA-256 `eb9ffa08255fda9469d04713362ab14960054404d9fc818be263452118ae098d`).

QEMU pseudo-terminal tests cover all eight terminal profiles plus the unknown
terminal fallback, resize handling, UTF-8 editor save, command editing and panel
search (Czech, Chinese, combining marks, and emoji), copy/move/mkdir, mouse click
and wheel input, and cursor repaint behavior. Repeated navigation at the end
of a list emits no terminal output; editor redraw hides the cursor until its
final position. The two integer parsers also passed 100,000 differential cases
against the original `sscanf` behavior, including partial and invalid input.

Use `static/mc.map`, `static/mc.symbols`, and `static/mc.sections` from a new
build to investigate further. They describe the unpacked code; `.bss` reserves
runtime memory and does not contribute its full size to the executable file.
