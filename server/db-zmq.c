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
#include "../i_speak_db.h"

/**
 * Initialize with set of operations.
 */
dbz* dbz_init(struct dbz_op* ops)
{
	dbz* x = (dbz*)malloc(sizeof(dbz));
	assert(x != NULL);
	if(!x) return NULL;

	memset(x, 0, sizeof(dbz));
	x->ops = ops;
	return x;
}

/**
 * Open a .so file which exports "i_speak_db"
 * @return Database handle
 */
dbz* dbz_open(const char *filename)
{
	mod_init_fn f = NULL;
	dbz* x = dbz_init(NULL);
	x->mod = dlopen(filename, RTLD_LAZY);
	if( ! x->mod ) {
		warnx("Cannot dlopen(%p, '%s') = %s", x->mod, filename, dlerror());
		free(x);
		return NULL;
	}
    f = (mod_init_fn)dlsym(x->mod, "i_speak_db");
    if( ! f ) {
    	warnx("Cannot dlsym(%p, 'i_speak_db') = %s", x->mod, dlerror());
    	dlclose(x->mod);
    	x->mod = NULL;
    	free(x);
    	return NULL;
    }

    x->ops = (struct dbz_op*)f();
    return x;
}

/**
 * Find an operation with matching name.
 *
 * Human readable type informaion can be appended to
 * the name when defining an operation.
 *
 * Providing just "get" or "put" will match these correctly:
 *   "get (kN) -> k ++ vN || k"
 *   "put (kNvN) -> k ++ v || k"
 */
struct dbz_op* dbz_op(dbz* ctx, const char* name)
{
	struct dbz_op* f = ctx->ops;
	const char *x;
	while( f->name ) {
		if( strcmp(f->name, name) == 0 ) {
			x = f->name + strlen(name);
			if( *x == 0 || *x == ' ')
				return f;
		}
		f++;
	}
	return NULL;
}

/**
 * Close handle, unload module
 */
int dbz_close(dbz* ctx)
{
	assert(ctx != NULL);
	if( ctx->mod ) dlclose(ctx->mod);
	memset(ctx, 0, sizeof(dbz));
	free(ctx);
	return 1;
}

static struct dbz_op* dbz_bind(void* zctx, dbz* ctx, const char* name, const char *addr)
{
	dbzmq_socket_t *token;
	void *sock;
	int sock_type;
	struct dbz_op* op = dbz_op(ctx, name);
	if( ! op->name ) {
		warnx("Unknown bind name %s=%s", name, addr);
		return NULL;
	}

	if( strncmp(addr, "pull@", 5) == 0 ) {
		sock_type = ZMQ_PULL;
		addr += 5;
	}
	else if( strncmp(addr, "rep@", 4) == 0 ) {
		sock_type = ZMQ_REP;
		addr += 4;
	}
	else {
		warnx("Unknown bind type %s=%s", name, addr);
		return NULL;
	}

	if( (sock = zmq_socket(zctx, sock_type)) == NULL ) {
		warnx("Cannot create socket for '%s': %s", addr, zmq_strerror(zmq_errno()));	
		return NULL;;
	} 
	if( zmq_bind(sock, addr) == -1 ) {
		warnx("Cannot bind socket '%s': %s", addr, zmq_strerror(zmq_errno()));
		zmq_close(sock);
		return NULL;
	}	
	token = (dbzmq_socket_t*)malloc(sizeof(dbzmq_socket_t));
	memset(token, 0, sizeof(dbzmq_socket_t));
	token->socket = sock;
	op->token = (void*)token;
	return op;
}

static size_t reply_cb(const char* data, size_t len, dbzop_t cb, dbzmq_socket_t* token )
{
	zmq_msg_t msg;	
	assert(token->socket);
	assert(len > 0);

	zmq_msg_init_size(&msg, len);
	memcpy(zmq_msg_data(&msg), data, len);
	zmq_send(token->socket, &msg, 0);
	zmq_msg_close(&msg);

	token->bytes_out += len;
	if( ! cb )
		return len;

	return len + cb(data, len, NULL, token);
}

static void handle_POLLIN(dbzop_t cb, dbzmq_socket_t* token)
{
	zmq_msg_t msg;
	int rc = zmq_msg_init(&msg);

	assert(rc==0);
	if(rc!=0) return;

	assert(token);
	assert(token->socket);
	assert(cb);
	if( ! zmq_recv(token->socket, &msg, ZMQ_NOBLOCK) ) {
		token->calls += 1;
		token->bytes_in += zmq_msg_size(&msg);
		cb((const char*)zmq_msg_data(&msg), zmq_msg_size(&msg), (void*)reply_cb, token);
	}
	zmq_msg_close(&msg);
}

static int dbz_run(dbz* ctx)
{
	assert(ctx);
	ctx->running = 1;
	int fc = 0;
	int i;
	struct dbz_op* f = ctx->ops;
	while( (f++)->name ) {
		fc++;
	}
	zmq_pollitem_t items[fc];

	while( ctx->running == 1 ) {
		memset(&items[0], 0, sizeof(zmq_pollitem_t) * fc);
		for( i = 0; i < fc; i++ ) {
			items[i].socket = ((dbzmq_socket_t*)(ctx->ops[i].token))->socket;
			items[i].fd = 0;
			items[i].events = ZMQ_POLLIN;
			items[i].revents = 0;
		}
	
		int rc = zmq_poll(items, fc, /*over*/9001);
		if( rc > 0 ) {
			for( i = 0; i < fc; i++ ) {
				if( items[i].revents & ZMQ_POLLIN ){	
					handle_POLLIN(ctx->ops[i].cb, (dbzmq_socket_t*)ctx->ops[i].token);
				}
			}	
		}		
	}
	return ctx->running;
}

static dbz* d = NULL;
static struct sigaction old_action;

static void ctrl_c_handler(int sig_no)
{
	if( sig_no == SIGINT ){		
		d->running += 1;
		warnx("CTRL-C caught, shutting down\n");
		sigaction(sig_no, &old_action, NULL);
	}
}

static void setup_handlers()
{
	struct sigaction act;
	memset(&act, 0, sizeof(act));
	act.sa_handler = &ctrl_c_handler;
	sigaction(SIGINT, &act, &old_action);
}

int main(int argc, char **argv)
{
	int i, ok;
	void *zctx;

	if( argc < 2 ) {	
		fprintf(stderr, "Usage: %s <module.so> [op=tcp://... ]\n\n", argv[0]);
		fprintf(stderr, "Example:\n# %s mod-leveldb.so \\\n", argv[0]);
		fprintf(stderr,
			"     get=rep@tcp://127.0.0.1:17700 \\\n"
			"     put=pull@tcp://127.0.0.1:17701 \\\n"
			"     del=pull@tcp://127.0.0.1:17702 &\n"
		);

		printf("\ndbZMQ version v%.1f\n", VERSION);
		return( EXIT_FAILURE );
	}	

	d = dbz_open(argv[1]);
	if( ! d ) return( EXIT_FAILURE );

	zctx = zmq_init(1);
	assert(zctx != NULL);

	for( i = 2 ; i < argc; i++ ) {
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
		if( f->token ) {
			dbzmq_socket_t* token = (dbzmq_socket_t*)f->token;
			zmq_close(token->socket);
		}
		f++;
	}
	if( zctx ) zmq_term(zctx);
	dbz_close(d);
	return( EXIT_SUCCESS );
}

