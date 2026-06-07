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


#ifndef __EMB_ALLOC_PERFORMANCE_BENCHMARK_H__
#define __EMB_ALLOC_PERFORMANCE_BENCHMARK_H__

#include <stddef.h>

/* Default workload scale used by the C++ default argument and by explicit 0.
 * Define this before including the header to change the benchmark default. */
#ifndef EMB_ALLOC_PERFORMANCE_BENCHMARK_DEFAULT_SCALE_MULTIPLIER
#define EMB_ALLOC_PERFORMANCE_BENCHMARK_DEFAULT_SCALE_MULTIPLIER 1u
#endif

/**
 * Runs a simple malloc/free/realloc time performance benchmark.
 * ****************************************************************************
 * If RUN_WOF_ALLOCATOR_COMPARISON is defined, 
 * then wof_allocator.h and wof_allocator.c must be made available as well.
 * The project is available at https://github.com/eapache/wof_alloc.
 * ****************************************************************************
 * If RUN_KULMINAATOR_ALLOCATOR_COMPARISON is defined, 
 * then mempool.h and mempool.c must be made available as well.
 * The project is available at https://github.com/kulminaator/mempool.
 * @note When using the kluminaator mempool, the mempool.h header must 
 *       be modified so that is uses extern "C"
 * ****************************************************************************
 * If RUN_SOFTING_INDUSTRIAL_ALLOCATOR_COMPARISON is defined, 
 * then mempool.h, dlmalloc.h, mpcommon.h, dlmalloc.c and mempool.c must be made 
 * available as well.
 * The project is available at https://github.com/SoftingIndustrial/MemPool.
 * @note If used in conjunction with RUN_KULMINAATOR_ALLOCATOR_COMPARISON, 
 *       then one mempool.c file needs to be renamed.
 * @note This mempool implementation proved to be quite slow in testing, so expect
 *       quite a large run time.
 * @note When testing other mempools, the include files might need some adjustments
 *       inside the external project files as well.
 * ****************************************************************************
 * @note If the other mempools headers are located in different relative paths,
 *       then the includes inside emb_alloc_performance_benchmark.cpp must be
 *       adjusted as well.
 * @param scale_multiplier scales the initial pool sizes and the number of
 *        allocation operations. Use 0 to select the configured default.
 */
#ifdef __cplusplus
void EmbAllocRunPerformanceBenchmark (
    size_t scale_multiplier = EMB_ALLOC_PERFORMANCE_BENCHMARK_DEFAULT_SCALE_MULTIPLIER);
#else
void EmbAllocRunPerformanceBenchmark (size_t scale_multiplier);
#endif

#endif /** __EMB_ALLOC_PERFORMANCE_BENCHMARK_H__ */
