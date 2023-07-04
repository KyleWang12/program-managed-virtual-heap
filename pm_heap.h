#ifndef PM_HEAP_H
#define PM_HEAP_H

#include <stddef.h>
#include <stdint.h>

/* 2560 pages in memory */
#define PM_PAGE_NUM 2560
#define PM_PAGE_SIZE 4096
/* support up to 65536 (2^16) pages (including vm) */
#define VM_MAP_SIZE (4 * 65536)
/* pm to vm */
#define PM_MAP_SIZE VM_MAP_SIZE
#define PM_HEAP_SIZE (PM_PAGE_NUM * PM_PAGE_SIZE)

typedef struct pm_heap_block {
    /* size of the block including the header */
    uint32_t size;
    uint32_t offset;
    /* the index for first empty index */
    uint32_t index;
    struct pm_heap_block* next;
} pm_heap_block;

/**
 * @brief initiate pm heap
 * 
 */
void pm_init();

/**
 * @brief malloc space using custom heap that supports virtual memory
 * 
 * @param size the size in char
 * @return virtual address of the allocated space. If error happens, return -1;
 */
int32_t pm_malloc(uint32_t size);

/**
 * @brief free the space
 * 
 * @param VA virtual address of the allocated space
 */
void pm_free(int32_t VA);

/**
 * @brief get char at the given index.
 * 
 * @param VA the virtual address of the space
 * @param index index
 * @return '\0' if the index is out of range, else return the char at index
 */
char get_char(int32_t VA, int index);

/**
 * @brief Set the char at given index
 * 
 * @param VA the virtual address of the space
 * @param index index
 * @param c given char
 * @return 0 if out of range, else 1 if success
 */
int set_char(int32_t VA, int index, char c);


#endif