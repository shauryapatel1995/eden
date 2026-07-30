/*
 * test_rmem_touch.c - allocates memory via rmalloc() and touches it beyond
 * the local memory threshold to exercise eviction to a remote memory server
 */

#include <errno.h>
#include <stdio.h>

#include <base/log.h>
#include <base/assert.h>
#include <rmem/api.h>
#include <runtime/thread.h>

#define ALLOC_SIZE  (32UL * 1024 * 1024)
#define STRIDE      4096

static void main_handler(void *arg)
{
	char *buf;
	size_t i;
	size_t mismatches = 0;

	log_info("rmalloc: allocating %lu bytes", ALLOC_SIZE);
	buf = rmalloc(ALLOC_SIZE);
	BUG_ON(!buf);

	log_info("writing buffer (forces local faults + eviction to remote)");
	for (i = 0; i < ALLOC_SIZE; i += STRIDE)
		buf[i] = (char)(i & 0xff);

	log_info("reading buffer back (forces remote fetches)");
	for (i = 0; i < ALLOC_SIZE; i += STRIDE) {
		if (buf[i] != (char)(i & 0xff))
			mismatches++;
	}

	if (mismatches)
		log_err("FAILED: %lu mismatches found", mismatches);
	else
		log_info("PASSED: all pages read back correctly");
}

int main(int argc, char *argv[])
{
	int ret;

	if (argc < 2) {
		printf("arg must be config file\n");
		return -EINVAL;
	}

	ret = runtime_init(argv[1], main_handler, NULL);
	if (ret) {
		printf("failed to start runtime\n");
		return ret;
	}

	return 0;
}
