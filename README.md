# emb_alloc
Embedded Memory Allocator
=========================
This is C99 C implementation of a mempool. It should compile without any issues on most platforms.
It might need some minor extensions (related to mutexes). If this will be the case, then a compile
error will be triggered. It is provided as C source and header files, so it can be integrated 
within any project and build as needed.

It is a bit similar in structure as the FAT file system. It stores the data in fixed size chunks 
and keeps track of the statistics of the occupied memory inside a table.

It has the following characteristics:
1. It allocates a fixed size continuous memory. It does not grow or shrink is memory requirements 
change within the lifetime of the mempool. The requested initial size will remain unchanged 
throughout the whole lifecycle of the mempool.
2. It can initiate newly allocated memory to 0x00. This is configurable in the settings used
to create the mempool.
3. It can check for buffer overflow when performing memory operations (alloc, free, realloc).
This is configurable in the settings used to create the mempool.
4. It can be threadsafe. This is configurable in the settings used to create the mempool.
5. It can log errors and dump the whole mempool content. This is configurable in the settings 
used to create the mempool and via a VERBOSE_DUMP_MEMPOOL define at build time.
6. It can signal errors via a callback function. This is configurable in the settings used to 
create the mempool.

The project included a benchmark C++ source file that compares the mempools time performance 
against other available mempools. See the comment in the benchmark header file 
(emb_alloc_performance_benchmark.h) for more info on how to pick the mempool implementations to 
compare against.

In Memory Representation
------------------------
The mempool occupies more memory than the actual requested value. 

This is due to the fact that there are a few extra needed bytes:
- mempool start and end padding
- mempool creation settings
- mempool memory blocks management data table
- mempool threadsync data and error messages
- block start and end padding
- block usage data

```
_______________________________________________________________________________
                            MEMPOOL START PADDING
_______________________________________________________________________________
                                MEMPOOL SETTINGS
_______________________________________________________________________________
                MEMPOOL MEMORY BLOCKS MANAGEMENT DATA TABLE
_______________________________________________________________________________
                    MEMPOOL THREADSYNC AND ERROR DATA
_______________________________________________________________________________
                                MEMORY BLOCKS               
...............................................................................
................_____________________________________________..................
                |       MEMORY BLOCK START PADDING           |
................_____________________________________________..................
                |       MEMORY BLOCK USAGE DATA              |
................_____________________________________________..................
                |       MEMORY BLOCK ACTUAL DATA             |
................_____________________________________________..................
                |       MEMORY BLOCK END PADDING             |
................_____________________________________________..................
...............................................................................
_______________________________________________________________________________
                                MEMPOOL END PADDING
_______________________________________________________________________________
```

The extra used memory is roughly ~1KB + 24/48 B(depending on the actual memory address size of 
the underlying OS; e.g. 24 extra bytes for a 32 bits OS) * <the_number_of_memory_blocks>.

The padding is required to easily identify a mempool an memory blocks in the memory, while other 
data (blocks management, threadsync, errors, settings) allows for defining the mempool functionality
and to optimize the speed of the operations.

The memory blocks have data sizes of 32, 64, 128, 256, 512, 1024, 2048 and 4096 bytes. The number of 
blocks per size category is configured in the creation settings.

The mempool's memory blocks management data table contain information relevant for each category of 
data blocks (one table entry for wach data size). It stores the allocation limits, the free blocks 
limits and the number of occupied blocks. The limits are relevant because all the blocks in one 
category will be continuously allocated within the whole mempool memory. For example if a mempool 
contains blocks of all sizes, the mempool will contain 8 groups of continuous memory blocks. Each 
group of blocks will contain only blocks of a single size.

```
_______________________________________________________________________________
                            MEMPOOL START PADDING
_______________________________________________________________________________
                                MEMPOOL SETTINGS
_______________________________________________________________________________
                MEMPOOL MEMORY BLOCKS MANAGEMENT DATA TABLE
_______________________________________________________________________________
                    MEMPOOL THREADSYNC AND ERROR DATA
_______________________________________________________________________________
-------------------------------------------------------------------------------
|--|--|--|--|--|MEMORY BLOCKS WITH 32 BYTES OF DATA |--|--|--|--|--|--|--|--|--|
-------------------------------------------------------------------------------
...............................................................................
-------------------------------------------------------------------------------
|--|--|--|--|--|MEMORY BLOCKS WITH 64 BYTES OF DATA |--|--|--|--|--|--|--|--|--|
-------------------------------------------------------------------------------
...............................................................................
-------------------------------------------------------------------------------
|--|--|--|--|--|MEMORY BLOCKS WITH 128 BYTES OF DATA|--|--|--|--|--|--|--|--|--|
-------------------------------------------------------------------------------
...............................................................................
-------------------------------------------------------------------------------
|--|--|--|--|--|MEMORY BLOCKS WITH 256 BYTES OF DATA|--|--|--|--|--|--|--|--|--|
-------------------------------------------------------------------------------
...............................................................................
-------------------------------------------------------------------------------
|--|--|--|--|--|MEMORY BLOCKS WITH 512 BYTES OF DATA|--|--|--|--|--|--|--|--|--|
-------------------------------------------------------------------------------
...............................................................................
-------------------------------------------------------------------------------
|--|--|--|--|--|MEMORY BLOCKS WITH 1024 BYTES OF DATA       |--|--|--|--|--|--|
-------------------------------------------------------------------------------
...............................................................................
-------------------------------------------------------------------------------
|--|--|--|--|--|MEMORY BLOCKS WITH 2048 BYTES OF DATA       |--|--|--|--|--|--|
-------------------------------------------------------------------------------
...............................................................................
-------------------------------------------------------------------------------
|--|--|--|--|--|MEMORY BLOCKS WITH 4096 BYTES OF DATA       |--|--|--|--|--|--|
-------------------------------------------------------------------------------
...............................................................................
_______________________________________________________________________________
                                MEMPOOL END PADDING
_______________________________________________________________________________
```

Behaviour
---------
When a memory allocation is requested, the mempool is first validated, then allocation is attempted 
inside the memory block that fits best (with minimal memory waste; e.g. 55 bytes are best allocated 
inside a memory block with a data size of 64 bytes). If this is not possible, then other free blocks 
are taken into consideration. The alternatives would be to either allocate inside a single block 
with a larger data size (and thus more memory waste; e.g. allocate 55 bytes inside a memory block 
with a data size of 512 bytes) or inside multiple contiunous blocks with an individual size smaller 
than the requested memory size (e.g. allocate 55 bytes inside 2 continuous free blocks, each with a 
data size of 32 bytes). The decision on which non-optimal blocks to allocate memory is hardcoded, 
but inside the code there are comments on how to update that logic. A possible upgrade would be to 
allow for this logic to be chosen from inside the mempool creation settings. The memory block usage
data contains the number of used blocks and the size of the data being allocated inside that number 
of blocks. The mempool only allocates memory within block(s) of a single data size. If the memory 
does not fit within a single category of memory blocks, then the operation will fail, even if there
is sufficient continuous memory (e.g. if a mempool has 1 memory block of 32 data bytes and 1 memory 
block of 64 bytes data size and a 65 bytes allocation is attempted, then this will fail, even if the
total available memory is 96 bytes; this will fail because the allocation requires blocks from more 
than 1 category; if on the other hand, the mempool would have 2 memory blocks of 32 data bytes, then 
the operation will succeed).

When a memory deallocation is requested, the mempool is validated first, then the memory block is 
validated as well. The block usage data is reverted to the initial values. If the allocated memory
is from multiple continuous blocks, then the merged blocks will be split again into individual ones.

When a memory reallocation is requested, the mempool is validated first then the memory block is 
validated as well. A reallocation is first attempted in a continuous manner (either within the 
already allocated block(s) or within the next continuous ones). If this is not possible, then a
regular allocation is done, followes by a memory copy and then the initial memory location is freed.

The mempool and memory block validation is being done by memory comparison against the expected 
padding values.
