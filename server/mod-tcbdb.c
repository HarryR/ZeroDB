#include <tcutil.h>
#include <tcbdb.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include "../i_speak_db.h"

static TCBDB *db = NULL;

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
		tcbdbopen(db, "database.tcbdb.dat", BDBOCREAT|BDBOREADER|BDBOWRITER);
		atexit(close_db);
	}
}

DB_OP(do_put){
	if(in_sz<=20)
		return 0;
	open_db();
	if( tcbdbput(db, in_data, 20, in_data+20, in_sz-20) )
		return in_sz;
	return 0;
}

DB_OP(do_get){
	int sz;
	void *data;
	open_db();
	data = tcbdbget(db, in_data, in_sz, &sz);
	if( data ) {
		cb(data, sz, NULL, token);
		return in_sz+sz;
	}
	return 0;
}

DB_OP(do_del){
	open_db();
	if( tcbdbout(db, in_data, in_sz) ) {
		return in_sz;
	}
	return 0;
}

DB_OP(do_next){
	TCLIST *keys;
	open_db();
	keys = tcbdbfwmkeys(db, in_data, in_sz, 1);
	if(keys){
		int out_sz = 0;
		if( cb && tclistnum(keys)) {
			const char *out_data = tclistval(keys, 0, &out_sz);
			cb(out_data, out_sz, NULL, token);
			out_sz += in_sz;
		}
		tclistclear(keys);
		return out_sz;
	}
	return 0;
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