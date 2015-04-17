ACLOCAL_AMFLAGS = -I m4

AM_CFLAGS = -std=c99 -g -O2 -isystem$(top_srcdir)/h264bitstream -I$(top_srcdir)/tslib \
    -fstack-protector -fPIE -ftrapv -D_FORTIFY_SOURCE=2 \
    -Wall -Winit-self -Wstrict-overflow=3 -Wfloat-equal -Wshadow \
    -Wpointer-arith -Wtype-limits -Wbad-function-cast -Wcast-qual \
    -Wstrict-prototypes -Wold-style-definition -Wmissing-prototypes -Wno-multichar \
    -Wredundant-decls -Wvla -Wformat-security -Wno-format-zero-length \
    -Werror=implicit-function-declaration \
    $(GLIB_CFLAGS) $(GIO_CFLAGS) $(LIBPCRE_CFLAGS) $(LIBXML2_CFLAGS)
AM_LDFLAGS = h264bitstream/libh264bitstream.la $(GLIB_LIBS) $(GIO_LIBS) $(LIBPCRE_LIBS) $(LIBXML2_LIBS)

SUBDIRS = h264bitstream

noinst_LIBRARIES = tslib/libts.a
bin_PROGRAMS = tslib/apps/ts_validate_mult_segment
TESTS = tests/check_bitreader tests/check_cets_ecm tests/check_descriptors tests/check_isobmff tests/check_mpd \
        tests/check_pes tests/check_pes_demux tests/check_psi tests/check_ts
noinst_PROGRAMS = $(TESTS)

tslib_libts_a_SOURCES = tslib/bitreader.c tslib/cets_ecm.c tslib/crc32m.c tslib/descriptors.c tslib/isobmff.c \
        tslib/log.c tslib/mpd.c tslib/mpeg2ts_demux.c tslib/pes.c tslib/pes_demux.c tslib/psi.c \
        tslib/segment_validator.c tslib/ts.c

tslib_apps_ts_validate_mult_segment_SOURCES = tslib/apps/ts_validate_mult_segment.c
tslib_apps_ts_validate_mult_segment_LDADD = tslib/libts.a $(AM_LDFLAGS)

TEST_CFLAGS = $(AM_CFLAGS) $(CHECK_CFLAGS)
TEST_LIBS = tslib/libts.a $(AM_LDFLAGS) $(CHECK_LIBS) -lrt

tests_check_bitreader_SOURCES = tests/bitreader.c tests/main.c
tests_check_bitreader_CFLAGS = $(TEST_CFLAGS)
tests_check_bitreader_LDADD = $(TEST_LIBS)

tests_check_cets_ecm_SOURCES = tests/cets_ecm.c tests/main.c
tests_check_cets_ecm_CFLAGS = $(TEST_CFLAGS)
tests_check_cets_ecm_LDADD = $(TEST_LIBS)

tests_check_descriptors_SOURCES = tests/descriptors.c tests/main.c
tests_check_descriptors_CFLAGS = $(TEST_CFLAGS)
tests_check_descriptors_LDADD = $(TEST_LIBS)

tests_check_isobmff_SOURCES = tests/isobmff.c tests/main.c
tests_check_isobmff_CFLAGS = $(TEST_CFLAGS)
tests_check_isobmff_LDADD = $(TEST_LIBS)

tests_check_mpd_SOURCES = tests/mpd.c tests/main.c
tests_check_mpd_CFLAGS = $(TEST_CFLAGS)
tests_check_mpd_LDADD = $(TEST_LIBS)

tests_check_pes_SOURCES = tests/pes.c tests/main.c
tests_check_pes_CFLAGS = $(TEST_CFLAGS)
tests_check_pes_LDADD = $(TEST_LIBS)

tests_check_pes_demux_SOURCES = tests/pes_demux.c tests/main.c
tests_check_pes_demux_CFLAGS = $(TEST_CFLAGS)
tests_check_pes_demux_LDADD = $(TEST_LIBS)

tests_check_psi_SOURCES = tests/psi.c tests/main.c
tests_check_psi_CFLAGS = $(TEST_CFLAGS)
tests_check_psi_LDADD = $(TEST_LIBS)

tests_check_ts_SOURCES = tests/ts.c tests/main.c
tests_check_ts_CFLAGS = $(TEST_CFLAGS)
tests_check_ts_LDADD = $(TEST_LIBS)

.PHONY: check-valgrind ;
check-valgrind: $(TESTS)
	@for test in $$(echo $(TESTS) | sed 's/tests\//tests\/.libs\//g') ; do \
	    CK_FORK=no LD_LIBRARY_PATH=h264bitstream/.libs valgrind --error-exitcode=1 --leak-resolution=low --quiet \
	        --leak-check=full --show-possibly-lost=no --suppressions=tests/valgrind.suppressions $${test} ; \
	done

MAINTAINERCLEANFILES = Makefile.in aclocal.m4 configure config-h.in
