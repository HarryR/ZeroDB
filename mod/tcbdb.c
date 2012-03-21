#include <tcutil.h>
#include <tcbdb.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <err.h>
#include "../i_speak_db.h"

static TCBDB *db = NULL;
static size_t key_size = -1;

static void
close_db(){
	if(db){
		tcbdbclose(db);
		db = NULL;
	}
}

static void
open_db() {
	if(!db){
		db = tcbdbnew();
		const char* filename = getenv("TCBDB_FILE");
		if(!filename) filename = "database.tcbdb.dat";

		const char* prot_keysize = getenv("DBZMQ_KEYSIZE");
		if(!prot_keysize) prot_keysize = "20";
		key_size = atoi(prot_keysize);
		if(key_size < 1 || key_size > 0xFF) {
			errx(EXIT_FAILURE, "Invalid key size %zu", key_size);
		}

		/* TODO: tcbdbtune, tcbdbsetcache etc. */

		if( ! tcbdbopen(db, filename, BDBOCREAT|BDBOREADER|BDBOWRITER) ) {
			errx(EXIT_FAILURE, "Cannot tcbdbopen('%s'): %s", filename, tcbdberrmsg(tcbdbecode(db)));
		}
		atexit(close_db);
	}
}

static
DB_OP(do_put){
	if(in_sz<=key_size)
		return 0;
	open_db();
	if( tcbdbput(db, in_data, key_size, in_data+key_size, in_sz-key_size) ) {
		if(cb) {
			cb(in_data, in_sz, NULL, token);
		}
		return in_sz;
	}

	if(cb){
		cb(in_data, key_size, NULL, token);
	}
	return key_size;
}

static
DB_OP(do_get){
	size_t out_sz = in_sz;
	char *out_data = NULL;
	int data_sz = 0;
	char* data;
	
	open_db();
	data = (char*)tcbdbget(db, in_data, in_sz, &data_sz);
	if(!data){
		if(cb) cb(in_data, in_sz, NULL, token);
		return key_size;
	}

	/* 
	 * +---------+-----------+
	 * | KEY[20] | DATA[n..] |
	 * +---------+-----------+
	 */
	out_sz += data_sz;
	if(cb){
		out_data = (char*)malloc(out_sz);
		memcpy(out_data, in_data, in_sz);
		memcpy(out_data+in_sz, data, data_sz);
		cb(out_data, out_sz, NULL, token);
		free(out_data);
	}
	free(data);

	return out_sz;
}

static
DB_OP(do_del){
	open_db();
	tcbdbout(db, in_data, in_sz);
	cb(in_data, in_sz, NULL, token);
	return in_sz;
}

void* i_speak_db(void)
{
	static struct dbz_op ops[] = {
		{"put", 0, (dbzop_t)do_put, NULL},
		{"get", 1, (dbzop_t)do_get, NULL},
		{"del", 0, (dbzop_t)do_del, NULL},
		{NULL, 0, 0, 0}
	};
	return &ops;
}