SHELL = /bin/sh

CC = gcc
CFLAGS = -std=c99 -O0 -g -Wall -Wno-unused-variable -Wno-format

LD = gcc
LDFLAGS += -g -static

RM = rm -f

SRCS = $(wildcard *.c) 
OBJS = $(SRCS:%.c=%.o)

INCLUDES = -I . -I../common -I../libstructures/ -I../h264bitstream/ -I../ISOBMFF/  -I../logging/
LIBS = -L../h264bitstream/.libs -lh264bitstream  -L../ISOBMFF -lisobmff  -L../logging/ -llogging   -L../libstructures/ -ldatastruct -lm

CFLAGS  += $(INCLUDES)
LDFLAGS += $(LIBS)

BINARIES = apps/ts_split apps/ts_validate_single_segment apps/ts_validate_mult_segment

all: $(BINARIES)

apps/ts_split: apps/ts_split.c $(OBJS)
	$(CC) $(CFLAGS) -o apps/ts_split $^ $(LIBS)

apps/ts_validate_single_segment: apps/ts_validate_single_segment.c $(OBJS)
	$(CC) $(CFLAGS) -o apps/ts_validate_single_segment $^ $(LIBS)

apps/ts_validate_mult_segment: apps/ts_validate_mult_segment.c $(OBJS)
	$(CC) $(CFLAGS) -o apps/ts_validate_mult_segment $^ $(LIBS)

clean:
	rm -f $(OBJS) $(BINARIES) core
