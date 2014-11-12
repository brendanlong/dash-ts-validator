SHELL = /bin/sh

SUBDIRS = logging tslib libstructures h264bitstream ISOBMFF

all: subdirs
default: all

ISOBMFF: logging libstructures
tslib: logging libstructures h264bitstream ISOBMFF

$(SUBDIRS):
	@if [ -f "$@/autogen.sh" ] && [ ! -f "$@/configure" ] ; then \
	    cd "$@" ; \
	    ./autogen.sh ; \
	    if [ ! -f Makefile ] ; then \
	        ./configure ; \
	    fi ; \
	    cd .. ; \
	fi

	$(MAKE) -C $@

subdirs: $(SUBDIRS)

subdirs-clean:
	for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean; \
	done


clean: subdirs-clean

.PHONY: subdirs $(SUBDIRS) subdirs-clean
