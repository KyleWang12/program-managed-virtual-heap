PROG = test
SRCS = test.c pm_heap.c
OBJS = ${SRCS:%.c=%.o}
LDLIBS = -lpthread

$(PROG): $(OBJS)
	$(CC) -o $(PROG) $(OBJS) $(LDLIBS)

clean:
	-rm $(OBJS)