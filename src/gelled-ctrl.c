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
  #include <syslog.h>
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
#define WMASTERD_PORT		1111
/** Address used to send frames to wmasterd */
#ifndef VMADDR_CID_HOST
	#define VMADDR_CID_HOST	 2
#endif
/** Buffer size for VMCI datagrams */
#define VMCI_BUFF_LEN		4096

/** address family for sockfd */
int af;
/** wmasterd port */
int port;
/** for wmasterd IP */
struct in_addr wmasterd_address;
/** whether to use vsock */
int vsock;
/** Whether to print verbose output */
int verbose;
/** Whether to break loop after signal */
int running;
/** FD for vmci send */
int sockfd;
/** sockaddr_vm for vmci send */
struct sockaddr_vm servaddr_vm;
/** sockaddr_vm for ip */
struct sockaddr_in servaddr_in;
/** for the desired log level */
int loglevel;
/** the radio id to receive location info from */
int radio_id;
/** netns of this process */
long int mynetns;
/** whether this process is inside a netns */
int inside_netns;

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

	printf("Usage: gelled-ctrl [-hVv] [-r<radio_id>] [-x<lon>|-y<lat>|-k<speed>|-d<heading>|-p<pitch>] [-f<vm_name>] [-n <name>]\n\n");

	printf("Options:\n");
	printf("  -h, --help       print this help and exit\n");
	printf("  -V, --version    print version and exit\n");
	printf("  -v, --verbose    verbose output\n");
	printf("  -r, --radio	   radio id\n");
	printf("  -y, --latitude   new latitude in decimal degrees\n");
	printf("  -x, --longitude  new longitude in decimal degrees\n");
	printf("  -a, --altitude   new altitude in meters\n");
	printf("  -k, --knots      new velocity in knots\n");
	printf("  -d, --degrees    new heading in degrees\n");
	printf("  -p, --pitch      new pitch angle in degrees\n");
	printf("  -f, --follow     follow gps feed of this vm ip/name\n");
	printf("  -n, --name       name for this this machine\n");
	printf("  -s, --server	   wmasterd server address\n");
	printf("  -p, --port	   wmasterd server port\n");

	printf("\n");
	printf("Copyright (C) 2023 Carnegie Mellon University\n\n");
	printf("License GPLv2: GNU GPL version 2 <http://gnu.org/licenses/gpl.html>\n");
	printf("This is free software; you are free to change and redistribute it.\n");
	printf("There is NO WARRANTY, to the extent permitted by law.\n\n");

	printf("Report bugs to <arwelle@cert.org>\n\n");

	_exit(exval);
}

void print_debug(int level, char *format, ...)
{
	char buffer[1024];

	if (loglevel < 0)
		return;

	if (level > loglevel)
		return;

	va_list args;
	va_start(args, format);
	vsprintf(buffer, format, args);
	va_end(args);

	char *lev;
	switch(level) {
		case 0:
			lev = "emerg";
			break;
		case 1:
			lev = "alert";
			break;
		case 2:
			lev = "crit";
			break;
		case 3:
			lev = "error";
			break;
		case 4:
			lev = "err";
			break;
		case 5:
			lev = "notice";
			break;
		case 6:
			lev = "info";
			break;
		case 7:
			lev = "debug";
			break;
		default:
			lev = "unknown";
			break;
	}

#ifndef _WIN32
	syslog(level, "%s: %s", lev, buffer);
#endif
	time_t now;
	struct tm *mytime;
	char timebuff[128];
	time(&now);
	mytime = gmtime(&now);

	if (strftime(timebuff, sizeof(timebuff), "%Y-%m-%dT%H:%M:%SZ", mytime)) {
		printf("%s - gelled: %8s: %s\n", timebuff, lev, buffer);
	} else {
		printf("gelled: %8s: %s\n", lev, buffer);
	}
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

int get_mynetns(void)
{
        char *nspath = "/proc/self/ns/net";
        char pathbuf[256];
        int len = readlink(nspath, pathbuf, sizeof(pathbuf));
        if (len < 0) {
                perror("readlink");
                return -1;
        }
        if (sscanf(pathbuf, "net:[%ld]", &mynetns) < 0) {
                perror("sscanf");
                return -1;
        }
        print_debug(LOG_DEBUG, "netns: %ld", mynetns);
        return 0;
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
	int err;
	int ioctl_fd;

	#ifndef ANDROID
	GKeyFile* gkf;
	gchar *key_value;
	#endif

	radio_id = 0;

	static struct option long_options[] = {
		{"help",	no_argument, 0, 'h'},
		{"version",     no_argument, 0, 'V'},
		{"verbose",     no_argument, 0, 'v'},
		{"latitude",    required_argument, 0, 'y'},
		{"longitude",   required_argument, 0, 'x'},
		{"altitude",	required_argument, 0, 'a'},
		{"knots",       required_argument, 0, 'k'},
		{"degrees",     required_argument, 0, 'd'},
		{"pitch",       required_argument, 0, 'P'},
		{"follow",      required_argument, 0, 'f'},
		{"name",	required_argument, 0, 'n'},
		{"server",      required_argument, 0, 's'},
		{"port",	required_argument, 0, 'p'},
                {"radio",       required_argument, 0, 'r'},
		{"debug",       required_argument, 0, 'D'},
		{0, 0, 0, 0}
	};

	err = 0;
	loglevel = -1;
	vsock = 1;
	port = WMASTERD_PORT;

	memset(&data, 0, sizeof(struct update_2));
	data.latitude = 9999;
	data.longitude = 9999;
	data.heading = -1;
	data.altitude = -1;
	data.velocity = -1;
	data.pitch = -1;

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
	while ((opt = getopt_long(argc, argv, "hVvs:p:y:x:a:k:d:f:n:P:D:r:",
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
                case 'r':
                        radio_id = atoi(optarg);
                        break;
		case 'y':
			data.latitude = strtof(optarg, NULL);
			// check boundsa
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
		case 'P':
			data.pitch = strtof(optarg, NULL);
			// check bounds
			break;
		case 'f':
			/* sync to another vm's gps feed */
			strncpy(data.follow, optarg, FOLLOW_LEN);
			break;
		case 'n':
			strncpy(data.name, optarg, NAME_LEN);
			break;
		case 'D':
			loglevel = atoi(optarg);
			printf("gelled: syslog level set to %d\n", loglevel);
			break;
		case 's':
			vsock = 0;
			if (inet_pton(AF_INET, optarg, &wmasterd_address) == 0) {
				printf("gelled: invalid ip address\n");
				show_usage(EXIT_FAILURE);
			}
			break;
		case 'p':
			port = atoi(optarg);
			if (port < 1 || port > 65535) {
				show_usage(EXIT_FAILURE);
			}
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

	if (vsock) {
		#ifdef _WIN32
		/* old code for vmci_sockets.h */
		af = VMCISock_GetAFValue();
		cid = VMCISock_GetLocalCID();
		printf("CID: %d\n", cid);
		#else
		/* new code for vm_sockets */
		af = AF_VSOCK;
		ioctl_fd = open("/dev/vsock", 0);
		if (ioctl_fd < 0) {
			perror("open");
			print_debug(LOG_ERR, "could not open /dev/vsock");
			_exit(EXIT_FAILURE);
		}
		err = ioctl(ioctl_fd, VMCI_SOCKETS_GET_LOCAL_CID, &cid);
		if (err < 0) {
			perror("ioctl: Cannot get local CID");
			print_debug(LOG_ERR, "could not get local CID");
		} else {
			printf("CID: %u\n", cid);
		}
		#endif
	} else {
		af = AF_INET;
	}

        inside_netns = 0;

        /* get my netns */
        if (get_mynetns() < 0) {
                        mynetns = 0;
        };
        /* get all netns */
        DIR *d;
        struct dirent *file;
        struct stat *statbuf;
        d = opendir("/run/netns");
        if (d) {
                while ((file = readdir(d)) != NULL) {
                        if (strcmp(file->d_name, ".") == 0)
                                continue;
                        if (strcmp(file->d_name, "..") == 0)
                                continue;

                        statbuf = malloc(sizeof(struct stat));
                        if (!statbuf) {
                                perror("malloc");
                                exit(1);
                        }
                        int maxlen = strlen("/run/netns/") + strlen(file->d_name) + 1;
                        char *fullpath = malloc(maxlen);
                        if (!fullpath) {
                                perror("malloc");
                                exit(1);
                        }
                        snprintf(fullpath, maxlen, "/run/netns/%s", file->d_name);
                        int fd = open(fullpath, O_RDONLY);
                        if (fd < 0) {
                                perror("open");
                        }
                        if (fstat(fd, statbuf) < 0) {
                                perror("fstat");
                        }
                        long int inode = statbuf->st_ino;
                        print_debug(LOG_DEBUG, "%s has inode %ld", fullpath, inode);

                        if (inode == mynetns) {
                                inside_netns = 1;
                        }
                        close(fd);
                        free(statbuf);
                        free(fullpath);
                }
                closedir(d);
        } else {
                print_debug(LOG_ERR, "cannot open /run/netns");
        }

        if (inside_netns) {
                print_debug(LOG_INFO, "running inside netns %ld", mynetns);
        } else {
                print_debug(LOG_INFO, "not runnning inside netns");
        }

	strncpy(data.version, (const char *)VERSION_STR, 8);

	/* setup wmasterd details */
	memset(&servaddr_vm, 0, sizeof(servaddr_vm));
	servaddr_vm.svm_cid = VMADDR_CID_HOST;
	servaddr_vm.svm_port = port;
	servaddr_vm.svm_family = af;

	memset(&servaddr_in, 0, sizeof(servaddr_in));
	servaddr_in.sin_addr = wmasterd_address;
	servaddr_in.sin_port = port;
	servaddr_in.sin_family = af;

	if (vsock) {
		print_debug(LOG_ERR, "connecting to wmasterd on VSOCK %u:%d",
				servaddr_vm.svm_cid, servaddr_vm.svm_port);
	} else {
		print_debug(LOG_ERR, "connecting to wmasterd on IP %s:%d",
				inet_ntoa(servaddr_in.sin_addr), servaddr_in.sin_port);
	}

	/* setup socket */
	if (vsock) {
		sockfd = socket(af, SOCK_STREAM, 0);
	} else {
		sockfd = socket(af, SOCK_STREAM, IPPROTO_TCP);
	}
	if (sockfd < 0) {
		perror("socket");
		_exit(EXIT_FAILURE);
	}

	if (vsock) {
		ret = connect(sockfd, (struct sockaddr *)&servaddr_vm, sizeof(struct sockaddr));
	} else {
		ret = connect(sockfd, (struct sockaddr *)&servaddr_in, sizeof(struct sockaddr));
	}
	if (ret < 0) {
		if (vsock) {
			print_debug(LOG_ERR, "could not connect to wmasterd on %u:%d",
					servaddr_vm.svm_cid, servaddr_vm.svm_port);
		} else {
			print_debug(LOG_ERR, "could not connect to wmasterd on %s:%d",
					inet_ntoa(servaddr_in.sin_addr), servaddr_in.sin_port);
		}
	}

    struct message_hdr hdr;
    msg_len = sizeof(struct message_hdr);
    memcpy(hdr.name, "gelled", 6);
    strncpy(hdr.version, (const char *)VERSION_STR, 8);
    hdr.len = msg_len;
    hdr.src_radio_id = radio_id;
    hdr.netns = mynetns;

	msg_len = sizeof(struct message_hdr) + sizeof(struct update_2);
	msg = (char *)malloc(msg_len);
	memset(msg, 0, msg_len);
	memcpy(msg, &hdr, sizeof(struct message_hdr));
        memcpy(msg + sizeof(struct message_hdr), &data, sizeof(struct update_2));

	if (verbose)
		hex_dump(msg, msg_len);

	bytes = send(sockfd, msg, msg_len, 0);

	if (bytes != msg_len) {
		perror("send");
	} else {
		printf("gelled-ctrl: sent %d bytes to wmasterd\n", bytes);
		if (verbose) {
			printf("latitude:  %f\n", data.latitude);
			printf("longitude: %f\n", data.longitude);
			printf("altitude:  %f\n", data.altitude);
			printf("velocity:  %f\n", data.velocity);
			printf("heading:   %f\n", data.heading);
			printf("pitch:     %f\n", data.pitch);
			printf("follow:    %s\n", data.follow);
		}
	}

	free(msg);

	close(sockfd);

	_exit(EXIT_SUCCESS);
}

