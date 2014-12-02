#!/bin/bash
autoreconf --verbose --force --install --make
./configure --enable-silent-rules $@
