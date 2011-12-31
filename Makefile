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
STRIP_MODULE = strip -R .note -R .comment -K i_speak_db -s

BENCH_OBJS = ./bench/db-bench.c

NESSDB=../../nessDB-original/

MODS =	$(OUT)mod-null.so \
		$(OUT)mod-tcbdb.so \
		$(OUT)mod-mongodb.so \
		$(OUT)mod-leveldb.so		
		# Currently not working
		#$(OUT)mod-sqlite.so
		#$(OUT)mod-nessdb.so

MAINS = $(OUT)db-zmq $(OUT)db-bench $(OUT)leveldb-zmq

OUT = build/

ALL = $(OUT) $(MAINS) $(MODS)

all: $(ALL)

$(OUT):
	echo $(UNAME)
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

.PHONY: analyze
ANALYZE:
	cppcheck -q .
	## Both 'rats' and 'flawfinder' are overly verbose
	## Hard to use with all the default checks enabled.
	#rats .
	#flawfinder *

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

# Can't build without patch to db_get
$(OUT)mod-nessdb.so: mod/nessdb.c server/sha1.c $(NESSDB)/libnessdb.a
	$(BUILD_MODULE) $@ -I$(NESSDB)/engine $+

$(OUT)mod-null.so: mod/null.c
	$(BUILD_MODULE) $@ $+

$(OUT)mod-tcbdb.so: mod/tcbdb.c
	$(BUILD_MODULE) $@ $+ -ltokyocabinet

$(OUT)mod-sqlite.so: mod/sqlite.c -lsqlite3
	$(BUILD_MODULE) $@ $+

$(OUT)mod-mongodb.so: mod/mongodb.c
	$(BUILD_MODULE) $@ $+ -DMONGO_HAVE_STDINT -Imod/mongo-c-driver/src mod/mongo-c-driver/libbson.a mod/mongo-c-driver/libmongoc.a

$(OUT)mod-leveldb.so: mod/leveldb.c mod/leveldb/libleveldb.a
	$(CXX) $(CXXFLAGS) -pthread -shared -o $@ $+ -Imod/leveldb/include

$(OUT)leveldb-zmq: mod/leveldb.c server/db-zmq.c mod/leveldb/libleveldb.a
	$(CXX) $(CXXFLAGS) -DDBZ_STATIC_MAIN -pthread -o $@ $+ -Imod/leveldb/include -ldl -lzmq
