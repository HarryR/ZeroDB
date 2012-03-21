#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <err.h>
#include <unistd.h>
#include <sqlite3.h>

#include "../i_speak_db.h"

static sqlite3* db = NULL;
static sqlite3_stmt* db_put_stmt = NULL;
static sqlite3_stmt* db_get_stmt = NULL;
static sqlite3_stmt* db_del_stmt = NULL;

static size_t key_size = -1;

static const char init_sql[] = "CREATE TABLE kv(k BLOB PRIMARY KEY, v BLOB)";
static const char get_sql[]  = "SELECT v FROM kv WHERE k = ? LIMIT 1";
static const char put_sql[]  = "INSERT INTO kv VALUES (?,?)";
static const char del_sql[]  = "DELETE FROM kv WHERE k = ? LIMIT 1";

static void
close_db(){
	if(db){
		/* TODO: validate return codes. */
		sqlite3_finalize(db_put_stmt);
		sqlite3_finalize(db_get_stmt);
		sqlite3_finalize(db_del_stmt);
		while( sqlite3_close(db) == SQLITE_BUSY ) {
			sleep(1);
		}
		db = NULL;
	}
}

static void
open_db() {
	if(!db){
		const char* filename = getenv("SQLITE3_FILE");
		if(!filename) filename = "sqlite3.dat";

		const char* prot_keysize = getenv("DBZMQ_KEYSIZE");
		if(!prot_keysize) prot_keysize = "20";
		key_size = atoi(prot_keysize);
		if(key_size < 1 || key_size > 0xFF) {
			errx(EXIT_FAILURE, "Invalid key size %zu", key_size);
		}

		if( sqlite3_open(filename, &db) != SQLITE_OK ) {
			db = NULL;
			errx(EXIT_FAILURE, "Cannot sqlite3_open('%s'): %s", filename, sqlite3_errmsg(db));
		}
		else {			
			sqlite3_exec(db, init_sql, NULL, NULL, NULL);
			sqlite3_exec(db, 
			    "PRAGMA synchronous = off;"
			    "PRAGMA journal_mode = off;"
			    "PRAGMA locking_mode = exclusive;"
			, 0, 0, 0
			  );
			sqlite3_prepare_v2(db, get_sql, -1, &db_get_stmt, 0);
			sqlite3_prepare_v2(db, put_sql, -1, &db_put_stmt, 0);
			sqlite3_prepare_v2(db, del_sql, -1, &db_del_stmt, 0);
			atexit(close_db);
		}
	}
}

static
DB_OP(do_put){
	if(in_sz<=key_size)
		return 0;
	open_db();
	size_t ret_sz;

	sqlite3_bind_blob(db_put_stmt, 1, in_data, key_size, SQLITE_STATIC);
	sqlite3_bind_blob(db_put_stmt, 2, in_data+key_size, in_sz-key_size, SQLITE_STATIC);

	if( sqlite3_step(db_put_stmt) == SQLITE_DONE ) {
		ret_sz = in_sz;
		if(cb){
			cb(in_data, in_sz, NULL, token);			
		}
	}
	else {
		ret_sz = key_size;
		if(cb) cb(in_data, key_size, NULL, token);
	}
	sqlite3_reset(db_put_stmt);
	return ret_sz;
}

static
DB_OP(do_get){
	size_t out_sz = in_sz;
	char *out_data = NULL;
	
	open_db();

	sqlite3_bind_blob(db_get_stmt, 1, in_data, in_sz, SQLITE_STATIC);	
	if( sqlite3_step(db_get_stmt) == SQLITE_ROW ) {
		if(cb){
			out_sz += sqlite3_column_bytes(db_get_stmt, 0);
			out_data = (char*)malloc(out_sz);
			memcpy(out_data, in_data, in_sz);
			memcpy(out_data+in_sz, sqlite3_column_blob(db_get_stmt, 0), out_sz - in_sz);
			cb(out_data, out_sz, NULL, token);
			free(out_data);			
		}
	}
	else {
		if(cb) cb(in_data, in_sz, NULL, token);
	}
	sqlite3_reset(db_get_stmt);
	return out_sz;
}

static
DB_OP(do_del){
	open_db();

	sqlite3_bind_blob(db_del_stmt, 1, in_data, in_sz, SQLITE_STATIC);	
	sqlite3_step(db_del_stmt);	
	if(cb){
		cb(in_data, in_sz, NULL, token);			
	}
	sqlite3_reset(db_del_stmt);
	return in_sz;
}

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