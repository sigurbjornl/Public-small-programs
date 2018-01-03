/*
* Write, a small program that converts any binary file into a hexadecimal dump, and writes them to serial
* and finally followed by a Q, this output can be used by the Amiga "read" tool, available in the 
* (very) early amiga developer kits, by transferring the data over a serial connection 

This can be used for example in FS-UAE by utilizing something like socat, to create virtual serial ports
to connect FS-UAE and this tool, for example

socat -d -d pty,raw,echo=0 pty,raw,echo=0

2018/01/02 10:59:44 socat[46325] N PTY is /dev/ttys011
2018/01/02 10:59:44 socat[46325] N PTY is /dev/ttys013
2018/01/02 10:59:44 socat[46325] N starting data transfer loop with FDs [5,5] and [7,7]
2018/01/02 11:09:28 socat[46325] N socket 2 (fd 7) is at EOF
2018/01/02 11:09:28 socat[46325] N exiting with status 0

This way you could make FS-UAE use /dev/ttys011, using the following line under additionals options (or the in UAE configuration file)

serial_port = /dev/ttys011

You would then instruct this program to use /dev/ttys013 which would connect the two together

The format is fairly trivial, it's just a hex dump finally terminated by a Q character but you can use it to send any file

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

// The Baudrate for the serial port iself, you can modify this on the command line with -b
#define BAUDRATE	9600
#define BUFFERSIZE	  80

// The serial port to use (unless something else is specificed on the commandline), you can modify this on the command line with -b
#define SERIAL_DEVICE	"/dev/ttyS00"
#define SERIAL_DEVICE_PATHLENGTH_MAX	80

// How long is the read buffer?
// 27 is the default, that's 27 two character hex dumps for a total of 54 characters
// and 25 spaces, for a total of 79 characters per line
#define BUFFERLEN	27

// Debug?  You can modify this on the command line with the -d switch
#define DEBUG 0

// Use the standard input/output functions
#include <stdio.h>
// Required for getopt
#include <unistd.h>
// Use string functions
#include <string.h>
// Serial I/O
#include <termios.h>
#include <unistd.h>
// Error numbers
#include <errno.h>
// C library calls including malloc
#include <libc.h>
// strtoimax
#include <inttypes.h>
// time
#include <time.h>

void usage(char *programname) {
        fprintf(stderr,"Write 1.0 (C) 2018 Sigurbjorn B. Larusson\n");
	fprintf(stderr,"\nFor use with its counterpart read on the early AmigaOS floppy disks, start the read program on the amiga side before starting this program\n");
        fprintf(stderr,"\nUsage: %s [-d] [-r <transfer rate in bytes/sec>] [-b <baud rate of serial port in bits/sec>] [-d <serial device>] <filename to send>\n",programname);
        fprintf(stderr,"\n\t-d will activate debugging output which will print more information about what is going on");
	fprintf(stderr,"\n\t-b sets the baud rate to use on the serial port, 9600 works well with the default 1000 bytes rate");   
	fprintf(stderr,"\n\t   if you're feeling adventurous you can try 19200 or even higher, default is 9600");
        fprintf(stderr,"\n\t-l sets the serial line to use for the transfer, default is /dev/ttyS00");
        fprintf(stderr,"\n\t-o For use with pre 1.0 (<30) version of read, this pauses for 50 ms between each sent byte (instead of 5), apparently the developers had tried to compensate");
	fprintf(stderr,"\n\t   for these drops by making the buffer 120k (as opposed to about a kilobyte on 1.0 and later releases, but it still works very poorly");
        fprintf(stderr,"\n\n\tFinally the last argument is the file to send over the serial port");
}

// Function to set serial interface attributes, lifted from https://stackoverflow.com/questions/6947413/how-to-open-read-and-write-from-serial-port-in-c
int set_interface_attribs(int fd, int speed, int old)
{
    struct termios tty;

    if (tcgetattr(fd, &tty) < 0) {
        fprintf(stdout,"Error from tcgetattr: %s\n", strerror(errno));
        return -1;
    }

    cfsetospeed(&tty, (speed_t)speed);
    cfsetispeed(&tty, (speed_t)speed);

    tty.c_cflag = ~(CLOCAL | CREAD);    /* ignore modem controls */
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag &= ~CRTSCTS;    /* disable hardware flowcontrol */
    tty.c_cflag |= CS8;

    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP
                | INLCR | IGNCR | ICRNL | IXON);
    tty.c_oflag &= ~OPOST;

    /* fetch bytes as they become available */
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 5;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        fprintf(stdout,"Error from tcsetattr: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

// Main program
int main(int argc, char **argv) {
	// File descriptors for the input file and the serial interface
	FILE *inf = NULL;
	int ser = 0;
	// Input filename
	char infile[255];
	// Temporary variable
	int i;
	// Variable to hold whether we're using the old or new hexdump format
	int old=0;
	// Read and written bytes
	int readbytes,writtenbytes;
	// Debug  Can be overwritten from command line
	int debug = DEBUG;
	// Char buffer to hold the bytes to transfer and to read the bytes from the file
	char transfer_buffer[3];
	char buffer[BUFFERSIZE+1];
	// Baud rate of the serial port, can be overridden by command line arguments
	int baud_rate = BAUDRATE;
	// The serial device to use, can be overridden by command line arguments
	char *serial_device;
	serial_device = malloc(sizeof(char)*SERIAL_DEVICE_PATHLENGTH_MAX);
	snprintf(serial_device,SERIAL_DEVICE_PATHLENGTH_MAX,"%s",SERIAL_DEVICE);
	// Variables for getopt
	int optionflag = 0; int index = 0;

	// Read in the command line options
	while((optionflag = getopt(argc, argv, "db:l:o")) != -1)
                switch(optionflag) {
                        // Debug flag is set to on
                        case 'd':
                                debug=1;
                                break;
                        // Baud rate
                        case 'b':
				// Convert to integer, then check if we succeeded (there was an int value passed), and whether it matches the available speeds
				// The max here is 230400
                                i = strtoimax(optarg,NULL,10);
				if(i == 9600 || i == 19200 || i == 38400 || i == 57600 || i == 115200 || i == 230400) {
					// Set baud rate in accordance with user selection
					switch(i) {
						case 9600:
							baud_rate = B9600;
							break;
						case 19200:
							baud_rate = B19200;
							break;
						case 38400:
							baud_rate = B38400;
							break;
						case 57600:
							baud_rate = B57600;
							break;
						case 115200:
							baud_rate = B115200;
							break;
						case 230400:
							baud_rate = B230400;
							break;
					}
                                // Otherwise print out the usage help
                                } else {
                                        usage(argv[0]);
                                        return 2;
                                }
                                break;
			// Serial line
                        case 'l':
				// Set the serial device
				snprintf(serial_device,80,"%s",optarg);
                                break;
			// Old (pre 1.0) format
			case 'o':
				// Set the old bit
				old=1;
				break;
                        // Missing argument to r, b or l
                        case '?':
				// Print usage information
                                usage(argv[0]);
                                return 2;
                                break;
                }
	// End while optionflag = getopt
	// The filename should be the last non-option argument given
        for (index = optind; index < argc; index++) {
		// Try opening the file
                inf = fopen(argv[index],"r");
                if(inf == NULL) {
                        fprintf(stderr,"Can't open file %s for reading, error returned was: %s\n",argv[index],strerror(errno));
                        return 1;
                } else {
			if(debug)
				fprintf(stdout,"Successfully opened file %s\n",argv[index]);
		}
        }
        // No file given, print usage instructions
        if(inf == NULL) {
                usage(argv[0]);
                return 2;
        }

	if(debug) {
		fprintf(stdout,"Baud rate is set to: %d bits per second\n",baud_rate);	
		fprintf(stdout,"Serial device to write to is set as %s\n",serial_device);
	}

	// Open up the serial device as read/write
	ser = open(serial_device, O_RDWR | O_NOCTTY | O_NDELAY);	
	fcntl(ser, F_SETFL, FNDELAY);
	// Check whether it opened successfully
	if(ser < 0) {
		fprintf(stderr,"Cannot open serial device %s, error was %s\n",serial_device,strerror(errno));
		return 3;
	} else {
		if(debug)
			fprintf(stdout,"Successfully opened serial device\n");
	}
	// Set serial device options
	set_interface_attribs(ser,baud_rate,old);

	// Read transfer_rate bytes from the source file and dump it as hex for as long we have data into a buffer
	// then send it over the serial, and wait while it drains
	do {
		// Since we need two characters to represent each byte (this being a hex dump format) we'll read half as many characters from the file as we write to the serial
		readbytes=fread(buffer,sizeof(char),BUFFERSIZE,inf);
		if(readbytes > 0) {
			// If debug is enabled pump out more information
			if(debug)
				fprintf(stdout,"Read %d bytes from file into buffer\n",readbytes);
			// Dump as character as a hex code followed by a space
			for(i=0;i<readbytes;i++) {
				snprintf(transfer_buffer,sizeof(transfer_buffer),"%02X",(unsigned char)buffer[i]);
				// If debug is enabled pump out more information
				if(debug)
					fprintf(stdout,"Preparing to write %lu bytes to serial port\n",strlen(transfer_buffer));
				// Write the bytes out to the serial port
				writtenbytes=write(ser,transfer_buffer,2);
				// If debug is enabled pump out more information
				if(debug)
					fprintf(stdout,"Wrote %lu bytes to serial,%s\n",strlen(transfer_buffer),transfer_buffer);
				if (writtenbytes != 2)  {
					fprintf(stdout,"Error from write: %d, %s\n", writtenbytes, strerror(errno));
					return 3;
				} 
				// Wait for the serial port buffer to empty
				tcdrain(ser);
				// Sleep a bit longer for the older versions of the read command
				if(old) { 	
					// Sleep for 50ms before transmitting more data
					nanosleep((const struct timespec[]){{0, 50000000L}}, NULL);
				} else {
					// Sleep for 5ms before transmitting more data
					nanosleep((const struct timespec[]){{0, 5000000L}}, NULL);
				}
			}
		} else {
			if(debug)
				fprintf(stdout,"Finished reading bytes from file\n");
			readbytes=-1;
		}	
	} while(readbytes != -1);
	// And finally followed by a Q to indicate file end!
	snprintf(transfer_buffer,sizeof(transfer_buffer),"Q");
	writtenbytes=write(ser,transfer_buffer,1);
	if (writtenbytes != 1)  {
		if(debug)
        		fprintf(stdout,"Error while sending closing character: %d, %d\n", writtenbytes, errno);
	} else {
		if(debug)
			fprintf(stdout,"Successfully sent closing character\n");
	}
		
	// Wait for the serial port buffer to empty
	tcdrain(ser);
	if(debug)
		fprintf(stdout,"Successfully drained serial port\n");

	// Read any return
	do {
		if(debug)
			fprintf(stdout,"Preparing to read from serial port\n");
        	readbytes = read(ser, transfer_buffer, sizeof(transfer_buffer) - 1);
        	if (readbytes > 0) {
		    if(debug)
		    	fprintf(stdout,"Read %d bytes from serial port: \"%s\"\n", readbytes,transfer_buffer);
		    snprintf(transfer_buffer,sizeof(transfer_buffer),"");
		} else {
            		printf("Error from read: %d: %s\n", readbytes, strerror(errno));
        	}

	} while(readbytes > 0);
	if(debug) 
		fprintf(stdout,"File transfer completed!\n");

	// Close the input file and the serial device
	fclose(inf);
	if(debug)		
		fprintf(stdout,"Closed input file\n");
	close(ser);
	if(debug)
		fprintf(stdout,"Successfully closed the serial port\n");

	// Free up memory
	free(serial_device);

	fprintf(stdout,"Finished\n");	

	return 0;
}
