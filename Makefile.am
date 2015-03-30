ACLOCAL_AMFLAGS = -I m4

CFLAGS = -std=c99 -g  $(GLIB_CFLAGS) $(GIO_CFLAGS)
LDFLAGS = $(GLIB_LIBS) $(GIO_LIBS)

SUBDIRS = h264bitstream

noinst_LIBRARIES = tslib/libts.a
bin_PROGRAMS = tslib/apps/ts_validate_mult_segment
noinst_PROGRAMS = tests/check_tslib
TESTS = tests/check_tslib

tslib_libts_a_SOURCES = tslib/bitreader.c tslib/cets_ecm.c tslib/crc32m.c tslib/descriptors.c tslib/isobmff.c tslib/log.c \
        tslib/mpd.c tslib/mpeg2ts_demux.c tslib/pes.c tslib/pes_demux.c tslib/psi.c tslib/segment_validator.c tslib/ts.c
tslib_libts_a_CFLAGS = -isystem$(top_srcdir)/h264bitstream $(LIBXML2_CFLAGS) \
        -O -fdiagnostics-color=always \
        -fstack-protector -fPIE -ftrapv -D_FORTIFY_SOURCE=2 \
        -Wall -Winit-self -Wstrict-overflow=3 -Wfloat-equal -Wshadow \
        -Wpointer-arith -Wtype-limits -Wbad-function-cast -Wcast-qual \
        -Wstrict-prototypes -Wold-style-definition -Wmissing-prototypes -Wno-multichar \
        -Wredundant-decls -Wvla -Wformat-security

tslib_apps_ts_validate_mult_segment_SOURCES = tslib/apps/ts_validate_mult_segment.c
tslib_apps_ts_validate_mult_segment_CFLAGS = -I$(top_srcdir)/tslib -I$(top_srcdir)/h264bitstream $(LIBXML2_CFLAGS)
tslib_apps_ts_validate_mult_segment_LDADD = tslib/libts.a h264bitstream/libh264bitstream.la $(LIBXML2_LIBS)

tests_check_tslib_SOURCES = tests/cets_ecm.c
tests_check_tslib_CFLAGS = -I$(top_srcdir)/tslib -I$(top_srcdir)/h264bitstream $(LIBXML2_CFLAGS) $(CHECK_CFLAGS)
tests_check_tslib_LDADD = tslib/libts.a h264bitstream/libh264bitstream.la $(LIBXML2_LIBS) $(CHECK_LIBS)

MAINTAINERCLEANFILES = Makefile.in aclocal.m4 configure config-h.in
