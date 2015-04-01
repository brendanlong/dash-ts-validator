ACLOCAL_AMFLAGS = -I m4

CFLAGS = -std=c99 -g -isystem$(top_srcdir)/h264bitstream -I$(top_srcdir)/tslib \
    -O -fdiagnostics-color=always \
    -fstack-protector -fPIE -ftrapv -D_FORTIFY_SOURCE=2 \
    -Wall -Winit-self -Wstrict-overflow=3 -Wfloat-equal -Wshadow \
    -Wpointer-arith -Wtype-limits -Wbad-function-cast -Wcast-qual \
    -Wstrict-prototypes -Wold-style-definition -Wmissing-prototypes -Wno-multichar \
    -Wredundant-decls -Wvla -Wformat-security -Wno-format-zero-length \
    $(GLIB_CFLAGS) $(GIO_CFLAGS) $(LIBPCRE_CFLAGS) $(LIBXML2_CFLAGS)
LDFLAGS = h264bitstream/libh264bitstream.la $(GLIB_LIBS) $(GIO_LIBS) $(LIBPCRE_LIBS) $(LIBXML2_LIBS)

SUBDIRS = h264bitstream

noinst_LIBRARIES = tslib/libts.a
bin_PROGRAMS = tslib/apps/ts_validate_mult_segment
noinst_PROGRAMS = tests/check_bitreader tests/check_cets_ecm tests/check_descriptors tests/check_mpd tests/check_psi
TESTS = tests/check_bitreader tests/check_cets_ecm tests/check_descriptors tests/check_mpd tests/check_psi

tslib_libts_a_SOURCES = tslib/bitreader.c tslib/cets_ecm.c tslib/crc32m.c tslib/descriptors.c tslib/isobmff.c \
        tslib/log.c tslib/mpd.c tslib/mpeg2ts_demux.c tslib/pes.c tslib/pes_demux.c tslib/psi.c \
        tslib/segment_validator.c tslib/ts.c

tslib_apps_ts_validate_mult_segment_SOURCES = tslib/apps/ts_validate_mult_segment.c
tslib_apps_ts_validate_mult_segment_LDADD = tslib/libts.a

tests_check_bitreader_SOURCES = tests/bitreader.c
tests_check_bitreader_CFLAGS = $(CFLAGS) $(CHECK_CFLAGS)
tests_check_bitreader_LDADD = tslib/libts.a $(LDFLAGS) $(CHECK_LIBS)

tests_check_cets_ecm_SOURCES = tests/cets_ecm.c
tests_check_cets_ecm_CFLAGS = $(CFLAGS) $(CHECK_CFLAGS)
tests_check_cets_ecm_LDADD = tslib/libts.a $(LDFLAGS) $(CHECK_LIBS)

tests_check_descriptors_SOURCES = tests/descriptors.c
tests_check_descriptors_CFLAGS = $(CFLAGS) $(CHECK_CFLAGS)
tests_check_descriptors_LDADD = tslib/libts.a $(LDFLAGS) $(CHECK_LIBS)

tests_check_mpd_SOURCES = tests/mpd.c
tests_check_mpd_CFLAGS = $(CFLAGS) $(CHECK_CFLAGS)
tests_check_mpd_LDADD = tslib/libts.a $(LDFLAGS) $(CHECK_LIBS)

tests_check_psi_SOURCES = tests/psi.c
tests_check_psi_CFLAGS = $(CFLAGS) $(CHECK_CFLAGS)
tests_check_psi_LDADD = tslib/libts.a $(LDFLAGS) $(CHECK_LIBS)

MAINTAINERCLEANFILES = Makefile.in aclocal.m4 configure config-h.in
