#include "../i_speak_db.h"

#include "db.h"

#include <unistd.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <err.h>

static struct nessdb* db = NULL;
static size_t key_size;

static void close_db(void) {
	if(db) {		
		db_close(db);
		db = NULL;
	}
}

static struct nessdb* open_db() {
	if( ! db ) {
		const char* prot_keysize = getenv("DBZMQ_KEYSIZE");
		if(!prot_keysize) prot_keysize = "20";
		key_size = atoi(prot_keysize);
		if(key_size < 1 || key_size > 0xFF) {
			errx(EXIT_FAILURE, "Invalid key size %zu", key_size);
		}

		db = db_open(4 * 1024 * 1024, getcwd(NULL,0), 1);
		assert( db != NULL );
		atexit(close_db);
	}
	return db;
}


DB_OP(nessdb_put){
	size_t out_sz = 0;
	struct slice sk = {in_data, key_size};
	struct slice sv = {in_data+key_size, in_sz-key_size};
	if(in_sz <= key_size)
		return 0;

	open_db();

	#if 0
	sha1nfo data_hash;
	// The key should be unique to the database
	// k = {∃! k ∈ DB|SHA1(in_data)}
	sha1_init(&data_hash);
	sha1_write(&data_hash, in_data, in_sz);
	sk.data = (char*)sha1_result(&data_hash);
	sk.len = key_size;
	#endif

	if( db_add(db, &sk, &sv) ) {		
		out_sz = in_sz;
	}
	else {
		out_sz = key_size;
	}

	if(cb)
		cb(in_data, out_sz, NULL, token);

	return out_sz;
}

DB_OP(nessdb_get){
	open_db();
	struct slice sk = {in_data, in_sz};
	struct slice sv = {NULL, 0};
	size_t ret = in_sz;
	sv.len = db_get(db, &sk, &sv);
	if( sv.len && sv.data ) {
		if(cb) {
			struct slice out = {malloc(sk.len+sv.len), sk.len+sv.len};
			memcpy(out.data, in_data, in_sz);
			memcpy(out.data, sv.data, sv.len);
			cb(out.data, out.len, NULL, token);
			free(out.data);
			free(sv.data);
		}
		ret += sv.len;
	}
	else {
		if(cb)
			cb(in_data, in_sz, NULL, token);
	}
	return ret;
}

DB_OP(nessdb_del){
	open_db();
	struct slice sk = {in_data, in_sz};
	db_remove(db, &sk);
	if(cb)
		cb(in_data,in_sz,NULL,token);
	return in_sz;
}

DB_OP(nessdb_walk){	
	if(cb){
		cb(in_data, in_sz, NULL, token);
	}
	return in_sz;

	// Unsupported by DB layer
	/*
	open_db();
	struct slice sk = {in_data, in_sz};
	struct slice sv = {NULL, 0};
	if( db_next(db, &sk, &sv) ) {
		// TODO: construct buffer & return
	}
	*/
}

void*
i_speak_db(void){
	static struct dbz_op ops[] = {
		{"put", 0, (dbzop_t)nessdb_put, NULL},
		{"get", 1, (dbzop_t)nessdb_get, NULL},
		{"del", 0, (dbzop_t)nessdb_del, NULL},
		{"walk", 0, (dbzop_t)nessdb_walk, NULL},
		{NULL, 0, 0, 0}
	};
	return &ops;
}
