#define _POSIX_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <libgen.h>
#include <signal.h>
#include <string.h>

#include <err.h>
#include <assert.h>
#include <dlfcn.h>

#include <zmq.h>
#include <stdint.h>

#include "db-zmq.h"

struct dbz_s {
	int running;
	void* mod;
	void* mod_ctx;
	struct dbz_op* ops;
};
typedef struct dbz_s dbz;

void*
dbz_load(const char *filename){
	void *f = NULL;
	dbz* x = malloc(sizeof(dbz));
	if(!x) return NULL;

	memset(x, 0, sizeof(dbz));
	x->mod = dlopen(filename, RTLD_LAZY);
	if( ! x->mod ) {
		warnx("Cannot dlopen(%p, '%s') = %s", x->mod, filename, dlerror());
		free(x);
		return NULL;
	}
    f = dlsym(x->mod, "i_speak_db");
    if( ! f ) {
    	warnx("Cannot dlsym(%p, 'i_speak_db') = %s", x->mod, dlerror());
    	dlclose(x->mod);
    	free(x);
    	return NULL;
    }

    x->ops = ((mod_init_fn) f)();
    return x;
}

struct dbz_op*
dbz_op(void* _ctx, const char* name){
	dbz* ctx = (dbz*)_ctx;
	struct dbz_op* f = ctx->ops;
	while( f->name ) {
		if( strcmp(f->name, name) == 0 ) {
			return f;
		}
		f++;
	}
	return NULL;
}

int
dbz_close(void* _ctx){
	dbz* ctx = (dbz*)_ctx;
	if( ctx->mod ) dlclose(ctx->mod);
	memset(ctx, 0, sizeof(dbz));
	free(ctx);
	return 1;
}

#ifdef DBZ_MAIN

typedef struct {
	void *socket;
	uint64_t bytes_in;
	uint64_t bytes_out;
	uint64_t calls;
} dbzmq_stats_t;

static size_t
reply_cb(void* data, size_t len, void* cb, void* token ) {
	zmq_msg_t msg;
	assert( cb == NULL );
	zmq_msg_init_size(&msg, len);
	memcpy(zmq_msg_data(&msg), data, len);
	zmq_send(token, &msg, 0);
	zmq_msg_close(&msg);
	return len;
}

static struct dbz_op*
dbz_bind(void* zctx, void* _ctx, const char* name, const char *addr) {
	void *sock;
	struct dbz_op* op = dbz_op(_ctx, name);
	if( ! op->name ) {
		warnx("Unknown bind name %s=%s", name, addr);
		return NULL;
	}

	if( (sock = zmq_socket(zctx, op->opts ? ZMQ_REP : ZMQ_PULL)) == NULL ) {
		warnx("Cannot create socket for '%s': %s", addr, zmq_strerror(zmq_errno()));	
		return NULL;;
	} 
	if( zmq_bind(sock, addr) == -1 ) {
		warnx("Cannot bind socket '%s': %s", addr, zmq_strerror(zmq_errno()));
		zmq_close(sock);
		return NULL;
	}
	op->token = sock;
	return op;
}

static int
dbz_run(void* _ctx) {
	dbz* ctx = (dbz*)_ctx;
	ctx->running = 1;
	int fc = 0;
	int i;
	struct dbz_op* f = ctx->ops;
	while( (f++)->name ) {
		fc++;
	}
	zmq_pollitem_t items[fc];
/*	dbzmq_stats_t stats[fc]; */

	while( ctx->running == 1 ) {
		memset(&items[0], 0, sizeof(zmq_pollitem_t) * fc);
		for( i = 0; i < fc; i++ ) {
			items[i].socket = ctx->ops[i].token;
			items[i].fd = 0;
			items[i].events = ZMQ_POLLIN;
			items[i].revents = 0;
		}
	
		int rc = zmq_poll(items, fc, /*over*/9001);
		if( rc >= 0 ) {
			for( i = 0; i < fc; i++ ) {
				if( items[i].revents & ZMQ_POLLIN ){				
					zmq_msg_t msg;
					rc = zmq_msg_init(&msg);
					assert(rc==0);
					int msg_recvd = zmq_recv(items[i].socket, &msg, 0);
					/* TODO: collect statistics */
					if( 0 == msg_recvd ) {
						ctx->ops[i].cb(zmq_msg_data(&msg), zmq_msg_size(&msg), (void*)reply_cb, items[i].socket);
					}
					zmq_msg_close(&msg);
				}
			}	
		}		
	}
	return ctx->running;
}

static dbz* d = NULL;
struct sigaction old_action;

static void
ctrl_c_handler(int sig_no){
	if( sig_no == SIGINT ){		
		d->running += 1;
		warnx("CTRL-C caught, shutting down\n");
		sigaction(sig_no, &old_action, NULL);
	}
}

static void
setup_handlers(){
	struct sigaction act;
	memset(&act, 0, sizeof(act));
	act.sa_handler = &ctrl_c_handler;
	sigaction(SIGINT, &act, &old_action);
}

int main(int argc, char **argv) {
	if( argc < 2 ) {
		fprintf(stderr, "Usage: %s <module.so> [op=tcp://... ]\n", argv[0]);
		return( EXIT_FAILURE );
	}

	void *zctx = zmq_init(1);
	assert(zctx != NULL);

	d = dbz_load(argv[1]);
	if( ! d ) return( EXIT_FAILURE );

	int ok = 0;
	int i;
	for( i = 2; i < argc; i++ ) {
		char *op = argv[i];
		char *addr = strchr(op, '=');
		*addr++ = 0;
		struct dbz_op* f = dbz_bind(zctx, d, op, addr);
		if( ! f  ) {
			errx(EXIT_FAILURE, "Cannot bind '%s'='%s'", op, addr);
		}
		ok += f!=0;
	}

	if( ! ok ) {
		struct dbz_op* f = d->ops;
		fprintf(stderr, "Operations:\n");
		while( f->name ) {
			fprintf(stderr, "\t%s\n", f->name);
			f++;
		}		
		exit(EXIT_FAILURE);
	}

	setup_handlers();
	dbz_run(d);	

	struct dbz_op* f = d->ops;
	while( f && f->name ) {
		if( f->token ) zmq_close(f->token);
		f++;
	}
	if( zctx ) zmq_term(zctx);
	dbz_close(d);
	return( EXIT_SUCCESS );
}

/* DBZ_MAIN */
#endif
