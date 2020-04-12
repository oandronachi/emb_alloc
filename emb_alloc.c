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
 */
static bool EmbAllocSanitizeSettingsInternal (EmbAllocMemPoolSettings* settings);

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
 * @param aux_data mempool aux data for error marking and thread sync.
 * @param size the actual size of the data to be allocated.
 * @return the pointer to the beginning of newly allocated memory on success, 
 *         NULL otherwise
 */
static void* EmbAllocMallocInternal (const EmbAllocMemPoolSettings* settings, 
    EmbAllocBlockCategory* categories, EmbAllocMempoolAuxData* aux_data, size_t size);

/**
 * Merge free blocks and performs the sanity checks on them.
 * @param settings used for full_overflow_checks and to call error_callback_fn.
 * @param category mempool blocks management data to be updated.
 * @param aux_data mempool aux data for error marking.
 * @param block the actual start block that to be used for allocation.
 * @param blocks_count the number of blocks that will to be used for allocation.
 * @param keep_start keep the start padding and the counters at the start of the first block.
 * @param keep_end keep the end padding at the end of the last block.
 */
static void EmbAllocMergeFreeBlocksInternal (const EmbAllocMemPoolSettings* settings, 
    EmbAllocBlockCategory* category, EmbAllocMempoolAuxData* aux_data, 
    void* block, size_t blocks_count, bool keep_start, bool keep_end);

/**
 * Allocates a memory chunk in a single memory block.
 * @param settings used for full_overflow_checks and to call error_callback_fn.
 * @param category mempool blocks management data to be updated.
 * @param aux_data mempool aux data for error marking.
 * @param size the actual size of the data to be allocated.
 * @return the pointer to the beginning of newly allocated memory on success, 
 *         NULL otherwise
 */
static void* EmbAllocMallocOneBlockInternal (const EmbAllocMemPoolSettings* settings, 
    EmbAllocBlockCategory* category, EmbAllocMempoolAuxData* aux_data, size_t size);

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
 * @param aux_data mempool aux data for error marking.
 * @param size the actual size of the data to be allocated.
 * @param block the actual start block that to be used for allocation.
 * @param blocks_count the number of blocks that will to be used for allocation.
 * @return the pointer to the beginning of newly allocated memory on success, 
 *         NULL otherwise
 */
static void* EmbAllocMallocMultiBlocksInternal (const EmbAllocMemPoolSettings* settings, 
    EmbAllocBlockCategory* category, EmbAllocMempoolAuxData* aux_data, 
    size_t size, void* block, size_t blocks_count);

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
 * @param aux_data mempool aux data for error marking and thread sync.
 * @param ptr the actual memory chunk address to be freed.
 */
static void EmbAllocFreeInternal (const EmbAllocMemPoolSettings* settings, 
    EmbAllocBlockCategory* categories, EmbAllocMempoolAuxData* aux_data, void* ptr);

/**
 * Frees a memory chunk in a specific category.
 * @param settings used for full_overflow_checks and to call error_callback_fn.
 * @param category mempool blocks management data to be updated after the chunk is freed.
 * @param aux_data mempool aux data for error marking and thread sync.
 * @param ptr the actual memory chunk address to be freed.
 */
static void EmbAllocFreeBlockInternal (const EmbAllocMemPoolSettings* settings, 
    EmbAllocBlockCategory* category, EmbAllocMempoolAuxData* aux_data, 
    void* ptr);

/**
 * Reallocates a memory chunk.
 * @param settings used for full_overflow_checks and to call error_callback_fn.
 * @param categories mempool blocks management data to be updated after the chunk is reallocated.
 * @param aux_data mempool aux data for error marking and thread sync.
 * @param ptr the actual memory chunk address to be reallocated.
 * @param size number of bytes to reallocated.
 * @return the pointer to the beginning of newly allocated memory on success, 
 *         NULL otherwise
 * 
 */
static void* EmbAllocReallocInternal (const EmbAllocMemPoolSettings* settings, 
    EmbAllocBlockCategory* categories, EmbAllocMempoolAuxData* aux_data, 
    void* ptr, size_t size);

/**
 * Reallocates a memory chunk in a specific category.
 * @param settings used for full_overflow_checks and to call error_callback_fn.
 * @param category mempool blocks management data that contains the ptr address 
 *                 to be updated after the chunk is reallocated.
 * @param categories mempool blocks management data to be updated after the chunk is reallocated.
 * @param aux_data mempool aux data for error marking and thread sync.
 * @param ptr the actual memory chunk address to be reallocated.
  * @param size number of bytes to reallocated.
 * @return the pointer to the beginning of newly allocated memory on success, 
 *         NULL otherwise
 */
static void* EmbAllocReallocBlockInternal (const EmbAllocMemPoolSettings* settings, 
    EmbAllocBlockCategory* category, EmbAllocBlockCategory* categories, 
    EmbAllocMempoolAuxData* aux_data, void* ptr, size_t size);

/** ===========================================================================
===============================================================================
===============================================================================
============================================================================ */

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
    EmbAllocMempoolAuxData* aux_data = EMB_ALLOC_GET_MEMPOOL_AUX_DATA_PTR (mempool);
    const EmbAllocMemPoolSettings* settings = EMB_ALLOC_GET_MEMPOOL_SETTINGS_PTR (mempool);
    size_t memory_offset = EMB_ALLOC_VALUE_NOT_SET;
    aux_data->last_error = error;
    memset (aux_data->last_error_message, 0, sizeof (aux_data->last_error_message));
    strncpy (aux_data->last_error_message, error_message, sizeof (aux_data->last_error_message));

    if (NULL != error_memory_location) {
        char memory_location [EMB_ALLOC_ERROR_MESSAGE_SIZE];
        memory_offset = (unsigned char*) error_memory_location - (unsigned char*) mempool;
        memset (memory_location, 0, sizeof (memory_location));
        snprintf (memory_location, sizeof (memory_location), 
            EMB_ALLOC_MEMORY_LOCATION_ERROR_FORMAT, error_memory_location, 
            memory_offset);
        strncat (aux_data->last_error_message, memory_location, sizeof (aux_data->last_error_message) - strlen (memory_location));
    }

    if (NULL != settings->error_callback_fn) {
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

bool EmbAllocSanitizeSettingsInternal (EmbAllocMemPoolSettings* settings)
{
    /** 
     * No need to check for the (pointer) param validity inside static functions.
     * Callers should make sure that the params are valid.
     */

    size_t initial_total_size = settings->total_size;
    settings->total_size = (   (settings->num_32_bytes_blocks * 32) + 
                                (settings->num_64_bytes_blocks * 64) + 
                                (settings->num_128_bytes_blocks * 128) + 
                                (settings->num_256_bytes_blocks * 256) + 
                                (settings->num_512_bytes_blocks * 512) + 
                                (settings->num_1k_bytes_blocks * 1024) + 
                                (settings->num_2k_bytes_blocks * 2048) +
                                (settings->num_4k_bytes_blocks * 4096));

    /**
     * remove declared inside stdio.h
     * Delete the error dump file when creating the mempool.
     */
    if (strlen (settings->error_dump_file_name)) {
        /** It does not really matter if this fails or not, 
         * so we do not handle the return value. 
         */
        if (!remove (settings->error_dump_file_name)) {
            perror ("Error removing the mempool error dump file");
        }
    }

    /** 
     * For the moment just align the total size with the one deducted 
     * from the blockes counters. The total size is adjusted.
     * If this logic needs to change in the future, 
     * then this function needs to be adjusted.
     */
    return (settings->total_size == initial_total_size);
}

size_t EmbAllocGetMemoryRequirementsInternal (const EmbAllocMemPoolSettings* settings)
{
    /** 
     * No need to check for the (pointer) param validity inside static functions.
     * Callers should make sure that the params are valid.
     */

    size_t total_num_blocks =   settings->num_32_bytes_blocks + 
                                settings->num_64_bytes_blocks + 
                                settings->num_128_bytes_blocks + 
                                settings->num_256_bytes_blocks + 
                                settings->num_512_bytes_blocks + 
                                settings->num_1k_bytes_blocks + 
                                settings->num_2k_bytes_blocks + 
                                settings->num_4k_bytes_blocks;

    return (EMB_ALLOC_MEMPOOL_CONTROL_ALIGN_SIZE + 
            (EMB_ALLOC_BLOCK_CONTROL_ALIGN_SIZE * total_num_blocks) + 
            settings->total_size);
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
        }
    }

    fprintf (appendable_file, "Mempool dump at location 0x%x (%ld lines)", 
        (unsigned int) mempool, (mempool_size / EMB_ALLOC_ALIGN_AMOUNT));

    for (i = 0; i < mempool_size; i++) {
        if (0 == i % EMB_ALLOC_ALIGN_AMOUNT) {
            fprintf (appendable_file, "\n%ld: ", (i / EMB_ALLOC_ALIGN_AMOUNT));
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
        bool consistent_settings = EmbAllocSanitizeSettingsInternal (&sanitized_settings);
        allocated_size = EmbAllocGetMemoryRequirementsInternal (&sanitized_settings);
        return_value = (void*) malloc (allocated_size);
        
        if (NULL != return_value) {
            EmbAllocInitializeInternal (return_value, allocated_size, &sanitized_settings);

            if (!consistent_settings) {
                EmbAllocSetErrorInternal (return_value, kEmbAllocInconsistentSettings,
                    EMB_ALLOC_INCONSISTENT_SETTINGS, NULL);
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
        EmbAllocMutex thread_sync_mutex = aux_data->thread_sync_mutex;
        bool thread_sync_mutex_initialized = aux_data->thread_sync_mutex_initialized;
        const EmbAllocMemPoolSettings* settings = EMB_ALLOC_GET_MEMPOOL_SETTINGS_PTR (mempool);
        EmbAllocErrorCallback error_callback_fn = settings->error_callback_fn;

        if (thread_sync_mutex_initialized && 
            EmbAllocLockMutex (&thread_sync_mutex)) {
            EmbAllocSetErrorInternal (mempool, kEmbAllocThreadSyncError,
                EMB_ALLOC_MUTEX_LOCK_ERROR, NULL);

        }

        memset (mempool, 0, EmbAllocGetMemoryRequirementsInternal (settings));
        free (mempool);

        if (thread_sync_mutex_initialized && 
            EmbAllocUnlockMutex (&thread_sync_mutex) &&
            (NULL != error_callback_fn)) {
            /** 
             * There will be no more mempool aux data, 
             * so just call the error callback.
             */
            error_callback_fn (kEmbAllocThreadSyncError, EMB_ALLOC_MUTEX_UNLOCK_ERROR);
        }

        if (thread_sync_mutex_initialized && 
            EmbAllocDestroyMutex (&thread_sync_mutex) &&
            (NULL != error_callback_fn)) {
            /** 
             * There will be no more mempool aux data, 
             * so just call the error callback.
             */
            error_callback_fn (kEmbAllocThreadSyncError, EMB_ALLOC_MUTEX_DESTROY_ERROR);
        }

        return true;
    } else {
        /** This is not a mempool, so we cannot send back a more detailed error message. */
        return false;
    }
}

void* EmbAllocMallocInternal (const EmbAllocMemPoolSettings* settings, 
    EmbAllocBlockCategory* categories, EmbAllocMempoolAuxData* aux_data, size_t size)
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

    if (EMB_ALLOC_CAN_ALLOC_IN_A_BLOCK (categories [0], size)) {
        return EmbAllocMallocOneBlockInternal (settings, categories, aux_data, size);
    }

    for (i = EMB_ALLOC_NUM_BLOCK_CATEGORIES - 1; i > 0; i--) {
        if (categories [i].occupied_blocks < categories [i].total_blocks) {
            if (EMB_ALLOC_CAN_ALLOC_IN_A_BLOCK (categories [i], size)) {
                if (categories [i - 1].block_data_size < size) {
                    /** 
                     * Alloc in this category only if this block size is the best fit
                     * (it's the smallest block that fits).
                     */
                    return EmbAllocMallocOneBlockInternal (settings, categories + i, 
                        aux_data, size);
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
                    categories + large_size_block_idx, aux_data, size);
            } else {
                return EmbAllocMallocMultiBlocksInternal(settings, 
                    categories + small_size_block_idx, aux_data, size, 
                    multi_block_alloc_address, multi_block_alloc_count);
            }
    } else if (EMB_ALLOC_NUM_BLOCK_CATEGORIES != large_size_block_idx) {
        return EmbAllocMallocOneBlockInternal (settings, 
            categories + large_size_block_idx, aux_data, size);
    } else if (EMB_ALLOC_NUM_BLOCK_CATEGORIES != small_size_block_idx) {
        return EmbAllocMallocMultiBlocksInternal(settings, 
            categories + small_size_block_idx, aux_data, size, 
            multi_block_alloc_address, multi_block_alloc_count);
    }

    EmbAllocSetErrorInternal (EMB_ALLOC_GET_MEMPOOL_FROM_SETTINGS_PTR (settings), kEmbAllocNoMemory,
        EMB_ALLOC_NOT_ENOUGH_MEMORY_ERROR, NULL);
    return NULL;
}

void EmbAllocMergeFreeBlocksInternal (const EmbAllocMemPoolSettings* settings, 
    EmbAllocBlockCategory* category, EmbAllocMempoolAuxData* aux_data, 
    void* block, size_t blocks_count, bool keep_start, bool keep_end) 
{
    size_t i = 0;
    void* mempool = EMB_ALLOC_GET_MEMPOOL_FROM_SETTINGS_PTR (settings);

    for (i = 0; i < blocks_count; i++) {
        void* current_block = (void*) ((unsigned char*) block + 
            (i * EMB_ALLOC_BLOCK_TOTAL_ALIGN_SIZE (category->block_data_size)));
        size_t* used_block_count = EMB_ALLOC_GET_BLOCK_USE_COUNT_FROM_BLOCK (current_block);
        size_t* data_size = EMB_ALLOC_GET_MEMORY_USE_COUNT_FROM_BLOCK (current_block);
        void* block_end_padding = EMB_ALLOC_GET_END_PADDING_FROM_BLOCK (current_block, 
            category->block_data_size);
        void* data_pointer = EMB_ALLOC_GET_PTR_FROM_BLOCK (current_block);
        
        if (memcmp (current_block, kEmbAllocBlockStart, EMB_ALLOC_ALIGN_AMOUNT)) {
            EmbAllocSetErrorInternal (mempool, kEmbAllocOverflow,
                EMB_ALLOC_OVERFLOW_ERROR, current_block);
        }

        if (memcmp (block_end_padding, kEmbAllocBlockEnd , EMB_ALLOC_ALIGN_AMOUNT)) {
            EmbAllocSetErrorInternal (mempool, kEmbAllocOverflow,
                EMB_ALLOC_OVERFLOW_ERROR, block_end_padding);
        }

        if (EMB_ALLOC_VALUE_NOT_SET != *used_block_count) {
            EmbAllocSetErrorInternal (mempool, kEmbAllocOverflow,
                EMB_ALLOC_OVERFLOW_ERROR, (void*) used_block_count);
        }

        if (EMB_ALLOC_VALUE_NOT_SET != *data_size) {
            EmbAllocSetErrorInternal (mempool, kEmbAllocOverflow,
                EMB_ALLOC_OVERFLOW_ERROR, (void*) data_size);
        }

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
    EmbAllocBlockCategory* category, EmbAllocMempoolAuxData* aux_data, size_t size)
{
    /** 
     * No need to check for the (pointer) param validity inside static functions.
     * Callers should make sure that the params are valid.
     */

    void* free_block = category->first_free_address;
    void* return_value = EMB_ALLOC_GET_PTR_FROM_BLOCK (free_block);
    size_t* used_block_count = EMB_ALLOC_GET_BLOCK_USE_COUNT_FROM_BLOCK (free_block);
    size_t* data_size = EMB_ALLOC_GET_MEMORY_USE_COUNT_FROM_BLOCK (free_block);

    if (category->total_blocks <= category->occupied_blocks) {
        EmbAllocSetErrorInternal (EMB_ALLOC_GET_MEMPOOL_FROM_SETTINGS_PTR (settings), 
            kEmbAllocInconsistentBlocks, EMB_ALLOC_BLOCK_INCONSISTENCY_ERROR, (void*) category);
        return false;
    }

    if ((NULL == category->first_free_address) ||
        (NULL == category->last_free_address)) {
        EmbAllocSetErrorInternal (EMB_ALLOC_GET_MEMPOOL_FROM_SETTINGS_PTR (settings), kEmbAllocInconsistentBlocks,
            EMB_ALLOC_BLOCK_INCONSISTENCY_ERROR, (void*) category);
        category->occupied_blocks = category->total_blocks;
        category->first_free_address = NULL;
        category->last_free_address = NULL;
        return false;
    }

    EmbAllocMergeFreeBlocksInternal (settings, category, aux_data, free_block, 1, true, true);

    if (settings->init_allocated_memory) {
        memset (return_value, 0, size);
    }

    *used_block_count = 1;
    *data_size = size;

    category->occupied_blocks++;

    if (category->occupied_blocks < category->total_blocks) {
        void* initial_free_block = free_block;

        while (free_block <= category->last_free_address) {
            free_block = (void*) ((char*) free_block + 
                EMB_ALLOC_BLOCK_TOTAL_ALIGN_SIZE (category->block_data_size));

            if (EMB_ALLOC_VALUE_NOT_SET == 
                *EMB_ALLOC_GET_BLOCK_USE_COUNT_FROM_BLOCK (free_block)) {
                category->first_free_address = free_block;
                break;
            }
        }

        /** Safety net. This should never be reached. */
        if (category->first_free_address == initial_free_block) {
            category->first_free_address = NULL;
            category->last_free_address = NULL;
        }
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
        category->occupied_blocks = category->total_blocks;
        category->first_free_address = NULL;
        category->last_free_address = NULL;
        return false;
    }

    *blocks_count = (EMB_ALLOC_BLOCK_TOTAL_ALIGN_SIZE (size) / 
        EMB_ALLOC_BLOCK_TOTAL_ALIGN_SIZE (category->block_data_size)) + 
        (( EMB_ALLOC_BLOCK_TOTAL_ALIGN_SIZE (size) %
            EMB_ALLOC_BLOCK_TOTAL_ALIGN_SIZE (category->block_data_size)) ?
            1 : 0);
    *block = NULL;

    if ((category->occupied_blocks + *blocks_count) > category->total_blocks) {
        return false;
    }

    while (verified_block <= category->last_free_address) {
        if (EMB_ALLOC_VALUE_NOT_SET == * EMB_ALLOC_GET_BLOCK_USE_COUNT_FROM_BLOCK (verified_block)) {
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

            if ((   ((size_t) category->last_free_address - (size_t) verified_block) /
                    (size_t) EMB_ALLOC_BLOCK_TOTAL_ALIGN_SIZE (category->block_data_size)) > 
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
    EmbAllocBlockCategory* category, EmbAllocMempoolAuxData* aux_data, 
    size_t size, void* block, size_t blocks_count) 
{
    /** 
     * No need to check for the (pointer) param validity inside static functions.
     * Callers should make sure that the params are valid.
     */

    void* return_value = EMB_ALLOC_GET_PTR_FROM_BLOCK (block);
    size_t* used_block_count = EMB_ALLOC_GET_BLOCK_USE_COUNT_FROM_BLOCK (block);
    size_t* data_size = EMB_ALLOC_GET_MEMORY_USE_COUNT_FROM_BLOCK (block);
    size_t block_data_size = category->block_data_size + 
        (   (blocks_count - 1) * 
            EMB_ALLOC_BLOCK_TOTAL_ALIGN_SIZE (category->block_data_size));
    void* block_end_padding = EMB_ALLOC_GET_END_PADDING_FROM_BLOCK (block, 
        block_data_size);

    if (category->total_blocks <= category->occupied_blocks) {
        EmbAllocSetErrorInternal (EMB_ALLOC_GET_MEMPOOL_FROM_SETTINGS_PTR (settings), 
            kEmbAllocInconsistentBlocks, EMB_ALLOC_BLOCK_INCONSISTENCY_ERROR, (void*) category);
        return false;
    }

    if ((NULL == block) ||
        (NULL == category->first_free_address) ||
        (NULL == category->last_free_address)) {
        EmbAllocSetErrorInternal (EMB_ALLOC_GET_MEMPOOL_FROM_SETTINGS_PTR (settings), 
            kEmbAllocInconsistentBlocks, EMB_ALLOC_BLOCK_INCONSISTENCY_ERROR, (void*) category);    
        category->occupied_blocks = category->total_blocks;
        category->first_free_address = NULL;
        category->last_free_address = NULL;
        return false;
    }
    
    EmbAllocMergeFreeBlocksInternal (settings, category, aux_data, block, blocks_count, true, true);

    if (settings->init_allocated_memory) {
        memset (return_value, 0, size);
    }

    *used_block_count = blocks_count;
    *data_size = size;

    category->occupied_blocks += blocks_count;

    if (category->occupied_blocks < category->total_blocks) {
        if (category->first_free_address == block) {
            void* initial_block = block;
            block = (void*) ((unsigned char*) block + 
                        ((blocks_count - 1) * 
                        EMB_ALLOC_BLOCK_TOTAL_ALIGN_SIZE (category->block_data_size)));

            while (block <= category->last_free_address) {
                block = (void*) ((unsigned char*) block + 
                    EMB_ALLOC_BLOCK_TOTAL_ALIGN_SIZE (category->block_data_size));

                if (EMB_ALLOC_VALUE_NOT_SET == 
                    *EMB_ALLOC_GET_BLOCK_USE_COUNT_FROM_BLOCK (block)) {
                    category->first_free_address = block;
                    break;
                }
            }

            /** Safety net. This should never be reached. */
            if (category->first_free_address == initial_block) {
                category->first_free_address = NULL;
                category->last_free_address = NULL;
            }
        }
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

        ClearMempoolErrorInternal (aux_data);

#ifdef VERBOSE_DUMP_MEMPOOL
        if (strlen (settings->error_dump_file_name)) {
            FILE* error_file = fopen (settings->error_dump_file_name, "a");

            if (NULL != error_file) {
                fprintf(error_file, "\nTrying to allocate %ld bytes", size);

                fflush (error_file);
                fclose (error_file);
            } else {
                perror ("Error writing the error message in the mempool error dump file");
            }
        }
#endif /** VERBOSE_DUMP_MEMPOOL */

        if (size) {
            if (aux_data->thread_sync_mutex_initialized && 
                EmbAllocLockMutex ( &(aux_data->thread_sync_mutex))) {
                EmbAllocSetErrorInternal (mempool, kEmbAllocThreadSyncError,
                    EMB_ALLOC_MUTEX_LOCK_ERROR, NULL);
            }

            return_value = EmbAllocMallocInternal (settings, 
                EMB_ALLOC_GET_MEMPOOL_BLOCK_CATEGORIES_PTR (mempool), 
                aux_data, size);

            if (aux_data->thread_sync_mutex_initialized && 
                EmbAllocUnlockMutex ( &(aux_data->thread_sync_mutex))) {
                EmbAllocSetErrorInternal (mempool, kEmbAllocThreadSyncError,
                    EMB_ALLOC_MUTEX_UNLOCK_ERROR, NULL);
            }
        }

#ifdef VERBOSE_DUMP_MEMPOOL
        if (strlen (settings->error_dump_file_name)) {
            FILE* error_file = fopen (settings->error_dump_file_name, "a");

            if (NULL != error_file) {
                if (NULL != return_value) {
                    size_t memory_offset = (unsigned char*) return_value - (unsigned char*) mempool;

                    fprintf(error_file, "Allocated %ld bytes at the 0x%p location "
                        "/ %ld mempool offset\n", size, return_value,
                        memory_offset);

                    EmbAllocDumpMempoolInternal (mempool,  EmbAllocGetMemoryRequirementsInternal (settings), 
                        error_file, memory_offset);
                } else {
                    fprintf(error_file, "\nFailed to allocate %ld bytes\n", size);
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
    void* block = EMB_ALLOC_GET_BLOCK_FROM_PTR (ptr);
    size_t* used_block_count = EMB_ALLOC_GET_BLOCK_USE_COUNT_FROM_BLOCK (block);
    size_t* data_size = EMB_ALLOC_GET_MEMORY_USE_COUNT_FROM_BLOCK (block);
    void* mempool = EMB_ALLOC_GET_MEMPOOL_FROM_BLOCK_CATEGORIES_PTR (categories);


    if (memcmp (block, kEmbAllocBlockStart, EMB_ALLOC_ALIGN_AMOUNT)) {
        EmbAllocSetErrorInternal (mempool, kEmbAllocOverflow,
            EMB_ALLOC_OVERFLOW_ERROR, block);
        memcpy (block, kEmbAllocBlockStart, EMB_ALLOC_ALIGN_AMOUNT);
    }

    if (EMB_ALLOC_VALUE_NOT_SET == *used_block_count) {
        *data_size = EMB_ALLOC_VALUE_NOT_SET;
        EmbAllocSetErrorInternal (mempool, kEmbAllocOverflow,
            EMB_ALLOC_OVERFLOW_ERROR, (void*) used_block_count);
        return NULL;
    }

    if (EMB_ALLOC_VALUE_NOT_SET == *data_size) {
        *used_block_count = EMB_ALLOC_VALUE_NOT_SET;
        EmbAllocSetErrorInternal (mempool, kEmbAllocOverflow,
            EMB_ALLOC_OVERFLOW_ERROR, (void*) data_size);
        return NULL;
    }

    for (i = 0; i < EMB_ALLOC_NUM_BLOCK_CATEGORIES; i++) {
        if ((categories [i].start_address <= block) &&
            (categories [i].last_address >= block)) {
            size_t block_data_size = (categories + i)->block_data_size + 
                (   (*used_block_count - 1) * 
                    EMB_ALLOC_BLOCK_TOTAL_ALIGN_SIZE ((categories + i)->block_data_size));
            void* block_end_padding = EMB_ALLOC_GET_END_PADDING_FROM_BLOCK (block, 
                block_data_size);

            if (memcmp (block_end_padding, kEmbAllocBlockEnd , EMB_ALLOC_ALIGN_AMOUNT)) {
                EmbAllocSetErrorInternal (mempool, kEmbAllocOverflow,
                    EMB_ALLOC_OVERFLOW_ERROR, block_end_padding);
                memcpy (block_end_padding, kEmbAllocBlockEnd, EMB_ALLOC_ALIGN_AMOUNT);
            }
            
            return categories + i;
        }
    }

    return NULL;
}

void EmbAllocFreeInternal (const EmbAllocMemPoolSettings* settings, 
    EmbAllocBlockCategory* categories, EmbAllocMempoolAuxData* aux_data, void* ptr)
{
    /** 
     * No need to check for the (pointer) param validity inside static functions.
     * Callers should make sure that the params are valid.
     */

    EmbAllocBlockCategory* category = EmbAllocGetCategoryForPtr (categories, ptr);

    if (NULL != category) {
        EmbAllocFreeBlockInternal (settings, category, aux_data, ptr);
    } else {
        EmbAllocSetErrorInternal (EMB_ALLOC_GET_MEMPOOL_FROM_SETTINGS_PTR (settings), 
            kEmbAllocPointerParamError, EMB_ALLOC_INVALID_POINTER_PARAM_ERROR, NULL);
    }
}

void EmbAllocFreeBlockInternal (const EmbAllocMemPoolSettings* settings, 
    EmbAllocBlockCategory* category, EmbAllocMempoolAuxData* aux_data, 
    void* ptr)
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
    

    if (settings->full_overflow_checks &&
        !EmbAllocCheckBuffer (
                (void*) ((unsigned char*) ptr + data_size),
                block_data_size - data_size, 
                EMB_ALLOC_INIT_VALUE)) {
        EmbAllocSetErrorInternal (EMB_ALLOC_GET_MEMPOOL_FROM_SETTINGS_PTR (settings), 
            kEmbAllocOverflow, EMB_ALLOC_OVERFLOW_ERROR, 
            (void*) ((unsigned char*) ptr + data_size));
    }

    memset (ptr, EMB_ALLOC_INIT_VALUE, block_data_size);

    /**
     * Restore the block control data to an "unitialized" value for each occupied block.
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

    category->occupied_blocks -= used_block_count;

    if ((NULL == category->first_free_address) || 
        (category->first_free_address > block)) {
       category->first_free_address = block; 
    } 
    
    if ((NULL == category->last_free_address) ||
        (category->last_free_address < block)) {
       category->last_free_address = block; 
    } 
}

void EmbAllocFree (EmbAllocMempool mempool, void* ptr)
{
    if (EMB_ALLOC_PTR_IS_MEMPOOL (mempool, kEmbAllocMempoolStart)) {
        EmbAllocMempoolAuxData* aux_data = EMB_ALLOC_GET_MEMPOOL_AUX_DATA_PTR (mempool);
        const EmbAllocMemPoolSettings* settings = EMB_ALLOC_GET_MEMPOOL_SETTINGS_PTR (mempool);
        bool valid_pointer_param = false;

        ClearMempoolErrorInternal (aux_data);

#ifdef VERBOSE_DUMP_MEMPOOL
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
            if (aux_data->thread_sync_mutex_initialized && 
                EmbAllocLockMutex ( &(aux_data->thread_sync_mutex))) {
                EmbAllocSetErrorInternal (mempool, kEmbAllocThreadSyncError,
                    EMB_ALLOC_MUTEX_LOCK_ERROR, NULL);
            }

            if (EMB_ALLOC_PTR_IS_BLOCK (ptr, kEmbAllocBlockStart)) {
                valid_pointer_param = true;
                EmbAllocFreeInternal (settings, 
                    EMB_ALLOC_GET_MEMPOOL_BLOCK_CATEGORIES_PTR (mempool), 
                    aux_data,
                    ptr);
            } else {
                EmbAllocSetErrorInternal (mempool, kEmbAllocPointerParamError, 
                    EMB_ALLOC_INVALID_POINTER_PARAM_ERROR, NULL);
            }

            if (aux_data->thread_sync_mutex_initialized && 
                EmbAllocUnlockMutex ( &(aux_data->thread_sync_mutex))) {
                EmbAllocSetErrorInternal (mempool, kEmbAllocThreadSyncError,
                    EMB_ALLOC_MUTEX_UNLOCK_ERROR, NULL);
            }
        }

#ifdef VERBOSE_DUMP_MEMPOOL
        if (strlen (settings->error_dump_file_name)) {
            FILE* error_file = fopen (settings->error_dump_file_name, "a");

            if (NULL != error_file) {
                if (valid_pointer_param) {
                    size_t memory_offset = (unsigned char*) ptr - (unsigned char*) mempool;

                    fprintf(error_file, "Freed bytes at the 0x%p location "
                        "/ %ld mempool offset\n", ptr,
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
    EmbAllocBlockCategory* categories, EmbAllocMempoolAuxData* aux_data, 
    void* ptr, size_t size)
{
    /** 
     * No need to check for the (pointer) param validity inside static functions.
     * Callers should make sure that the params are valid.
     */

    EmbAllocBlockCategory* category = EmbAllocGetCategoryForPtr (categories, ptr);

    if (NULL != category) {
        return EmbAllocReallocBlockInternal (settings, category, categories, aux_data, ptr, size);
    } else {
        EmbAllocSetErrorInternal (EMB_ALLOC_GET_MEMPOOL_FROM_SETTINGS_PTR (settings), 
            kEmbAllocPointerParamError, EMB_ALLOC_INVALID_POINTER_PARAM_ERROR, NULL);
    }

    return NULL;
}


void* EmbAllocReallocBlockInternal (const EmbAllocMemPoolSettings* settings, 
    EmbAllocBlockCategory* category, EmbAllocBlockCategory* categories, 
    EmbAllocMempoolAuxData* aux_data, void* ptr, size_t size)
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
    }if (size < *data_size) {
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
        if (size < block_data_size) {
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
            size_t required_extra_blocks = 
                (size - block_data_size) / EMB_ALLOC_BLOCK_TOTAL_ALIGN_SIZE (category->block_data_size) + 
                (((size - block_data_size) % EMB_ALLOC_BLOCK_TOTAL_ALIGN_SIZE (category->block_data_size)) ?
                1 : 0);

            /**
             * Try to realloc in the same category only if the new extra size 
             * fits in the number of free blocks from this category. 
             */
            if (required_extra_blocks <= (category->total_blocks - category->occupied_blocks)) {
                size_t i = 0;
                bool can_realloc_continously = true;

                /**
                 * It is not sufficient to have the required number of free blocks,
                 * they need to be continous as well. 
                 */
                for (i = 0; i < required_extra_blocks; i++) {
                    if (EMB_ALLOC_VALUE_NOT_SET != 
                        *EMB_ALLOC_GET_BLOCK_USE_COUNT_FROM_BLOCK (
                            ((unsigned char*) block + 
                            (   *used_block_count + i) * 
                                EMB_ALLOC_BLOCK_TOTAL_ALIGN_SIZE (category->block_data_size))   
                        )) {
                        can_realloc_continously = false;
                        break;
                    }
                }

                if (can_realloc_continously) {
                    void* block_end_padding = EMB_ALLOC_GET_END_PADDING_FROM_BLOCK (block, 
                        block_data_size);

                    EmbAllocMergeFreeBlocksInternal (settings, category, aux_data, 
                        (void*)((unsigned char*) block + 
                            (   *used_block_count * 
                                EMB_ALLOC_BLOCK_TOTAL_ALIGN_SIZE (category->block_data_size))), 
                            required_extra_blocks, false, true);

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
                    }

                    return ptr;
                }
            }

            /**
             * Malloc, copy and free only if the memory reallocation could not be done
             * inside the same category continously.
             */
            return_value = EmbAllocMallocInternal (settings, categories, aux_data, size);

            if (NULL != return_value) {
                memcpy (return_value, ptr, *data_size);
            }

            EmbAllocFreeBlockInternal (settings, category, aux_data, ptr);

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

        ClearMempoolErrorInternal (aux_data);

#ifdef VERBOSE_DUMP_MEMPOOL
        if (strlen (settings->error_dump_file_name)) {
            size_t memory_offset = EMB_ALLOC_VALUE_NOT_SET;
            FILE* error_file = fopen (settings->error_dump_file_name, "a");

            if (NULL != error_file) {
                if (NULL != ptr) {
                    memory_offset = (unsigned char*) ptr - (unsigned char*) mempool;
                }

                fprintf(error_file, "\nTrying to reallocate %ld bytes from the 0x%p location "
                    "/ %ld mempool offset\n", size, ptr, memory_offset);

                fflush (error_file);
                fclose (error_file);
            } else {
                perror ("Error writing the error message in the mempool error dump file");
            }
        }
#endif /** VERBOSE_DUMP_MEMPOOL */

        if (ptr || size) {
            if (aux_data->thread_sync_mutex_initialized && 
                EmbAllocLockMutex ( &(aux_data->thread_sync_mutex))) {
                EmbAllocSetErrorInternal (mempool, kEmbAllocThreadSyncError,
                    EMB_ALLOC_MUTEX_LOCK_ERROR, NULL);
            }

            if (NULL == ptr) {
                if (size) {
                    /**
                     * Perform a simple malloc if there's no initial pointer.
                     */
                    return_value = EmbAllocMallocInternal (settings, 
                        EMB_ALLOC_GET_MEMPOOL_BLOCK_CATEGORIES_PTR (mempool), 
                        aux_data, size);
                }
            } else if (EMB_ALLOC_PTR_IS_BLOCK (ptr, kEmbAllocBlockStart)) {
                if (0 == size) {
                    /**
                     * Free the valid pointer if the new size is 0.
                     */
                    EmbAllocFreeInternal (settings, 
                        EMB_ALLOC_GET_MEMPOOL_BLOCK_CATEGORIES_PTR (mempool), 
                        aux_data,
                        ptr);
                } else {
                    /**
                     * Realloc otherwise. 
                     */
                    return_value = EmbAllocReallocInternal (settings, 
                        EMB_ALLOC_GET_MEMPOOL_BLOCK_CATEGORIES_PTR (mempool), 
                        aux_data,
                        ptr, size);
                }

            } else {
                EmbAllocSetErrorInternal (mempool, kEmbAllocPointerParamError, 
                    EMB_ALLOC_INVALID_POINTER_PARAM_ERROR, NULL);
            }

            if (aux_data->thread_sync_mutex_initialized && 
                EmbAllocUnlockMutex ( &(aux_data->thread_sync_mutex))) {
                EmbAllocSetErrorInternal (mempool, kEmbAllocThreadSyncError,
                    EMB_ALLOC_MUTEX_UNLOCK_ERROR, NULL);
            }
        }

#ifdef VERBOSE_DUMP_MEMPOOL
        if (strlen (settings->error_dump_file_name)) {
            FILE* error_file = fopen (settings->error_dump_file_name, "a");

            if (NULL != error_file) {
                size_t initial_memory_offset = EMB_ALLOC_VALUE_NOT_SET;

                if (NULL != ptr) {
                    initial_memory_offset = (unsigned char*) ptr - (unsigned char*) mempool;
                }

                if (NULL != return_value) {
                    size_t final_memory_offset = (unsigned char*) return_value - (unsigned char*) mempool;

                    fprintf(error_file, "Reallocated %ld bytes from the 0x%p location "
                        "/ %ld mempool offset to the 0x%p location "
                        "/ %ld mempool offset\n", size, ptr, initial_memory_offset, 
                        return_value, final_memory_offset);

                    EmbAllocDumpMempoolInternal (mempool,  EmbAllocGetMemoryRequirementsInternal (settings), 
                        error_file, final_memory_offset);
                } else {
                    fprintf(error_file, "\nFailed to reallocate %ld bytes from the 0x%p location "
                        "/ %ld mempool offset\n", size, ptr, initial_memory_offset);
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
            if (aux_data->thread_sync_mutex_initialized && 
                EmbAllocLockMutex ( &(aux_data->thread_sync_mutex))) {
                EmbAllocSetErrorInternal (mempool, kEmbAllocThreadSyncError,
                    EMB_ALLOC_MUTEX_LOCK_ERROR, NULL);
            }

            EmbAllocSetErrorInternal (mempool, 
                kEmbAllocOutputParamError, EMB_ALLOC_INVALID_OUTPUT_PARAM_ERROR, NULL);

            if (aux_data->thread_sync_mutex_initialized && 
                EmbAllocUnlockMutex ( &(aux_data->thread_sync_mutex))) {
                EmbAllocSetErrorInternal (mempool, kEmbAllocThreadSyncError,
                    EMB_ALLOC_MUTEX_UNLOCK_ERROR, NULL);
            }

            return false;
        }
    } else {
        /** This is not a mempool, so we cannot send back a more detailed error message. */
        return false;
    }
}

EmbAllocErrors EmbAllocGetLastErrorCode (EmbAllocMempool mempool) 
{
    if (EMB_ALLOC_PTR_IS_MEMPOOL (mempool, kEmbAllocMempoolStart)) {
        /** This is a static location, so there's no point to sync this. */
        return (EMB_ALLOC_GET_MEMPOOL_AUX_DATA_PTR (mempool))->last_error;
    } else {
        return kEmbAllocInvalidMempool;
    }
}

const char* EmbAllocGetLastErrorMessage (EmbAllocMempool mempool)
{
    if (EMB_ALLOC_PTR_IS_MEMPOOL (mempool, kEmbAllocMempoolStart)) {
        /** This is a static location, so there's no point to sync this. */
        return (EMB_ALLOC_GET_MEMPOOL_AUX_DATA_PTR (mempool))->last_error_message;
    } else {
        return EMB_ALLOC_NOT_A_MEMPOOL_ERROR;
    }
}