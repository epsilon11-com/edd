BUILDDIR = ../build

SOURCES = $(wildcard *.c)
OBJS = $(patsubst %.c,$(BUILDDIR)/%.o,$(SOURCES))

BIN = $(BUILDDIR)/edd
LIBS = -L../lib -lscsicmd -lpcre2-8 -lm

INCLUDES = -Iinclude -I../include -I../lib/include

CC = /usr/bin/gcc
CFLAGS = $(INCLUDES) -g

.PHONY: all clean ctags

all: ctags edd

clean:
	rm $(OBJS) $(BIN) tags

ctags:
	/usr/bin/ctags *.h *.c


depend: .depend

.depend: $(SOURCES)
	rm -f ./.depend
	$(CC) $(CFLAGS) -MM $^>>./.depend;

include .depend

$(OBJS): $(BUILDDIR)/%.o : %.c
	$(CC) $(CFLAGS) -c $< $(LIBS) -o $@


#%.o: %.c
#	$(CC) -c -g $(CFLAGS) $< -o $@

edd: $(OBJS)
	$(CC) $(OBJS) $(LIBS) -o $(BIN)

