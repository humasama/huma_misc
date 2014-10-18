CC=gcc
PGMS=mc-mapping

CFLAGS=-Wall

all: $(PGMS)
mc-mapping: mc-mapping.c
	$(CC) $< -O0 -o $@ -lrt -g

.PHONY : clean
clean:
	rm $(wildcard *.o) $(wildcard  *~) $(PGMS)
