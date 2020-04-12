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

#ifndef __EMB_ALLOC_H__
#define __EMB_ALLOC_H__

/** size_t, typedef, *printf, file and NULL declaration */
#include <stdio.h>
/** SIZE_MAX declaration */
#include <stdint.h>

#ifndef __cplusplus
    /*bool type declarations for C */
    #include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** Define for uninitialized integer value. */
#define EMB_ALLOC_VALUE_NOT_SET SIZE_MAX

/** The placeholder size for the mempool dump file name. */
#define EMB_ALLOC_ERROR_DUMP_FILE_NAME_SIZE 128

/** EmbAlloc errors enum. */
typedef enum
{
    /** Everything is ok. */
    kEmbAllocNoErr,
    /** The total size of all software blocks is different than the mempool size. */
    kEmbAllocInconsistentSettings,
    /** A thread sync operation failed */
    kEmbAllocThreadSyncError,
    /** An output parameter is not valid. */
    kEmbAllocOutputParamError,
    /** The mempool pointer does not point to a valid mempool. */
    kEmbAllocInvalidMempool,
    /** Could not allocate memory. */
    kEmbAllocNoMemory, 
    /** Memory overflow detected. */
    kEmbAllocOverflow,
    /** Inconsistent mempool blocks detected. */
    kEmbAllocInconsistentBlocks,
    /** Apointer parameter is not valid. */
    kEmbAllocPointerParamError
} EmbAllocErrors;

/**
 * Error callback function pointer.
 * Used by the mempool owner to receive error data.
 * It is a method to provide a C alternative to C++ exceptions.
 */
typedef void (*EmbAllocErrorCallback) (EmbAllocErrors error_code, const char* error_message);

/** EmbAlloc initialization settings. */
typedef struct
{
    /** 
     * The usable mempool size (in bytes).
     * @note the total allocated memory is higher due to internal padding. 
     */
    size_t total_size;
    /** The number of blocks that have an usable size of 32 bytes. */
    size_t num_32_bytes_blocks;
    /** The number of blocks that have an usable size of 64 bytes. */
    size_t num_64_bytes_blocks;
    /** The number of blocks that have an usable size of 128 bytes. */
    size_t num_128_bytes_blocks;
    /** The number of blocks that have an usable size of 256 bytes. */
    size_t num_256_bytes_blocks;
    /** The number of blocks that have an usable size of 512 bytes. */
    size_t num_512_bytes_blocks;
    /** The number of blocks that have an usable size of 1 kB. */
    size_t num_1k_bytes_blocks;
    /** The number of blocks that have an usable size of 2 kB. */
    size_t num_2k_bytes_blocks;
    /** The number of blocks that have an usable size of 4 kB. */
    size_t num_4k_bytes_blocks;
    /** The callback function pointer that will receive error notifications. */
    EmbAllocErrorCallback error_callback_fn;
    /** Can be shared between threads without any issues. */
    /** 
     * @note If this flag is set, it is recommended to also 
     *       define USE_WIN_CRITICAL_SECTION if compiling for Windows OS.
     * Not having this define, will use mutexes, which are quite slow
     * when compared to critical sections. 
     */
    bool threadsafe;
    /** 
     * Check ALL the block data (at allocation/deallocation) 
     * to detect if an overflow occured.
     */
    bool full_overflow_checks;
    /**
     * Initialize all the allocated memory to 0.
     */
    bool init_allocated_memory;
    /**
     * The file name of the mempool dump file (in case of error).
     */
    char error_dump_file_name [EMB_ALLOC_ERROR_DUMP_FILE_NAME_SIZE];
} EmbAllocMemPoolSettings;

/**
 * Mempool declaration.
 * The implementation is hidden from the user behind a void* pointer.
 */
typedef void* EmbAllocMempool;

/**
 * Creates a new mempool.
 * @note Use error_callback_fn for extra details in case of error.
 * @param settings mempool size and block distribution, error behaviour
 *                  and errors callback function pointer.
 * @return a pointer to a newly allocated mempool. NULL in case of error.
 */
EmbAllocMempool EmbAllocCreate (const EmbAllocMemPoolSettings* settings);

/**
 * Destroys an existing mempool.
 * @note Use error_callback_fn for extra details in case of error.
 * @param mempool allocated memory to be destroyed.
 * @return true if the mempool has been destroyed, false otherwise.
 */
bool EmbAllocDestroy (EmbAllocMempool mempool);


/**
 * Allocates size bytes of uninitialized storage.
 * @note Use error_callback_fn for extra details in case of error.
 * @param mempool the chuck that holds all pre-allocated memory.
 * @param size number of bytes to br allocated.
 * @return the pointer to the beginning of newly allocated memory on success,
 *         NULL otherwise.
 * @warning The allocation function can only alloc within the same 
 *          category of memory blocks. If the mempool has memory blocks of 2 sizes
 *          (e.g. 128 and 256 bytes) and the required size does not fit into 
 *          continous block of one category, then the allocation will fail even if 
 *          the required size fits into the total ammount of free memory.
 * @see EmbAllocMemPoolSettings for more details on the memory blocks sizes.
 */
void* EmbAllocMalloc (EmbAllocMempool mempool, size_t size);

/**
 * Deallocates the space previously allocated by EmbAllocMalloc or EmbAllocRealloc.
 * If ptr is a null pointer, the function does nothing.
 * @note Use error_callback_fn for extra details in case of error.
 * @param mempool the chuck that holds all pre-allocated memory.
 * @param ptr pointer to the memory to deallocate
 */
void EmbAllocFree (EmbAllocMempool mempool, void* ptr);

/**
 * Reallocates the given area of memory.
 * It must be previously allocated by EmbAllocMalloc() or EmbAllocRealloc() and
 * not yet freed with a call to EmbAllocFree() or EmbAllocMalloc().
 * If ptr is NULL, the behavior is the same as
 * calling EmbAllocMalloc (mempool, size).
 * @note Use error_callback_fn for extra details in case of error.
 * @param mempool the chuck that holds all pre-allocated memory.
 * @param ptr pointer to the memory area to be reallocated.
 * @param size number of bytes to reallocated.
 * @return the pointer to the beginning of newly allocated memory on success,
 *         NULL otherwise.
 * @warning The allocation function can only alloc within the same 
 *          category of memory blocks. If the mempool has memory blocks of 2 sizes
 *          (e.g. 128 and 256 bytes) and the required size does not fit into 
 *          continous block of one category, then the allocation will fail even if 
 *          the required size fits into the total ammount of free memory.
 * @see EmbAllocMemPoolSettings for more details on the memory blocks sizes.
 */
void* EmbAllocRealloc (EmbAllocMempool mempool, void* ptr, size_t size);

/**
 * Retrieves the actual setting that were used to create the mempool.
 * If the initial creation settings are inconsistent
 * and these errors are bypassed, then the actual settings might differ
 * from the initial ones.
 * @note Use error_callback_fn for extra details in case of error.
 * @param mempool the chuck that holds all pre-allocated memory.
 * @param settings the output param that will hold the current settings.
 *                 @ote It should point to a valid memory address.
 * @return true if the settings could be retrieved, false otherwise.
 */
bool EmbAllocGetSettings (const EmbAllocMempool mempool, EmbAllocMemPoolSettings* settings);

/**
 * Retrieves the error code set in the last function call
 * (kEmbAllocNoErr if the last call succeeded).
 * @param mempool the chuck that holds all pre-allocated memory.
 * @return a value from the EmbAllocErrors enum.
 */
EmbAllocErrors EmbAllocGetLastErrorCode (EmbAllocMempool mempool);

/**
 * Retrieves the error string set in the last function call
 * (an empty string if the last call succeeded).
 * @param mempool the chuck that holds all pre-allocated memory.
 * @return a null terminated string.
 */
const char* EmbAllocGetLastErrorMessage (EmbAllocMempool mempool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __EMB_ALLOC_H__ */