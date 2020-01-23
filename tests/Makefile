CC=gcc
CFLAGS=-I .. $(shell pkg-config --cflags check)
LDFLAGS=$(shell pkg-config --libs check)

OBJS=task.o logger.o test-lib.o

# -- main target : compile test suite and execute it
check: run-tu
	-./run-tu
	gcov task

run-tu: $(OBJS)
	$(CC) --coverage -o $@ $(OBJS) $(LDFLAGS)

task.o: ../task.h
task.o: ../task.c
	$(CC) -c --coverage $(CFLAGS) -o $@ $<

logger.o: ../logger.c
	$(CC) -c --coverage $(CFLAGS) -o $@ $<

clean:
	-rm *.gcno
	-rm *.gcov
	-rm *.gcda
	-rm *.o
	-rm *.exe
