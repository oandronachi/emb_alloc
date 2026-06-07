/**
 * Embedded Memory Allocator
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

#include "emb_alloc.h"

/** malloc, free declarations */
#include <stdlib.h>
/** memcpy, memset, memcmp, string-related declarations */
#include <string.h>

#include "emb_alloc_internal.h"
#include "emb_alloc_util.h"



/**
 * Mempool start and end markers. 
 * 16 bytes is at least EMB_ALLOC_ALIGN_AMOUNT, so this should be safe.
 */
static const unsigned char kEmbAllocMempoolStart [] = {     
    0xDE, 0xCE, 0xCA, 0xDE, 0xF0, 0xCA, 0xAC, 0xDC,
    0xF0, 0x0D, 0xFA, 0xCE, 0xDE, 0xAD, 0xBE, 0xEF  };
static const unsigned char kEmbAllocMempoolEnd [] = {   
    0xDE, 0xAD, 0xBE, 0xEF, 0xF0, 0x0D, 0xFA, 0xCE,
    0xAC, 0xDC, 0xDE, 0xCE, 0xCA, 0xDE, 0xF0, 0xCA  };

/**
 * Memory block start and end markers. 
 * 16 bytes is at least EMB_ALLOC_ALIGN_AMOUNT, so this should be safe.
 */
static const unsigned char kEmbAllocBlockStart [] = {    
    0xF0, 0x0D, 0xFA, 0xCE, 0xDE, 0xAD, 0xBE, 0xEF,
    0xDE, 0xCE, 0xCA, 0xDE, 0xF0, 0xCA, 0xAC, 0xDC  };
static const unsigned char kEmbAllocBlockEnd [] = {       
    0xAC, 0xDC, 0xDE, 0xCE, 0xCA, 0xDE, 0xF0, 0xCA,
    0xDE, 0xAD, 0xBE, 0xEF, 0xF0, 0x0D, 0xFA, 0xCE  };

/**
 * Clears the mempool errors.
 * @param aux_data mempool aux data for error marking and thread sync.
 */
static void ClearMempoolErrorInternal (EmbAllocMempoolAuxData* aux_data);

/**
 * Sets the mempool current error.
 * @param mempool the mempool that will hold the current error.
 * @param error the current error code.
 * @param error_message the current error message.
 * @param error_memory_location the memory location where 
*                           the error was generated from.
 */
static void EmbAllocSetErrorInternal (void* mempool, EmbAllocErrors error,
    const char* error_message, void* error_memory_location);

/**
 * Checks the mempool creation settings for consistency
 * (and updates them as best as possible).
 * @param settings the creation data that needs to be checked 
 *                          (and updated if necessary).
 * @param overflow size overflow output flag.
 */
static bool EmbAllocSanitizeSettingsInternal (EmbAllocMemPoolSettings* settings, bool *overflow);

/**
 * Returns the requied memory to be allocates.
 * @param settings the mempool settings based on which
 *                           the required padding is computed.
 * @return the required allocated size in bytes.
 */
static size_t EmbAllocGetMemoryRequirementsInternal (const EmbAllocMemPoolSettings* settings);

/**
 * Initializes the newly allocated mempool.
 * Fills the data sections with EMB_ALLOC_INIT_VALUE, 
 * sets the padding bits as needed and initializes the management data.
 * @param mempool the mempool that needs to be initialized.
 * @param allocated_size the allocated size of the mempool.
 * @param settings the settings based on which the padding bits and
 *                           the management data is initialized.
 */
static void EmbAllocInitializeInternal (void* mempool, size_t allocated_size, 
     const EmbAllocMemPoolSettings* settings);

/**
 * Initializes the blocks management data inside the mempool.
 * The block categories are initialized in an ascending block size order.
 * This kind or ordering is helpful when allocating/reallocatin memory using 
 * EmbAllocMalloc, EmbAllocRealloc, EmbAllocFree.
 * @param mempool the newly created mempool that needs to be initialized.
 */
static void EmbAllocInitializeBlockCategoriesInternal (void* mempool);

/**
 * Initializes the aux data inside the mempool.
 * @param mempool the newly created mempool that needs to be initialized.
 */
static void EmbAllocInitializeAuxDataInternal (void* mempool);

/**
 * Initializes the actual data blocks inside the mempool.
 * @param mempool the newly created mempool that needs to be initialized.
 */
static void EmbAllocInitializeDataBlocksInternal (void* mempool);

/**
 * Returns the block size and count for each block category (identified by an index).
 * The index usage should be synced with the number of "num_<size>_bytes_blocks" fields 
 * inside EmbAllocMemPoolSettings. The returned data should be presented so that
 * the increasing index returns increasing size.
 * @param settings the mempool creation settings
 * @param idx the index for which size and number of blocks is retrieved.
 * @param data_size output parameter that will hold the block data size.
 * @param num_blocks output parameter that will hold the number of blocks in the category.
 */
static void EmbAllocGetCategorySettingsInternal (const EmbAllocMemPoolSettings* settings, 
    unsigned char idx, size_t* data_size, size_t* num_blocks);

/**
 * Prints the entire mempool content.
 * @param mempool the mempool that will be printed out.
 * @param mempool_size the size of the mempool.
 * @param file the file pointer where to dump the mempool data.
 * @param mark_point_idx the index where to mark the mempool.
 *                          if set to EMB_ALLOC_VALUE_NOT_SET, then nothing will be marked.
 */
static void EmbAllocDumpMempoolInternal (void* mempool, size_t mempool_size,
    FILE* file, size_t mark_point_idx);

/**
 * Allocates a memory chunk.
 * @param settings used for full_overflow_checks and to call error_callback_fn.
 * @param categories mempool blocks management data to be checked for free space.
 * @param size the actual size of the data to be allocated.
 * @return the pointer to the beginning of newly allocated memory on success, 
 *         NULL otherwise
 */
static void* EmbAllocMallocInternal (const EmbAllocMemPoolSettings* settings, 
    EmbAllocBlockCategory* categories, size_t size);

/**
 * Merge free blocks and performs the sanity checks on them.
 * @param settings used for full_overflow_checks and to call error_callback_fn.
 * @param category mempool blocks management data to be updated.
 * @param block the actual start block that to be used for allocation.
 * @param blocks_count the number of blocks that will to be used for allocation.
 * @param keep_start keep the start padding and the counters at the start of the first block.
 * @param keep_end keep the end padding at the end of the last block.
 */
static void EmbAllocMergeFreeBlocksInternal (const EmbAllocMemPoolSettings* settings, 
    EmbAllocBlockCategory* category, void* block, size_t blocks_count,
    bool keep_start, bool keep_end);

/**
 * Allocates a memory chunk in a single memory block.
 * @param settings used for full_overflow_checks and to call error_callback_fn.
 * @param category mempool blocks management data to be updated.
 * @param size the actual size of the data to be allocated.
 * @return the pointer to the beginning of newly allocated memory on success, 
 *         NULL otherwise
 */
static void* EmbAllocMallocOneBlockInternal (const EmbAllocMemPoolSettings* settings, 
    EmbAllocBlockCategory* category, size_t size);

/**
 * Checks whether a chunck of a certain size can be allocated within 
 * multiple continous blocks in a certain category.
 * If the allocation process can be done, the valid start block and
 * the actual used blocks requirements are returned as output params.
 * @param mempool used for setting the mempool error.
 * @param category the category within which the check is made.
 * @param size the size that needs to be allocated.
 * @param block the actual start block that can be used for allocation (output param).
 * @param blocks_count the number of blocks that need to be used for allocation (output param).
 * @return true if the memory size can be allocated in multiple blocks of this category,
 *         false otherwise.
 */
static bool EmbAllocCanAllocInMultipleBlocksInternal (void* mempool, 
    EmbAllocBlockCategory* category, size_t size, void** block, 
    size_t* blocks_count);

/**
 * Allocates a memory chunk in multiple continous memory blocks of the same size.
 * @param settings used for full_overflow_checks and to call error_callback_fn.
 * @param category mempool blocks management data to be updated.
 * @param size the actual size of the data to be allocated.
 * @param block the actual start block that to be used for allocation.
 * @param blocks_count the number of blocks that will to be used for allocation.
 * @return the pointer to the beginning of newly allocated memory on success, 
 *         NULL otherwise
 */
static void* EmbAllocMallocMultiBlocksInternal (const EmbAllocMemPoolSettings* settings, 
    EmbAllocBlockCategory* category, size_t size, void* block, size_t blocks_count);

/**
 * Gets the category to which the pointer belongs to.
 * @param categories mempool blocks management data used for verification verified.
 * @param ptr the actual memory chunk address to be verified.
 * @return the actual category which contains the pointer. 
 *         NULL if the pointer is out of bounds.
 */
static EmbAllocBlockCategory* EmbAllocGetCategoryForPtr (EmbAllocBlockCategory* categories, 
    void* ptr);

/**
 * Frees a memory chunk.
 * @param settings used for full_overflow_checks and to call error_callback_fn.
 * @param categories mempool blocks management data to be updated after the chunk is freed.
 * @param ptr the actual memory chunk address to be freed.
 */
static void EmbAllocFreeInternal (const EmbAllocMemPoolSettings* settings, 
    EmbAllocBlockCategory* categories, void* ptr);

/**
 * Frees a memory chunk in a specific category.
 * @param settings used for full_overflow_checks and to call error_callback_fn.
 * @param category mempool blocks management data to be updated after the chunk is freed.
 * @param ptr the actual memory chunk address to be freed.
 */
static void EmbAllocFreeBlockInternal (const EmbAllocMemPoolSettings* settings, 
    EmbAllocBlockCategory* category, void* ptr);

/**
 * Reallocates a memory chunk.
 * @param settings used for full_overflow_checks and to call error_callback_fn.
 * @param categories mempool blocks management data to be updated after the chunk is reallocated.
 * @param ptr the actual memory chunk address to be reallocated.
 * @param size number of bytes to reallocated.
 * @return the pointer to the beginning of newly allocated memory on success, 
 *         NULL otherwise
 * 
 */
static void* EmbAllocReallocInternal (const EmbAllocMemPoolSettings* settings, 
    EmbAllocBlockCategory* categories, void* ptr, size_t size);

/**
 * Reallocates a memory chunk in a specific category.
 * @param settings used for full_overflow_checks and to call error_callback_fn.
 * @param category mempool blocks management data that contains the ptr address 
 *                 to be updated after the chunk is reallocated.
 * @param categories mempool blocks management data to be updated after the chunk is reallocated.
 * @param ptr the actual memory chunk address to be reallocated.
  * @param size number of bytes to reallocated.
 * @return the pointer to the beginning of newly allocated memory on success, 
 *         NULL otherwise
 */
static void* EmbAllocReallocBlockInternal (const EmbAllocMemPoolSettings* settings, 
    EmbAllocBlockCategory* category, EmbAllocBlockCategory* categories, 
    void* ptr, size_t size);

/**
 * @brief Computes the 0-based index of a block within its category.
 *
 * Blocks of one category are laid out contiguously with a fixed stride of
 * EMB_ALLOC_BLOCK_TOTAL_ALIGN_SIZE(block_data_size) bytes, so the index is just the
 * byte offset from the category's first block divided by that stride.
 *
 * @param category the category that owns @p block; must have a valid start_address
 *                 and a non-zero block_data_size.
 * @param block    a block-start address inside @p category. The caller guarantees it
 *                 sits on a real block boundary; this helper performs no bounds check.
 * @return the 0-based block index (byte offset / stride) within the category.
 */
static size_t EmbAllocBlockIndexInternal (const EmbAllocBlockCategory* category,
    const void* block)
{
    /** Fixed-stride layout: index == (byte offset from the first block) / stride. */
    return ((size_t) ((uintptr_t) block - (uintptr_t) category->start_address)) /
        EMB_ALLOC_BLOCK_TOTAL_ALIGN_SIZE (category->block_data_size);
}

/**
 * @brief Tests, via the authoritative out-of-band free bitmap, whether a block is free.
 *
 * The free bitmap holds one bit per block (set == occupied, clear == free) and is the
 * ONLY source of truth for free/occupied state. It exists because the inner blocks of
 * a multi-block allocation overlay user data on their in-band use_count slot, so
 * reading use_count to detect free blocks is unreliable: user data equal to
 * EMB_ALLOC_VALUE_NOT_SET (0xFF..FF) would masquerade as "free" and let a scanner hand
 * out an allocation overlapping live memory. No scanner reads use_count; they all test
 * free/occupied through this helper.
 *
 * @param category the category to consult. A NULL free_bitmap (empty category) is
 *                 treated as "not free".
 * @param block    the block-start address to test.
 * @return true iff @p block lies on @p category's grid AND its bitmap bit is clear;
 *         false if occupied, out of range, or the category is empty.
 */
static bool EmbAllocBlockIsFreeInternal (const EmbAllocBlockCategory* category,
    const void* block)
{
    const unsigned char* bitmap = (const unsigned char*) category->free_bitmap;
    size_t index;

    /**
     * Defensive bounds check: never index the bitmap with an out-of-range block
     * pointer (e.g. a drifted free hint). last_free_address is never index-fed today,
     * but bounding the access here keeps a future misuse from becoming a wild read.
     * An out-of-range block, or an empty category (NULL bitmap), reports "not free".
     */
    if ((NULL == bitmap) ||
        ((uintptr_t) block < (uintptr_t) category->start_address) ||
        ((uintptr_t) block > (uintptr_t) category->last_address)) {
        return false;
    }

    /** Bit lives at byte (index / 8), position (index % 8); a clear bit means free. */
    index = EmbAllocBlockIndexInternal (category, block);
    return (0 == (bitmap [index >> 3] & (unsigned char) (1u << (index & 7u))));
}

/**
 * @brief Marks a run of consecutive blocks occupied or free in the free bitmap.
 *
 * Sets (@p occupied == true) or clears (@p occupied == false) the bitmap bits for the
 * @p blocks_count blocks starting at @p block. Allocation marks its whole span
 * occupied; free clears the span. The bitmap is kept in lockstep with the
 * occupied_blocks counter and the head's in-band use_count, which is what lets the
 * bitmap serve as the authoritative free/occupied oracle.
 *
 * @param category     the category that owns the run. A NULL free_bitmap (empty
 *                     category) makes this a no-op.
 * @param block        the block-start address of the first block in the run.
 * @param blocks_count the number of consecutive blocks to mark.
 * @param occupied     true to set the bits (occupied), false to clear them (free).
 * @note No bounds check: the caller guarantees [block, block + blocks_count) stays
 *       within the category's grid.
 */
static void EmbAllocMarkBlocksInternal (EmbAllocBlockCategory* category,
    const void* block, size_t blocks_count, bool occupied)
{
    unsigned char* bitmap = (unsigned char*) category->free_bitmap;
    size_t index = EmbAllocBlockIndexInternal (category, block);
    size_t i = 0;

    /** Empty category (no blocks): nothing to track. */
    if (NULL == bitmap) {
        return;
    }

    /** Flip one bit per block across the run: OR-in the mask to set, AND-NOT to clear. */
    for (i = 0; i < blocks_count; i++) {
        size_t bit = index + i;
        unsigned char mask = (unsigned char) (1u << (bit & 7u));

        if (occupied) {
            bitmap [bit >> 3] = (unsigned char) (bitmap [bit >> 3] | mask);
        } else {
            bitmap [bit >> 3] = (unsigned char) (bitmap [bit >> 3] & (unsigned char) ~mask);
        }
    }
}

/**
 * @brief Tests whether a block is recorded as a live allocation head.
 *
 * The allocation-start bitmap holds one bit per block, set ONLY on the first (head)
 * block of a live allocation. It is the out-of-band oracle that defeats forged
 * inner-block frees: a multi-block allocation's inner-block header (start marker,
 * use_count, data_size, end marker) is overlaid by user data and can be forged to
 * look like a valid 1-block allocation, but the start bitmap is not addressable by
 * the caller -- only a genuine head carries the bit. EmbAllocGetCategoryForPtr
 * requires this bit before accepting a Free/Realloc pointer, which also rejects
 * double-frees (the head's bit is cleared on free) and any occupied-but-not-head block.
 *
 * @param category the category to consult. A NULL alloc_start_bitmap (empty category)
 *                 is treated as "not a start".
 * @param block    the block-start address to test.
 * @return true iff @p block lies on @p category's grid AND its allocation-start bit
 *         is set; false otherwise.
 * @note Same defensive bounds check as EmbAllocBlockIsFreeInternal: an out-of-range
 *       block reports "not a start" rather than producing a wild bitmap index.
 */
static bool EmbAllocBlockIsAllocStartInternal (const EmbAllocBlockCategory* category,
    const void* block)
{
    const unsigned char* bitmap = (const unsigned char*) category->alloc_start_bitmap;
    size_t index;

    /** Defensive bounds check (see EmbAllocBlockIsFreeInternal): an out-of-range block
     *  or empty category reports "not a start" instead of indexing out of range. */
    if ((NULL == bitmap) ||
        ((uintptr_t) block < (uintptr_t) category->start_address) ||
        ((uintptr_t) block > (uintptr_t) category->last_address)) {
        return false;
    }

    /** A set bit means this block is the head of a live allocation. */
    index = EmbAllocBlockIndexInternal (category, block);
    return (0 != (bitmap [index >> 3] & (unsigned char) (1u << (index & 7u))));
}

/**
 * @brief Sets or clears the allocation-start bit for a single head block.
 *
 * Called with @p is_start == true when a block becomes the head of a new allocation
 * (single- or multi-block), and with false when that allocation is freed. Only the
 * head ever carries the bit; inner / extension blocks never do.
 *
 * @param category the category that owns @p block. A NULL alloc_start_bitmap (empty
 *                 category) makes this a no-op.
 * @param block    the block-start address of the allocation head.
 * @param is_start true to set the bit (block is now a live head), false to clear it.
 * @note No bounds check: the allocation / free paths always pass a real, in-range head.
 */
static void EmbAllocSetAllocStartInternal (EmbAllocBlockCategory* category,
    const void* block, bool is_start)
{
    unsigned char* bitmap = (unsigned char*) category->alloc_start_bitmap;
    size_t index = EmbAllocBlockIndexInternal (category, block);
    unsigned char mask;

    /** Empty category (no blocks): nothing to track. */
    if (NULL == bitmap) {
        return;
    }

    /** Single head bit: OR-in the mask to set it, AND-NOT to clear it. */
    mask = (unsigned char) (1u << (index & 7u));
    if (is_start) {
        bitmap [index >> 3] = (unsigned char) (bitmap [index >> 3] | mask);
    } else {
        bitmap [index >> 3] = (unsigned char) (bitmap [index >> 3] & (unsigned char) ~mask);
    }
}

/**
 * @brief Finds the first genuinely free block at or after a starting block.
 *
 * Walks the category's blocks from @p from up to and including the absolute last
 * block, returning the first whose free-bitmap bit is clear. The scan is bounded by
 * last_address (the category's fixed last block), NOT by the drift-prone
 * last_free_address hint, so it stays correct even when that hint is stale.
 *
 * @param category the category to scan. A NULL start_address (empty category) yields
 *                 NULL.
 * @param from     the block-start address to start scanning from (inclusive). NULL
 *                 yields NULL.
 * @return the block-start address of the first free block at or after @p from, or NULL
 *         if no free block exists in that range.
 */
static void* EmbAllocFirstFreeFromInternal (const EmbAllocBlockCategory* category,
    void* from)
{
    unsigned char* candidate = (unsigned char*) from;

    /** Nothing to scan for an empty category or a NULL starting point. */
    if ((NULL == from) || (NULL == category->start_address)) {
        return NULL;
    }

    /** Step block-by-block up to the absolute last block; return the first free one. */
    while ((uintptr_t) candidate <= (uintptr_t) category->last_address) {
        if (EmbAllocBlockIsFreeInternal (category, candidate)) {
            return (void*) candidate;
        }

        candidate = candidate +
            EMB_ALLOC_BLOCK_TOTAL_ALIGN_SIZE (category->block_data_size);
    }

    return NULL;
}

/**
 * @brief Recomputes a category's first_free_address hint from the authoritative bitmap.
 *
 * Sets first_free_address to the lowest genuinely free block: it first scans from the
 * current hint (maintained as a lower bound on free blocks) and, if that finds nothing,
 * falls back to a full scan from the category start. Both hints are set to NULL only
 * when the category is truly full per the bitmap.
 *
 * Call this after any allocation that consumes blocks while occupied_blocks < total.
 * It replaces the earlier ad-hoc rescans that were bounded by the drift-prone
 * last_free_address hint and could either (a) leave first_free pointing at an occupied
 * block or (b) wrongly NULL both hints while blocks were still free -- an inconsistent
 * "looks full but isn't" state that then dereferenced the NULL hint.
 *
 * @param category the category whose first_free_address / last_free_address hints are
 *                 refreshed.
 */
static void EmbAllocRefreshFirstFreeInternal (EmbAllocBlockCategory* category)
{
    /** Fast path: scan upward from the current lower-bound hint. */
    void* found = EmbAllocFirstFreeFromInternal (category, category->first_free_address);

    /** Hint drifted above a free block: re-scan authoritatively from the category start. */
    if (NULL == found) {
        found = EmbAllocFirstFreeFromInternal (category, category->start_address);
    }

    category->first_free_address = found;

    /** No free block anywhere == the category is full; keep both hints NULL in step. */
    if (NULL == found) {
        category->last_free_address = NULL;
    }
}

void ClearMempoolErrorInternal (EmbAllocMempoolAuxData* aux_data)
{
    /** 
     * No need to check for the (pointer) param validity inside static functions.
     * Callers should make sure that the params are valid.
     */

    aux_data->last_error = kEmbAllocNoErr;
    memset (aux_data->last_error_message, 0, sizeof (aux_data->last_error_message));
}

void EmbAllocSetErrorInternal (void* mempool, EmbAllocErrors error,
    const char* error_message, void* error_memory_location)
{
    /** 
     * No need to check for the (pointer) param validity inside static functions.
     * Callers should make sure that the params are valid.
     */
    EmbAllocMempoolAuxData* aux_data = NULL;
    const EmbAllocMemPoolSettings* settings = NULL;
    size_t memory_offset = EMB_ALLOC_VALUE_NOT_SET;

    if (!EMB_ALLOC_PTR_IS_MEMPOOL (mempool, kEmbAllocMempoolStart)) {
        return;
    }

    aux_data = EMB_ALLOC_GET_MEMPOOL_AUX_DATA_PTR (mempool);
    settings = EMB_ALLOC_GET_MEMPOOL_SETTINGS_PTR (mempool);

    aux_data->last_error = error;
    memset (aux_data->last_error_message, 0, sizeof (aux_data->last_error_message));
    strncpy (aux_data->last_error_message, error_message, sizeof (aux_data->last_error_message));
    aux_data->last_error_message [EMB_ALLOC_ERROR_MESSAGE_SIZE - 1] = '\0';

    if (NULL != error_memory_location) {
        char memory_location [EMB_ALLOC_ERROR_MESSAGE_SIZE];
        memory_offset = (uintptr_t) error_memory_location - (uintptr_t) mempool;
        memset (memory_location, 0, sizeof (memory_location));
        snprintf (memory_location, sizeof (memory_location), 
            EMB_ALLOC_MEMORY_LOCATION_ERROR_FORMAT, error_memory_location, 
            memory_offset);
        strncat (aux_data->last_error_message, memory_location, sizeof (aux_data->last_error_message) - strlen (aux_data->last_error_message) - 1);
        aux_data->last_error_message [EMB_ALLOC_ERROR_MESSAGE_SIZE - 1] = '\0';
    }

    if (NULL != settings->error_callback_fn) {
        /** Synchronous, possibly with the pool mutex held: the callback must not
         * re-enter any EmbAlloc function on this pool (see EmbAllocErrorCallback). */
        settings->error_callback_fn (aux_data->last_error, aux_data->last_error_message);
    }

    if (strlen (settings->error_dump_file_name)) {
        FILE* error_file = fopen (settings->error_dump_file_name, "a");

        if (NULL != error_file) {
            fputs ("\n", error_file);
            fputs (aux_data->last_error_message, error_file);
            fputs ("\n", error_file);
            EmbAllocDumpMempoolInternal (mempool,  EmbAllocGetMemoryRequirementsInternal (settings), 
                    error_file, memory_offset);
            fflush (error_file);
            fclose (error_file);
        } else {
            perror ("Error writing the error message in the mempool error dump file");
        }
    }  
}

#define SIZE_T_SUM_OVERFLOW(lhs, rhs) ((lhs) > (SIZE_MAX - (rhs)))
#define SIZE_T_MUL_OVERFLOW(lhs, rhs) ((lhs) > (SIZE_MAX / (rhs)))

bool EmbAllocSanitizeSettingsInternal (EmbAllocMemPoolSettings* settings, bool *overflow)
{
    /** 
     * No need to check for the (pointer) param validity inside static functions.
     * Callers should make sure that the params are valid.
     */
    bool error = false;
    size_t initial_total_size = settings->total_size;
    settings->total_size = 0;

    /**
     * Recompute the usable pool size from the per-category block counts, one category
     * at a time, accumulating count * block_size into total_size. Every step is guarded
     * twice: SIZE_T_MUL_OVERFLOW rejects a count * size product that would wrap, and
     * SIZE_T_SUM_OVERFLOW rejects a running-total addition that would wrap. The first
     * overflow latches `error` and, via the && short-circuits, skips the remaining
     * categories -- so an attacker-sized settings struct can never yield a too-small
     * total_size (creation later fails closed on the *overflow flag).
     */
    if (!SIZE_T_MUL_OVERFLOW(settings->num_32_bytes_blocks, 32) &&
        !SIZE_T_SUM_OVERFLOW(settings->total_size, settings->num_32_bytes_blocks * 32)) {
        settings->total_size += (settings->num_32_bytes_blocks * 32);
    } else {
        error = true;
    }
    if (!error && !SIZE_T_MUL_OVERFLOW(settings->num_64_bytes_blocks, 64) &&
        !SIZE_T_SUM_OVERFLOW(settings->total_size, settings->num_64_bytes_blocks * 64)) {
        settings->total_size += (settings->num_64_bytes_blocks * 64);
    } else {
        error = true;
    }
    if (!error && !SIZE_T_MUL_OVERFLOW(settings->num_128_bytes_blocks, 128) &&
        !SIZE_T_SUM_OVERFLOW(settings->total_size, settings->num_128_bytes_blocks * 128)) {
        settings->total_size += (settings->num_128_bytes_blocks * 128);
    } else {
        error = true;
    }
     if (!error && !SIZE_T_MUL_OVERFLOW(settings->num_256_bytes_blocks, 256) &&
        !SIZE_T_SUM_OVERFLOW(settings->total_size, settings->num_256_bytes_blocks * 256)) {
        settings->total_size += (settings->num_256_bytes_blocks * 256);
    } else {
        error = true;
    }
    if (!error && !SIZE_T_MUL_OVERFLOW(settings->num_512_bytes_blocks, 512) &&
        !SIZE_T_SUM_OVERFLOW(settings->total_size, settings->num_512_bytes_blocks * 512)) {
        settings->total_size += (settings->num_512_bytes_blocks * 512);
    } else {
        error = true;
    }
    if (!error && !SIZE_T_MUL_OVERFLOW(settings->num_1k_bytes_blocks, 1024) &&
        !SIZE_T_SUM_OVERFLOW(settings->total_size, settings->num_1k_bytes_blocks * 1024)) {
        settings->total_size += (settings->num_1k_bytes_blocks * 1024);
    } else {
        error = true;
    }
    if (!error && !SIZE_T_MUL_OVERFLOW(settings->num_2k_bytes_blocks, 2048) &&
        !SIZE_T_SUM_OVERFLOW(settings->total_size, settings->num_2k_bytes_blocks * 2048)) {
        settings->total_size += (settings->num_2k_bytes_blocks * 2048);
    } else {
        error = true;
    }
    if (!error && !SIZE_T_MUL_OVERFLOW(settings->num_4k_bytes_blocks, 4096) &&
        !SIZE_T_SUM_OVERFLOW(settings->total_size, settings->num_4k_bytes_blocks * 4096)) {
        settings->total_size += (settings->num_4k_bytes_blocks * 4096);
    } else {
        error = true;
    }

    /**
     * remove declared inside stdio.h
     * Delete the error dump file when creating the mempool.
     */
    settings->error_dump_file_name [EMB_ALLOC_ERROR_DUMP_FILE_NAME_SIZE - 1] = '\0';

    if (strlen (settings->error_dump_file_name)) {
        /** It does not really matter if this fails or not, 
         * so we do not handle the return value. 
         */
        if (remove (settings->error_dump_file_name)) {
            perror ("Error removing the mempool error dump file");
        }
    }

    /** 
     * For the moment just align the total size with the one deducted 
     * from the blockes counters. The total size is adjusted.
     * If this logic needs to change in the future, 
     * then this function needs to be adjusted.
     */
    *overflow = error;
    return (!error && (settings->total_size == initial_total_size));
}

size_t EmbAllocGetMemoryRequirementsInternal (const EmbAllocMemPoolSettings* settings)
{
    /** 
     * No need to check for the (pointer) param validity inside static functions.
     * Callers should make sure that the params are valid.
     */
    size_t total_size = EMB_ALLOC_MEMPOOL_CONTROL_ALIGN_SIZE;
    const size_t block_counts [EMB_ALLOC_NUM_BLOCK_CATEGORIES] = {
        settings->num_32_bytes_blocks,  settings->num_64_bytes_blocks,
        settings->num_128_bytes_blocks, settings->num_256_bytes_blocks,
        settings->num_512_bytes_blocks, settings->num_1k_bytes_blocks,
        settings->num_2k_bytes_blocks,  settings->num_4k_bytes_blocks };
    size_t total_blocks = 0;
    size_t control_size = 0;
    size_t bitmap_size = 0;
    unsigned char i = 0;

    for (i = 0; i < EMB_ALLOC_NUM_BLOCK_CATEGORIES; i++) {        /** checked count sum */
        if (SIZE_T_SUM_OVERFLOW (total_blocks, block_counts [i])) { return 0; }
        total_blocks += block_counts [i];
        /** Per-category, byte-aligned free bitmap (ceil(n/8) bytes). ceil(n/8) <= n,
         * so this running sum stays <= total_blocks (sum-overflow-checked just above).
         * The (n + 7) inside EMB_ALLOC_CATEGORY_BITMAP_BYTES could only wrap for n
         * near SIZE_MAX, which EmbAllocSanitizeSettingsInternal (run earlier in
         * EmbAllocCreate) already rejects via its count*block_size overflow check --
         * so it is unreachable here. */
        bitmap_size += EMB_ALLOC_CATEGORY_BITMAP_BYTES (block_counts [i]);
    }
    if (SIZE_T_MUL_OVERFLOW (total_blocks, EMB_ALLOC_BLOCK_CONTROL_ALIGN_SIZE)) { return 0; }
    control_size = total_blocks * EMB_ALLOC_BLOCK_CONTROL_ALIGN_SIZE;
    if (SIZE_T_SUM_OVERFLOW (total_size, control_size)) { return 0; }
    total_size += control_size;
    if (SIZE_T_SUM_OVERFLOW (total_size, settings->total_size)) { return 0; }
    total_size += settings->total_size;
    /** Reserve the aligned bitmap region. It holds TWO per-block bitmaps -- the free
     * bitmap and the allocation-start bitmap -- each Sum(ceil(n/8)) bytes. It sits
     * after the data blocks and before the mempool end marker, so block offsets and
     * the marker are unchanged. 2*bitmap_size cannot overflow: bitmap_size <=
     * total_blocks <= SIZE_MAX/EMB_ALLOC_BLOCK_CONTROL_ALIGN_SIZE (guaranteed by the
     * control-size multiply check above). */
    bitmap_size = EMB_ALLOC_ALIGN_SIZE (2 * bitmap_size);
    if (SIZE_T_SUM_OVERFLOW (total_size, bitmap_size)) { return 0; }
    total_size += bitmap_size;

    return total_size;
}

void EmbAllocInitializeInternal ( void* mempool, size_t allocated_size, 
    const EmbAllocMemPoolSettings* settings)
{
    /** 
     * No need to check for the (pointer) param validity inside static functions.
     * Callers should make sure that the params are valid.
     */

    /** Init all mempool with EMB_ALLOC_INIT_VALUE. */
    memset (mempool, EMB_ALLOC_INIT_VALUE, allocated_size);

    /** 
     * Add the mempool start padding marker.
     * kEmbAllocMempoolStart is definitely smaller or equal than EMB_ALLOC_ALIGN_AMOUNT.
     */
    memcpy (mempool, kEmbAllocMempoolStart, EMB_ALLOC_ALIGN_AMOUNT);
    /** 
     * Add the mempool end padding marker.
     * kEmbAllocMempoolEnd is definitely smaller or equal than EMB_ALLOC_ALIGN_AMOUNT.
     */
    memcpy ((void*) ((unsigned char*) mempool + allocated_size - EMB_ALLOC_ALIGN_AMOUNT), 
        kEmbAllocMempoolEnd, EMB_ALLOC_ALIGN_AMOUNT);

    /**
     * Bit copy is safe due to the datatypes of the EmbAllocMemPoolSettings structure.
     * If the structure will change, then re-check this statement.
     * No need to check the mempool start padding here.
     */
    *(EMB_ALLOC_GET_MEMPOOL_SETTINGS_PTR (mempool)) = *settings;

    EmbAllocInitializeBlockCategoriesInternal (mempool);
    EmbAllocInitializeAuxDataInternal  (mempool);
    EmbAllocInitializeDataBlocksInternal (mempool);
}

void EmbAllocInitializeBlockCategoriesInternal (void* mempool)
{
    /** 
     * No need to check for the (pointer) param validity inside static functions.
     * Callers should make sure that the params are valid.
     */

    /** Make sure this fits into EMB_ALLOC_NUM_BLOCK_CATEGORIES. */
    unsigned char i = 0;
    EmbAllocBlockCategory* block_category = EMB_ALLOC_GET_MEMPOOL_BLOCK_CATEGORIES_PTR (mempool);
    /** Use this to calculate the start address of the first block of its kind. */
    unsigned char* current_start_address = (unsigned char*) EMB_ALLOC_GET_MEMPOOL_FIRST_BLOCK_PTR (mempool);

    for (i = 0; i < EMB_ALLOC_NUM_BLOCK_CATEGORIES; i++) {
        /** Init the block category data related to the creation settings. */
        EmbAllocGetCategorySettingsInternal (
            (const EmbAllocMemPoolSettings*) EMB_ALLOC_GET_MEMPOOL_SETTINGS_PTR (mempool), 
            i, 
            &(block_category [i].block_data_size), 
            &(block_category [i].total_blocks));

        block_category [i].occupied_blocks = 0;

        /** Init everything else that requires the above initialization as a start point. */
        if (block_category [i].total_blocks) {
            block_category [i].start_address = (void*) current_start_address;
            block_category [i].first_free_address = block_category [i].start_address;
            block_category [i].last_address = (void*) 
                (current_start_address + 
                    (   (block_category [i].total_blocks - 1) * 
                        EMB_ALLOC_BLOCK_TOTAL_ALIGN_SIZE (block_category [i].block_data_size)));
            block_category [i].last_free_address = block_category [i].last_address;
        } else {
            block_category [i].start_address = NULL;
            block_category [i].first_free_address = NULL;
            block_category [i].last_free_address = NULL;
            block_category [i].last_address = NULL;
        }

        /** Update the carry-over start address. */
        current_start_address +=
            (block_category [i].total_blocks *
            EMB_ALLOC_BLOCK_TOTAL_ALIGN_SIZE (block_category [i].block_data_size));
    }

    /**
     * Wire each category's free bitmap into the region that follows the data
     * blocks (current_start_address now points just past the last block) and
     * clear it so every block starts free. The whole mempool was memset to
     * EMB_ALLOC_INIT_VALUE earlier, so the bitmap MUST be explicitly zeroed --
     * a stray set bit would read as "occupied" and that block would never be
     * handed out. Slices are laid out in the same per-category, byte-aligned
     * order used to size the region in EmbAllocGetMemoryRequirementsInternal.
     */
    {
        unsigned char* bitmap_cursor = current_start_address;

        /** Free bitmap slices. */
        for (i = 0; i < EMB_ALLOC_NUM_BLOCK_CATEGORIES; i++) {
            if (block_category [i].total_blocks) {
                block_category [i].free_bitmap = (void*) bitmap_cursor;
                bitmap_cursor +=
                    EMB_ALLOC_CATEGORY_BITMAP_BYTES (block_category [i].total_blocks);
            } else {
                block_category [i].free_bitmap = NULL;
            }
        }

        /** Allocation-start bitmap slices (same per-category, byte-aligned layout,
         * placed immediately after all the free-bitmap slices). */
        for (i = 0; i < EMB_ALLOC_NUM_BLOCK_CATEGORIES; i++) {
            if (block_category [i].total_blocks) {
                block_category [i].alloc_start_bitmap = (void*) bitmap_cursor;
                bitmap_cursor +=
                    EMB_ALLOC_CATEGORY_BITMAP_BYTES (block_category [i].total_blocks);
            } else {
                block_category [i].alloc_start_bitmap = NULL;
            }
        }

        /** Zero both bitmaps: every block starts free and is not an allocation head. */
        memset (current_start_address, 0,
            (size_t) (bitmap_cursor - current_start_address));
    }
}

void EmbAllocInitializeAuxDataInternal (void* mempool)
{
    /** 
     * No need to check for the (pointer) param validity inside static functions.
     * Callers should make sure that the params are valid.
     */

    EmbAllocMempoolAuxData* aux_data = EMB_ALLOC_GET_MEMPOOL_AUX_DATA_PTR (mempool);
    const EmbAllocMemPoolSettings* settings = EMB_ALLOC_GET_MEMPOOL_SETTINGS_PTR (mempool);

    aux_data->thread_sync_mutex_initialized = false;

    /** Mark the mutex as being initialized only if the mempool is threadsafe 
     * and the initialization completed successfully. 
     */
    if (settings->threadsafe) {
        aux_data->thread_sync_mutex_initialized = 
            (0 == EmbAllocInitMutex (&(aux_data->thread_sync_mutex)));
    }

    /** No errors */
    ClearMempoolErrorInternal (aux_data);
}

void EmbAllocInitializeDataBlocksInternal (void* mempool)
{
    /** 
     * No need to check for the (pointer) param validity inside static functions.
     * Callers should make sure that the params are valid.
     */

    /** Make sure this fits into EMB_ALLOC_NUM_BLOCK_CATEGORIES. */
    unsigned char i = 0;
    EmbAllocBlockCategory* block_category = EMB_ALLOC_GET_MEMPOOL_BLOCK_CATEGORIES_PTR (mempool);

    /**
     * Stamp every data block in every category into the "free / unallocated" state:
     * write its start and end padding markers and set both the use_count and the
     * data_size slots to EMB_ALLOC_VALUE_NOT_SET. The out-of-band free and start
     * bitmaps are zeroed separately (in EmbAllocInitializeBlockCategoriesInternal), so
     * together every block starts out free and not an allocation head.
     */
    for (i = 0; i < EMB_ALLOC_NUM_BLOCK_CATEGORIES; i++) {
        size_t j = 0;

        for (j = 0; j < block_category [i].total_blocks; j++) {
            unsigned char* current_block_address = (unsigned char*) block_category [i].start_address + 
                (j * EMB_ALLOC_BLOCK_TOTAL_ALIGN_SIZE (block_category [i].block_data_size));
            size_t* used_block_count = EMB_ALLOC_GET_BLOCK_USE_COUNT_FROM_BLOCK (current_block_address);
            size_t* data_size = EMB_ALLOC_GET_MEMORY_USE_COUNT_FROM_BLOCK (current_block_address);

            /** 
             * Add the block start padding marker.
             * kEmbAllocBlockStart is definitely smaller or equal than EMB_ALLOC_ALIGN_AMOUNT.
             */
            memcpy ((void*) current_block_address, kEmbAllocBlockStart, 
                EMB_ALLOC_ALIGN_AMOUNT);
            /** 
             * Add the block end padding marker.
             * kEmbAllocBlockEnd is definitely smaller or equal than EMB_ALLOC_ALIGN_AMOUNT.
             */
            memcpy (EMB_ALLOC_GET_END_PADDING_FROM_BLOCK (current_block_address, 
                        block_category [i].block_data_size), 
                kEmbAllocBlockEnd, EMB_ALLOC_ALIGN_AMOUNT);


            *used_block_count = EMB_ALLOC_VALUE_NOT_SET;
            *data_size = EMB_ALLOC_VALUE_NOT_SET;
        }
    }
}

void EmbAllocGetCategorySettingsInternal (const EmbAllocMemPoolSettings* settings, 
    unsigned char idx, size_t* data_size, size_t* num_blocks)
{
    /** 
     * No need to check for the (pointer) param validity inside static functions.
     * Callers should make sure that the params are valid.
     */

    /**
     * Keep the idx usage in sync with the "num_<size>_bytes_blocks" fields 
     * inside EmbAllocMemPoolSettings. Using defines for the 32, 64, 128, ... values 
     * instead of "magic numbers"seemed as an overkill.
     */
    switch (idx) {
        case 0: {
            *data_size = 32;
            *num_blocks = settings->num_32_bytes_blocks;
        }
        break;
        case 1: {
            *data_size = 64;
            *num_blocks = settings->num_64_bytes_blocks;
        }
        break;
        case 2: {
            *data_size = 128;
            *num_blocks = settings->num_128_bytes_blocks;
        }
        break;
        case 3: {
            *data_size = 256;
            *num_blocks = settings->num_256_bytes_blocks;
        }
        break;
        case 4: {
            *data_size = 512;
            *num_blocks = settings->num_512_bytes_blocks;
        }
        break;
        case 5: {
            *data_size = 1024;
            *num_blocks = settings->num_1k_bytes_blocks;
        }
        break;
        case 6: {
            *data_size = 2048;
            *num_blocks = settings->num_2k_bytes_blocks;
        }
        break;
        case 7: {
            *data_size = 4096;
            *num_blocks = settings->num_4k_bytes_blocks;
        }
        break;
        /** This should never be reached. */
        default: {
            *data_size = 0;
            *num_blocks = 0;
        }
    }
}

void EmbAllocDumpMempoolInternal (void* mempool, size_t mempool_size,
    FILE* file, size_t mark_point_idx)
{
    /** 
     * No need to check for the (pointer) param validity inside static functions.
     * Callers should make sure that the params are valid.
     */

    size_t i = 0;
    FILE* appendable_file = file;
    unsigned char* printable_mempool = (unsigned char*) mempool;
    const EmbAllocMemPoolSettings* settings = EMB_ALLOC_GET_MEMPOOL_SETTINGS_PTR (mempool);

    if ((NULL == file) &&
        (0 == strlen (settings->error_dump_file_name))) {
        return;
    }

    if (NULL == file) {
        appendable_file = fopen (settings->error_dump_file_name, "a");

        if (NULL == appendable_file) {
            perror ("Error dumping the mempool in the mempool error dump file");
            return;
        }
    }

    fprintf (appendable_file, "Mempool dump at location 0x%p (%zu lines)",
        mempool, (mempool_size / EMB_ALLOC_ALIGN_AMOUNT));

    for (i = 0; i < mempool_size; i++) {
        if (0 == i % EMB_ALLOC_ALIGN_AMOUNT) {
            fprintf (appendable_file, "\n%zu: ", (i / EMB_ALLOC_ALIGN_AMOUNT));
        }

        fprintf (appendable_file, " %s%02x", 
            ((  (EMB_ALLOC_VALUE_NOT_SET != mark_point_idx) &&
                (mark_point_idx == i))? "(!!!MARK POINT!!!)": "" ), 
            printable_mempool [i]);
    }

    fprintf (appendable_file, "\n");

    if (NULL == file) {
        fflush (appendable_file);
        fclose (appendable_file);
    }
}

EmbAllocMempool EmbAllocCreate (const EmbAllocMemPoolSettings* settings)
{
    if (NULL == settings) {
        return NULL;
    } else {
        /**
         * Bit copy is safe due to the datatypes of the EmbAllocMemPoolSettings structure.
         * If the structure will change, then re-check this statement.
         */
        EmbAllocMemPoolSettings sanitized_settings = *settings;
        size_t allocated_size = 0;
        EmbAllocMempool return_value = NULL;
        bool overflow = false;
        bool consistent_settings = EmbAllocSanitizeSettingsInternal (&sanitized_settings, &overflow);

        if (overflow) {
            /** EmbAllocSetErrorInternal(...) cannot be called because the mempool is not created. */
            return NULL;
        }

        allocated_size = EmbAllocGetMemoryRequirementsInternal (&sanitized_settings);

        if (0 == allocated_size) {
            return NULL;
        }

        return_value = (void*) malloc (allocated_size);
        
        if (NULL != return_value) {
            EmbAllocInitializeInternal (return_value, allocated_size, &sanitized_settings);

            if (!consistent_settings) {
                EmbAllocSetErrorInternal (return_value, kEmbAllocInconsistentSettings,
                    EMB_ALLOC_INCONSISTENT_SETTINGS, NULL);
            }

            /** This has a higher priority. Due to the current "one error per mempool" current
             * architecture, this can override an kEmbAllocInconsistentSettings error.
             * Different future approaces will need to fix this limitation.
             */
            if (sanitized_settings.threadsafe &&
                !EMB_ALLOC_GET_MEMPOOL_AUX_DATA_PTR (return_value)->thread_sync_mutex_initialized) {
                EmbAllocSetErrorInternal (return_value, kEmbAllocThreadSyncError,
                    EMB_ALLOC_MUTEX_CREATE_ERROR, NULL);
            }

#ifdef VERBOSE_DUMP_MEMPOOL
            if (strlen (sanitized_settings.error_dump_file_name)) {
                FILE* error_file = fopen (sanitized_settings.error_dump_file_name, "a");

                if (NULL != error_file) {
                    fputs ("\nMempool created\n", error_file);

                    EmbAllocDumpMempoolInternal (return_value, allocated_size, 
                        error_file, EMB_ALLOC_VALUE_NOT_SET);

                    fflush (error_file);
                    fclose (error_file);
                } else {
                    perror ("Error writing the error message in the mempool error dump file");
                } 
            }
#endif /** VERBOSE_DUMP_MEMPOOL */
        } else if (NULL != sanitized_settings.error_callback_fn) {
            sanitized_settings.error_callback_fn (kEmbAllocNoMemory, 
                EMB_ALLOC_CANNOT_CREATE_MEMPOOL_ERROR);
        }

        return return_value;
    }
}

bool EmbAllocDestroy (EmbAllocMempool mempool) 
{
    if (EMB_ALLOC_PTR_IS_MEMPOOL (mempool, kEmbAllocMempoolStart)) {
        EmbAllocMempoolAuxData* aux_data = EMB_ALLOC_GET_MEMPOOL_AUX_DATA_PTR (mempool);
        const EmbAllocMemPoolSettings* settings = EMB_ALLOC_GET_MEMPOOL_SETTINGS_PTR (mempool);
        EmbAllocErrorCallback error_callback_fn = settings->error_callback_fn;
        bool lock_acquired = true;

        if (aux_data->thread_sync_mutex_initialized) {
            lock_acquired = !EmbAllocLockMutex ( &(aux_data->thread_sync_mutex));

            if (!lock_acquired) {
                /** Lock failed: report (if a callback is set) and fail immediately, without
                * reading the shared error slot unsynchronized or unlocking a mutex we
                * never acquired. */
                if (NULL != error_callback_fn) {
                    error_callback_fn (kEmbAllocThreadSyncError, EMB_ALLOC_MUTEX_LOCK_ERROR);
                }
                return false;
            }
        }

        if (!EMB_ALLOC_PTR_IS_MEMPOOL (mempool, kEmbAllocMempoolStart)) {
            /** This is a rather rare race condition.
             * 2 threads destroy the same mempool. The 1st one manages to memset the mempool +
             * unlock the mutex (it is yet unclear if it managed to destroy the mutex).
             * since locking a destroyed mutex or destroying a locked mutex is undefined behavior,
             * the simplest thing to to is return and hope that the EMB_ALLOC_PTR_IS_MEMPOOL check
             * did not cause a memory access violation.
             */
            if (aux_data->thread_sync_mutex_initialized) {
                EmbAllocUnlockMutex ( &(aux_data->thread_sync_mutex));
            }
            return false;
        }

        memset (mempool, 0, EMB_ALLOC_MEMPOOL_NO_THREADSAFE_CONTROL_ALIGN_SIZE);

        if (aux_data->thread_sync_mutex_initialized &&
            EmbAllocUnlockMutex ( &(aux_data->thread_sync_mutex)) &&
            (NULL != error_callback_fn)) {
            /** 
             * There will be no more mempool aux data, 
             * so just call the error callback.
             */
            error_callback_fn (kEmbAllocThreadSyncError, EMB_ALLOC_MUTEX_UNLOCK_ERROR);
        }

        if (aux_data->thread_sync_mutex_initialized &&
            EmbAllocDestroyMutex ( &(aux_data->thread_sync_mutex)) &&
            (NULL != error_callback_fn)) {
            /** 
             * There will be no more mempool aux data, 
             * so just call the error callback.
             */
            error_callback_fn (kEmbAllocThreadSyncError, EMB_ALLOC_MUTEX_DESTROY_ERROR);
        }

        free (mempool);
        return true;
    } else {
        /** This is not a mempool, so we cannot send back a more detailed error message. */
        return false;
    }
}

void* EmbAllocMallocInternal (const EmbAllocMemPoolSettings* settings, 
    EmbAllocBlockCategory* categories, size_t size)
{
    /** 
     * No need to check for the (pointer) param validity inside static functions.
     * Callers should make sure that the params are valid.
     */

    void* multi_block_alloc_address = NULL;
    size_t multi_block_alloc_count = 0;
    /** Make sure this fits into EMB_ALLOC_NUM_BLOCK_CATEGORIES. */
    unsigned char i = 0;
    /** 
     * The smallest index of the larger than needed block category that 
     * can be used for allocation.
     */
    unsigned char large_size_block_idx = EMB_ALLOC_NUM_BLOCK_CATEGORIES;
    /** 
     * The largest index of the smaller than needed block category that 
     * can be used for allocation.
     */
    unsigned char small_size_block_idx = EMB_ALLOC_NUM_BLOCK_CATEGORIES;

    /**
     * Allocation strategy, in order of preference:
     *   1. If the smallest category (index 0) can hold `size` in a single block, use
     *      it -- that is the least-waste fit.
     *   2. Otherwise scan the categories from largest to smallest, remembering the best
     *      single-block fit (large_size_block_idx) and the largest category that can
     *      satisfy `size` across a multi-block run (small_size_block_idx).
     *   3. If both candidates exist, pick whichever leaves its category with more free
     *      memory afterwards (the heuristic below); if only one exists, use it.
     *   4. If nothing fits, report kEmbAllocNoMemory.
     */
    if (EMB_ALLOC_CAN_ALLOC_IN_A_BLOCK (categories [0], size)) {
        return EmbAllocMallocOneBlockInternal (settings, categories, size);
    }

    for (i = EMB_ALLOC_NUM_BLOCK_CATEGORIES - 1; i > 0; i--) {
        if (categories [i].occupied_blocks < categories [i].total_blocks) {
            if (EMB_ALLOC_CAN_ALLOC_IN_A_BLOCK (categories [i], size)) {
                if (categories [i - 1].block_data_size < size) {
                    /** 
                     * Alloc in this category only if this block size is the best fit
                     * (it's the smallest block that fits).
                     */
                    return EmbAllocMallocOneBlockInternal (settings, categories + i, size);
                } else {
                    large_size_block_idx = i;
                }
            } else if (EmbAllocCanAllocInMultipleBlocksInternal (
                EMB_ALLOC_GET_MEMPOOL_FROM_SETTINGS_PTR (settings),
                categories + i, size, &multi_block_alloc_address, 
                &multi_block_alloc_count)) {
                small_size_block_idx = i;
                break;
            }
        }
    }

    if ((EMB_ALLOC_NUM_BLOCK_CATEGORIES == small_size_block_idx) &&
        (categories [0].occupied_blocks < categories [0].total_blocks) &&
        EmbAllocCanAllocInMultipleBlocksInternal (
            EMB_ALLOC_GET_MEMPOOL_FROM_SETTINGS_PTR (settings),
            categories, size, &multi_block_alloc_address, 
            &multi_block_alloc_count)) {
        small_size_block_idx = i;
    }

    /**
     * If there is not a perfect fit block available, minimise the waste by 
     * choosing the closest block category with the largest percentage of free blocks.
     */
    if ((EMB_ALLOC_NUM_BLOCK_CATEGORIES != large_size_block_idx) && 
        (EMB_ALLOC_NUM_BLOCK_CATEGORIES != small_size_block_idx)) {
            /**
             * Choose the category to allocate depending on how much memory
             * will be free after a potential allocation.
             * This method maximizes the free space left in the caategory.
             * If the category [large_size_block_idx] remains with more free memory, 
             * then the allocation will be done in that category, 
             * otherwise it will be allocated in the category [small_size_block_idx].
             * 
             * This logic can also be changed so that the wasted memory is minimized 
             * (best fit case).
             * if ( (categories [large_size_block_idx].block_data_size - size) <
             *      (   (   categories [small_size_block_idx].block_data_size * 
             *              multi_block_alloc_count) + 
             *          (   (multi_block_alloc_count - 1) * 
             *              EMB_ALLOC_BLOCK_CONTROL_ALIGN_SIZE) - 
             *          size))
             * 
             * Another option could be to allocate where the percent of occupied blocks 
             * before allocation is smaller.
             * if ( (   (double) (categories [large_size_block_idx].occupied_blocks) / 
             *          (double) (categories [large_size_block_idx].total_blocks)) < 
             *      (   (double) (categories [small_size_block_idx].occupied_blocks) / 
             *          (double) (categories [small_size_block_idx].total_blocks)))
             * 
             * Or the percent of occupied blocks after the allocation is smaller...
             * if ( (   (double) (categories [large_size_block_idx].occupied_blocks + 1) / 
             *          (double) (categories [large_size_block_idx].total_blocks)) < 
             *      (   (double) (  categories [small_size_block_idx].occupied_blocks + 
             *                      multi_block_alloc_count) / 
             *          (double) (categories [small_size_block_idx].total_blocks)))
             * 
             * The user can decide the allocation logic later on.
             * TODO: Maybe add an enum param in the mempool settings 
             * to make the allocation logic configurable.
             */
            if (    (   categories [large_size_block_idx].block_data_size * 
                        (   categories [large_size_block_idx].total_blocks - 
                            categories [large_size_block_idx].occupied_blocks - 
                            1)) > 
                    (   categories [small_size_block_idx].block_data_size * 
                        (   categories [small_size_block_idx].total_blocks - 
                            categories [small_size_block_idx].occupied_blocks - 
                            multi_block_alloc_count))) {
                return EmbAllocMallocOneBlockInternal (settings, 
                    categories + large_size_block_idx, size);
            } else {
                return EmbAllocMallocMultiBlocksInternal(settings, 
                    categories + small_size_block_idx, size,
                    multi_block_alloc_address, multi_block_alloc_count);
            }
    } else if (EMB_ALLOC_NUM_BLOCK_CATEGORIES != large_size_block_idx) {
        return EmbAllocMallocOneBlockInternal (settings, 
            categories + large_size_block_idx, size);
    } else if (EMB_ALLOC_NUM_BLOCK_CATEGORIES != small_size_block_idx) {
        return EmbAllocMallocMultiBlocksInternal(settings, 
            categories + small_size_block_idx, size,
            multi_block_alloc_address, multi_block_alloc_count);
    }

    EmbAllocSetErrorInternal (EMB_ALLOC_GET_MEMPOOL_FROM_SETTINGS_PTR (settings), kEmbAllocNoMemory,
        EMB_ALLOC_NOT_ENOUGH_MEMORY_ERROR, NULL);
    return NULL;
}

void EmbAllocMergeFreeBlocksInternal (const EmbAllocMemPoolSettings* settings, 
    EmbAllocBlockCategory* category, void* block, size_t blocks_count,
    bool keep_start, bool keep_end)
{
    size_t i = 0;
    void* mempool = EMB_ALLOC_GET_MEMPOOL_FROM_SETTINGS_PTR (settings);

    /**
     * Claims the run [block, block + blocks_count) for a fresh allocation. For each
     * block it first runs the corruption / overflow checks below (a block being claimed
     * must still be intact and free), then reformats the run so the blocks_count blocks
     * read as ONE contiguous payload: the head keeps its start control (start marker +
     * use_count + data_size) only when keep_start is set, the tail keeps its end marker
     * only when keep_end is set, and every other control region is overwritten with
     * EMB_ALLOC_INIT_VALUE so it becomes usable payload.
     */
    for (i = 0; i < blocks_count; i++) {
        void* current_block = (void*) ((unsigned char*) block + 
            (i * EMB_ALLOC_BLOCK_TOTAL_ALIGN_SIZE (category->block_data_size)));
        size_t* used_block_count = EMB_ALLOC_GET_BLOCK_USE_COUNT_FROM_BLOCK (current_block);
        size_t* data_size = EMB_ALLOC_GET_MEMORY_USE_COUNT_FROM_BLOCK (current_block);
        void* block_end_padding = EMB_ALLOC_GET_END_PADDING_FROM_BLOCK (current_block, 
            category->block_data_size);
        void* data_pointer = EMB_ALLOC_GET_PTR_FROM_BLOCK (current_block);
        
        /**
         * Corruption / overflow detection on the block about to be claimed. A truly
         * free block still carries intact start and end markers and has its use_count /
         * data_size slots at the EMB_ALLOC_VALUE_NOT_SET sentinel; any mismatch means a
         * neighbour overflowed into it. Each check reports the exact offending offset.
         * Errors here are advisory -- the allocation still proceeds.
         */
        if (memcmp (current_block, kEmbAllocBlockStart, EMB_ALLOC_ALIGN_AMOUNT)) {
            /** Start marker clobbered (typically an underflow from the previous block). */
            EmbAllocSetErrorInternal (mempool, kEmbAllocOverflow,
                EMB_ALLOC_OVERFLOW_ERROR, current_block);
        }

        if (memcmp (block_end_padding, kEmbAllocBlockEnd , EMB_ALLOC_ALIGN_AMOUNT)) {
            /** End marker clobbered (an overflow past this block's payload). */
            EmbAllocSetErrorInternal (mempool, kEmbAllocOverflow,
                EMB_ALLOC_OVERFLOW_ERROR, block_end_padding);
        }

        if (EMB_ALLOC_VALUE_NOT_SET != *used_block_count) {
            /** use_count slot not at the free sentinel: the block was not really free. */
            EmbAllocSetErrorInternal (mempool, kEmbAllocOverflow,
                EMB_ALLOC_OVERFLOW_ERROR, (void*) used_block_count);
        }

        if (EMB_ALLOC_VALUE_NOT_SET != *data_size) {
            /** data_size slot not at the free sentinel: likewise a corruption signal. */
            EmbAllocSetErrorInternal (mempool, kEmbAllocOverflow,
                EMB_ALLOC_OVERFLOW_ERROR, (void*) data_size);
        }

        /**
         * Full-overflow mode also verifies the whole payload is still the INIT fill
         * (a free block is left filled with EMB_ALLOC_INIT_VALUE), catching writes that
         * landed inside the block without disturbing the markers; on a mismatch it
         * restores the fill so stale data cannot leak into the caller.
         */
        if (settings->full_overflow_checks &&
            !EmbAllocCheckBuffer (
                data_pointer,
                category->block_data_size, 
                EMB_ALLOC_INIT_VALUE)) {
            EmbAllocSetErrorInternal (mempool, kEmbAllocOverflow,
                EMB_ALLOC_OVERFLOW_ERROR, data_pointer);
            memset (data_pointer, 
                EMB_ALLOC_INIT_VALUE, category->block_data_size);
        }

        if (!keep_start || 
            i) {
            /** 
             * Set the start block control part to EMB_ALLOC_INIT_VALUE 
             * in the first block if keep_start is not set
             * or if the current block is not the first one.
             */
            memset (current_block, EMB_ALLOC_INIT_VALUE, EMB_ALLOC_BLOCK_START_CONTROL_ALIGN_SIZE);
        } else {
            /**
             * Else make sure that the first block control is properly set.
             */
            memcpy (current_block, kEmbAllocBlockStart, EMB_ALLOC_ALIGN_AMOUNT);
            *used_block_count = EMB_ALLOC_VALUE_NOT_SET;
            *data_size = EMB_ALLOC_VALUE_NOT_SET;
        }

        if (!keep_end || (blocks_count - 1 != i)) {
            /** 
             * Set the start block end padding to EMB_ALLOC_INIT_VALUE 
             * in the last block if keep_end is not set
             * or if the current block is not the lase one.
             */
            memset (block_end_padding, EMB_ALLOC_INIT_VALUE, EMB_ALLOC_ALIGN_AMOUNT);
        } else {
            /**
             * Else make sure that the last block padding is properly set.
             */
            memcpy (block_end_padding, kEmbAllocBlockEnd, EMB_ALLOC_ALIGN_AMOUNT);
        }
    }
}

void* EmbAllocMallocOneBlockInternal (const EmbAllocMemPoolSettings* settings, 
    EmbAllocBlockCategory* category, size_t size)
{
    /** 
     * No need to check for the (pointer) param validity inside static functions.
     * Callers should make sure that the params are valid.
     */

    void* free_block = category->first_free_address;
    void* return_value = NULL;
    size_t* used_block_count = NULL;
    size_t* data_size = NULL;

    if (category->total_blocks <= category->occupied_blocks) {
        EmbAllocSetErrorInternal (EMB_ALLOC_GET_MEMPOOL_FROM_SETTINGS_PTR (settings),
            kEmbAllocInconsistentBlocks, EMB_ALLOC_BLOCK_INCONSISTENCY_ERROR, (void*) category);
        return NULL;
    }

    if ((NULL == category->first_free_address) ||
        (NULL == category->last_free_address)) {
        EmbAllocSetErrorInternal (EMB_ALLOC_GET_MEMPOOL_FROM_SETTINGS_PTR (settings), kEmbAllocInconsistentBlocks,
            EMB_ALLOC_BLOCK_INCONSISTENCY_ERROR, (void*) category);
        /** Damage containment for already-corrupt metadata: drive the authoritative
         * bitmap to the forced "full" state too, so popcount stays == occupied_blocks. */
        EmbAllocMarkBlocksInternal (category, category->start_address,
            category->total_blocks, true);
        category->occupied_blocks = category->total_blocks;
        category->first_free_address = NULL;
        category->last_free_address = NULL;
        return NULL;
    }

    /** first_free_address is only a lower-bound HINT; the free bitmap is the
     * authoritative free/occupied oracle. Defensive re-check: should the hint ever
     * lag the bitmap and point at an occupied block, confirm the target is genuinely
     * free and otherwise scan forward for the first free block. Without this a
     * single-block allocation could be handed an occupied multi-block inner
     * block, overlapping a live allocation. */
    if (!EmbAllocBlockIsFreeInternal (category, free_block)) {
        free_block = EmbAllocFirstFreeFromInternal (category, free_block);
    }

    if (NULL == free_block) {
        EmbAllocSetErrorInternal (EMB_ALLOC_GET_MEMPOOL_FROM_SETTINGS_PTR (settings),
            kEmbAllocInconsistentBlocks, EMB_ALLOC_BLOCK_INCONSISTENCY_ERROR, (void*) category);
        return NULL;
    }

    category->first_free_address = free_block;
    return_value = EMB_ALLOC_GET_PTR_FROM_BLOCK (free_block);
    used_block_count = EMB_ALLOC_GET_BLOCK_USE_COUNT_FROM_BLOCK (free_block);
    data_size = EMB_ALLOC_GET_MEMORY_USE_COUNT_FROM_BLOCK (free_block);

    EmbAllocMergeFreeBlocksInternal (settings, category, free_block, 1, true, true);

    if (settings->init_allocated_memory) {
        memset (return_value, 0, size);
    }

    *used_block_count = 1;
    *data_size = size;

    /** Record the block as occupied, and as a 1-block allocation head, in the
     * authoritative out-of-band bitmaps. */
    EmbAllocMarkBlocksInternal (category, free_block, 1, true);
    EmbAllocSetAllocStartInternal (category, free_block, true);

    category->occupied_blocks++;

    if (category->occupied_blocks < category->total_blocks) {
        /** free_block (now occupied) is still a valid lower bound on free blocks,
         * so refresh the hint to the next genuinely free block authoritatively. */
        EmbAllocRefreshFirstFreeInternal (category);
    } else {
        category->occupied_blocks = category->total_blocks;
        category->first_free_address = NULL;
        category->last_free_address = NULL;
    }

    return return_value;

}

bool EmbAllocCanAllocInMultipleBlocksInternal (void* mempool, EmbAllocBlockCategory* category,
    size_t size, void** block, size_t* blocks_count)
{
    /** 
     * No need to check for the (pointer) param validity inside static functions.
     * Callers should make sure that the params are valid.
     */

    unsigned char* verified_block = category->first_free_address;
    size_t counter = 0; 
    *block = NULL;
    *blocks_count = 0;

    if (category->total_blocks <= category->occupied_blocks) {
        EmbAllocSetErrorInternal (mempool, 
            kEmbAllocInconsistentBlocks, EMB_ALLOC_BLOCK_INCONSISTENCY_ERROR, (void*) category);
        return false;
    }

    if ((NULL == category->first_free_address) ||
        (NULL == category->last_free_address)) {
        EmbAllocSetErrorInternal (mempool, 
            kEmbAllocInconsistentBlocks, EMB_ALLOC_BLOCK_INCONSISTENCY_ERROR, (void*) category);
        /** Damage containment for already-corrupt metadata: drive the authoritative
         * bitmap to the forced "full" state too, so popcount stays == occupied_blocks. */
        EmbAllocMarkBlocksInternal (category, category->start_address,
            category->total_blocks, true);
        category->occupied_blocks = category->total_blocks;
        category->first_free_address = NULL;
        category->last_free_address = NULL;
        return false;
    }

    if (SIZE_T_SUM_OVERFLOW (size, EMB_ALLOC_BLOCK_CONTROL_ALIGN_SIZE)) {
        EmbAllocSetErrorInternal (mempool, kEmbAllocOverflow,
            EMB_ALLOC_OVERFLOW_ERROR, (void*) category);
        return false;
    }

    /**
     * Round up: how many whole blocks of this category are needed to hold `size`
     * bytes. Numerator and denominator are both block-total sizes (data + control), so
     * the inner blocks' control bytes count as usable payload -- an n-block run holds
     * block_data_size + (n-1)*stride bytes.
     */
    *blocks_count = (EMB_ALLOC_BLOCK_TOTAL_ALIGN_SIZE (size) /
        EMB_ALLOC_BLOCK_TOTAL_ALIGN_SIZE (category->block_data_size)) + 
        (( EMB_ALLOC_BLOCK_TOTAL_ALIGN_SIZE (size) %
            EMB_ALLOC_BLOCK_TOTAL_ALIGN_SIZE (category->block_data_size)) ?
            1 : 0);
    *block = NULL;

    if ((category->occupied_blocks + *blocks_count) > category->total_blocks) {
        return false;
    }

    while (verified_block <= (unsigned char*) category->last_address) {
        /** Authoritative free test via the bitmap. Reading use_count here was the
         * root cause of multi-block aliasing: an inner block of a live multi-block
         * allocation holds user data, and user data of 0xFF..FF (== NOT_SET) made
         * this scanner treat a live inner block as free and hand out overlapping
         * memory. The bitmap is immune to whatever the user writes into the block. */
        if (EmbAllocBlockIsFreeInternal (category, verified_block)) {
            if (NULL == *block) {
                *block = (void*) verified_block;
            }

            counter++;

            if (counter >= *blocks_count) {
                return true;
            }
        } else {
            counter = 0;
            *block = NULL;

            if ((   ((size_t) category->last_address - (size_t) verified_block) /
                    (size_t) EMB_ALLOC_BLOCK_TOTAL_ALIGN_SIZE (category->block_data_size)) <
                *blocks_count) {
                    /** There is simply not enough space, so just return false. */
                    return false;
                }
        }

        verified_block += EMB_ALLOC_BLOCK_TOTAL_ALIGN_SIZE (category->block_data_size);
    }

    *block = NULL;

    return false;
}

void* EmbAllocMallocMultiBlocksInternal (const EmbAllocMemPoolSettings* settings, 
    EmbAllocBlockCategory* category, size_t size, void* block, size_t blocks_count)
{
    /** 
     * No need to check for the (pointer) param validity inside static functions.
     * Callers should make sure that the params are valid.
     */

    void* return_value = NULL;
    size_t* used_block_count = NULL;
    size_t* data_size = NULL;

    if (category->total_blocks <= category->occupied_blocks) {
        EmbAllocSetErrorInternal (EMB_ALLOC_GET_MEMPOOL_FROM_SETTINGS_PTR (settings), 
            kEmbAllocInconsistentBlocks, EMB_ALLOC_BLOCK_INCONSISTENCY_ERROR, (void*) category);
        return NULL;
    }

    if ((NULL == block) ||
        (NULL == category->first_free_address) ||
        (NULL == category->last_free_address)) {
        EmbAllocSetErrorInternal (EMB_ALLOC_GET_MEMPOOL_FROM_SETTINGS_PTR (settings),
            kEmbAllocInconsistentBlocks, EMB_ALLOC_BLOCK_INCONSISTENCY_ERROR, (void*) category);
        /** Damage containment for already-corrupt metadata: drive the authoritative
         * bitmap to the forced "full" state too, so popcount stays == occupied_blocks. */
        EmbAllocMarkBlocksInternal (category, category->start_address,
            category->total_blocks, true);
        category->occupied_blocks = category->total_blocks;
        category->first_free_address = NULL;
        category->last_free_address = NULL;
        return NULL;
    }

    return_value = EMB_ALLOC_GET_PTR_FROM_BLOCK (block);
    used_block_count = EMB_ALLOC_GET_BLOCK_USE_COUNT_FROM_BLOCK (block);
    data_size = EMB_ALLOC_GET_MEMORY_USE_COUNT_FROM_BLOCK (block);
    
    EmbAllocMergeFreeBlocksInternal (settings, category, block, blocks_count, true, true);

    if (settings->init_allocated_memory) {
        memset (return_value, 0, size);
    }

    *used_block_count = blocks_count;
    *data_size = size;

    /** Record every spanned block as occupied, and the head block as the allocation
     * start, in the authoritative out-of-band bitmaps. Only the head gets the start
     * bit, so a forged inner-block header can never pass validation as a head. */
    EmbAllocMarkBlocksInternal (category, block, blocks_count, true);
    EmbAllocSetAllocStartInternal (category, block, true);

    category->occupied_blocks += blocks_count;

    if (category->occupied_blocks < category->total_blocks) {
        /** CanAlloc may have chosen a run above isolated lower free blocks, so the
         * old first_free hint remains a valid lower bound; refresh authoritatively.
         * This replaces a last_free-bounded rescan whose "safety net" could wrongly
         * NULL both hints while blocks were still free (occupied < total), leaving an
         * inconsistent state that later crashed on the NULL hint. */
        EmbAllocRefreshFirstFreeInternal (category);
    } else {
        category->occupied_blocks = category->total_blocks;
        category->first_free_address = NULL;
        category->last_free_address = NULL;
    }

    return return_value;
}

void* EmbAllocMalloc (EmbAllocMempool mempool, size_t size)
{
    if (EMB_ALLOC_PTR_IS_MEMPOOL (mempool, kEmbAllocMempoolStart)) {
        EmbAllocMempoolAuxData* aux_data = EMB_ALLOC_GET_MEMPOOL_AUX_DATA_PTR (mempool);
        const EmbAllocMemPoolSettings* settings = EMB_ALLOC_GET_MEMPOOL_SETTINGS_PTR (mempool);
        void* return_value = NULL;

#ifdef VERBOSE_DUMP_MEMPOOL
        if (strlen (settings->error_dump_file_name)) {
            FILE* error_file = fopen (settings->error_dump_file_name, "a");

            if (NULL != error_file) {
                fprintf(error_file, "\nTrying to allocate %zu bytes", size);

                fflush (error_file);
                fclose (error_file);
            } else {
                perror ("Error writing the error message in the mempool error dump file");
            }
        }
#endif /** VERBOSE_DUMP_MEMPOOL */

        if (size) {
            bool lock_acquired = true;
            EmbAllocErrorCallback error_callback_fn = settings->error_callback_fn;

            if (aux_data->thread_sync_mutex_initialized) {
                lock_acquired = !EmbAllocLockMutex ( &(aux_data->thread_sync_mutex));

                if (!lock_acquired) {
                    /** Lock failed: report (if a callback is set) and fail immediately, without
                    * reading the shared error slot unsynchronized or unlocking a mutex we
                    * never acquired. */
                    if (NULL != error_callback_fn) {
                        error_callback_fn (kEmbAllocThreadSyncError, EMB_ALLOC_MUTEX_LOCK_ERROR);
                    }
                }
            }

            if (lock_acquired) {
                ClearMempoolErrorInternal (aux_data);

                return_value = EmbAllocMallocInternal (settings,
                                                        EMB_ALLOC_GET_MEMPOOL_BLOCK_CATEGORIES_PTR (mempool),
                                                        size);

                if (aux_data->thread_sync_mutex_initialized &&
                    EmbAllocUnlockMutex ( &(aux_data->thread_sync_mutex)) &&
                    (NULL != error_callback_fn)) {
                    /** Unlock failed: the mutex is no longer reliably held, so report
                     * via the callback directly rather than writing the shared error
                     * slot unsynchronized (which would race a lock-holding writer). */
                    error_callback_fn (kEmbAllocThreadSyncError, EMB_ALLOC_MUTEX_UNLOCK_ERROR);
                }
            }
        }

#ifdef VERBOSE_DUMP_MEMPOOL
        if (strlen (settings->error_dump_file_name)) {
            FILE* error_file = fopen (settings->error_dump_file_name, "a");

            if (NULL != error_file) {
                if (NULL != return_value) {
                    size_t memory_offset = (unsigned char*) return_value - (unsigned char*) mempool;

                    fprintf(error_file, "Allocated %zu bytes at the 0x%p location "
                        "/ %zu mempool offset\n", size, return_value,
                        memory_offset);

                    EmbAllocDumpMempoolInternal (mempool,  EmbAllocGetMemoryRequirementsInternal (settings), 
                        error_file, memory_offset);
                } else {
                    fprintf(error_file, "\nFailed to allocate %zu bytes\n", size);
                }

                fflush (error_file);
                fclose (error_file);
            } else {
                perror ("Error writing the error message in the mempool error dump file");
            }
        }
#endif /** VERBOSE_DUMP_MEMPOOL */

        return return_value;
    } else {
        /** This is not a mempool, so we cannot send back a more detailed error message. */
        return NULL;
    }
}

EmbAllocBlockCategory* EmbAllocGetCategoryForPtr (EmbAllocBlockCategory* categories, 
    void* ptr)
{
    /** 
     * No need to check for the (pointer) param validity inside static functions.
     * Callers should make sure that the params are valid.
     */

    /** Make sure this fits into EMB_ALLOC_NUM_BLOCK_CATEGORIES. */
    unsigned char i = 0;
    void* block = NULL;
    size_t* used_block_count = NULL;
    size_t* data_size = NULL;
    void* mempool = EMB_ALLOC_GET_MEMPOOL_FROM_BLOCK_CATEGORIES_PTR (categories);
    EmbAllocBlockCategory* category = NULL;
    size_t block_total = 0;
    size_t block_index = 0;
    size_t block_data_size = 0;
    void* block_end_padding = NULL;


    /** Single validation authority: a public caller can hand us any pointer, so prove
     * it lies in the pool's block region before reading or writing ANY block-relative
     * metadata. The forgeable block-start marker is intentionally NOT checked here: a
     * foreign pointer is rejected by this in-pool bound, and a forged/interior pointer
     * is caught geometrically by the category-membership + grid-alignment checks and by
     * the out-of-band allocation-start bitmap below -- none of which the caller can
     * forge. Only address comparisons happen until the grid check proves a boundary. */
    if (!EMB_ALLOC_PTR_IS_IN_MEMPOOL (ptr, mempool, kEmbAllocMempoolStart)) {
        EmbAllocSetErrorInternal (mempool, kEmbAllocPointerParamError,
            EMB_ALLOC_INVALID_POINTER_PARAM_ERROR, ptr);
        return NULL;
    }

    block = EMB_ALLOC_GET_BLOCK_FROM_PTR (ptr);

    /** Prove category membership by address alone -- no block-relative metadata is
     * read or written until the pointer is shown to sit on a real block boundary. */
    for (i = 0; i < EMB_ALLOC_NUM_BLOCK_CATEGORIES; i++) {
        if (((uintptr_t) categories [i].start_address <= (uintptr_t) block) &&
            ((uintptr_t) categories [i].last_address >= (uintptr_t) block)) {
            category = categories + i;
            break;
        }
    }

    if (NULL == category) {
        EmbAllocSetErrorInternal (mempool, kEmbAllocPointerParamError,
            EMB_ALLOC_INVALID_POINTER_PARAM_ERROR, block);
        return NULL;
    }

    block_total = EMB_ALLOC_BLOCK_TOTAL_ALIGN_SIZE (category->block_data_size);
    block_index = ((uintptr_t) block - (uintptr_t) category->start_address) / block_total;

    /** The block-start marker is forgeable; require the pointer to sit exactly on a
     * block boundary BEFORE touching the (caller-reachable) block header, so a forged
     * interior pointer cannot drive an in-pool metadata write. */
    if (((uintptr_t) block - (uintptr_t) category->start_address) % block_total) {
        EmbAllocSetErrorInternal (mempool, kEmbAllocPointerParamError,
            EMB_ALLOC_INVALID_POINTER_PARAM_ERROR, block);
        return NULL;
    }

    /** Out-of-band allocation-start oracle. A multi-block allocation's inner-block
     * headers are user-writable payload, so the in-band start marker / use_count /
     * end marker can ALL be forged to make an interior block look like a valid
     * 1-block allocation (confirmed exploitable: forged inner free -> inner block
     * re-handed-out, overlapping the live allocation). The start bitmap is not
     * user-addressable: only a real allocation head has its bit set. Requiring it
     * here rejects forged interior frees, double-frees (the head's start bit is
     * cleared on free), and any occupied-but-not-head block -- none of which the
     * forgeable in-band checks below can catch. */
    if (!EmbAllocBlockIsAllocStartInternal (category, block)) {
        EmbAllocSetErrorInternal (mempool, kEmbAllocPointerParamError,
            EMB_ALLOC_INVALID_POINTER_PARAM_ERROR, block);
        return NULL;
    }

    /** The pointer is now a proven allocation head: its header is real block metadata. */
    used_block_count = EMB_ALLOC_GET_BLOCK_USE_COUNT_FROM_BLOCK (block);
    data_size = EMB_ALLOC_GET_MEMORY_USE_COUNT_FROM_BLOCK (block);

    if (EMB_ALLOC_VALUE_NOT_SET == *used_block_count) {
        EmbAllocSetErrorInternal (mempool, kEmbAllocOverflow,
            EMB_ALLOC_OVERFLOW_ERROR, (void*) used_block_count);
        return NULL;
    }

    if (EMB_ALLOC_VALUE_NOT_SET == *data_size) {
        EmbAllocSetErrorInternal (mempool, kEmbAllocOverflow,
            EMB_ALLOC_OVERFLOW_ERROR, (void*) data_size);
        return NULL;
    }

    /** The head's recorded span must be sane: at least one block, and the run must
     * not extend past the category's last block (block_index + used_block_count <=
     * total_blocks). Rejecting a corrupted/oversized use_count here stops it from
     * feeding the block_data_size computation below, which would otherwise describe
     * a run reaching past the category and drive out-of-range reads/writes. */
    if ((0 == *used_block_count) ||
        (*used_block_count > (category->total_blocks - block_index))) {
        EmbAllocSetErrorInternal (mempool, kEmbAllocPointerParamError,
            EMB_ALLOC_INVALID_POINTER_PARAM_ERROR, block);
        return NULL;
    }

    block_data_size = category->block_data_size +
        ((*used_block_count - 1) * block_total);

    /** An intact block can never record more payload than it can hold. Reject a
     * corrupted/oversized data_size as corruption, rather than letting a downstream
     * (block_data_size - *data_size) subtraction wrap into a huge read/write length. */
    if (*data_size > block_data_size) {
        EmbAllocSetErrorInternal (mempool, kEmbAllocOverflow,
            EMB_ALLOC_OVERFLOW_ERROR, (void*) data_size);
        return NULL;
    }

    block_end_padding = EMB_ALLOC_GET_END_PADDING_FROM_BLOCK (block, block_data_size);

    if (memcmp (block_end_padding, kEmbAllocBlockEnd, EMB_ALLOC_ALIGN_AMOUNT)) {
        EmbAllocSetErrorInternal (mempool, kEmbAllocOverflow,
            EMB_ALLOC_OVERFLOW_ERROR, block_end_padding);
        memcpy (block_end_padding, kEmbAllocBlockEnd, EMB_ALLOC_ALIGN_AMOUNT);
    }

    return category;
}

void EmbAllocFreeInternal (const EmbAllocMemPoolSettings* settings, 
    EmbAllocBlockCategory* categories, void* ptr)
{
    /** 
     * No need to check for the (pointer) param validity inside static functions.
     * Callers should make sure that the params are valid.
     */

    EmbAllocBlockCategory* category = EmbAllocGetCategoryForPtr (categories, ptr);

    if (NULL != category) {
        EmbAllocFreeBlockInternal (settings, category, ptr);
    } else {
        EmbAllocSetErrorInternal (EMB_ALLOC_GET_MEMPOOL_FROM_SETTINGS_PTR (settings), 
            kEmbAllocPointerParamError, EMB_ALLOC_INVALID_POINTER_PARAM_ERROR, NULL);
    }
}

void EmbAllocFreeBlockInternal (const EmbAllocMemPoolSettings* settings, 
    EmbAllocBlockCategory* category, void* ptr)
{
    /** 
     * No need to check for the (pointer) param validity inside static functions.
     * Callers should make sure that the params are valid.
     */

    size_t i = 0;
    void* block = EMB_ALLOC_GET_BLOCK_FROM_PTR (ptr);
    size_t used_block_count = *EMB_ALLOC_GET_BLOCK_USE_COUNT_FROM_BLOCK (block);
    size_t data_size = *EMB_ALLOC_GET_MEMORY_USE_COUNT_FROM_BLOCK (block);
    size_t block_data_size = category->block_data_size + 
        (   (used_block_count - 1) * 
            EMB_ALLOC_BLOCK_TOTAL_ALIGN_SIZE (category->block_data_size));
    

    /**
     * Overflow check on free: the unused tail of the allocation -- the bytes between
     * the user's requested data_size and the block(s)' full capacity block_data_size --
     * must still be the INIT fill. A non-INIT byte there is a write past the requested
     * size (a buffer overflow), reported at the first tail offset. data_size <=
     * block_data_size is guaranteed by EmbAllocGetCategoryForPtr, so the length below
     * never wraps.
     */
    if (settings->full_overflow_checks &&
        !EmbAllocCheckBuffer (
                (void*) ((unsigned char*) ptr + data_size),
                block_data_size - data_size,
                EMB_ALLOC_INIT_VALUE)) {
        EmbAllocSetErrorInternal (EMB_ALLOC_GET_MEMPOOL_FROM_SETTINGS_PTR (settings), 
            kEmbAllocOverflow, EMB_ALLOC_OVERFLOW_ERROR, 
            (void*) ((unsigned char*) ptr + data_size));
    }

    /** Wipe the whole freed payload back to the INIT fill: no stale user data lingers,
     *  and the next overflow check on this span has a clean baseline. */
    memset (ptr, EMB_ALLOC_INIT_VALUE, block_data_size);

    /**
     * Restore the per-block control data to its "uninitialized / free" value for every
     * block in the freed run: re-stamp each block's start and end markers and reset its
     * use_count / data_size slots to EMB_ALLOC_VALUE_NOT_SET. This splits a multi-block
     * allocation back into individually-formatted free blocks.
     */
    for (i = 0; i < used_block_count; i++) {
        void* freed_block = (void*) ((unsigned char*) block + 
            (i * EMB_ALLOC_BLOCK_TOTAL_ALIGN_SIZE (category->block_data_size)));

        memcpy (freed_block, kEmbAllocBlockStart, EMB_ALLOC_ALIGN_AMOUNT);
        memcpy (EMB_ALLOC_GET_END_PADDING_FROM_BLOCK (freed_block, 
            category->block_data_size), kEmbAllocBlockEnd, EMB_ALLOC_ALIGN_AMOUNT);
        *EMB_ALLOC_GET_BLOCK_USE_COUNT_FROM_BLOCK (freed_block) = EMB_ALLOC_VALUE_NOT_SET;
        *EMB_ALLOC_GET_MEMORY_USE_COUNT_FROM_BLOCK (freed_block) = EMB_ALLOC_VALUE_NOT_SET;
    }

    /** Clear the freed span (occupied) and the head's allocation-start bit in the
     * authoritative out-of-band bitmaps. Clearing the start bit makes a later
     * double-free of this head fail validation. */
    EmbAllocMarkBlocksInternal (category, block, used_block_count, false);
    EmbAllocSetAllocStartInternal (category, block, false);

    category->occupied_blocks -= used_block_count;

    /**
     * Extend the free-block hints to cover the just-freed head: first_free_address is
     * kept as a lower bound (the minimum free-block address) and last_free_address as
     * an upper bound (the maximum), so a later scan starts no later than the lowest
     * free block and stops no earlier than the highest.
     */
    if ((NULL == category->first_free_address) ||
        ((uintptr_t)category->first_free_address > (uintptr_t)block)) {
       category->first_free_address = block;
    }
    
    if ((NULL == category->last_free_address) ||
        ((uintptr_t)category->last_free_address < (uintptr_t)block)) {
       category->last_free_address = block; 
    } 
}

void EmbAllocFree (EmbAllocMempool mempool, void* ptr)
{
    if (EMB_ALLOC_PTR_IS_MEMPOOL (mempool, kEmbAllocMempoolStart)) {
        EmbAllocMempoolAuxData* aux_data = EMB_ALLOC_GET_MEMPOOL_AUX_DATA_PTR (mempool);
        const EmbAllocMemPoolSettings* settings = EMB_ALLOC_GET_MEMPOOL_SETTINGS_PTR (mempool);
#ifdef VERBOSE_DUMP_MEMPOOL
        bool valid_pointer_param = false;

        if (strlen (settings->error_dump_file_name)) {
            FILE* error_file = fopen (settings->error_dump_file_name, "a");

            if (NULL != error_file) {
                 fprintf(error_file, "\nTrying to free memory from the 0x%p location\n", ptr);

                fflush (error_file);
                fclose (error_file);
            } else {
                perror ("Error writing the error message in the mempool error dump file");
            }
        }
#endif /** VERBOSE_DUMP_MEMPOOL */

        if (ptr) {
            bool lock_acquired = true;
            EmbAllocErrorCallback error_callback_fn = settings->error_callback_fn;

            if (aux_data->thread_sync_mutex_initialized) {
                lock_acquired = !EmbAllocLockMutex ( &(aux_data->thread_sync_mutex));

                if (!lock_acquired) {
                    /** Lock failed: report (if a callback is set) and fail immediately, without
                    * reading the shared error slot unsynchronized or unlocking a mutex we
                    * never acquired. */
                    if (NULL != error_callback_fn) {
                        error_callback_fn (kEmbAllocThreadSyncError, EMB_ALLOC_MUTEX_LOCK_ERROR);
                    }
                }
            }

            if (lock_acquired) {
                ClearMempoolErrorInternal (aux_data);

                /** GetCategoryForPtr (reached via FreeInternal) is now the single
                 * validator; it rejects a foreign/interior/forged pointer with
                 * kEmbAllocPointerParamError, so there is no duplicated pre-check. */
                EmbAllocFreeInternal (settings,
                    EMB_ALLOC_GET_MEMPOOL_BLOCK_CATEGORIES_PTR (mempool),
                    ptr);
#ifdef VERBOSE_DUMP_MEMPOOL
                valid_pointer_param = (kEmbAllocPointerParamError != aux_data->last_error);
#endif /** VERBOSE_DUMP_MEMPOOL */

                if (aux_data->thread_sync_mutex_initialized &&
                    EmbAllocUnlockMutex ( &(aux_data->thread_sync_mutex)) &&
                    (NULL != error_callback_fn)) {
                    /** Unlock failed: the mutex is no longer reliably held, so report
                     * via the callback directly rather than writing the shared error
                     * slot unsynchronized (which would race a lock-holding writer). */
                    error_callback_fn (kEmbAllocThreadSyncError, EMB_ALLOC_MUTEX_UNLOCK_ERROR);
                }
            }
        }

#ifdef VERBOSE_DUMP_MEMPOOL
        if (strlen (settings->error_dump_file_name)) {
            FILE* error_file = fopen (settings->error_dump_file_name, "a");

            if (NULL != error_file) {
                if (valid_pointer_param) {
                    size_t memory_offset = (unsigned char*) ptr - (unsigned char*) mempool;

                    fprintf(error_file, "Freed bytes at the 0x%p location "
                        "/ %zu mempool offset\n", ptr,
                        memory_offset);

                    EmbAllocDumpMempoolInternal (mempool,  EmbAllocGetMemoryRequirementsInternal (settings), 
                        error_file, memory_offset);
                } else {
                    fprintf(error_file, "\nFailed to free bytes at the 0x%p location\n", ptr);
                }

                fflush (error_file);
                fclose (error_file);
            } else {
                perror ("Error writing the error message in the mempool error dump file");
            }
        }
#endif /** VERBOSE_DUMP_MEMPOOL */
    }
}

void* EmbAllocReallocInternal (const EmbAllocMemPoolSettings* settings, 
    EmbAllocBlockCategory* categories, void* ptr, size_t size)
{
    /** 
     * No need to check for the (pointer) param validity inside static functions.
     * Callers should make sure that the params are valid.
     */

    EmbAllocBlockCategory* category = EmbAllocGetCategoryForPtr (categories, ptr);

    if (NULL != category) {
        return EmbAllocReallocBlockInternal (settings, category, categories, ptr, size);
    } else {
        EmbAllocSetErrorInternal (EMB_ALLOC_GET_MEMPOOL_FROM_SETTINGS_PTR (settings), 
            kEmbAllocPointerParamError, EMB_ALLOC_INVALID_POINTER_PARAM_ERROR, NULL);
    }

    return NULL;
}


void* EmbAllocReallocBlockInternal (const EmbAllocMemPoolSettings* settings, 
    EmbAllocBlockCategory* category, EmbAllocBlockCategory* categories, 
    void* ptr, size_t size)
{
    /** 
     * No need to check for the (pointer) param validity inside static functions.
     * Callers should make sure that the params are valid.
     */

    void* block = EMB_ALLOC_GET_BLOCK_FROM_PTR (ptr);
    size_t* used_block_count = EMB_ALLOC_GET_BLOCK_USE_COUNT_FROM_BLOCK (block);
    size_t* data_size = EMB_ALLOC_GET_MEMORY_USE_COUNT_FROM_BLOCK (block);
    size_t block_data_size = category->block_data_size + 
        (   (*used_block_count - 1) * 
            EMB_ALLOC_BLOCK_TOTAL_ALIGN_SIZE (category->block_data_size));

    if (settings->full_overflow_checks &&
        !EmbAllocCheckBuffer (
                (void*) ((unsigned char*) ptr + *data_size),
                block_data_size - *data_size, 
                EMB_ALLOC_INIT_VALUE)) {
        EmbAllocSetErrorInternal (EMB_ALLOC_GET_MEMPOOL_FROM_SETTINGS_PTR (settings), 
            kEmbAllocOverflow, EMB_ALLOC_OVERFLOW_ERROR, 
            (void*) ((unsigned char*) ptr + *data_size));
        memset ((unsigned char*) ptr + *data_size, EMB_ALLOC_INIT_VALUE, block_data_size - *data_size);
    }

    if (size == *data_size) {
        /** Ih the new size is the same, do nothing, just return the same pointer. */
        return ptr;
    }

    if (size < *data_size) {
        /** 
         * If the new size is smaller, just reset the extra buffer to EMB_ALLOC_INIT_VALUE. 
         * WARNING: This function DOES NOT split blocks again if used_block_count is decremented.
         * This will generate memory waste in this case.
         * TODO: Free unused blocks if the allocated memory is partially freed.
         */
        memset ((unsigned char*) ptr + size, EMB_ALLOC_INIT_VALUE, *data_size - size);
        *data_size = size;
        return ptr;
    } else {
        if (size <= block_data_size) {
            if (settings->init_allocated_memory) {
                /**
                 * If the new size is bigger, but still fits inside the current block,
                 * just reset the extra buffer to 0.
                 */
                memset ((unsigned char*) ptr + *data_size, 0, size - *data_size);
            }

            *data_size = size;
            return ptr;
        } else {
            void* return_value = NULL;
            /**
             * Grow beyond the current block(s): `size` exceeds the existing capacity
             * block_data_size. Round up the extra bytes (size - block_data_size) by the
             * per-block stride to get how many additional blocks must be appended -- the
             * code then tries to claim that many free blocks contiguously right after
             * the current run (an in-place grow), falling back to allocate-copy-free.
             */
            size_t required_extra_blocks =
                (size - block_data_size) / EMB_ALLOC_BLOCK_TOTAL_ALIGN_SIZE (category->block_data_size) +
                (((size - block_data_size) % EMB_ALLOC_BLOCK_TOTAL_ALIGN_SIZE (category->block_data_size)) ?
                1 : 0);

            /**
             * Try to realloc in the same category only if the new extra size 
             * fits in the number of free blocks from this category. 
             */
            if ((category->occupied_blocks <= category->total_blocks) &&
                (required_extra_blocks <= (category->total_blocks - category->occupied_blocks))) {
                size_t i = 0;
                bool can_realloc_continously = true;

                /**
                 * It is not sufficient to have the required number of free blocks,
                 * they need to be continous as well. 
                 */
                for (i = 0; i < required_extra_blocks; i++) {
                    void* next_block = (void*) ((unsigned char*) block +
                        ((*used_block_count + i) *
                            EMB_ALLOC_BLOCK_TOTAL_ALIGN_SIZE (category->block_data_size)));

                    if (((uintptr_t) next_block > (uintptr_t) category->last_address) ||
                        (!EmbAllocBlockIsFreeInternal (category, next_block))) {
                        can_realloc_continously = false;
                        break;
                    }
                }

                if (can_realloc_continously) {
                    void* block_end_padding = EMB_ALLOC_GET_END_PADDING_FROM_BLOCK (block, 
                        block_data_size);

                    EmbAllocMergeFreeBlocksInternal (settings, category,
                        (void*)((unsigned char*) block +
                            (   *used_block_count *
                                EMB_ALLOC_BLOCK_TOTAL_ALIGN_SIZE (category->block_data_size))),
                            required_extra_blocks, false, true);

                    /** Mark the newly merged extension blocks occupied in the bitmap
                     * (uses the pre-increment *used_block_count as the span offset). */
                    EmbAllocMarkBlocksInternal (category,
                        (void*) ((unsigned char*) block +
                            (*used_block_count *
                                EMB_ALLOC_BLOCK_TOTAL_ALIGN_SIZE (category->block_data_size))),
                        required_extra_blocks, true);

                    /**
                     * Reset the "old" block end padding to EMB_ALLOC_INIT_VALUE.
                     */
                    memset (block_end_padding, 
                        EMB_ALLOC_INIT_VALUE, EMB_ALLOC_ALIGN_AMOUNT);

                    if (settings->init_allocated_memory) {
                        memset ((unsigned char*) ptr + *data_size, 0, size - *data_size);
                    }

                    *used_block_count += required_extra_blocks;
                    *data_size = size;
                    category->occupied_blocks += required_extra_blocks;

                    if (category->occupied_blocks >= category->total_blocks) {
                        category->occupied_blocks = category->total_blocks;
                        category->first_free_address = NULL;
                        category->last_free_address = NULL;
                    } else {
                        /** The grow consumed blocks above the original allocation;
                         * first_free remains a valid lower bound, so refresh it
                         * authoritatively rather than via stale-last_free rescans. */
                        EmbAllocRefreshFirstFreeInternal (category);
                    }

                    return ptr;
                }
            }

            /**
             * Malloc, copy and free only if the memory reallocation could not be done
             * inside the same category continously.
             */
            return_value = EmbAllocMallocInternal (settings, categories, size);

            if (NULL != return_value) {
                memcpy (return_value, ptr, *data_size);
                EmbAllocFreeBlockInternal (settings, category, ptr);
            }

            return return_value;
        }
    }

    return NULL;
}

void* EmbAllocRealloc (EmbAllocMempool mempool, void* ptr, size_t size)
{
    if (EMB_ALLOC_PTR_IS_MEMPOOL (mempool, kEmbAllocMempoolStart)) {
        EmbAllocMempoolAuxData* aux_data = EMB_ALLOC_GET_MEMPOOL_AUX_DATA_PTR (mempool);
        const EmbAllocMemPoolSettings* settings = EMB_ALLOC_GET_MEMPOOL_SETTINGS_PTR (mempool);
        void* return_value = NULL;

#ifdef VERBOSE_DUMP_MEMPOOL
        if (strlen (settings->error_dump_file_name)) {
            size_t memory_offset = EMB_ALLOC_VALUE_NOT_SET;
            FILE* error_file = fopen (settings->error_dump_file_name, "a");

            if (NULL != error_file) {
                if ((NULL != ptr) &&
                    EMB_ALLOC_PTR_IS_IN_MEMPOOL(ptr, mempool, kEmbAllocMempoolStart)) {
                    memory_offset = (unsigned char*) ptr - (unsigned char*) mempool;
                }

                fprintf(error_file, "\nTrying to reallocate %zu bytes from the 0x%p location "
                    "/ %zu mempool offset\n", size, ptr, memory_offset);

                fflush (error_file);
                fclose (error_file);
            } else {
                perror ("Error writing the error message in the mempool error dump file");
            }
        }
#endif /** VERBOSE_DUMP_MEMPOOL */

        if (ptr || size) {
            bool lock_acquired = true;
            EmbAllocErrorCallback error_callback_fn = settings->error_callback_fn;

            if (aux_data->thread_sync_mutex_initialized) {
                lock_acquired = !EmbAllocLockMutex ( &(aux_data->thread_sync_mutex));

                if (!lock_acquired) {
                    /** Lock failed: report (if a callback is set) and fail immediately, without
                    * reading the shared error slot unsynchronized or unlocking a mutex we
                    * never acquired. */
                    if (NULL != error_callback_fn) {
                        error_callback_fn (kEmbAllocThreadSyncError, EMB_ALLOC_MUTEX_LOCK_ERROR);
                    }
                }
            }

            if (lock_acquired) {
                ClearMempoolErrorInternal (aux_data);

                if (NULL == ptr) {
                    if (size) {
                        return_value = EmbAllocMallocInternal (settings,
                            EMB_ALLOC_GET_MEMPOOL_BLOCK_CATEGORIES_PTR (mempool),
                            size);
                    }
                } else if (0 == size) {
                    EmbAllocFreeInternal (settings,
                        EMB_ALLOC_GET_MEMPOOL_BLOCK_CATEGORIES_PTR (mempool),
                        ptr);
                } else {
                    return_value = EmbAllocReallocInternal (settings,
                        EMB_ALLOC_GET_MEMPOOL_BLOCK_CATEGORIES_PTR (mempool),
                        ptr, size);
                }

                if (aux_data->thread_sync_mutex_initialized &&
                    EmbAllocUnlockMutex ( &(aux_data->thread_sync_mutex)) &&
                    (NULL != error_callback_fn)) {
                    /** Unlock failed: the mutex is no longer reliably held, so report
                     * via the callback directly rather than writing the shared error
                     * slot unsynchronized (which would race a lock-holding writer). */
                    error_callback_fn (kEmbAllocThreadSyncError, EMB_ALLOC_MUTEX_UNLOCK_ERROR);
                }
            }
        }

#ifdef VERBOSE_DUMP_MEMPOOL
        if (strlen (settings->error_dump_file_name)) {
            FILE* error_file = fopen (settings->error_dump_file_name, "a");

            if (NULL != error_file) {
                size_t initial_memory_offset = EMB_ALLOC_VALUE_NOT_SET;

                if ((NULL != ptr) &&
                    EMB_ALLOC_PTR_IS_IN_MEMPOOL(ptr, mempool, kEmbAllocMempoolStart)) {
                    initial_memory_offset = (unsigned char*) ptr - (unsigned char*) mempool;
                }

                if (NULL != return_value) {
                    size_t final_memory_offset = (unsigned char*) return_value - (unsigned char*) mempool;

                    fprintf(error_file, "Reallocated %zu bytes from the 0x%p location "
                        "/ %zu mempool offset to the 0x%p location "
                        "/ %zu mempool offset\n", size, ptr, initial_memory_offset,
                        return_value, final_memory_offset);

                    EmbAllocDumpMempoolInternal (mempool,  EmbAllocGetMemoryRequirementsInternal (settings), 
                        error_file, final_memory_offset);
                } else {
                    fprintf(error_file, "\nFailed to reallocate %zu bytes from the 0x%p location "
                        "/ %zu mempool offset\n", size, ptr, initial_memory_offset);
                }

                fflush (error_file);
                fclose (error_file);
            } else {
                perror ("Error writing the error message in the mempool error dump file");
            }
        }
#endif /** VERBOSE_DUMP_MEMPOOL */

        return return_value;
    } else {
        /** This is not a mempool, so we cannot send back a more detailed error message. */
        return NULL;
    }
}

bool EmbAllocGetSettings (const EmbAllocMempool mempool, EmbAllocMemPoolSettings* settings)
{
    if (EMB_ALLOC_PTR_IS_MEMPOOL (mempool, kEmbAllocMempoolStart)) {
        EmbAllocMemPoolSettings* mempool_settings = EMB_ALLOC_GET_MEMPOOL_SETTINGS_PTR (mempool);
        EmbAllocMempoolAuxData* aux_data = EMB_ALLOC_GET_MEMPOOL_AUX_DATA_PTR (mempool);

        if (NULL != settings) {
            /** No need to threadsync here since the settings do not change after mempool create. */
            *settings = *mempool_settings;
            return true;
        } else {
            bool lock_acquired = true;
            EmbAllocErrorCallback error_callback_fn = mempool_settings->error_callback_fn;

            if (aux_data->thread_sync_mutex_initialized) {
                lock_acquired = !EmbAllocLockMutex ( &(aux_data->thread_sync_mutex));

                if (!lock_acquired) {
                    /** Lock failed: report (if a callback is set) and fail immediately, without
                    * reading the shared error slot unsynchronized or unlocking a mutex we
                    * never acquired. */
                    if (NULL != error_callback_fn) {
                        error_callback_fn (kEmbAllocThreadSyncError, EMB_ALLOC_MUTEX_LOCK_ERROR);
                    }
                }
            }

            if (lock_acquired) {
                EmbAllocSetErrorInternal (mempool,
                    kEmbAllocOutputParamError, EMB_ALLOC_INVALID_OUTPUT_PARAM_ERROR, NULL);

                if (aux_data->thread_sync_mutex_initialized &&
                    EmbAllocUnlockMutex ( &(aux_data->thread_sync_mutex)) &&
                    (NULL != error_callback_fn)) {
                    /** Unlock failed: the mutex is no longer reliably held, so report
                     * via the callback directly rather than writing the shared error
                     * slot unsynchronized (which would race a lock-holding writer). */
                    error_callback_fn (kEmbAllocThreadSyncError, EMB_ALLOC_MUTEX_UNLOCK_ERROR);
                }
            }

            return false;
        }
    } else {
        /** This is not a mempool, so we cannot send back a more detailed error message. */
        return false;
    }
}

bool EmbAllocGetLastErrorCodeAndMessage (EmbAllocMempool mempool, EmbAllocErrors *code, char *message, size_t message_len)
{
    if (EMB_ALLOC_PTR_IS_MEMPOOL (mempool, kEmbAllocMempoolStart)) {
        EmbAllocMempoolAuxData* aux_data = EMB_ALLOC_GET_MEMPOOL_AUX_DATA_PTR (mempool);
        const EmbAllocMemPoolSettings* settings = EMB_ALLOC_GET_MEMPOOL_SETTINGS_PTR (mempool);
        EmbAllocErrorCallback error_callback_fn = settings->error_callback_fn;
        bool ret_val = true;
        bool lock_acquired = true;

        if (aux_data->thread_sync_mutex_initialized) {
            lock_acquired = !EmbAllocLockMutex ( &(aux_data->thread_sync_mutex));
        }

        if (!lock_acquired) {
            /** Lock failed: report (if a callback is set) and fail immediately, without
             * reading the shared error slot unsynchronized or unlocking a mutex we
             * never acquired. */
            if (NULL != error_callback_fn) {
                error_callback_fn (kEmbAllocThreadSyncError, EMB_ALLOC_MUTEX_LOCK_ERROR);
            }
            return false;
        }

        if (NULL != code) {
            *code = aux_data->last_error;
        } else {
            ret_val = false;
        }

        if (ret_val && (NULL != message) &&
            (message_len > strlen (aux_data->last_error_message))) {
            strncpy (message, aux_data->last_error_message, message_len - 1);
            message[message_len - 1] = '\0';
        } else {
            ret_val = false;
        }

        if (aux_data->thread_sync_mutex_initialized &&
            EmbAllocUnlockMutex ( &(aux_data->thread_sync_mutex)) &&
            (NULL != error_callback_fn)) {
            /** Unlock failed: the mutex is no longer reliably held, so report
                * via the callback directly rather than writing the shared error
                * slot unsynchronized (which would race a lock-holding writer). */
            error_callback_fn (kEmbAllocThreadSyncError, EMB_ALLOC_MUTEX_UNLOCK_ERROR);
        }

        return ret_val;
    } else {
        return false;
    }
}
