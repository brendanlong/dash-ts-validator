ACLOCAL_AMFLAGS = -I m4

CFLAGS = -std=c99 -g -Wall $(GLIB_CFLAGS) -Werror
LDFLAGS = $(GLIB_LIBS)

SUBDIRS = h264bitstream

noinst_LIBRARIES = logging/liblogging.a ISOBMFF/libisobmff.a tslib/libts.a
noinst_PROGRAMS = tslib/apps/ts_split tslib/apps/ts_validate_mult_segment \
        tslib/apps/ts_validate_single_segment


logging_liblogging_a_SOURCES = logging/log.c


ISOBMFF_libisobmff_a_SOURCES = ISOBMFF/ISOBMFF.c


tslib_libts_a_SOURCES = tslib/cas.c tslib/descriptors.c tslib/mpd.c \
        tslib/pes.c tslib/section.c tslib/std.c tslib/ts.c tslib/crc32m.c \
        tslib/mpeg2ts_demux.c tslib/psi.c tslib/segment_validator.c tslib/tpes.c
tslib_libts_a_CFLAGS = -I$(top_srcdir)/h264bitstream -I$(top_srcdir)/logging \
        -I$(top_srcdir)/ISOBMFF $(LIBXML2_CFLAGS) $(LIBPCRE_CFLAGS)

tslib_CFLAGS = -I$(top_srcdir)/tslib -I$(top_srcdir)/h264bitstream \
        -I$(top_srcdir)/logging -I$(top_srcdir)/ISOBMFF
tslib_LDADD = tslib/libts.a ISOBMFF/libisobmff.a \
        logging/liblogging.a logging/liblogging.a h264bitstream/libh264bitstream.la

tslib_apps_ts_split_SOURCES = tslib/apps/ts_split.c
tslib_apps_ts_split_CFLAGS = $(tslib_CFLAGS)
tslib_apps_ts_split_LDADD = $(tslib_LDADD)

tslib_apps_ts_validate_mult_segment_SOURCES = tslib/apps/ts_validate_mult_segment.c
tslib_apps_ts_validate_mult_segment_CFLAGS = $(tslib_CFLAGS) $(LIBXML2_CFLAGS)
tslib_apps_ts_validate_mult_segment_LDADD = $(tslib_LDADD) $(LIBXML2_LIBS) \
        $(LIBPCRE_LIBS)

tslib_apps_ts_validate_single_segment_SOURCES = tslib/apps/ts_validate_single_segment.c
tslib_apps_ts_validate_single_segment_CFLAGS = $(tslib_CFLAGS)
tslib_apps_ts_validate_single_segment_LDADD = $(tslib_LDADD)


MAINTAINERCLEANFILES = Makefile.in aclocal.m4 configure \
			  config-h.in
