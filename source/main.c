/*
 * hash-bench-nds — Nintendo DS hashing-algorithm benchmark.
 *
 * 32 algorithms × 3 input sizes (64 / 256 / 1024 B) across both screens.
 *
 *   Single-size mode (default):  shows the 1024 B baseline with full
 *                                 digest + KB/s. Press SELECT to switch.
 *   Matrix mode:                  shows KB/s at all three sizes side-
 *                                 by-side, so you can spot setup-cost
 *                                 amortization and block-boundary
 *                                 cliffs at a glance.
 *
 * Rows are colored by tier:
 *   green  = checksums (CRC, Fletcher, Adler, Pearson)
 *   yellow = non-cryptographic hashes
 *   cyan   = cryptographic
 *   plus a '*' on the fastest in each tier (by 1024 B baseline).
 *
 * Controls:
 *   B       — re-run sweep
 *   A       — cycle sort mode (category / by-speed / by-name)
 *   SELECT  — toggle single-size ↔ matrix view (no re-bench)
 *   START   — exit to launcher
 */
#include <nds.h>
#include <stdio.h>
#include <string.h>

#include "hashes.h"

PrintConsole topConsole;
PrintConsole bottomConsole;

/* ---- workload buffer (lives in main RAM) ----------------------------- */
static uint8_t  buffer[BENCH_BUF_LEN];
static uint8_t  digest[HASH_MAX_DIGEST];

/* ---- algorithm table ------------------------------------------------- */
typedef void (*hash_fn)(const uint8_t *data, uint16_t len, uint8_t *out);

#define TIER_CHECKSUM   0u
#define TIER_NONCRYPTO  1u
#define TIER_CRYPTO     2u
#define NUM_TIERS       3u

static const char * const TIER_NAMES[NUM_TIERS] = {
    "checksum", "non-crypto", "crypto"
};

/* ANSI SGR foreground colors — green / yellow / cyan. libnds's
 * PrintConsole parses these escape sequences. Reset afterwards with
 * "\x1b[0m" so the next row starts from default white. */
static const char * const TIER_ANSI[NUM_TIERS] = {
    /* libnds parses SGR with siscanf("[%d;%dm", &parameter, &intensity)
     * — REQUIRES two parameters separated by a semicolon. Single-param
     * `\x1b[32m` fails to parse and corrupts the palette-bank selector
     * (which is why this whole screen previously rendered blank).
     * Two-param form: parameter=color (30-37), intensity=brightness.
     * intensity=1 picks the "bright" variant via parameter += 8. */
    "\x1b[32;1m",   /* checksum   — bright green   (palette bank 10) */
    "\x1b[33;1m",   /* non-crypto — bright yellow  (palette bank 11) */
    "\x1b[36;1m"    /* crypto     — bright cyan    (palette bank 14) */
};
#define ANSI_RESET    "\x1b[37;1m"   /* bright white (bank 15) = default */
#define ANSI_DIM      "\x1b[37;0m"   /* normal white (bank 7)            */
#define ANSI_HILITE   "\x1b[37;1m"   /* bright white                     */

typedef struct {
    const char *name;
    hash_fn     fn;
    uint8_t     digest_len;
    uint16_t    iters;            /* base iters at 1024 B */
    uint8_t     tier;
} bench_algo;

static const bench_algo ALGOS[] = {
    /* --- TOP SCREEN: tiny/fast non-cryptographic (16 algos) --- */
    { "CRC8  ", hash_crc8,            1, 200u, TIER_CHECKSUM  },
    { "CRC16 ", hash_crc16,           2, 200u, TIER_CHECKSUM  },
    { "CRC32 ", hash_crc32,           4, 200u, TIER_CHECKSUM  },
    { "CRC64 ", hash_crc64,           8, 100u, TIER_CHECKSUM  },
    { "ADL32 ", hash_adler32,         4, 400u, TIER_CHECKSUM  },
    { "FLT16 ", hash_fletcher16,      2, 400u, TIER_CHECKSUM  },
    { "FLT32 ", hash_fletcher32,      4, 400u, TIER_CHECKSUM  },
    { "FLT64 ", hash_fletcher64,      8, 400u, TIER_CHECKSUM  },
    { "PRSN8 ", hash_pearson,         1, 800u, TIER_NONCRYPTO },
    { "KNUTH ", hash_knuth,           4, 400u, TIER_NONCRYPTO },
    { "OAT   ", hash_jenkins_oat,     4, 400u, TIER_NONCRYPTO },
    { "PJW   ", hash_pjw_elf,         4, 400u, TIER_NONCRYPTO },
    { "SDBM  ", hash_sdbm,            4, 400u, TIER_NONCRYPTO },
    { "DJB2  ", hash_djb2,            4, 400u, TIER_NONCRYPTO },
    { "FNV1A ", hash_fnv1a32,         4, 200u, TIER_NONCRYPTO },
    { "MMUR3 ", hash_murmur3,         4, 200u, TIER_NONCRYPTO },
    /* --- BOTTOM SCREEN: modern non-crypto + cryptographic (16 algos) --- */
    { "M3-128", hash_murmur3_128,    16, 200u, TIER_NONCRYPTO },
    { "XXH32 ", hash_xxh32,           4, 400u, TIER_NONCRYPTO },
    { "XXH64 ", hash_xxh64,           8, 400u, TIER_NONCRYPTO },
    { "SIP24 ", hash_siphash24,       8, 200u, TIER_NONCRYPTO },
    { "MD4   ", hash_md4,            16, 100u, TIER_CRYPTO    },
    { "MD5   ", hash_md5,            16, 100u, TIER_CRYPTO    },
    { "RMD160", hash_ripemd160,      20,  50u, TIER_CRYPTO    },
    { "SHA1  ", hash_sha1,           20, 100u, TIER_CRYPTO    },
    { "SHA256", hash_sha256,         32,  50u, TIER_CRYPTO    },
    { "SHA3  ", hash_sha3_256,       32,  30u, TIER_CRYPTO    },
    { "BLK2S ", hash_blake2s,        32,  50u, TIER_CRYPTO    },
    { "SHA512", hash_sha512,         64,  30u, TIER_CRYPTO    },
    { "SHA3L ", hash_sha3_512,       64,  20u, TIER_CRYPTO    },
    { "HSHA2 ", hash_hmac_sha256,    32,  30u, TIER_CRYPTO    },
    { "PBKDF2", hash_pbkdf2_sha256,  32,   5u, TIER_CRYPTO    },
    { "AESCBC", hash_aes_cbc_mac,    16, 100u, TIER_CRYPTO    }
};
#define NUM_ALGOS         ((uint8_t)(sizeof(ALGOS) / sizeof(ALGOS[0])))
#define ALGOS_PER_SCREEN  16u

/* Buffer-size sweep. */
#define BENCH_SIZE_COUNT     3u
#define HEADLINE_SIZE_IDX    2u   /* index of 1024 B in BENCH_SIZES[]   */
static const uint16_t BENCH_SIZES[BENCH_SIZE_COUNT]      = {  64u, 256u, 1024u };
static const uint8_t  BENCH_SIZE_SCALE[BENCH_SIZE_COUNT] = {  16u,   4u,    1u };

typedef struct {
    uint32_t us_per;
    uint32_t kb_per_s;
    uint8_t  hash[4];           /* meaningful only for headline size */
} bench_result;

static bench_result results[NUM_ALGOS][BENCH_SIZE_COUNT];
static uint8_t      fastest_in_tier_flag[NUM_ALGOS];

/* Sort + display state. */
#define SORT_DEFAULT 0u
#define SORT_SPEED   1u
#define SORT_NAME    2u
#define NUM_SORT_MODES 3u
static uint8_t sort_mode = SORT_DEFAULT;
static uint8_t sort_indices[NUM_ALGOS];

#define MODE_SINGLE 0u
#define MODE_MATRIX 1u
static uint8_t display_mode = MODE_SINGLE;

static uint32_t total_us            = 0u;
static uint8_t  fastest_overall_idx = 0xFFu;

/* ---- helpers --------------------------------------------------------- */
static void fill_buffer(void) {
    uint16_t i;
    for (i = 0; i < BENCH_BUF_LEN; i++) {
        buffer[i] = (uint8_t)((i * 31u + 7u) & 0xFFu);
    }
}

static const char *sort_label(void) {
    if (sort_mode == SORT_SPEED) return "by-speed";
    if (sort_mode == SORT_NAME)  return "by-name";
    return "category";
}

static const char *mode_label(void) {
    return (display_mode == MODE_MATRIX) ? "matrix" : "single";
}

static int name_cmp6(const char *a, const char *b) {
    uint8_t i;
    for (i = 0; i < 6u; i++) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return  1;
    }
    return 0;
}

static void compute_order(void) {
    uint8_t i, j, tmp;
    for (i = 0; i < NUM_ALGOS; i++) sort_indices[i] = i;

    if (sort_mode == SORT_SPEED) {
        for (i = 0; i < (uint8_t)(NUM_ALGOS - 1u); i++) {
            for (j = 0; j < (uint8_t)(NUM_ALGOS - 1u - i); j++) {
                if (results[sort_indices[j]    ][HEADLINE_SIZE_IDX].us_per >
                    results[sort_indices[j + 1u]][HEADLINE_SIZE_IDX].us_per) {
                    tmp = sort_indices[j];
                    sort_indices[j] = sort_indices[j + 1u];
                    sort_indices[j + 1u] = tmp;
                }
            }
        }
    } else if (sort_mode == SORT_NAME) {
        for (i = 0; i < (uint8_t)(NUM_ALGOS - 1u); i++) {
            for (j = 0; j < (uint8_t)(NUM_ALGOS - 1u - i); j++) {
                if (name_cmp6(ALGOS[sort_indices[j]].name,
                              ALGOS[sort_indices[j + 1u]].name) > 0) {
                    tmp = sort_indices[j];
                    sort_indices[j] = sort_indices[j + 1u];
                    sort_indices[j + 1u] = tmp;
                }
            }
        }
    }
}

static void compute_tier_flags(void) {
    uint8_t  i, t;
    uint32_t best[NUM_TIERS]     = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu };
    uint8_t  best_idx[NUM_TIERS] = { 0xFFu, 0xFFu, 0xFFu };
    uint32_t best_overall        = 0xFFFFFFFFu;

    fastest_overall_idx = 0xFFu;

    for (i = 0; i < NUM_ALGOS; i++) {
        fastest_in_tier_flag[i] = 0u;
        uint32_t us = results[i][HEADLINE_SIZE_IDX].us_per;
        t = ALGOS[i].tier;
        if (us < best[t]) {
            best[t]     = us;
            best_idx[t] = i;
        }
        if (us < best_overall) {
            best_overall        = us;
            fastest_overall_idx = i;
        }
    }
    for (t = 0; t < NUM_TIERS; t++) {
        if (best_idx[t] != 0xFFu) {
            fastest_in_tier_flag[best_idx[t]] = 1u;
        }
    }
}

/* ---- live status (bottom screen during sweep) ----------------------- */
static void status_clear_bottom(void) {
    consoleSelect(&bottomConsole);
    int r;
    for (r = 8; r <= 14; r++) {
        iprintf("\x1b[%d;0H                                ", r);
    }
}

static void status_running(uint8_t cur, uint8_t total,
                           const bench_algo *alg, uint16_t size_bytes) {
    consoleSelect(&bottomConsole);
    iprintf("\x1b[8;0H%s >>> running sweep <<<        %s",
            ANSI_HILITE, ANSI_RESET);
    iprintf("\x1b[10;0H  %2u / %2u  %s%s%s",
            (unsigned)cur, (unsigned)total,
            TIER_ANSI[alg->tier], alg->name, ANSI_RESET);
    iprintf("\x1b[11;0H  tier: %-10s  size: %4u B    ",
            TIER_NAMES[alg->tier], (unsigned)size_bytes);
}

/* ---- render --------------------------------------------------------- */
static void render_table_section_single(PrintConsole *cons,
                                        uint8_t start, uint8_t end,
                                        const char *header) {
    consoleSelect(cons);
    consoleClear();

    iprintf("\x1b[0;0H%s", header);
    iprintf("\x1b[1;0HALGO   HASH       us KB/s");

    for (uint8_t i = start; i < end; i++) {
        uint8_t      algo_idx = sort_indices[i];
        bench_result *r       = &results[algo_idx][HEADLINE_SIZE_IDX];
        iprintf("\x1b[%d;0H%s%s %02X%02X%02X%02X %6lu %4lu %c%s",
                (uint8_t)(i - start + 2u),
                TIER_ANSI[ALGOS[algo_idx].tier],
                ALGOS[algo_idx].name,
                r->hash[0], r->hash[1], r->hash[2], r->hash[3],
                (unsigned long)r->us_per,
                (unsigned long)r->kb_per_s,
                fastest_in_tier_flag[algo_idx] ? '*' : ' ',
                ANSI_RESET);
    }
}

static void render_table_section_matrix(PrintConsole *cons,
                                        uint8_t start, uint8_t end,
                                        const char *header) {
    consoleSelect(cons);
    consoleClear();

    iprintf("\x1b[0;0H%s", header);
    iprintf("\x1b[1;0HALGO    64B  256B  1KB *");

    for (uint8_t i = start; i < end; i++) {
        uint8_t algo_idx = sort_indices[i];
        iprintf("\x1b[%d;0H%s%s %4lu %5lu %5lu %c%s",
                (uint8_t)(i - start + 2u),
                TIER_ANSI[ALGOS[algo_idx].tier],
                ALGOS[algo_idx].name,
                (unsigned long)results[algo_idx][0].kb_per_s,
                (unsigned long)results[algo_idx][1].kb_per_s,
                (unsigned long)results[algo_idx][2].kb_per_s,
                fastest_in_tier_flag[algo_idx] ? '*' : ' ',
                ANSI_RESET);
    }
}

static void render_bottom_stats_footer(void) {
    uint32_t cs    = total_us / 10000u;
    uint32_t whole = cs / 100u;
    uint32_t frac  = cs % 100u;

    consoleSelect(&bottomConsole);
    iprintf("\x1b[19;0Hsweep done in %lu.%02lu s   ",
            (unsigned long)whole, (unsigned long)frac);
    if (fastest_overall_idx != 0xFFu) {
        iprintf("\x1b[20;0Hfastest: %s%s%s  %lu us/KB",
                TIER_ANSI[ALGOS[fastest_overall_idx].tier],
                ALGOS[fastest_overall_idx].name,
                ANSI_RESET,
                (unsigned long)results[fastest_overall_idx][HEADLINE_SIZE_IDX].us_per);
    }
    iprintf("\x1b[21;0Hsort: %-10s mode: %-6s", sort_label(), mode_label());
    iprintf("\x1b[22;0HA=sort SELECT=mode B=rerun");
    iprintf("\x1b[23;0H%sgreen%s=chk %syel%s=nc %scyn%s=crypto",
            TIER_ANSI[TIER_CHECKSUM], ANSI_RESET,
            TIER_ANSI[TIER_NONCRYPTO], ANSI_RESET,
            TIER_ANSI[TIER_CRYPTO], ANSI_RESET);
}

static void render_all(void) {
    if (display_mode == MODE_MATRIX) {
        render_table_section_matrix(&topConsole, 0u, ALGOS_PER_SCREEN,
            "hash-bench-nds matrix mode");
        render_table_section_matrix(&bottomConsole, ALGOS_PER_SCREEN, NUM_ALGOS,
            "  -- KB/s @ 64/256/1024 B --");
    } else {
        render_table_section_single(&topConsole, 0u, ALGOS_PER_SCREEN,
            "hash-bench-nds  1024 B");
        render_table_section_single(&bottomConsole, ALGOS_PER_SCREEN, NUM_ALGOS,
            "  >> next 16 algos <<");
    }
    render_bottom_stats_footer();
}

/* ---- bench ---------------------------------------------------------- */
static void run_one_at_size(const bench_algo *alg, uint16_t size_bytes,
                            uint32_t actual_iters, bench_result *out_res) {
    uint32_t cycles, us_total_alg, us_per, kb_per_s;
    uint32_t k;
    uint8_t  d;

    for (d = 0; d < HASH_MAX_DIGEST; d++) digest[d] = 0u;

    cpuStartTiming(0);
    for (k = 0; k < actual_iters; k++) {
        alg->fn(buffer, size_bytes, digest);
    }
    cycles       = cpuGetTiming();
    us_total_alg = timerTicks2usec(cycles);
    us_per       = us_total_alg / actual_iters;
    /* KB/s = bytes_per_iter * 1e6 / (us_per * 1024). */
    kb_per_s     = (us_per > 0u)
                   ? ((uint32_t)size_bytes * 1000000u) / (us_per * 1024u)
                   : 0u;

    out_res->us_per   = us_per;
    out_res->kb_per_s = kb_per_s;
    out_res->hash[0]  = digest[0];
    out_res->hash[1]  = digest[1];
    out_res->hash[2]  = digest[2];
    out_res->hash[3]  = digest[3];
}

static void run_sweep(void) {
    consoleSelect(&topConsole);
    consoleClear();
    iprintf("\x1b[0;0Hhash-bench-nds  1024 B");
    iprintf("\x1b[1;0HALGO   HASH       us KB/s");
    iprintf("\x1b[3;0H  sweep starting...");

    consoleSelect(&bottomConsole);
    consoleClear();
    iprintf("\x1b[0;0Hhash-bench-nds status panel");
    iprintf("\x1b[2;0H ARM9 @ 33.514 MHz");
    iprintf("\x1b[3;0H 32 algos x 3 sizes (64/256/1KB)");
    iprintf("\x1b[5;0H buf[i] = (i*31+7) & 0xFF");

    total_us = 0u;
    for (uint8_t a = 0; a < NUM_ALGOS; a++) {
        for (uint8_t s = 0; s < BENCH_SIZE_COUNT; s++) {
            uint32_t actual_iters =
                (uint32_t)ALGOS[a].iters * BENCH_SIZE_SCALE[s];
            status_running((uint8_t)(a + 1u), NUM_ALGOS, &ALGOS[a],
                           BENCH_SIZES[s]);
            run_one_at_size(&ALGOS[a], BENCH_SIZES[s], actual_iters,
                            &results[a][s]);
            total_us += results[a][s].us_per * actual_iters;
        }
    }

    compute_tier_flags();
    compute_order();
    status_clear_bottom();
    render_all();
}

/* ---- input ----------------------------------------------------------- */
static int wait_for_action(void) {
    while (pmMainLoop()) {
        swiWaitForVBlank();
        scanKeys();
        int down = keysDown();
        if (down & KEY_B)      return 1;
        if (down & KEY_START)  return 0;
        if (down & KEY_A) {
            sort_mode = (uint8_t)((sort_mode + 1u) % NUM_SORT_MODES);
            compute_order();
            render_all();
        }
        if (down & KEY_SELECT) {
            display_mode = (uint8_t)((display_mode + 1u) % 2u);
            render_all();
        }
    }
    return 0;
}

/* ---- entry ----------------------------------------------------------- */
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    videoSetMode(MODE_0_2D);
    videoSetModeSub(MODE_0_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankC(VRAM_C_SUB_BG);

    consoleInit(&topConsole,    3, BgType_Text4bpp, BgSize_T_256x256, 31, 0, true,  true);
    consoleInit(&bottomConsole, 3, BgType_Text4bpp, BgSize_T_256x256, 31, 0, false, true);

    consoleSelect(&topConsole);
    iprintf("hash-bench-nds booting...");
    consoleSelect(&bottomConsole);
    iprintf("filling 1 KB workload buffer...");

    fill_buffer();

    do {
        run_sweep();
    } while (wait_for_action());

    return 0;
}
