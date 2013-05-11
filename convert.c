/*
* Convert, a small program that converts any binary file into a hexadecimal dump, 30 per lines
* and finally followed by a Q, this output can be used by the Amiga Download tool, available in the 
* very early amiga developer kits, by transferring the data over a parallel or serial connection 

This program is licensed under the BSD License

Copyright (c) 2011, Sigurbjorn B. Larusson
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the Sigurbjorn B. Larusson nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Sigurbjorn B. Larusson BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// How long is the read buffer?
// 27 is the default, that's 27 two character hex dumps for a total of 54 characters
// and 25 spaces, for a total of 79 characters per line
#define BUFFERLEN	1

// Debug?
#define DEBUG 0

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
	// File pointers
	FILE *inf = NULL;
	FILE *outf = NULL;
	// Input filename
	char infile[255];
	// Output filename
	char outfile[255];
	// Hex dumping buffer
	unsigned char buffer[42];
	// Temporary variable
	int i;
	// Read bytes
	int readbytes;
	// Debug?
	int debug = DEBUG;
	// Get the filename from the arguments
	if(argc > 1) {		
		strncpy(infile,argv[1],sizeof(infile));	
		if(argc > 2) {
			strncpy(outfile,argv[2],sizeof(outfile));
		} else {
			inf = stdout;
		}
	} else {
		inf = stdin;
		outf = stdout;
	}

	if(debug) {
		if(inf==NULL)
			fprintf(stdout,"Infile: %s\n",infile);
		else
			fprintf(stdout,"Using stdin for input\n");
		if(outf==NULL)
			fprintf(stdout,"Outfile: %s\n",outfile);
		else
			fprintf(stdout,"Using stdout for output\n");
	}

	// Open up the infile if not already set to stdin
	if(inf==NULL) 
		inf = fopen(infile,"r");
	// Did we open the file
	if(inf==NULL) {
		fprintf(stderr,"Can't open input file %s, exiting\n",infile);
		return 1;
	}
	
	// Open up the outfile if not already set to stdout
	if(outf==NULL) 
		outf=fopen(outfile,"w");
	// Did we open the file?
	if(outf==NULL) {
		fprintf(stderr,"Can't open outfile %s, exiting\n",outfile);
		return 1;
	}	

	// Read BUFFERLEN bytes from the source file and dump it as hex for as long we have data
	do {
		readbytes=fread(buffer,sizeof(char),BUFFERLEN,inf);
		// Dump as character as a hex code followed by a space
		for(i=0;i<readbytes;i++) {
			fprintf(outf,"%02X ",buffer[i]);
		}
	} while(readbytes != 0);
	// And finally followed by a Q to indicate file end!
	fprintf(outf,"Q");

	// Close the input and output files
	fclose(inf);
	fclose(outf);
	
	// Succesful run..
	return 0;
}
