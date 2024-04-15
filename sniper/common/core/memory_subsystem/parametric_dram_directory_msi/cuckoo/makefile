CC=g++
CFLAGS=-O3 -g -Wall -Wextra -Wno-long-long -mrdrnd -m64 -lm
CUCKOOBINS=elastic_cuckoo

all:		$(CUCKOOBINS)

elastic_cuckoo:	elastic_cuckoo_table.cc blake2b-ref.cc city.cc
		$(CC) elastic_cuckoo_table.cc blake2b-ref.cc city.cc -o $@ $(CFLAGS)

clean:
		rm -rf *.o $(CUCKOOBINS)
