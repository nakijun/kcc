.POSIX:

include ../config.mk

OBJS = types.o decl.o lex.o error.o symbol.o main.o expr.o \
	code.o stmt.o cpp.o fold.o

all: cc1


$(OBJS) : cc1.h ../inc/cc.h ../inc/sizes.h arch/$(ARCH)/arch.h

cc1: $(OBJS) ../lib/libcc.a
	$(CC) $(LDFLAGS) $(OBJS) ../lib/libcc.a -o $@

cpp: cc1
	ln -f cc1 cpp

test:
	cd tests && ./chktest.sh *.c

clean:
	rm -f $(OBJS)
	rm -f cc1 cpp

