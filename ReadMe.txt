
This is an interim release of the ts_validator.  The code and results still need a bit of work and polish, but the testing procedures are in place.

The checks that the the ts_validator performs are:

0) Basic checking of the mpeg stream syntax
1) Checking the SAP type of each segment
2) Checking the time alignment of both the video and audio components of each segment which each other



Building ts_validator
---------------------

To build ts_validator, just use the make file in the ts_validator directory, either undr Linux or Cygwin for Windows.  The built executable is placed in the ts_lib/apps directory.  The executables included with the distribution were built using Cygwin on a Win64 machine.


Running tts_validator
---------------------

The build produces several executables, two of which are relevant to segment testing:

ts_validate_single_segment.exe: this validates a single segment.  Just run the executable for a description of the input parameters required

ts_validate_multi_segment.exe: this validates a full set of representations in an adaptation set.  This exe requires a parameter file as input which lists all the segments, etc.  An example parameter file is in the tslib_apps directory and is named SegInfoFile.txt.  Note that the fields in the file are tab-delimited.

