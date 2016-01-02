/* A simple test harness for memory alloction. */

#include "mm_alloc.h"
#include <stdio.h>
#include <sys/time.h>
#include <sys/resource.h>

void sanity_test(void);
void big_test(void);
void realloc_test(void);
void fusion_test(void);

int main(int argc, char **argv)
{
	sanity_test();
	fusion_test();
	realloc_test();
	big_test();
    return 0;
}

void sanity_test(void) {
	int *data;
    data = (int*) mm_malloc(50);
    data[0] = 1;
    mm_free(data);
    printf("malloc sanity test successful!\n");
}

void big_test(void) {
	int *data;
	struct rlimit lim;
	lim.rlim_cur = 5000L;
	lim.rlim_max = 5000L;
    setrlimit(RLIMIT_DATA, &lim);
    data = (int*) mm_malloc(6000);
    if (data) {
		printf("big test FAILED!\n");
    } else {
    	printf("big test successful!\n");
    }
}

void realloc_test(void) {
	int *data;
	int *new_data;
    data = (int*) mm_malloc(4);
    data[0] = 1;
    data[1] = 3;
    printf("old %d\n", data[0]);
    printf("old %d\n", data[1]);
    new_data = (int*) mm_realloc(data, 8);
    printf("new %d\n", new_data[0]);
    printf("new %d\n", new_data[1]);
    if (new_data[0] == 1 && new_data[1] == 3) {
    	printf("realloc test successful!\n");
    } else {
    	printf("realloc test FAILED!\n");
    }
}

void fusion_test(void) {
	struct rlimit lim;
	lim.rlim_cur = 4000L;
	lim.rlim_max = 4000L;
    setrlimit(RLIMIT_DATA, &lim);
    int* data1 = (int*) mm_malloc(1000);
    if (!data1) printf("data1 shouldn't fail\n");
    int* data2 = (int*) mm_malloc(1000);
    if (!data2) printf("data2 shouldn't fail\n");
    int* data3 = (int*) mm_malloc(1000);
    if (!data3) printf("data3 shouldn't fail\n");
    int* data4 = (int*) mm_malloc(1000);
    if (!data4) printf("data4 should fail\n");
    mm_free(data3);
    int* test1 = (int*) mm_malloc(2000); 
    if (!test1) printf("test1 should fail\n");
    mm_free(data2);
    mm_free(data1); 
    int* test2 = (int*) mm_malloc(2000); 

    if (!test2) {
		printf("fusion test FAILED!\n");
    } else {
    	printf("fusion test successful!\n");
    }
}


