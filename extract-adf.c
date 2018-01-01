/*
 * extract-adf.c 
 *
 * (C)2008 Michael Steil, http://www.pagetable.com/
 * Do whatever you want with it, but please give credit.
 *
 * 2011 Sigurbjorn B. Larusson, http://www.dot1q.org/
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
 * 2017 Sigurbjorn B. Larusson, http://www.dot1q.org/
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
 * TODO:
 * The source code could do with a cleanup and even a rewrite, I'll leave that for the next time I have time to work on it
 */

#include <libc.h>
#include <sys/_endian.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <inttypes.h>

// These are defaults
#define SECTORS 1760
#define FIRST_SECTOR 0 

// These are hard coded, no way to set them
#define	T_HEADER 2
#define	T_DATA 8
#define	T_LIST 16

// Set this to 1 (or anything higher) if you want debugging information printed out, this can now be set at the command line so this option is largely useless
#define DEBUG 0

// Maximum AmigaDOS filename length
#define MAX_AMIGADOS_FILENAME_LENGTH 32
// Maximum filename length generated by the program, since we make up filenames for orphaned files that contain Orphaned-Sector-Filename-ParentFilename we need 8+1+4+1+32+1+32 characters to store those or 79 characters, we'll use 80
#define MAX_FILENAME_LENGTH 80
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
	uint32_t days;
	uint32_t mins;
	uint32_t ticks;
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


#define DATABYTES (sizeof(union sector)-sizeof(struct blkhdr))

// Print usage information
void usage(char *programname) {
	fprintf(stderr,"Extract-ADF 3.0 Originally (C)2008 Michael Steil with many further additions by Sigurbjorn B. Larusson\n");
        fprintf(stderr,"\nUsage: %s [-d] [-s <startsector>] [-e <endsector>] [-o <outputfilename>] <adffilename>\n",programname);
	fprintf(stderr,"\n\t-d will activate debugging output which will print very detailed information about everything that is going on");
	fprintf(stderr,"\n\t-s along with an integer argument from 0 to 1760 (DD) or 3520 (HD), will set the starting sector of the extraction process");
	fprintf(stderr,"\n\t-e along with an integer argument from 0 to 1760 (DD) or 3520 (HD), will set the end sector of the extraction process");
	fprintf(stderr,"\n\t-o along with an outputfilename will redirect output (including debugging output) to a file instead of to the screen");
	fprintf(stderr,"\n\tFinally the last argument is the ADF filename to process, note that this program does not support compressed ADF files");
	fprintf(stderr,"\n\nThe defaults for start and end sector are 0 and 1760 respectively, this tool was originally"),
	fprintf(stderr,"\ncreated to salvage lost data from kickstart disks (which contain the kickstart on sectors 0..512)");
	fprintf(stderr,"\nin order to skip the sectors on kickstart disks which might contain non OFS data, set the start sector to 513\n");
	fprintf(stderr,"\nTo use this tool on a HD floppy, the end sector needs to be 3520\n");
	fprintf(stderr,"\nIf you get a Bus error it means that you specificed a non-existing end sector\n");
	fprintf(stderr,"\nHappy hunting!\n");
}

int main(int argc,char **argv) {
	// The Filepointer used to write the file to the disk
	FILE *f;
	// Temporary variables
	int i=0; int j=0; int n=0;
	// A integer to store whether the file is an orphan
	int orphan = 0;
	// A integer to store whether the filename or parent path name is a legal string
	int invalidstring=0; int invalidparentstring=0;
	// A integer to store the header key
	uint32_t type, header_key;
	// To store the name of the file
	char filename[MAX_FILENAME_LENGTH];
	/* Generate a array of strings to store the filepath and each directory name used in it */
	char **filepath;
	// Array to keep track of orphaned sectors
	int *orphansector;
	// Array to keep track of orphan filenames
	char **orphanfilename;
	// A string to store the previous filepath, used for orphaned files
	char previousfilepath[MAX_FILENAME_LENGTH] = "";
	/* A integer to store the current working directory */
	int cwd;
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
	// Alloc and init the orphanfilename array
	orphanfilename = malloc(3520 * sizeof(char *));
	for(i = 0; i<3520; i++) {
		orphanfilename[i] = malloc(MAX_FILENAME_LENGTH * sizeof(char));
		if(orphanfilename[i] == NULL) {
			fprintf(stderr, "Out of memory\n");
			return 1;
		}
		snprintf(orphanfilename[i],MAX_FILENAME_LENGTH,"");
	}
	
	// Malloc and init Init the orphansector array, all sectors are not orphans to start with
	orphansector = malloc(3520 * sizeof(*orphansector));
	for(i = 0; i<3520 ; i++) {
		orphansector[i]=0;
	}

	// Read the passed options if any (-d sets debug, -o sets an optional filename to pipe the output to)
        while((optionflag = getopt(argc, argv, "do:s:e:")) != -1) 
		switch(optionflag) {
                        // Debug flag is set to on
                        case 'd':
                                debug=1;
                                break;
                        // Output file flag is specified
                        case 'o':
                                outfile = fopen(optarg,"w");
                                // If output file ddidn't open, error occured, print error, exit
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
	// The filename should be the last non-option argument given
	for (index = optind; index < argc; index++) {
		f = fopen(argv[index],"r");
		if(f == NULL) {
			fprintf(stderr,"Can't open file %s for reading, error returned was: %s\n",argv[index],strerror(errno));
			return 1;
		}
	}
	// No file given, print usage instructions
	if(f == NULL) {
		usage(argv[0]);
		return 2;
	}
        if(f == NULL) 
		fprintf(stdout,"Holy shit\n");
	// Check if outfile is set, if not set outfile as stdout
	if(outfile == NULL)
		outfile=stdout;
	// Define and allocate memory for the sectors
	union sector *sector = malloc((endsector+1)*sizeof(union sector));
	// Print start and end sector
	fprintf(outfile,"Startsector is %d\n",startsector);
	fprintf(outfile,"Endsector is %d\n",endsector);

	// Read from the file into the sector array
	int r = fread(sector, sizeof(union sector), endsector, f);
	if(debug)
		fprintf(outfile,"Total sectors: %d\n\n", r);

	// Not enough sectors read?
	if(r < (endsector-startsector)) {
		fprintf(stderr,"Only managed to read %d sectors out of %d requested, cowardly refusing to continue\n",r,(endsector-startsector));
		return 1;
	}
	// Close the ADF file
	fclose(f);

	// Loop through the esctors we are supposed to read and recover the data
	for (i=startsector; i<endsector; i++) {
		type = ntohl(sector[i].hdr.type);
		if (type != T_HEADER && type != T_DATA && type != T_LIST)
			continue;
		if(debug) {
			fprintf(outfile,"%x: type       %x\n", i, ntohl(sector[i].hdr.type));
			fprintf(outfile,"%x: header_key %x\n", i, ntohl(sector[i].hdr.header_key));
			fprintf(outfile,"%x: seq_num    %x\n", i, ntohl(sector[i].hdr.seq_num));
			fprintf(outfile,"%x: data_size  %x\n", i, ntohl(sector[i].hdr.data_size));
			fprintf(outfile,"%x: next_data  %x\n", i, ntohl(sector[i].hdr.next_data));
			fprintf(outfile,"%x: chksum     %x\n", i, ntohl(sector[i].hdr.chksum));
		}
		switch (type) {
			case T_HEADER:
				header_key=ntohl(sector[i].hdr.header_key);
				if(debug) {
					fprintf(outfile,"%x:  filename  \"%s\"\n", i, sector[i].fh.filename);
					fprintf(outfile,"%x:  byte_size %d\n", i, ntohl(sector[i].fh.byte_size));
				}
				// Set n here as the header key of the current sector, we'll use it to find all parent headers so we can re-create the directory structure of the disk
				n=i;
				// J is used here to reverse the directory structure (from the disk root and downwards) 
				j=0;
				while(n >= 0) {
					// Add the current filename to the array of paths
					snprintf(filepath[j],MAX_AMIGADOS_FILENAME_LENGTH,"%s",sector[n].fh.filename);
					if(debug) {
						fprintf(outfile,"N: %d I: %d J: %d Current object is %s\n",n,i,j,sector[n].fh.filename);
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
					n = ntohl(sector[n].fh.parent);
				}
				// We should now have an array of all the filepaths belonging to this header, we'll now re-create all the directories in that path (we might do this multiple times for the top level directories but there is no harm in that)
				// Open the current directory so we can return to it later
				cwd = open(".",O_RDONLY);
				if(cwd == -1 ) {
					fprintf(stderr,"Can't open current directory, exiting\n");
					return 1;
				}
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
						// Change directory to the newly created directory
						if(chdir(filepath[n]) == -1) {
							// There is a chance, there's a filename that's the same as the name of the directory we're trying to create, in that case this is most likely
							// an orphaned directory (there can't be a directory and a file with the same name in the same directory so that means one of the entries is corrupted,
							// an orphaned file would never have been created under the restored directory structure, but rather at the root level, so there is a high probability
							// that this directory is an orphan, in which case we should place it at the root level, like an orphaned file
							// Return to the root directory
							if(fchdir(cwd) == -1) {
								fprintf(stderr,"Can't return to previous working directory, exiting\n");
								return 1;
							} 
							// Try to create the directory at the root level instead of down the filesystem level
							if(mkdir(filepath[n],0777) < 0 && errno != EEXIST) {
								fprintf(stderr,"Can't create directory %s\n",filepath[n]);
							} else {
								if(debug)
									fprintf(outfile,"Created Orphaned directory %s\n",filepath[n]);
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
				// Note that there is a good chance that the object is not a file, but another directory, in which case you'll find another orphaned directory with the same name as the file
				// at the root, if you have ideas how this could be puzzled together please fix this code and throw me an email
				// This however makes the work of puzzling together the structure relatively easy
				f = fopen(sector[i].fh.filename, "r+"); /* try to open existing file, if it already exists we won't need to creqte it */
				if (!f) 
					f = fopen(sector[i].fh.filename, "w"); /* doesn't exist, so create */
				// Close the file again
				fclose(f);
				// Return to the previous working directory
				if(fchdir(cwd) == -1) {
					fprintf(stderr,"Can't return to previous working directory, exiting\n");
					return 1;
				} 
				// Close the previously opened working directory
				close(cwd);
				// Leave this sector
				break;
			case T_DATA:
				header_key = ntohl(sector[i].hdr.header_key);
				if (header_key<SECTORS && ntohl(sector[header_key].hdr.type) == T_HEADER) {
					if(debug) {
						fprintf(outfile,"%x:  filename  \"%s\"\n", i, sector[header_key].fh.filename);
						fprintf(outfile,"%x:  byte_size %d\n", i, ntohl(sector[header_key].fh.byte_size));
					}
					snprintf(filename, sizeof(filename), "%s", sector[header_key].fh.filename);
					orphan = 0;
				} else {
					if(debug) {
						fprintf(outfile,"Orphaned file found at header key %d previous orphansector value: %d\n",header_key,orphansector[header_key]);
						fprintf(outfile,"%x:  filename  \"%s\"\n", i, sector[header_key].fh.filename);
						fprintf(outfile,"%x:  byte_size %d\n", i, ntohl(sector[header_key].fh.byte_size));
					}
					// Check whether we've come across this orphaned file before and given it a name, if so we'll keep that name so that we don't write to many different files
					if(orphansector[header_key]) {
						// Fetch the filename from the orphanfilename array
						snprintf(filename,MAX_FILENAME_LENGTH,"%s",orphanfilename[header_key]);
						// Mark this file as an orphan so it gets placed in the root directory
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
						if(sector[header_key].fh.parent && (sector[header_key].fh.parent % 32) == 0 && ntohl(sector[header_key].fh.parent) < endsector) {
							// Check whether there are invalid characters in the parent filename string and mark it as invalid if there are
							for(n=0;n<sizeof(sector[ntohl(sector[header_key].fh.parent)].fh.filename)/sizeof(sector[ntohl(sector[header_key].fh.parent)].fh.filename[0]);n++) {
								if((((unsigned char)sector[ntohl(sector[header_key].fh.parent)].fh.filename[n] <32) && (unsigned char)sector[ntohl(sector[header_key].fh.parent)].fh.filename[n] > 0) || (unsigned char)sector[ntohl(sector[header_key].fh.parent)].fh.filename[n] ==47 || ((unsigned char)sector[ntohl(sector[header_key].fh.parent)].fh.filename[n] >127 && (unsigned char)sector[ntohl(sector[header_key].fh.parent)].fh.filename[n] < 161))
									invalidparentstring=1;
							}
							// If the parent filename is longer than the max_filename_length or it's empty, then it's invalid
							if(strlen(sector[ntohl(sector[header_key].fh.parent)].fh.filename) > MAX_AMIGADOS_FILENAME_LENGTH || strlen(sector[ntohl(sector[header_key].fh.parent)].fh.filename) == 0)
								invalidparentstring=1;
						// If there is no parent then we can't use that to construct the string
						} else {
							invalidparentstring=1;
						}
						
						// If the filename string is good and the parent string is good, we'll use both (only one parent used here)
						if(!invalidstring && !invalidparentstring) {
							snprintf(filename, MAX_FILENAME_LENGTH,"Orphan-%d-%s-%s",header_key,sector[ntohl(sector[header_key].fh.parent)].fh.filename,sector[header_key].fh.filename);
							if(ntohl(sector[header_key].fh.parent) == 880) {
								fprintf(outfile,"Parent er 880\n");
							}
						// Otherwise, if the filename string is good, but the parent string is not, we'll use that
						} else if(!invalidstring) {
							snprintf(filename, MAX_FILENAME_LENGTH,"Orphan-%d-%s",header_key,sector[header_key].fh.filename);
						// Otherwise, if the parent filepath is good, we'll use that
						} else if(!invalidparentstring) {
							snprintf(filename, MAX_FILENAME_LENGTH,"Orphan-%d-%s",header_key,sector[ntohl(sector[header_key].fh.parent)].fh.filename);
						// Otherwise, if the previous filepath is good, we'll use that instead of the parent
						} else if(strlen(previousfilepath) > 0) {
							snprintf(filename, MAX_FILENAME_LENGTH,"Orphan-%d-%s",header_key,previousfilepath);
						// Else we'll have to settle for just the header key value to identify the file since both the filename and parent filename strings are corrupt
						} else {
							snprintf(filename, MAX_FILENAME_LENGTH,"Orphan-%d",header_key);
						}
						// Set the orphan flag to 1
						orphan = 1;
						// Mark this sector as an orphan so if we come across it again we'll use the same filename
						orphansector[header_key] = 1;
						// And set the orphan filename to our current filename
						snprintf(orphanfilename[header_key],MAX_FILENAME_LENGTH,"%s",filename);
						if(debug) {
							if(!invalidstring && !invalidparentstring) {	
								fprintf(outfile, "Filename:%s: Parent Filename: %s Orphan Filename: %s\n",sector[header_key].fh.filename,sector[ntohl(sector[header_key].fh.parent)].fh.filename,filename);
							} else if(!invalidstring) {
								fprintf(outfile, "Filename:%s: Orphan Filename: %s\n",sector[header_key].fh.filename,filename);
							} else if(!invalidparentstring) {
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
				while( n != 0 && header_key<SECTORS && ntohl(sector[header_key].hdr.type) == T_HEADER && (sector[n].fh.parent % 32) == 0) {
					if(sector[n].fh.parent && n != 880)  {
						// Get the path entry name into the filepath array
						snprintf(filepath[j],MAX_AMIGADOS_FILENAME_LENGTH,"%s",sector[ntohl(sector[n].fh.parent)].fh.filename);
						if(debug)
							fprintf(outfile,"File belongs to Directory tree %d, found path %s\n",j,filepath[j]);
						// Set n as the parent to recurse backwards
					 	n = ntohl(sector[n].fh.parent);
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
				cwd = open(".",O_RDONLY);
				if(cwd == -1 ) {
					fprintf(stderr,"Can't open current directory, exiting\n");
					return 1;
				}
				// Reverse loop through the filepath to change into the correct directory to output the file 
				// filepath for this file other than the root filepath, this will re-create directory structures of orphaned directories as well
				for(n=j;n>=0;n--) {
					// If we can't create the directory, and it's not because it already exists 
					// That will happen quite a bit since this code is executed once per block, 
					// and the same file (and directory) can be passed over hundreds of times. 
					// This isn't very efficient, but it does work.
					if(mkdir(filepath[n],0777) < 0 && errno != EEXIST) {
						fprintf(stderr,"Can't create directory %s, exiting\n",filepath[n]);
					} else {
						if(debug)
							fprintf(outfile,"Created directory %s\n",filepath[n]);
						// Change directory to the newly created directory
						if(chdir(filepath[n]) == -1) {
							// There is a chance, there's a filename that's the same as the name of the directory we're trying to create, 
							// in which case this is most likely an orphaned directory, we'll create it at the root level instead
							// Return to the previous working directory
							if(fchdir(cwd) == -1) {
								fprintf(stderr,"Can't return to previous working directory, exiting\n");
								return 1;
							}
							if(mkdir(filepath[n],0777) < 0 && errno != EEXIST) {
								fprintf(stderr,"Can't create directory %s, exiting\n",filepath[n]);
							} else {
								if(debug)
									fprintf(outfile,"Created Orphaned directory %s\n",filepath[n]);
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
				// Store the current filepath, this can help us realise where orphaned files belong
				if(filepath[0] && strlen(filepath[0]) > 0)
					snprintf(previousfilepath,MAX_AMIGADOS_FILENAME_LENGTH,"%s",filepath[0]);
				// If this is an orphan file we'll need to treat it a little differently, we can't fully recreate the path but we most likely have at least the parent directory
				if(orphan) {
					// Split the file 
					// Char points for the split string as well as the original filename
					char *split;
					split = malloc(MAX_FILENAME_LENGTH);
					char *filenamecopy;
					filenamecopy = malloc(MAX_FILENAME_LENGTH);
					// Copy the filename into the copy
					snprintf(filenamecopy,sizeof(filename),"%s",filename);
					// Variable to hold number of splits in the string
					int splits=0;
					// Use strsep to split the string by -  (Orphan, Sector, Parent if there was a parent)
					while((split = strsep(&filenamecopy,"-")) != NULL && splits <2) {
						splits++;
					}
					// If there are 3 parts to the filename, there is a path component which either the last directory traversed or the parent directory of the orphaned file
					// then we can place the file in that path, making it easier to sort out which orphans belong in which directory (and therefore, what could be in the file)
					if(splits == 2) {
						// Return to the root directory first
						if(fchdir(cwd) == -1) {
							fprintf(stderr,"Can't return to previous working directory, exiting\n");
							return 1;
						}
						// Create a directory based on the path component of the oprhan
						if(mkdir(split,0777) < 0 && errno != EEXIST) {
							fprintf(stderr,"Can't create directory %s\n",split);
						} else {
							if(debug)
								fprintf(outfile,"Created directory %s\n",split);
							// Change directory to the newly created directory
							if(chdir(split) == -1) {
								fprintf(stderr,"Can't change to newly created directory,placing orphan in the root directory");
							}
						}
					} else {
						// Return to the previous working directory and place the file in the root
						if(fchdir(cwd) == -1) {
							fprintf(stderr,"Can't return to previous working directory, exiting\n");
							return 1;
						}
					}
				}
					
					
				// Open the file for appending (in the current directory with the path intact)
				f = fopen(filename, "r+"); /* try to open existing file */
				if (!f) 
					f = fopen(filename, "w"); /* doesn't exist, so create */
				if(!f) {
					// File could already exist under the same name or even as a directory, try append the sector header to the filename and try again
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
				if(debug)
					fprintf(outfile,"Seek seq_num %02x : DATABYTES: %lu SEEKSET: %d \n",ntohl(sector[i].hdr.seq_num),DATABYTES,SEEK_SET);
				fseek(f, (ntohl(sector[i].hdr.seq_num)-1)*DATABYTES, SEEK_SET);
				if(debug)
					fprintf(outfile,"seek to %ld\n",  (ntohl(sector[i].hdr.seq_num)-1)*DATABYTES);
				fwrite(sector[i].dh.data, ntohl(sector[i].hdr.data_size), 1, f);
				fclose(f);
				// Return to the previous working directory
				if(fchdir(cwd) == -1) {
					fprintf(stderr,"Can't return to previous working directory, exiting\n");
					return 1;
				}
				// Close the previously opened working directory
				close(cwd);
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

	// Successful run	
	return 0;
}
