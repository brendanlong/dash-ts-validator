# TS Validator

This is an interim release of the `ts_validator`.  The code and results still need a bit of work and polish, but the testing procedures are in place.

## Dependencies

To build `ts_validator`, you will need the GNU Autotools installed (`autoconf`, `automake`, `make`). You will also need libxml2 and glib.

### Ubuntu

    sudo apt-get install build-essential libxml2-dev libglib2.0-dev

## Building `ts_validator`

To build `ts_validator`, run:

    ./autogen.sh
    make

The built executables are placed in the `ts_lib/apps` directory.

## Running `ts_validator`

The build produces an executable:

`ts_validate_multi_segment`: The first argument is the MPD to validate. It will validate all segments in the MPD (correctly handling different adaptation sets and representations).
