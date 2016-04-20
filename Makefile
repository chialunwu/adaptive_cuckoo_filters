CC = g++

# Uncomment one of the following to switch between debug and opt mode
#OPT = -O3 -DNDEBUG
OPT = -g -ggdb

CFLAGS += -Wall -c -I. -I./include -I/usr/include/ -I./src/ $(OPT)

LDFLAGS+= -Wall -lpthread -lssl -lcrypto

LIBOBJECTS = \
	./src/hashutil.o \
	./src/hash_functions.o\

HEADERS = $(wildcard src/*.h)

TEST = test

all: $(TEST)

clean:
	rm -f $(TEST) */*.o

test_ori: example/test_ori.o $(LIBOBJECTS) 
	$(CC) example/test_ori.o $(LIBOBJECTS) $(LDFLAGS) -o $@

test_busc: example/test_busc.o $(LIBOBJECTS) 
	$(CC) example/test_busc.o $(LIBOBJECTS) $(LDFLAGS) -o $@

test_big_cf: example/test_big_cf.o $(LIBOBJECTS) 
	$(CC) example/test_big_cf.o $(LIBOBJECTS) $(LDFLAGS) -o $@

test_split_cf: example/test_split_cf.o $(LIBOBJECTS) 
	$(CC) example/test_split_cf.o $(LIBOBJECTS) $(LDFLAGS) -o $@

test_split_cf_adaptive: example/test_split_cf_adaptive.o $(LIBOBJECTS) 
	$(CC) example/test_split_cf_adaptive.o $(LIBOBJECTS) $(LDFLAGS) -o $@

%.o: %.cc ${HEADERS} Makefile
	$(CC) $(CFLAGS) $< -o $@

