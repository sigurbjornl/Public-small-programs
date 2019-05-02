/*
 * extract-adf.c 
 *
 * (C)2008 Michael Steil, http://www.pagetable.com/
 * Do whatever you want with it, but please give credit.
 *
 * 2011 Sigurbjorn B. Larusson, http://www.dot1q.org/, version 2.0
 *
 * Hack to restore the file path and to pass the adf file and start sector/end sector used as an argument 
 * This makes it possible to use on any OFS adf file and on HD OFS floppies and to tune where to start and end the process
 * for any other purpose
 *
 * Orphaned files still end up where ever you launched the binary, you'll have to manually move them into the structure if you
 * know where the files should be located
 *
 * Also killed all output unless DEBUG is defined as 1 or higher, you can easily enable it (along with even more debugging
 * output from all the crap I added) by defining DEBUG as 1...
 *
 * 2017 Sigurbjorn B. Larusson, http://www.dot1q.org/, version 3.0
 * 
 * Changed the default start sector to 0
 * Now uses getopt to retrieve command line options
 *    * Added a commandline option flag (-s) to specifiy a start sector, you can now place this in any order on the command line
 *    * Added a commandline option flag (-e) to specify an end sector, you can now place this in any order on the commandline
 *    * Added a commandline option flag (-d) to turn on debugging from the command line instead of having to recompile with the debug flag on
 *    * Added a commandline option flag (-o) to specify writing the output to a file instead of to stdout, this is usefull due to the amount of debugging that has been built into the program
 *
 * Added quite a bit of additional debugging output, it now goes through significant detail for every sector it works on, this can help puzzle together the filesystem, I highly recommend writing to a file!
 * Significantly altered the progress of creating directories, it now creates orphaned directories at the CWD so that the directory tree present on disk is always restored as close to the original as possible
 * Significantly altered the progress of writing orphaned files
 *    * It now tries to extract the original filename as well as the parent filename before making up a filename
 *    * It also tries to place them in their respective directories
 *    * It now creates files when parsing the headers, so even files that have no recoverable data get created and you know where they would have been placed in the hierarchy
 * Added a much better ascii/hexdumper function which now dumps both ascii and hex, 20 characters per line (20 ascii characters + FF format for the hex codes) 
 * Added many additional segmentation checks and cleaned up the memory allocation, it should be fairly rare for this utility to crash now, even with badly mangled filesystems
 *
 * 2019 Sigurbjorn B. Larusson, http://www.dot1q.org/, version 4.0
 *
 * Added support for compressed ADF files (gzip compressed)
 * Added support for DMS files
 *    This uses a lot of code borrowed from undmz.c, (C) David Tritscher and available on Aminet http://aminet.net/package/misc/unix/undms-1.3.c
 *    It also uses header information for DMS files from http://lclevy.free.fr/amiga/DMS.txt
 * Added support for ZIP files containing an ADF
 * Added commandline option to force treating file as a ADF file 
 * Added commandline option to force treating file as a ADZ file (gzip compressed ADF)
 * Added commandline option to force treating file as a DMS file
 * Added function to automagically determine format from file extension
 * Added support for re-creating original file timestamps 
 * Added support to detect endianness and checks to not convert msb to lsb if we're running on a big endian system
 *       it should therefore now be possible to compile and use this on a bigendian machine, if you try it, tell me about it
 * Fixed some minor bugs
 *
 * TODO:
 * The source code could do with a cleanup and even a rewrite, I'll leave that for the next time I have time to work on it
 */

// Define if you have ZLIB support, don't forget to compile with -lz
// Comment out if zlib support is not available (support for adz will not work)
#define	_HAVE_ZLIB

#include <libc.h>
#include <sys/_endian.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <inttypes.h>
#include <ctype.h>
#ifdef _HAVE_ZLIB
	#include <zlib.h>
#endif
#include <assert.h>
#include <utime.h>

// These are defaults
#define SECTORS 1760
#define FIRST_SECTOR 0 

// These are hard coded, no way to set them
#define	T_HEADER 2
#define	T_DATA 8
#define	T_LIST 16

// DMS statics
#define	DMS_NOZERO	1
#define DMS_ENCRYPT	2
#define DMS_APPENDS	4
#define DMS_BANNER	8
#define DMS_HIGHDENSITY 16
#define DMS_PC		32
#define DMS_DEVICEFIX	64
#define DMS_FILEIDBIZ	256

// Size of zlib chunks
#define CHUNK 0x4000

// Maximum number of sectors
#define MAX_SECTORS 3520

// Set this to 1 (or anything higher) if you want debugging information printed out, this can now be set at the command line 
#define DEBUG 0

// Maximum AmigaDOS filename length
#define MAX_AMIGADOS_FILENAME_LENGTH 32
// Maximum filename length generated by the program and on the input, this is a reasonably large assumption
#define MAX_FILENAME_LENGTH 256
// Maximum path depth, I'm not sure there is an explicit limit, but the max path length is 255 characters, so it can
// never go over that (or even reach it), we'll use 256 just to be safe
#define MAX_PATH_DEPTH 256

typedef unsigned int uint32_t;

// Stucture for the raw sector
struct sector_raw {
	uint8_t byte[512];
};

// Tthe blockheader
struct blkhdr {
	uint32_t type;
	uint32_t header_key;
	uint32_t seq_num;
	uint32_t data_size;
	uint32_t next_data;
	uint32_t chksum;
};

// The fileheader
struct fileheader {
	struct blkhdr hdr;
	uint8_t misc[288];
	uint32_t unused1;
	uint16_t uid;
	uint16_t gid;
	uint32_t protect;
	uint32_t byte_size;
	uint8_t comm_len;
	uint8_t comment[79];
	uint8_t unused2[12];
	int32_t days;
	int32_t mins;
	int32_t ticks;
	uint8_t name_len;
	char filename[30];
	uint8_t unused3;
	uint32_t unused4;
	uint32_t real_entry;
	uint32_t next_link;
	uint32_t unused5[5];
	uint32_t hash_chain;
	uint32_t parent;
	uint32_t extension;
	uint32_t sec_type;
};

// The dataheader
struct dataheader {
	struct blkhdr hdr;
	uint8_t data[488];
};

// The sector
union sector {
	struct blkhdr hdr;
	struct fileheader fh;
	struct dataheader dh;
};

// Added sibbi 2019, DMS packing variables and tables along with DMS unpacking functions

// Copy paste from code written by David Tritscher, with slight formatting changes
#define BUFFERSIZE 48000

unsigned char pack_buffer[BUFFERSIZE];
unsigned char unpack_buffer[BUFFERSIZE];

unsigned char info_header[4];
unsigned char archive_header[52];
unsigned char track_header[20];

unsigned char quick_buffer[256];

unsigned char medium_buffer[16384];

unsigned char deep_buffer[16384];
unsigned short deep_weights[628];
unsigned short deep_symbols[628];
unsigned short deep_hash[942];

unsigned char heavy_buffer[8192];
unsigned short heavy_literal_table[5120];
unsigned short heavy_offset_table[320];
unsigned char heavy_literal_len[512];
unsigned char heavy_offset_len[32];

unsigned int quick_local;
unsigned int medium_local;
unsigned int deep_local;
unsigned int heavy_local;
unsigned int heavy_last_offset;


static const unsigned short CRCTable[256]=
{
   0x0000,0xC0C1,0xC181,0x0140,0xC301,0x03C0,0x0280,0xC241,
   0xC601,0x06C0,0x0780,0xC741,0x0500,0xC5C1,0xC481,0x0440,
   0xCC01,0x0CC0,0x0D80,0xCD41,0x0F00,0xCFC1,0xCE81,0x0E40,
   0x0A00,0xCAC1,0xCB81,0x0B40,0xC901,0x09C0,0x0880,0xC841,
   0xD801,0x18C0,0x1980,0xD941,0x1B00,0xDBC1,0xDA81,0x1A40,
   0x1E00,0xDEC1,0xDF81,0x1F40,0xDD01,0x1DC0,0x1C80,0xDC41,
   0x1400,0xD4C1,0xD581,0x1540,0xD701,0x17C0,0x1680,0xD641,
   0xD201,0x12C0,0x1380,0xD341,0x1100,0xD1C1,0xD081,0x1040,
   0xF001,0x30C0,0x3180,0xF141,0x3300,0xF3C1,0xF281,0x3240,
   0x3600,0xF6C1,0xF781,0x3740,0xF501,0x35C0,0x3480,0xF441,
   0x3C00,0xFCC1,0xFD81,0x3D40,0xFF01,0x3FC0,0x3E80,0xFE41,
   0xFA01,0x3AC0,0x3B80,0xFB41,0x3900,0xF9C1,0xF881,0x3840,
   0x2800,0xE8C1,0xE981,0x2940,0xEB01,0x2BC0,0x2A80,0xEA41,
   0xEE01,0x2EC0,0x2F80,0xEF41,0x2D00,0xEDC1,0xEC81,0x2C40,
   0xE401,0x24C0,0x2580,0xE541,0x2700,0xE7C1,0xE681,0x2640,
   0x2200,0xE2C1,0xE381,0x2340,0xE101,0x21C0,0x2080,0xE041,
   0xA001,0x60C0,0x6180,0xA141,0x6300,0xA3C1,0xA281,0x6240,
   0x6600,0xA6C1,0xA781,0x6740,0xA501,0x65C0,0x6480,0xA441,
   0x6C00,0xACC1,0xAD81,0x6D40,0xAF01,0x6FC0,0x6E80,0xAE41,
   0xAA01,0x6AC0,0x6B80,0xAB41,0x6900,0xA9C1,0xA881,0x6840,
   0x7800,0xB8C1,0xB981,0x7940,0xBB01,0x7BC0,0x7A80,0xBA41,
   0xBE01,0x7EC0,0x7F80,0xBF41,0x7D00,0xBDC1,0xBC81,0x7C40,
   0xB401,0x74C0,0x7580,0xB541,0x7700,0xB7C1,0xB681,0x7640,
   0x7200,0xB2C1,0xB381,0x7340,0xB101,0x71C0,0x7080,0xB041,
   0x5000,0x90C1,0x9181,0x5140,0x9301,0x53C0,0x5280,0x9241,
   0x9601,0x56C0,0x5780,0x9741,0x5500,0x95C1,0x9481,0x5440,
   0x9C01,0x5CC0,0x5D80,0x9D41,0x5F00,0x9FC1,0x9E81,0x5E40,
   0x5A00,0x9AC1,0x9B81,0x5B40,0x9901,0x59C0,0x5880,0x9841,
   0x8801,0x48C0,0x4980,0x8941,0x4B00,0x8BC1,0x8A81,0x4A40,
   0x4E00,0x8EC1,0x8F81,0x4F40,0x8D01,0x4DC0,0x4C80,0x8C41,
   0x4400,0x84C1,0x8581,0x4540,0x8701,0x47C0,0x4680,0x8641,
   0x8201,0x42C0,0x4380,0x8341,0x4100,0x81C1,0x8081,0x4040
};

/* --------------------------------------------------------------------- */

static const unsigned char table_one[256]=
{
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
   3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,
   6,6,6,6,6,6,6,6,7,7,7,7,7,7,7,7,8,8,8,8,8,8,8,8,9,9,9,9,9,9,9,9,
   10,10,10,10,10,10,10,10,11,11,11,11,11,11,11,11,
   12,12,12,12,13,13,13,13,14,14,14,14,15,15,15,15,
   16,16,16,16,17,17,17,17,18,18,18,18,19,19,19,19,
   20,20,20,20,21,21,21,21,22,22,22,22,23,23,23,23,
   24,24,25,25,26,26,27,27,28,28,29,29,30,30,31,31,
   32,32,33,33,34,34,35,35,36,36,37,37,38,38,39,39,
   40,40,41,41,42,42,43,43,44,44,45,45,46,46,47,47,
   48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63
};

static const unsigned char table_two[256]=
{
   3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
   4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
   4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
   5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
   5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
   6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
   7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
   7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8
};

// Helper function to convert Amiga timestamp to unix timestamp
struct tm *amigatoepoch(unsigned int amigatime,struct tm *tm) {

	// Time variable for epoch at 1978-01-01
	time_t origstamp = 252460800;

	// Add to the origstampto get epoch time
	origstamp += amigatime;

	// Make tm struct from timestamp
	tm = localtime(&origstamp);

	// Return the timestruct
	return tm;

}

// Helper function to convert Amiga days, minutes, ticks to timestamp
struct utimbuf *amigadaystoutimbuf(uint32_t days, uint32_t minutes, uint32_t ticks,struct utimbuf *utim) {

	// Time variable for epoch at 1978-01-01
	time_t origstamp = 252460800;

	// Div by zero check and fix for ticks
	if(!ticks)
		ticks=1;

	// Add to the origstampto get epoch time
	// Multiply days with 86400 seconds per day, minutes with 60 seconds, and divide the ticks by 50 (ticks per second)
	origstamp += (time_t)(days*86400)+(minutes*60)+(ticks/50);

	// Set struct mod and access times as the current timestamp

	utim->actime = (time_t)origstamp;
	utim->modtime = (time_t)origstamp;

	// Return the utimstruct
	return utim;

}

// DMS helper functions to calculate CRC, (C) 1998 David Tritscher
unsigned int mycrc(unsigned char *memory, unsigned int length)
{
   register unsigned int temp = 0;

   while(length--)
      temp = CRCTable[(temp ^ *memory++) & 255] ^ ((temp >> 8) & 255);

   return (temp & 65535);
}

// DMS helper functions to calculate CRC, (C) 1998 David Tritscher
unsigned int mysimplecrc(unsigned char *memory, uint32_t length)
{
   register uint32_t temp = 0;

   while(length--) temp += *memory++;

   return (temp & 65535);
}

// DMS helper functions to depack sectors

// Store (unpacked) function, (C) 1998 David Tritscher
int crunch_store(unsigned char *source, unsigned char *source_end,
                 unsigned char *destination, unsigned char *destination_end,
	         unsigned int debug,FILE *debugfile)
{
	while((destination < destination_end) && (source < source_end))
		*destination++ = *source++;

	// Print debug output if wanted
	if(debug) 
		fprintf(debugfile,"\tstore: %s",((source != source_end) || (destination != destination_end)) ? "bad" : "good");

	return((source != source_end) || (destination != destination_end));
}


// RLE crunch function, (C) 1998 David Tritscher
// Commented and modified by Sigurbjorn B. Larusson, >255 count didn't work
int crunch_rle(unsigned char *source, unsigned char *source_end,
               unsigned char *destination, unsigned char *destination_end,
	       unsigned int debug, FILE *debugfile) 
{
	
	register unsigned char temp = 0;
	register unsigned char rlechar = 0;
	register unsigned int count = 0;

	int tempcount = 0;

	int rlebytes = 0;
	int rlesaved = 0;

	int totalbytes = 0;
	int unpackedsize = 0;
	int rletotalbytes = 0;

	// Until we've completed reading all the destination bytes...
	while((destination < destination_end) && (source < source_end)) {
		// Read current pointed to of source into temp and then increment source
		temp = *source++;
		totalbytes++;
		rletotalbytes++;
		// Not RLE encoded, just copy bytes
		if(temp != 144) {
			*destination++ = temp;
		} else {
			// We've wasted a character here on rlebytes
			rlebytes++;
			// Count is in next seat
			count = *source++;
			// Another character here on rlebytes
			rlebytes++;
			totalbytes++;
			rletotalbytes++;
			// Count uses more than one byte?
			if(count==255) {
				// Next byte is rlechar
				rlechar = *source++;
				totalbytes++;
				rletotalbytes++;
				// Next two bytes are the counter
				temp = *source++;
				totalbytes++;
				rletotalbytes++;
				tempcount = (temp <<8);
				count = tempcount;
				temp = *source++;
				totalbytes++;
				rletotalbytes++;
				count += temp;
				rlebytes+=3;
			} else if(count != 0) {
				// Next byte is rlechar
				rlechar = *source++;
				totalbytes++;
				rletotalbytes++;
			}
			// Count is 0 than this is a literal � character
			if(count == 0) {
				*destination++ = temp;
				// And we've saved one rle byte
				rlebytes--;
			// Counter is not 0, procceed with unRLE
			} else {
				// While we have repeats, fill the destination array with the repeated byte
				while(count > 0) {
					*destination++ = rlechar;
					count--;
					rlesaved++;
					rletotalbytes++;
				}
				rlesaved--;
			}
		}
	}  // End while
	unpackedsize = totalbytes - rlebytes + rlesaved;

	if(debug) {
		fprintf(debugfile,"\tTotal bytes used on RLE: %u, total bytes saved by RLE: %u, Totalbytes read: %u Totalbytes processed: %u Unpacked size: %u\n",rlebytes,rlesaved,totalbytes,rletotalbytes,unpackedsize);
		fprintf(debugfile,"\trunlength: %s\n",((source != source_end) || (destination != destination_end)) ? "bad" : "good");
	}

	return((source != source_end) || (destination != destination_end));
}

// Quick crunch function, (C) 1998 David Tritscher
int crunch_quick(unsigned char *source, unsigned char *source_end,
                 unsigned char *destination, unsigned char *destination_end,
	         unsigned int debug, FILE *debugfile,unsigned int no_clear_flag) 
{
	register unsigned int control = 0;
	register int shift = 0;
	int count, offset;

	quick_local += 5; /* i have no idea why it adds 5 */
	if(!no_clear_flag)
	for(quick_local = count = 0; count < 256; count++)
		quick_buffer[count] = 0;

	while((destination < destination_end) && (source < source_end)) {
		control <<= 9; /* all codes are at least 9 bits long */
		if((shift += 9) > 0) {
			control += *source++ << (8 + shift);
			control += *source++ << shift;
			shift -= 16;
		}
		if(control & 16777216) {
			*destination++ = quick_buffer[quick_local++ & 255] = control >> 16;
		} else {
			control <<= 2; /* 2 extra bits for length */
			if((shift += 2) > 0) {
				control += *source++ << (8 + shift);
				control += *source++ << shift;
				shift -= 16;
			}
			count = ((control >> 24) & 3) + 2;
			offset = quick_local - ((control >> 16) & 255) - 1;
			while((destination < destination_end) && (count--))
				*destination++ = quick_buffer[quick_local++ & 255] =
			quick_buffer[offset++ & 255];
		}
	} /* while */

	if(debug)
		fprintf(debugfile,"\tquick: %s\n",((source > source_end) || (destination != destination_end)) ? "bad" : "good");

	return((source > source_end) || (destination != destination_end));
}

// Medium crunch function, (C) 1998 David Tritscher
int crunch_medium(unsigned char *source, unsigned char *source_end,
                  unsigned char *destination, unsigned char *destination_end,
	          unsigned int debug, FILE *debugfile, unsigned int no_clear_flag) 
{
	register unsigned int control = 0;
	register int shift = 0;
	int count, offset, temp;

	medium_local += 66; /* i have no idea why it adds 66 */
	if(!no_clear_flag)
		for(medium_local = count = 0; count < 16384; count++)
			medium_buffer[count] = 0;

	while((destination < destination_end) && (source < source_end)) {
		control <<= 9; /* all codes are 9 bits long */
		if((shift += 9) > 0) {
			control += *source++ << (8 + shift);
			control += *source++ << shift;
			shift -= 16;
		}
		if((temp = (control >> 16) & 511) >= 256) {
			*destination++ = medium_buffer[medium_local++ & 16383] = temp;
		} else {
			count = table_one[temp] + 3;
			temp = table_two[temp];
			control <<= temp;
			if((shift += temp) > 0) {
				control += *source++ << (8 + shift);
				control += *source++ << shift;
				shift -= 16;
			}
			temp = (control >> 16) & 255;
			offset = table_one[temp] << 8;
			temp = table_two[temp];
			control <<= temp;
			if((shift += temp) > 0) {
				control += *source++ << (8 + shift);
				control += *source++ << shift;
				shift -= 16;
			}
			offset += (control >> 16) & 255;
			offset = medium_local - offset - 1;
			while((destination < destination_end) && (count--))
				*destination++ = medium_buffer[medium_local++ & 16383] = medium_buffer[offset++ & 16383];
		}
	} /* while */

	if(debug)
		fprintf(debugfile,"\tmedium: %s\n",((source > source_end) || (destination != destination_end)) ? "bad" : "good");

	return((source > source_end) || (destination != destination_end));
}

// Deep clear helper function (C) 1998 David Tritscher
void deep_clear(unsigned int debug, FILE *debugfile) {
	unsigned short count, temp;
	temp = 627;
	for(count = 0; count < 314; count++) {
		deep_weights[count] = 1;
		deep_symbols[count] = temp;
		deep_hash[temp] = count;
		temp++;
	}
	temp = 0;
	for(count = 314; count < 627; count++) {
		deep_weights[count] = deep_weights[temp] + deep_weights[temp + 1];
		deep_symbols[count] = temp;
		deep_hash[temp] = deep_hash[temp + 1] = count;
		temp += 2;
	}
	deep_weights[count] = 65535;
	deep_hash[temp] = 0;

	if(debug)
		fprintf(debugfile," ...clear");
}

// Deep scale helper function (C) 1998 David Tritscher
void deep_scale(unsigned int debug, FILE *debugfile)
{
	int symbol, swap, temp, weight;

	temp = 0;
	for(symbol = 0; symbol < 627; symbol++) {
		if(deep_symbols[symbol] >= 627)
		{
			deep_weights[temp] = (deep_weights[symbol] + 1) >> 1;
			deep_symbols[temp] = deep_symbols[symbol];
			temp++;
		}
	}
	temp = 0;
	for(symbol = 314; symbol < 627; symbol++) {
		weight = deep_weights[temp] + deep_weights[temp + 1];
		for(swap = symbol; deep_weights[swap - 1] > weight; swap--)
		{
			deep_weights[swap] = deep_weights[swap - 1];
			deep_symbols[swap] = deep_symbols[swap - 1];
		}
		deep_weights[swap] = weight;
		deep_symbols[swap] = temp;
		temp += 2;
	}
	for(symbol = 0; symbol < 627; symbol++) {
		temp = deep_symbols[symbol];
		deep_hash[temp] = symbol; if(temp < 627) deep_hash[temp + 1] = symbol;
	}

	if(debug)
		fprintf(debugfile," ...scale");
} 

// Deep crunch function, (C) 1998 David Tritscher
int crunch_deep(unsigned char *source, unsigned char *source_end,
                unsigned char *destination, unsigned char *destination_end,
		unsigned int debug, FILE *debugfile, unsigned int no_clear_flag)
{
	register unsigned int control = 0;
	register int shift = 0;
	int count, offset, temp, symbol, swap, temp1, temp2;

	deep_local += 60; /* i have no idea why it adds 60 */
	if(!no_clear_flag) {
		deep_clear(debug,debugfile);
		for(deep_local = count = 0; count < 16384; count++)
			deep_buffer[count] = 0;
	}

	while((destination < destination_end) && (source < source_end)) {
		count = deep_symbols[626]; /* start from the root of the trie */
		do {
			if(!shift++) {
				control += *source++ << 8;
				control += *source++;
				shift = -15;
			}
			control <<= 1;
			count += (control >> 16) & 1;
		} while((count = deep_symbols[count]) < 627);

		if(deep_weights[626] == 32768) /* scale the trie if the weight gets too large */
			deep_scale(debug,debugfile);

		symbol = deep_hash[count];
		do {
			deep_weights[symbol]++; /* increase the weight of this node */
			if(deep_weights[symbol + 1] < deep_weights[symbol]) {
				temp1 = deep_weights[(swap = symbol)];
				do {
					swap++;
				} while(deep_weights[swap + 1] < temp1);
				deep_weights[symbol] = deep_weights[swap];
				deep_weights[swap] = temp1;
				temp1 = deep_symbols[symbol];
				temp2 = deep_symbols[swap];
				deep_symbols[swap] = temp1;
				deep_symbols[symbol] = temp2;
				deep_hash[temp1] = swap; 
				if(temp1 < 627) 
					deep_hash[temp1 + 1] = swap;
				deep_hash[temp2] = symbol; 
				if(temp2 < 627) 
					deep_hash[temp2 + 1] = symbol;
				symbol = swap;
			}
		} while((symbol = deep_hash[symbol])); /* repeat until we reach root */

		if((count -= 627) < 256) {
			*destination++ = deep_buffer[deep_local++ & 16383] = count;
		} else {
			count -= 253; /* length is always at least 3 characters */
			control <<= 8;
			if((shift += 8) > 0) {
				control += *source++ << (8 + shift);
				control += *source++ << shift;
				shift -= 16;
			}
			temp = (control >> 16) & 255;
			offset = table_one[temp] << 8;
			temp = table_two[temp];
			control <<= temp;
			if((shift += temp) > 0) {
				control += *source++ << (8 + shift);
				control += *source++ << shift;
				shift -= 16;
			}
			offset += (control >> 16) & 255;
			offset = deep_local - offset - 1;
			while((destination < destination_end) && (count--))
				*destination++ = deep_buffer[deep_local++ & 16383] = deep_buffer[offset++ & 16383];
		}
	} /* while */

	if(debug)
		fprintf(debugfile,"\tdeep: %s\n",((source > source_end) || (destination != destination_end)) ? "bad" : "good");

	return((source > source_end) || (destination != destination_end));
}

// Helper function for heavy crunch, (C) 1998 David Tritscher
int make_decode_table(int number_symbols, int table_size,
                      unsigned char *length, unsigned short *table,
		      unsigned int debug, FILE *debugfile)
{
	register unsigned char bit_num = 0;
	register int symbol;
	unsigned int table_mask, bit_mask, pos, fill, next_symbol, leaf;
	int abort = 0;

	pos = 0;
	fill = 0;
	bit_mask = table_mask = 1 << table_size;

	while((!abort) && (bit_num <= table_size)) {
		for(symbol = 0; symbol < number_symbols; symbol++) {
			if(length[symbol] == bit_num) {
				if((pos += bit_mask) > table_mask) {
					abort = 1;
					break; /* we will overrun the table! abort! */
				}
				while(fill < pos)
					table[fill++] = symbol;
			}
		}
		bit_mask >>= 1;
		bit_num++;
	}

	if((!abort) && (pos != table_mask)) {
		for(; fill < table_mask; fill++)
			table[fill] = 0; /* clear the rest of the table */
		next_symbol = table_mask >> 1;
		pos <<= 16;
		table_mask <<= 16;
		bit_mask = 32768;

		while((!abort) && (bit_num <= 18)) {
			for(symbol = 0; symbol < number_symbols; symbol++) {
				if(length[symbol] == bit_num) {
					leaf = pos >> 16;
					for(fill = 0; fill < bit_num - table_size; fill++) {
						if(table[leaf] == 0) {
							table[(next_symbol << 1)] = 0;
							table[(next_symbol << 1) + 1] = 0;
							table[leaf] = next_symbol++;
						}
						leaf = table[leaf] << 1;
						leaf += (pos >> (15 - fill)) & 1;
					}
					table[leaf] = symbol;
					if((pos += bit_mask) > table_mask) {
						abort = 1;
						break; /* we will overrun the table! abort! */
					}
				}
			}
			bit_mask >>= 1;
			bit_num++;
		}
	}

	if(debug)
		fprintf(debugfile,"\tcreate_table: %s\n",((pos != table_mask) || abort) ? "bad" : "good");

	return((pos != table_mask) || abort);
}

// Heavy crunch function, (C) 1998 David Tritscher
int crunch_heavy(unsigned char *source, unsigned char *source_end,
                unsigned char *destination, unsigned char *destination_end,
                int flag, int special,
		int debug, FILE *debugfile, int no_clear_flag)
{
	register unsigned int control = 0;
	register int shift = 0;

	int count, offset, temp;

	if(!no_clear_flag) 
		heavy_local = 0;

	if(flag) {
		flag = 0;

		/* read the literal table */
		if(!flag) {
			for(count = 0; count < 512; count++)
				heavy_literal_len[count] = 255;
			control <<= 9; /* get number of literals */
			if((shift += 9) > 0) {
				control += *source++ << (8 + shift);
				control += *source++ << shift;
				shift -= 16;
			}
			if((offset = (control >> 16) & 511))
				for(count = 0; count < offset; count++) {
					control <<= 5; /* get the length of this literal */
					if((shift += 5) > 0) {
						control += *source++ << (8 + shift);
						control += *source++ << shift;
						shift -= 16;
					}
					temp = (control >> 16) & 31;
					heavy_literal_len[count] = (temp ? temp : 255);
				} 
			else {
				control <<= 9; /* get the defined literal */
				if((shift += 9) > 0) {
					control += *source++ << (8 + shift);
					control += *source++ << shift;
					shift -= 16;
				}
				heavy_literal_len[(control >> 16) & 511] = 0;
			}

			flag = make_decode_table(512, 12, heavy_literal_len, heavy_literal_table,debug,debugfile);
		}

		/* read the offset table */
		if(!flag) {
			for(count = 0; count < 32; count++)
				heavy_offset_len[count] = 255;

			control <<= 5; /* get number of offsets */
			if((shift += 5) > 0) {
				control += *source++ << (8 + shift);
				control += *source++ << shift;
				shift -= 16;
			}
			if((offset = (control >> 16) & 31))
				for(count = 0; count < offset; count++) {
					control <<= 4; /* get the length of this offset */
					if((shift += 4) > 0) {
						control += *source++ << (8 + shift);
						control += *source++ << shift;
						shift -= 16;
					}
					temp = (control >> 16) & 15;
					heavy_offset_len[count] = (temp ? temp : 255);
				}
			else {
				control <<= 5; /* get the defined offset */
				if((shift += 5) > 0) {
					control += *source++ << (8 + shift);
					control += *source++ << shift;
					shift -= 16;
				}
				heavy_offset_len[(control >> 16) & 31] = 0;
			}
			temp = heavy_offset_len[special];
			heavy_offset_len[special] = heavy_offset_len[31];
			heavy_offset_len[31] = temp;

			flag = make_decode_table(32, 8, heavy_offset_len, heavy_offset_table,debug,debugfile);
		}

	} /* if(flag) */

	if(!flag) {
		/* prefetch 12 bits for fast huffman decoding */
		control <<= 12;
		if((shift += 12) > 0) {
			control += *source++ << (8 + shift);
			control += *source++ << shift;
			shift -= 16;
		}

		while((destination < destination_end) && (source < source_end)) {

			/* get a literal */
			if((count = heavy_literal_table[(control >> 16) & 4095]) >= 512) {
				do /* literal is longer than 12 bits */ {
					if(!shift++) {
						control += *source++ << 8;
						control += *source++;
						shift = -15;
					}
					control <<= 1;
					count = heavy_literal_table[((control >> 16) & 1) + (count << 1)];
				} while(count >= 512);
				temp = 12; /* skip the original 12 bits */
			} else {
				temp = heavy_literal_len[count];
			}
			control <<= temp;
			if((shift += temp) > 0) {
				control += *source++ << (8 + shift);
				control += *source++ << shift;
				shift -= 16;
			}

			/* less than 256 = literal, otherwise = length of string */
			if(count < 256) {
				*destination++ = heavy_buffer[heavy_local++ & 8191] = count;
			} else { /* must have been a string */
				count -= 253; /* length is always at least 3 characters */
				if((offset = heavy_offset_table[(control >> 20) & 255]) >= 32) {
					do { /* offset is longer than 8 bits */
						if(!shift++) {
							control += *source++ << 8;
							control += *source++;
							shift = -15;
						}
						control <<= 1;
						offset = heavy_offset_table[((control >> 20) & 1) + (offset << 1)];
					} while(offset >= 32);
					temp = 8; /* skip the original 8 bits */
				} else {
					temp = heavy_offset_len[offset];
				}
				control <<= temp;
				if((shift += temp) > 0) {
					control += *source++ << (8 + shift);
					control += *source++ << shift;
					shift -= 16;
				}

				if(offset == 31) {
					offset = heavy_last_offset;
				} else {
					if(offset) {
						temp = offset - 1;
						offset = ((control & 268369920) | 268435456) >> (28 - temp);
						control <<= temp;
						if((shift += temp) > 0) {
							control += *source++ << (8 + shift);
							control += *source++ << shift;
							shift -= 16;
						}
					}
					heavy_last_offset = offset;
				}
				offset = heavy_local - offset - 1;
				while((destination < destination_end) && (count--))
					*destination++ = heavy_buffer[heavy_local++ & 8191] = heavy_buffer[offset++ & 8191];
			} /* if(string) */
		}
	} /* if(!flag) */

	if(debug)
		fprintf(debugfile,"\theavy: %s\n",((source > source_end) || (destination != destination_end) || flag) ? "bad" : "good");

	return((source > source_end) || (destination != destination_end) || flag);
}

#define DATABYTES (sizeof(union sector)-sizeof(struct blkhdr))

// Print usage information, Added Sibbi 2011, added 2017, 2019
void usage(char *programname) {
	fprintf(stderr,"Extract-ADF 4.0 Originally (C)2008 Michael Steil with many further additions by Sigurbjorn B. Larusson\n");
	fprintf(stderr,"DMS extraction code (C) 1998 David Tritscher\n");
        fprintf(stderr,"\nUsage: %s [-D] [-a] [-z] [-d] [-s <startsector>] [-e <endsector>] [-o <outputfilename>] <adf/adz/dmsfilename>\n",programname);
	fprintf(stderr,"\n\t-a will force ADF extraction (if the filename ends in adf ADF will be assumed");
	fprintf(stderr,"\n\t-z will force ADZ extraction (if the filename ends in adz or adf.gz ADZ will be assumed");
	fprintf(stderr,"\n\t-d will force DMS extraction (if the filename ends in dms DMS format will be assumed");
	fprintf(stderr,"\n\t-D will activate debugging output which will print very detailed information about everything that is going on");
	fprintf(stderr,"\n\t-s along with an integer argument from 0 to 1760 (DD) or 3520 (HD), will set the starting sector of the extraction process");
	fprintf(stderr,"\n\t-e along with an integer argument from 0 to 1760 (DD) or 3520 (HD), will set the end sector of the extraction process");
	fprintf(stderr,"\n\t-o along with an outputfilename will redirect output (including debugging output) to a file instead of to the screen");
	fprintf(stderr,"\n\tFinally the last argument is the ADF/ADZ or DMS filename to process");
	fprintf(stderr,"\n\nThe defaults for start and end sector are 0 and 1760 respectively, this tool was originally"),
	fprintf(stderr,"\ncreated to salvage lost data from kickstart disks (which contain the kickstart on sectors 0..512)");
	fprintf(stderr,"\nin order to skip the sectors on kickstart disks which might contain non OFS data, set the start sector to 513\n");
	fprintf(stderr,"\nTo use this tool on a HD floppy, the end sector needs to be 3520\n");
	fprintf(stderr,"\nIf you get a Bus error it means that you specificed a non-existing end sector\n");
	fprintf(stderr,"\nThis program does not support FFS floppies(!), it only supports OFS style Amiga Floppies\n");
	fprintf(stderr,"\nHappy hunting!\n");
}

// Added Sibbi for version 4
// Uncompress file to temporary location and return a filepointer to the temporary uncompressed file
#ifdef _HAVE_ZLIB
FILE *uncompressfile(char *inputfile,unsigned int debug, FILE *debugfile) {
	// File pointers
	FILE *infile;
	FILE *outfile;

	// Temporary filename
	char tmptemplate[] = "/tmp/extractadf.XXXXX";

	// File descriptor for temp file
	int fd = 0;

	// To store whether this is a gzip or zip file, both of which are (annoyingly) common
	int iszip = 0;
	// To store the filename length for a zip file header
	int zipfilenamelength = 0;
	//  To store the extra header length for a zip file header
	int zipextraheader = 0;

	if(debug)
	fprintf(debugfile,"Input filename is %s\n",inputfile);
	
	
	// Define ZLib stream, and input and output chunks (CHUNK is defined as 0x4000 earlier in thsi program)
	z_stream strm;
	unsigned char in[CHUNK];
	unsigned char out[CHUNK];

	// For reading in the file header
	unsigned char header[5];
	// How many bytes we have
	unsigned int have = 0;
	// Store return code of inflate
	int ret = 0;

	// Open input and output files
	if(inputfile != NULL) {
		infile = fopen(inputfile,"r");
		if(infile == NULL)  {
			fprintf(stderr,"Can't open input file\n");
			// Can't open file
			return NULL;
		}
		// Is this a zip file?  A lot of people assume gzip/zip are the same, they are obviously not, zip is an archive file format
		// However, even if this is a zip file, we might still be able to decompress it
		// Read in the first four bytes
		if(fread(header,1,4,infile) == 4) {
			if(header[0] == 0x50 && header[1] == 0x4B && header[2] == 0x03 && header[3] == 0x04)  {
				fprintf(stderr,"Input file appears to be in zip format\n");
				// It's a zip file
				iszip=1;
				// Check the minimum version fields
				if(fread(header,1,2,infile) == 2) {
					if(header[1] != 0) {
						// Seek to byte 5, after the pkzip + min version
						fseek(infile,5,SEEK_SET);
					// Otherwise this is a regular zip file
					} else {
						// Skip to byte 26 in the header
						fseek(infile,26,SEEK_SET);
						// Read the next 2 bytes which represent the length of the filename 
						if(fread(header,1,2,infile) == 2) {
							// Get length of filename (which is in LSB format) 
							zipfilenamelength = (header[1]<<8)+header[0];
							if(debug)
								fprintf(debugfile,"Filename length %u %u\n",header[0],header[1]);
							// Then the length of the extra header
							if(fread(header,1,2,infile) == 2) {
								zipextraheader = (header[1]<<8)+header[0];
								// Skip 30+zipfilenamelength + zipextraheader bytes into the file
								if(debug)
									fprintf(debugfile,"Extra header length %u %u\n",header[0],header[1]);
								fseek(infile,30+zipextraheader+zipfilenamelength,SEEK_SET);
							} else {
								fprintf(stderr,"ZIP header damaged\n");
								return NULL;
							}
						} else {
							fprintf(stderr,"ZIP header damaged\n");
							return NULL;
						}
					} 
				} else {
					fprintf(stderr,"ZIP header damaged\n");
					return NULL;
				}
			} else {
				// Rewind to start of file
				rewind(infile);
			}
		} else {
			// Can't read 4 bytes from file
			fprintf(stderr,"Can't read from input file\n");
			return NULL;
		}	
		// Open temporary outfile
		fd = mkstemp(tmptemplate);
		// Can't create temp file
		if(fd == -1) {
			fprintf(stderr,"Can't open temporary file\n");
			return NULL;
		}
		outfile = fdopen(fd,"w+");
		if(outfile == NULL) {
			fprintf(stderr,"Can't write temporary file\n");
			// Can't write temporary outfile
			return NULL;
		}
	} else {
		fprintf(stderr,"Inputfile is not valid\n");
		// Input file is not valid
		return NULL;
	}

	// Initial inflate state
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	strm.avail_in = 0;
	strm.next_in = Z_NULL;

	// if zip file return header, if we can't init, exit with NULL
	if(!iszip) {
		// Rewind to start of file
		if(inflateInit2(&strm,32+MAX_WBITS) != Z_OK) {
			fprintf(stderr,"Can't init zlib\n");
			return NULL;
		}
	} else {
		if(inflateInit2(&strm,-MAX_WBITS) != Z_OK) {
			fprintf(stderr,"Can't init zlib\n");
			return NULL;
		}
	}


	// Otherwise decompress until end of stream
	do {
		// Read CHUNK bytes from inputfile into the in buffer
		strm.avail_in = fread(in,1,CHUNK,infile);
		// Check for file error
		if(ferror(infile)) {
			fprintf(stderr,"Can't read inputfile\n");
			// Error reading from inputfile, end inflate, return NULL
			(void)inflateEnd(&strm);
			return NULL;
		}
		if(strm.avail_in == 0)
			// We are done reading, end this loop
			break;
		strm.next_in = in;
		// Decompress data until available out bytes i 0
		do {
			strm.avail_out = CHUNK;
			strm.next_out = out;

			// Inflate and check for return code
			ret = inflate(&strm,Z_NO_FLUSH);
			assert(ret != Z_STREAM_ERROR);  /* state not clobbered, if so exit */
			// If we encounter errors then exit
			switch(ret) {
				case Z_NEED_DICT:
					fprintf(stderr,"Dictionary error while decompressing\n");
					ret = Z_DATA_ERROR;     /* and fall through */
				case Z_DATA_ERROR:
					fprintf(stderr,"Data error while decompressing\n");
				case Z_MEM_ERROR:
					(void)inflateEnd(&strm);
					return NULL;
			}
			// Number of bytes we have to write
			have = CHUNK - strm.avail_out;
			// If we can't write out or there's a file error, exit
			if(fwrite(out,1,have,outfile) != have || ferror(outfile)) {
				fprintf(stderr,"Can't write to output file\n");
				(void)inflateEnd(&strm);
				return NULL;
			}
		} while(strm.avail_out == 0);
	} while (ret != Z_STREAM_END);
	// If we reached here the file is uncompressed...
	// Close the input file
	fclose(infile);
	// Seek to 0 in the output file
	rewind(outfile);
	//  Return filepointer
	return outfile;
}	// End function uncompressfile
#endif 	// if defined _HAVE_ZLIB

// Added Sibbi for version 4
// Unpack DMS file to temporary location and return a filepointer to the temporary uncompressed file
// Loosely based on code (C) 1998 David Tritscher
FILE *undmsfile(char *inputfile,int endsector, unsigned int debug, FILE *debugfile) {
	// File pointers
	FILE *infile;
	FILE *outfile;

	// Temporary filename
	char tmptemplate[] = "/tmp/extractadf.XXXXX";

	// File descriptor for temp file
	int fd = 0;

	if(debug)
		fprintf(debugfile,"Input filename is %s\n",inputfile);
	
	// Header array to read the dms header
	unsigned char header[64];

	//  Track header array to read the trackk header
	unsigned char trackheader[32];

	// Infobits integer
	unsigned int infobits = 0;

	//  Start and end track of DMS file
	unsigned short dmsstarttrack = 0;
	unsigned short dmsendtrack =  0;

	// Size of compressed and uncompressed file
	unsigned int dmspacksize = 0;
	unsigned int dmsunpacksize = 0;

	// Serial number of DMS file
	unsigned int dmsserial = 0;

	// DMS creator machine information
	unsigned int dmscpu = 0;
	unsigned int dmscopro = 0;
	unsigned int dmsmachine = 0;
	unsigned int dmsextra = 0;
	unsigned int dmscpuspeed = 0;

	// Time taken to create archive
	unsigned int dmscreatetime = 0;

	// Versions used to create and needed to extract this DMS
	unsigned short dmscreateversion = 0;
	unsigned short dmsextractversion = 0;

	// Type of disk in this archive
	unsigned short dmsdisktype = 0;

	// Struct to store dms time
	struct tm *dmstime;
	dmstime = malloc(sizeof(struct tm));

	// Crunchmode used
	unsigned short dmscrunchmode = 0;

	// DMS header CRC
	unsigned short dmsheadercrc = 0;

	// Timestamp integer
	time_t dmstimestamp = 0;

	// Track header variables
	unsigned int trackcurrent = 0;
	unsigned int trackpacked = 0;
	unsigned int trackrlesize = 0;
	unsigned int trackunpacked = 0;
	unsigned int trackpackmode = 0;
	unsigned int trackcrc = 0;
	unsigned int trackpackcrc = 0;
	unsigned int trackunpackcrc = 0;
	unsigned int trackcflags = 0;

	// Flags set in trackcflags
	unsigned int trackcflag_noclear = 0;
	unsigned int trackcflag_compressed = 0;
	unsigned int trackcflag_rle = 0;

	// Pointer to the current buffer
	unsigned char *buffer;

	// String to print time
	char timestring[80];

	// Temporary loop variable
	int i=0;

	// Open input and output files
	if(inputfile != NULL) {
		infile = fopen(inputfile,"r");
		if(infile == NULL)  {
			fprintf(stderr,"Can't open input file\n");
			// Can't open file
			return NULL;
		}
		// Open temporary outfile
		fd = mkstemp(tmptemplate);
		// Can't create temp file
		if(fd == -1) {
			fprintf(stderr,"Can't open temporary file\n");
			return NULL;
		}
		// Open outfile for read/write
		outfile = fdopen(fd,"w+");
		if(outfile == NULL) {
			fprintf(stderr,"Can't write temporary file\n");
			// Can't write temporary outfile
			return NULL;
		}
		// We're ready to start processing the header...
		if((fread(header,1,4,infile) == 4)) {
			// Check for DMS header
			if(header[0] == 'D' && header[1] == 'M' && header[2] == 'S' && header[3] == '!') {
				if(debug)
					fprintf(debugfile,"Valid DMS header found, proceeding\n");
			}
		}
		// Read the rest of the header
		if((fread(header,1,52,infile) == 52)) {
			if((header[0] == ' ' && header[1] == 'P' && header[2] == 'R' && header[3] == 'O') && debug) 
				fprintf(debugfile,"File is a DMS PRO file\n");

			// Infobits is a long word stored in MSB at header locations 8-11
			infobits=(header[4]<<24)+(header[5]<<16)+(header[6]<<8)+header[7];

			if(debug)
				fprintf(debugfile,"Infobits %u %u %u %u total: %u\n",header[4],header[5],header[6],header[7],infobits);

			// DMS no zero flag is set
			if((infobits & DMS_NOZERO) && debug)
				fprintf(debugfile,"DMS No zero flag is set\n");
			// Encrypted DMS file, return null, print error
			if(infobits & DMS_ENCRYPT) {
				fprintf(stderr,"This is an encrypted DMS file, those are unsupported, please decrypt the file before using thisi program\n");
				return NULL;	
			}

			// Optimized DMS file (appends)
			if((infobits & DMS_APPENDS) && debug) 
				fprintf(debugfile,"DMS Appens flag is set\n");

			// Banner in file
			if((infobits & DMS_BANNER) && debug) 
				fprintf(debugfile,"DMS banner exists in file\n");
			
			// File is high density file...
			if((infobits & DMS_HIGHDENSITY) && endsector < MAX_SECTORS) {
				fprintf(stderr,"File is high density and endsector is less than 3520\n");
				return NULL;
			}

			// File is a PC floppy
			if(infobits & DMS_PC)  {
				fprintf(stderr,"File is a PC floppy\n");
				return NULL;
			}

			// DMS device fix bit is set
			if((infobits & DMS_DEVICEFIX) && debug)
				fprintf(debugfile,"DMS Device Fix bit is set\n");
	
			// DMS FILE_ID.DIZ bit is set
			if((infobits & DMS_FILEIDBIZ) && debug)
				fprintf(debugfile,"DMS FILE.ID_BIZ bit is set\n");

			// Get amiga timestamp from header (Amiga seconds since 1. jan 1978)
			dmstimestamp=(time_t)(header[8]<<24)+(header[9]<<16)+(header[10]<<8)+header[11];	

			// Convert DMS timestring into epoch, and then to a valid timestring
			strftime(timestring,80,"%c",amigatoepoch(dmstimestamp,dmstime));

			if(debug)
				fprintf(debugfile,"File created %s\n",timestring);

			// Start and end track
			dmsstarttrack = (header[12]<<8)+header[13];
			dmsendtrack = (header[14]<<8)+header[15];

			if(debug)
				fprintf(debugfile,"DMS start track: %u, end track: %u\n",dmsstarttrack,dmsendtrack);

			// Before and after compression sizes
			dmspacksize = (header[16]<<24)+(header[17]<<16)+(header[18]<<8)+header[19];
			dmsunpacksize = (header[20]<<24)+(header[21]<<16)+(header[22]<<8)+header[23];

			if(debug)
				fprintf(debugfile,"DMS Packed size: %u, unpacked size: %u\n",dmspacksize,dmsunpacksize);

			// Serial number of the creator
			dmsserial = (header[24]<<24)+(header[25]<<16)+(header[26]<<8)+header[27];
			if(debug) {
				if(dmsserial == 4292345787)
					fprintf(debugfile,"DMS Serial number of creator: Unregistered copy\n");
				else
					fprintf(debugfile,"DMS Serial number of creator: %u\n",dmsserial);
			}

			// CPU, co-pro, machine type and cpu speed of creator
			dmscpu = (header[28]<<8)+header[29];
			dmscopro = (header[30]<<8)+header[30];
			dmsmachine = (header[32]<<8)+header[33];
			dmsextra = (header[34]<<8)+header[35];	
			dmscpuspeed = ((header[36]<<8)+header[37]);

			if(debug) {
				switch(dmscpu) {
					case 0:
						fprintf(debugfile,"DMS CPU type of creator: M68000\n");
						break;
					case 1:
						fprintf(debugfile,"DMS CPU type of creator: M68010\n");
						break;
					case 2:
						fprintf(debugfile,"DMS CPU type of creator: M68020\n");
						break;
					case 3:
						fprintf(debugfile,"DMS CPU type of creator: M68030\n");
						break;
					case 4:
						fprintf(debugfile,"DMS CPU type of creator: M68040\n");
						break;
					case 5:
						fprintf(debugfile,"DMS CPU type of creator: M68060\n");
						break;
					case 6:
						fprintf(debugfile,"DMS CPU type of creator: i8086\n");
						break;
					case 7:
						fprintf(debugfile,"DMS CPU type of creator: i8088\n");
						break;
					case 8:
						fprintf(debugfile,"DMS CPU type of creator: i80188\n");
						break;
					case 9:
						fprintf(debugfile,"DMS CPU type of creator: i80186\n");
						break;
					case 10:
						fprintf(debugfile,"DMS CPU type of creator: i80286\n");
						break;
					case 11:
						fprintf(debugfile,"DMS CPU type of creator: i80386SX\n");
						break;
					case 12:
						fprintf(debugfile,"DMS CPU type of creator: i80386\n");
						break;
					case 13:
						fprintf(debugfile,"DMS CPU type of creator: i80486\n");
						break;
					case 14:
						fprintf(debugfile,"DMS CPU type of creator: i80586\n");
						break;
					default:
						fprintf(debugfile,"DMS CPU type of creator: unknown\n");
						break;
				}
				switch(dmscopro) {
					case 0:
						fprintf(debugfile,"DMS Math Coprocessor type of creator: None\n");
						break;
					case 1:
						fprintf(debugfile,"DMS Math Coprocessor type of creator: M68881\n");
						break;
					case 2:
						fprintf(debugfile,"DMS Math Coprocessor type of creator: M68882\n");
						break;
					case 3:
						fprintf(debugfile,"DMS Math Coprocessor type of creator: i8087\n");
						break;
					case 4:
						fprintf(debugfile,"DMS Math Coprocessor type of creator: i80287SX\n");
						break;
					case 5:
						fprintf(debugfile,"DMS Math Coprocessor type of creator: i80387\n");
						break;
					default:
						fprintf(debugfile,"DMS Math Coprocessor type of creator: unknown\n");
						break;
				}
				switch(dmsmachine) {
					case 0:
						fprintf(debugfile,"DMS Machine type of creator: Unknown\n");
						break;
					case 1:
						fprintf(debugfile,"DMS Machine type of creator: Amiga\n");
						break;
					case 2:
						fprintf(debugfile,"DMS Machine type of creator: x86 Clone\n");
						break;
					case 3:
						fprintf(debugfile,"DMS Machine type of creator: Mac\n");
						break;
					case 4:
						fprintf(debugfile,"DMS Machine type of creator: Atari\n");
						break;
					default:
						fprintf(debugfile,"DMS Machine type of creator: Unknown\n");
						break;
				}
				switch(dmsextra) {
					case 0x8000:
						fprintf(debugfile,"DMS Machine of creator is AGA\n");
						break;

				}
				fprintf(debugfile,"DMS CPU speed of creator (approx) in Mhz: %d\n",dmscpuspeed);
			} // if debug print creator machine info

			dmscreatetime = (header[38]<<24)+(header[39]<<16)+(header[40]<<8)+header[41];
			if(debug)
				fprintf(debugfile,"DMS Time taken to create archive by creator: %u\n",dmscreatetime);

			dmscreateversion = (header[42]<<8)+header[43];
			dmsextractversion = (header[44]<<8)+header[45];

			if(debug) {
				fprintf(debugfile,"DMS version used to create this archive: %u\n",dmscreateversion);
				fprintf(debugfile,"DMS version required to extract this archive: %u\n",dmsextractversion);
			}

			dmsdisktype = (header[46]<<8)+header[47];
		
			if(debug) {
				switch(dmsdisktype) {
					case 0:
						fprintf(debugfile,"DMS Diskette type: Unknown, proceeding anyway\n");
						break;
					case 1:
						fprintf(debugfile,"DMS Diskette type: Amiga OFS\n");
						break;
					case 2:
						fprintf(debugfile,"DMS Diskette type: Amiga FFS, this program does not support non OFS floppies\n");
						return NULL;
						break;
					case 3:
						fprintf(debugfile,"DMS Diskette type: Amiga 3.0 International mode, this program does not support non OFS floppies\n");
						return NULL;
						break;
					case 4:
						fprintf(debugfile,"DMS Diskette type: Amiga 3.0 FFS International mode, this program does not support non OFS floppies\n");
						return NULL;
						break;
					case 5:
						fprintf(debugfile,"DMS Diskette type: Amiga 3.0 Dircache mode, this program does not support non OFS floppies\n");
						return NULL;
						break;
					case 6:
						fprintf(debugfile,"DMS Diskette type: Amiga 3.0 FFS Dircache mode, this program does not support non OFS floppies\n");
						return NULL;
						break;
					case 7:
						fprintf(debugfile,"DMS Diskette type: FMS (Filemasher) mode, this program does not support non OFS floppies\n");
						return NULL;
						break;
					default:
						fprintf(debugfile,"DMS Diskette type: Unknown, proceeding anyway\n");
						break;

				}
			}

			dmscrunchmode = (header[48]<<8) + header[49];
			dmsheadercrc = (header[50]<<8)+header[51];

			if(mycrc(header, 50) == dmsheadercrc) 
				fprintf(debugfile,"DMS header CRC is OK\n");
			else
				fprintf(debugfile,"DMS header CRC mismatch, changes are this is a damaged archive, continuing anyway\n");

			if(debug) {
				fprintf(debugfile,"DMS Header CRC: %u\n",dmsheadercrc);
			}
			// Print the crunch mode
			switch(dmscrunchmode) {
				case 0:
					fprintf(debugfile,"DMS crunch mode: No compression\n");
					break;
				case 1:
					fprintf(debugfile,"DMS crunch mode: Simple compression\n");
					break;
				case 2:
					fprintf(debugfile,"DMS crunch mode: Quick compression\n");
					break;
				case 3:
					fprintf(debugfile,"DMS crunch mode: Medium compression\n");
					break;
				case 4:
					fprintf(debugfile,"DMS crunch mode: Deep compression\n");
					break;
				case 5:
					fprintf(debugfile,"DMS crunch mode: Heavy (1) compression\n");
					break;
				case 6:
					fprintf(debugfile,"DMS crunch mode: Heavy (2) compression\n");
					break;
				case 7:
					fprintf(debugfile,"DMS crunch mode: Heavy (3) compression\n");
					break;
				case 8:
					fprintf(debugfile,"DMS crunch mode: Heavy (4) compression\n");
					break;
				case 9:
					fprintf(debugfile,"DMS crunch mode: Heavy (5) compression\n");
					break;
				default:
					fprintf(debugfile,"Unknown crunch mode used in DMSg\n");
					return NULL;
			}
			// Read the track headers and on and on until we're done..
			for(i=dmsstarttrack;i<=dmsendtrack;i++) {
				if((fread(trackheader,1,20,infile)) == 20)  {
					// Read the trackheader successfully, check if it's valid
					if(trackheader[0] == 'T' && trackheader[1] == 'R')  {
						if(debug) 
							fprintf(debugfile,"Valid track header on track %u, file position: 0x%lx\n",i,ftell(infile));
						// Get CRC of header
						trackcrc = (trackheader[18]<<8) + trackheader[19];
						// Get CRC of packed track
						trackpackcrc = (trackheader[16]<<8) + trackheader[17];
						// Get CRC of unpacked track
						trackunpackcrc = (trackheader[14]<<8) + trackheader[15];
						// Get flags
						trackcflags = trackheader[12];

						// Set flags accordingly
						trackcflag_noclear = trackcflags & 1;
						trackcflag_compressed = trackcflags & 2;
						trackcflag_rle = trackcflags & 4;

						// This is a valid trackheader, check if CRC is valid
						if( mycrc(trackheader, 18) == trackcrc) {
							if(debug)
								fprintf(debugfile,"\tTrack header CRC is OK\n");
							trackcurrent = (trackheader[2]<<8)+trackheader[3];
							if(trackcurrent != i) 
								fprintf(debugfile,"\tCurrent track, track header mismatch, current: %u header: %u\n",trackcurrent,i);
							else if(debug)
								fprintf(debugfile,"\tCurrent track OK, current: %u header: %u\n",trackcurrent,i);
							trackpacked = (trackheader[6]<<8)+trackheader[7];
							trackrlesize = (trackheader[8]<<8)+trackheader[9];
							trackunpacked = (trackheader[10]<<8)+trackheader[11];
							if(debug) {
									fprintf(debugfile,"\tPacked track size: %u, RLE size: %u, Unpacked size: %u\n",trackpacked,trackrlesize,trackunpacked);
									fprintf(debugfile,"\tTrack compression flags set: %u noclear: %u compressed: %u rle: %u\n",trackcflags,trackcflag_noclear,trackcflag_compressed,trackcflag_rle);
							}

							trackpackmode = trackheader[13]; 

							// Read in the packed bytes
							if((fread(pack_buffer,1,trackpacked,infile) == trackpacked) && mycrc(pack_buffer, trackpacked) == trackpackcrc) {
								// Managed to read in the packed bytes from the file
	
								// Deal with the decompression
								// This is in order of increasing compression, the simplest is just store encryption where there is no compression and no RLE encoding
								// Then there is just RLE, followed by quick, medium, deep, heavy and heavy 2 compression, there are further 3 options available in DMS which are not supported in this program, heavy(3), heavy(4) and heavy(5) 
				
								switch(trackpackmode) {
									case 0:
										if(debug)
											fprintf(debugfile,"\tTrack crunch mode: No compression\n");
										if(!crunch_store(unpack_buffer, unpack_buffer + trackrlesize,
											         pack_buffer, pack_buffer + trackunpacked,
												 debug,debugfile))
											buffer = unpack_buffer;
										break;
									case 1:
										if(debug)
											fprintf(debugfile,"\tDMS crunch mode: Simple compression\n");
										if(!crunch_rle(unpack_buffer, unpack_buffer + trackrlesize,
											       pack_buffer, pack_buffer + trackunpacked,
											       debug,debugfile))
											buffer=unpack_buffer;
										break;
									case 2:
										if(debug)
											fprintf(debugfile,"\tDMS crunch mode: Quick compression\n");
										if(!crunch_quick(pack_buffer, pack_buffer + trackpacked + 16,
											        unpack_buffer, unpack_buffer + trackrlesize,
												debug,debugfile,trackcflag_noclear))
											if(!crunch_rle(unpack_buffer, unpack_buffer + trackrlesize,
											               pack_buffer, pack_buffer + trackunpacked,
												       debug,debugfile))
												buffer=pack_buffer;
										break;
									case 3:
										if(debug)
											fprintf(debugfile,"\tDMS crunch mode: Medium compression\n");
										if(!crunch_medium(pack_buffer, pack_buffer + trackpacked + 16,
											        unpack_buffer, unpack_buffer + trackrlesize,
												debug,debugfile,trackcflag_noclear))
											if(!crunch_rle(unpack_buffer, unpack_buffer + trackrlesize,
											               pack_buffer, pack_buffer + trackunpacked,
												       debug,debugfile))
												buffer = pack_buffer;
										break;
									case 4:
										if(debug)
											fprintf(debugfile,"\tDMS crunch mode: Deep compression\n");

										if(!crunch_deep(pack_buffer, pack_buffer + trackpacked + 16,
											        unpack_buffer, unpack_buffer + trackrlesize,
												debug,debugfile,trackcflag_noclear))
											if(!crunch_rle(unpack_buffer, unpack_buffer + trackrlesize,
											               pack_buffer, pack_buffer + trackunpacked,
													debug,debugfile))
												buffer = pack_buffer;
										break;
									case 5:
										if(debug)
											fprintf(debugfile,"\tDMS crunch mode: Heavy (1) compression\n");
										// Decrunch, the track
										if(!crunch_heavy(pack_buffer, pack_buffer + trackpacked + 16, 
											        unpack_buffer, unpack_buffer + trackrlesize,
											        trackcflag_compressed, 13,
												debug,debugfile, trackcflag_noclear)) {	
											// If RLE is set, also use RLE
											if(trackcflag_rle) { 
												if(!crunch_rle(unpack_buffer, unpack_buffer + trackrlesize,
                                        							               pack_buffer, pack_buffer + trackunpacked,
													       debug,debugfile)) {
                          										buffer = pack_buffer;	
													if(debug)
														fprintf(debugfile,"\tBuffer is set to pack buffer\n");
												}
											} else {
												if(debug)
													fprintf(debugfile,"Buffer is set to unpack buffer\n");
												buffer = unpack_buffer;
											}
										} else {
											fprintf(debugfile,"Cannot heavy(2) decompress track %u\n",i);
											return NULL;
										}
										break;
									case 6:
										if(debug)
											fprintf(debugfile,"\tDMS crunch mode: Heavy (2) compression\n");
										// Decrunch, the track
										if(!crunch_heavy(pack_buffer, pack_buffer + trackpacked + 16, 
											        unpack_buffer, unpack_buffer + trackrlesize,
											        trackcflag_compressed, 14,
												debug,debugfile, trackcflag_noclear)) {	
											// If RLE is set, also use RLE
											if(trackcflag_rle) { 
												if(!crunch_rle(unpack_buffer, unpack_buffer + trackrlesize,
                                        							               pack_buffer, pack_buffer + trackunpacked,
													       debug,debugfile)) {
                          										buffer = pack_buffer;	
													if(debug)
														fprintf(debugfile,"\tBuffer is set to pack buffer\n");
												}
											} else {
												if(debug)
													fprintf(debugfile,"Buffer is set to unpack buffer\n");
												buffer = unpack_buffer;
											}
										} else {
											fprintf(debugfile,"Cannot heavy(2) decompress track %u\n",i);
											return NULL;
										}
										break;
									case 7:
										if(debug)
											fprintf(debugfile,"\tDMS crunch mode: Heavy (3) compression\n");

										fprintf(stderr,"Heavy(3) compression not supported\n");
										return NULL;
										break;
									case 8:
										if(debug)
											fprintf(debugfile,"\tDMS crunch mode: Heavy (4) compression\n");
										fprintf(stderr,"Heavy(4) compression not supported\n");
										return NULL;
										break;
									case 9:
										if(debug)
											fprintf(debugfile,"\tDMS crunch mode: Heavy (5) compression\n");
										fprintf(stderr,"Heavy(5) compression not supported\n");
										return NULL;
										break;
									default:
										fprintf(debugfile,"Unknown crunch mode used in DMS\n");
										return NULL;
								}
								// Verify CRC of unpacked track vs unpack CRC
								if(mysimplecrc(buffer,trackunpacked) == trackunpackcrc) {
									// Unpack CRC is okay, track was unpacked succesfully
									if(debug)
										fprintf(debugfile,"\tUnpack CRC: %u Trackheader unpack CRC: %u\n",mysimplecrc(buffer,trackunpacked),trackunpackcrc);
									// Write track and buffer to outfile
									if(fwrite(buffer,1,trackunpacked,outfile) != trackunpacked) {
										fprintf(debugfile,"Cannot write to outputfile, exiting\n");
										return NULL;
									} else if(debug) {
										fprintf(debugfile,"\tSuccessfully wrote track %u, output file offset: %lx\n",i,ftell(outfile));
									}
								} else {
									fprintf(debugfile,"Unpack CRC does not match, header: %u, actual: %u, uncrunch or file error\n",trackunpackcrc,mysimplecrc(buffer,trackunpacked));
									return NULL;
								}
							} else {
								fprintf(debugfile,"Can't read packed bytes from DMS file or CRC error, file is probably corrupt\n");
								return NULL;
							}
						} else {
							fprintf(debugfile,"Track header CRC on track %u is invalid\n",i);
							if(debug)
								fprintf(debugfile,"Track header CRC: %u Calculated CRC: %u\n",((trackheader[18]<<8)+trackheader[19]),mycrc(trackheader, 18));
						}
					} else {
						fprintf(debugfile,"Corrupt track header %u from DMS file\n",i);
						return NULL;
					}
				} else {
					fprintf(debugfile,"Error reading track %u from DMS file\n",i);
					return NULL;
				}
			}
		} else {
			fprintf(stderr,"File is not a valid DMS file or header is corrupt\n");
			return NULL;
		}	
	} else {
		fprintf(stderr,"Inputfile is not valid\n");
		// Input file is not valid
		return NULL;
	}

	// If we reached here the file is uncompressed...
	// Close the input file
	fclose(infile);
	// Seek to 0 in the temporary file before returning
	rewind(outfile);
	// Free time struct
	free(dmstime);
	//  Return filepointer
	return outfile;
} // End function undmsfile


int main(int argc,char **argv) {
	// The Filepointer used to write the file to the disk
	FILE *f;
	// Temporary variables
	int i=0; int j=0; int n=0;
	// Type of file, 0 is unset (determined by filename, or exit if unsuccesful)
	int format=0;
	// A integer to store whether the file is an orphan
	int orphan = 0;
	// A integer to store whether the filename or parent path name is a legal string
	int invalidstring=0; int invalidparentstring=0;
	// A integer to store the header key
	uint32_t type, header_key;
	// To store the name of the file
	char *filename;
	// To store the extension of the file
	char *extension;
	/* Generate a array of strings to store the filepath and each directory name used in it */
	char **filepath;
	// We also need three arrays of uint32_t to store the days, minutes and ticks timestamps of the directories
	uint32_t days[MAX_PATH_DEPTH];
	uint32_t minutes[MAX_PATH_DEPTH];
	uint32_t ticks[MAX_PATH_DEPTH];
	// Array to keep track of orphaned sectors
	int *orphansector;
	// Array to keep track of orphan filenames
	char **orphanfilename;
	// Arrays to keep track of days, minutes and tick for orphans
	uint32_t orphandays[MAX_SECTORS];
	uint32_t orphanminutes[MAX_SECTORS];
	uint32_t orphanticks[MAX_SECTORS];
	// Variables to temporarily store orphandays, minutes and ticks
	uint32_t orphanday = 0;
	uint32_t orphanminute = 0;
	uint32_t orphantick = 0;
	// Time struct to store access and mod times
	struct utimbuf *utim;
	utim=malloc(sizeof(struct utimbuf));
	// A string to store the previous filepath, used for orphaned files
	char previousfilepath[MAX_FILENAME_LENGTH] = "";
	// Strings for orphan string splitting
	char *orphansplit = malloc(MAX_FILENAME_LENGTH +1 * sizeof(char *));
	/* A integer to store the root and orphan directories */
	int root = 0; int orphandir = 0;
	/* A integer to hold the start sector, defaults to the defined value FIRST_SECTOR */
	int startsector = FIRST_SECTOR;
	/* A integer to hold the last sector, defaults to the defined value SECTORS */
	unsigned int endsector = SECTORS;
	/* Variable to hold the debugging value */
	int debug = DEBUG;
	// int to read option value from getopt and a temp variable to read in the option index
        int optionflag; int index = 0;
	// File descriptor used either for the outfile, or set as stdout
        FILE *outfile = NULL;
	// Boolean to check whether system is big or little endian
	short bigendian = 1;
	// Allocate space for filename and extension
	filename = malloc(MAX_FILENAME_LENGTH + 1 * sizeof(char *));
	extension = malloc(MAX_FILENAME_LENGTH + 1 * sizeof(char *));
	/* Allocate space for MAX_PATH_DEPTH rows in filepath[X][] */
	filepath = malloc(MAX_PATH_DEPTH * sizeof(char *));
	if(filepath == NULL) {
		fprintf(stderr, "Out of memory\n");
		return 1;
	}
	// Alloc and init the filepath array
	for(i = 0; i < MAX_PATH_DEPTH ; i++) {
		/* Allocate space for MAX_AMIGADOS_FILENAME_LENGTH entries in filepath[X][Y] */
		filepath[i] = malloc(MAX_AMIGADOS_FILENAME_LENGTH * sizeof(char));
		if(filepath[i] == NULL) {
			fprintf(stderr, "Out of memory\n");
			return 1;
		}
		snprintf(filepath[i],MAX_AMIGADOS_FILENAME_LENGTH,"");
	}
	// Alloc and init the orphanfilename array as well as the orphan day, minutes, seconds array
	orphanfilename = malloc(MAX_SECTORS * sizeof(char *));
	for(i = 0; i<MAX_SECTORS; i++) {
		orphanfilename[i] = malloc(MAX_FILENAME_LENGTH * sizeof(char));
		if(orphanfilename[i] == NULL) {
			fprintf(stderr, "Out of memory\n");
			return 1;
		}
		snprintf(orphanfilename[i],MAX_FILENAME_LENGTH,"");
		orphandays[i] = 0;
		orphanminutes[i] = 0;
		orphanticks[i] = 0;
	}
	
	// Malloc and init Init the orphansector array, all sectors are not orphans to start with
	orphansector = malloc(MAX_SECTORS * sizeof(*orphansector));
	for(i = 0; i<MAX_SECTORS ; i++) {
		orphansector[i]=0;
	}

	// Check for endianness
	uint8_t swaptest[2] = {1,0};
	if ( *(short *)swaptest == 1) 
    		bigendian = 0;

	// Read the passed options if any (-d sets debug, -o sets an optional filename to pipe the output to)
        while((optionflag = getopt(argc, argv, "adzDo:s:e:")) != -1) 
		switch(optionflag) {
			// ADF format forced
			case 'a':
				format=1;
				break;
			// ADZ format forced
			case 'z':
				format=2;
				break;
			// DMS format forced
			case 'd':
				format=3;
				break;
                        // Debug flag is set to on
                        case 'D':
                                debug=1;
                                break;
                        // Output file flag is specified
                        case 'o':
                                outfile = fopen(optarg,"w");
                                // If output file didn't open, error occured, print error, exit
                                if(outfile == NULL) {
                                        fprintf(stderr,"Can't open output file %s for writing, error returned was: %s\n",optarg,strerror(errno));
                                        return 1;
                                } else {
                                        // Announce that we're writing the output to a file on stdout
                                        fprintf(stdout,"Writing output to %s\n",optarg);
                                }
                                break;
			// Start sector is specified
			case 's':
				i=strtoimax(optarg,NULL,10);
				// Not an integer or value over 3520 (last sector on a HD adf), print usage
				if(i>3520 || i <0 || i>endsector) {
					usage(argv[0]);
					return 2;
				// Otherwise set the start sector
				} else {
					startsector = i;
				}
				break;
			case 'e':		
				i=strtoimax(optarg,NULL,10);
				// Not an integer or value over 3520 (last sector on a HD adf), print usage
				if(i>3520 || i <0 || i<startsector) {
					usage(argv[0]);
					return 2;
				// Otherwise set the end sector
				} else {
					endsector = i;
				}
				break;
                        // Missing argument to o,s or e
                        case '?':
                                usage(argv[0]);
                                return 2;
                                break;
                }
	// Check if outfile is set, if not set outfile as stdout
	if(outfile == NULL)
		outfile=stdout;
	if(debug) {
		if(format==0)
			fprintf(outfile,"File format is not set!\n");
		else if(format==1) 
			fprintf(outfile,"File format is ADF\n");
		else if(format==2) 
			fprintf(outfile,"File format is ADZ\n");
		else if(format==3) 
			fprintf(outfile,"File format is DMS\n");
	}
	// The filename should be the last non-option argument given
	for (index = optind; index < argc; index++) {
		// Copy argument into the filename variable
		snprintf(filename,MAX_FILENAME_LENGTH-1,"%s",argv[index]);
		// Try opening the file for reading...
		f = fopen(filename,"r");
		if(f == NULL) {
			fprintf(stderr,"Can't open file %s for reading, error returned was: %s\n",filename,strerror(errno));
			return 1;
		} else {
			// Close the file for now
			fclose(f);			
			// If format not already set, determine format from file ending
			if(!format) {
				if(debug)
					fprintf(outfile,"Input filename is %s\n",argv[index]);
				// Get the extension of the file
				extension = strrchr(argv[index],'.');
				// No extension, assume ADF
				if(extension == NULL)  {
					fprintf(outfile,"No file extension, assuming ADF");
					format=1;
				} else {
					if(debug)
						fprintf(outfile,"Extension is %s\n",extension);
					// Lowercase the extension
					for (i = 0; extension[i] != '\0'; i++)
					    extension[i] = (char)tolower(extension[i]);
					if(debug)
						fprintf(outfile,"Extension lowercase is %s\n",extension);
					// Reset i
					i=0;
					// Is this an adf file?
					if(strncmp(".adf",extension,MAX_FILENAME_LENGTH) == 0) {
						format=1;
						fprintf(outfile,"Autodetected fileformat from extension is ADF\n");
					// or an adz file?
					} else if(strncmp(".adz",extension,MAX_FILENAME_LENGTH) == 0) {
						format=2;
						fprintf(outfile,"Autodetected fileformat from extension is ADZ (.adz)\n");
					// or an adf.gz file (same thing as an adz, but perhaps more *nix like)
					} else if(strncmp(".adf.gz",extension,MAX_FILENAME_LENGTH) == 0) {
						format=2;
						fprintf(outfile,"Autodetected fileformat from extension is ADZ (.adf.gz)\n");
					// or a zip file (this will also work since there's support in the adz decompression code to skip the zip header)
					} else if(strncmp(".zip",extension,MAX_FILENAME_LENGTH) == 0) {
						format=2;
						fprintf(outfile,"Autodetected fileformat from extension is ZIP (.zip)\n");
					// or a DMS file
					} else if(strncmp(".dms",extension,MAX_FILENAME_LENGTH) == 0) {
						format=3;
						fprintf(outfile,"Autodetected fileformat from extension is DMS (.dms)\n");
					// Otherwise have no idea what it is and we'll assume it's an adf
					} else {
						fprintf(outfile,"Can not figure out file format from file extension, assuming ADF");
						format=1;
					}
				}
			}
		}
	}
	// No file given, print usage instructions
	if(f == NULL) {
		usage(argv[0]);
		return 2;
	}
	// Define and allocate memory for the sectors
	union sector *sector = malloc((endsector+1)*sizeof(union sector));
	
	// Print start and end sector
	fprintf(outfile,"Startsector is %d\n",startsector);
	fprintf(outfile,"Endsector is %d\n",endsector);

	// Integer to hold total sectors read..
	int r=0;

	// How we fill up the sector array depends on the file format
	switch(format) {
		// Simplest case, simple uncompressed ADF file, we can simply read the sectors directly into an array...
		case 1:
			// Open the ADF file
			f=fopen(filename,"r");
			// If Null exit
			if(f == NULL) {
				fprintf(stderr,"Can't open file %s for reading, error returned was: %s\n",filename,strerror(errno));
				return 1;
			}
			break;
		case 2:
			#ifdef _HAVE_ZLIB
			// Uncompress the adf file and get a pointer to the open file
			f=uncompressfile(filename,debug,outfile);
			// If we get null back (TODO, would be good to do more error checking here)
			if(f == NULL) {
				fprintf(stderr,"Can't uncompress file %s\n",filename);
				return 1;
			}
			break;
			#else
			fprintf(outfile,"No zlib support, try changing _HAVE_ZLIB define and compiling with -lz\n");
			return 1;
			break;
			#endif
		case 3:
			// Decrunch DMS file and write to temporary raw file which we'll use to extract from
			if(debug)
				fprintf(outfile,"Decoding DMS file\n");
			f=undmsfile(filename,endsector,debug,outfile);
			if(f == NULL) {
				fprintf(stderr,"Fatal error, exiting\n");
				return 1;
			}
			break;
		default:
			// We've reached here and the format is not clear, print error and exit
			fprintf(outfile,"No format selected, don't know what to do, exiting\n");
			return 1;
			break;
	}
	// Read from the file (which is in the ADF sector format regardless of what the input file was) into the sector array
	r= fread(sector, sizeof(union sector), endsector, f);
	if(debug)
		fprintf(outfile,"Total sectors: %d\n\n", r);

	// Not enough sectors read?
	if(r < (endsector-startsector)) {
		fprintf(stderr,"Only managed to read %d sectors out of %d requested, cowardly refusing to continue\n",r,(endsector-startsector));
		return 1;
	}
	// Close the file
	fclose(f);


	// Loop through the sectors we are supposed to read and recover the data
	for (i=startsector; i<endsector; i++) {
		if(bigendian)
			type = sector[i].hdr.type;
		else
			type = ntohl(sector[i].hdr.type);
		if (type != T_HEADER && type != T_DATA && type != T_LIST)
			continue;
		if(debug) {
			if(bigendian) {
				fprintf(outfile,"%x: type       %x\n", i, sector[i].hdr.type);
				fprintf(outfile,"%x: header_key %x\n", i, sector[i].hdr.header_key);
				fprintf(outfile,"%x: seq_num    %x\n", i, sector[i].hdr.seq_num);
				fprintf(outfile,"%x: data_size  %x\n", i, sector[i].hdr.data_size);
				fprintf(outfile,"%x: next_data  %x\n", i, sector[i].hdr.next_data);
				fprintf(outfile,"%x: chksum     %x\n", i, sector[i].hdr.chksum);
			} else {
				fprintf(outfile,"%x: type       %x\n", i, ntohl(sector[i].hdr.type));
				fprintf(outfile,"%x: header_key %x\n", i, ntohl(sector[i].hdr.header_key));
				fprintf(outfile,"%x: seq_num    %x\n", i, ntohl(sector[i].hdr.seq_num));
				fprintf(outfile,"%x: data_size  %x\n", i, ntohl(sector[i].hdr.data_size));
				fprintf(outfile,"%x: next_data  %x\n", i, ntohl(sector[i].hdr.next_data));
				fprintf(outfile,"%x: chksum     %x\n", i, ntohl(sector[i].hdr.chksum));
			}
		}
		switch (type) {
			case T_HEADER:
				if(bigendian)
					header_key=sector[i].hdr.header_key;
				else
					header_key=ntohl(sector[i].hdr.header_key);
				if(debug) {
					fprintf(outfile,"%x:  filename  \"%s\"\n", i, sector[i].fh.filename);
					if(bigendian) 
						fprintf(outfile,"%x:  byte_size %d\n", i, sector[i].fh.byte_size);
					else 
						fprintf(outfile,"%x:  byte_size %d\n", i, ntohl(sector[i].fh.byte_size));
					
				}
				// Set n here as the header key of the current sector, we'll use it to find all parent headers so we can re-create the directory structure of the disk
				n=i;
				// J is used here to reverse the directory structure (from the disk root and downwards) 
				j=0;
				while(n >= 0) {
					// Add the current filename to the array of paths
					snprintf(filepath[j],MAX_AMIGADOS_FILENAME_LENGTH,"%s",sector[n].fh.filename);
					//  Also store days, minutes and ticks to recreate the correct date and time of the files
					if(bigendian) {
						days[j] = sector[n].fh.days;
						minutes[j] = sector[n].fh.mins;
						ticks[j] = sector[n].fh.ticks;
					} else { 
						days[j] = ntohl(sector[n].fh.days);
						minutes[j] = ntohl(sector[n].fh.mins);
						ticks[j] = ntohl(sector[n].fh.ticks);
					}
					
					if(debug) {
						fprintf(outfile,"N: %d I: %d J: %d Current object is %s\n",n,i,j,sector[n].fh.filename);
						if(bigendian)
							fprintf(outfile,"Parent object is %s\n",sector[sector[n].fh.parent].fh.filename);
						else
							fprintf(outfile,"Parent object is %s\n",sector[ntohl(sector[n].fh.parent)].fh.filename);
					}
					// If the current path is the root sector we don't need more iterations	
					if(n == 880) {
						if(debug)
							fprintf(outfile,"Parent is root block 880, stopping this loop\n");
						break;
					} else if(!sector[n].fh.parent) {
						// No parent object, leave the loop
						break;
					} else {
						// Increment j since the path length is increasing
						j++;
					}
					// Set n as the parent to recurse backwards
					if(bigendian)
						n = sector[n].fh.parent;
					else
						n = ntohl(sector[n].fh.parent);
				}
				// We should now have an array of all the filepaths belonging to this header, we'll now re-create all the directories in that path (we might do this multiple times for the top level directories but there is no harm in that)
				// Open the current directory so we can return to it later
				root = open(".",O_RDONLY);
				if(root == -1 ) {
					fprintf(stderr,"Can't write to root directory, exiting\n");
					return 1;
				}
				// Make a directory for orphaned files and directories, ignore if it already exists but stop on other errors
				if(mkdir("Orphaned",0777) > 0 && errno != EEXIST) 
					fprintf(stderr,"Can't create directory in current path, check permissions\n");

				chdir("Orphaned");
				// Save the orphandir so we cacn return laer
				orphandir = open(".",O_RDONLY);
				if(orphandir == -1 ) {
					fprintf(stderr,"Can't write to orphan directory, exiting\n");
					return 1;
				}
				
				// Change back to root directory
				fchdir(root);
				// Reverse loop through the filepath to change into the correct directory to output the file 
				// filepath for this file other than the root filepath
				for(n=j;n>0;n--) {
					// If we can't create the directory, and it's not because it already exists 
					// That will happen quite a bit since this code is executed once per block, 
					// and the same file (and directory) can be passed over hundreds of times. 
					// This isn't very efficient, but it does work.
					if(mkdir(filepath[n],0777) < 0 && errno != EEXIST) {
						fprintf(stderr,"Can't create directory %s, exiting\n",filepath[n]);
					} else {
						if(debug)
							fprintf(outfile,"Created directory %s\n",filepath[n]);
						// Set correct timestamp for directory
						utime(filepath[n],amigadaystoutimbuf(days[n],minutes[n],ticks[n],utim));
						// Change directory to the newly created directory
						if(chdir(filepath[n]) == -1) {
							// There is a chance, there's a filename that's the same as the name of the directory we're trying to create, in that case this is most likely
							// an orphaned directory (there can't be a directory and a file with the same name in the same directory so that means one of the entries is corrupted,
							// an orphaned file would never have been created under the restored directory structure, but rather at the root level, so there is a high probability
							// that this directory is an orphan, in which case we should place it at the root level, like an orphaned file
							// Return to the root directory
							if(fchdir(orphandir) == -1) {
								fprintf(stderr,"Can't return to previous working directory, exiting\n");
								return 1;
							} 
							// Try to create the directory at the root level instead of down the filesystem level
							if(mkdir(filepath[n],0777) < 0 && errno != EEXIST) {
								fprintf(stderr,"Can't create directory %s\n",filepath[n]);
							} else {
								if(debug)
									fprintf(outfile,"Created Orphaned directory %s\n",filepath[n]);
								// Set correct timestamp for directory
								utime(filepath[n],amigadaystoutimbuf(days[n],minutes[n],ticks[n],utim));
								// Change directory to the newly created directory
								if(chdir(filepath[n]) == -1) {
									fprintf(stderr,"Can't change to newly created directory, exiting\n");
									return 1;
								} else { 
									if(debug)
										fprintf(outfile,"Changing directory to %s\n",filepath[n]);
								}
							}
						}
					}
				}
				// Here we "touch" the file name this header belongs to, to create the file on the filesystem, in some cases there are no surviving data entries in which case
				// the file won't be created, since we want to know about every file that is there, even if none of it is recoverable, we'll create the file here
				// First we check whether the entry is 0 bytes, if it is, then it's very likely that it's a directory entry and not a file entry
				// If it really is a file entry, and it is 0 bytes, and it is orphaned, then we're out of luck, if the file is okay (even though it's 0 bytes), it will be created later 
				// This should hopefully make the work of puzzling together the structure relatively easy
				if(((ntohl(sector[i].fh.byte_size) == 0) && !bigendian) || (bigendian && (sector[i].fh.byte_size == 0))) {
					// Make a directory for this entry instead
					if(mkdir(sector[i].fh.filename,0777) < 0 && errno != EEXIST) 
						fprintf(stderr,"Can't create directory %s\n",sector[i].fh.filename);
					else {
						if(bigendian)
							// Set correct timestamp for directory
							utime(sector[i].fh.filename,amigadaystoutimbuf(sector[i].fh.days,sector[i].fh.mins,sector[i].fh.ticks,utim));
						else
							utime(sector[i].fh.filename,amigadaystoutimbuf(ntohl(sector[i].fh.days),ntohl(sector[i].fh.mins),ntohl(sector[i].fh.ticks),utim));
					}
				} else {
					// Make a file for this entry (empty)
					f = fopen(sector[i].fh.filename, "r+"); /* try to open existing file, if it already exists we won't need to creqte it */
					if (!f) 
						f = fopen(sector[i].fh.filename, "w"); /* doesn't exist, so create */
					// Close the file again
					fclose(f);
					// Modify the timestamp
					if(bigendian)
						// Set correct timestamp for directory
						utime(sector[i].fh.filename,amigadaystoutimbuf(sector[i].fh.days,sector[i].fh.mins,sector[i].fh.ticks,utim));
					else
						utime(sector[i].fh.filename,amigadaystoutimbuf(ntohl(sector[i].fh.days),ntohl(sector[i].fh.mins),ntohl(sector[i].fh.ticks),utim));
				}
				// Return to the previous working directory
				if(fchdir(root) == -1) {
					fprintf(stderr,"Can't return to previous working directory, exiting\n");
					return 1;
				} 
				// Close the previously opened working directories
				close(root);
				close(orphandir);
				// Leave this sector
				break;
			case T_DATA:
				if( (!bigendian && (ntohl(sector[i].hdr.header_key % 32 != 0))) || (bigendian && (sector[i].hdr.header_key % 32 != 0)))
					continue;
				if(bigendian)
					header_key = sector[i].hdr.header_key;
				else
					header_key = ntohl(sector[i].hdr.header_key);
				if (header_key<SECTORS && ( (!bigendian && (ntohl(sector[header_key].hdr.type) == T_HEADER)) || (bigendian && (sector[header_key].hdr.type == T_HEADER)))) {
					if(debug) {
						fprintf(outfile,"%x:  filename  \"%s\"\n", i, sector[header_key].fh.filename);
						if(bigendian)
							fprintf(outfile,"%x:  byte_size %d\n", i, sector[header_key].fh.byte_size);
						else
							fprintf(outfile,"%x:  byte_size %d\n", i, ntohl(sector[header_key].fh.byte_size));
					}
					snprintf(filename,MAX_AMIGADOS_FILENAME_LENGTH,"%s",sector[header_key].fh.filename);
					orphan = 0;
				} else {
					if(debug) {
						fprintf(outfile,"Orphaned file found at header key %d previous orphansector value: %d\n",header_key,orphansector[header_key]);
						fprintf(outfile,"%x:  filename  \"%s\"\n", i, sector[header_key].fh.filename);
						if(bigendian)
							fprintf(outfile,"%x:  byte_size %d\n", i, sector[header_key].fh.byte_size);
						else
							fprintf(outfile,"%x:  byte_size %d\n", i, ntohl(sector[header_key].fh.byte_size));
					}
					// Defaults for days, minutes, ticks if nothing else is readable, date will be set as 1978-01-01 
					orphanday = 0;
					orphanminute = 0;
					orphantick = 0; 
					// Check whether we've come across this orphaned file before and given it a name, if so we'll keep that name so that we don't write to many different files
					if(orphansector[header_key]) {
						// Fetch the filename from the orphanfilename array
						snprintf(filename,MAX_FILENAME_LENGTH,"%s",orphanfilename[header_key]);
						// Fetch days, minutes, ticks from orphandays/minutes/ticks arrays
						orphanday=orphandays[header_key];
						orphanminute=orphanminutes[header_key];
						orphantick=orphanticks[header_key];
						// Mark this file as an orphan so it gets placed in the Orphaned directory
						orphan=1;
						if(debug) 
							fprintf(outfile,"This orphan already has a filename selected, it is %s\n",filename);
					} else {
						// This is the first time we've come across this orphaned file
						// If this file is an orphan, the filename might not be a legal string since it might be corrupted so we setup some variables to check whether that is the cause to avoid crashes
						invalidstring=0; invalidparentstring=0;
						// Check whether there are invalid characters in the filename string and mark it as invalid if there are
						for(n=0;n<sizeof(sector[header_key].fh.filename)/sizeof(sector[header_key].fh.filename[0]);n++) {
								if((((unsigned char)sector[header_key].fh.filename[n] <32) && (unsigned char)sector[header_key].fh.filename[n] > 0) || (unsigned char)sector[header_key].fh.filename[n] ==47 || ((unsigned char)sector[header_key].fh.filename[n] >127 && (unsigned char)sector[header_key].fh.filename[n] < 161))
								invalidstring=1;
						}
						// If the filename is longer than the max_filename_length or it's empty, then it's invalid
						if(strlen(sector[header_key].fh.filename) > MAX_AMIGADOS_FILENAME_LENGTH || strlen(sector[header_key].fh.filename) ==  0)
							invalidstring=1;
						// Check whether there is a filepath still stored from a previous run if so we'll use that instead of the filename
						if(strlen(previousfilepath) > 0) {
							invalidstring=1;
						}
						// Extra boundary check here to verify that we have a valid parent index, it's possible that the parent number is corrupt
						if(bigendian) {
					
							if(sector[header_key].fh.parent && (sector[header_key].fh.parent % 32) == 0 && sector[header_key].fh.parent < endsector) {
								// Check whether there are invalid characters in the parent filename string and mark it as invalid if there are
								for(n=0;n<sizeof(sector[sector[header_key].fh.parent].fh.filename)/sizeof(sector[sector[header_key].fh.parent].fh.filename[0]);n++) {
									if((((unsigned char)sector[sector[header_key].fh.parent].fh.filename[n] <32) && (unsigned char)sector[sector[header_key].fh.parent].fh.filename[n] > 0) || (unsigned char)sector[sector[header_key].fh.parent].fh.filename[n] ==47 || ((unsigned char)sector[sector[header_key].fh.parent].fh.filename[n] >127 && (unsigned char)sector[sector[header_key].fh.parent].fh.filename[n] < 161))
										invalidparentstring=1;
								}
								// If the parent filename is longer than the max_filename_length or it's empty, then it's invalid
								if(strlen(sector[sector[header_key].fh.parent].fh.filename) > MAX_AMIGADOS_FILENAME_LENGTH || strlen(sector[sector[header_key].fh.parent].fh.filename) == 0)
									invalidparentstring=1;
							// If there is no parent then we can't use that to construct the string
							} else {
								invalidparentstring=1;
							}
						} else {
							if(sector[header_key].fh.parent && (sector[header_key].fh.parent % 32) == 0 && sector[header_key].fh.parent < endsector) {
								// Check whether there are invalid characters in the parent filename string and mark it as invalid if there are
								for(n=0;n<sizeof(sector[sector[header_key].fh.parent].fh.filename)/sizeof(sector[sector[header_key].fh.parent].fh.filename[0]);n++) {
									if((((unsigned char)sector[sector[header_key].fh.parent].fh.filename[n] <32) && (unsigned char)sector[sector[header_key].fh.parent].fh.filename[n] > 0) || (unsigned char)sector[sector[header_key].fh.parent].fh.filename[n] ==47 || ((unsigned char)sector[sector[header_key].fh.parent].fh.filename[n] >127 && (unsigned char)sector[sector[header_key].fh.parent].fh.filename[n] < 161))
										invalidparentstring=1;
								}
								// If the parent filename is longer than the max_filename_length or it's empty, then it's invalid
								if(strlen(sector[sector[header_key].fh.parent].fh.filename) > MAX_AMIGADOS_FILENAME_LENGTH || strlen(sector[sector[header_key].fh.parent].fh.filename) == 0)
									invalidparentstring=1;
							// If there is no parent then we can't use that to construct the string
							} else {
								invalidparentstring=1;
							}
						}
						
						// If the filename string is good and the parent string is good, we'll use both (only one parent used here)
						if(!invalidstring && !invalidparentstring) {
							// Store days, minutes, ticks from file since it's available
							if(bigendian) {
								snprintf(filename, MAX_FILENAME_LENGTH,"Orphan-%d-%s-%s",header_key,sector[sector[header_key].fh.parent].fh.filename,sector[header_key].fh.filename);
								orphandays[header_key] = sector[header_key].fh.days;
								orphanminutes[header_key] = sector[header_key].fh.mins;
								orphanticks[header_key] = sector[header_key].fh.ticks;
								orphanday = sector[header_key].fh.days;
								orphanminute = sector[header_key].fh.mins;
								orphantick = sector[header_key].fh.ticks;
								if(sector[header_key].fh.parent == 880) {
									fprintf(outfile,"Parent er 880\n");
								}
							} else {
								snprintf(filename, MAX_FILENAME_LENGTH,"Orphan-%d-%s-%s",header_key,sector[ntohl(sector[header_key].fh.parent)].fh.filename,sector[header_key].fh.filename);
								orphandays[header_key] = ntohl(sector[header_key].fh.days);
								orphanminutes[header_key] = ntohl(sector[header_key].fh.mins);
								orphanticks[header_key] = ntohl(sector[header_key].fh.ticks);
								orphanday = ntohl(sector[header_key].fh.days);
								orphanminute = ntohl(sector[header_key].fh.mins);
								orphantick = ntohl(sector[header_key].fh.ticks);
								if(ntohl(sector[header_key].fh.parent) == 880) {
									fprintf(outfile,"Parent er 880\n");
								}
							}
						// Otherwise, if the filename string is good, but the parent string is not, we'll use that
						} else if(!invalidstring) {
							snprintf(filename, MAX_FILENAME_LENGTH,"Orphan-%d-%s",header_key,sector[header_key].fh.filename);
							// Store days, minutes, ticks from file since it's available
							if(bigendian) {
								orphandays[header_key] = sector[header_key].fh.days;
								orphanminutes[header_key] = sector[header_key].fh.mins;
								orphanticks[header_key] = sector[header_key].fh.ticks;
								orphanday = sector[header_key].fh.days;
								orphanminute = sector[header_key].fh.mins;
								orphantick = sector[header_key].fh.ticks;
							} else {
								orphandays[header_key] = ntohl(sector[header_key].fh.days);
								orphanminutes[header_key] = ntohl(sector[header_key].fh.mins);
								orphanticks[header_key] = ntohl(sector[header_key].fh.ticks);
								orphanday = ntohl(sector[header_key].fh.days);
								orphanminute = ntohl(sector[header_key].fh.mins);
								orphantick = ntohl(sector[header_key].fh.ticks);
							}
						// Otherwise, if the parent filepath is good, we'll use that
						} else if(!invalidparentstring) {
							// Store days, minutes, ticks from parent since that's all we have
							if(bigendian) {
								snprintf(filename, MAX_FILENAME_LENGTH,"Orphan-%d-%s",header_key,sector[sector[header_key].fh.parent].fh.filename);
								orphandays[header_key] = sector[ntohl(sector[header_key].fh.parent)].fh.days;
								orphanminutes[header_key] = sector[ntohl(sector[header_key].fh.parent)].fh.mins;
								orphanticks[header_key] = sector[ntohl(sector[header_key].fh.parent)].fh.ticks;
								orphanday = sector[ntohl(sector[header_key].fh.parent)].fh.days;
								orphanminute = sector[ntohl(sector[header_key].fh.parent)].fh.mins;
								orphantick = sector[ntohl(sector[header_key].fh.parent)].fh.ticks;
							} else {
								snprintf(filename, MAX_FILENAME_LENGTH,"Orphan-%d-%s",header_key,sector[ntohl(sector[header_key].fh.parent)].fh.filename);
								orphandays[header_key] = ntohl(sector[ntohl(sector[header_key].fh.parent)].fh.days);
								orphanminutes[header_key] = ntohl(sector[ntohl(sector[header_key].fh.parent)].fh.mins);
								orphanticks[header_key] = ntohl(sector[ntohl(sector[header_key].fh.parent)].fh.ticks);
								orphanday = ntohl(sector[ntohl(sector[header_key].fh.parent)].fh.days);
								orphanminute = ntohl(sector[ntohl(sector[header_key].fh.parent)].fh.mins);
								orphantick = ntohl(sector[ntohl(sector[header_key].fh.parent)].fh.ticks);
							}
						// Otherwise, if the previous filepath is good, we'll use that instead of the parent
						} else if(strlen(previousfilepath) > 0) {
							snprintf(filename, MAX_FILENAME_LENGTH,"Orphan-%s-%s",previousfilepath,previousfilepath);
						// Else we'll have to settle for just the header key value to identify the file since both the filename and parent filename strings are corrupt
						} else {
							snprintf(filename, MAX_FILENAME_LENGTH,"Orphan-%d-%d",header_key,header_key);
						}
						// Set the orphan flag to 1
						orphan = 1;
						// Mark this sector as an orphan so if we come across it again we'll use the same filename and dates
						orphansector[header_key] = 1;
						// And set the orphan filename to our current filename
						snprintf(orphanfilename[header_key],MAX_FILENAME_LENGTH,"%s",filename);
						if(debug) {
							if(!invalidstring && !invalidparentstring) {	
								if(bigendian)
									fprintf(outfile, "Filename:%s: Parent Filename: %s Orphan Filename: %s\n",sector[header_key].fh.filename,sector[sector[header_key].fh.parent].fh.filename,filename);
								else
									fprintf(outfile, "Filename:%s: Parent Filename: %s Orphan Filename: %s\n",sector[header_key].fh.filename,sector[ntohl(sector[header_key].fh.parent)].fh.filename,filename);
							} else if(!invalidstring) {
								fprintf(outfile, "Filename:%s: Orphan Filename: %s\n",sector[header_key].fh.filename,filename);
							} else if(!invalidparentstring) {
								if(bigendian)
									fprintf(outfile, "Parent Filename: %s Orphan Filename: %s\n",sector[sector[header_key].fh.parent].fh.filename,filename);
								else
									fprintf(outfile, "Parent Filename: %s Orphan Filename: %s\n",sector[ntohl(sector[header_key].fh.parent)].fh.filename,filename);
							} else if(strlen(previousfilepath) > 0) {
								fprintf(outfile, "Previous filepath: %s Orphan Filename: %s\n",previousfilepath,filename);
							} else {
								fprintf(outfile, "Orphan Filename: %s\n",filename);
							}
						}
					}
				}

				// Find the file path and put it into the filepath array
				j = 0; n=header_key;
				// If this is a regular file (has a regular filename, and a directory structure) as well as a valid parent find the path
				while( n != 0 && header_key<SECTORS && ( (!bigendian && (ntohl(sector[header_key].hdr.type) == T_HEADER)) || (bigendian && (sector[header_key].hdr.type == T_HEADER))) && (sector[n].fh.parent % 32) == 0) {
					if(sector[n].fh.parent && n != 880)  {
						//  Also store days, minutes and ticks to recreate the correct date and time of the files
						if(bigendian) {
							// Get the path entry name into the filepath array
							snprintf(filepath[j],MAX_AMIGADOS_FILENAME_LENGTH,"%s",sector[sector[n].fh.parent].fh.filename);
							days[j] = sector[n].fh.days;
							minutes[j] = sector[n].fh.mins;
							ticks[j] = sector[n].fh.ticks;
							// Set n as the parent to recurse backwards
							n = sector[n].fh.parent;
						} else { 
							// Get the path entry name into the filepath array
							snprintf(filepath[j],MAX_AMIGADOS_FILENAME_LENGTH,"%s",sector[ntohl(sector[n].fh.parent)].fh.filename);
							days[j] = ntohl(sector[n].fh.days);
							minutes[j] = ntohl(sector[n].fh.mins);
							ticks[j] = ntohl(sector[n].fh.ticks);
							// Set n as the parent to recurse backwards
							n = ntohl(sector[n].fh.parent);
						}
						if(debug)
							fprintf(outfile,"File belongs to Directory tree %d, found path %s\n",j,filepath[j]);
						// If the parent is the root sector we don't need more iterations	
						if(n == 880) {
							if(debug)
								fprintf(outfile,"Parent is root block 880, stopping this loop\n");
							break;
						} else {
							// Increment loop count by 1
							j++;
						}
					} else {
					 	break;
					}
				}
				// We should now have an array of all the filepaths belonging to this header, we'll now re-create all the directories in that path (we might do this multiple times for the top level directories but there is no harm in that)
				// Open the current directory so we can return to it later
				root = open(".",O_RDONLY);
				if(root == -1 ) {
					fprintf(stderr,"Can't write to root directory, exiting\n");
					return 1;
				}

				// Make a directory for orphaned files and directories, ignore if it already exists but stop on other errors
				if(mkdir("Orphaned",0777) > 0 && errno != EEXIST)
					fprintf(stderr,"Can't create directory in current path, check permissions\n");

				chdir("Orphaned");
				orphandir = open(".",O_RDONLY);
	
				if(orphandir == -1) {
					fprintf(stderr,"Can't write to orphan directory, exiting\n");
					return 1;
				}
				// Change back to root directory
				fchdir(root);

				// Struct we'll use for filestats
				struct stat st;
				// Integer to get error code from stat
				int staterr = 0;
				// Reverse loop through the filepath to change into the correct directory to output the file 
				// filepath for this file other than the root filepath, this will re-create directory structures of orphaned directories as well
				for(n=j;n>=0;n--) {
					// Recreate directory tree
					// That will happen quite a bit since this code is executed once per block, 
					// and the same file (and directory) can be passed over hundreds of times. 
					// If the filepath is NULL the parent directory is lost in the filesystem and we'll set it as orphaned
					if(filepath[n] != NULL) 
						staterr=stat(filepath[n],&st);
					else {
						// NULL filepaths get put into a directory in the root called Orphaned
						snprintf(filepath[n],9,"Orphaned");
						// Stat the orphaned directory
						staterr=stat(filepath[n],&st);
					}
					// File/Directory does not exist, let's create it
					if(staterr != -1 && S_ISDIR(st.st_mode) && !orphan) {
						if(debug)
							fprintf(outfile,"Directory %s already exists, not creating\n",filepath[n]);
						// Set correct timestamp for directory
						utime(filepath[n],amigadaystoutimbuf(days[n],minutes[n],ticks[n],utim));
						if(chdir(filepath[n])) 
							fprintf(outfile,"Can't CD to directory %s\n",filepath[n]);
						else
							if(debug)
								fprintf(outfile,"Changing directory to %s\n",filepath[n]);
					} else if(staterr != -1 && !S_ISDIR(st.st_mode) && st.st_size != 0 && !orphan) {
						if(debug)		
							fprintf(outfile,"File with same name as directory (%s) already exists and is not empty, cowardly refusing to delete it\n",filepath[n]);
					} else if(staterr != -1 && !S_ISDIR(st.st_mode) && st.st_size == 0 && !orphan) {
						if(remove(filepath[n])) {
							fprintf(outfile,"Cannot delete file %s, placing directory in orphanpath instead\n",filepath[n]);
							// Return to the previous working directory
							if(fchdir(root) == -1) {
								fprintf(stderr,"Can't return to previous working directory, exiting\n");
								return 1;
							}
						} else {
							if(debug)
								fprintf(outfile,"Deleted empty file %s to make room for directory of the same name\n",filepath[n]);
						}
						// Create directory
						if(mkdir(filepath[n],0777) < 0) {
							fprintf(outfile,"Can't create directory %s\n",filepath[n]);
						} else {
							if(debug)
								fprintf(outfile,"Created directory %s in place of file\n",filepath[n]);
							// Set correct timestamp for directory
							utime(filepath[n],amigadaystoutimbuf(days[n],minutes[n],ticks[n],utim));
							if(chdir(filepath[n])) {;
								fprintf(stderr,"Can't change to newly created directory, exiting\n");
								return 1;
							} else { 
								if(debug)
									fprintf(outfile,"Changing directory to %s\n",filepath[n]);
							}
						}
								
					} else if(staterr == -1 && errno == ENOENT && !orphan) {
						// Create directories for files
						if(mkdir(filepath[n],0777) < 0) {
							fprintf(outfile,"Can't create directory %s\n",filepath[n]);
						} else {
							if(debug)
								fprintf(outfile,"Created directory %s\n",filepath[n]);
							// Set correct timestamp for directory
							utime(filepath[n],amigadaystoutimbuf(days[n],minutes[n],ticks[n],utim));
							// Change directory to the newly created directory
							if(chdir(filepath[n]) == -1) {
								fprintf(stderr,"Can't change to newly created directory, exiting\n");
								return 1;
							} else { 
								if(debug)
									fprintf(outfile,"Changing directory to %s\n",filepath[n]);
							}
						}
					}
					
				}
				// Store the current filepath, this can help us realise where orphaned files belong
				if(filepath[0] && strlen(filepath[0]) > 0)
					snprintf(previousfilepath,MAX_AMIGADOS_FILENAME_LENGTH,"%s",filepath[0]);
				// If this is an orphan file we'll need to treat it a little differently, we can't fully recreate the path but we most likely have at least the parent directory
				if(orphan) {
					// Split the file 
					// Temporary variable for strsep
					char *orphanfilenamecopy = malloc(MAX_FILENAME_LENGTH*sizeof(char *));
					char *temp;
					// Copy the filename into the copy
					snprintf(orphanfilenamecopy,MAX_FILENAME_LENGTH,"%s",filename);
					// Variable to hold number of splits in the string
					int splits=0;
					// Store orphansplit before going into function
					temp=orphansplit;
					// Use strsep to split the string by -  (Orphan, Sector, Parent if there was a parent)
					while((orphansplit = strsep(&orphanfilenamecopy,"-")) != NULL && splits <2) {
						splits++;
					}
					// If there are 3 parts to the filename, there is a path component which either the last directory traversed or the parent directory of the orphaned file
					// then we can place the file in that path, making it easier to sort out which orphans belong in which directory (and therefore, what could be in the file)
					if(splits == 2) {
						// Return to the orphaned directory first
						if(fchdir(orphandir) == -1) {
							fprintf(stderr,"Can't return to previous working directory, exiting\n");
							return 1;
						}
						// Create a directory based on the path component of the oprhan
						if(mkdir(orphansplit,0777) < 0 && errno != EEXIST) {
							fprintf(stderr,"Can't create directory %s\n",orphansplit);
						} else {
							if(debug)
								fprintf(outfile,"Created orphan directory %s\n",orphansplit);
							// Set modification time
							//utime(orphansplit,amigadaystoutimbuf(orphanday,orphanminute,orphantick,utim));
							// Change directory to the newly created directory
							if(chdir(orphansplit) == -1) {
								fprintf(stderr,"Can't change to newly created directory,placing orphan in the root directory");
							} else 
								if(debug)
									fprintf(outfile,"Changing directory to orphaned %s\n",orphansplit);
						}
					} else {
						// Return to the previous working directory and place the file in the root
						if(fchdir(orphandir) == -1) {
							fprintf(stderr,"Can't return to previous working directory, exiting\n");
							return 1;
						}
					}
					// Restore orphansplit
					orphansplit=temp;
					// Free memory used by orphanfilenamecopy
					free(orphanfilenamecopy);
				}

					
					
				// Open the file for appending (in the current directory with the path intact)
				f = fopen(filename, "r+"); /* try to open existing file */
				if (!f) 
					f = fopen(filename, "w"); /* doesn't exist, so create */
				if(!f) {
					// File could already exist under the same name or even as a directory, try append the sector header to the filename and try again
					if(bigendian)
						snprintf(filename,MAX_AMIGADOS_FILENAME_LENGTH,"%s-%d",filename,sector[i].hdr.header_key);
					else
						snprintf(filename,MAX_AMIGADOS_FILENAME_LENGTH,"%s-%d",filename,ntohl(sector[i].hdr.header_key));
					f = fopen(filename, "w"); /* doesn't exist, so create */
					if(!f) 
						fprintf(stderr,"Can't create file, this is probably fatal!\n");
				}

				// Dumper function that dumps out ascii text, isn't really useful, only active if you enable debug
				if(debug == 8) {
					char outascii[21];
					char outhex[61];
					uint8_t c;
					for (j=0; j<sizeof(sector[i].dh.data)/sizeof(sector[i].dh.data[0]); j++) {
						// Every 20 characters print out the ascii and hex dump as well as a newline
						if(j%20 == 0u && j!= 0)  {
							fprintf(outfile,"%s %s\n",outascii,outhex);
							snprintf(outascii,sizeof(outascii),"");
							snprintf(outhex,sizeof(outhex),"");
						}
						// Collect the ascii and hex dumps into two seperate concatenated strings
						c=sector[i].dh.data[j];
						if (c>=32 && c<127) {
							snprintf(outascii,sizeof(outascii),"%s%c",outascii,c);
							snprintf(outhex,sizeof(outhex),"%s %02x",outhex,c);
						} else {
							snprintf(outascii,sizeof(outascii),"%s%c",outascii,'.');
							snprintf(outhex,sizeof(outhex),"%s %02x",outhex,c);
						}
					}
					fprintf(outfile,"debug done\n");
					// Print out the remaining characters
					fprintf(outfile,"%-20s %s\n",outascii,outhex);
					// End with a final newline
					fprintf(outfile,"\n");
				}
				// Write the content to the file
				if(debug) {
					if(bigendian)	
						fprintf(outfile,"Seek seq_num %02x : DATABYTES: %lu SEEKSET: %d \n",sector[i].hdr.seq_num,DATABYTES,SEEK_SET);
					else
						fprintf(outfile,"Seek seq_num %02x : DATABYTES: %lu SEEKSET: %d \n",ntohl(sector[i].hdr.seq_num),DATABYTES,SEEK_SET);
				}
				if(bigendian)
					fseek(f, (sector[i].hdr.seq_num-1)*DATABYTES, SEEK_SET);
				else
					fseek(f, (ntohl(sector[i].hdr.seq_num)-1)*DATABYTES, SEEK_SET);
				if(debug) {
					if(bigendian)
						fprintf(outfile,"seek to %ld\n",  (sector[i].hdr.seq_num-1)*DATABYTES);
					else
						fprintf(outfile,"seek to %ld\n",  (ntohl(sector[i].hdr.seq_num)-1)*DATABYTES);
				}
				if(bigendian)
					fwrite(sector[i].dh.data, sector[i].hdr.data_size, 1, f);
				else 
					fwrite(sector[i].dh.data, ntohl(sector[i].hdr.data_size), 1, f);
				// Close file
				fclose(f);
				// Set modification time based on the days/minutes/ticks timestamp of the original file
				if(bigendian) 
					utime(filename,amigadaystoutimbuf(sector[header_key].fh.days,sector[header_key].fh.mins,sector[header_key].fh.ticks,utim));
				else
					//All the stamps are in big-endian so need to be converted..
					utime(filename,amigadaystoutimbuf(ntohl(sector[header_key].fh.days),ntohl(sector[header_key].fh.mins),ntohl(sector[header_key].fh.ticks),utim));
				// Return to the previous working directory
				if(fchdir(root) == -1) {
					fprintf(stderr,"Can't return to previous working directory, exiting\n");
					return 1;
				}
				// Close the previously opened working directories
				close(root);
				close(orphandir);
		}
		if(debug)
			fprintf(outfile,"\n");
	}

	// Free the space allocated for the filepath array, in reverse order to the malloc obviously
	for(i=0; i< MAX_AMIGADOS_FILENAME_LENGTH; i++) {
		free(filepath[i]);
	}
	// Free the filepath array itself
	free(filepath);

	// Free the space allocated for the orphanfilename array, in reverse order to the malloc
	for(i=0; i<3520; i++) {
		free(orphanfilename[i]);
	}
	free(orphanfilename);

	// Free the space used by the orphansector array
	free(orphansector);

	// Free the space used by the sector array
	free(sector);

	// Free the time struct
	free(utim);

	// Free the orphansplit
	free(orphansplit);

	// Successful run	
	return 0;
	fprintf(stderr,"ermahgerdus\n");
}
