#include "../i_speak_db.h"

#include "db.h"

#include <unistd.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifndef HASH_LENGTH
#define HASH_LENGTH 20
#endif

struct nessdb* db = NULL;

static void close_db(void) {
	db_close(db);
}

static struct nessdb* open_db(void *can_reply, void *reply_directed) {
	// NOTE: The plugin should be agnostic to the socket type,
	//       but the presence of either or the `cb` and `token`
	//       parameters can be associated with channel types.
	//
	//   can_reply && reply_directed = REPLY/PAIR socket
	//   can_reply && !reply_directed = PUB socket
	//   !can_reply = PULL/SUB socket
	//
	(void)can_reply;
	(void)reply_directed;
	if( ! db ) {
		db = db_open(4 * 1024 * 1024, getcwd(NULL,0), 1);
		assert( db != NULL );
		atexit(close_db);
	}
	return db;
}


DB_OP(nessdb_put){
	if(in_sz < (HASH_LENGTH+1))
		return 0;

	open_db(cb, token);
	struct slice sk = {in_data, HASH_LENGTH};
	struct slice sv = {in_data+HASH_LENGTH, in_sz-HASH_LENGTH};

	#if 0
	sha1nfo data_hash;
	// The key should be unique to the database
	// k = {∃! k ∈ DB|SHA1(in_data)}
	sha1_init(&data_hash);
	sha1_write(&data_hash, in_data, in_sz);
	sk.data = (char*)sha1_result(&data_hash);
	sk.len = HASH_LENGTH;
	#endif

	if( db_add(db, &sk, &sv) ) {
		if(cb) {			
			cb(in_data, in_sz, NULL, token);
		}
		return sk.len + sv.len;
	}

	cb(sk.data, sk.len, NULL, token);
	return sk.len;
}

DB_OP(nessdb_get){
	open_db(cb, token);
	struct slice sk = {in_data, in_sz};
	struct slice sv = {NULL, 0};
	size_t ret = in_sz;
	if( db_get(db, &sk, &sv) ) {
		if(cb) {
			struct slice out = {malloc(sk.len+sv.len), sk.len+sv.len};
			struct slice out_data = {out.data+sk.len, out.len - sk.len};
			memcpy(out.data, in_data, in_sz);
			memcpy(out_data.data, sv.data, sv.len);
			cb(out.data, out.len, NULL, token);
			free(out.data);
		}
		ret += sv.len;
	}
	return ret;
}

DB_OP(nessdb_del){
	open_db(cb, token);
	struct slice sk = {in_data, in_sz};
	db_remove(db, &sk);
	if(cb)
		cb(in_data,in_sz,NULL,token);
	return in_sz;
}

DB_OP(nessdb_walk){
	if(in_sz!=HASH_LENGTH)
		return 0;

	if(cb){
		cb(in_data, in_sz, NULL, token);
	}

	// Unsupported by DB layer
	/*
	open_db(cb, token);
	struct slice sk = {in_data, in_sz};
	struct btree_item* next_item = btree_next(db->btree, db->btree->top, &sk);
	if( next_item && cb ) {
		cb(next_item->sha1, HASH_LENGTH, NULL, token);
		return HASH_LENGTH;
	}
	*/

	return in_sz;
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
