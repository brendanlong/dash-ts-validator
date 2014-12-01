SHELL = /bin/sh

CC = gcc
CFLAGS = -std=c99 -O0 -g -Wall -Wno-unused-variable -Wno-format -MD

SRCS = $(wildcard *.c) 
OBJS = $(SRCS:%.c=%.o)

INCLUDES = -I . -I../common -I../libstructures/ -I../h264bitstream/ -I../ISOBMFF/  -I../logging/
LIBS = -L../h264bitstream/.libs -lh264bitstream  -L../ISOBMFF -lisobmff  -L../logging/ -llogging   -L../libstructures/ -ldatastruct -lm
DEPEND_LIBS = ../ISOBMFF/libisobmff.a ../logging/liblogging.a ../libstructures/libdatastruct.a

CFLAGS  += $(INCLUDES)
LDFLAGS += $(LIBS)

BINARIES = apps/ts_split apps/ts_validate_single_segment apps/ts_validate_mult_segment

all: $(BINARIES)

apps/ts_split: apps/ts_split.o $(OBJS) $(DEPEND_LIBS)
	$(CC) $(CFLAGS) -o apps/ts_split apps/ts_split.o $(OBJS) $(LIBS)

apps/ts_validate_single_segment: apps/ts_validate_single_segment.o $(OBJS) $(DEPEND_LIBS)
	$(CC) $(CFLAGS) -o apps/ts_validate_single_segment apps/ts_validate_single_segment.o $(OBJS) $(LIBS)

apps/ts_validate_mult_segment: apps/ts_validate_mult_segment.o $(OBJS) $(DEPEND_LIBS)
	$(CC) $(CFLAGS) -o apps/ts_validate_mult_segment apps/ts_validate_mult_segment.o $(OBJS) $(LIBS)

clean:
	rm -f $(OBJS) apps/*.o $(BINARIES) *.P apps/*.P core

%.o : %.c
	$(CC) $(CFLAGS) -c -o $@ $<
	@cp $*.d $*.P; \
	  sed -e 's/#.*//' -e 's/^[^:]*: *//' -e 's/ *\\$$//' \
	      -e '/^$$/ d' -e 's/$$/ :/' < $*.d >> $*.P; \
	  rm -f $*.d

-include $(SRCS:%.c=%.P) $(wildcard apps/*.P)
