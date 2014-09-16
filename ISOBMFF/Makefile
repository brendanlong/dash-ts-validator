SHELL = /bin/sh

CC = gcc
CFLAGS = -std=c99 -O0 -g -Wall -Wno-unused-variable
#CFLAGS = -std=c99 -O2 -ffast-math -g -pedantic -pipe -Wall -Wextra

LD = gcc
LDFLAGS += -g -static

AR = ar
ARFLAGS = rcls

RANLIB = ranlib
RM = rm -f

SRCS = $(wildcard *.c) 
OBJS = $(SRCS:%.c=%.o)

INCLUDES = -I . -I../common -I../logging
LIBS = -L . -lisobmff -L../logging/ -llogging -L../libstructures/ -ldatastruct -lm

CFLAGS  += $(INCLUDES)
LDFLAGS += $(LIBS)

BINARIES = ISOBMFF

all: libisobmff.a $(BINARIES)

libisobmff.a: $(OBJS)
	$(AR) $(ARFLAGS) $@ $^ 
	$(RANLIB) $@

ISOBMFF: ISOBMFF.c
	$(CC) $(CFLAGS) -o ISOBMFFApp ISOBMFFApp.c $(LIBS)

clean:
	rm -f $(OBJS) *.a $(BINARIES) core
