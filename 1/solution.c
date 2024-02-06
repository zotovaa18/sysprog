#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include "libcoro.h"


struct my_context {
	char *name;
	
	char *inputFile;
	int **array;
	int *size;
	int sec_start;
	int nsec_start;
	int sec_finish;
	int nsec_finish;
    double time;
};

static struct my_context *
my_context_new(const char *name, char *inputFile, int** array_p, int* size_p) {
	struct my_context *ctx = malloc(sizeof(*ctx));
	ctx->name = strdup(name);
	ctx->inputFile = inputFile;
	ctx->array = array_p;
	ctx->size = size_p;
	ctx->time = 0;
	return ctx;
}

static void
my_context_delete(struct my_context *ctx) {
    free(ctx->name);
    free(ctx);
}

static void
swap(int *a, int *b) {
    int temp = *a;
    *a = *b;
    *b = temp;
}

static int partition(int arr[], int low, int high) {
    int pivot = arr[high];
    int i = (low - 1);
    for (int j = low; j <= high - 1; j++) {
        if (arr[j] < pivot) {
            i++;
            swap(&arr[i], &arr[j]);
        }
    }
    swap(&arr[i + 1], &arr[high]);
    return (i + 1);
}

static void quickSort(int arr[], int low, int high, struct my_context *ctx) {
    if (low < high) {
        int pivotIndex = partition(arr, low, high);
        quickSort(arr, low, pivotIndex - 1, ctx);
        quickSort(arr, pivotIndex + 1, high, ctx);

		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		ctx->sec_finish = now.tv_sec;
		ctx->nsec_finish = now.tv_nsec;

		ctx->time += (ctx->sec_finish - ctx->sec_start) * 1e9;
		ctx->time += (ctx->nsec_finish - ctx->nsec_start) * 1e-6;

		coro_yield();

		clock_gettime(CLOCK_MONOTONIC, &now);
		ctx->sec_start = now.tv_sec;
		ctx->nsec_start = now.tv_nsec;
    }
}


static int
coroutine_func_f(void *context) {
	struct coro *this = coro_this();
    struct my_context *ctx = context;

	struct timespec start, finish;
	clock_gettime(CLOCK_MONOTONIC, &start);
	ctx->sec_start = start.tv_sec;
	ctx->nsec_start = start.tv_nsec;

	FILE *file = fopen(ctx->inputFile, "r");
	if (file == NULL) {
		printf("Error opening file");
		return 1;
	}

	int capacity = 1000;
	*ctx->array = malloc(capacity * sizeof(int));
	*ctx->size = 0;

	int num;
	while (fscanf(file, "%d", &num) == 1) {
		if (*ctx->size == capacity) {
			capacity *= 2;
			*ctx->array = realloc(*ctx->array, capacity * sizeof(int));
		}
		(*ctx->array)[*ctx->size] = num;
		(*ctx->size)++;
	}
	
	quickSort(*ctx->array, 0, *ctx->size - 1, ctx);
	fclose(file);

	clock_gettime(CLOCK_MONOTONIC, &finish);

	ctx->time += (finish.tv_sec - ctx->sec_start) * 1e9;
	ctx->time += (finish.tv_nsec - ctx->nsec_start) * 1e-6;

	printf("%s: switch count %lld, work time: %f ms\n", ctx->name, coro_switch_count(this), ctx->time);

	my_context_delete(ctx);
	return 0;
}


int
main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s <file1> <file2> ...\n", argv[0]);
        return 1;
    }

    struct timespec start;
	clock_gettime(CLOCK_MONOTONIC, &start);
	
	// Initialize our coroutine global cooperative scheduler.
    coro_sched_init();

	int coroNum = argc - 1;
	
	int *arrays[coroNum];
	int sizes[coroNum];
	
	for (int i = 0; i < coroNum; i++) {
		char name[16]; 
		sprintf(name, "coro_%d", i); 
		coro_new(coroutine_func_f, my_context_new(name, argv[i + 1], &arrays[i], &sizes[i])); 
	}

	struct coro* c;
	while ((c = coro_sched_wait()) != NULL){
		coro_delete(c);
	}


	FILE *outputFile = fopen("output.txt", "w");
	if (outputFile == NULL) {
		printf("Error opening file output.txt");
		return 1;
	}
	
	int *indices = calloc(coroNum, sizeof(int));
	int minIndex, minVal;

	while (1) {
		minIndex = -1;
		minVal = INT_MAX;

		for (int i = 0; i < argc - 1; i++) {
			if (indices[i] < sizes[i] && arrays[i][indices[i]] < minVal) {
				minVal = arrays[i][indices[i]];
				minIndex = i;
			}
		}

		if (minIndex == -1) break;

		fprintf(outputFile, "%d ", minVal);
		indices[minIndex]++;
	}
	fclose(outputFile);

	for (int i = 0; i < argc -1; i++) {
		free(arrays[i]);
	}

	free(indices);

	struct timespec finish;
	clock_gettime(CLOCK_MONOTONIC, &finish);

    double time_taken = (finish.tv_sec - start.tv_sec) * 1e9; 
    time_taken = (time_taken + (finish.tv_nsec - start.tv_nsec)) * 1e-6; 
    printf("Total time: %f ms\n", time_taken);

	
    return 0;
}