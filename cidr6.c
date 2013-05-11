/*

 Cidr6
 This tool is useful (but not perfect) for IPv6 subnet calculations

 Revision History
 1.00 - Initial release

This program is licensed under the BSD License

Copyright (c) 2011, Sigurbjorn B. Larusson (sibbi (A _ T ) dot1q.org)
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


// Printf and other functions
#include <stdio.h>
// Regular Expression matching
#include <regex.h>
// Exit and other standard functions
#include <stdlib.h>
// Getopt
#include <unistd.h>
// Error output
#include <errno.h>

// Protos
#include <string.h>
#include <ctype.h>

int printout = 0;

//
void printoutrange(int *octet,int prefixlength) {
	if(prefixlength > 64) {
		printf("This application does not support prefix lengths above 64\n");
		return;
	}
	if(prefixlength < 16) {
		printf("This application does not support prefix lengths below 16\n");
	}
	// Setup an array to hold the range...
	unsigned short range[8];
 	// Set the first 4 range as all FFFF since they are part of the host network
	range[0] = ~0; range[1] = ~0; range[2] = ~0; range[3] = ~0; 
	// Prefixlength is 16 or greater but less than 32?
	if(prefixlength > 16 && prefixlength < 32) {
		// 5 and 6 are all Fs
		range[4] = ~0;
		range[5] = ~0;
		// 7 is 1*2^n where n is the remainder of 32-prefixlength
		range[6] = 1 << (32-prefixlength);
		// 8 is the actual octet
		range[7] = octet[7];
	}
	// Print out the range:
	printf("From:\t%x:%x:%x:%x:%x:%x:%x:%x\n",octet[0],octet[1],octet[2],octet[3],octet[4],octet[5],octet[6],octet[7]);
	printf("To:\t%x:%x:%x:%x:%x:%x:%x:%x\n",range[0],range[1],range[2],range[3],range[4],range[5],range[6],range[7]);
	return;
}
// Functions
// ProcessIPv6 takes the IPv6 prefix string as given by the user, checks it for sanity, splits it up, and passes it to printoutrange which prints out the actual range of ip addresses available for that particular prefix
int processipv6(char *ipv6prefix) {
	// Int to indicate whether we had a failure or not
	int failure=0;
	// Char array for splitting into tokens
	char *tokens;
	printf("IPv6 Prefix: %s\n",ipv6prefix);
	// Split the IPv6 string into tokens
	tokens = strtok(ipv6prefix,":/");
	// Read the tokens in
	char ipv6[9][4];
	// Temporary integer to loop on
	int n=0;
	// Temporary integer to store the numbers
	int octet[9];
	// Integer to store the prefixlen
	int prefixlen=0;
	octet[0] = 0; octet[1] = 0; octet[2] = 0; octet[3] = 0; octet[4] = 0;
	octet[5] = 0; octet[6] = 0; octet[7] = 0; octet[8] = 0;
	
	// Split the string into the tokens and populate the ipv6 string array
	while (tokens != NULL) {
		// Copy the tokens into the ipv6 fields
		strncpy(ipv6[n],tokens,4);
		printf("IPV6 %d is %s\n",n,ipv6[n]);
		// Get next token
		tokens = strtok(NULL,":/");
		// Increment n
		n++;
	}
	// How many tokens do we have?
	switch(n) {	
		// We can't really have 0 tokens, that means there was something wrong with the input
		case 0:
			failure=1;			
			break;
		// 1 can only happen for default route, we can easily print that out, but only if the prefixlen was 0
		case 1:
			octet[0]=strtoimax(ipv6[0],NULL,10);
			if(octet==0 != errno) {
				// Default route
				printf("ipv6prefix has the following range:\n\n");
				printf("From: 0000:0000:0000:0000:0000:0000:0000:0000\n");
				printf("To:   FFFF:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF\n");
			} else {
				failure=1;
			}
			break;

		// 2 can happen for a variety of cases, 1 will the contain the first word of the ipv6 address and 2
		// will contain the prefix length
		case 2:
			octet[0]=strtoimax(ipv6[0],NULL,16);
			// Check whether we successfully converted the first word to a number in base16
			if(!errno) {
				// Need to check whether the next octet is a valid prefix-length
				octet[1]=strtoimax(ipv6[1],NULL,10);	
				if(!errno) {
					// Check whether the prefix is of the correct length (no smaller than 16, no larger than 31)
					if(octet[1] < 16 || octet[1] > 31) {
						printf("Invalid prefix length octet[1] for Prefix %s\n",ipv6prefix);
						break;
					}
					// Put the prefixlen in a seperate variable
					prefixlen = octet[1];
					// Make octet[1] zero...
					octet[1] = 0;
					// Prefix length is okay and so is the first part of the prefix, print out the relevant
					// information
					printf("ipv6prefix has the following range:\n\n");
					printoutrange((int *)&octet,prefixlen);

				// Otherwise there is something wrong with the input string
				} else {
					failure=1;
				}
			// Otherwise there is something wrong with the input string
			} else {
				failure=1;
			}
			break;
			
			
	

		// None of the cases matched, so something was wrong with the input	
		default:
			failure=1;
	}
	// Now we need to do some parsing, if every token but the first was empty, and we have a number as the first token, then thi is a network of ::/prefixlength
	return failure;	
}


// Main function
int main(int argc, char **argv) {
	// Temporary variable for getopt
	int c=0;

	// Print usage information
	int usage=1;

	// Variable to store the IPv6 prefix, max length is 43 plus the nullbyte, so the string length is 45
	char ipv6prefix[45];
	while ( ( c = getopt (argc,argv, "p:" ) ) != -1 ) {
		switch(c) {
			case 'p':
				printout=1;
				break;
			case '?':
				if(isprint(optopt))
                                        fprintf(stderr, "Unknown option '-%c'.\n", optopt);
                                else
                                        fprintf(stderr, "Unknown option character '\\x%x'.\n" ,optopt);
                                return 1;
                        default:
                                abort();

		}
	}
	// Accept one, and only one other argument other than possible options
	if(optind == (argc-1)) {
		if(sizeof(argv[argc-1]) <= 43) {
			// Copy the last argument, up till and no further than 44 characters to prevent overflow
			strncpy(ipv6prefix,argv[argc-1],44);
			// No need to print usage information
			usage=0;
			// Process the IPv6 prefix given, printing out the networks if the user so decided
			processipv6((char *)&ipv6prefix);
		} 
	}

	// Valid options received?
	if(usage) {
		printf("Usage: cidr6 [-p prefixbits] IPv6-Prefix\n\n");
		printf("-p\tUsage of -p followed by a number in the range from 0-64 will enable the\n");
		printf("\tprinting out of all networks in your IPv6 prefix that match your given prefixbits\n");
		printf("\nThe IPv6 prefix is written in the standard form of 2a02:48::/32.\n\nExample:\n");
		printf("\tcidr6 -p 56 2a02:48::/32\n\n\tPrint out all /56 networks that belong to 2a02:48::/32 as well as the start and end range of the IPv6 network\n\n");
		exit(1);
	}
	exit(0);
}
