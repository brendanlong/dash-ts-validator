# TS Validator

[![Circle CI](https://circleci.com/gh/brendanlong/dash-ts-validator.svg?style=shield)](https://circleci.com/gh/brendanlong/dash-ts-validator)

This is an interim release of the `ts_validator`.  The code and results still need a bit of work and polish, but the testing procedures are in place.

## Dependencies

To build `ts_validator`, you will need the GNU Autotools installed (`autoconf`, `automake`, `make`). You will also need libxml2, pcre and glib.

### Ubuntu

    sudo apt-get install build-essential libxml2-dev libpcre3-dev libglib2.0-dev

### OS X with Homebrew

See [Homebrew](http://brew.sh/) if you don't already use it. You can also install these packages manually if you'd prefer.

    brew install pcre libxml2 glib

## Building `ts_validator`

To build `ts_validator`, run:

    ./autogen.sh
    make

On OS X with Homebrew you will need to set your `PKG_CONFIG_PATH` to `/usr/local/Cellar/libxml2/$highest_version/lib/pkgconfig` like this:

    LIBXML_PATH=/usr/local/Cellar/libxml2/
    LIBXML_LATEST=$(ls -v $LIBXML_PATH | tail -n 1)
    PKG_CONFIG_PATH=$LIBXML_PATH/$LIBXML_LATEST/lib/pkgconfig ./autogen.sh

The built executables are placed in the `ts_lib/apps` directory.

## Running `ts_validator`

The build produces an executable:

`ts_validate_multi_segment`: The first argument is the MPD to validate. It will validate all segments in the MPD (correctly handling different adaptation sets and representations).
