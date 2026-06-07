/**
 * Embedded Memory Allocator -- portable self-test
 * Copyright (c) 2020, Ovidiu Andronachi <ovidiu.andronachi@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 *
 * https://en.wikipedia.org/wiki/MIT_License#License_terms
 */

/**
 * Portable, black-box self-test for the Embedded Memory Allocator.
 *
 * Build it together with the allocator sources, e.g.:
 *   cc -std=c99 -Wall -Wextra emb_alloc.c emb_alloc_util.c emb_alloc_test.c \
 *      -pthread -o emb_alloc_test && ./emb_alloc_test
 * (-pthread is only needed on some Linux libc versions; drop it on Windows/MSVC.
 *  This test never creates a threadsafe pool, so no runtime threading is used.)
 *
 * The test drives the allocator through the public emb_alloc.h API only. The few
 * cases that must point inside a block (forged-free, interior-pointer) derive
 * every offset from sizeof(size_t) alone -- exactly how the allocator computes
 * its alignment -- so the test is correct on 32- and 64-bit targets without
 * including any internal header.
 *
 * Cases (the hardening findings from the audit):
 *   1  create/destroy + basic malloc/free round-trip
 *   2  NULL / zero-size edge cases for malloc, free and realloc
 *   3  multi-block allocation; free splits the run back into reusable blocks
 *   4  forged inner-block free is rejected (out-of-band allocation-start oracle)
 *   5  double free is rejected
 *   6  foreign pointer and interior / unaligned pointer are rejected
 *   7  realloc grow (in place and relocate) and shrink preserve the contents
 *   8  buffer overflow on the unused tail is detected on free
 *   9  randomized stress: live allocations never alias / overlap each other
 */

#include "emb_alloc.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---- block layout, derived only from sizeof(size_t) (see emb_alloc_internal.h) ---- */
#define EA_ALIGN          (2u * sizeof (size_t))            /* GNU libc alignment      */
#define EA_BLOCK_CONTROL  (3u * EA_ALIGN)                   /* 2 markers + 2 counters  */
#define EA_STRIDE(d)      ((size_t) (d) + EA_BLOCK_CONTROL) /* head-to-head distance   */

/* ---- minimal harness ---- */
static int g_checks;
static int g_fails;

#define CHECK(cond, what) \
    do { \
        ++g_checks; \
        if (!(cond)) { \
            ++g_fails; \
            printf ("  [FAIL] %s (line %d)\n", (what), __LINE__); \
        } \
    } while (0)

#define RUN(fn) do { printf ("- %s\n", #fn); fn (); } while (0)

/** Reads back the pool's last error code (the message is intentionally ignored). */
static EmbAllocErrors LastError (EmbAllocMempool pool)
{
    EmbAllocErrors code = kEmbAllocNoErr;
    /* message == NULL makes the call return false but it still writes the code. */
    EmbAllocGetLastErrorCodeAndMessage (pool, &code, NULL, 0);
    return code;
}

/** Creates a single-category pool of `count` blocks whose usable size is 32 bytes. */
static EmbAllocMempool MakePool32 (size_t count, bool overflow_checks)
{
    EmbAllocMemPoolSettings s;
    memset (&s, 0, sizeof s);
    s.num_32_bytes_blocks = count;
    s.total_size = count * 32u;
    s.full_overflow_checks = overflow_checks;
    return EmbAllocCreate (&s);
}

/** Writes a position-dependent fingerprint over [p, p + n). */
static void Fingerprint (unsigned char* p, size_t n, unsigned char seed)
{
    size_t i;
    for (i = 0; i < n; ++i) { p[i] = (unsigned char) (seed + (unsigned char) i); }
}

/** Returns 1 iff [p, p + n) still holds the fingerprint written with `seed`. */
static int FingerprintOk (const unsigned char* p, size_t n, unsigned char seed)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        if (p[i] != (unsigned char) (seed + (unsigned char) i)) { return 0; }
    }
    return 1;
}

/* ---- cases ---- */

static void TestBasic (void)
{
    EmbAllocMempool pool = MakePool32 (8, false);
    void* p;

    CHECK (NULL != pool, "create pool");
    if (NULL == pool) { return; }

    p = EmbAllocMalloc (pool, 16);
    CHECK (NULL != p, "malloc 16");
    if (NULL != p) {
        memset (p, 0x5A, 16);                 /* writable, owned region */
        EmbAllocFree (pool, p);
        CHECK (kEmbAllocNoErr == LastError (pool), "free clears error");
    }
    CHECK (EmbAllocDestroy (pool), "destroy pool");
}

static void TestNullZero (void)
{
    EmbAllocMempool pool = MakePool32 (8, false);
    void* p;

    if (NULL == pool) { CHECK (0, "create pool"); return; }

    CHECK (NULL == EmbAllocMalloc (pool, 0), "malloc(0) returns NULL");
    EmbAllocFree (pool, NULL);                                /* silent no-op */
    CHECK (kEmbAllocNoErr == LastError (pool), "free(NULL) is a no-op");

    /* realloc(NULL, n) behaves like malloc(n); realloc(p, 0) behaves like free. */
    p = EmbAllocRealloc (pool, NULL, 24);
    CHECK (NULL != p, "realloc(NULL, n) acts as malloc");
    CHECK (NULL == EmbAllocRealloc (pool, p, 0), "realloc(p, 0) frees, returns NULL");

    EmbAllocDestroy (pool);
}

static void TestMultiBlock (void)
{
    EmbAllocMempool pool = MakePool32 (16, false);
    unsigned char* p;

    if (NULL == pool) { CHECK (0, "create pool"); return; }

    /* 100 bytes cannot fit one 32-byte block, so this spans several blocks. */
    p = (unsigned char*) EmbAllocMalloc (pool, 100);
    CHECK (NULL != p, "multi-block malloc(100)");
    if (NULL != p) {
        Fingerprint (p, 100, 0x11);
        CHECK (FingerprintOk (p, 100, 0x11), "multi-block data integrity");
        EmbAllocFree (pool, p);
        CHECK (kEmbAllocNoErr == LastError (pool), "multi-block free ok");

        /* The run must be split back into free blocks: all 16 are reusable. */
        {
            void* a[16];
            size_t k, got = 0;
            for (k = 0; k < 16; ++k) { a[k] = EmbAllocMalloc (pool, 32); if (a[k]) { ++got; } }
            CHECK (16 == got, "freed multi-block run fully reusable");
            for (k = 0; k < 16; ++k) { if (a[k]) { EmbAllocFree (pool, a[k]); } }
        }
    }
    EmbAllocDestroy (pool);
}

static void TestForgedInnerFree (void)
{
    EmbAllocMempool pool = MakePool32 (16, false);
    unsigned char* p;

    if (NULL == pool) { CHECK (0, "create pool"); return; }

    /* >= 2 blocks in the 32-byte category, so block #1 is a live inner block. */
    p = (unsigned char*) EmbAllocMalloc (pool, 100);
    CHECK (NULL != p, "alloc for forge test");
    if (NULL != p) {
        void* forged = p + EA_STRIDE (32);    /* payload of the 2nd block: not a head */

        Fingerprint (p, 100, 0x77);
        EmbAllocFree (pool, forged);
        CHECK (kEmbAllocPointerParamError == LastError (pool),
               "forged inner-block free is rejected");
        CHECK (FingerprintOk (p, 100, 0x77),
               "allocation intact after rejected forged free");

        /* The genuine head still frees cleanly afterwards. */
        EmbAllocFree (pool, p);
        CHECK (kEmbAllocNoErr == LastError (pool), "genuine free after forge ok");
    }
    EmbAllocDestroy (pool);
}

static void TestDoubleFree (void)
{
    EmbAllocMempool pool = MakePool32 (8, false);
    void* p;

    if (NULL == pool) { CHECK (0, "create pool"); return; }

    p = EmbAllocMalloc (pool, 32);
    CHECK (NULL != p, "alloc for double-free");
    if (NULL != p) {
        EmbAllocFree (pool, p);
        CHECK (kEmbAllocNoErr == LastError (pool), "first free ok");
        EmbAllocFree (pool, p);
        CHECK (kEmbAllocPointerParamError == LastError (pool), "double free rejected");
    }
    EmbAllocDestroy (pool);
}

static void TestBadPointers (void)
{
    EmbAllocMempool pool = MakePool32 (8, false);
    unsigned char* p;
    int stack_obj = 0;

    if (NULL == pool) { CHECK (0, "create pool"); return; }

    /* Foreign pointer: not inside the pool at all. */
    EmbAllocFree (pool, &stack_obj);
    CHECK (kEmbAllocPointerParamError == LastError (pool), "foreign pointer rejected");

    p = (unsigned char*) EmbAllocMalloc (pool, 32);
    CHECK (NULL != p, "alloc for interior test");
    if (NULL != p) {
        /* Interior / unaligned: in the pool but not on a block boundary. */
        EmbAllocFree (pool, p + 1);
        CHECK (kEmbAllocPointerParamError == LastError (pool), "interior pointer rejected");

        /* The real allocation is untouched and still freeable. */
        EmbAllocFree (pool, p);
        CHECK (kEmbAllocNoErr == LastError (pool), "genuine free after interior reject");
    }
    EmbAllocDestroy (pool);
}

static void TestRealloc (void)
{
    EmbAllocMempool pool = MakePool32 (16, false);
    unsigned char *a, *b, *p, *q;

    if (NULL == pool) { CHECK (0, "create pool"); return; }

    /* (a) grow in place: the only allocation, so the next block is free. */
    p = (unsigned char*) EmbAllocMalloc (pool, 32);
    CHECK (NULL != p, "alloc for grow-in-place");
    if (NULL != p) {
        Fingerprint (p, 32, 0x20);
        q = (unsigned char*) EmbAllocRealloc (pool, p, 40);    /* needs 1 more block */
        CHECK (q == p, "grow in place keeps the pointer");
        CHECK (FingerprintOk (q, 32, 0x20), "grow in place preserves data");
        EmbAllocFree (pool, q);
    }

    /* (b) shrink: stays in place, keeps the leading bytes. */
    p = (unsigned char*) EmbAllocMalloc (pool, 100);
    CHECK (NULL != p, "alloc for shrink");
    if (NULL != p) {
        Fingerprint (p, 100, 0x30);
        q = (unsigned char*) EmbAllocRealloc (pool, p, 40);
        CHECK (q == p, "shrink keeps the pointer");
        CHECK (FingerprintOk (q, 40, 0x30), "shrink preserves leading data");
        EmbAllocFree (pool, q);
    }

    /* (c) relocate: the block after `a` is taken by `b`, so growing `a` must move. */
    a = (unsigned char*) EmbAllocMalloc (pool, 32);
    b = (unsigned char*) EmbAllocMalloc (pool, 32);
    CHECK ((NULL != a) && (NULL != b), "allocs for relocate");
    if ((NULL != a) && (NULL != b)) {
        Fingerprint (a, 32, 0x40);
        q = (unsigned char*) EmbAllocRealloc (pool, a, 80);    /* neighbour busy -> move */
        CHECK (NULL != q, "relocate returns memory");
        CHECK (q != a, "relocate moves the allocation");
        CHECK (FingerprintOk (q, 32, 0x40), "relocate preserves data");
        EmbAllocFree (pool, b);
        if (NULL != q) { EmbAllocFree (pool, q); }
    }
    EmbAllocDestroy (pool);
}

static void TestOverflowDetect (void)
{
    EmbAllocMempool pool = MakePool32 (8, true);    /* full_overflow_checks = true */
    unsigned char* p;

    if (NULL == pool) { CHECK (0, "create pool"); return; }

    /* Clean: request < block size and never touch the tail -> no overflow. */
    p = (unsigned char*) EmbAllocMalloc (pool, 20);
    CHECK (NULL != p, "alloc for clean tail");
    if (NULL != p) {
        Fingerprint (p, 20, 0x01);
        EmbAllocFree (pool, p);
        CHECK (kEmbAllocOverflow != LastError (pool), "clean tail is not flagged");
    }

    /* Tainted: write one byte past the requested size. It is still inside the
       32-byte block (a defined write), but it dirties the guarded tail. */
    p = (unsigned char*) EmbAllocMalloc (pool, 20);
    CHECK (NULL != p, "alloc for tainted tail");
    if (NULL != p) {
        p[25] = 0x00;                           /* tail is [20, 32); 0x00 != INIT fill */
        EmbAllocFree (pool, p);
        CHECK (kEmbAllocOverflow == LastError (pool), "tail overflow detected");
    }
    EmbAllocDestroy (pool);
}

#define STRESS_SLOTS 48
#define STRESS_ITERS 6000u

static void TestStressNoAlias (void)
{
    EmbAllocMemPoolSettings s;
    EmbAllocMempool pool;
    unsigned char* ptr[STRESS_SLOTS];
    size_t len[STRESS_SLOTS];
    unsigned char tag[STRESS_SLOTS];
    uint32_t rng = 0x00C0FFEEu;     /* fixed seed -> reproducible, no time() needed */
    size_t i, k;
    int corrupted = 0;

    /* A varied pool so both single- and multi-block paths are exercised. */
    memset (&s, 0, sizeof s);
    s.num_32_bytes_blocks  = 64;
    s.num_64_bytes_blocks  = 16;
    s.num_128_bytes_blocks = 8;
    s.num_256_bytes_blocks = 4;
    s.total_size = (64u * 32u) + (16u * 64u) + (8u * 128u) + (4u * 256u);
    pool = EmbAllocCreate (&s);
    if (NULL == pool) { CHECK (0, "create stress pool"); return; }

    for (i = 0; i < STRESS_SLOTS; ++i) { ptr[i] = NULL; len[i] = 0; tag[i] = 0; }

    for (i = 0; (i < STRESS_ITERS) && !corrupted; ++i) {
        rng = (rng * 1664525u) + 1013904223u;          /* Numerical Recipes LCG */
        k = (rng >> 8) % STRESS_SLOTS;

        if (NULL == ptr[k]) {
            size_t want = 1u + ((rng >> 3) % 200u);    /* 1..200 bytes */
            unsigned char* q = (unsigned char*) EmbAllocMalloc (pool, want);
            if (NULL != q) {                           /* NULL just means "pool full" */
                ptr[k] = q;
                len[k] = want;
                tag[k] = (unsigned char) ((rng >> 16) | 1u);
                Fingerprint (q, want, tag[k]);
            }
        } else {
            if (!FingerprintOk (ptr[k], len[k], tag[k])) { corrupted = 1; }
            EmbAllocFree (pool, ptr[k]);
            ptr[k] = NULL;
            len[k] = 0;
        }

        /* Periodically sweep every live allocation for cross-contamination. */
        if (0 == (i & 0x3F)) {
            for (k = 0; k < STRESS_SLOTS; ++k) {
                if ((NULL != ptr[k]) && !FingerprintOk (ptr[k], len[k], tag[k])) {
                    corrupted = 1;
                    break;
                }
            }
        }
    }

    CHECK (!corrupted, "no aliasing/overlap across live allocations");

    for (k = 0; k < STRESS_SLOTS; ++k) { if (ptr[k]) { EmbAllocFree (pool, ptr[k]); } }
    EmbAllocDestroy (pool);
}

static void TestInvalidHandles (void)
{
    int x = 0;

    /* Every public entry point must tolerate a non-mempool (NULL) handle.
       A non-NULL but bogus handle is undefined by contract, so it is not tested. */
    CHECK (NULL == EmbAllocCreate (NULL), "create(NULL) returns NULL");
    CHECK (NULL == EmbAllocMalloc (NULL, 16), "malloc(NULL pool) returns NULL");
    EmbAllocFree (NULL, &x);                            /* must be a no-op, not a crash */
    CHECK (NULL == EmbAllocRealloc (NULL, NULL, 16), "realloc(NULL pool) returns NULL");
    CHECK (!EmbAllocDestroy (NULL), "destroy(NULL) returns false");
}

static void TestSettingsValidation (void)
{
    EmbAllocMemPoolSettings s;
    EmbAllocMempool pool;

    /* A block count whose byte size overflows size_t is rejected at creation. */
    memset (&s, 0, sizeof s);
    s.num_32_bytes_blocks = SIZE_MAX;
    CHECK (NULL == EmbAllocCreate (&s), "settings size overflow is rejected");

    /* total_size disagreeing with the per-category counts is non-fatal: the pool
       is created and usable, but the inconsistency is flagged. */
    memset (&s, 0, sizeof s);
    s.num_32_bytes_blocks = 4;
    s.total_size = 999u;                               /* != 4 * 32 */
    pool = EmbAllocCreate (&s);
    CHECK (NULL != pool, "inconsistent settings still create a pool");
    if (NULL != pool) {
        CHECK (kEmbAllocInconsistentSettings == LastError (pool),
               "inconsistent settings are flagged");
        CHECK (NULL != EmbAllocMalloc (pool, 32), "inconsistent-settings pool still usable");
        EmbAllocDestroy (pool);
    }
}

static void TestGetSettings (void)
{
    EmbAllocMemPoolSettings in, out;
    EmbAllocMempool pool;

    memset (&in, 0, sizeof in);
    in.num_32_bytes_blocks = 8;
    in.total_size = 8u * 32u;
    in.full_overflow_checks = true;
    in.init_allocated_memory = true;
    pool = EmbAllocCreate (&in);
    if (NULL == pool) { CHECK (0, "create pool"); return; }

    memset (&out, 0, sizeof out);
    CHECK (EmbAllocGetSettings (pool, &out), "GetSettings succeeds");
    CHECK (8u == out.num_32_bytes_blocks, "GetSettings returns the block counts");
    CHECK ((8u * 32u) == out.total_size, "GetSettings returns the total size");
    CHECK (out.full_overflow_checks && out.init_allocated_memory,
           "GetSettings returns the flags");

    CHECK (!EmbAllocGetSettings (pool, NULL), "GetSettings(NULL output) fails");
    CHECK (kEmbAllocOutputParamError == LastError (pool), "GetSettings(NULL) flags an output error");

    EmbAllocDestroy (pool);
}

static void TestInitZeroing (void)
{
    EmbAllocMemPoolSettings s;
    EmbAllocMempool pool;
    unsigned char* p;
    size_t i;
    int all_zero = 1;

    memset (&s, 0, sizeof s);
    s.num_32_bytes_blocks = 8;
    s.total_size = 8u * 32u;
    s.init_allocated_memory = true;
    pool = EmbAllocCreate (&s);
    if (NULL == pool) { CHECK (0, "create pool"); return; }

    p = (unsigned char*) EmbAllocMalloc (pool, 32);
    CHECK (NULL != p, "alloc from init pool");
    if (NULL != p) {
        for (i = 0; i < 32; ++i) { if (0 != p[i]) { all_zero = 0; break; } }
        CHECK (all_zero, "init_allocated_memory zeroes the allocation");
        EmbAllocFree (pool, p);
    }
    EmbAllocDestroy (pool);
}

static void TestCrossCategory (void)
{
    EmbAllocMemPoolSettings s;
    EmbAllocMempool pool;

    /* 1x32 + 1x64: 65 bytes need two same-size blocks, which neither category owns. */
    memset (&s, 0, sizeof s);
    s.num_32_bytes_blocks = 1;
    s.num_64_bytes_blocks = 1;
    s.total_size = 32u + 64u;
    pool = EmbAllocCreate (&s);
    if (NULL == pool) { CHECK (0, "create 1x32+1x64 pool"); return; }
    CHECK (NULL == EmbAllocMalloc (pool, 65), "allocation spanning categories fails");
    CHECK (kEmbAllocNoMemory == LastError (pool), "cross-category failure reports no-memory");
    EmbAllocDestroy (pool);

    /* 2x32: the same 65 bytes now fit in two continuous same-size blocks. */
    memset (&s, 0, sizeof s);
    s.num_32_bytes_blocks = 2;
    s.total_size = 2u * 32u;
    pool = EmbAllocCreate (&s);
    if (NULL == pool) { CHECK (0, "create 2x32 pool"); return; }
    CHECK (NULL != EmbAllocMalloc (pool, 65), "allocation across 2 blocks of one category succeeds");
    EmbAllocDestroy (pool);
}

static void TestExhaustion (void)
{
    EmbAllocMempool pool = MakePool32 (4, false);
    void* a[4];
    size_t k, got = 0;

    if (NULL == pool) { CHECK (0, "create pool"); return; }

    for (k = 0; k < 4; ++k) { a[k] = EmbAllocMalloc (pool, 32); if (a[k]) { ++got; } }
    CHECK (4 == got, "pool fills to capacity");
    CHECK (NULL == EmbAllocMalloc (pool, 32), "allocation past capacity fails");
    CHECK (kEmbAllocNoMemory == LastError (pool), "full pool reports no-memory");

    EmbAllocFree (pool, a[0]);
    a[0] = EmbAllocMalloc (pool, 32);
    CHECK (NULL != a[0], "capacity is recovered after a free");

    for (k = 0; k < 4; ++k) { if (a[k]) { EmbAllocFree (pool, a[k]); } }
    EmbAllocDestroy (pool);
}

static void TestErrorMessage (void)
{
    EmbAllocMempool pool = MakePool32 (4, false);
    EmbAllocErrors code = kEmbAllocNoErr;
    char msg[256];
    char tiny[4];
    int dummy = 0;

    if (NULL == pool) { CHECK (0, "create pool"); return; }

    EmbAllocFree (pool, &dummy);                       /* force an error to be recorded */

    CHECK (EmbAllocGetLastErrorCodeAndMessage (pool, &code, msg, sizeof msg),
           "error code and message retrieved");
    CHECK (kEmbAllocPointerParamError == code, "retrieved error code matches");
    CHECK (strlen (msg) > 0, "retrieved error message is non-empty");
    CHECK (!EmbAllocGetLastErrorCodeAndMessage (pool, &code, tiny, sizeof tiny),
           "a too-small message buffer is rejected");

    EmbAllocDestroy (pool);
}

static void TestReallocEdges (void)
{
    EmbAllocMempool pool = MakePool32 (16, false);
    unsigned char *p, *q;

    if (NULL == pool) { CHECK (0, "create pool"); return; }

    p = (unsigned char*) EmbAllocMalloc (pool, 100);
    CHECK (NULL != p, "alloc for realloc edges");
    if (NULL != p) {
        Fingerprint (p, 100, 0x55);

        q = (unsigned char*) EmbAllocRealloc (pool, p, 100);    /* same size */
        CHECK (q == p, "realloc to the same size keeps the pointer");
        CHECK (FingerprintOk (q, 100, 0x55), "same-size realloc preserves data");

        q = (unsigned char*) EmbAllocRealloc (pool, p, 105);    /* grow within capacity */
        CHECK (q == p, "grow within existing capacity keeps the pointer");
        CHECK (FingerprintOk (q, 100, 0x55), "grow within capacity preserves data");

        EmbAllocFree (pool, q);
    }
    EmbAllocDestroy (pool);
}

static void TestEndMarkerGuard (void)
{
    EmbAllocMempool pool = MakePool32 (8, false);  /* full_overflow_checks = false */
    unsigned char* p;

    if (NULL == pool) { CHECK (0, "create pool"); return; }

    /* Fill the whole block (no tail), then clobber the first end-marker byte.
       p[32] is inside the block buffer (a defined write); the always-on end
       marker guard must still report the overflow on free, even with
       full_overflow_checks disabled. */
    p = (unsigned char*) EmbAllocMalloc (pool, 32);
    CHECK (NULL != p, "alloc for end-marker guard");
    if (NULL != p) {
        p[32] = (unsigned char) 0x00;
        EmbAllocFree (pool, p);
        CHECK (kEmbAllocOverflow == LastError (pool),
               "end-marker overflow detected without full checks");
    }
    EmbAllocDestroy (pool);
}

static int g_cb_count;
static EmbAllocErrors g_cb_code;

static void CountingErrorCallback (EmbAllocErrors code, const char* message)
{
    (void) message;
    ++g_cb_count;
    g_cb_code = code;
}

static void TestErrorCallback (void)
{
    EmbAllocMemPoolSettings s;
    EmbAllocMempool pool;
    int dummy = 0;

    memset (&s, 0, sizeof s);
    s.num_32_bytes_blocks = 4;
    s.total_size = 4u * 32u;
    s.error_callback_fn = CountingErrorCallback;
    pool = EmbAllocCreate (&s);
    if (NULL == pool) { CHECK (0, "create pool"); return; }

    g_cb_count = 0;
    g_cb_code = kEmbAllocNoErr;
    EmbAllocFree (pool, &dummy);                        /* foreign pointer -> error -> callback */
    CHECK (g_cb_count > 0, "error callback is invoked");
    CHECK (kEmbAllocPointerParamError == g_cb_code, "error callback receives the code");

    EmbAllocDestroy (pool);
}

static void TestThreadsafeSmoke (void)
{
    EmbAllocMemPoolSettings s;
    EmbAllocMempool pool;
    void* p;

    /* Single-threaded exercise of the threadsafe paths (mutex init / lock /
       unlock / destroy). True concurrency needs platform threads plus a race
       detector and is out of scope for a portable C99 test. */
    memset (&s, 0, sizeof s);
    s.num_32_bytes_blocks = 8;
    s.total_size = 8u * 32u;
    s.threadsafe = true;
    pool = EmbAllocCreate (&s);
    if (NULL == pool) { CHECK (0, "create threadsafe pool"); return; }

    CHECK (kEmbAllocThreadSyncError != LastError (pool), "threadsafe mutex initialized");
    p = EmbAllocMalloc (pool, 32);
    CHECK (NULL != p, "malloc on a threadsafe pool");
    if (NULL != p) {
        EmbAllocFree (pool, p);
        CHECK (kEmbAllocNoErr == LastError (pool), "free on a threadsafe pool");
    }
    CHECK (EmbAllocDestroy (pool), "destroy threadsafe pool");
}

int main (void)
{
    printf ("emb_alloc portable self-test (size_t = %u bytes)\n",
        (unsigned) sizeof (size_t));

    RUN (TestBasic);
    RUN (TestNullZero);
    RUN (TestInvalidHandles);
    RUN (TestSettingsValidation);
    RUN (TestGetSettings);
    RUN (TestInitZeroing);
    RUN (TestMultiBlock);
    RUN (TestCrossCategory);
    RUN (TestExhaustion);
    RUN (TestForgedInnerFree);
    RUN (TestDoubleFree);
    RUN (TestBadPointers);
    RUN (TestErrorMessage);
    RUN (TestRealloc);
    RUN (TestReallocEdges);
    RUN (TestOverflowDetect);
    RUN (TestEndMarkerGuard);
    RUN (TestErrorCallback);
    RUN (TestThreadsafeSmoke);
    RUN (TestStressNoAlias);

    printf ("SUMMARY: checks=%d failures=%d -> %s\n",
        g_checks, g_fails, (0 == g_fails) ? "PASS" : "FAIL");
    return (0 == g_fails) ? 0 : 1;
}
