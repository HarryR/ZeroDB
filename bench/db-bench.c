/* Copyright (c) 2011, BohuTANG <overred.shuttler at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of nessDB nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* HarryR:
 * The series of benchmarks we need to provide should be comparable to the
 * LevelDB benchmarks, but flexible enough to apply to a load of different
 * database systems.
 *
 * The benchmarks should be used in-place of a test suite and should represent
 * many different use cases and scenarios.
 *
 * Reference at: http://code.google.com/p/leveldb/source/browse/db/db_bench.cc
 *
 * TODO: output CSV of results
 */

#define _POSIX_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include <err.h>
#include <sys/time.h>

#include "../server/db-zmq.h"

#define HEADER	"| Status               | OK Rate   | Response Tm | Throughput         | Bandwidth     | Time               |\n"
#define LINE1	"+----------------------+-----------+-------------+--------------------+---------------+--------------------+\n"
#define LINE	"+--------------------------------------------------------------------------------------------------------------+\n"

#define cycle32(i) (((i) >> 1) ^ (-((i) & 1u) & 0xD0000001u))

struct benchmark {
	char *name;
	size_t entries;
	char* key;
	size_t key_len;
	char* val;
	size_t val_len;
	size_t read_pct;

	uint32_t count;
	uint32_t ok_count;
	double cost;
	uint64_t io_bytes;

	struct timeval start;

	dbzop_t put;
	dbzop_t get;
	dbzop_t del;
	dbzop_t walk;
	dbzop_t flush;

	void (*controller)( struct benchmark* );
};
typedef struct benchmark benchmark_t;

struct benchmark_controller {
	char* name;
	void (*runner)( struct benchmark* );
};

/**
 * Runs operation `i` for benchmark `b` and returns total bytes of DB I/O
 */
typedef size_t (*benchmark_op_t)(struct benchmark *b);


static void
start_timer(struct timeval *start)
{
    gettimeofday(start, NULL);
}

static double
get_timer(struct timeval *start)
{
    struct timeval end;
    assert(start != NULL);
    gettimeofday(&end, NULL);
	return (double)(end.tv_sec - start->tv_sec) + (double)(end.tv_usec - start->tv_usec) / 1000000.0;
}

static void
fill_random( char* x, size_t len ) {
	size_t i;
	assert(x != NULL);
	for( i = 0; i < len; i++ ) {
		x[i] = '0' + (rand() % 40);
	}
}

static void
fill_pseudorandom( char* x, size_t len, benchmark_t *b ) {
	unsigned int i, rstate;
	assert(x != NULL);
	assert(b != NULL);
	rstate = (int)(b->count % (b->entries/4));
	for( i = 0; i < len; i++ ) {
		x[i] = (rstate + i % 0xFF);
		rstate = (rstate >> 1) ^ (-(rstate & 1u) & 0xD0000001u);
	}
}


static void
benchmark_reset( benchmark_t *self ) {
	assert(self != NULL);
	self->count = 0;
	self->ok_count = 0;
	self->io_bytes = 0;
	self->cost = 0.0;
}

static size_t
benchmark_bps(benchmark_t *self) {
	assert(self != NULL);
	return (self->io_bytes / (get_timer(&self->start) + 1));
}

DB_OP(count_value){
	(void)cb;(void)in_data;(void)in_sz;(void)token;
	return in_sz;
}

static void
benchmark_op(benchmark_t *self, uint32_t count, benchmark_op_t op, char *progress)
{
	uint32_t i;
	uint32_t prog_stop = (count / 50);

	assert( self != NULL );
	assert( op != NULL );
	assert( count > 0 );

	struct timeval start;
	start_timer(&start);

	for (i = 0; i < count; i++) {
		size_t io_bytes = op(self);
		self->count++;
		self->ok_count += (io_bytes>0 ? 1 : 0);
		self->io_bytes += io_bytes;
		if( progress && (i % (prog_stop + 1)) == 0) {
			fprintf(stderr, "%3zu%% @ %5.1fmb/s -- %-50s\r", self->count / (self->entries / 100), benchmark_bps(self) / 1024.0 / 1024.0, progress);
		}
	}

	self->cost += get_timer(&start);	
}


static void
benchmark_report(benchmark_t *self, const char* name) {
	assert(self != NULL);
	printf("| %-20s | %8.1f%% | %8.6f ms | %10.2f ops/sec | %5.1f MiB/sec | start + %6.1f sec |\n"
		,name
		,(double)(self->ok_count / (self->count / 100.0))
		,(double)(self->cost / self->count) * 1000
		,self->count / self->cost
		,benchmark_bps(self) / 1024.0 / 1024.0
		,self->cost);
}


static void
benchmark_run(benchmark_t *self) {
	assert(self != NULL);
	srand(time(NULL));
	printf(LINE1);
	printf(HEADER);
	start_timer(&self->start);
	self->controller(self);
	printf(LINE1);
}

static size_t
bop_null(benchmark_t* b) {
	return b != NULL;
}

static size_t
bop_read_random(benchmark_t* b) {
	assert(b != NULL);
	fill_random(b->key, b->key_len);
	return b->get(b->key, b->key_len, count_value, NULL);
}

static size_t
bop_write_random(benchmark_t* b) {
	assert(b != NULL);
	size_t pairsz = b->val_len+b->key_len;
	char* pair = malloc(pairsz);
	fill_random(pair, pairsz);
	size_t ret = b->put(pair, pairsz, NULL, NULL);
	free(pair);
	return ret;
}

static size_t
bop_write_pseudorand(benchmark_t* b) {
	assert(b != NULL);
	size_t pairsz = b->key_len + b->val_len;
	char* pair = malloc(pairsz);
	fill_pseudorandom(pair, pairsz, b);
	size_t retsz = b->put(pair, pairsz, NULL, NULL);
	free(pair);
	return retsz;
}

static size_t
bop_read_pseudorand(benchmark_t* b) {
	assert(b != NULL);
	fill_pseudorandom(b->key, b->key_len, b);
	return b->get(b->key, b->key_len, NULL, NULL);
}

static size_t
bop_write_sequence(benchmark_t* b) {
	assert(b != NULL);
	size_t pair_sz = b->key_len+b->val_len;
	char* pair = malloc(pair_sz);
	int i = b->count % (b->entries/100);
	memset(pair, 'X', pair_sz);
	snprintf(pair, b->key_len, "%X", i);
	snprintf(pair+b->key_len, b->val_len, "V%XA%XL%XU%XE%X", i, i, i, i, i);
	return b->put(pair, pair_sz, NULL, NULL);
}

static size_t
bop_read_sequence(benchmark_t* b) {
	assert(b != NULL);
	int i = b->count % (b->entries/100);
	snprintf(b->key, b->key_len, "%X", i);
	fill_pseudorandom(b->val, b->val_len, b);

	return b->get(b->key, b->key_len, NULL, NULL);
}

static size_t
bop_remove_sequence(benchmark_t* b) {
	assert(b != NULL);
	int i = b->count % (b->entries/100);
	memset(b->key, 'X', b->key_len);
	snprintf(b->key, b->key_len, "%X", i);
	return b->del(b->key, b->key_len, NULL, NULL);
}

static void
db_test_null( benchmark_t* b ) {
	assert(b != NULL);
	benchmark_op(b, b->entries, bop_null, "Doing nothing");
}

static void
run_test_rwmix( benchmark_t *b, size_t entries, benchmark_op_t readop_cb, benchmark_op_t writeop_cb ) {
	size_t i;
	size_t runs = 1000;
	size_t read_cnt = 0, write_cnt = 0;
	size_t entries_per_run = (entries/runs);
	assert(b != NULL);
	assert(readop_cb != NULL);
	b->key = malloc(b->key_len);
	memset(b->key, 'X', b->key_len);
	b->val = malloc(b->val_len);
	memset(b->val, 'X', b->val_len);
	for( i = 0; i < runs; i++ ) {
		if( b->read_pct >= (size_t)(rand() % 100) ) {			
			benchmark_op(b, entries_per_run, readop_cb, NULL);			
			read_cnt++;
		}
		else {
			benchmark_op(b, entries_per_run, writeop_cb, NULL);
			write_cnt++;
		}

		if( i != 0 && ((i % (runs/10) == 0) || i == runs) ) {			
			char buf[200];
			sprintf(buf, "R:%.1f%% / W:%.1f%%", read_cnt/(i/100.0), write_cnt/(i/100.0));

			if( b->flush ) {
				b->flush("Hello",4,NULL,NULL);
			}

			benchmark_report(b, buf);
		}
	}
	free(b->key);
	free(b->val);
	b->key = NULL;
	b->val = NULL;
}

static void
db_test_pseudorandom( benchmark_t* b ) {
	assert(b != NULL);
	run_test_rwmix(b, b->entries, bop_read_pseudorand, bop_write_pseudorand);
}

static void
db_test_removewrite( benchmark_t* b ) {
	assert(b != NULL);
	run_test_rwmix(b, b->entries, bop_remove_sequence, bop_write_sequence);
}

static void
db_test_sequence( benchmark_t* b ) {
	assert(b != NULL);
	run_test_rwmix(b, b->entries, bop_read_sequence, bop_write_sequence);
}

static void
db_test_random( benchmark_t *b ) {
	assert(b != NULL);
	run_test_rwmix(b, b->entries, bop_read_random, bop_write_random);
}

static struct benchmark_controller
available_benchmarks[] = {
	{"null", db_test_null},
	{"readwrite-pseudorandom", db_test_pseudorandom},
	{"removewrite-sequence", db_test_removewrite},
	{"readwrite-sequence", db_test_sequence},
	{"readwrite-random", db_test_random},
	{NULL, NULL}	
};

static bool
benchmark_validate( benchmark_t *b ) {
	static char* all = "all";
	assert(b != NULL);
	struct benchmark_controller* r = &available_benchmarks[0];
	if( b->name == NULL ) {
		warnx("No benchmark name specified, performing all");
		b->name = all;
	}
	else {
		while( r->name ) {
			if( ! strcmp(b->name, r->name) ) {
				b->controller = r->runner;
				break;
			}
			r++;
		}
	}

	if( r->name == NULL && strcmp("all",b->name) != 0 ) {
		warnx("Unknown benchmark name '%s'", b->name);
		return false;
	}

	if( b->read_pct > 100 ) b->read_pct = 100;

	if( ! b->get || ! b->put || ! b->del ) {
		warnx("Database does not support all basic operations needed");
		return false;
	}

	return (b->name != NULL)
		&& (b->entries > 100)
		&& (b->key_len > 0)
		&& (b->val_len > 0);
}

static void
print_environment(void)
{
	time_t now = time(NULL);
	printf("  Compiler:	%s\n", __VERSION__);
	printf("  Date:		%s", (char*)ctime(&now));
	
	int num_cpus = 0;
	char cpu_type[256] = {0};
	char cache_size[256] = {0};

	FILE* cpuinfo = fopen("/proc/cpuinfo", "r");
	if (cpuinfo) {
		char line[1024] = {0};
		while (fgets(line, sizeof(line), cpuinfo) != NULL) {
			const char* sep = strchr(line, ':');
			if (sep == NULL || strlen(sep) < 10)
				continue;
			
			char key[1024] = {0};
			char val[1024] = {0};
			strncpy(key, line, sep - 1 - line);
			strncpy(val, sep+1, strlen(sep)-1);
			if (strcmp("model name", key) == 0) {
				num_cpus++;
				strncpy(cpu_type, val, strlen(val) - 1);
			}
			else if(strcmp("cache size", key) == 0)
				strncpy(cache_size, val + 1, strlen(val) - 2);	
		}

		fclose(cpuinfo);
		printf("  CPU:		%d * %s\n", num_cpus, cpu_type);
		printf("  CPUCache:	%s\n", cache_size);
	}
}

static void
print_usage( char *prog ) {
	fprintf(stderr,
		"Usage: %s [options] <module.so> <benchmark-name>\n"
		"\t-r <pct> Workload read percentage %%\n"
		"\t-e <num> Number of DB entries (default: 500000)\n"
		"\t-k <num> Key size in bytes (default: 20)\n"
		"\t-v <num> Value size in bytes (default: 100)\n"
		"\t-c <mb>  Cache size in megabytes (default: 4)\n"
		"\n"
		"Benchmarks:\n", prog);
	
	struct benchmark_controller *b = &available_benchmarks[0];
	while( b->name ) {
		fprintf(stderr, "\t%s\n", b->name);
		b++;
	}
	fprintf(stderr, "\n");
}

int
main(int argc, char** argv)
{
	int c;
	benchmark_t bench = {
		.name = NULL,
		.read_pct = 50,
		.entries = 500000,
		.key_len = 20,
		.val_len = 100,
		.put = NULL,
		.get = NULL,
		.del = NULL,
	};
	benchmark_reset(&bench);

	while( (c = getopt(argc, argv, "d:e:k:v:c:r:")) != -1 ) {
		switch( c ) {
		case 'r':
			bench.read_pct = atoi(optarg);
			break;

		case 'e':
			bench.entries = atoi(optarg);
			break;

		case 'k':
			bench.key_len = atoi(optarg);
			break;

		case 'v':
			bench.val_len = atoi(optarg);
			break;	

		default:
			fprintf(stderr, "Unknown option -%c\n", c);
			break;
		}
	}

	const char *mod_file = NULL;
	if( optind < (argc-1) ) {
		bench.name = argv[optind + 1];
		mod_file = argv[optind];
	}

	void* dbz = NULL;
	if( mod_file ) {
		dbz = dbz_load(mod_file);
		if( ! dbz ) {
			return EXIT_FAILURE;
		}
		struct dbz_op *put_op = dbz_op(dbz, "put"),
					*get_op = dbz_op(dbz, "get"),
					*del_op = dbz_op(dbz, "del"),
					*walk_op = dbz_op(dbz, "walk"),
					*flush_op = dbz_op(dbz, "flush");

		if( put_op ) bench.put = put_op->cb;
		if( get_op ) bench.get = get_op->cb;
		if( del_op ) bench.del = del_op->cb;
		if( walk_op ) bench.walk = walk_op->cb;
		if( flush_op ) bench.flush = flush_op->cb;
	}
	
	if( ! benchmark_validate(&bench) ) {
		print_usage(argv[0]);
		return EXIT_FAILURE;
	}

	print_environment();
	printf("\n");
	printf("  Benchmark:    %s\n", bench.name);
	printf("  Backend:      %s\n", mod_file);
	printf("  Keys:         %zu bytes each\n", bench.key_len);
	printf("  Values:       %zu bytes each\n", bench.val_len);
	printf("  Entries:      %zu\n", bench.entries);
	printf("  Load:         %d%% READS / %d%% WRITES\n", (int)bench.read_pct, (int)(100-bench.read_pct));
	printf("\n");

	benchmark_run(&bench);
	dbz_close(dbz);
	dbz=NULL;
	return EXIT_SUCCESS;
}
