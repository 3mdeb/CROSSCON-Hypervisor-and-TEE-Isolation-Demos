/*
 * Copyright (c) 2016, Linaro Limited
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <err.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdatomic.h>

/* OP-TEE TEE client API (built by optee_client) */
#include <tee_client_api.h>

/* For the UUID (found in the TA's h-file(s)) */
#include <security_test_ta.h>

#define LLC_SIZE 1024*1024
#define SHM_SIZE 1024*512
#define TEST_REPEAT 50

#define SEC_TO_NS(sec) ((sec)*1000000000)

typedef struct {
	TEEC_Context ctx;
	TEEC_Session sess;
	TEEC_SharedMemory shm;
	TEEC_Operation op;
} Tee_Data;

uint64_t dummy_value;

void flush_cache() {
	static const int allocate_amount = LLC_SIZE * 10;
	int* data = malloc(allocate_amount * sizeof(int));
	for (int i = 0; i < allocate_amount; ++i)
		data[i] = i;
	free(data);
}

uint64_t nanosec(struct timespec ts) {
	return SEC_TO_NS((uint64_t)ts.tv_sec) + (uint64_t)ts.tv_nsec;
}

/**
 * Returns ts2 - ts2 in nanoseconds
 */
uint64_t nano_diff(struct timespec ts1, struct timespec ts2) {
	uint64_t time1 = nanosec(ts1);
	uint64_t time2 = nanosec(ts2);
	// probably needs better way to deal with case where variable wraps around
	return time2 > time1 ? time2 - time1 : 0;
}

void prepare(Tee_Data *tee) {
	TEEC_Result res;
	TEEC_UUID uuid = TA_SECURITY_TEST_UUID;
	uint32_t err_origin;

	/* Initialize a context connecting us to the TEE */
	res = TEEC_InitializeContext(NULL, &tee->ctx);
	if (res != TEEC_SUCCESS)
		errx(1, "TEEC_InitializeContext failed with code 0x%x", res);

	/* Allocate shared memory */
	tee->shm.size = SHM_SIZE;
	tee->shm.flags = TEEC_MEM_INPUT | TEEC_MEM_OUTPUT;
	printf("Allocating %lu bytes of shared memory\n", tee->shm.size);
	res = TEEC_AllocateSharedMemory(&tee->ctx, &tee->shm);
	if (res != TEEC_SUCCESS)
		errx(1, "TEEC_AllocateSharedMemory failed with code 0x%x", res);
	/*
	 * Open a session to the "security test" TA
	 */
	res = TEEC_OpenSession(&tee->ctx, &tee->sess, &uuid,
			       TEEC_LOGIN_PUBLIC, NULL, NULL, &err_origin);
	if (res != TEEC_SUCCESS)
		errx(1, "TEEC_Opensession failed with code 0x%x origin 0x%x",
			res, err_origin);

	/* Clear the TEEC_Operation struct */
	memset(&tee->op, 0, sizeof(tee->op));

	/*
		* Prepare the argument. Pass a value in the first parameter,
		* the remaining three parameters are unused.
		*/
	tee->op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_PARTIAL_INPUT, TEEC_NONE,
					TEEC_NONE, TEEC_NONE);
	tee->op.params[0].memref.parent = &tee->shm;
	tee->op.params[0].memref.size = tee->shm.size;
}

uint64_t time_access(volatile uint8_t *addr) {
	struct timespec ts1;
	struct timespec ts2;
	int8_t tmp;
	atomic_thread_fence(memory_order_acquire);
	clock_gettime(CLOCK_MONOTONIC_RAW, &ts1);
	tmp = *addr;
	atomic_thread_fence(memory_order_release);
	clock_gettime(CLOCK_MONOTONIC_RAW, &ts2);
	dummy_value += tmp;
	return nano_diff(ts1, ts2);
}

void time_cache_access(uint8_t *buffer) {
	uint64_t cache_hit = 0, cache_miss = 0;
	for (int i = 0; i < TEST_REPEAT; ++i) {
		volatile uint8_t *data = buffer;
		*data = i;
		cache_hit += time_access(data);
	}

	for (int i = 0; i < TEST_REPEAT; ++i) {
		flush_cache();
		volatile uint8_t *data = buffer;
		cache_miss += time_access(data);
	}

	printf("Average cache hit time: %lu\n", cache_hit / TEST_REPEAT);
	printf("Average cache miss time: %lu\n", cache_miss / TEST_REPEAT);
}

/* Should be cache miss, otherwise shared buffer is accessed (probably copied) */
void time_nop_tee_command(Tee_Data *tee) {
	uint64_t access_time = 0;
	uint32_t err_origin;
	TEEC_Result res;
	for (int i = 0; i < TEST_REPEAT; ++i) {
		flush_cache();
		res = TEEC_InvokeCommand(&tee->sess, TA_SECURITY_TEST_CMD_DO_NOTHING,
					 &tee->op, &err_origin);
		if (res != TEEC_SUCCESS)
			errx(1, "TEEC_InvokeCommand failed with code 0x%x origin 0x%x",
				 res, err_origin);
		access_time += time_access((uint8_t*)tee->shm.buffer);
	}

	printf("Average shared memory access time: %lu\n", access_time / TEST_REPEAT);
}

void flush_and_reload(Tee_Data *tee) {
	uint64_t access_time = 0;
	uint32_t err_origin;
	TEEC_Result res;
	for (int i = 0; i < TEST_REPEAT; ++i) {
		flush_cache();
		res = TEEC_InvokeCommand(&tee->sess, TA_SECURITY_TEST_CMD_READ_MEM,
					 &tee->op, &err_origin);
		if (res != TEEC_SUCCESS)
			errx(1, "TEEC_InvokeCommand failed with code 0x%x origin 0x%x",
				 res, err_origin);
		access_time += time_access((uint8_t*)tee->shm.buffer);
	}

	printf("Flush+Reload: Average memory access time: %lu\n", access_time / TEST_REPEAT);
}

void evict_and_time(Tee_Data *tee) {
	struct timespec ts1, ts2;
	uint64_t time_no_evict = 0, time_evict = 0;
	uint32_t err_origin;
	TEEC_Result res;
	/**
	 * Calculate how long it takes process to finish when shared memory is
	 * in cache.
	 */
	dummy_value += *(uint64_t*)tee->shm.buffer;
	for (int i = 0; i < TEST_REPEAT; ++i) {
		atomic_thread_fence(memory_order_acquire);
		clock_gettime(CLOCK_MONOTONIC_RAW, &ts1);
		res = TEEC_InvokeCommand(&tee->sess, TA_SECURITY_TEST_CMD_READ_MEM,
			&tee->op, &err_origin);
		atomic_thread_fence(memory_order_release);
		clock_gettime(CLOCK_MONOTONIC_RAW, &ts2);
		if (res != TEEC_SUCCESS)
			errx(1, "TEEC_InvokeCommand failed with code 0x%x origin 0x%x",
				 res, err_origin);
		time_no_evict += nano_diff(ts1, ts2);
	}

	printf("Evict+Time: Average program time without evicting cache: %lu\n", time_no_evict / TEST_REPEAT);

	for (int i = 0; i < TEST_REPEAT; ++i) {
		flush_cache();
		atomic_thread_fence(memory_order_acquire);
		clock_gettime(CLOCK_MONOTONIC_RAW, &ts1);
		res = TEEC_InvokeCommand(&tee->sess, TA_SECURITY_TEST_CMD_READ_MEM,
			&tee->op, &err_origin);
		atomic_thread_fence(memory_order_release);
		clock_gettime(CLOCK_MONOTONIC_RAW, &ts2);
		if (res != TEEC_SUCCESS)
			errx(1, "TEEC_InvokeCommand failed with code 0x%x origin 0x%x",
				 res, err_origin);
		time_evict += nano_diff(ts1, ts2);
	}

	printf("Evict+Time: Average program time with evicted cache: %lu\n", time_evict / TEST_REPEAT);
}

void prime_and_probe(Tee_Data *tee) {
	uint64_t access_time = 0;
	uint32_t err_origin;
	TEEC_Result res;
	uint8_t *data_in_cache = malloc(LLC_SIZE);
	uint64_t no_access_time_per_line[LLC_SIZE / 64] = { 0 };
	uint64_t access_time_per_line[LLC_SIZE / 64] = { 0 };

	// test which lines are evicted when TA doesn't access it's own dummy
	// memory
	for (int i = 0; i < TEST_REPEAT; ++i) {
		// check each line separately as due to replacement policy we
		// might evict our own data if we try to test whole cache in one
		// go
		for (size_t line = 0; line < LLC_SIZE / 64; ++line) {
			// fill cache with our data
			for (int i = 0; i < LLC_SIZE; ++i) {
				dummy_value += data_in_cache[i];
			}
			res = TEEC_InvokeCommand(&tee->sess, TA_SECURITY_TEST_CMD_DO_NOTHING,
						&tee->op, &err_origin);
			if (res != TEEC_SUCCESS)
				errx(1, "TEEC_InvokeCommand failed with code 0x%x origin 0x%x",
					res, err_origin);
			no_access_time_per_line[line] += time_access(data_in_cache + line * 64);
		}
	}

	// test which lines are evicted when TA accesses it's own dummy memory
	for (int i = 0; i < TEST_REPEAT; ++i) {
		// check each line separately as due to replacement policy we
		// might evict our own data if we try to test whole cache in one
		// go
		for (size_t line = 0; line < LLC_SIZE / 64; ++line) {
			// fill cache with our data
			for (int i = 0; i < LLC_SIZE; ++i) {
				dummy_value += data_in_cache[i];
			}
			res = TEEC_InvokeCommand(&tee->sess, TA_SECURITY_TEST_CMD_ACCESS_INTERNAL_MEMORY,
						&tee->op, &err_origin);
			if (res != TEEC_SUCCESS)
				errx(1, "TEEC_InvokeCommand failed with code 0x%x origin 0x%x",
					res, err_origin);
			access_time_per_line[line] += time_access(data_in_cache + line * 64);
		}
	}

	printf("Prime+Probe: Average line access time difference\n");
	for (size_t line; line < LLC_SIZE / 64; ++line) {
		printf("Line %lu:\t", line);
		if (access_time_per_line[line] > no_access_time_per_line[line]) {
			access_time = access_time_per_line[line] - no_access_time_per_line[line];
		} else {
			access_time = no_access_time_per_line[line] - access_time_per_line[line];
			printf("-");
		}
		printf("%lu\n", access_time);
	}

	free(data_in_cache);
}

int main(void)
{
	Tee_Data tee = {};

	printf("Prepare program\n");
	prepare(&tee);
	fflush(NULL);

	/**
	 * Some base statistics, how long average cache hit/miss takes and
	 * whether passing shared memory buffer results in it being
	 * copied/accessed.
	 */
	printf("Time average time of cache hit and cache miss when accessing shared memory\n");
	time_cache_access((uint8_t*)tee.shm.buffer);
	fflush(NULL);
	printf("\nCheck whether passing shared buffer results in it being copied\n");
	time_nop_tee_command(&tee);
	fflush(NULL);

	/* Test cases */
	printf("\nFlush+Reload:\n");
	flush_and_reload(&tee);
	fflush(NULL);

	printf("\nEvict+Time:\n");
	evict_and_time(&tee);
	fflush(NULL);

	// printf("\nPrime+Probe:\n");
	// prime_and_probe(&tee);

	/* End test cases */

	/*
	 * We're done with the TA, close the session and
	 * destroy the context.
	 */
	TEEC_CloseSession(&tee.sess);
	TEEC_ReleaseSharedMemory(&tee.shm);
	TEEC_FinalizeContext(&tee.ctx);

	// Print dummy value to make sure compiler doesn't optimize out
	// instructions without side effects
	printf("Dummy value: %lu\n", dummy_value);

	return 0;
}
