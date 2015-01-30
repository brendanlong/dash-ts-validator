ACLOCAL_AMFLAGS = -I m4

CFLAGS = -std=c99 -g  $(GLIB_CFLAGS) $(GIO_CFLAGS)
LDFLAGS = $(GLIB_LIBS) $(GIO_LIBS)

SUBDIRS = h264bitstream

noinst_LIBRARIES = tslib/libts.a
noinst_PROGRAMS = tslib/apps/ts_validate_mult_segment tslib/apps/ts_validate_single_segment


tslib_libts_a_SOURCES = tslib/cas.c tslib/crc32m.c tslib/descriptors.c tslib/isobmff.c tslib/log.c tslib/mpd.c \
        tslib/mpeg2ts_demux.c tslib/pes.c tslib/pes_demux.c tslib/psi.c tslib/segment_validator.c tslib/ts.c
tslib_libts_a_CFLAGS = -isystem$(top_srcdir)/h264bitstream \
        -I$(top_srcdir)/ISOBMFF $(LIBXML2_CFLAGS) \
        -O \
        -fstack-protector -pie -fPIE -ftrapv -D_FORTIFY_SOURCE=2 -Wl,-z,relro,-z,now \
        -Werror \
        -Wall -Winit-self -Wstrict-overflow=3 -Wtrampolines -Wfloat-equal -Wshadow -Wstack-usage=4096 \
        -Wunsafe-loop-optimizations -Wpointer-arith -Wtype-limits -Wbad-function-cast -Wcast-qual -Wclobbered \
        -Wstrict-prototypes -Wold-style-definition -Wmissing-parameter-type -Wmissing-prototypes -Wno-multichar \
        -Wredundant-decls -Wvla -Wformat-security

tslib_CFLAGS = -I$(top_srcdir)/tslib -I$(top_srcdir)/h264bitstream
tslib_LDADD = tslib/libts.a h264bitstream/libh264bitstream.la

tslib_apps_ts_validate_mult_segment_SOURCES = tslib/apps/ts_validate_mult_segment.c
tslib_apps_ts_validate_mult_segment_CFLAGS = $(tslib_CFLAGS) $(LIBXML2_CFLAGS)
tslib_apps_ts_validate_mult_segment_LDADD = $(tslib_LDADD) $(LIBXML2_LIBS)

tslib_apps_ts_validate_single_segment_SOURCES = tslib/apps/ts_validate_single_segment.c
tslib_apps_ts_validate_single_segment_CFLAGS = $(tslib_CFLAGS)
tslib_apps_ts_validate_single_segment_LDADD = $(tslib_LDADD)


MAINTAINERCLEANFILES = Makefile.in aclocal.m4 configure config-h.in
