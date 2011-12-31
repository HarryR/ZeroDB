#ifndef _DB_ZMQ_H
#define _DB_ZMQ_H

#include "../i_speak_db.h"

struct dbz_s {
	int running;
	void* mod;
	void* mod_ctx;
	struct dbz_op* ops;
};
typedef struct dbz_s dbz;

dbz* dbz_init(struct dbz_op* ops);
dbz* dbz_open(const char *filename);
struct dbz_op* dbz_op(dbz* ctx, const char* name);
int dbz_close(dbz* ctx);

#endif
