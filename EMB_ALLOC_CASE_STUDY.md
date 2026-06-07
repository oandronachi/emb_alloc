# emb_alloc - Case Study

## Overview

**`emb_alloc` is a C99 fixed-size memory-pool allocator for systems where memory
behavior should be predictable, bounded, and easy to reason about.** Instead of
asking the platform heap for storage on every allocation, the user creates one
pool up front, defines how many blocks of each size class it contains, and then
allocates from that controlled region through a small `malloc` / `free` /
`realloc`-style API.

The project sits in a practical embedded and systems-programming niche: it is
small enough to embed directly as source files, does not require a framework,
and exposes knobs for zero-initialization, overflow checking, error reporting,
and optional thread serialization.

| Area | Snapshot |
|---|---|
| **Project type** | Embedded-style fixed-pool allocator |
| **Core language** | C99 |
| **Integration model** | Add `emb_alloc.c`, `emb_alloc_util.c`, and headers to a project |
| **Allocation model** | One fixed pool, eight block-size categories, no pool growth after creation |
| **Public API** | `EmbAllocCreate`, `EmbAllocMalloc`, `EmbAllocFree`, `EmbAllocRealloc`, settings and error helpers |
| **Optional features** | Zero-fill, overflow checks, thread serialization, error callback, dump file |
| **Companion tooling** | Portable C self-test and C++ performance benchmark |

---

## The Problem Space

General-purpose heap allocators are flexible, but that flexibility is not always
what a constrained system wants. In embedded software, games, real-time-ish
components, test harnesses, or long-running services, the hard question is often
not "can I allocate memory?" but:

- How much memory can this subsystem use at most?
- What allocation sizes are expected?
- Can allocation behavior be tested before deployment?
- Can memory misuse be detected close to the allocator boundary?
- Can the allocator be dropped into a C project without bringing a large runtime?

`emb_alloc` answers those questions by making the pool shape explicit at
creation time. The caller trades general heap flexibility for a bounded memory
region and a visible block layout.

---

## The Solution: A Fixed Pool With Size Categories

At creation time, the caller fills an `EmbAllocMemPoolSettings` structure. The
settings describe the usable pool size and the number of blocks available in
each category:

| Category | Usable bytes per block |
|---|---:|
| `num_32_bytes_blocks` | 32 |
| `num_64_bytes_blocks` | 64 |
| `num_128_bytes_blocks` | 128 |
| `num_256_bytes_blocks` | 256 |
| `num_512_bytes_blocks` | 512 |
| `num_1k_bytes_blocks` | 1024 |
| `num_2k_bytes_blocks` | 2048 |
| `num_4k_bytes_blocks` | 4096 |

The allocator groups blocks of the same size together inside the pool. A request
is served from the smallest suitable category when possible, or from multiple
continuous blocks inside one category when that is the better fit. The important
constraint is that one allocation never spans several categories. If a request
cannot fit inside one category, it fails even if the total free bytes across the
whole pool look large enough.

That constraint is a deliberate design trade-off: simpler metadata and more
predictable layout in exchange for less flexibility than a general heap.

---

## Memory Layout At A Glance

The pool contains both user storage and allocator metadata. The README describes
the layout in detail; conceptually it looks like this:

```text
pool start marker
settings copy
category table
threading and error state
32-byte block region
64-byte block region
128-byte block region
256-byte block region
512-byte block region
1 KiB block region
2 KiB block region
4 KiB block region
free-block bitmap
allocation-start bitmap
pool end marker
```

Each data block also has control bytes around the user-visible payload. These
markers and counters let the allocator validate pool structure, track allocation
size, and detect some overwrite patterns when overflow checking is enabled.

Two out-of-band bitmaps make the block state explicit:

- The free-block bitmap tracks whether each physical block is occupied.
- The allocation-start bitmap tracks whether a physical block is the first block
  of a live allocation.

Those two pieces of state are central to the current design. The allocator does
not have to infer every important fact from user-adjacent block bytes, which
makes multi-block allocations easier to reason about.

---

## Public API Shape

The public surface is intentionally close to the C allocation model:

| Function | Role |
|---|---|
| `EmbAllocCreate(settings)` | Allocates and initializes one mempool |
| `EmbAllocDestroy(pool)` | Releases the entire mempool |
| `EmbAllocMalloc(pool, size)` | Allocates from the pool |
| `EmbAllocFree(pool, ptr)` | Frees a pointer allocated by this pool |
| `EmbAllocRealloc(pool, ptr, size)` | Resizes an allocation when possible |
| `EmbAllocGetSettings(pool, out)` | Reads back the effective pool settings |
| `EmbAllocGetLastErrorCodeAndMessage(pool, ...)` | Retrieves the last allocator error |

The handle type is opaque (`void*` behind `EmbAllocMempool`), so users interact
with the allocator through the API rather than the internal layout. The contract
is also explicit: the pool handle must be a live handle returned by
`EmbAllocCreate`, and `EmbAllocDestroy` is an exclusive lifetime operation.

---

## Configuration Knobs

The allocator has a small number of settings that change runtime behavior:

| Setting | Effect |
|---|---|
| `init_allocated_memory` | Zeroes newly allocated memory |
| `full_overflow_checks` | Enables broader marker checking during allocator operations |
| `threadsafe` | Serializes operations on the same pool through platform mutex support |
| `error_callback_fn` | Reports allocator errors synchronously to caller code |
| `error_dump_file_name` | Allows dumping pool state on errors when verbose dumping is enabled |

These options keep the default allocator small while allowing a caller to pay for
extra diagnostics or synchronization when a target needs it.

---

## Design Decisions

### 1. Source-Level Integration

The project is distributed as C source and header files rather than as a package
manager artifact. That fits embedded and native projects where build systems,
toolchains, and platform constraints vary widely.

### 2. Fixed Capacity By Construction

The pool never grows or shrinks after creation. This makes memory consumption
visible at startup and keeps allocation failures tied to known capacity instead
of platform heap state.

### 3. Size Classes Instead Of Arbitrary Splits

Eight block categories keep allocation lookup and metadata management simple.
The cost is fragmentation behavior that depends on the chosen category counts.
A good pool configuration should reflect the caller's expected allocation sizes.

### 4. Diagnostics Stay Close To The Allocator

Overflow checks, error messages, callbacks, and optional dump files are part of
the allocator surface. That is useful because memory bugs are often easiest to
understand at the point where allocation metadata is still available.

### 5. Tests Use The Public Contract

The portable `emb_alloc_test.c` file exercises the allocator through
`emb_alloc.h`, not through private internals. That makes it a regression test for
what users can actually rely on.

---

## Worked Example

A caller that expects many small allocations can shape the pool around that
workload:

```c
EmbAllocMemPoolSettings settings;
memset(&settings, 0, sizeof(settings));

settings.total_size = 64 * 1024;
settings.num_32_bytes_blocks = 256;
settings.num_64_bytes_blocks = 256;
settings.num_256_bytes_blocks = 64;
settings.init_allocated_memory = true;
settings.full_overflow_checks = true;

EmbAllocMempool pool = EmbAllocCreate(&settings);
void* message = EmbAllocMalloc(pool, 48);

message = EmbAllocRealloc(pool, message, 96);
EmbAllocFree(pool, message);
EmbAllocDestroy(pool);
```

The interesting behavior is in the `48` and `96` byte requests. The first fits
naturally into a 64-byte block. The second may resize in place if the allocator
can use suitable continuous blocks in one category; otherwise it allocates a new
region, copies the old contents, and frees the original allocation.

---

## Validation And Tooling

The repository includes two useful developer-facing checks:

- `emb_alloc_test.c` is a portable C self-test that compiles with the allocator
  sources and exercises the public API.
- `emb_alloc_performance_benchmark.cpp` compares allocator timing against libc
  and optional local third-party allocator sources selected by compile-time
  defines.

The self-test is the most important first-pass validation artifact because it
captures expected behavior in a form that can be run by CI or by a developer
after changing allocator internals.

Example build:

```sh
cc -std=c99 -Wall -Wextra emb_alloc.c emb_alloc_util.c emb_alloc_test.c \
    -pthread -o emb_alloc_test
./emb_alloc_test
```

---

## What It Demonstrates

- **Predictable allocation design** - capacity is configured up front and does
  not depend on later heap growth.
- **Embedded-friendly C** - the core is C99 source that can be integrated into
  existing native build systems.
- **Explicit memory layout** - settings, category metadata, block regions, and
  bitmaps have a documented place in the pool.
- **Configurable safety checks** - callers can choose zero-fill, overflow
  checks, callbacks, dump files, and thread serialization.
- **Practical validation culture** - the project includes a public-API self-test
  and benchmark harness instead of leaving behavior only in comments.

---

## First-Pass Review Notes

For a first reviewer, the most important things to evaluate are not cosmetic.
They are the allocator's contracts and target workload assumptions:

- Does the chosen block distribution match the application allocation profile?
- Are callers prepared for allocation failure when free bytes exist in the wrong
  category?
- Is `EmbAllocDestroy` called only after all users of the pool have stopped?
- Should the target enable `full_overflow_checks` in debug, production, or both?
- Does the platform provide the mutex behavior expected by `threadsafe` mode?
- Should CI include 32-bit and sanitizer builds in addition to the default build?

Those questions define whether `emb_alloc` is a good fit for a system. The code
is most compelling when the caller wants bounded memory behavior and can shape
the pool around known allocation patterns.
