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

#### OS X Open File Limit

On recent versions of OS X, you may need to increase the "open file limit". The TS validator only opens a few files at a time, but OS X seems to have trouble with it.

Open or create /etc/launchd.conf (`sudo nano /etc/launchd.conf`) and add:

    limit maxfiles 16384 16384
    limit maxproc 2048 2048

Then edit ~/.bashrc (`nano ~/.bashrc`) and add:

    ulimit -n 1024
    ulimit -u 1024

Then **restart**, and the open file limit error should go away.

Note: You can close nano by typing `ctrl+x`, then `y` to save.

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

## Running Tests

There are some unit tests. Run them with:

    make check

On Linux machines with Valgrind, you can also run the tests under Valgrind to check for memory leaks:

    make check-valgrind