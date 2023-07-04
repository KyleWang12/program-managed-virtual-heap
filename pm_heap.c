#include "pm_heap.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>

static char phys_mem[PM_MAP_SIZE + VM_MAP_SIZE + PM_HEAP_SIZE + PM_PAGE_SIZE];
static uint32_t* pm_map = (uint32_t*)(phys_mem);
static uint32_t* vm_map = (uint32_t*)(phys_mem + PM_MAP_SIZE);
static char* pm_heap = (char*)(phys_mem + PM_MAP_SIZE + VM_MAP_SIZE);
static char* temporary_swap_space = phys_mem + PM_MAP_SIZE + VM_MAP_SIZE + PM_HEAP_SIZE;
static uint32_t pm_heap_offset= 0;
static pm_heap_block* pm_free_list = NULL;
static uint32_t evict_index = 0;
static uint32_t vm_map_index = 0;
static int is_full = 0;

static pthread_mutex_t lock;
static pthread_mutexattr_t attr;

/* used for FIFO of stuff saved in file*/
static uint32_t file_ptr_head = 0;  // the idx for page to be evicted
static uint32_t file_ptr_tail = 0;  // the idx for page to be loaded to


static uint32_t pm_align(uint32_t size);
static char* get_block_address(pm_heap_block* block);
static uint32_t get_block_PN(pm_heap_block* block);
static void pm_delete_block(pm_heap_block* node);
static void update_mapping(uint32_t VN, uint32_t PN, int is_on_file);
static void load_free_list(uint32_t VN, char* dst_page);
static void load_to_memory(uint32_t PN, char* dst_page);
static uint32_t getVN(uint32_t PN, int is_on_file);
static void pm_add_block_to_free_list(pm_heap_block* block);
static int32_t getVA(uint32_t VN, uint32_t offset);
static void save_free_block_to_file(pm_heap_block* block, uint32_t VN);
static void check_and_remove_file(uint32_t VN);
static void pop_to_file();
static void pm_add_page_to_free_list(uint32_t PN);
static int is_present(uint32_t VN);
static int on_file(uint32_t VN);
static void swap_head_to_tmp();
static void pop_from_tmp_to_file(uint32_t dst_PN);
static void evict_and_load(uint32_t PN);
static void init_block(pm_heap_block* block);
static int32_t _pm_malloc(uint32_t size);
static void _pm_free(int32_t VA);
static char _get_char(int32_t VA, int index);
static int _set_char(int32_t VA, int index, char c);


/* align to the next offset divisible by page size */
static uint32_t pm_align(uint32_t size) {
    uint32_t remainder = size % PM_PAGE_SIZE;
    if (remainder != 0) {
        size += PM_PAGE_SIZE - remainder;
    }
    return size;
}

/* get the start address of a block */
static char* get_block_address(pm_heap_block* block) {
    return (char*)block;
}

static uint32_t get_block_PN(pm_heap_block* block) {
    return ((char*)block - pm_heap) / PM_PAGE_SIZE;
}

/* delete the node in free list */
static void pm_delete_block(pm_heap_block* node){
    if(pm_free_list == node) {
        pm_free_list = pm_free_list->next;
        node->next = NULL;
        return;
    }
    pm_heap_block* prev = pm_free_list;
    while(prev->next != NULL && prev->next != node) {
        prev = prev->next;
    }
    if(prev->next == NULL) {
        return;
    }
    prev->next = node->next;
    node->next = NULL;
}

static void update_mapping(uint32_t VN, uint32_t PN, int is_on_file) {
    vm_map[VN] = PN + (is_on_file << 16) + 0x20000;
    if(is_on_file) {
        pm_map[PN + PM_PAGE_NUM] = VN;
    }else {
        pm_map[PN] = VN;
    }
}

static void load_free_list(uint32_t VN, char* dst_page) {
    char file_name[25];
    sprintf(file_name, "./free_lists/%x", VN);
    FILE* file = fopen(file_name, "rb");
    if(file == NULL) return;
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    while (file_size > 0) {
        long read_offset = file_size - 12;
        fseek(file, read_offset, SEEK_SET);
        int size, offset, index;
        fread(&size, sizeof(uint32_t), 1, file);
        fread(&offset, sizeof(uint32_t), 1, file);
        fread(&index, sizeof(uint32_t), 1, file);
        pm_heap_block* block = (pm_heap_block*) (dst_page + offset);
        block->size = size;
        block->offset = offset;
        block->index = index;
        pm_add_block_to_free_list(block);
        file_size -= 12;
    }
    fclose(file);
    remove(file_name);
}

static void load_to_memory(uint32_t PN, char* dst_page){
    char* ptr = dst_page;
    FILE* file = fopen("./swap.data", "rb");
    fseek(file, PN * PM_PAGE_SIZE, SEEK_SET);
    fread(ptr, sizeof(char), PM_PAGE_SIZE, file);
    fclose(file);
    load_free_list(getVN(PN, 1), dst_page);
}

static uint32_t getVN(uint32_t PN, int is_on_file) {
    // this if only happens when not all phys memo has a map to virt memo 
    if(!is_on_file && pm_map[PN] == 0 && vm_map[0] != PN + 0x20000) {
        pm_map[PN] = vm_map_index;
        vm_map[vm_map_index++] = PN + 0x20000;
    }
    if(is_on_file && pm_map[PN + PM_PAGE_NUM] == 0 && vm_map[0] != PN + 0x20000 + 0x10000) {
        pm_map[PN + PM_PAGE_NUM] = vm_map_index;
        vm_map[vm_map_index++] = PN + 0x20000 + 0x10000;
    }
    if(is_on_file) {
        return pm_map[PN + PM_PAGE_NUM];
    }
    return pm_map[PN];
}

/* add block to the free list, perform merge to reduce fragmentation */
static void pm_add_block_to_free_list(pm_heap_block* block) {
    // check if can merge into other blocks
    pm_heap_block* ptr = pm_free_list;
    pm_heap_block* pre = NULL;
    pm_heap_block* post = NULL;
    while(ptr != NULL) {
        // ptr is after block, and on same page
        if(get_block_PN(block) == get_block_PN(ptr) && get_block_address(block) + block->size == get_block_address(ptr)) {
            post = ptr;
        }
        // block is after ptr, and on same page
        if(get_block_PN(block) == get_block_PN(ptr) && get_block_address(ptr) + ptr->size == get_block_address(block)) {
            pre = ptr;
        }
        ptr = ptr->next;
    }
    if(pre != NULL) {
        pm_delete_block(pre);
        pre->size += block->size;
        block = pre;
    }
    if(post != NULL) {
        pm_delete_block(post);
        block->size += post->size;
    }
    // if a page is free and there are data on file, load it to memory
    if(block->size == PM_PAGE_SIZE && (file_ptr_head != file_ptr_tail || is_full)) {
        uint32_t PN = get_block_PN(block);
        load_to_memory(file_ptr_head, (char*)block);
        uint32_t original_vm_VN = getVN(PN, 0);
        update_mapping(getVN(file_ptr_head, 1), PN, 0);
        update_mapping(original_vm_VN, file_ptr_head, 1);
        file_ptr_head = (file_ptr_head + 1) % (PM_MAP_SIZE / 4 - PM_PAGE_NUM);
        if(file_ptr_head == file_ptr_tail) is_full = 0;
        return;
    }
    block->next = pm_free_list;
    pm_free_list = block;
}

static int32_t getVA(uint32_t VN, uint32_t offset) {
    return (VN << 12) + offset;
}

static void save_free_block_to_file(pm_heap_block* block, uint32_t VN) {
    char file_name[25];
    sprintf(file_name, "./free_lists/%x", VN);
    FILE* file = fopen(file_name, "ab+");
    fwrite(&(block->size), sizeof(uint32_t), 1, file);
    fwrite(&(block->offset), sizeof(uint32_t), 1, file);
    fwrite(&(block->index), sizeof(uint32_t), 1, file);
    fclose(file);
}

static void check_and_remove_file(uint32_t VN) {
    char file_name[25];
    sprintf(file_name, "./free_lists/%x", VN);
    remove(file_name);
}

static void pop_to_file() {
    // remove all free list nodes having page_number == evict_index
    pm_heap_block* block = pm_free_list;
    check_and_remove_file(getVN(evict_index, 0));
    while(block != NULL) {
        uint32_t PN = ((char*)block - pm_heap) / PM_PAGE_SIZE;
        if(PN == evict_index) {
            pm_heap_block* prev = block;
            block = block->next;
            save_free_block_to_file(prev, getVN(get_block_PN(prev), 0));
            pm_delete_block(prev);
        }else {
            block = block->next;
        }
    }
    char* ptr = pm_heap + evict_index * PM_PAGE_SIZE;
    FILE* file = fopen("./swap.data", "rb+");
    fseek(file, file_ptr_tail * PM_PAGE_SIZE, SEEK_SET);
    fwrite(ptr, sizeof(char), PM_PAGE_SIZE, file);
    fclose(file);
}

static void pm_add_page_to_free_list(uint32_t PN) {
    pm_heap_block* block = (pm_heap_block*)(pm_heap + PN * PM_PAGE_SIZE);
    block->size = PM_PAGE_SIZE;
    block->index = 0;
    block->offset = 0;
    block->next = pm_free_list;
    pm_free_list = block;
}

static void init_block(pm_heap_block* block) {
    char* ptr = (char*)block + sizeof(pm_heap_block);
    for(int i=0; i<block->size - sizeof(pm_heap_block); i++) {
        *(ptr + i) = '\0';
    }
}

static int is_present(uint32_t VN) {
    return (vm_map[VN] & 0x20000);
}

static int on_file(uint32_t VN) {
    return (vm_map[VN] & 0x10000);
}

static void swap_head_to_tmp() {
    FILE* file = fopen("./swap.data", "rb");
    fseek(file, file_ptr_head * PM_PAGE_SIZE, SEEK_SET);
    fread(temporary_swap_space, sizeof(char), PM_PAGE_SIZE, file);
    fclose(file);
}

static void pop_from_tmp_to_file(uint32_t dst_PN) {
    FILE* file = fopen("./swap.data", "rb+");
    fseek(file, dst_PN * PM_PAGE_SIZE, SEEK_SET);
    fwrite(temporary_swap_space, sizeof(char), PM_PAGE_SIZE, file);
    fclose(file);
}

static void evict_and_load(uint32_t PN) {
    uint32_t evict_VN = getVN(evict_index, 0);
    uint32_t head_VN = getVN(file_ptr_head, 1);
    uint32_t target_VN = getVN(PN, 1);

    swap_head_to_tmp();
    file_ptr_head = (file_ptr_head + 1) % (PM_MAP_SIZE / 4 - PM_PAGE_NUM);
    pop_to_file();
    load_to_memory(PN, pm_heap + evict_index * PM_PAGE_SIZE);
    pop_from_tmp_to_file(PN);
    
    update_mapping(evict_VN, file_ptr_tail, 1);
    file_ptr_tail = (file_ptr_tail + 1) % (PM_MAP_SIZE / 4 - PM_PAGE_NUM);
    update_mapping(head_VN, PN, 1);
    update_mapping(target_VN, evict_index, 0);
    evict_index = (evict_index + 1) % PM_PAGE_NUM;
}

static int32_t _pm_malloc(uint32_t size) {
    if(size == 0) {
        return -1;
    }
    size = size + sizeof(pm_heap_block);
    if(size > PM_PAGE_SIZE) {
        printf("size too large!\n");
        return -1;
    }
    pm_heap_block* block = pm_free_list;
    pm_heap_block* prev = NULL;
    // find if pm_free_list has available block
    while(block != NULL) {
        if(block->size >= size) {
            if (prev != NULL) {
                prev->next = block->next;
            } else {
                pm_free_list = block->next;
            }
            // try to add remained space to free list if it's bigger 
            // than a single header
            uint32_t remainder = block->size - size;
            if(remainder > sizeof(pm_heap_block)) {
                pm_heap_block* new_block = (pm_heap_block*)(get_block_address(block) + size);
                new_block->size = remainder;
                new_block->offset = block->offset + size;
                new_block->index = 0;
                pm_add_block_to_free_list(new_block);
            }else {
                size += remainder;
            }
            block->size = size;
            block->index = 0;
            init_block(block);
            int32_t VA = getVA(getVN(get_block_PN(block), 0), block->offset);
            return VA;
        }
        prev = block;
        block = block->next;
    }
    // allocate space in disk if not enough
    if (pm_heap_offset + size > PM_HEAP_SIZE) {
        if(is_full) {
            printf("exceeding total memory!\n");
            return -1;
        }
        pop_to_file();
        update_mapping(getVN(evict_index, 0), file_ptr_tail, 1);
        update_mapping(vm_map_index++, evict_index, 0);
        pm_add_page_to_free_list(evict_index);
        evict_index = (evict_index + 1) % PM_PAGE_NUM;
        file_ptr_tail = (file_ptr_tail + 1) % (PM_MAP_SIZE / 4 - PM_PAGE_NUM);
        if(file_ptr_head == file_ptr_tail) is_full = 1;
        return _pm_malloc(size - sizeof(pm_heap_block));
    }
    // allocate new free space in pm_heap
    block = (pm_heap_block*)(pm_heap + pm_heap_offset);
    block->size = size;
    block->offset = 0;
    block->index = 0;
    pm_heap_offset = pm_align(pm_heap_offset + size);
    // if remained space on the page is larger
    // then a single page header, add it to free list
    uint32_t remainder = PM_PAGE_SIZE - size;
    if(remainder > sizeof(pm_heap_block)) {
        pm_heap_block* new_block = (pm_heap_block*)(get_block_address(block) + size);
        new_block->size = remainder;
        new_block->offset = size;
        new_block->index = 0;
        pm_add_block_to_free_list(new_block);
    }else {
        size += remainder;
        block->size = size;
    }
    init_block(block);
    int32_t VA = getVA(getVN(get_block_PN(block), 0), block->offset);
    return VA;
}

static void _pm_free(int32_t VA) {
    uint32_t VN = VA >> 12;
    uint32_t offset = VA & 0xfff;
    uint32_t PN = vm_map[VN] & 0xffff;
    if(is_present(VN)) {
        if(on_file(VN)) {
            evict_and_load(PN);
            _pm_free(VA);
        }else {
            pm_heap_block* block = (pm_heap_block*)(pm_heap + PN * PM_PAGE_SIZE + offset);
            block->index = 0;
            pm_add_block_to_free_list(block);
        }
    }
}

static char _get_char(int32_t VA, int index) {
    uint32_t VN = VA >> 12;
    uint32_t offset = VA & 0xfff;
    uint32_t PN = vm_map[VN] & 0xffff;
    if(is_present(VN)) {
        if(on_file(VN)) {
            evict_and_load(PN);
            return _get_char(VA, index);
        }
        pm_heap_block* block = (pm_heap_block*)(pm_heap + PN * PM_PAGE_SIZE + offset);
        if(index + sizeof(pm_heap_block) >= block->size) {
            printf("Index %d out of range %lu\n", index, block->size - sizeof(pm_heap_block));
            return '\0';
        }
        return *((char*)block + sizeof(pm_heap_block) + index);
    }else {
        printf("Address not correct\n");
        return '\0';
    }
}

static int _set_char(int32_t VA, int index, char c) {
    uint32_t VN = VA >> 12;
    uint32_t offset = VA & 0xfff;
    uint32_t PN = vm_map[VN] & 0xffff;
    if(is_present(VN)) {
        if(on_file(VN)) {
            evict_and_load(PN);
            return _set_char(VA, index, c);
        }
        pm_heap_block* block = (pm_heap_block*)(pm_heap + PN * PM_PAGE_SIZE + offset);
        if(index + sizeof(pm_heap_block) >= block->size) {
            printf("Index %d out of range %lu\n", index, block->size - sizeof(pm_heap_block));
            return 0;
        }
        *((char*)block + sizeof(pm_heap_block) + index) = c;
        return 1;
    }else {
        printf("Address not correct\n");
        return 0;
    }
}

void pm_init(){
    system("sudo rm swap.data");
    system("sudo touch swap.data");
    system("sudo rm -rf free_lists");
    system("sudo mkdir free_lists");
    pthread_mutex_init(&lock, NULL);
}

int32_t pm_malloc(uint32_t size) {
    pthread_mutex_lock(&lock);
    int32_t VA = _pm_malloc(size);
    pthread_mutex_unlock(&lock);
    return VA;
}

void pm_free(int32_t VA) {
    pthread_mutex_lock(&lock);
    _pm_free(VA);
    pthread_mutex_unlock(&lock);
}

char get_char(int32_t VA, int index) {
    pthread_mutex_lock(&lock);
    char c = _get_char(VA, index);
    pthread_mutex_unlock(&lock);
    return c;
}

int set_char(int32_t VA, int index, char c) {
    pthread_mutex_lock(&lock);
    char success = _set_char(VA, index, c);
    pthread_mutex_unlock(&lock);
    return success;
}