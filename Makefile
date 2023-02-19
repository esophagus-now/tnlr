all: main

#CC = clang
CC = gcc -fno-diagnostics-show-caret

# https://stackoverflow.com/questions/714100/os-detecting-makefile
ifeq ($(OS),Windows_NT)
	override CFLAGS += -g -Wall -Wno-unused-command-line-argument -pthread -lm -l:liblua.a -levent -lws2_32
else
	# Original line from replit
	override CFLAGS += -g -Wall -Wno-unused-command-line-argument -pthread -lm -lssl -lcrypto -llua -levent
endif
SRCS = main.c
OBJS = $(SRCS:.c=.o)
DEPS = $(SRCS:.c=.d)

%.d: %.c
	@set -e; rm -f $@; \
	$(CC) -MM $(CFLAGS) $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

include $(DEPS)

main: $(OBJS)
	$(CC) $(OBJS) -o "$@" $(CFLAGS) 

main-debug: $(OBJS)
	$(CC) $(CFLAGS) -O0 $(OBJS) -o "$@"

clean:
	rm -f $(OBJS) $(DEPS) main main-debug
