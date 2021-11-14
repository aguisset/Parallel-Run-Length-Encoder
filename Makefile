CC=g++
CFLAGS=-std=c++11

.PHONY: all
all: nyuenc

nyuenc: nyuenc.o
	$(CC) $(CFLAGS) -o nyuenc nyuenc.o -pthread

nyuenc.o: nyuenc.cpp
	$(CC) $(CCFLAGS) -c nyuenc.cpp -pthread

.PHONY: clean
clean:
	rm -f *.o nyuenc