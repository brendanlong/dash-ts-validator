SHELL = /bin/sh

CC = gcc
CFLAGS = -std=c99 -O0 -g -Wall -Wno-unused-variable

LD = gcc
LDFLAGS += -g

AR = ar
ARFLAGS = rcls
RANLIB = ranlib

SRCS = $(wildcard *.c) 
OBJS = $(SRCS:%.c=%.o)

INCLUDES = -I . -I../common -I../logging
LIBS = -L../logging/ -llogging -L../libstructures/ -ldatastruct -lm

CFLAGS  += $(INCLUDES) -MD
LDFLAGS += $(LIBS)

BINARIES = ISOBMFFApp

all: libisobmff.a $(BINARIES)

libisobmff.a: $(OBJS)
	$(AR) $(ARFLAGS) $@ $^
	$(RANLIB) $@

ISOBMFFApp: $(OBJS)
	$(LD) -o ISOBMFFApp $^ $(LDFLAGS)

%.o : %.c
	$(CC) $(CFLAGS) -c -o $@ $<
	@cp $*.d $*.P; \
	  sed -e 's/#.*//' -e 's/^[^:]*: *//' -e 's/ *\\$$//' \
	      -e '/^$$/ d' -e 's/$$/ :/' < $*.d >> $*.P; \
	  rm -f $*.d

clean:
	rm -f $(OBJS) *.a *.P $(BINARIES) core

-include $(SRCS:%.c=%.P)
