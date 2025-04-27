// set in Makefile
#include DEVICE_CONFIGURATION
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <libflush/libflush.h>
#include <linux/perf_event.h>
#include <asm/unistd.h>
#include <sys/ioctl.h>

#define MMAP_SIZE 0x2000
#define EVICT_ACCESS_USLEEP 500
#define TIME_RELOAD_USLEEP EVICT_ACCESS_USLEEP*3.33
#define TIMING_SAMPLES 500

enum OP {
    EVICT,  // evict cache lines each EVICT_USLEEP us
    TIME,   // time accesses each EVICT_USLEEP*5
    ACCESS, // access cache lines each EVICT_USLEEP us
    FLUSH_RELOAD, // flush, wait EVICT_USLEEP*5 and time reload
};

struct Params {
    enum OP op;
    size_t *cache_lines;
    size_t count;
} params;

struct ProgramData {
    int ipc_fd;
    void *ipc_mmap;
#if PERF == 1
    int perf_fd;
    int perf_ref_fd;
#endif
} data;

// Moves cursor to beginning of the previous line
const char CPL[] = "\033[F";
static volatile atomic_bool stop = false;
libflush_session_t* libflush_session;
uint8_t dummy_value;

/** parse params and save them to 'params' global struct */
int parse_params(int argc, char **argv);
/**
 * prepare program, open file descriptors, mmap them and save them in 'data'
 * global struct
 */
int prepare();
void cleanup();
/**
 * evict/flush all cache lines in params.cache_lines.
 * Cache line <n> == addr[n*LINE_LENGTH]
 */
void evict_lines(uint8_t *addr);
/** Access all cache lines in params.cache_lines */
void access_lines(volatile uint8_t *addr);
uint64_t time_access(volatile uint8_t *addr);
void intHandler(int dummy);
/** push new element to the top of array, remove last */
void push(void *arr, size_t count, size_t el_size, void *element);
int cmp_uint64(const void* a, const void* b);
int add_element_get_median(uint64_t *timings, uint64_t new_time);

long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                     int cpu, int group_fd, unsigned long flags)
{
    return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

int main(int argc, char **argv) {
    signal(SIGINT, intHandler);
    if (parse_params(argc, argv)) {
        return -1;
    }
    if (prepare()) {
        return -1;
    }

    uint64_t timings[TIMING_SAMPLES] = { 0 };
    uint64_t median_baseline = 0;
    uint64_t median = 0;
#if PERF == 1
    long long cache_hit_miss = 0;
    long long cache_hit_miss_total = 0;
    long long cache_access = 0;
    long long cache_access_total = 0;
#endif

    if (params.op == TIME) {
        printf("Calculating median baseline. Don't evict.\n");
        for (int i = 0; i < TIMING_SAMPLES; ++i) {
            median_baseline = add_element_get_median(timings, time_access(data.ipc_mmap));
        }
        printf("Calculated median time: %lu\n", median_baseline);
    }

    while(!stop) {
        switch(params.op) {
            case EVICT:
                evict_lines(data.ipc_mmap);
                usleep(EVICT_ACCESS_USLEEP);
                break;
            case TIME:
            #if PERF == 1
                ioctl(data.perf_fd, PERF_EVENT_IOC_RESET, 0);
                ioctl(data.perf_ref_fd, PERF_EVENT_IOC_RESET, 0);
                ioctl(data.perf_fd, PERF_EVENT_IOC_ENABLE, 0);
                ioctl(data.perf_ref_fd, PERF_EVENT_IOC_ENABLE, 0);
            #endif
                uint64_t time = time_access(data.ipc_mmap);
            #if PERF == 1
                ioctl(data.perf_ref_fd, PERF_EVENT_IOC_DISABLE, 0);
                ioctl(data.perf_fd, PERF_EVENT_IOC_DISABLE, 0);
                read(data.perf_fd, &cache_hit_miss, sizeof(long long));
                read(data.perf_ref_fd, &cache_access, sizeof(long long));
                cache_hit_miss_total += cache_hit_miss;
                cache_access_total += cache_access;
            #endif
                median = add_element_get_median(timings, time);
            #if PERF == 1
                    printf(CPL);
            #endif
                if (median > median_baseline)
                    printf("\rMedian time diff from baseline:  %lu    ", median - median_baseline);
                else
                    printf("\rMedian time diff from baseline: -%lu    ", median_baseline - median);
            #if PERF == 1
                    printf("\nCache misses: %llu (%.2f%%)    ",
                        cache_access_total - cache_hit_miss_total,
                        (float)cache_access_total/cache_hit_miss_total);
            #endif
                usleep(TIME_RELOAD_USLEEP);
                break;
            case ACCESS:
                access_lines(data.ipc_mmap);
                usleep(EVICT_ACCESS_USLEEP);
                break;
            case FLUSH_RELOAD:
                break;
        }
    }

    printf("\nStopping\n");
    printf("Dummy value: %u\n", (unsigned int)dummy_value);
    return 0;
}

void intHandler(int dummy) {
    if (stop) {
        exit(1);
    }
    stop = true;
}

int parse_params(int argc, char **argv) {
    char usage_str[] = "%s <evict|time> <cache_line> [cache_line]...\n";
    if (argc < 3) {
        fprintf(stderr, usage_str, argv[0]);
        return -1;
    }

    if (strcmp(argv[1], "evict") == 0)
        params.op = EVICT;
    else if (strcmp(argv[1], "time") == 0)
        params.op = TIME;
    else {
        fprintf(stderr, usage_str, argv[0]);
        return -1;
    }
    params.count = argc - 2;
	params.cache_lines = malloc((params.count) * sizeof(size_t));
    for (int i = 0; i < params.count; ++i) {
        params.cache_lines[i] = strtoul(argv[i + 2], NULL, 10);
    }

    return 0;
}

int prepare() {
    printf("Opening /dev/crossconhypipc"IPC_ID"\n");
    data.ipc_fd = open("/dev/crossconhypipc"IPC_ID, O_RDWR);
    if (data.ipc_fd == -1) {
        perror("opening /dev/crossconhypipc"IPC_ID" failed");
        return -1;
    }
    printf("mmaping /dev/crossconhypipc"IPC_ID"\n");
    data.ipc_mmap = mmap(NULL, MMAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, data.ipc_fd, 0);
    if (data.ipc_mmap == MAP_FAILED) {
        perror("mmaping failed");
        return -1;
    }

    printf("Libflush init\n");
    if (libflush_init(&libflush_session, NULL) == false) {
		return -1;
	}

#if PERF == 1
    // we don't need perf for these operations
    if (params.op == EVICT || params.op == ACCESS)
        return 0;
    struct perf_event_attr pe;

    memset(&pe, 0, sizeof(struct perf_event_attr));
    /** if it doesn't work on rpi:
     * type=PERF_TYPE_HARDWARE,
     * config=PERF_COUNT_HW_CACHE_MISSES
     */
    pe.type = PERF_TYPE_HW_CACHE;
    pe.size = sizeof(struct perf_event_attr);
    pe.config = (PERF_COUNT_HW_CACHE_LL) | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
    pe.disabled = 1;
    pe.exclude_idle = 1;
    pe.exclude_guest = 1;
    pe.exclude_kernel = 1;
    pe.exclude_hv = 1;

    data.perf_fd = perf_event_open(&pe, 0, -1, -1, 0);
    if (data.perf_fd == -1) {
        perror("perf_event_open failed");
        return -1;
    }
    pe.config = (PERF_COUNT_HW_CACHE_LL) | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16);
    data.perf_ref_fd = perf_event_open(&pe, 0, -1, -1, 0);
    if (data.perf_ref_fd == -1) {
        perror("perf_event_open failed");
        return -1;
    }
#endif

    return 0;
}

void cleanup() {
    free(params.cache_lines);
    munmap(data.ipc_mmap, MMAP_SIZE);
    close(data.ipc_fd);
#if PERF == 1
    if(params.op != EVICT && params.op != ACCESS) {
        close(data.perf_fd);
        close(data.perf_ref_fd);
    }
#endif
    if (libflush_terminate(libflush_session) == false) {
		fprintf(stderr, "libflush_terminate failed\n");
	}
}

void evict_lines(uint8_t *addr) {
    for (int i=0; i<params.count; i++) {
        libflush_flush(libflush_session, addr + params.cache_lines[i]*LINE_LENGTH);
    }
}

void access_lines(volatile uint8_t *addr) {
    for (int i=0; i<params.count; i++) {
        dummy_value ^= *(addr + params.cache_lines[i]*LINE_LENGTH);
        // libflush_access_memory(addr + params.cache_lines[i]*LINE_LENGTH);
    }
}

uint64_t time_access(volatile uint8_t *addr) {
    libflush_memory_barrier();
    uint64_t time = libflush_get_timing(libflush_session);
    libflush_memory_barrier();
    for (int i=0; i<params.count; i++) {
        dummy_value ^= *(addr + params.cache_lines[i]*LINE_LENGTH);
    }
    libflush_memory_barrier();
    time = libflush_get_timing(libflush_session) - time;
    return time;
}

void push(void *arr, size_t count, size_t el_size, void *element) {
    void *end = arr + (count - 1)*el_size;
    while (end > arr) {
        memcpy(end, end-el_size, el_size);
        end -= el_size;
    }
    memcpy(arr, element, el_size);
}

int cmp_uint64(const void* a, const void* b)
{
	uint64_t arg1 = *(const uint64_t*)a;
	uint64_t arg2 = *(const uint64_t*)b;
	return (arg1 > arg2) - (arg1 < arg2);
}

int add_element_get_median(uint64_t *timings, uint64_t new_time) {
    uint64_t timings_sorted[TIMING_SAMPLES];
    push(timings, TIMING_SAMPLES, sizeof(new_time), &new_time);
    memcpy(timings_sorted, timings, TIMING_SAMPLES*sizeof(new_time));
    qsort(timings_sorted, TIMING_SAMPLES, sizeof(new_time), cmp_uint64);
    return timings_sorted[TIMING_SAMPLES/2];
}
