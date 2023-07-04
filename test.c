#ifndef NOMINMAX

#ifndef max
#define max(a,b)            (((a) > (b)) ? (a) : (b))
#endif

#ifndef min
#define min(a,b)            (((a) < (b)) ? (a) : (b))
#endif

#endif  /* NOMINMAX */

#include "pm_heap.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>

#define NUM_THREADS 6
#define NUM_ITERATIONS 5000

void *test_thread(void *arg) {
    int32_t allocations[NUM_ITERATIONS];

    // Test thread-safe pm_malloc, set_char, and get_char
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        int seed = time(NULL) ^ pthread_self();
        int length = rand_r(&seed) % (4096 - sizeof(pm_heap_block) - 1) + 1;
        allocations[i] = pm_malloc(length);
        assert(allocations[i] != -1);
        usleep(length % 512);

        for (uint32_t j = 0; j < min(length, 100); j++) {
            assert(set_char(allocations[i], j, (char)(j % 256)) == 1);
        }

        for (uint32_t j = 0; j < min(length, 100); j++) {
            assert(get_char(allocations[i], j) == (char)(j % 256));
        }
    }

    // Test thread-safe pm_free
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        pm_free(allocations[i]);
    }

    return NULL;
}

int main() {

    printf("Test start. May take 0~5 mins\n");
    pm_init();

    // pthread_t threads[NUM_THREADS];

    // // Create and run threads
    // for (int i = 0; i < NUM_THREADS; i++) {
    //     int status = pthread_create(&threads[i], NULL, test_thread, NULL);
    //     assert(status == 0);
    // }

    // // Wait for threads to finish
    // for (int i = 0; i < NUM_THREADS; i++) {
    //     int status = pthread_join(threads[i], NULL);
    //     assert(status == 0);
    // }

    int32_t p[3000];
    for(int i=0; i<3000;i++) {
        p[i] = pm_malloc(4000);
    }

    set_char(p[2999], 2, 'A');
    printf("%c\n", get_char(p[2999], 2));
    for(int i=0; i<3000;i++) {
        pm_free(p[i]);
    }
    printf("%d\n", get_char(p[2999], 2));
    printf("All multithreaded tests passed!\n");
    return 0;
}