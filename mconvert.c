/*

 mconvert.c
 This program converts MAC->IP and IP->MAC
 Revision History
 Version 1.00 Initial release
 Version 1.01 Fixed compiler warning due to not importing string.h
 Version 1.02 Fixed compiler warning due to using %d for a %zd value in fprintf
 Version 1.03 Fixed compiler warning for isprint due to not importing ctype.h, now compiles with -Werror
 Version 1.04 Added check for a valid multicast address 
 Version 1.05 Added check for a valid IP address

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

// printf,fprintf,sprintf
#include <stdio.h>
// Various conversion functions...
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
// Regular express matching
#include <regex.h>
// Error output from regexp
#include <errno.h>

// Protos
// Strsep
#include <string.h>
// isprint
#include <ctype.h>

// Function ipconvert
// Convert ip to mac
// Accept value string from command line as input...
// Returns 0 on success
// != 0 on failure
int ipconvert(char *value);

// Function macconvert
// Convert mac to mac
// Accept value string from command line as input...
// Returns 0 on success
// != 0 on failure
int macconvert(char *value);

// Functions themselves..

int ipconvert(char *value) {
                // Make a mac address out of an ip address...
                // Status variable
                int status = 0;
                // Compiled regexp struct...
                regex_t compregex;
                // Compile regexp to match on IP address
                if(regcomp(&compregex,"^((25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[0-9]?[0-9])\\.){3}(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[0-9]?[0-9])$",REG_EXTENDED|REG_NOSUB) != 0) {
                        fprintf(stderr, "Regular expression error occured, exiting\n\n");
                        return 2;
                }
                // Does it match?
                status = regexec(&compregex, value, (size_t) 0, NULL, 0);
                // Free up the memory used
                regfree(&compregex);
                // If it didn't match...
                if(status) {
                        // Not matching a correct ip address format...
                        fprintf(stderr, "IP address doesn't match, the ip address needs to be in the dotted quad form (123.123.123.123)\n\n");
                // Otherwise we continue
                } else {
                        // Well it's an ip address alright...
                        // Use an array of strings to hold the seperated strings (by .)
                        char **seperated = 0;
                        // Malloc space for 4 strings for the ip addresses upto 19 bytes in length
                        seperated=(char **)malloc(4*sizeof(char*));
                        // Duplicate the value string for strsep
                        char *s = value;
                        // To strip the 8th bit from the second octet...
                        unsigned int bitmask = 127; //0111 1111
                        // Integer array to grab the IP addressess...
                        unsigned int ips[4];
                        ips[0] = 0; ips[1] = 0; ips[2] = 0; ips[3] = 0;
                        // Jumper variable
                        int i=0;
                        for(i=0;i<4;i++) {
                                // Seperate the string on .
                                *seperated = strsep((char **)&s,".");
                                // Set the values for ips[0]..[3] based on the octets...
                                ips[i] = (int)strtol(*seperated, (char **)NULL, 10);
                                // Next string from the array
                                seperated++;
                        }
                        // Now that we've done that, we can do some magic...
                        // First we print out the IP address itself...
                        fprintf(stdout, "IP address : %d.%d.%d.%d\n",ips[0],ips[1],ips[2],ips[3]);
			// Check whether this is a multicast address
			if(ips[0] < 224) {
				fprintf(stderr, "Given IP address is not a valid multicast address!\n");
				return 0;
			}
                        // Then we calculate the mac address, the first part is always 01-00-5e
                        // Then we use a bitmask to strip the 8th bit from the second octet
                        ips[1] = ips[1] & bitmask;
                        // Then we just print them out in hex format...
                        fprintf(stdout, "MAC address: 0100.5e%02x.%02x%02x\n",ips[1],ips[2],ips[3]);
                } // End of regex match
        // If nothing out of the ordinary happened, return 0
        return 0;
// End of int ipconvert(char *value)
}

int macconvert(char *value) {
                // Value array..
                long values[4];
                values[0] = 0; values[1] = 0; values[2] = 0; values[3] = 0; 
                // Status variable
                int status = 0;
                // Type of mac address 1 for 00.00.00.00.00.00, 00-00-00-00-00-00 or 00:00:00:00:00:00 2 for 0000.0000.0000 or 0000:0000:0000
                int type = 0;
                // Compiled regexp struct...
                regex_t compregex;
                // Compile regexp...
                if(regcomp(&compregex,"[0-9|A-F][0-9|A-F][-|:|.][0-9|A-F][0-9|A-F][-|:|.][0-9|A-F][0-9|A-F][-|:|.][0-9|A-F][0-9|A-F][-|:|.][0-9|A-F][0-9|A-F][-|:|.][0-9|A-F][0-9|A-F]",REG_ICASE) != 0) {
                        fprintf(stderr, "Regular expression error occured, exiting\n\n");
                        return 2;
                }
                // Execute the regexp 
                status = regexec(&compregex, value, (size_t) 0, NULL, 0);
                // Free up memory for regexp compilations
                regfree(&compregex);
                // Was there a match
                if(status) {
                        // No, Let's try the other format...
                        if(regcomp(&compregex,"[0-9|A-F][0-9|A-F][0-9|A-F][0-9|A-F][.|:][0-9|A-F][0-9|A-F][0-9|A-F][0-9|A-F][.|:][0-9|A-F][0-9|A-F][0-9|A-F][0-9|A-F]",REG_ICASE) != 0) {
                                fprintf(stderr, "Regular expression error occurred, exiting\\n");
                                return 3;
                        }
                        status = regexec(&compregex, value, (size_t) 0, NULL, 0);
                        regfree(&compregex);
                        if(status) {
                                // Still an error?
                                fprintf(stderr, "Unknown MAC address format, try 00-00-00-00-00-00, 0000.0000.0000 or 0000:0000:0000\n\n");
                                // Return with error
                                return 4;
                        } else {
                                // If there was no error the second time around this is a type 2 mac address format
                                type = 2;
                        }
                } else {
                        // There was a match on the first try, this is a type 1 mac address format
                        type = 1;
                }
                // Mac address is okay...
                // Jumper variable
                int i = 0;
                // For temporarily storing long values
                long tempvalue = 0L;
                // String for holding parts of the mac address and a string terminator
                char tempchar[5];
                // Set the first charcters of the tempchar string to be 0x to signify that we are working with hex numbers...
                tempchar[0] = '0'; tempchar[1] = 'x';
                // If the type is either 1 or 2
                if(type == 1 || type == 2) {
                        if(type == 1) {
                                // Type 1 (00-00-00-00-00-00 or 00:00:00:00:00:00)
                                // Convert to IP address...
                                // Set first octet to 224...
                                values[0] = 224;
                                // Second octet...      
                                tempchar[2] = value[9];
                                tempchar[3] = value[10];
                                tempchar[4] = '\0';
                                // Convert the string to long...
                                tempvalue = strtol(tempchar,NULL,16);
                                // Error?
                                if(errno != EINVAL) {
                                        values[1] = tempvalue;
                                }
                                // Third octet...
                                tempchar[2] = value[12];
                                tempchar[3] = value[13];
                                // Convert the string to long...
                                tempvalue = strtol(tempchar,NULL,16);
                                // Error?
                                if(errno != EINVAL) {
                                        values[2] = tempvalue;
                                }
                                // Fourth octet
                                tempchar[2] = value[15];
                                tempchar[3] = value[16];
                                // Convert the string to long...
                                tempvalue = strtol(tempchar,NULL,16);
                                // Error?
                                if(errno != EINVAL) {
                                        values[3] = tempvalue;
                                }
                        // end if(type == 1)
                        } else {
                                // Type 2 (0000.0000.0000)
                                // Convert to IP address...
                                values[0] = 224;
                                // Second octet...
                                tempchar[2] = value[7];
                                tempchar[3] = value[8];
                                tempchar[4] = '\0';
                                // Convert the string to long...
                                tempvalue = strtol(tempchar,NULL,16);
                                // Error?
                                if(errno != EINVAL) {
                                        values[1] = tempvalue;
                                }
                                // Third octet...
                                tempchar[2] = value[10];
                                tempchar[3] = value[11];
                                // Convert the string to long...
                                tempvalue = strtol(tempchar,NULL,16);
                                // Error?
                                if(errno != EINVAL) {
                                        values[2] = tempvalue;
                                }
                                // Fourth octet
                                tempchar[2] = value[12];
                                tempchar[3] = value[13];
                                // Convert the string to long...
                                tempvalue = strtol(tempchar,NULL,16);
                                // Error?
                                if(errno != EINVAL) {
                                        values[3] = tempvalue;
                                }
                        } // At this point we have filled values[0]..values[3] with correct decimal values for the mac address...
                        // Print out the original mac address...
                        fprintf(stdout,"MAC Address: %s\n",value);
			// Check whether the mac address is a real multicast adddress
			if(values[1] > 127L || values[2] > 127L || values[3] > 127L) {
				fprintf(stdout,"Given MAC address is not a valid multicast MAC address!\n");
				return 1;
			}
                        // And then print all possible matches...
                        for(i=224;i<240;i++) {
                                fprintf(stdout,"IP address : %d.%zd.%zd.%zd\n",i,values[1],values[2],values[3]);
                                fprintf(stdout,"IP address : %d.%zd.%zd.%zd\n\n",i,values[1]+128,values[2],values[3]);
                        }
                // end if(type == 1 || type==2)
                }
        // If nothing out of the ordinary happened, return 0
        return 0;
// End of int macconvert(*value);
}


// Main program...
int main(int argc, char **argv) {
        // Whether the user opted for IP->MAC or MAC->IP conversion...
        int ipopt = 0;
        int macopt = 0;
        // Strings to hold the content of the IP and MAC arguments
        char *ipvalue = NULL;
        char *macvalue = NULL;
        // Temporary variable for getopt
        int c;
        fprintf(stdout, "Multicast IP<->MAC converter, V1.05 (c) 2009 Burdarnet Vodafone\n\n");
        // Use GNU GetOpt to get command line arguments...
        while (( c = getopt (argc, argv, "p:q:" ) ) != -1)
                switch (c)
                {
                        case 'p':
                                ipvalue = optarg;
                                ipopt = 1;
                                break;
                        case 'q':
                                macvalue = optarg;
                                macopt = 1;
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
        // Check what the user wanted to do
        if(!ipopt && !macopt) {
                fprintf(stderr, "Usage: %s -p <IP value> or -q <MAC value>\n\n",argv[0]);
                return 1;
        } else if(ipopt) {
                ipconvert(ipvalue);
        } else if(macopt) {
                macconvert(macvalue);
        }

        // If nothing out of the ordinary happened, return 0
        return 0;
// End of main
}

// End of mconvert.c
