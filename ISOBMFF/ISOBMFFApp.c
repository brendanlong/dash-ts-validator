/*
** Copyright (C) 2014  Cable Television Laboratories, Inc.
** Contact: http://www.cablelabs.com/
 

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/stat.h>

#include "common.h"
#include "ISOBMFF.h"

#define ISOBMF_2BYTE_SZ 2
#define ISOBMF_4BYTE_SZ 4
#define ISOBMF_8BYTE_SZ 8

static struct option long_options[] = { 
   { "verbose",	   no_argument,        NULL, 'v' }, 
   { "dash",	   optional_argument,  NULL, 'd' }, 
   { "byte-range", required_argument,  NULL, 'b' }, 
   { "help",       no_argument,        NULL, 'h' }, 
}; 

static char options[] = 
   "\t-d, --dash\n"
   "\t-v, --verbose\n"
   "\t-h, --help\n"; 

static void usage(char *name) 
{ 
   fprintf(stderr, "\n%s\n", name); 
   fprintf(stderr, "\nUsage: \n%s [options] <input file with segment info>\n\nOptions:\n%s\n", name, options);
}

// for testing
int main(int argc, char *argv[]) 
{ 
   if (argc < 2) 
   {
      usage(argv[0]); 
      return 1;
   }

   char *fname = argv[1]; 
   printf ("Index file = %s\n", fname);


   int durations[26];
   data_segment_iframes_t *pIFrames = (data_segment_iframes_t *)calloc(26, sizeof(data_segment_iframes_t));
   validateIndexSegment(fname, 26, durations, pIFrames);
}

