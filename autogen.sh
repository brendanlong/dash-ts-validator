#!/bin/bash
autoreconf --install --verbose
./configure --enable-silent-rules $@
