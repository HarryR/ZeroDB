#include "../i_speak_db.h"

DB_OP(nullop_null){
	if(cb) cb(in_data, in_sz, NULL, token);
	return in_sz;
}

void*
i_speak_db(void){
	static struct dbz_op ops[] = {
		{"put", 0, (dbzop_t)nullop_null, NULL},
		{"get", 1, (dbzop_t)nullop_null, NULL},
		{"del", 0, (dbzop_t)nullop_null, NULL},
		{"walk", 0, (dbzop_t)nullop_null, NULL},
		{NULL, 0, 0, 0}
	};
	return &ops;
}
