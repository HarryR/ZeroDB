#ifndef _SKIPLIST_H
#define _SKIPLIST_H

#include "util.h"

#define MAXLEVEL (15)
#define SKIP_KSIZE (64)

typedef enum {DEL, ADD} OPT;

struct skipnode{
    char key[SKIP_KSIZE];
	uint64_t val;
	unsigned opt:2;                   
    struct skipnode *forward[1]; 
	struct skipnode *next;
};

struct skiplist{
	struct  skipnode *hdr;                 
	size_t count;
	size_t size;
	int level; 
	char pool_embedded[1024];
	struct pool *pool;
};

struct skiplist *skiplist_new(size_t size);
void skiplist_init(struct skiplist *list);
int skiplist_insert(struct skiplist *list, struct slice *sk, uint64_t val, OPT opt);
struct skipnode *skiplist_lookup(struct skiplist *list, struct slice *sk);
struct skipnode *skiplist_next(struct skiplist *list, struct slice *sk);
int skiplist_full(struct skiplist *list);
void skiplist_dump(struct skiplist *list);
void skiplist_free(struct skiplist *list);


#endif
