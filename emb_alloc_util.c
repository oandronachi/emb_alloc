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

#include "emb_alloc_util.h"

/**
 * https://www.codeproject.com/Articles/25569/Cross-Platform-Mutex
 */

int EmbAllocInitMutex (EmbAllocMutex *mutex)
{
    if (NULL == mutex) {
        return -1;
    }

    #if defined (__linux__)
        return ((0 != pthread_mutex_init (mutex, NULL))? -1: 0);
    #elif defined (_WIN32) || defined (_WIN64 )
        #ifdef USE_WIN_CRITICAL_SECTION
            InitializeCriticalSection (mutex);
            return 0;
        #else /** USE_WIN_CRITICAL_SECTION */
            *mutex = CreateMutex (0, FALSE, 0);
            return ((NULL == *mutex)? -1: 0);
        #endif /** USE_WIN_CRITICAL_SECTION  */
    #else /** Neither __linux__ nor _WIN32/_WIN64 are defined*/
        #error Don't know how to create mutexes
        return -1;
    #endif /** __linux__ || _WIN32/_WIN64 */
}

int EmbAllocDestroyMutex (EmbAllocMutex *mutex)
{
    if (NULL == mutex) {
        return -1;
    }

    #if defined (__linux__)
        return ((0 != pthread_mutex_destroy (mutex))? -1: 0);
    #elif defined (_WIN32) || defined (_WIN64 )
        #ifdef USE_WIN_CRITICAL_SECTION
            DeleteCriticalSection (mutex);
            return 0;
        #else /** USE_WIN_CRITICAL_SECTION */
            return ((FALSE == CloseHandle (*mutex))? -1: 0);
        #endif /** USE_WIN_CRITICAL_SECTION  */
    #else /** Neither __linux__ nor _WIN32/_WIN64 are defined*/
        #error Don't know how to destroy mutexes
        return -1;
    #endif /** __linux__ || _WIN32/_WIN64 */
}

int EmbAllocLockMutex (EmbAllocMutex *mutex)
{
    if (NULL == mutex) {
        return -1;
    }

    #if defined (__linux__)
        return ((0 != pthread_mutex_lock(mutex))? -1: 0);
    #elif defined (_WIN32) || defined (_WIN64 )
        #ifdef USE_WIN_CRITICAL_SECTION
            EnterCriticalSection (mutex);
            return 0;
        #else /** USE_WIN_CRITICAL_SECTION */
            return ((WAIT_FAILED == WaitForSingleObject (*mutex, INFINITE))? -1: 0);
        #endif /** USE_WIN_CRITICAL_SECTION  */
    #else /** Neither __linux__ nor _WIN32/_WIN64 are defined*/
        #error Don't know how to lock mutexes
        return -1;
    #endif /** __linux__ || _WIN32/_WIN64 */
}
int EmbAllocUnlockMutex (EmbAllocMutex *mutex)
{
    if (NULL == mutex) {
        return -1;
    }

    #if defined (__linux__)
        return ((0 != pthread_mutex_unlock (mutex))? -1: 0);
    #elif defined (_WIN32) || defined (_WIN64 )
        #ifdef USE_WIN_CRITICAL_SECTION
            LeaveCriticalSection (mutex);
            return 0;
        #else /** USE_WIN_CRITICAL_SECTION */
            return ((FALSE == ReleaseMutex (*mutex))? -1: 0);
        #endif /** USE_WIN_CRITICAL_SECTION  */
    #else /** Neither __linux__ nor  _WIN32/_WIN64 are defined*/
        #error Don't know how to unlock mutexes
        return -1;
    #endif /** __linux__ || _WIN32/_WIN64 */
}

bool EmbAllocCheckBuffer (void* buffer, size_t size, unsigned char reference_value)
{
    if ((NULL == buffer) ||
        (0 == size)) {
        return true;
    }

    return ((*((unsigned char*) buffer) == reference_value) &&
            (0 == memcmp (buffer, ((unsigned char*) buffer + 1), size - 1)));
}