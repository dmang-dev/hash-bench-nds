# hash-bench-nds

Native **Nintendo DS** hashing-algorithm benchmark — **32 algorithms**
on a 33.51 MHz ARM946E with cycle-accurate timing, displayed across
**both screens** (top: 16 tiny / non-crypto, bottom: 16 modern +
cryptographic). Same algorithm set as
[`hash-bench-gba`](https://github.com/dmang-dev/hash-bench-gba),
[`hash-bench-dsi`](https://github.com/dmang-dev/hash-bench-dsi) (134 MHz DSi-mode build), and
[`hash-bench-3ds`](https://github.com/dmang-dev/hash-bench-3ds) (ARM11 native libctru build).
The GB sibling [`hash-bench-gb`](https://github.com/dmang-dev/hash-bench-gb) runs a subset of
23 — it skips the nine algorithms that need 64-bit math or won't fit
in MBC1's banks.

The expanded set:

| Tier | Algorithms |
|---|---|
| **Non-crypto checksums (4)** | CRC-8, CRC-16, CRC-32, Adler-32 |
| **Non-crypto hashes (5)**    | DJB2, FNV-1a, Pearson-8, Murmur3-32, xxHash32 |
| **Non-crypto 64-bit (2)**    | **xxHash64**, **SipHash-2-4** |
| **Cryptographic (5)**        | MD4, MD5, SHA-1, SHA-256, BLAKE2s |
| **Cryptographic, GBA + NDS (4)** | **RIPEMD-160**, **SHA-3-256**, **SHA-512**, **HMAC-SHA256** |

[![ROM](https://img.shields.io/badge/ROM-prebuilt%20%26%20committed-success)](artifacts/)
[![Built with devkitARM](https://img.shields.io/badge/built%20with-devkitARM%20%2B%20libnds-orange)](https://devkitpro.org)
[![Algorithms verified](https://img.shields.io/badge/algorithms-verified%20vs%20hashlib-blue)](tests/refs.txt)

---

## What it does

On boot, libnds initialises **two text consoles** — one per screen.

- **Top screen** shows the live results table: 20 rows of
  `ALGO  HASH    us/iter  KB/s` at 32×24 chars.
- **Bottom screen** shows boot status, sweep progress, and the
  re-run / exit hints.

Each algorithm is timed via `cpuStartTiming(0)` + `cpuGetTiming()`,
which sets up the cascaded TM0/TM1 timers as a 32-bit free-running
counter at the ARM9 BUS_CLOCK (33.514 MHz). Cycle counts get
converted to microseconds with `timerTicks2usec()` from libnds.

Press **B** to re-run the sweep, **START** to exit to the launcher /
homebrew menu.

---

## Try it

Pre-built ROM in [`artifacts/`](artifacts/):

| File | Target | Notes |
|---|---|---|
| `hash-bench-nds.nds` | DS / DS Lite / DSi (homebrew menu / flashcart) | ARM9-only, ~193 KB |

Load in **melonDS**, **DeSmuME**, or any homebrew-capable flashcart
(R4, M3, EZ-Flash, …). The DSi can run it via Unlaunch / TWiLight Menu.
No SRAM, no save data, no FAT, no DLDI — everything's in ROM and main RAM.

---

## Build from source

Requires **devkitPro** with the `nds-dev` meta-package (devkitARM 15+,
libnds, ndstool, libfat — though we don't actually link libfat):
https://github.com/devkitPro/installer/releases.

```
.\build.bat            # build hash-bench-nds.nds and copy to artifacts\
.\build.bat clean      # nuke build/ + outputs
```

Linux / macOS / CI: `make` works directly.

---

## Algorithms

All twenty `.c` algorithm files are **byte-identical** to the ones in
[`hash-bench-gba/source/`](https://github.com/dmang-dev/hash-bench-gba/tree/main/source/); sixteen of them
are also byte-identical to [`hash-bench-gb/src/`](https://github.com/dmang-dev/hash-bench-gb/tree/main/src/)
(the four 64-bit ones — `xxhash64.c`, `siphash.c`, `sha3.c`,
`sha512.c` — are GBA + NDS only). Single-source-of-truth across all
three CPU architectures.

The six "expanded set" algorithms — added in the second pass beyond
the original 14:

| Algorithm | Type | Digest | Where | Why it's interesting |
|---|---|---|---|---|
| **xxHash64**    | non-crypto | 64 bit  | GBA + NDS | Modern 64-bit ARX. Workhorse of LZ4/Zstd/RocksDB. Showcases the cost of `uint64_t` ops on a 32-bit ARM with no 64-bit ALU. |
| **SipHash-2-4** | non-crypto keyed PRF | 64 bit | GBA + NDS | Default hash inside Python `dict`, Rust `HashMap`, Perl, Ruby — minimal modern crypto. |
| **RIPEMD-160**  | crypto | 160 bit | **all three** | Belgian SHA-1 alternative, twin-pipeline design. Bitcoin's address-derivation hash. |
| **SHA-3-256**   | crypto | 256 bit | GBA + NDS | Keccak-f[1600] permutation in a sponge construction — radically different from MD/SHA-2. |
| **SHA-512**     | crypto | 512 bit | GBA + NDS | 128-byte block, 80 rounds, all 64-bit math. The "expensive crypto" benchmark. |
| **HMAC-SHA256** | crypto MAC | 256 bit | **all three** | RFC 2104 keyed wrapper. Two SHA-256 calls per invocation — useful for measuring per-call overhead vs raw SHA-256. |

### Reference digests

Workload buffer: `buf[i] = (i * 31 + 7) & 0xFF` for `i ∈ [0, 1024)`.
HMAC-SHA256 key: ASCII `hash-bench-nds` + two zero bytes (16 B total).
SipHash-2-4 key: bytes `0x00..0x0F` (the standard self-test key).
xxHash64 seed: `0`.

| Algorithm   | Digest |
|---|---|
| CRC-8       | `DD` |
| CRC-16      | `F009` |
| CRC-32      | `7C321B5D` |
| Adler-32    | `13D3FE10` |
| DJB2        | `358B5305` |
| FNV-1a      | `2C6D0DC5` |
| Pearson-8   | `A0` |
| Murmur3-32  | `56530DF1` |
| xxHash64    | `149AA44972CDAE00` (LE) |
| SipHash-2-4 | `12AB28BE1797B5D6` (LE) |
| MD4         | `A2909A641975A5CB590984B5323BB03B` |
| MD5         | `63B2177A7AF739B5CC52AB1D1C714702` |
| RIPEMD-160  | `21D1D63F50CDF5CACD90D8B323D22D0E7EAB00D2` |
| SHA-1       | `B66786CDE756750241D1F0EAB86CE6F81855B017` |
| SHA-256     | `8D7E566766F6BD1BB4CAC87CADFDE681197F9243F4D2692A0FD12674092212A7` |
| SHA-3-256   | `D925394CF5841554B9FC100363BD6EB55E69CD166C164C11EE5F699F181219BD` |
| BLAKE2s     | `AD41D5E917BE8E9CD95975E72CF44E118268294566BD95D7FCEB3D23D200EDC8` |
| SHA-512     | `076291378444AC54F7E7AC5717C498F169218B3BA608D08A3FA63E56063C4A9E0E26EA6FC82654B79AADB9E70AE90D5401DE36A0DF9B4B2DD046B7BAD4E4DA32` |
| HMAC-SHA256 | `CE139393FFB45956D62726B112E7F34C538104578DB59F75B564F1AF764BB3BD` |

The on-screen "HASH" column shows the first 4 hex bytes (8 chars) of
each digest. SipHash and xxHash64 store their 64-bit output as
little-endian bytes, so the on-screen leading bytes are the
*low-order* bytes of the integer above (e.g. xxHash64 displays
`00AECD72…`, byte-reversed from the integer `149AA44972CDAE00`).

### Cross-validation

[`tests/verify.c`](tests/verify.c) compiles each of the six new
algorithms under the host gcc (mingw-w64 on Windows) and prints
their digests against this same workload buffer. Build & run:

```
cd tests
gcc -O2 -I../include -o verify.exe verify.c \
    ../source/sha512.c ../source/sha3.c ../source/ripemd160.c \
    ../source/hmac_sha256.c ../source/siphash.c ../source/xxhash64.c \
    ../source/sha256.c
./verify.exe
```

All six match the values above (and the corresponding
`hashlib.<algo>(buf).hexdigest()` outputs).

---

## Timing methodology

libnds's `cpuStartTiming(0)` configures TM0+TM1 as a cascaded 32-bit
counter ticking at `BUS_CLOCK = 33514272 Hz`. `cpuGetTiming()`
returns the elapsed cycle count, and `timerTicks2usec()` converts to
microseconds via `(ticks * 1e6) / BUS_CLOCK` using a 64-bit divide.

For each algorithm:

```c
cpuStartTiming(0);
for (k = 0; k < iters; k++) hash(buf, 1024, digest);
cycles = cpuGetTiming();
us_per = timerTicks2usec(cycles) / iters;
kb_per_s = 1e6 / us_per;        // for 1024-B iter, binary KB
```

Iteration counts (30–800) are tuned per-algorithm so each row gets at
least ~50 ms of wall time, well above the ~30 ns counter quantisation
and big enough to amortise the function-pointer indirection.

---

## Layout

```
source/                Pure-C sources
  main.c                 Boot, dual-console init, dispatcher + timer
  crc8.c crc16.c crc32.c adler32.c                     non-crypto checksums   (shared)
  djb2.c fnv1a.c pearson.c murmur3.c xxhash32.c        non-crypto hashes      (shared)
  xxhash64.c siphash.c                                 non-crypto, NDS-only
  md4.c md5.c sha1.c sha256.c blake2s.c                cryptographic          (shared)
  ripemd160.c sha3.c sha512.c hmac_sha256.c            cryptographic, NDS-only
include/
  hashes.h               20 function declarations
tests/
  verify.c               Host gcc cross-check for the 6 new algorithms
  refs.txt               Reference digests
artifacts/               Prebuilt ROM (committed)
Makefile                 devkitPro NDS template (libnds, ARM9-only)
build.bat                Windows wrapper (forces devkitPro paths, runs ndstool)
```

Fourteen of the twenty `*.c` files are shared verbatim with the GB /
GBA projects. The six new ones use only `<stdint.h>` and the local
`hashes.h`, so they too compile cleanly under devkitARM (libnds /
libgba) and any host gcc — no NDS-specific dependencies in the
algorithm code itself.

---

## Acknowledgments

- [devkitPro / devkitARM](https://devkitpro.org/) — ARM toolchain
- [libnds](https://github.com/devkitPro/libnds) — DS hardware abstraction,
  console, timer helpers
- [melonDS](https://melonds.kuribo64.net/) — accurate DS emulator
- [`hash-bench-gba`](https://github.com/dmang-dev/hash-bench-gba) / [`hash-bench-gb`](https://github.com/dmang-dev/hash-bench-gb)
  — sibling projects supplying the 14 baseline algorithm sources
