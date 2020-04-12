/** 
 * Embedded Memory Allocator Utilities
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

#ifndef __EMB_ALLOC_UTIL_H__
#define __EMB_ALLOC_UTIL_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef __cplusplus
    /*bool type declarations for C */
    #include <stdbool.h>
#endif

/**
 * https://blog.kowalczyk.info/article/j/guide-to-predefined-macros-in-c-compilers-gcc-clang-msvc-etc..html
 */
#if defined (__linux__)
    #include <pthread.h>

    #define EmbAllocMutex pthread_mutex_t
#elif defined (_WIN32) || defined (_WIN64 )
    #include <windows.h>
    #include <process.h>

    #ifdef USE_WIN_CRITICAL_SECTION
        #define EmbAllocMutex CRITICAL_SECTION 
    #else /** USE_WIN_CRITICAL_SECTION */
        #define EmbAllocMutex HANDLE
    #endif /** USE_WIN_CRITICAL_SECTION  */
#else /** Neither __linux__ nor _WIN32/_WIN64 are defined*/
    #error Don't know how to create mutexes
#endif /** __linux__ || _WIN32/_WIN64 */


/**
 * Initializes an OS independent mutex.
 * @param mutex the mutex to be initialized.
 * @return 0 in case of success, -1 otherwise.
 */
int EmbAllocInitMutex (EmbAllocMutex *mutex);

/**
 * Destroys an OS independent mutex.
 * @param mutex the mutex to be destroyed.
 * @return 0 in case of success, -1 otherwise.
 */
int EmbAllocDestroyMutex (EmbAllocMutex *mutex);

/**
 * Locks an OS independent mutex.
 * @param mutex the mutex to be locked.
 * @return 0 in case of success, -1 otherwise.
 */
int EmbAllocLockMutex (EmbAllocMutex *mutex);

/**
 * Unlocks an OS independent mutex.
 * @param mutex the mutex to be unlocked.
 * @return 0 in case of success, -1 otherwise.
 */
int EmbAllocUnlockMutex (EmbAllocMutex *mutex);

/**
 * Checks whether the whole buffer is initialized to a predefined value.
 * @param buffer the buffer to be checked.
 * @param size the reference_valuesize of the buffer.
 * @param reference_value the value against which all buffer elements will be checked.
 */
bool EmbAllocCheckBuffer (void* buffer, size_t size, unsigned char reference_value);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif //__EMB_ALLOC_UTIL_H__