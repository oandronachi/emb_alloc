/** 
 * Performance Benchmark for the Embedded Memory Allocator
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

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <stdlib.h>
#include <vector>

#include "emb_alloc.h"
#include "emb_alloc_performance_benchmark.h"

#ifdef RUN_WOF_ALLOCATOR_COMPARISON
    #include "eapache/wof_alloc/wof_allocator.h"
#endif /** RUN_WOF_ALLOCATOR_COMPARISON */

#ifdef RUN_KULMINAATOR_ALLOCATOR_COMPARISON
    #include "kulminaator/mempool/mempool.h"
#endif /** RUN_KULMINAATOR_ALLOCATOR_COMPARISON */

#ifdef RUN_SOFTING_INDUSTRIAL_ALLOCATOR_COMPARISON
    #include "SoftingIndustrial/MemPool/mempool.h"
#endif /** RUN_SOFTING_INDUSTRIAL_ALLOCATOR_COMPARISON */

#ifdef RUN_C_MEMPOOL_ALLOCATOR_COMPARISON
    #include "CMemoryPool/CMemoryPool.h"
#endif /** RUN_C_MEMPOOL_ALLOCATOR_COMPARISON */

namespace {
    std::vector <size_t> GetAllocParameters ();
    void EmbAllocPrintErrorInternal (EmbAllocErrors error_code, const char* error_message);

    void EmbAllocRunPerformanceBenchmarkInternal (const EmbAllocMemPoolSettings& mempool_settings, std::vector <size_t> memory_blocks_sizes);
    void libcRunPerformanceBenchmarkInternal (std::vector <size_t> memory_blocks_sizes);

    #ifdef RUN_WOF_ALLOCATOR_COMPARISON
        static void WofAllocRunPerformanceBenchmarkInternal (std::vector <size_t> memory_blocks_sizes);
    #endif /** RUN_WOF_ALLOCATOR_COMPARISON */

    #ifdef RUN_KULMINAATOR_ALLOCATOR_COMPARISON
        static void KluminaatorMempoolRunPerformanceBenchmarkInternal (std::vector <size_t> memory_blocks_sizes);
    #endif /** RUN_KULMINAATOR_ALLOCATOR_COMPARISON */

    #ifdef RUN_SOFTING_INDUSTRIAL_ALLOCATOR_COMPARISON
        static void SoftingIndustrialMemPoolRunPerformanceBenchmarkInternal (std::vector <size_t> memory_blocks_sizes);
    #endif /** RUN_SOFTING_INDUSTRIAL_ALLOCATOR_COMPARISON */

    #ifdef RUN_C_MEMPOOL_ALLOCATOR_COMPARISON
        static void CMemPoolRunPerformanceBenchmarkInternal (std::vector <size_t> memory_blocks_sizes);
    #endif /** RUN_C_MEMPOOL_ALLOCATOR_COMPARISON */
}

/** These defines below are related to the way the mempool is initialized. */
#define B32_ALLOCATION_COUNT 65536 / 2
#define B64_ALLOCATION_COUNT 131072 / 2
#define B256_ALLOCATION_COUNT 16384 / 2

//(2097152 + 8388608 + 4194304)
//32 * B32_ALLOCATION_COUNT + 64 * B64_ALLOCATION_COUNT + 256 * B256_ALLOCATION_COUNT
#define BASE_MEMPOOL_SIZE 14680064 / 2

/** 4MB initial memory allocation * SCALE_MULTIPLIER */
#define SCALE_MULTIPLIER 4

#define BLOCK_ALLOC_MIN_MEMORY_SIZE 51
#define BLOCK_ALLOC_MAX_MEMORY_SIZE 64
#define ALLOCATION_COUNT B64_ALLOCATION_COUNT

void EmbAllocRunPerformanceBenchmark ()
{
    EmbAllocMemPoolSettings mempool_settings;
    memset (&mempool_settings, 0, sizeof (mempool_settings));
    mempool_settings.total_size = SCALE_MULTIPLIER * BASE_MEMPOOL_SIZE;
    mempool_settings.num_32_bytes_blocks = SCALE_MULTIPLIER * B32_ALLOCATION_COUNT;
    mempool_settings.num_64_bytes_blocks = SCALE_MULTIPLIER * B64_ALLOCATION_COUNT;
    mempool_settings.num_256_bytes_blocks = SCALE_MULTIPLIER * B256_ALLOCATION_COUNT;
    mempool_settings.init_allocated_memory = true;
    mempool_settings.full_overflow_checks = true;
    /** 
     * mempool_settings.error_callback_fn = EmbAllocPrintErrorInternal;
     * strcpy (mempool_settings.error_dump_file_name, "C:\\Work\\emb_alloc_dump.txt"); 
     */
    mempool_settings.threadsafe = true;
    std::vector <size_t> memory_blocks_sizes = GetAllocParameters ();

#ifdef RUN_C_MEMPOOL_ALLOCATOR_COMPARISON
    std::cout << std::endl << "Alternative mempool(CMempool) implementation" << std::endl;
    CMemPoolRunPerformanceBenchmarkInternal (memory_blocks_sizes);
#endif /** RUN_C_MEMPOOL_ALLOCATOR_COMPARISON */

#ifdef RUN_SOFTING_INDUSTRIAL_ALLOCATOR_COMPARISON
    std::cout << std::endl << "Alternative mempool(SoftingIndustrial/MemPool) implementation" << std::endl;
    SoftingIndustrialMemPoolRunPerformanceBenchmarkInternal (memory_blocks_sizes);
#endif /** RUN_SOFTING_INDUSTRIAL_ALLOCATOR_COMPARISON */

#ifdef RUN_WOF_ALLOCATOR_COMPARISON
    std::cout << std::endl << "Alternative mempool(eapache/wof_alloc) implementation" << std::endl;
    WofAllocRunPerformanceBenchmarkInternal (memory_blocks_sizes);
#endif /** RUN_WOF_ALLOCATOR_COMPARISON */

#ifdef RUN_KULMINAATOR_ALLOCATOR_COMPARISON
    std::cout << std::endl << "Alternative mempool(kluminaator/mempool) implementation" << std::endl;
    KluminaatorMempoolRunPerformanceBenchmarkInternal (memory_blocks_sizes);
#endif /** RUN_KULMINAATOR_ALLOCATOR_COMPARISON */

    std::cout << std::endl << "No mempool (libc)" << std::endl;
    libcRunPerformanceBenchmarkInternal (memory_blocks_sizes);

    mempool_settings.init_allocated_memory = false;
    mempool_settings.full_overflow_checks = false;
    mempool_settings.threadsafe = false;

    std::cout << std::endl << "Full safety disabled" << std::endl;
    EmbAllocRunPerformanceBenchmarkInternal (mempool_settings, memory_blocks_sizes);

    mempool_settings.init_allocated_memory = true;
    mempool_settings.full_overflow_checks = false;
    mempool_settings.threadsafe = false;

    std::cout << std::endl << "Partial safety enabled(init_allocated_memory)" << std::endl;
    EmbAllocRunPerformanceBenchmarkInternal (mempool_settings, memory_blocks_sizes);

    mempool_settings.init_allocated_memory = false;
    mempool_settings.full_overflow_checks = true;
    mempool_settings.threadsafe = false;

    std::cout << std::endl << "Partial safety enabled(full_overflow_checks)" << std::endl;
    EmbAllocRunPerformanceBenchmarkInternal (mempool_settings, memory_blocks_sizes);

    mempool_settings.init_allocated_memory = true;
    mempool_settings.full_overflow_checks = true;
    mempool_settings.threadsafe = false;

    std::cout << std::endl << "Partial safety enabled(init_allocated_memory & full_overflow_checks)" << std::endl;
    EmbAllocRunPerformanceBenchmarkInternal (mempool_settings, memory_blocks_sizes);

    mempool_settings.init_allocated_memory = false;
    mempool_settings.full_overflow_checks = false;
    mempool_settings.threadsafe = true;

    std::cout << std::endl << "Partial safety enabled(threadsafe)" << std::endl;
    EmbAllocRunPerformanceBenchmarkInternal (mempool_settings, memory_blocks_sizes);

    mempool_settings.init_allocated_memory = true;
    mempool_settings.full_overflow_checks = true;
    mempool_settings.threadsafe = true;

    std::cout << std::endl << "Full safety enabled" << std::endl;
    EmbAllocRunPerformanceBenchmarkInternal (mempool_settings, memory_blocks_sizes);
}

namespace {
    std::vector <size_t> GetAllocParameters ()
    {
        std::vector <size_t> return_value;
        return_value.reserve (SCALE_MULTIPLIER * ALLOCATION_COUNT);

        std::srand(std::time(nullptr));

        for (size_t i = 0; i < SCALE_MULTIPLIER * ALLOCATION_COUNT; i++) {
            size_t value = BLOCK_ALLOC_MAX_MEMORY_SIZE + 1;

            while (value > BLOCK_ALLOC_MAX_MEMORY_SIZE) {
                value = BLOCK_ALLOC_MIN_MEMORY_SIZE + std::rand()/((RAND_MAX + 1u)/BLOCK_ALLOC_MAX_MEMORY_SIZE);  
                /**
                 * BLOCK_ALLOC_MIN_MEMORY_SIZE + rand() % (BLOCK_ALLOC_MAX_MEMORY_SIZE - BLOCK_ALLOC_MIN_MEMORY_SIZE) is biased
                 */
            }

            return_value.push_back (value);
    }

        return return_value;
    }

    void EmbAllocPrintErrorInternal (EmbAllocErrors error_code, const char* error_message)
    {
        std::cout << "ERROR: " << (int)error_code << "!!!" << std::endl << error_message <<std::endl;
    }

    void EmbAllocRunPerformanceBenchmarkInternal (const EmbAllocMemPoolSettings& mempool_settings, std::vector <size_t> memory_blocks_sizes)
    {
        std::cout << "Starting the mempool creation." << std::endl;     
        auto t_start = std::chrono::high_resolution_clock::now ();

        EmbAllocMempool mempool = EmbAllocCreate (&mempool_settings);

        if (NULL != mempool) {
            std::cout << "Successfully created the mempool" << std::endl;
        } else {
            std::cout << "Could not create the mempool" << std::endl;
            return;
        }

        auto t_end = std::chrono::high_resolution_clock::now ();
        std::cout << "Operation took " << std::chrono::duration<double, std::milli>(t_end-t_start).count () << " ms" <<std::endl;

        {
            std::cout << "Starting the memory allocation." << std::endl;     
            t_start = std::chrono::high_resolution_clock::now ();

            std::vector <void*> allocations;
            allocations.reserve (memory_blocks_sizes.size ());

            for (int i = 0; i < memory_blocks_sizes.size (); i++) {
                allocations.push_back (NULL);
            }

            for (size_t i = 0; i < memory_blocks_sizes.size () / 2; i++) {
                allocations [i] = EmbAllocMalloc (mempool, memory_blocks_sizes [i] * 2);

                if (NULL == allocations [i]) {
                    std::cout << "Could not allocate the large memory at index " << i << std::endl;
                }
            }

            for (size_t i = 0; i < memory_blocks_sizes.size () / 2; i ++) {
                EmbAllocFree (mempool, allocations [i]);
                allocations [i] = NULL;
            }

            for (size_t i = 0; i < memory_blocks_sizes.size (); i++) {
                allocations [i] = EmbAllocMalloc (mempool, memory_blocks_sizes [i]);

                if (NULL == allocations [i]) {
                    std::cout << "Could not allocate the memory at index " << i << std::endl;
                }
            }

            for (size_t i = 1; i < memory_blocks_sizes.size (); i += 2) {
                EmbAllocFree (mempool, allocations [i]);
                allocations [i] = NULL;
            }

            for (size_t i = 0; i < memory_blocks_sizes.size () / 4; i += 2) {
                allocations [i] = EmbAllocRealloc (mempool, allocations [i], memory_blocks_sizes [i] / 2);

                if (NULL == allocations [i]) {
                    std::cout << "Could not reallocate the (smaller) memory at index " << i << std::endl;
                }
            }

            {
                size_t i  = 0;

                for (i = memory_blocks_sizes.size () - 1; i > 3 * memory_blocks_sizes.size () / 4; i--) {
                    if (allocations [i]) {
                        break;
                    }
                }

                for (; i >= 3 * memory_blocks_sizes.size (); i -= 2) {
                    allocations [i] = EmbAllocRealloc (mempool, allocations [i], memory_blocks_sizes [i] * 3);

                    if (NULL == allocations [i]) {
                        std::cout << "Could not reallocate the (previous block larger) memory at index " << i << std::endl;
                    }
                }
            }

            for (size_t i = memory_blocks_sizes.size () / 4; i < memory_blocks_sizes.size () / 2; i += 2) {
                allocations [i] = EmbAllocRealloc (mempool, allocations [i], memory_blocks_sizes [i] * 2);

                if (NULL == allocations [i]) {
                    std::cout << "Could not reallocate the (larger) memory at index " << i << std::endl;
                }
            }

            {
                size_t i  = 0;

                for (i = 3 * memory_blocks_sizes.size () / 4 - 1; i > memory_blocks_sizes.size () / 2; i--) {
                    if (allocations [i]) {
                        break;
                    }
                }

                for (; i >= memory_blocks_sizes.size () / 2; i -= 2) {
                    allocations [i] = EmbAllocRealloc (mempool, allocations [i], memory_blocks_sizes [i] * 3);

                    if (NULL == allocations [i]) {
                        std::cout << "Could not reallocate the (next block larger) memory at index " << i << std::endl;
                    }
                }
            }

            for (size_t i = 0; i < memory_blocks_sizes.size (); i ++) {
                if (allocations [i]) {
                    EmbAllocFree (mempool, allocations [i]);
                    allocations [i] = NULL;
                }
            }

            for (size_t i = 0; i < memory_blocks_sizes.size (); i++) {
                allocations [i] = EmbAllocMalloc (mempool, memory_blocks_sizes [i]);

                if (NULL == allocations [i]) {
                    std::cout << "Could not allocate the (final) memory at index " << i << std::endl;
                }
            }

            std::srand(std::time(nullptr));

            for (size_t i = 0; i < memory_blocks_sizes.size (); i++) {
                size_t index = std::rand() % memory_blocks_sizes.size ();
                EmbAllocFree (mempool, allocations [index]);
                allocations [index] = EmbAllocMalloc (mempool, memory_blocks_sizes [i]);

                if (NULL == allocations [index]) {
                    std::cout << "Could not allocate the (random) memory at index " << index << std::endl;
                }
            }

            t_end = std::chrono::high_resolution_clock::now ();
            std::cout << "Operation took " << std::chrono::duration<double, std::milli>(t_end-t_start).count () << " ms" <<std::endl;
        }

        std::cout << "Destroying the mempool." << std::endl;     
        t_start = std::chrono::high_resolution_clock::now ();

        if (EmbAllocDestroy (mempool)) {
            std::cout << "Successfully destroyed the mempool" << std::endl;
        } else {
            std::cout << "Could not destroye the mempool" << std::endl;
        }

        t_end = std::chrono::high_resolution_clock::now ();
        std::cout << "Operation took " << std::chrono::duration<double, std::milli>(t_end-t_start).count () << " ms" <<std::endl;
    }

    void libcRunPerformanceBenchmarkInternal (std::vector <size_t> memory_blocks_sizes)
    {
        std::cout << "Starting the memory allocation." << std::endl;     
        auto t_start = std::chrono::high_resolution_clock::now ();

        std::vector <void*> allocations;
        allocations.reserve (memory_blocks_sizes.size ());

        for (int i = 0; i < memory_blocks_sizes.size (); i++) {
            allocations.push_back (NULL);
        }

        for (size_t i = 0; i < memory_blocks_sizes.size () / 2; i++) {
            allocations [i] = malloc (memory_blocks_sizes [i] * 2);

            if (NULL == allocations [i]) {
                std::cout << "Could not allocate the large memory at index " << i << std::endl;
            }
        }

        for (size_t i = 0; i < memory_blocks_sizes.size () / 2; i ++) {
            free (allocations [i]);
            allocations [i] = NULL;
        }

        for (size_t i = 0; i < memory_blocks_sizes.size (); i++) {
            allocations [i] = malloc (memory_blocks_sizes [i]);

            if (NULL == allocations [i]) {
                std::cout << "Could not allocate the memory at index " << i << std::endl;
            }
        }

        for (size_t i = 1; i < memory_blocks_sizes.size (); i += 2) {
            free (allocations [i]);
            allocations [i] = NULL;
        }

        for (size_t i = 0; i < memory_blocks_sizes.size () / 4; i += 2) {
            allocations [i] = realloc (allocations [i], memory_blocks_sizes [i] / 2);

            if (NULL == allocations [i]) {
                std::cout << "Could not reallocate the (smaller) memory at index " << i << std::endl;
            }
        }

        {
            size_t i  = 0;

            for (i = memory_blocks_sizes.size () - 1; i > 3 * memory_blocks_sizes.size () / 4; i--) {
                if (allocations [i]) {
                    break;
                }
            }

            for (; i >= 3 * memory_blocks_sizes.size (); i -= 2) {
                allocations [i] = realloc (allocations [i], memory_blocks_sizes [i] * 3);

                if (NULL == allocations [i]) {
                    std::cout << "Could not reallocate the (previous block larger) memory at index " << i << std::endl;
                }
            }
        }

        for (size_t i = memory_blocks_sizes.size () / 4; i < memory_blocks_sizes.size () / 2; i += 2) {
            allocations [i] = realloc (allocations [i], memory_blocks_sizes [i] * 2);

            if (NULL == allocations [i]) {
                std::cout << "Could not reallocate the (larger) memory at index " << i << std::endl;
            }
        }

        {
            size_t i  = 0;

            for (i = 3 * memory_blocks_sizes.size () / 4 - 1; i > memory_blocks_sizes.size () / 2; i--) {
                if (allocations [i]) {
                    break;
                }
            }

            for (; i >= memory_blocks_sizes.size () / 2; i -= 2) {
                allocations [i] = realloc (allocations [i], memory_blocks_sizes [i] * 3);

                if (NULL == allocations [i]) {
                    std::cout << "Could not reallocate the (next block larger) memory at index " << i << std::endl;
                }
            }
        }

        for (size_t i = 0; i < memory_blocks_sizes.size (); i ++) {
            if (allocations [i]) {
                free (allocations [i]);
                allocations [i] = NULL;
            }
        }

        for (size_t i = 0; i < memory_blocks_sizes.size (); i++) {
            allocations [i] = malloc (memory_blocks_sizes [i]);

            if (NULL == allocations [i]) {
                std::cout << "Could not allocate the (final) memory at index " << i << std::endl;
            }
        }

        std::srand(std::time(nullptr));

        for (size_t i = 0; i < memory_blocks_sizes.size (); i++) {
            size_t index = std::rand() % memory_blocks_sizes.size ();
            free (allocations [index]);
            allocations [index] = malloc (memory_blocks_sizes [i]);

            if (NULL == allocations [index]) {
                std::cout << "Could not allocate the (random) memory at index " << index << std::endl;
            }
        }

        auto t_end = std::chrono::high_resolution_clock::now ();
        std::cout << "Operation took " << std::chrono::duration<double, std::milli>(t_end-t_start).count () << " ms" <<std::endl;
    }

#ifdef RUN_WOF_ALLOCATOR_COMPARISON
    void WofAllocRunPerformanceBenchmarkInternal (std::vector <size_t> memory_blocks_sizes)
    {
        std::cout << "Starting the mempool creation." << std::endl;     
        auto t_start = std::chrono::high_resolution_clock::now ();

        wof_allocator_t* mempool = wof_allocator_new ();

        if (NULL != mempool) {
            std::cout << "Successfully created the mempool" << std::endl;
        } else {
            std::cout << "Could not create the mempool" << std::endl;
            return;
        }

        auto t_end = std::chrono::high_resolution_clock::now ();
        std::cout << "Operation took " << std::chrono::duration<double, std::milli>(t_end-t_start).count () << " ms" <<std::endl;

        {
            std::cout << "Starting the memory allocation." << std::endl;     
            t_start = std::chrono::high_resolution_clock::now ();

            std::vector <void*> allocations;
            allocations.reserve (memory_blocks_sizes.size ());

            for (int i = 0; i < memory_blocks_sizes.size (); i++) {
                allocations.push_back (NULL);
            }

            for (size_t i = 0; i < memory_blocks_sizes.size () / 2; i++) {
                allocations [i] = wof_alloc (mempool, memory_blocks_sizes [i] * 2);

                if (NULL == allocations [i]) {
                    std::cout << "Could not allocate the large memory at index " << i << std::endl;
                }
            }

            for (size_t i = 0; i < memory_blocks_sizes.size () / 2; i ++) {
                wof_free (mempool, allocations [i]);
                allocations [i] = NULL;
            }

            for (size_t i = 0; i < memory_blocks_sizes.size (); i++) {
                allocations [i] = wof_alloc (mempool, memory_blocks_sizes [i]);

                if (NULL == allocations [i]) {
                    std::cout << "Could not allocate the memory at index " << i << std::endl;
                }
            }

            for (size_t i = 1; i < memory_blocks_sizes.size (); i += 2) {
                wof_free (mempool, allocations [i]);
                allocations [i] = NULL;
            }

            for (size_t i = 0; i < memory_blocks_sizes.size () / 4; i += 2) {
                allocations [i] = wof_realloc (mempool, allocations [i], memory_blocks_sizes [i] / 2);

                if (NULL == allocations [i]) {
                    std::cout << "Could not reallocate the (smaller) memory at index " << i << std::endl;
                }
            }

            {
                size_t i  = 0;

                for (i = memory_blocks_sizes.size () - 1; i > 3 * memory_blocks_sizes.size () / 4; i--) {
                    if (allocations [i]) {
                        break;
                    }
                }

                for (; i >= 3 * memory_blocks_sizes.size (); i -= 2) {
                    allocations [i] = wof_realloc (mempool, allocations [i], memory_blocks_sizes [i] * 3);

                    if (NULL == allocations [i]) {
                        std::cout << "Could not reallocate the (previous block larger) memory at index " << i << std::endl;
                    }
                }
            }

            for (size_t i = memory_blocks_sizes.size () / 4; i < memory_blocks_sizes.size () / 2; i += 2) {
                allocations [i] = wof_realloc (mempool, allocations [i], memory_blocks_sizes [i] * 2);

                if (NULL == allocations [i]) {
                    std::cout << "Could not reallocate the (larger) memory at index " << i << std::endl;
                }
            }

            {
                size_t i  = 0;

                for (i = 3 * memory_blocks_sizes.size () / 4 - 1; i > memory_blocks_sizes.size () / 2; i--) {
                    if (allocations [i]) {
                        break;
                    }
                }

                for (; i >= memory_blocks_sizes.size () / 2; i -= 2) {
                    allocations [i] = wof_realloc (mempool, allocations [i], memory_blocks_sizes [i] * 3);

                    if (NULL == allocations [i]) {
                        std::cout << "Could not reallocate the (next block larger) memory at index " << i << std::endl;
                    }
                }
            }

            for (size_t i = 0; i < memory_blocks_sizes.size (); i ++) {
                if (allocations [i]) {
                    wof_free (mempool, allocations [i]);
                    allocations [i] = NULL;
                }
            }

            for (size_t i = 0; i < memory_blocks_sizes.size (); i++) {
                allocations [i] = wof_alloc (mempool, memory_blocks_sizes [i]);

                if (NULL == allocations [i]) {
                    std::cout << "Could not allocate the (final) memory at index " << i << std::endl;
                }
            }

            std::srand(std::time(nullptr));

            for (size_t i = 0; i < memory_blocks_sizes.size (); i++) {
                size_t index = std::rand() % memory_blocks_sizes.size ();
                wof_free (mempool, allocations [index]);
                allocations [index] = wof_alloc (mempool, memory_blocks_sizes [i]);

                if (NULL == allocations [index]) {
                    std::cout << "Could not allocate the (random) memory at index " << index << std::endl;
                }
            }

            t_end = std::chrono::high_resolution_clock::now ();
            std::cout << "Operation took " << std::chrono::duration<double, std::milli>(t_end-t_start).count () << " ms" <<std::endl;
        }

        std::cout << "Destroying the mempool." << std::endl;     
        t_start = std::chrono::high_resolution_clock::now ();

        wof_allocator_destroy (mempool);

        t_end = std::chrono::high_resolution_clock::now ();
        std::cout << "Operation took " << std::chrono::duration<double, std::milli>(t_end-t_start).count () << " ms" <<std::endl;
    }
#endif /** RUN_WOF_ALLOCATOR_COMPARISON */

#ifdef RUN_KULMINAATOR_ALLOCATOR_COMPARISON
    void KluminaatorMempoolRunPerformanceBenchmarkInternal (std::vector <size_t> memory_blocks_sizes)
    {
        std::cout << "Starting the mempool creation." << std::endl;     
        auto t_start = std::chrono::high_resolution_clock::now ();

        mempool* mempool = mempool_create ();

        if (NULL != mempool) {
            std::cout << "Successfully created the mempool" << std::endl;
        } else {
            std::cout << "Could not create the mempool" << std::endl;
            return;
        }

        auto t_end = std::chrono::high_resolution_clock::now ();
        std::cout << "Operation took " << std::chrono::duration<double, std::milli>(t_end-t_start).count () << " ms" <<std::endl;

        {
            std::cout << "Starting the memory allocation." << std::endl;     
            t_start = std::chrono::high_resolution_clock::now ();

            std::vector <void*> allocations;
            allocations.reserve (memory_blocks_sizes.size ());

            for (int i = 0; i < memory_blocks_sizes.size (); i++) {
                allocations.push_back (NULL);
            }

            for (size_t i = 0; i < memory_blocks_sizes.size () / 2; i++) {
                allocations [i] = mempool_malloc (mempool, memory_blocks_sizes [i] * 2);

                if (NULL == allocations [i]) {
                    std::cout << "Could not allocate the large memory at index " << i << std::endl;
                }
            }

            for (size_t i = 0; i < memory_blocks_sizes.size () / 2; i ++) {
                mempool_free (mempool, allocations [i]);
                allocations [i] = NULL;
            }

            for (size_t i = 0; i < memory_blocks_sizes.size (); i++) {
                allocations [i] = mempool_malloc (mempool, memory_blocks_sizes [i]);

                if (NULL == allocations [i]) {
                    std::cout << "Could not allocate the memory at index " << i << std::endl;
                }
            }

            for (size_t i = 1; i < memory_blocks_sizes.size (); i += 2) {
                mempool_free (mempool, allocations [i]);
                allocations [i] = NULL;
            }

            for (size_t i = 0; i < memory_blocks_sizes.size () / 4; i += 2) {
                allocations [i] = mempool_realloc (mempool, allocations [i], memory_blocks_sizes [i] / 2);

                if (NULL == allocations [i]) {
                    std::cout << "Could not reallocate the (smaller) memory at index " << i << std::endl;
                }
            }

            {
                size_t i  = 0;

                for (i = memory_blocks_sizes.size () - 1; i > 3 * memory_blocks_sizes.size () / 4; i--) {
                    if (allocations [i]) {
                        break;
                    }
                }

                for (; i >= 3 * memory_blocks_sizes.size (); i -= 2) {
                    allocations [i] = mempool_realloc (mempool, allocations [i], memory_blocks_sizes [i] * 3);

                    if (NULL == allocations [i]) {
                        std::cout << "Could not reallocate the (previous block larger) memory at index " << i << std::endl;
                    }
                }
            }

            for (size_t i = memory_blocks_sizes.size () / 4; i < memory_blocks_sizes.size () / 2; i += 2) {
                allocations [i] = mempool_realloc (mempool, allocations [i], memory_blocks_sizes [i] * 2);

                if (NULL == allocations [i]) {
                    std::cout << "Could not reallocate the (larger) memory at index " << i << std::endl;
                }
            }

            {
                size_t i  = 0;

                for (i = 3 * memory_blocks_sizes.size () / 4 - 1; i > memory_blocks_sizes.size () / 2; i--) {
                    if (allocations [i]) {
                        break;
                    }
                }

                for (; i >= memory_blocks_sizes.size () / 2; i -= 2) {
                    allocations [i] = mempool_realloc (mempool, allocations [i], memory_blocks_sizes [i] * 3);

                    if (NULL == allocations [i]) {
                        std::cout << "Could not reallocate the (next block larger) memory at index " << i << std::endl;
                    }
                }
            }

            for (size_t i = 0; i < memory_blocks_sizes.size (); i ++) {
                if (allocations [i]) {
                    mempool_free (mempool, allocations [i]);
                    allocations [i] = NULL;
                }
            }

            for (size_t i = 0; i < memory_blocks_sizes.size (); i++) {
                allocations [i] = mempool_malloc (mempool, memory_blocks_sizes [i]);

                if (NULL == allocations [i]) {
                    std::cout << "Could not allocate the (final) memory at index " << i << std::endl;
                }
            }

            std::srand(std::time(nullptr));

            for (size_t i = 0; i < memory_blocks_sizes.size (); i++) {
                size_t index = std::rand() % memory_blocks_sizes.size ();
                mempool_free (mempool, allocations [index]);
                allocations [index] = mempool_malloc (mempool, memory_blocks_sizes [i]);

                if (NULL == allocations [index]) {
                    std::cout << "Could not allocate the (random) memory at index " << index << std::endl;
                }
            }

            t_end = std::chrono::high_resolution_clock::now ();
            std::cout << "Operation took " << std::chrono::duration<double, std::milli>(t_end-t_start).count () << " ms" <<std::endl;
        }

        std::cout << "Destroying the mempool." << std::endl;     
        t_start = std::chrono::high_resolution_clock::now ();

        mempool_clean (mempool);

        t_end = std::chrono::high_resolution_clock::now ();
        std::cout << "Operation took " << std::chrono::duration<double, std::milli>(t_end-t_start).count () << " ms" <<std::endl;
    }
#endif /** RUN_KULMINAATOR_ALLOCATOR_COMPARISON */

#ifdef RUN_SOFTING_INDUSTRIAL_ALLOCATOR_COMPARISON
    void SoftingIndustrialMemPoolRunPerformanceBenchmarkInternal (std::vector <size_t> memory_blocks_sizes)
    {
        std::cout << "Starting the mempool creation." << std::endl;     
        auto t_start = std::chrono::high_resolution_clock::now ();

        if (MemPool_init (SCALE_MULTIPLIER * BASE_MEMPOOL_SIZE)) {
            std::cout << "Successfully created the mempool" << std::endl;
        } else {
            std::cout << "Could not create the mempool" << std::endl;
            return;
        }

        auto t_end = std::chrono::high_resolution_clock::now ();
        std::cout << "Operation took " << std::chrono::duration<double, std::milli>(t_end-t_start).count () << " ms" <<std::endl;

        {
            std::cout << "Starting the memory allocation." << std::endl;     
            t_start = std::chrono::high_resolution_clock::now ();

            std::vector <void*> allocations;
            allocations.reserve (memory_blocks_sizes.size ());

            for (int i = 0; i < memory_blocks_sizes.size (); i++) {
                allocations.push_back (NULL);
            }

            for (size_t i = 0; i < memory_blocks_sizes.size () / 2; i++) {
                allocations [i] = MemPool_Memory_Alloc_Func (memory_blocks_sizes [i] * 2);

                if (NULL == allocations [i]) {
                    std::cout << "Could not allocate the large memory at index " << i << std::endl;
                }
            }

            for (size_t i = 0; i < memory_blocks_sizes.size () / 2; i ++) {
                MemPool_Memory_Free_Func (allocations [i]);
                allocations [i] = NULL;
            }

            for (size_t i = 0; i < memory_blocks_sizes.size (); i++) {
                allocations [i] = MemPool_Memory_Alloc_Func (memory_blocks_sizes [i]);

                if (NULL == allocations [i]) {
                    std::cout << "Could not allocate the memory at index " << i << std::endl;
                }
            }

            for (size_t i = 1; i < memory_blocks_sizes.size (); i += 2) {
                MemPool_Memory_Free_Func (allocations [i]);
                allocations [i] = NULL;
            }

            for (size_t i = 0; i < memory_blocks_sizes.size () / 4; i += 2) {
                allocations [i] = MemPool_Memory_ReAlloc_Func (allocations [i], memory_blocks_sizes [i] / 2);

                if (NULL == allocations [i]) {
                    std::cout << "Could not reallocate the (smaller) memory at index " << i << std::endl;
                }
            }

            {
                size_t i  = 0;

                for (i = memory_blocks_sizes.size () - 1; i > 3 * memory_blocks_sizes.size () / 4; i--) {
                    if (allocations [i]) {
                        break;
                    }
                }

                for (; i >= 3 * memory_blocks_sizes.size (); i -= 2) {
                    allocations [i] = MemPool_Memory_ReAlloc_Func (allocations [i], memory_blocks_sizes [i] * 3);

                    if (NULL == allocations [i]) {
                        std::cout << "Could not reallocate the (previous block larger) memory at index " << i << std::endl;
                    }
                }
            }

            for (size_t i = memory_blocks_sizes.size () / 4; i < memory_blocks_sizes.size () / 2; i += 2) {
                allocations [i] = MemPool_Memory_ReAlloc_Func (allocations [i], memory_blocks_sizes [i] * 2);

                if (NULL == allocations [i]) {
                    std::cout << "Could not reallocate the (larger) memory at index " << i << std::endl;
                }
            }

            {
                size_t i  = 0;

                for (i = 3 * memory_blocks_sizes.size () / 4 - 1; i > memory_blocks_sizes.size () / 2; i--) {
                    if (allocations [i]) {
                        break;
                    }
                }

                for (; i >= memory_blocks_sizes.size () / 2; i -= 2) {
                    allocations [i] = MemPool_Memory_ReAlloc_Func (allocations [i], memory_blocks_sizes [i] * 3);

                    if (NULL == allocations [i]) {
                        std::cout << "Could not reallocate the (next block larger) memory at index " << i << std::endl;
                    }
                }
            }

            for (size_t i = 0; i < memory_blocks_sizes.size (); i ++) {
                if (allocations [i]) {
                    MemPool_Memory_Free_Func (allocations [i]);
                    allocations [i] = NULL;
                }
            }

            for (size_t i = 0; i < memory_blocks_sizes.size (); i++) {
                allocations [i] = MemPool_Memory_Alloc_Func (memory_blocks_sizes [i]);

                if (NULL == allocations [i]) {
                    std::cout << "Could not allocate the (final) memory at index " << i << std::endl;
                }
            }

            std::srand(std::time(nullptr));

            for (size_t i = 0; i < memory_blocks_sizes.size (); i++) {
                size_t index = std::rand() % memory_blocks_sizes.size ();
                MemPool_Memory_Free_Func (allocations [index]);
                allocations [index] = MemPool_Memory_Alloc_Func (memory_blocks_sizes [i]);

                if (NULL == allocations [index]) {
                    std::cout << "Could not allocate the (random) memory at index " << index << std::endl;
                }
            }

            t_end = std::chrono::high_resolution_clock::now ();
            std::cout << "Operation took " << std::chrono::duration<double, std::milli>(t_end-t_start).count () << " ms" <<std::endl;
        }

        std::cout << "Destroying the mempool." << std::endl;     
        t_start = std::chrono::high_resolution_clock::now ();

        MemPool_exit ();

        t_end = std::chrono::high_resolution_clock::now ();
        std::cout << "Operation took " << std::chrono::duration<double, std::milli>(t_end-t_start).count () << " ms" <<std::endl;
    }
#endif /** RUN_SOFTING_INDUSTRIAL_ALLOCATOR_COMPARISON */

#ifdef RUN_C_MEMPOOL_ALLOCATOR_COMPARISON
    void CMemPoolRunPerformanceBenchmarkInternal (std::vector <size_t> memory_blocks_sizes)
    {
        std::cout << "Starting the mempool creation." << std::endl;     
        auto t_start = std::chrono::high_resolution_clock::now ();

        MemPool::CMemoryPool* mempool = new MemPool::CMemoryPool (SCALE_MULTIPLIER * BASE_MEMPOOL_SIZE);

        if (mempool) {
            std::cout << "Successfully created the mempool" << std::endl;
        } else {
            std::cout << "Could not create the mempool" << std::endl;
            return;
        }

        auto t_end = std::chrono::high_resolution_clock::now ();
        std::cout << "Operation took " << std::chrono::duration<double, std::milli>(t_end-t_start).count () << " ms" <<std::endl;

        {
            std::size_t dummyVal = 0;
            std::cout << "Starting the memory allocation." << std::endl;     
            t_start = std::chrono::high_resolution_clock::now ();

            std::vector <void*> allocations;
            allocations.reserve (memory_blocks_sizes.size ());

            for (int i = 0; i < memory_blocks_sizes.size (); i++) {
                allocations.push_back (NULL);
            }

            for (size_t i = 0; i < memory_blocks_sizes.size () / 2; i++) {
                allocations [i] = mempool->GetMemory (memory_blocks_sizes [i] * 2);

                if (NULL == allocations [i]) {
                    std::cout << "Could not allocate the large memory at index " << i << std::endl;
                }
            }

            for (size_t i = 0; i < memory_blocks_sizes.size () / 2; i ++) {
                mempool->FreeMemory (allocations [i], dummyVal);
                allocations [i] = NULL;
            }

            for (size_t i = 0; i < memory_blocks_sizes.size (); i++) {
                allocations [i] = mempool->GetMemory (memory_blocks_sizes [i]);

                if (NULL == allocations [i]) {
                    std::cout << "Could not allocate the memory at index " << i << std::endl;
                }
            }

            for (size_t i = 1; i < memory_blocks_sizes.size (); i += 2) {
                mempool->FreeMemory (allocations [i], dummyVal);
                allocations [i] = NULL;
            }

            for (size_t i = 0; i < memory_blocks_sizes.size () / 4; i += 2) {
                void* tmp = allocations [i];
                allocations [i] = mempool->GetMemory (memory_blocks_sizes [i] / 2);

                if (NULL == allocations [i]) {
                    std::cout << "Could not reallocate the (smaller) memory at index " << i << std::endl;
                } else {
                    memcpy (allocations [i], tmp, memory_blocks_sizes [i] / 2);
                    mempool->FreeMemory (tmp, dummyVal);
                }
            }

            {
                size_t i  = 0;

                for (i = memory_blocks_sizes.size () - 1; i > 3 * memory_blocks_sizes.size () / 4; i--) {
                    if (allocations [i]) {
                        break;
                    }
                }

                for (; i >= 3 * memory_blocks_sizes.size (); i -= 2) {
                    void* tmp = allocations [i];
                    allocations [i] = mempool->GetMemory (memory_blocks_sizes [i] * 3);

                    if (NULL == allocations [i]) {
                        std::cout << "Could not reallocate the (previous block larger) memory at index " << i << std::endl;
                    } else {
                        memcpy (allocations [i], tmp, memory_blocks_sizes [i]);
                        mempool->FreeMemory (tmp, dummyVal);
                    }
                }
            }

            for (size_t i = memory_blocks_sizes.size () / 4; i < memory_blocks_sizes.size () / 2; i += 2) {
                void* tmp = allocations [i];
                allocations [i] = mempool->GetMemory (memory_blocks_sizes [i] * 2);

                if (NULL == allocations [i]) {
                    std::cout << "Could not reallocate the (larger) memory at index " << i << std::endl;
                } else {
                    memcpy (allocations [i], tmp, memory_blocks_sizes [i]);
                    mempool->FreeMemory (tmp, dummyVal);
                }
            }

            {
                size_t i  = 0;

                for (i = 3 * memory_blocks_sizes.size () / 4 - 1; i > memory_blocks_sizes.size () / 2; i--) {
                    if (allocations [i]) {
                        break;
                    }
                }

                for (; i >= memory_blocks_sizes.size () / 2; i -= 2) {
                    void* tmp = allocations [i];
                    allocations [i] = mempool->GetMemory (memory_blocks_sizes [i] * 3);

                    if (NULL == allocations [i]) {
                        std::cout << "Could not reallocate the (next block larger) memory at index " << i << std::endl;
                    } else {
                        memcpy (allocations [i], tmp, memory_blocks_sizes [i]);
                        mempool->FreeMemory (tmp, dummyVal);
                    }
                }
            }

            for (size_t i = 0; i < memory_blocks_sizes.size (); i ++) {
                if (allocations [i]) {
                    mempool->FreeMemory (allocations [i], dummyVal);
                    allocations [i] = NULL;
                }
            }

            for (size_t i = 0; i < memory_blocks_sizes.size (); i++) {
                allocations [i] = mempool->GetMemory ( memory_blocks_sizes [i]);

                if (NULL == allocations [i]) {
                    std::cout << "Could not allocate the (final) memory at index " << i << std::endl;
                }
            }

            std::srand(std::time(nullptr));

            for (size_t i = 0; i < memory_blocks_sizes.size (); i++) {
                size_t index = std::rand() % memory_blocks_sizes.size ();
                mempool->FreeMemory (allocations [index], dummyVal);
                allocations [index] = mempool->GetMemory ( memory_blocks_sizes [i]);

                if (NULL == allocations [index]) {
                    std::cout << "Could not allocate the (random) memory at index " << index << std::endl;
                }
            }

            t_end = std::chrono::high_resolution_clock::now ();
            std::cout << "Operation took " << std::chrono::duration<double, std::milli>(t_end-t_start).count () << " ms" <<std::endl;

            std::cout << "Destroying the mempool." << std::endl;     
            t_start = std::chrono::high_resolution_clock::now ();

            for (size_t i = 0; i < memory_blocks_sizes.size (); i++) {
                if (allocations [i]) {
                    mempool->FreeMemory (allocations [i], dummyVal);
                }
            }
        }

        delete mempool;

        t_end = std::chrono::high_resolution_clock::now ();
        std::cout << "Operation took " << std::chrono::duration<double, std::milli>(t_end-t_start).count () << " ms" <<std::endl;
    }
#endif /** RUN_C_MEMPOOL_ALLOCATOR_COMPARISON */
}