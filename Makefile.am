ACLOCAL_AMFLAGS = -I m4

CFLAGS = -std=c99 -Wall -I$(top_srcdir)/common -Wno-unused-variable -Wno-format

SUBDIRS = h264bitstream

noinst_LIBRARIES = logging/liblogging.a ISOBMFF/libisobmff.a \
        libstructures/libdatastruct.a tslib/libts.a
noinst_PROGRAMS = tslib/apps/ts_split tslib/apps/ts_validate_mult_segment \
        tslib/apps/ts_validate_single_segment


logging_liblogging_a_SOURCES = logging/log.c


libstructures_libdatastruct_a_SOURCES = libstructures/binheap.c \
        libstructures/hashtable_itr.c libstructures/varray.c \
        libstructures/hashtable_str.c libstructures/hashtable_str_rj.c \
        libstructures/vqarray.c libstructures/hashtable.c
libstructures_libdatastruct_a_CFLAGS = -Wno-unused-variable -std=c99 -g -O3 \
        -fexpensive-optimizations -funroll-loops -fomit-frame-pointer


ISOBMFF_libisobmff_a_SOURCES = ISOBMFF/ISOBMFF.c
ISOBMFF_libisobmff_a_CFLAGS = -I$(top_srcdir)/logging -I$(top_srcdir)/libstructures


tslib_libts_a_SOURCES = tslib/cas.c tslib/descriptors.c \
        tslib/pes.c tslib/section.c tslib/std.c tslib/ts.c tslib/crc32m.c \
        tslib/mpeg2ts_demux.c tslib/psi.c tslib/segment_validator.c tslib/tpes.c
tslib_libts_a_CFLAGS = -I$(top_srcdir)/logging -I$(top_srcdir)/libstructures \
        -I$(top_srcdir)/h264bitstream -I$(top_srcdir)/ISOBMFF

tslib_CFLAGS = -I$(top_srcdir)/tslib -I$(top_srcdir)/logging \
        -I$(top_srcdir)/libstructures -I$(top_srcdir)/h264bitstream \
        -I$(top_srcdir)/ISOBMFF
tslib_LDADD = tslib/libts.a ISOBMFF/libisobmff.a \
        libstructures/libdatastruct.a logging/liblogging.a \
        h264bitstream/libh264bitstream.la

tslib_apps_ts_split_SOURCES = tslib/apps/ts_split.c
tslib_apps_ts_split_CFLAGS = $(tslib_CFLAGS)
tslib_apps_ts_split_LDADD = $(tslib_LDADD)

tslib_apps_ts_validate_mult_segment_SOURCES = tslib/apps/ts_validate_mult_segment.c
tslib_apps_ts_validate_mult_segment_CFLAGS = $(tslib_CFLAGS)
tslib_apps_ts_validate_mult_segment_LDADD = $(tslib_LDADD)

tslib_apps_ts_validate_single_segment_SOURCES = tslib/apps/ts_validate_single_segment.c
tslib_apps_ts_validate_single_segment_CFLAGS = $(tslib_CFLAGS)
tslib_apps_ts_validate_single_segment_LDADD = $(tslib_LDADD)


MAINTAINERCLEANFILES = Makefile.in aclocal.m4 configure \
			  config-h.in
