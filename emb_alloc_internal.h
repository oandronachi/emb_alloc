/** 
 * Embedded Memory Allocator Internal Data Types and Defines
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


#ifndef __EMB_ALLOC_INTERNAL_H__
#define __EMB_ALLOC_INTERNAL_H__

#include "emb_alloc.h"
#include "emb_alloc_util.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** The per mempool error buffer size. This should be enough for all types of messages. */
#define EMB_ALLOC_ERROR_MESSAGE_SIZE 512

/**
 * The initialization value of the pre-allocated memory.
 * Useful for detecting buffer overflows.
 */
#define EMB_ALLOC_INIT_VALUE 0xAC

/**
 * https://mail.gnome.org/archives/gtk-devel-list/2004-December/msg00091.html
 * GNU libc alignment of 2 * sizeof(size_t).  
 * This is 16 bytes in the worst case scenarios (for 64 bit platforms).
 *
 */
#define EMB_ALLOC_ALIGN_AMOUNT (2 * sizeof (size_t))

/**
 * Macro for aligning a certain memory size.
 */
#define EMB_ALLOC_ALIGN_SIZE(size)  (   (~(EMB_ALLOC_ALIGN_AMOUNT - 1)) & \
                                        ((size) + (EMB_ALLOC_ALIGN_AMOUNT - 1)))

/**
 * The actual aaligned size in the mempool occupied by the blocks management data.
 * There are 8 block size categories (see EmbAllocMemPoolSettings structure).
 * @note The EMB_ALLOC_NUM_BLOCK_CATEGORIES must be kept in sync
 * with the number of "num_<size>_bytes_blocks" fields inside EmbAllocMemPoolSettings.
 */
#define EMB_ALLOC_NUM_BLOCK_CATEGORIES 8
#define EMB_ALLOC_BLOCK_CATEGORY_ALIGN_SIZE \
    EMB_ALLOC_ALIGN_SIZE (EMB_ALLOC_NUM_BLOCK_CATEGORIES * sizeof (EmbAllocBlockCategory))

/**
 * The aligned size in the mempool occupied by the settings data.
 */
#define EMB_ALLOC_MEMPOOL_SETTINGS_ALIGN_SIZE \
    EMB_ALLOC_ALIGN_SIZE (sizeof (EmbAllocMemPoolSettings))

/**
 * The aligned size in the mempool occupied by the  auxiliary data.
 */
#define EMB_ALLOC_MEMPOOL_AUX_DATA_ALIGN_SIZE \
    EMB_ALLOC_ALIGN_SIZE (sizeof (EmbAllocMempoolAuxData))

/**
 * The total aligned size in the mempool occupied by the control data
 * - start and end markers
 * - settings
 * - blocks management
 * - auxiliary (thread mutex, error storage, similar to Linux errno)
 * 
 * The control data is split in 2:
 * - start marker, settings, blocks management and auxiliary 
 * (aligned and put in this order at the begining of the mempool, 
 * before any memory blocks allocation)
 * - end marker (aligned and put at the end of the mempool, 
 * after all memory blocks)
 */
#define EMB_ALLOC_MEMPOOL_CONTROL_ALIGN_SIZE \
    (2 * EMB_ALLOC_ALIGN_AMOUNT) + EMB_ALLOC_MEMPOOL_SETTINGS_ALIGN_SIZE + \
    EMB_ALLOC_BLOCK_CATEGORY_ALIGN_SIZE + EMB_ALLOC_MEMPOOL_AUX_DATA_ALIGN_SIZE

/**
 * The control sections of a block are the 2*EMB_ALLOC_ALIGN_AMOUNT
 * (start & end paddings) + (2 * sizeof (size_t)) 
 * (same as EMB_ALLOC_ALIGN_AMOUNT; for blocks usage and data size)
 */
#define EMB_ALLOC_BLOCK_CONTROL_ALIGN_SIZE (3 * EMB_ALLOC_ALIGN_AMOUNT)

/**
 * The control sections of a block are the 2*EMB_ALLOC_ALIGN_AMOUNT
 * (start & end paddings) + (2 * sizeof (size_t)) 
 * (same as EMB_ALLOC_ALIGN_AMOUNT; for blocks usage and data size)
 */
#define EMB_ALLOC_BLOCK_START_CONTROL_ALIGN_SIZE (2 * EMB_ALLOC_ALIGN_AMOUNT)

/**
 * Retrieves the EmbAllocMemPoolSettings* associated with the mempool param.
 * Since this define is internal, its usage is restricted 
 * to the emb_alloc.c file alone. It should only be called after validating that the 
 * mempool is ok (by comparing the start padding with kEmbAllocMempoolStart).
 * EMB_ALLOC_ALIGN_AMOUNT is the kEmbAllocMempoolStart size that is compared.
 */
#define EMB_ALLOC_GET_MEMPOOL_SETTINGS_PTR(mempool) \
    (EmbAllocMemPoolSettings*) ((unsigned char*) (mempool) + EMB_ALLOC_ALIGN_AMOUNT)

/**
 * Retrieves the EmbAllocMempoolAuxData* array associated with the mempool param.
 * Since this define is internal, its usage is restricted 
 * to the emb_alloc.c file alone. It should only be called after validating that the 
 * mempool is ok (by comparing the start padding with kEmbAllocMempoolStart).
 * EMB_ALLOC_ALIGN_AMOUNT is the kEmbAllocMempoolStart size that is compared.
 */
#define EMB_ALLOC_GET_MEMPOOL_BLOCK_CATEGORIES_PTR(mempool) \
    (EmbAllocBlockCategory*) ((unsigned char*) (mempool) + EMB_ALLOC_ALIGN_AMOUNT + \
    EMB_ALLOC_MEMPOOL_SETTINGS_ALIGN_SIZE)

/**
 * Retrieves the EmbAllocMempoolAuxData* associated with the mempool param.
 * Since this define is internal, its usage is restricted 
 * to the emb_alloc.c file alone. It should only be called after validating that the 
 * mempool is ok (by comparing the start padding with kEmbAllocMempoolStart).
 * EMB_ALLOC_ALIGN_AMOUNT is the kEmbAllocMempoolStart size that is compared.
 */
#define EMB_ALLOC_GET_MEMPOOL_AUX_DATA_PTR(mempool) \
    (EmbAllocMempoolAuxData*) ((unsigned char*) (mempool) + EMB_ALLOC_ALIGN_AMOUNT + \
    EMB_ALLOC_MEMPOOL_SETTINGS_ALIGN_SIZE + EMB_ALLOC_BLOCK_CATEGORY_ALIGN_SIZE)

/**
 * Retrieves the first allocated block associated with the mempool param.
 * Since this define is internal, its usage is restricted 
 * to the emb_alloc.c file alone. It should only be called after validating that the 
 * mempool is ok (by comparing the start padding with kEmbAllocMempoolStart).
 * EMB_ALLOC_ALIGN_AMOUNT is the kEmbAllocMempoolStart size that is compared.
 */
#define EMB_ALLOC_GET_MEMPOOL_FIRST_BLOCK_PTR(mempool) \
    (void*) ((unsigned char*) (mempool) + EMB_ALLOC_ALIGN_AMOUNT + \
    EMB_ALLOC_MEMPOOL_SETTINGS_ALIGN_SIZE + EMB_ALLOC_BLOCK_CATEGORY_ALIGN_SIZE + \
    EMB_ALLOC_MEMPOOL_AUX_DATA_ALIGN_SIZE)

/**
 * This is the allocated size of a block.
 * It assumes that data_size is already aligned to EMB_ALLOC_ALIGN_AMOUNT.
 * If there will be "num_<size>_bytes_blocks" fields inside 
 * EmbAllocMemPoolSettings that are no aligned to EMB_ALLOC_ALIGN_AMOUNT, 
 * then the entire allocation mechanism needs to be adjusted to account for this.
 * Furthermore, data_size will need to be aligned with EMB_ALLOC_ALIGN_SIZE and the 
 * EmbAllocSanitizeSettingsInternal needs to to a different kind of sanitization 
 * in regards to total_size.
 * Since this define is internal, its usage is restricted 
 * to the emb_alloc.c file alone.
 */
#define EMB_ALLOC_BLOCK_TOTAL_ALIGN_SIZE(data_size) \
    ((data_size) + EMB_ALLOC_BLOCK_CONTROL_ALIGN_SIZE)

/**
 * Retrieves the block start memory address from the raw pointer.
 * Since this define is internal, its usage is restricted 
 * to the emb_alloc.c file alone.
 */
#define EMB_ALLOC_GET_BLOCK_FROM_PTR(pointer) \
    (void*) ((unsigned char*) (pointer) - \
    (2 * EMB_ALLOC_ALIGN_AMOUNT /*block start padding, blocks usage and data size offset*/))

/**
 * Retrieves the block usage counter from the raw pointer.
 * Since this define is internal, its usage is restricted 
 * to the emb_alloc.c file alone.
 */
#define EMB_ALLOC_GET_BLOCK_USE_COUNT_FROM_PTR(pointer) \
    (size_t*) ((unsigned char*) (pointer) - \
    (EMB_ALLOC_ALIGN_AMOUNT /*data size offset*/))

/**
 * Retrieves the block used bytes from the raw pointer.
 * Since this define is internal, its usage is restricted 
 * to the emb_alloc.c file alone.
 */
#define EMB_ALLOC_GET_MEMORY_USE_COUNT_FROM_PTR(pointer) \
    (size_t*) ((unsigned char*) (pointer) - \
    sizeof(size_t)/*memory usage offset*/)

/**
 * Retrieves the raw pointer from the block start memory address.
 */
#define EMB_ALLOC_GET_PTR_FROM_BLOCK(block) \
    (void*) ((unsigned char*) (block) + \
    (2 * EMB_ALLOC_ALIGN_AMOUNT /*block start padding, blocks usage and data size offset*/))

/**
 * Retrieves the block usage counter from the block start memory address.
 * Since this define is internal, its usage is restricted 
 * to the emb_alloc.c file alone.
 */
#define EMB_ALLOC_GET_BLOCK_USE_COUNT_FROM_BLOCK(block) \
    (size_t*) ((unsigned char*) (block) + \
    (EMB_ALLOC_ALIGN_AMOUNT /*block start padding*/))

/**
 * Retrieves the block used bytes from the block start memory address.
 * Since this define is internal, its usage is restricted 
 * to the emb_alloc.c file alone.
 */
#define EMB_ALLOC_GET_MEMORY_USE_COUNT_FROM_BLOCK(block) \
    (size_t*) ((unsigned char*) (block) + \
    (EMB_ALLOC_ALIGN_AMOUNT + sizeof(size_t)/*memory usage offset*/))

/**
 * Retrieves the block end padding from the block start memory address.
 * Since this define is internal, its usage is restricted 
 * to the emb_alloc.c file alone.
 */
#define EMB_ALLOC_GET_END_PADDING_FROM_BLOCK(block, size) \
    (void*) ((unsigned char*) (block) + \
    (2 * EMB_ALLOC_ALIGN_AMOUNT /*block start padding and counters*/) + (size))

/**
 * Checks whether the raw pointer is the memory start address of a block.
 * Since this define is internal, its usage is restricted 
 * to the emb_alloc.c file alone.
 */
#define EMB_ALLOC_PTR_IS_BLOCK(pointer, kEmbAllocBlockStart) \
    (0 == memcmp (EMB_ALLOC_GET_BLOCK_FROM_PTR (pointer), \
                (kEmbAllocBlockStart), EMB_ALLOC_ALIGN_AMOUNT))

/**
 * Retrieves the mempool associated with the EmbAllocMemPoolSettings param.
 * Since this define is internal, its usage is restricted 
 * to the emb_alloc.c file alone.
 */
#define EMB_ALLOC_GET_MEMPOOL_FROM_SETTINGS_PTR(settings) \
    (void*) ((unsigned char*) (settings) - EMB_ALLOC_ALIGN_AMOUNT)

/**
 * Retrieves the mempool associated with the EmbAllocBlockCategory param.
 * Since this define is internal, its usage is restricted 
 * to the emb_alloc.c file alone.
 */
#define EMB_ALLOC_GET_MEMPOOL_FROM_BLOCK_CATEGORIES_PTR(categories) \
    (void*) ((unsigned char*) (categories) - EMB_ALLOC_ALIGN_AMOUNT - \
    EMB_ALLOC_MEMPOOL_SETTINGS_ALIGN_SIZE)

/**
 * Retrieves the mempool associated with the EmbAllocMempoolAuxData param.
 * Since this define is internal, its usage is restricted 
 * to the emb_alloc.c file alone.
 */
#define EMB_ALLOC_GET_MEMPOOL_FROM_AUX_DATA_PTR(aux_data) \
    (void*) ((unsigned char*) (aux_data) - EMB_ALLOC_ALIGN_AMOUNT - \
    EMB_ALLOC_MEMPOOL_SETTINGS_ALIGN_SIZE - EMB_ALLOC_BLOCK_CATEGORY_ALIGN_SIZE)

/**
 * Checks whether the raw pointer is the memory start address of a mempool.
 * Since this define is internal, its usage is restricted 
 * to the emb_alloc.c file alone.
 */
#define EMB_ALLOC_PTR_IS_MEMPOOL(pointer, kEmbAllocMempoolStart) \
    (0 == memcmp ((pointer), (kEmbAllocMempoolStart), EMB_ALLOC_ALIGN_AMOUNT))

/**
 * Determines whether a certain memory size can be allocated 
 * in a single block of the param category.
 * Since this define is internal, its usage is restricted 
 * to the emb_alloc.c file alone.
 */
#define EMB_ALLOC_CAN_ALLOC_IN_A_BLOCK(category, size) \
    (   ((category).block_data_size >= (size)) && \
        ((category).occupied_blocks < ((category).total_blocks)))

/** Management structure for the blocks of a certain dimension in the mempool. */
typedef struct {
    /** The start address for the first block of this dimension. */
    void* start_address;
    /** The first free block in the continous pool of blocks of this dimension. */
    void* first_free_address;
    /** The last free block in the continous pool of blocks of this dimension. */
    void* last_free_address;
    /** The start address for the last block address of this dimension.  */
    void* last_address;
    /** The size of each block. */
    size_t block_data_size;
    /** The total number allocated of blocks. */
    size_t total_blocks;
    /** The number of available allocated blocks. */
    size_t occupied_blocks;
} EmbAllocBlockCategory;

/** Auxiliary data structure for handling multithreading and errors in the mempool */
typedef struct {
    /** OS generic mutex used for thread synchronization. */
    EmbAllocMutex thread_sync_mutex;
    /** 
     * Bool flag to mark that the thread sync mutex can be used.
     * It will be set to true when the EmbAllocMemPoolSettings.threadsafe is true
     * and the thread_sync_mutex has been initialized successfully.
     * If true, then thread_sync_mutex will be used when allocating, dealocating
     * or freeing memory blocks.
     */
    bool thread_sync_mutex_initialized;
    /** The last error code (similar to Linux errno). */
    EmbAllocErrors last_error;
    /** The human readable last error message (similar to Linux strerror(errno)). */
    char last_error_message [EMB_ALLOC_ERROR_MESSAGE_SIZE];
} EmbAllocMempoolAuxData;

/** Error strings. */
#define EMB_ALLOC_INCONSISTENT_SETTINGS "The mempool settings are inconsistent."
#define EMB_ALLOC_NOT_A_MEMPOOL_ERROR "The mempool is invalid."
#define EMB_ALLOC_NOT_ENOUGH_MEMORY_ERROR "The mempool is full. Cannot allocate memory."
#define EMB_ALLOC_CANNOT_CREATE_MEMPOOL_ERROR "The mempool cannot be allocated."
#define EMB_ALLOC_OVERFLOW_ERROR "Memory overflow detected."
#define EMB_ALLOC_BLOCK_INCONSISTENCY_ERROR "Inconsistency found in the data blocks management section."
#define EMB_ALLOC_INVALID_OUTPUT_PARAM_ERROR "Invalid output parameter."
#define EMB_ALLOC_MUTEX_LOCK_ERROR "Could not lock the threadsync mutex."
#define EMB_ALLOC_MUTEX_UNLOCK_ERROR "Could not unlock the threadsync mutex."
#define EMB_ALLOC_MUTEX_DESTROY_ERROR "Could not destroy the threadsync mutex."
#define EMB_ALLOC_INVALID_POINTER_PARAM_ERROR "Invalid pointer input parameter."

#define EMB_ALLOC_MEMORY_LOCATION_ERROR_FORMAT "(at the 0x%p location / %ld mempool offset)"

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif //__EMB_ALLOC_INTERNAL_H__