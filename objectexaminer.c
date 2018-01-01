/*
This is ObjectExaminer

This small (well it was meant to be small, it's over a 1000 lines now!) utility 
decodes "hunks" from Amiga Executable files and dumps information about them to 
outfile (or stdout if no outfile is provided).  It was written to examine the hidden 
files from the Kickstart 1.0 and Kickstart 1.1 disks, it's either very detailed 
(if you define DEBUG as 1 in this file or use the -d switch on the command line)
or quite detailed (if DEBUG is 0 and you don't use the -d switch) in its output.

It will decode the hunks and if debug is enabled do a hexdump and a string search
within the binary as well as well as output more information about the progress.
The string search currently supports regular ascii strings and will also try to 
rot13 the data and see if there are rot13 "encrypted" strings too 

The string finder finds any 3 ascii values strung together, and is pretty good at finding
strings as well (although it has lots of false positives, i.e. it finds things that are
not really text strings).

This tool was written for research purposes to use in an article that I wrote for PageTable
regarding the hidden files on the Kickstart 1.0 and 1.1 disk, if you are reading this you
have probably found some other use for it, and I'd love to know about it, shoot me an email
to <sibbi ( A_T  ) dot1q.org>

For reference I used the original AmigaDOS Manual from C=, i.e. the information from
Chapter 2.2 regarding the Object File Structure and http://en.wikipedia.org/wiki/Amiga_Hunk

This program is not intended to decode every single object files, it was only written for the 
hidden files which are all from the pre 1.0 era anyway. It does not therefore support
hunk types which were not listed in those references, and it might not even work properly
with all of them, since I only tested it on the files on the Kick1.0 and 1.1 disks and didn't
try every possible combination...

There's nothing stopping you from adding more hunk types, you have the source and the code 
is well commented and should be easily readable.  They are defined and matched, but not handled 
in any way, and I don't know the order in which they may appear, so you will probably need to 
spend some time to work them into the correct places, this program assumes the following order

Program unit header block
Hunks

The hunks are then in this format
Hunk name (optional)
Reloctable block (optioal)
Relocation Information block (optional)
External symbol information block, with one or more symbol table data entries (optional)
Symbol table block, with one or more symbol table data entries (optional)
Debug block, with 1 or more debug longwords (optional)
End block (required)

If you do make additions, please shoot me an email to <sibbi (A_T  ) dot1q.org>

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

/* Return codes 
0 - Success
1 - Can't open input file
2 - Missing filename argument
3 - Can't read magic cookie 
4 - Can't read number of hunks
5 - Can't reader number of progressive hunks
6 - Can't allocate memory to hold hunk lengths
7 - Can't read the hunk length
8 - Can't read the hunk header
9 - Can't read the size of the hunk from the object file
10 - File is not an AmigaDOS executable file
11 - Error reading hunk-type for hunk_unit
12 - Can't read hunk unit name length
13 - Can't read name from hunk unit
14 - Can't read hunk name length from the hunk unit
15 - Can't read hunk name from the hunk unit
*/


// fprintf and others
#include <stdio.h>
// errno
#include <errno.h>
// strerror
#include <string.h>
// C99 Integer
#include <stdint.h>
// Malloc
#include <stdlib.h>
// Regular expressions
#include <regex.h>
// BSD Getopt (should be GNU getopt compatible as well if you're compiling/linking this on a GNU based system)
#include <unistd.h>

// Do debugging output?
#define	DEBUG	0

// Minimum consecutive ASCII characters to be considered a ascii string, used to find strings in the binary code
#define MINASCII 3
// Maximum consecutive ASCII characters to be considered a ascii string, used to find strings in the binary code
#define MAXASCII 255

// The Hunk Types, this is from http://en.wikipedia.org/wiki/Amiga_Hunk
#define	HUNK_UNIT		0x3E7
#define HUNK_NAME		0x3E8
#define HUNK_CODE		0x3E9
#define HUNK_DATA		0x3EA
#define HUNK_BSS		0x3EB
#define HUNK_RELOC32		0x3EC
#define HUNK_RELOC16		0x3ED
#define HUNK_RELOC8		0x3EE
#define HUNK_EXT		0x3EF
#define HUNK_SYMBOL		0x3F0
#define HUNK_DEBUG		0x3F1
#define HUNK_END		0x3F2
// These are not supported but defined here for completeness (later time additions not defined in the AmigaDOS 1.0 Manual)
#define HUNK_HEADER		0x3F3
#define HUNK_OVERLAY		0x3F5
#define HUNK_BREAK		0x3F6
#define HUNK_DREL32		0x3F7
#define HUNK_DREL16		0x3F8
#define HUNK_DREL8		0x3F9
#define HUNK_LIB		0x3FA
#define HUNK_INDEX		0x3FB
#define HUNK_RELOC32SHORT	0x3FC
#define HUNK_RELRELOC32		0x3FD
#define HUNK_ABSRELOC16		0x3FE
#define HUNK_PPC_CODE		0x4E9
#define HUNK_RELRELOC26		0x4EC

// Hunk_ext Symbol data unit type
#define EXT_SYMB	0
#define EXT_DEF		1
#define EXT_ABS		2
#define EXT_RES		3
#define EXT_REF32	129
#define EXT_COMMON	130
#define EXT_REF16	131
#define EXT_REF8	132

// The Amiga "magic cookie", identifying an executable file
#define	MAGIC	0x000003F3

// Various helper functions, called from within the main program

// Helper function that prints out usage information
void usage(char *programname) {
	fprintf(stderr,"This is objectexaminer V0.2 (C) 2011-2013 Sigurbjorn B. Larusson\n");
	fprintf(stderr,"Usage: %s [-d] [-i] [-o <outputfilename>] <inputfilename>\n\n",programname);
	fprintf(stderr,"-d, will enable the debug option, which will print out a lot more information\n\n");
	fprintf(stderr,"-i will ignore the magic number of the file, potentially useful for partial files.\n");
	fprintf(stderr,"-o will output to a file instead of the screen.\n");
	fprintf(stderr,"\tIf you specify -o you must also specify a path to the output file\n\n");
	fprintf(stderr,"You must then specify the input file (an AmigaDOS object file) to be parsed.\n");
	return;
}


// Helper function that converts a 4 byte char array into a 32 bit integer, big-endian style (since 68k is big-endian)
uint32_t msb_bytearray_to_int32(char *bytearray) {
	// Check that the array is at least 4 bytes...
	if((sizeof(bytearray)/sizeof(char)) < 4) {
		return 0;
	} else {
		return (uint8_t)bytearray[0] << 24 | (uint8_t)bytearray[1] << 16 | (uint8_t)bytearray[2] << 8 | (uint8_t)bytearray[3];
	}
}

// Helper function that converts a 4 byte char array into a 32 bit integer using the bottom three bytes, big-endian style (since 68k is big-endian)
uint32_t msb_bytearray_to_int24(char *bytearray) {
	// Check that the array is at least 4 bytes...
	if((sizeof(bytearray)/sizeof(char)) < 3) {
		return 0;
	} else {
		return (uint8_t)bytearray[0] << 16 | (uint8_t)bytearray[1] << 8 | (uint8_t)bytearray[2];
	}
}

// Helper function that converts a 2 byte char array into a 16 bit integer, big-endian style (since 68k is big-endian)
uint16_t msb_bytearray_to_int16(char *bytearray) {
	// Check that the array is at least 2 bytes...
	if((sizeof(bytearray)/sizeof(char)) < 2) {
		return 0;
	} else {
		return (uint8_t)bytearray[0] << 8 | (uint8_t)bytearray[1];
	}
}

// Helper function that rot13s a given string
void rot13(char *buffer,uint32_t len) {
	// Temporary variable
	int i = 0;
	// Loop through the buffer...
	for(i=0;i<len;i++) {
		// Character is A-M or a-m, add 13
		if((buffer[i] >= 'A' && buffer[i] <= 'M') || (buffer[i] >= 'a' && buffer[i] <= 'm')) {		
			buffer[i] += 13;
		// Character is N-Z or n-z, deduct 13 
		} else if((buffer[i] >= 'N' && buffer[i] <= 'Z') || (buffer[i] >= 'n' && buffer[i] <= 'z')) {		
			// ROT 13 it
			buffer[i] -= 13;
		}
	}
}

// Helper function to find strings in a character array as well as perform a Hexdump 
void find_strings(char *buffer,uint32_t len,uint8_t debug,FILE *outfile) {
	// To hold the compiled regular expression
	regex_t regexp;
	// Define the matches array to hold the regexp matches
	regmatch_t matches[1];
	// Temp variables
	uint32_t i = 0;
	uint32_t j = 0;
	uint16_t retvalue = 0;
	// Current offset in the string
	uint32_t offset = 0;
	// Regular expression matching string
	char *match;

	// A string for the actual regular expression search string
	char searchstring[13];
	// Fill in the string with our minimum and maximum values
	i=sprintf(searchstring,"[ -~]{%d,%d}",MINASCII,MAXASCII);

	if(debug) {
		fprintf(outfile,"\tHexDump:\n\t\t");
		for(i=0;i<len;i++)  {
			// Hex dump the character
			fprintf(outfile,"%02x ",(unsigned char)buffer[i]);
			// Every 20th character print out an ascii dump of the current chars or a . for non-ascii
			if(((i+1) % 20) == 0 && i != 0)  {
				fprintf(outfile,"\t\t");
				// Print the ascii dump of the rest of the characters
				for(j=i-19;j<=i;j++) {
					// Print all characters from ! to ~ in the ascii table as themselves
					if(buffer[j] >= '!' && buffer[j] <= '~') {
						fprintf(outfile,"%c ",buffer[j]);
					// And everything else (include a space) as a .
					} else {
						fprintf(outfile,". ");
					}
				}
				fprintf(outfile,"\n\t\t");
			}
			// At the end of the run, print out the remaining ascii characters, unless we're actually 
			// exactly on the next line, in which case there are no characters!
			if(i == (len-1) && ((i+1)%20) != 0) {
				// Print out the neccesary spaces to pad the ascii characters to the correct location
				for(j=(i%20);j<20;j++)
					fprintf(outfile,"   ");
				fprintf(outfile,"\t\t");
				// Print the ascii dump of the rest of the characters
				for(j=i-(i%20);j<i;j++) {
					// Print all characters from ! to ~ in the ascii table as themselves
					if(buffer[j] >= '!' && buffer[j] <= '~') {
						fprintf(outfile,"%c ",buffer[j]);
					// And everything else (include a space) as a .
					} else {
						fprintf(outfile,". ");
					}
				}
			}
		}
		fprintf(outfile,"\n");
	}

	// Compile the regular epxression
	retvalue = regcomp(&regexp,searchstring,REG_EXTENDED);

	// Regular expression compilation was okay...
	if(retvalue == 0) {
		while(offset < len ) {
			// Set the beginning start and end value for the string
			matches[0].rm_so = offset; matches[0].rm_eo = len;
			// Remaining buffer is too short to match?  Stop...
			if((matches[0].rm_eo - matches[0].rm_so) < MINASCII) 
				break;
			// Match the regular expression to the string
			// The results are stored in the structure matches
			retvalue = regexec(&regexp,buffer,1,matches,REG_STARTEND);	
			// Check for a valid return code, ignore REG_NOMATCH since that is not an error
			if(retvalue != 0 && retvalue != REG_NOMATCH) {
				fprintf(stderr,"\tRegular expression error occured searching for strings, error value was %d\n",retvalue);
			// If there was a match
			} else if(retvalue != REG_NOMATCH) {
				if(debug) {
					fprintf(outfile,"\tRetvalue is %d\n",retvalue);
					fprintf(outfile,"\tCurrent offset is %d, starting pos is %lld and ending pos is %lld\n",offset,matches[0].rm_so,matches[0].rm_eo);
				}
				// Allocate some memory for the string
				match = malloc((matches[0].rm_eo - matches[0].rm_so) + 1);
				if(match == NULL) {
					fprintf(stderr,"Can't allocate memory to hold regular expression matches!\n");
					// Break out of the loop
					break;
				}
				// Copy the substring into the match string
				strncpy(match,(buffer + matches[0].rm_so),matches[0].rm_eo-matches[0].rm_so);
				match[matches[0].rm_eo-matches[0].rm_so] = 0;
				if(debug)
					fprintf(outfile,"\tMatching string between %lld and %lld\n",matches[0].rm_so,matches[0].rm_eo);
				// Print out the match
				if(debug)
					fprintf(outfile,"\t\tString:\t\t%s\n",match);
				rot13(match,matches[0].rm_eo-matches[0].rm_so);
				if(debug)
					fprintf(outfile,"\t\tRot13 String:\t%s\n",match);
				// Set a new offset in the string to start the search
				offset = matches[0].rm_eo;
				// Free the memory allocated for the string
				free(match);
			} else {
				// No match, stop the loop...
				break;
			}
		}
	}
	// Return successfully
	return;
}


// The main program
int main(int argc,char **argv) {
	// File pointer for the reading from the binary file
	FILE *file = NULL;
	// Int to store the filesize
	uint32_t filelength = 0;
	// Int to store the number of hunks
	uint32_t numberofhunks = 0;
	// Int to store the progressive number of hunks
	uint32_t numberofprogressivehunks = 0;
	// Int to store the value of debug, set to the value of the defined value for DEBUG, can be
	// overriden with the -d command line argument
	uint8_t	debug = DEBUG;
	// Int to store the value of ignoremagic, if set, the magic number of the file is ignored (can be useful for partial files or files that are object files but not executables)
	uint8_t ignoremagic = 0;
	// Int to store the current offset in the file
	uint32_t offset = 0;
	// Int to temporarily store hunklength
	uint32_t hunklength = 0;
	// Temporary looping variables
	uint32_t i=0;
	uint32_t j=0;
	uint32_t k=0;
	// LongLong to store the magic number once converted from a string
	uint32_t magicnumber = 0;
	// Integer to store the hunk type
	uint32_t hunktype = 0;
	// Temporary byte array to store 32 bit values read in from the file as 4*8bit values
	char tempbytearray[4];
	// Buffer to store the binary data from the hunk
	char *buffer;
	// Buffer store various values within the hunk types, names, offsets and so forth
	char *hunkbuffer;
	// Integer to store various lengths within the hunks, name length, offset lengths and so forth
	uint32_t hunktypelength = 0;
	// Integer to store the external hunk type
	uint8_t hunksymboltype = 0;
	// File descriptor used either for the outfile, or set as outfile
	FILE *outfile = NULL;

	// We use getopt to parse the command line arguments
	char optionflag;	

	// Read the passed options if any (-d sets debug, -o sets an optional filename to pipe the output to)
	while((optionflag = getopt(argc, argv, "dio:")) != -1) {
		switch(optionflag) {
			// Debug flag is set to on
			case 'd':
				debug=1;
				break;
			// Output file flag is specified
			case 'o':
				outfile = fopen(optarg,"w");
				// If file ddidn't open, error occured, print error, exit
				if(outfile == NULL) {
					fprintf(stderr,"Can't open file %s for reading, error returned was: %s\n",optarg,strerror(errno));
					return 1;
				} else {
					// Announce that we're writing the output to a file on stdout
					fprintf(stdout,"Writing output to %s\n",optarg);
				}
				break;
			// Ignore magic number 
			case 'i':
				ignoremagic=1;
				break;
			// Missing argument to o
			case '?':
				usage(argv[0]);
				return 2;
				break;
		}
	}
	// Set argc and argv based on what we've read from the arguments so far
	argc = argc - optind;
	// At this point, only the input filename should remain but it has to be present
	if(argc == 1) {
		// The filename is at the optind index of the argv list (the last non-option argument)
		file = fopen(argv[optind],"r");	
		// If there's an error, print an error message and exit
		if(file == NULL) {
			fprintf(stderr,"Can't open file %s for reading, error returned was: %s\n",argv[optind],strerror(errno));
			return 1;
		}
	// No input filename present
	} else {
		// Print usage information and exit...
		usage(argv[0]);
		return 2;
	}
	// Outfile not set? (no option passed)
	if(outfile == NULL) {
		// If not, set it to stdout
		outfile = stdout;
	}
	// Print to the outfile that we've started passing the input file
	fprintf(outfile,"Parsing %s:\n",argv[optind]);

	// File or files are open and ready for reading and writing,
	// Now get the total number of bytes in the inputfile by seeking to the end of the file
	fseek(file, 0, SEEK_END);
	filelength=ftell(file);
	// Seek back to the beginning of the file
	fseek(file,0,SEEK_SET);
	// Set the offset as 0, we will use this (and increment it) as we pass through the file
	offset = 0;

	if(debug)
		fprintf(outfile,"\tFilesize is %u bytes\n",filelength);

	// Now check whether the file starts with the magic cookie, if it doesn't, it isn't a valid AmigaDOS executable
	// To do this we read 4 bytes from the file
	if(fread(tempbytearray,sizeof(char),4,file) != 4) {
		fprintf(stderr,"Can't read magic cookie from file, file is probably corrupted, exiting\n");
		return 3;
	}
	// The next 4 bytes are empty so we skip them
	offset=8;
	// Bitshift them into a 32 bit integer, note that this if course big-endian since the 68k is big-endian
	magicnumber = msb_bytearray_to_int32(tempbytearray);
	
	if(debug) 
		fprintf(outfile,"\tMagic number is %x\n",magicnumber);

	// Check if the magic number is 3f3 or if ignoremagic is set
	if(magicnumber == MAGIC || ignoremagic) {
		// This is a valid AmigaDOS executable file, next we skip the 8th byte of the file and read another 4 bytes
		// these 4 bytes contain the total number of hunks in this object file
		fseek(file,offset,SEEK_SET);
		if(fread(tempbytearray,sizeof(char),4,file) != 4) {
			fprintf(stderr,"Can't read number of hunks from the object file, file is probably corrupted, exiting\n");
			return 4;
		}
		numberofhunks = msb_bytearray_to_int32(tempbytearray);
		// The next 4 bytes are empty so we skip them
		offset=16;
	
		if(debug)	
			fprintf(outfile,"\tNumber of hunks is %d\n",numberofhunks);

		fseek(file,offset,SEEK_SET);
		if(fread(tempbytearray,sizeof(char),4,file) != 4) {
			fprintf(stderr,"Can't read number of progressive hunks from the object file, file is probably corrupted, exiting\n");
			return 5;
		}
		
		numberofprogressivehunks = msb_bytearray_to_int32(tempbytearray);
		// Move the offset to byte 20 which is the start of the hunk length list
		offset = 20;

		if(debug)
			fprintf(outfile,"\tNumber of progressive hunks is %d\n",numberofprogressivehunks);

		// Create an array to hold the length of each hunk
		uint32_t *length = malloc(numberofhunks * sizeof(uint32_t));

		if(length == NULL) {	
			fprintf(stderr,"Could not allocate memory to hold hunk length, exiting\n");
			free(length);
			return 6;
		}

		// Loop through the hunks to get the length of each one
		for(i=0;i<numberofhunks;i++) {
			// Seek to current offset per hunk
			fseek(file,offset,SEEK_SET);
			// New offset is old offset + 4 bytes
			offset += 4;
			// Read 4 more bytes which contain the length of this hunk in longwords (4 bytes)
			if(fread(tempbytearray,sizeof(char),4,file) != 4) {
				fprintf(stderr,"\tCan't read the hunk length from the object file, file is probably corrupted, exiting\n");
				free(length);
				return 7;
			}
			// And store in the length array (the length is in long words, so multiply by 4 to get a byte value)
			length[i] = msb_bytearray_to_int32(tempbytearray) * 4;
			// Print out the hunks and their length
			fprintf(outfile,"\tHunk %d length is %d bytes\n",i,length[i]);
		}

		// Then proceed to read in each hunk, analyzing it as we go along
		for(i=0;i<numberofhunks;i++) {
			fprintf(outfile,"Processing hunk %d:\n",i);
			// Seek to current offset
			fseek(file,offset,SEEK_SET);
			// Parse the hunk header starting with the type
			if(fread(tempbytearray,sizeof(char),4,file) != 4) {
				fprintf(stderr,"\tCan't read the hunk header from the object file, file is probably corrupted, exiting\n");	
				free(length);
				return 8;
			}
			hunktype = msb_bytearray_to_int32(tempbytearray);
			// Increment the offset by 4 bytes
			offset += 4;

			if(debug) 
				fprintf(outfile,"\tHunk type is %04X\n",hunktype);

			// Next 4 bytes are the length of the hunk
			if(fread(tempbytearray,sizeof(char),4,file) != 4) {
				fprintf(stderr,"\tCan't read the size of the hunk from the object file, file is probably corrupted, exiting\n");
				free(length);
				return 9;
			}
			// Increment the offset by 4 bytes
			offset += 4;
			// Get the hunklength according to the hunk header
			hunklength = msb_bytearray_to_int32(tempbytearray) * 4;
		
			if(debug) {
				fprintf(outfile,"\tHunk length according to hunk header is %d bytes\n",hunklength);
				fprintf(outfile,"\tHunk length according to the file header is %d bytes\n",length[i]);
			}

			// Check for length mismatch
			if(hunklength != length[i]) {
				fprintf(stderr,"\tHunk length mismatch, program header says %d bytes, hunkheader says %d bytes, using hunkheader!\n",hunklength,length[i]);
				length[i] = hunklength;
			}

			// Allocate some memory for a buffer that containts the hunk data
			buffer = malloc(sizeof(char)*length[i]+1);
			if(buffer == NULL) {
				fprintf(stderr,"\tCan't allocate memory for hunk data buffer\n");
				// break out of the loop
				break;
			}
			// And read the hunk data into the buffer, change the length in case we can't read the full data
			length[i] = fread(buffer,sizeof(char),length[i],file);

			// Now we process the hunk based on the type!
			switch(hunktype) {
				case HUNK_END:
					fprintf(outfile,"\tFound a Hunk_end entry, this hunk is probably empty\n");
					break;
				case HUNK_UNIT:
					fprintf(outfile,"\tFound Hunk of type HUNK_UNIT, length is %d\n",length[i]);
					// We need to read the type in
					if(fread(tempbytearray,sizeof(char),2,file) != 2) {
						fprintf(stderr,"\tFound HUNK_UNIT but can't read the type, file is probably corrupted, exiting\n");
						free(buffer);
						free(length);
						return 11;
					} else {
						// Increment the offset by 4 bytes
						offset += 4;
						hunktype = msb_bytearray_to_int16(buffer);
						if(hunktype != HUNK_UNIT)  
							fprintf(stderr,"\tHunk_unit mismatch, header says hunk_unit but type is %X!\n",hunktype);
						// Read in the hunk unit length
						if(fread(tempbytearray,sizeof(char),4,file) != 4) {
							fprintf(stderr,"\tCan't read hunk unit name length from file, file is probably corrupted, exiting\n");
							free(buffer);
							free(length);
							return 12;
						} else {
							// Increment the offset by 4 bytes
							offset += 4;
							// We should now have the length of the hunk_unit name in the longword
							hunktypelength = msb_bytearray_to_int32(buffer);
							// Allocate some memory space for the buffer
							hunkbuffer = malloc(hunktypelength*4);
							if(hunkbuffer == NULL) {
								fprintf(stderr,"\tCan't allocate memory for hunk_unit name buffer!\n");
								break;
							}
							// And should be able to read that number of bytes*4 from the file
							if(fread(hunkbuffer,sizeof(char),hunktypelength*4,file) != hunktypelength*4) {
								fprintf(stderr,"\tCan't read name from hunk_unit, file is probably corrupted, exiting\n");
								free(buffer);
								free(length);
								free(hunkbuffer);
								return 13;
							} else {
								// Increment the offset by the buffer
								offset += hunktypelength*4;
								// We should now have the name inside the buffer
								fprintf(outfile,"\tHunk Unit Name is %s\n",hunkbuffer);
								// Free up the space for the hunkbuffer again
								free(hunkbuffer);
							}
						}
					} 
					break;

				case HUNK_NAME:
					fprintf(outfile,"\tFound Hunk of type HUNK_NAME, length is %d\n",length[i]);
					// Read in the hunk name length
					if(fread(tempbytearray,sizeof(char),4,file) != 4) {
						fprintf(stderr,"\tCan't read hunk_name name length from file, file is probably corrupted, exiting\n");
						free(buffer);
						free(length);
						return 14;
					} else {
						// Increment the offset by 4 bytes
						offset += 4;
						// We should now have the length of the hunk_name name in the longword
						hunktypelength = msb_bytearray_to_int32(buffer);
						// Allocate some memory space for the buffer
						hunkbuffer = malloc(hunktypelength*4);
						if(hunkbuffer == NULL) {
							fprintf(stderr,"\tCan't allocate memory for hunk_name name buffer!\n");
							break;
						}
						// And should be able to read that number of bytes*4 from the file
						if(fread(hunkbuffer,sizeof(char),hunktypelength*4,file) != hunktypelength*4) {
							fprintf(stderr,"\tCan't read name from hunk_name, file is probably corrupted, exiting\n");
							free(buffer);
							free(length);
							free(hunkbuffer);
							return 15;
						} else {
							// Increment the offset by 4 bytes
							offset += 4;
							// We should now have the name inside the buffer
							fprintf(outfile,"\tHunk Name is %s\n",hunkbuffer);
							// Free up the space for the hunkbuffer again
							free(hunkbuffer);
						}
					}
					break;

				case HUNK_CODE:
					fprintf(outfile,"\tFound Hunk of type HUNK_CODE, length is %d\n",length[i]);
					fprintf(outfile,"\tParsing hunk data:\n");
					if(debug) {
						fprintf(outfile,"\tSearching for and dumping strings in the hunk\n");
						find_strings(buffer,length[i],debug,outfile);
					}
					break;

				case HUNK_DATA:
					fprintf(outfile,"\tFound Hunk of type HUNK_DATA, length is %d\n",length[i]);
					fprintf(outfile,"\tParsing hunk data:\n");
					fprintf(outfile,"\tSearching for and dumping strings in the hunk\n");
					if(debug) {
						fprintf(outfile,"\tSearching for and dumping strings in the hunk\n");
						find_strings(buffer,length[i],debug,outfile);
					}
					break;
	
				case HUNK_BSS:
					length[i]=8;	
					fprintf(outfile,"\tFound Hunk of type HUNK_BSS, length is %d\n",length[i]);
					fprintf(outfile,"\tParsing hunk data:\n");
					fprintf(outfile,"\tSearching for and dumping strings in the hunk\n");
					if(debug) {
						fprintf(outfile,"\tSearching for and dumping strings in the hunk\n");
						find_strings(buffer,length[i],debug,outfile);
					}
					break;

				// Unsupported hunks, added after 1.0
				case HUNK_HEADER:
					fprintf(outfile,"\tHunk type HUNK_HEADER is not supported, skipping\n");
					break;

				case HUNK_OVERLAY:
					fprintf(outfile,"\tHunk type HUNK_OVERLAY is not supported, skipping\n");
					break;

				case HUNK_BREAK:
					fprintf(outfile,"\tHunk type HUNK_BREAK is not supported, skipping\n");
					break;

				case HUNK_DREL32:
					fprintf(outfile,"\tHunk type HUNK_DREL32 is not supported, skipping\n");
					break;
			
				case HUNK_DREL16:
					fprintf(outfile,"\tHunk type HUNK_DREL16 is not supported, skipping\n");
					break;

				case HUNK_DREL8:
					fprintf(outfile,"\tHunk type HUNK_DREL8 is not supported, skipping\n");
					break;

				case HUNK_LIB:
					fprintf(outfile,"\tHunk type HUNK_LIB is not supported, skipping\n");
					break;

				case HUNK_INDEX:
					fprintf(outfile,"\tHunk type HUNK_INDEX is not supported, skipping\n");
					break;

				case HUNK_RELOC32SHORT:
					fprintf(outfile,"\tHunk type HUNK_RELOC32SHORT is not supported, skipping\n");
					break;

				case HUNK_RELRELOC32:
					fprintf(outfile,"\tHunk type HUNK_RELRELOC32 is not supported, skipping\n");
					break;
	
				case HUNK_ABSRELOC16:
					fprintf(outfile,"\tHunk type HUNK_ABSRELOC16 is not supported, skipping\n");
					break;

				case HUNK_PPC_CODE:
					fprintf(outfile,"\tHunk type HUNK_PPC_CODE is not supported, skipping\n");
					break;

				case HUNK_RELRELOC26:
					fprintf(outfile,"\tHunk tyep HUNK_RELRELOC26 is not supported, skipping\n");
					break;

				default:
					fprintf(outfile,"\tUnknown hunk type with hex value %04X encountered!\n",hunktype);
					break;
			}
			// Free the buffer used for the hunk data
			free(buffer);

			// If we were not dealing with a reloctable hunk continue to the next hunk...
			if(!(hunktype == HUNK_CODE || hunktype == HUNK_DATA || hunktype == HUNK_BSS)) 
				continue;

			// If we're here we were dealing with a hunk code
			// New offset is current offset + (hunk length * 4 bytes since each entry is a long word (4 bytes)) - the header which was the type (4 bytes) and the length (another 4 bytes)
			offset += (length[i]);

			// Seek to the new offset
			fseek(file,offset,SEEK_SET);

			// There is at least one more hunk to be read since the code hunk ends with an end_hunk
			if(fread(tempbytearray,sizeof(char),4,file) != 4)  {
				fprintf(stderr,"\tCan't read end hunk from object file, file is probably corrupted, stopping!\n");
				// Break out of loop to exit
				break;
			}	
			hunktype = msb_bytearray_to_int32(tempbytearray);
			// Increment the offset by 4 bytes
			offset += 4;

			if(debug) 
				fprintf(outfile,"\tFound hunk type %04X\n",hunktype);

			// CODE Hunk followed by a relocation hunk?
			if(hunktype == HUNK_RELOC8 || hunktype == HUNK_RELOC16 || hunktype == HUNK_RELOC32) {
				// Print information about hunk type
				switch(hunktype) {
					case HUNK_RELOC8:
						fprintf(outfile,"\tFound 8 bit relocation hunk\n");	
						break;
					case HUNK_RELOC16:
						fprintf(outfile,"\tFound 16 bit relocation hunk\n");	
						break;
					case HUNK_RELOC32:
						fprintf(outfile,"\tFound 32 bit relocation hunk\n");	
						break;
				}
				// Set the temporary looping variable to 1 to execute the loop
				hunktypelength = 1;
				while(hunktypelength!=0) {
					if(fread(tempbytearray,sizeof(char),4,file) != 4) {
						fprintf(stderr,"\tCan't read reloc hunk offset list length, file is probably corrupted\n");
						break;
					} else {
						// Increment the offset by 4 bytes
						offset += 4;
						// Get the length
						hunktypelength = msb_bytearray_to_int32(tempbytearray);
						// If the length is 0, exit the loop
						if(hunktypelength == 0)
							break;
						if(fread(tempbytearray,sizeof(char),4,file) != 4) {
							fprintf(stderr,"\tCan't read hunk number from file, file is probably corrupted\n");
							break;
						} else {
							// Increment the offset by 4 bytes
							offset += 4;
							fprintf(outfile,"\tOffset list is %d entries long for hunk %d\n",hunktypelength,msb_bytearray_to_int32(tempbytearray));
							// And then proceed to read in the offsets, one by one
							for(j=0;j<hunktypelength;j++) {
								if(fread(tempbytearray,sizeof(char),4,file) != 4) {	
									fprintf(stderr,"\tCan't read offset list from file, file is probably corrupted\n");
									break;
								} else {	
									// Increment the offset by 4 bytes
									offset += 4;
									// Print the offset out
									fprintf(outfile,"\t\tOffset %d is %08X\n",j,msb_bytearray_to_int32(tempbytearray));
								}
							}
						  }
					}		
				}
				// Read in the next hunktype since we've have not reached the end of the hunks
				if(fread(tempbytearray,sizeof(char),4,file) != 4)  {
					fprintf(stderr,"\tCan't read end hunk from object file, file is probably corrupted, stopping!\n");
					break;
				}	
				hunktype = msb_bytearray_to_int32(tempbytearray);
				// Increment the offset by 4 bytes
				offset += 4;

				if(debug) 
					fprintf(outfile,"\tFound hunk type %04X\n",hunktype);
			}

			// A external symbol information block can follow a code block, or a reloc block, and there can be multiple entries in a row...
			while(hunktype == HUNK_EXT || hunktype == HUNK_SYMBOL) {
				// Loop until all symbol data units have been decoded or failure occurs
				k=1;
				while(k!=0) {
					if(fread(tempbytearray,sizeof(char),1,file) != 1) {
						fprintf(stderr,"\tCan't read external/symbol hunk type, file is probably corrupted, stopping!\n");
						break;
					} else {
						// Increment the offset by 1 byte
						offset += 1;
						hunksymboltype = (uint8_t)tempbytearray[0];	
						if(debug)	
							fprintf(outfile,"\tHunk_ext/symbol symbol type %d\n",hunksymboltype);
						switch(hunksymboltype) {
							case EXT_SYMB:
								fprintf(outfile,"\tSymbol table found\n");
								break;

							case EXT_DEF:
								fprintf(outfile,"\tRelocatable definition found\n");
								break;

							case EXT_ABS:
								fprintf(outfile,"\tAbsolute definition found\n");
								break;

							case EXT_RES:
								fprintf(outfile,"\tResident library definition found\n");
								break;

							case EXT_REF32:
								fprintf(outfile,"\t32-bit reference to symbol found\n");
								break;

							case EXT_COMMON:
								fprintf(outfile,"\t32-bit reference to common found\n");
								break;

							case EXT_REF16:
								fprintf(outfile,"\t16-bit reference to symbol found\n");
								break;

							case EXT_REF8:
								fprintf(outfile,"\t8-bit reference to symbol found\n");
								break;

							default:
								fprintf(stderr,"\tUnknown Symbol data type %d encountered!\n",hunksymboltype);
								// Stop the loop!
								k=0;
								break;
						}
						// Read in the name length					
						if(fread(tempbytearray,sizeof(char),3,file) != 3) {
							fprintf(stderr,"\t\tCan't read external hunk name length, file is probably corrupted, stopping!\n");
							break;
						} else {
							// Increment the offset by 3 bytes
							offset += 3;
							hunktypelength = msb_bytearray_to_int24(tempbytearray);
							if(debug)
								fprintf(outfile,"\t\tHunk ext/symbol symbol data name length %d\n",hunktypelength);
							// If both the symboltype and hunktypelength are 0 then we've reached the end
							if(hunksymboltype == 0 && hunktypelength == 0) { 
								if(debug) 
									fprintf(outfile,"\t\tReached end of symbol data\n");
								// Break out of loop
								break;
							}
			
							// Allocate some memory for the hunk_ext name
							hunkbuffer = malloc(hunktypelength*4);
							if(hunkbuffer == NULL) {
								fprintf(stderr,"\t\tCan't allocate memory for hunk_ext name buffer!\n");
								break;
							}
							if(fread(hunkbuffer,sizeof(char),hunktypelength*4,file) != hunktypelength*4) {
								fprintf(stderr,"\t\tCan't read hunktypename for hunk_ext/symbol, file is probably corrupted, stopping\n");
								free(hunkbuffer);
								break;
							} else {
								// Increment the offset by the buffer
								offset += hunktypelength*4;
								if(hunktype == HUNK_EXT) 
									fprintf(outfile,"\t\tHunk_ext symbol data name is %s\n",hunkbuffer);
								else
									fprintf(outfile,"\t\tHunk_symbol symbol data name is %s\n",hunkbuffer);
								free(hunkbuffer);
								// Read the symbolvalue
								if(fread(tempbytearray,sizeof(char),4,file) != 4) {
									fprintf(stderr,"\t\tCan't read symbol data for hunk_ext/symbol, file is probably corrupted, stopping\n");
									break;
								} else {
									// Increment the offset by 4 bytes
									offset += 4;
									// Get the value of the symbol
									hunktypelength = msb_bytearray_to_int32(tempbytearray);
									// And print it
									if(hunktype == HUNK_EXT) 
										fprintf(outfile,"\t\tHunk_ext symbol value is %08X\n",hunktypelength);
									else
										fprintf(outfile,"\t\tHunk_symbol symbol value is %08X\n",hunktypelength);
								}
		
							}
						}
					}
				}
				// Read in the next hunktype since we've have not reached the end of the hunks
				if(fread(tempbytearray,sizeof(char),4,file) != 4)  {
					fprintf(stderr,"\tCan't read end hunk from object file, file is probably corrupted, stopping!\n");
					break;
				}	
				hunktype = msb_bytearray_to_int32(tempbytearray);
				// Increment the offset by 4 bytes
				offset += 4;

				if(debug) 
					fprintf(outfile,"\tFound hunk type %04X\n",hunktype);
			}
		
			// Check for a debug chunk which might be the last chunk before the end chunk
			if(hunktype == HUNK_DEBUG) {
				fprintf(outfile,"\tFound a debug hunk\n");
				if(fread(tempbytearray,sizeof(char),4,file) != 4) {
					fprintf(stderr,"\tDebug hunk doesn't contain the number of debug data long words, file is probably corrupted, stopping\n");
					break;
				} else {		
					// Add 4 to the offset
					offset += 4;
					// Get the hunk type length
					hunktypelength = msb_bytearray_to_int32(tempbytearray);
					// Loop over the long words in the hunk and print them out
					for(j=0;j<hunktypelength;j++) {
						if(fread(tempbytearray,sizeof(char),4,file) != 4) {
							fprintf(stderr,"\tCan't read the debug data from the debug hunk, file is probably corrupted, stopping\n");
							break;
						} else {
							// Increase the offset by 4
							offset += 4;
							// print out the string in the longword...
							fprintf(outfile,"%s",tempbytearray);
						}
					}
					fprintf(outfile,"\n");
				}
			}
			// And by this point, we should be at the end hunk, if not, there is a chance that the end hunk is missing, it's not illegal to skip the end chunk although it is considered poor form (except at the end of the file, there the end hunk must exist)
			fprintf(outfile,"\tCurrent file offset is %d out of %d bytes\n",offset,filelength);
			if(hunktype != HUNK_END) {
				fprintf(stderr,"\tHunk is not the expected type, expected end, found %#010x, it might be missing which is poor form but not illegal\n",hunktype);
				offset-=8;
			}

			fprintf(outfile,"\tCurrent file offset is %d out of %d bytes\n",offset,filelength);
			if(debug)
				fprintf(outfile,"\tCurrent file offset is %d out of %d bytes\n",offset,filelength);

			if(offset==filelength)
				fprintf(outfile,"End of file reached successfully\n");

 		}
		// Free the memory for the length array
		free(length);

	} else {
		fprintf(stderr,"The file %s is not an AmigaDOS executable object file, magic number is %x, should be %x\n",argv[1],magicnumber,MAGIC);
		return 10;
	}
	
	
	// Close the file if open
	if(file) 
		fclose(file);
			
	// Success
	return 0;
}
