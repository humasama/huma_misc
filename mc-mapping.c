#define _GNU_SOURCE             /* See feature_test_macros(7) */
#include <sched.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/time.h>
#include <time.h>

#include <inttypes.h>

#include <sys/mman.h>
#include <fcntl.h>

#include <sys/types.h>
#include <errno.h>
#include <sys/resource.h>
#include <assert.h>

/**************************************************************************
 * Public Definitions
 **************************************************************************/
#define L3_NUM_WAYS   16                    // cat /sys/devices/system/cpu/cpu0/cache/index3/ways..
#define NUM_ENTRIES   (uint64_t)(L3_NUM_WAYS * 2)       // # of list entries to iterate
#define ENTRY_SHIFT   (17)                  // [27:23] bits are used for iterations? interval:32MB
#define ENTRY_DIST    (uint64_t)(1<<ENTRY_SHIFT)      // distance between the two entries
#define CACHE_LINE_SIZE 64

#define MAX(a,b) ((a>b)?(a):(b))
#define CEIL(val,unit) (((val + unit - 1)/unit)*unit)

#define FATAL do { fprintf(stderr, "Error at line %d, file %s (%d) [%s]\n", \
   __LINE__, __FILE__, errno, strerror(errno)); exit(1); } while(0)

/**************************************************************************
 * Public Types
 **************************************************************************/

/**************************************************************************
 * Global Variables
 **************************************************************************/
static uint64_t g_mem_size = (uint64_t)((NUM_ENTRIES) * (ENTRY_DIST));
/* bank data */
static int* list;
/* index array for accessing banks */
static uint64_t indices[NUM_ENTRIES];
static uint64_t next;

/**************************************************************************
 * Public Function Prototypes
 **************************************************************************/
uint64_t get_elapsed(struct timespec *start, struct timespec *end)
{
	uint64_t dur;
	if (start->tv_nsec > end->tv_nsec)
		dur = (uint64_t)(end->tv_sec - 1 - start->tv_sec) * 1000000000 +
			(1000000000 + end->tv_nsec - start->tv_nsec);
	else
		dur = (uint64_t)(end->tv_sec - start->tv_sec) * 1000000000 +
			(end->tv_nsec - start->tv_nsec);

	return dur;

}

/**************************************************************************
 * Implementation
 **************************************************************************/
//using array accesses phy address
uint64_t run(uint64_t iter)
{
	uint64_t i, j = 0;
	uint64_t cnt = 0;
	int data;

	for (i = 0; i < iter; i++) {
		data = list[next];
		next = indices[j];

		j ++;
		if(j == NUM_ENTRIES) j = 0;
		cnt ++;
	}
	return cnt;
}


int main(int argc, char* argv[])
{
	struct sched_param param;
        cpu_set_t cmask;
	int num_processors;
	int cpuid = 0;
	int use_dev_mem = 0;

	int *memchunk = NULL;
	int opt, prio;
	int i,j;

	uint64_t repeat = 1000;

	int page_shift = 0;
	int xor_page_shift = 0;

	/*
	 * get command line options 
	 */
	while ((opt = getopt(argc, argv, "a:xb:s:o:m:c:i:l:h")) != -1) {
		switch (opt) {
		case 'b': /* bank bit */
			page_shift = strtol(optarg, NULL, 0);
			break;
		case 's': /* xor-bank bit */
			xor_page_shift = strtol(optarg, NULL, 0);
			break;
		case 'm': /* set memory size */
			g_mem_size = 1024 * strtol(optarg, NULL, 0);
			break;
		case 'x': /* mmap to /dev/mem, owise use hugepage */
			use_dev_mem = 1;
			break;
		case 'c': /* set CPU affinity */
			cpuid = strtol(optarg, NULL, 0);
			num_processors = sysconf(_SC_NPROCESSORS_CONF);
			CPU_ZERO(&cmask);
			CPU_SET(cpuid % num_processors, &cmask);
			if (sched_setaffinity(0, num_processors, &cmask) < 0)
				perror("error");
			break;
		case 'p': /* set priority */
			prio = strtol(optarg, NULL, 0);
			if (setpriority(PRIO_PROCESS, 0, prio) < 0)
				perror("error");
			break;
		case 'i': /* iterations */
			repeat = (uint64_t)strtol(optarg, NULL, 0);
			printf("repeat=%lu\n", repeat);
			break;
		}

	}

	printf("xor_page_shift : %d -------------\n", xor_page_shift);

	//g_mem_size += (1 << page_shift);
	g_mem_size += (1 << page_shift) + (1 << xor_page_shift); 	//test
	g_mem_size = CEIL(g_mem_size, ENTRY_DIST);

	/* alloc memory. align to a page boundary */
	if (use_dev_mem) {
		int fd = open("/dev/mem", O_RDWR | O_SYNC);
		void *addr = (void *) 0x1000000080000000;

		if (fd < 0) {
			perror("Open failed");
			exit(1);
		}

		memchunk = mmap(0, g_mem_size,
				PROT_READ | PROT_WRITE, 
				MAP_SHARED, 
				fd, (off_t)addr);
	} else {
		memchunk = mmap(0, g_mem_size,
				PROT_READ | PROT_WRITE, 
				MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, 
				-1, 0);
	}

	if (memchunk == MAP_FAILED) {
		perror("failed to alloc");
		exit(1);
	}

	int off_idx = (1<<page_shift) / 4;
	
	if (xor_page_shift > 0) {
		off_idx = ((1<<page_shift) + (1<<xor_page_shift)) / 4;
	}

#if 0
	/* just test bits smaller than ENTRY_SHIFT */ 
	if (page_shift >= ENTRY_SHIFT || xor_page_shift >= ENTRY_SHIFT) {
		fprintf(stderr, "page_shift or xor_page_shift must be less than %d bits\n",
				ENTRY_SHIFT);
		exit(1);
	} 
#endif

#if defined(TEST)
	printf("/////////////////ENTRY_DIST : %lu\n////////////////", ENTRY_DIST);
	printf("/////////////////NUM_ENTRIES : %lu\n////////////////", NUM_ENTRIES);
	printf("/////////////////NUM_ENTRIES * ENTRY_DIST : %lu\n////////////////", NUM_ENTRIES * ENTRY_DIST);
	printf("/////////////////g_mem_size = NUM_ENTRIES * ENTRY_DIST : %lu\n////////////////", g_mem_size);
#endif
	list = &memchunk[off_idx];
	for (i = 0; i < NUM_ENTRIES; i++) {
		if (i == (NUM_ENTRIES - 1))
			indices[i] = 0;
		else
			indices[i] = (i + 1) * ENTRY_DIST/4;
	}
	next = 0;
	printf("pshift: %d, XOR-pshift: %d\n", page_shift, xor_page_shift);

	struct timespec start, end;

	clock_gettime(CLOCK_REALTIME, &start);

	/* access banks */
	uint64_t naccess = run(repeat);

	clock_gettime(CLOCK_REALTIME, &end);

	int64_t nsdiff = get_elapsed(&start, &end);
	double  avglat = (double)nsdiff/naccess;

	printf("size: %ld (%ld KB)\n", g_mem_size, g_mem_size/1024);
	printf("duration %ld ns, #access %ld\n", nsdiff, naccess);
	printf("average latency: %ld ns\n", nsdiff/naccess);
	printf("bandwidth %.2f MB/s\n", 64.0*1000.0*(double)naccess/(double)nsdiff);

	return 0;
}
