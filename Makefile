UNAME := $(shell uname)
DEBUG =	-g -ggdb -DDEBUG

# Goanna Studio by http://redlizards.com/
#CC = goannacc --all-checks

CC = gcc
CFLAGS = -std=c99 -O0 -ggdb -Wall -Wextra -fPIC $(DEBUG)	

BUILD_MODULE = $(CC) $(CFLAGS) -shared -o
STRIP_EXE = strip -s -R .note -R .comment
STRIP_MODULE = strip -s -R .note -R .comment -K i_speak_db

SVR_OBJS = \
	./server/ae.o \
	./server/anet.o \
	./server/request.o \
	./server/response.o \
	./server/zmalloc.o \

BENCH_OBJS = ./bench/db-bench.c

NESSDB=../../nessDB-original/
WREN=

MODS = $(OUT)mod-null.so $(OUT)mod-tcbdb.so $(OUT)mod-sqlite.so # mod-nessdb.so  $(OUT)mod-mongodb.so
MAINS = $(OUT)db-zmq $(OUT)db-bench # db-server

OUT = build/

ALL = $(OUT) $(MAINS) $(MODS)

all: $(ALL)

$(OUT):
	mkdir -p $@

release: all
	$(STRIP_MODULE) $(MODS)
	$(STRIP_EXE) $(MAINS)
	-upx -9 $(MAINS)

clean:
	-rm -rf $(OUT)
	-rm -f $(ALL)
	-rm -f bench/*.o server/*.o mod/*.o

cleandb:
	-rm -rf ndbs database.tcbdb.dat sqlite3.dat

$(LIBRARY): $(LIB_OBJS)
	@rm -f $@
	@$(AR) -rs $@ $(LIB_OBJS)
	@rm -f $(LIB_OBJS)

.PHONY: analyze
ANALYZE:
	cppcheck -q .
	# Both 'rats' and 'flawfinder' are relatively useless
	#rats .
	#flawfinder *

.PHONY: BENCHMARK
BENCHMARK: db-bench
	./db-bench -k 32 -e 5000000 rwmix > $@

$(OUT)db-server: server/db-server.o $(SVR_OBJS:.o=.c) $(LIB_OBJS:.o=.c)
	$(CC) $(CFLAGS) -o $@ $+

$(OUT)db-bench: bench/db-bench.c server/db-zmq.c $(LIB_OBJS:.o=.c)
	$(CC) $(CFLAGS) -o $@ $+ -ldl

$(OUT)db-zmq: server/db-zmq.c $(LIB_OBJS:.o=.c)
	$(CC) $(CFLAGS) -DDBZ_MAIN -o $@ $+ -lzmq -ldl

$(OUT)mod-nessdb.so: mod/nessdb.c server/sha1.c $(NESSDB)/libnessdb.a
	$(BUILD_MODULE) $@ -I$(NESSDB)/engine $+

$(OUT)mod-null.so: mod/null.c
	$(BUILD_MODULE) $@ $+

$(OUT)mod-tcbdb.so: mod/tcbdb.c
	$(BUILD_MODULE) $@ $+ -ltokyocabinet

$(OUT)mod-wrendb.so: mod/wrendb.c mod/wren/wvm.c
	$(BUILD_MODULE) $@ $+

$(OUT)mod-sqlite.so: mod/sqlite.c -lsqlite3
	$(BUILD_MODULE) $@ $+

$(OUT)mod-mongodb.so: mod/mongodb.c
	$(BUILD_MODULE) $@ $+ -Imod/mongo-c-driver/src mod/mongo-c-driver/libbson.a mod/mongo-c-driver/libmongoc.a