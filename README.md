# How to run
- Run `make`
- Run `./test` to get the test results, it may takes minutes to finish since lots of file I/O are involved.

# Design
- Page number mapping: The pm_map and vm_map keeps the mapping between virtual address and physical page number.
- Memory allocation: The pm_malloc function allocates memory of a given size. It checks the free list for available memory blocks and, if necessary, allocates new memory in the heap or evicts memory to disk to make space.
- Memory deallocation: The pm_free function deallocates memory at a given virtual address. It adds the freed memory block back to the free list and merges adjacent free blocks to reduce fragmentation.
- Eviction and loading: The code uses a FIFO mechanism to transfer data between physical memory and disk when physical memory is exhausted. An `evict_index` is used to perform FIFO on memory. Everytime when there is a need to clear out memory, the page on `evict_index` will be popped out, and then `evict_index` is plused by 1. It also maintains a file pointer head and tail to track the pages to be evicted or loaded on disk.
- Free list management: The allocator maintains a free list of available memory blocks. It tries to merge adjacent free blocks to reduce fragmentation.
- Virtual memory: The code uses `swap.data` as a backup store for virtual memory. Also when communicating with the disk, the free list is managed with files in directory `free_lists`. It synchronize the free list deleting and recovering with page evicting and loading. 
# Thread-safty
- In test.c 6 threads are created. Each thread allocates 5000 blocks of memory with randomized size. The each thread sleeps for some time and trys to set and get value. Finally all allocated memory got freed. Since only 2560 pages are stored in memory, virtual memory will be used.
