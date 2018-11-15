/*
 *	Copyright 2018 Carnegie Mellon University. All Rights Reserved.
 *
 *	NO WARRANTY. THIS CARNEGIE MELLON UNIVERSITY AND SOFTWARE ENGINEERING
 *	INSTITUTE MATERIAL IS FURNISHED ON AN "AS-IS" BASIS. CARNEGIE MELLON
 *	UNIVERSITY MAKES NO WARRANTIES OF ANY KIND, EITHER EXPRESSED OR IMPLIED,
 *	AS TO ANY MATTER INCLUDING, BUT NOT LIMITED TO, WARRANTY OF FITNESS FOR
 *	PURPOSE OR MERCHANTABILITY, EXCLUSIVITY, OR RESULTS OBTAINED FROM USE OF
 *	THE MATERIAL. CARNEGIE MELLON UNIVERSITY DOES NOT MAKE ANY WARRANTY OF
 *	ANY KIND WITH RESPECT TO FREEDOM FROM PATENT, TRADEMARK, OR COPYRIGHT
 *	INFRINGEMENT.
 *
 *	Released under a GNU GPL 2.0-style license, please see license.txt or
 *	contact permission@sei.cmu.edu for full terms.
 *
 *	[DISTRIBUTION STATEMENT A] This material has been approved for public
 *	release and unlimited distribution.  Please see Copyright notice for
 *	non-US Government use and distribution. Carnegie Mellon® and CERT® are
 *	registered in the U.S. Patent and Trademark Office by Carnegie Mellon
 *	University.
 *
 *	This Software includes and/or makes use of the following Third-Party
 *	Software subject to its own license:
 *	1. wmediumd (https://github.com/bcopeland/wmediumd)
 *		Copyright 2011 cozybit Inc..
 *	2. mac80211_hwsim (https://github.com/torvalds/linux/blob/master/drivers/net/wireless/mac80211_hwsim.c)
 *		Copyright 2008 Jouni Malinen <j@w1.fi>
 *		Copyright (c) 2011, Javier Lopez <jlopex@gmail.com>
 *
 *	DM17-0952
 */

#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <errno.h>
#include <unistd.h>
#ifdef _WIN32
  #include <glib-2.0/glib.h>
  #ifndef _WIN32_WINNT
    #define _WIN32_WINNT 0x0501  /* Windows XP. */
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include "vmci_sockets.h"
  #define CONFIG_FILE "C:/welled.conf"
  WSADATA wsa_data;
#else
  #ifndef _ANDROID
    #include <glib.h>
  #endif
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <sys/wait.h>
  #include <linux/if_arp.h>
  #define CONFIG_FILE "/etc/welled.conf"
#endif

#include "wmasterd.h"
#include "vmci_sockets.h"

/** Port used to send frames to wmasterd */
#define SEND_PORT		1111
/** Address used to send frames to wmasterd */
#define SERVER_CID		2
/** Buffer size for VMCI datagrams */
#define VMCI_BUFF_LEN		4096

/** Whether to print verbose output */
int verbose;
/** Whether to break loop after signal */
int running;
/** FD for vmci send */
int sockfd;
/** sockaddr_vm for vmci send */
struct sockaddr_vm servaddr_vmci;

/**
 *	@brief Prints the CLI help
 *	@param exval - exit code
 *	@return - void, calls exit()
 */
void show_usage(int exval)
{
	/* allow help2man to read this */
	setbuf(stdout, NULL);

	printf("gelled-ctrl - control program for GPS emulation\n\n");

	printf("Usage: gelled-ctrl [-hVv] [-x<lon>|-y<lat>|-k<speed>|-d<heading>|-c<climb>] [-f<vm_name>] [-n <name>]\n\n");

	printf("Options:\n");
	printf("  -h, --help       print this help and exit\n");
	printf("  -V, --version    print version and exit\n");
	printf("  -v, --verbose    verbose output\n");
	printf("  -y, --latitude   new latitude in decimal degrees\n");
	printf("  -x, --longitude  new longitude in decimal degrees\n");
	printf("  -a, --altitude   new altitude in meters\n");
	printf("  -k, --knots      new velocity in knots\n");
	printf("  -d, --degrees    new heading in degrees\n");
	printf("  -c, --climb      new climb angle in degrees\n");
	printf("  -f, --follow     follow gps feed of this vm ip/name\n");
	printf("  -n, --name       name for this this machine\n");

	printf("\n");
	printf("Copyright (C) 2017 Carnegie Mellon University\n\n");
	printf("License GPLv2: GNU GPL version 2 <http://gnu.org/licenses/gpl.html>\n");
	printf("This is free software; you are free to change and redistribute it.\n");
	printf("There is NO WARRANTY, to the extent permitted by law.\n\n");

	printf("Report bugs to <arwelle@cert.org>\n\n");

	_exit(exval);
}

/**
 *	@brief Display a hex dump of the passed buffer
 *	this function was pulled off the web somehwere... seems to throw a
 *	segfault when printing some of the netlink error message data
 *	@param addr - address of buffer
 *	@param len - size of buffer
 *	@return void - assumes success
 */
void hex_dump(void *addr, int len)
{
	int i;
	unsigned char buff[17];
	unsigned char *pc;

	pc = (unsigned char *)addr;

	/* Process every byte in the data. */
	for (i = 0; i < len; i++) {
		/* Multiple of 16 means new line (with line offset). */
		if ((i % 16) == 0) {
			/* Just don't print ASCII for the zeroth line. */
			if (i != 0)
				printf("  %s\n", buff);
			/* Output the offset. */
			printf("  %04x ", i);
		}

		/* Now the hex code for the specific character. */
		printf(" %02x", pc[i]);

		/* And store a printable ASCII character for later. */
		if ((pc[i] < 0x20) || (pc[i] > 0x7e))
			buff[i % 16] = '.';
		else
			buff[i % 16] = pc[i];
		buff[(i % 16) + 1] = '\0';
	}

	/* Pad out last line if not exactly 16 characters. */
	while ((i % 16) != 0) {
		printf("   ");
		i++;
	}

	/* And print the final ASCII bit. */
	printf("  %s\n", buff);
}

/**
 *      @brief main function
 */
int main(int argc, char *argv[])
{
	int afVMCI;
	int cid;
	int ret;
	int long_index;
	int opt;
	char *msg;
	int msg_len;
	int bytes;
	struct update_2 data;
	#ifndef ANDROID
	GKeyFile* gkf;
	gchar *key_value;
	#endif
	static struct option long_options[] = {
		{"help",	no_argument, 0, 'h'},
		{"version",     no_argument, 0, 'V'},
		{"verbose",     no_argument, 0, 'v'},
		{"latitude",    required_argument, 0, 'y'},
		{"longitude",   required_argument, 0, 'x'},
		{"altitude",	required_argument, 0, 'a'},
		{"knots",       required_argument, 0, 'k'},
		{"degrees",     required_argument, 0, 'd'},
		{"climb",       required_argument, 0, 'c'},
		{"follow",      required_argument, 0, 'f'},
		{"name",	required_argument, 0, 'n'},
		{0, 0, 0, 0}
	};

	memset(&data, 0, sizeof(struct update_2));
	data.latitude = 9999;
	data.longitude = 9999;
	data.heading = -1;
	data.altitude = -1;
	data.velocity = -1;
	data.climb = -1;

	#ifdef ANDROID
	goto options;
	#else
	/* parse config file */
	gkf = g_key_file_new();
	if (!g_key_file_load_from_file(
			gkf, CONFIG_FILE, G_KEY_FILE_NONE, NULL)) {
		g_print("Could not read config file %s\n", CONFIG_FILE);

		goto options;
	}

	key_value = g_key_file_get_string(gkf, "gelled", "name", NULL);
	if (key_value)
		g_snprintf(data.name, 1024, "%s", key_value);

	key_value = g_key_file_get_string(gkf, "gelled", "follow", NULL);
	if (key_value)
		g_snprintf(data.follow, FOLLOW_LEN, "%s", key_value);

	if (key_value)
		free(key_value);
	g_key_file_free(gkf);
	#endif

options:
	while ((opt = getopt_long(argc, argv, "hVv:y:x:a:k:d:f:n:",
			long_options, &long_index)) != -1) {
		switch (opt) {
		case 'h':
			show_usage(EXIT_SUCCESS);
			break;
		case 'V':
			/* allow help2man to read this */
			setbuf(stdout, NULL);
			printf("gelled-ctrl version %s\n", VERSION_STR);
			_exit(EXIT_SUCCESS);
			break;
		case 'v':
			verbose = 1;
			break;
		case 'y':
			data.latitude = strtof(optarg, NULL);
			// check bounds
			if ((data.latitude < -90) ||
					(data.latitude > 90)) {
				printf("latitude invalid\n");
				_exit(EXIT_FAILURE);
			}
			break;
		case 'x':
			data.longitude = strtof(optarg, NULL);
			// check bounds
			if ((data.longitude < -180) ||
					(data.longitude > 180)) {
				printf("longitude invalid\n");
				_exit(EXIT_FAILURE);
			}
			break;
		case 'a':
			data.altitude = strtof(optarg, NULL);
			// check bounds between 0 and 85k ft (sr-71)
			if ((data.altitude < 0) ||
					(data.altitude > 25908)) {
				printf("altitude invalid\n");
				_exit(EXIT_FAILURE);
			}
			break;
		case 'k':
			data.velocity = strtof(optarg, NULL);
			// check bounds
			break;
		case 'd':
			data.heading = strtof(optarg, NULL);
			// check bounds
			break;
		case 'c':
			data.climb = strtof(optarg, NULL);
			// check bounds
		case 'f':
			/* sync to another vm's gps feed */
			strncpy(data.follow, optarg, FOLLOW_LEN);
			break;
		case 'n':
			strncpy(data.name, optarg, NAME_LEN);
			break;
		case ':':
		case '?':
			printf("Error - No such option: `%c'\n\n",
				optopt);
			show_usage(EXIT_FAILURE);
			break;
		}

	}
	if (optind < argc)
		show_usage(EXIT_FAILURE);

	#ifdef _WIN32
	WSAStartup(MAKEWORD(1,1), &wsa_data);
	#endif

	afVMCI = VMCISock_GetAFValue();
	cid = VMCISock_GetLocalCID();
	if (verbose) {
		printf("CID: %d\n", cid);
		printf("af: %d\n", afVMCI);
	}
	data.cid = cid;

	/* setup vmci client socket */
	sockfd = socket(afVMCI, SOCK_DGRAM, 0);
	/* we can initalize this struct because it never changes */
	memset(&servaddr_vmci, 0, sizeof(servaddr_vmci));
	servaddr_vmci.svm_cid = SERVER_CID;
	servaddr_vmci.svm_port = SEND_PORT;
	servaddr_vmci.svm_family = afVMCI;
	if (sockfd < 0) {
		perror("socket");
		_exit(EXIT_FAILURE);
	}

	msg_len = 7 + sizeof(struct update_2);
	msg = (char *)malloc(msg_len);
	memset(msg, 0, msg_len);
	snprintf(msg, msg_len, "gelled:");
	memcpy(msg + 7, &data, sizeof(struct update_2));

//	if (verbose)
//		hex_dump(msg, msg_len);

	bytes = sendto(sockfd, msg, msg_len, 0,
			(struct sockaddr *)&servaddr_vmci,
			sizeof(struct sockaddr));

	if (bytes != msg_len) {
		perror("sendto");
	} else {
		printf("gelled-ctrl: sent %d bytes to wmasterd\n", bytes);
		if (verbose) {
			printf("latitude:  %f\n", data.latitude);
			printf("longitude: %f\n", data.longitude);
			printf("velocity:  %f\n", data.velocity);
			printf("heading:   %f\n", data.heading);
			printf("climb:     %f\n", data.climb);
		}
	}

	free(msg);

	close(sockfd);

	_exit(EXIT_SUCCESS);
}

