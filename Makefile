CC=gcc
CFLAGS=-g3 -Wall

OBJS=logger.o task.o

libfiber.a: $(OBJS)
	$(AR) -c $@ $(OBJS)
	@echo "done!"

clean:
	-rm -f $(OBJS)
	-rm -f libfiber.a

task.c: taskint.h task.h logger.h

logger.c: logger.h

