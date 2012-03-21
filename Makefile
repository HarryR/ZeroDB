UNAME := $(shell uname)
DEFS = -DPLATFORM=$(UNAME) -DVERSION=1.8

## ""Goanna Central supports whole program analysis for checking,
## e.g., that null pointers are not passed on and dereferenced
## in other functions and automatically tracks potential ranges
## of variables, detecting possible buffer overflows.""
## - http://redlizards.com/ 
#
#CC = goannacc --all-checks
#CXX = goannac++ --all-checks

CC = gcc
CXX = g++
DEBUG =	-g -ggdb -DDEBUG 

#CC = clang
#CXX = clang++

CXXFLAGS = -O0 -ggdb -Wall -Wextra -fPIC $(DEBUG) $(DEFS)
CFLAGS = -std=c99 $(CXXFLAGS)

BUILD_MODULE = $(CC) $(CFLAGS) -shared -o
STRIP_EXE = strip -s -R .note -R .comment
STRIP_MODULE = strip -R .note -R .comment -s --strip-unneeded -K i_speak_db

#BENCH_OBJS = ./bench/db-bench.c

MODS =	$(OUT)mod-null.so \
		$(OUT)mod-tcbdb.so \
		$(OUT)mod-mongodb.so \
		$(OUT)mod-leveldb.so \
		$(OUT)mod-nessdb.so \
		$(OUT)mod-sqlite.so

MAINS = $(OUT)db-zmq #$(OUT)db-bench

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
	scons -C mod/mongo-c-driver/ -c
	make -C mod/leveldb/ clean
	make -C mod/nessdb/ clean

cleandb:
	-rm -rf ndbs database.tcbdb.dat sqlite3.dat

.PHONY: ANALYZE
ANALYZE:
	cppcheck --enable=all -q server/*.c mod/*.c

.PHONY: BENCHMARK
BENCHMARK: $(MODS) $(MAINS)
	for MOD in $(MODS) ; do ./build/db-bench $$MOD null ; done
	for MOD in $(MODS) ; do ./build/db-bench $$MOD removewrite-sequence ; done
	for MOD in $(MODS) ; do ./build/db-bench $$MOD readwrite-sequence ; done
	for MOD in $(MODS) ; do ./build/db-bench $$MOD readwrite-random ; done
	for MOD in $(MODS) ; do ./build/db-bench $$MOD readwrite-pseudorandom ; done

########################################################

$(OUT)db-bench: bench/db-bench.c server/db-zmq.c
	$(CC) $(CFLAGS) -o $@ $+ -ldl

$(OUT)db-zmq: server/db-zmq.c
	$(CC) $(CFLAGS) -DDBZ_MAIN -o $@ $+ -lzmq -ldl

########################################################

$(OUT)mod-nessdb.so: mod/nessdb.c mod/nessdb/libnessdb.a
	$(BUILD_MODULE) $@ -Imod/nessdb/engine $+

mod/nessdb/libnessdb.a:
	CFLAGS="-fPIC" make -C mod/nessdb

$(OUT)mod-null.so: mod/null.c
	$(BUILD_MODULE) $@ $+

$(OUT)mod-tcbdb.so: mod/tcbdb.c
	$(BUILD_MODULE) $@ $+ -ltokyocabinet

$(OUT)mod-sqlite.so: mod/sqlite.c -lsqlite3
	$(BUILD_MODULE) $@ $+

$(OUT)mod-mongodb.so: mod/mongodb.c mod/mongo-c-driver/libmongoc.a
	$(BUILD_MODULE) $@ $+ -DMONGO_HAVE_STDINT -Imod/mongo-c-driver/src mod/mongo-c-driver/libbson.a mod/mongo-c-driver/libmongoc.a

mod/mongo-c-driver/libmongoc.a:
	scons -C mod/mongo-c-driver CFLAGS="-fPIC"

$(OUT)mod-leveldb.so: mod/leveldb.c mod/leveldb/libleveldb.a
	$(CXX) $(CXXFLAGS) -pthread -shared -o $@ $+ -Imod/leveldb/include

mod/leveldb/libleveldb.a:
	make -C mod/leveldb
