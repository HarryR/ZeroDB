#include <tcutil.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <err.h>
#include "../i_speak_db.h"

#include "mongo.h"

static mongo db[1];
static bool db_ok = false;
static const char* db_collection;
static size_t key_size = -1;

static void
close_db(){
	if(db_ok){
		mongo_destroy(db);
		db_ok = false;
	}
}

static void
open_db() {
	if(!db_ok){
		const char *mongo_host = getenv("MONGO_HOST");
		const char *mongo_port = getenv("MONGO_PORT");
		db_collection = getenv("MONGO_COLLECTION");
		if(!mongo_host) mongo_host = "127.0.0.1";
		if(!mongo_port) mongo_port = "27017";
		if(!db_collection) db_collection = "ness.kv";

		const char* prot_keysize = getenv("DBZMQ_KEYSIZE");
		if(!prot_keysize) prot_keysize = "20";
		key_size = atoi(prot_keysize);
		if(key_size < 1 || key_size > 0xFF) {
			errx(EXIT_FAILURE, "Invalid key size %zu", key_size);
		}		

		int port = atoi(mongo_port);
		if(port < 1024 || port >= 0xffff) {
			errx(EXIT_FAILURE,"Invalid port %d", port);
		}

		int rc = mongo_connect(db, mongo_host, port);
		if( MONGO_OK == rc ) {
			db_ok = true;
			atexit(close_db);
		}
		else{			
			err(EXIT_FAILURE,"Cannot connect to mongo://%s:%d - %d", mongo_host, port, rc);
		}
	}
}

static
DB_OP(do_put){
	if(key_size >= in_sz)
		return 0;

	bson b[1];
	bson_init(b);
	bson_append_binary(b, "_id", BSON_BIN_BINARY, in_data, key_size);
	bson_append_binary(b, "val", BSON_BIN_BINARY, in_data+key_size, in_sz-key_size);
	bson_finish(b);

	size_t ret = in_sz;
	open_db();
	if( mongo_insert(db, db_collection, b) ){			
		ret = in_sz;
		if(cb)
			cb(in_data, in_sz, NULL, token);
	}
	else{
		if(cb)
			cb(in_data, key_size, NULL, token);	
	}
	bson_destroy(b);

	return ret;
}

static
DB_OP(do_get){
	bson bquery[1];	
	bson bout[1];
	bson bfields[1];
	bson_empty(bout);
	
	bson_init(bfields);
	  bson_append_int(bfields, "val", 1);
	bson_finish(bfields);

	bson_init(bquery);
	  bson_append_start_object(bquery, "$query");
	  	bson_append_binary(bquery, "_id", BSON_BIN_BINARY, in_data, in_sz);
	  bson_append_finish_object(bquery);
	bson_finish(bquery);

	size_t ret = in_sz;
	open_db();
	int x = mongo_find_one(db, db_collection, bquery, NULL, bout);
	if( x == MONGO_OK ) {
		if(cb){			
			bson_iterator it;
			bson_iterator_init(&it, bout);
			if( bson_find(&it, bout, "val") ) {
				const char* data = bson_iterator_bin_data(&it);
				size_t data_sz = bson_iterator_bin_len(&it);
				size_t out_sz = data_sz + in_sz;
				char *out_data = (char*)malloc(out_sz);
				memcpy(out_data, in_data, in_sz);
				memcpy(out_data+in_sz, data, data_sz);
				cb(out_data, out_sz, NULL, token);
				ret = out_sz;
				free(out_data);
			}
		}
		bson_destroy(bout);
	}
	else{
		if(cb)
			cb(in_data, in_sz, NULL, token);
	}

	bson_destroy(bquery);
	bson_destroy(bfields);

	return ret;
}

static
DB_OP(do_del){
	if(in_sz!=key_size)
		return 0;

	bson b[1];
	bson_init(b);	
	  bson_append_binary(b, "_id", BSON_BIN_BINARY, in_data, in_sz);
	bson_finish(b);

	size_t ret = 0;
	open_db();
	if( mongo_remove(db, db_collection, b) ) {			
		ret = in_sz;
	}
	bson_destroy(b);

	cb(in_data, in_sz, NULL, token);

	return ret;
}

static
DB_OP(do_next){
	size_t ret = in_sz;
	int rc = 0;
	bson bquery[1], bout[1];	
	bson bfields[1];

	bson_init(bfields);
	  bson_append_int(bfields, "_id", 1);
	bson_finish(bfields);

	bson_init(bquery);
	  bson_append_start_object(bquery, "$query");
	    bson_append_start_object(bquery, "_id");
	      bson_append_binary(bquery, "$gt", BSON_BIN_BINARY, in_data, in_sz);
	    bson_append_finish_object(bquery);
	  bson_append_finish_object(bquery);

	  bson_append_start_object(bquery, "$orderby");
	  	bson_append_int(bquery, "_id", 1);
	  bson_append_finish_object(bquery);
	bson_finish(bquery);
	
	open_db();
	rc = mongo_find_one(db, db_collection, bquery, bfields, bout);
	if( MONGO_OK == rc ) {
		bson_iterator it;
		bson_iterator_init(&it, bout);
		if( bson_find(&it, bout, "_id") ) {
			const char* data = bson_iterator_bin_data(&it);
			size_t data_sz = bson_iterator_bin_len(&it);
			ret += data_sz;
			if(cb){
				size_t out_sz = in_sz + data_sz;
				char *out_data = (char*)malloc(out_sz);
				memcpy(out_data, in_data, in_sz);
				memcpy(out_data+in_sz, data, data_sz);
				cb(out_data, out_sz, NULL, token);	
				free(out_data);
			}
		}
		else{
			if( cb )
				cb(in_data, in_sz, NULL, token);
		}
		bson_destroy(bout);
	}
	else {
		if( cb )
			cb(in_data, in_sz, NULL, token);
	}
	bson_destroy(bquery);
	bson_destroy(bfields);

	return ret;
}

void*
i_speak_db(void){
	static struct dbz_op ops[] = {
		{"put", 0, (dbzop_t)do_put, NULL},
		{"get", 1, (dbzop_t)do_get, NULL},
		{"del", 0, (dbzop_t)do_del, NULL},
		{"walk", 1, (dbzop_t)do_next, NULL},
		{NULL, 0, 0, 0}
	};
	return &ops;
}