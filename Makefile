CC=g++
CFLAGS=-g -Wall

.PHONY: all
all: nyuenc

nyuenc: nyuenc.o 

nyuenc.o: nyuenc.cpp

.PHONY: clean
clean:
	rm -f *.o nyuenc
