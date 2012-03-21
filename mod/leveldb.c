#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <err.h>
#include <assert.h>

#include "leveldb/c.h"
#include "../i_speak_db.h"

static leveldb_t* db = NULL;
static leveldb_options_t* db_options = NULL;
static leveldb_readoptions_t* db_roptions = NULL;
static leveldb_writeoptions_t* db_woptions = NULL;
static size_t key_size = -1;

static void
close_db(){
	if(db){
		leveldb_close(db);
		leveldb_options_destroy(db_options);
		leveldb_readoptions_destroy(db_roptions);
  		leveldb_writeoptions_destroy(db_woptions);
		db = NULL;
	}
}

static void
open_db() {
	if(!db){
		const char* filename = getenv("LEVELDB_FILE");
		if(!filename) filename = "leveldb.dat";

		const char* prot_keysize = getenv("DBZMQ_KEYSIZE");
		if(!prot_keysize) prot_keysize = "20";
		key_size = atoi(prot_keysize);
		if(key_size < 1 || key_size > 0xFF) {
			errx(EXIT_FAILURE, "Invalid key size %zu", key_size);
		}

		char *dberr = NULL;
		db_options = leveldb_options_create();
		leveldb_options_set_error_if_exists(db_options, 0);
		leveldb_options_set_create_if_missing(db_options, 1);
		leveldb_options_set_compression(db_options, leveldb_no_compression);

		db_roptions = leveldb_readoptions_create();		

		db_woptions = leveldb_writeoptions_create();
		leveldb_writeoptions_set_sync(db_woptions, 0);

		db = leveldb_open(db_options, filename, &dberr);
		if( ! db ) {
			errx(EXIT_FAILURE, "Cannot leveldb_open('%s'): %s", filename, dberr);
		}

		atexit(close_db);
	}
}

static
DB_OP(do_put){
	size_t ret_sz;
	char *dberr = NULL;

	if(in_sz<=key_size) {
		if(cb)
			cb(in_data, in_sz, NULL, token);		
		return 0;
	}

	open_db();
	leveldb_put(db, db_woptions,
		in_data, key_size,
		in_data+key_size, in_sz-key_size,
		&dberr);

	if( dberr ) {
		// Error, return only key
		ret_sz = key_size;
	}
	else {
		// Success, return key ++ value
		ret_sz = in_sz;
	}

	if(cb){
		cb(in_data, ret_sz, NULL, token);
	}
	return ret_sz;
}

static
DB_OP(do_get){
	char *out_data = NULL;
	char *dberr = NULL;
	char *data = NULL;
	size_t data_sz = 0;
	size_t out_sz;

	open_db();
	data = leveldb_get(db, db_roptions, in_data, in_sz, &data_sz, &dberr);	
	if( ! data ) {
		if( cb ) {
			cb(in_data, in_sz, NULL, token);
		}
		return in_sz;
	}

	out_sz = in_sz + data_sz;
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
	char *dberr = NULL;

	open_db();
	leveldb_delete(db, db_woptions, in_data, in_sz, &dberr);
	if( dberr ){
		warnx("Cannot delete: %s", dberr);
	}
	if( cb )
		cb(in_data, in_sz, NULL, token);
	return in_sz;
}

#ifdef __cplusplus
extern "C" {
#endif
	void*
	i_speak_db(void){
		static struct dbz_op ops[] = {
			{"put", 0, (dbzop_t)do_put, NULL},
			{"get", 1, (dbzop_t)do_get, NULL},
			{"del", 0, (dbzop_t)do_del, NULL},
			{NULL, 0, 0, 0}
		};
		return &ops;
	}
#ifdef __cplusplus
}
#endif
