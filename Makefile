CC=gcc
CFLAGS=-g3 -Wall


OBJS=logger.o task.o main.o

coro:$(OBJS)
	$(CC) -o $@ $(OBJS)
	@echo "done!"

clean:
	-rm -f $(OBJS)
	-rm -f coro coro.exe

task.c main.c: task.h
