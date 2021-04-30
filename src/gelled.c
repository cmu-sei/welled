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
#include <signal.h>
#include <sys/types.h>
#include <string.h>
#ifdef _WIN32
  #ifndef _WIN32_WINNT
    #define _WIN32_WINNT 0x0501  /* Windows XP. */
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include "vmci_sockets.h"
  #include <tchar.h>
  #include <winbase.h>
  WSADATA wsa_data;
#else
  #include <syslog.h>
  #include <stdarg.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <sys/wait.h>
  #include <sys/ioctl.h>
  /* vmci_sockets removed in favor of more generic vm_scockets */
  //#include "vmci_sockets.h"
  #include <linux/vm_sockets.h>
#endif
#include <png.h>
#include <curl/curl.h>
#include <pthread.h>
#include <errno.h>
#include <math.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "wmasterd.h"

/* Address used to send frames to wmasterd */
#ifndef VMADDR_CID_HOST
	#define VMADDR_CID_HOST 2
#endif
/** Port used to send data to wmasterd */
#define SEND_PORT_G		1111
/** Port used to receive NMEA sentences from wmasterd */
#define RECV_PORT_G		3333
/** Buffer size for VMCI datagrams */
#define VMCI_BUFF_LEN		4096
/** Buffer size for UDP datagrams */
#define BUFF_LEN		4096
/** Zoom level to use when requesting tiles from OSM server */
#define ZOOM			18
/** Convert degrees to radians */
#define DEG2RAD(DEG) ((DEG)*((M_PI)/(180.0)))


/** Holds RGBA pixel data for a .png file */
typedef struct {
	int height;
	int width;
	png_bytep *row_pointers;
} png_data;

/** Whether to print verbose output */
int verbose;
/** Whether to break loop after signal */
int running;
/** FD for vmci send */
int sockfd;
/** FD for vmci receive */
int myservfd;
/** sockaddr_vm for vmci send */
struct sockaddr_vm servaddr_vmci;
/** sockaddr_vm for vmci receive */
struct sockaddr_vm myservaddr_vmci;
/** server address for OSM tiles */
char *mapserver;
/** device path for write */
char *dev_path;
/** for tracking our VMCI CID and sending in velocty change */
int cid;
/** whether our gps device needs to stay at sea */
int sea;
/** whether our gps device needs to stay on land */
int land;
/** whether or not we want to check land or sea position */
int check_position;
/** current latitude */
double lat;
/** current longitude */
double lon;
/** current velocity */
double velocity;
/** thread ID for keepalive thread */
pthread_t status_tid;
/** for the desired log level */
int loglevel;

#ifdef _WIN32
#ifndef HAVE_STRSEP
char *strsep(char **sp, char *sep)
{
	char *p;
	char *s;

	if (sp == NULL || *sp == NULL || **sp == '\0')
		return(NULL);

	s = *sp;
	p = s + strcspn(s, sep);

	if (*p != '\0')
		*p++ = '\0';

	*sp = p;

	return(s);
}
#endif
#endif

/**
 *      @brief Prints the CLI help
 *      @param exval - exit code
 *      @return - void, calls exit()
 */
void show_usage(int exval)
{
	/* allow help2man to read this */
	setbuf(stdout, NULL);

	printf("gelled - gelled, GPS emulation link layer exchange daemon \n\n");

	printf("Usage: gelled [-hVv] [-d<device>] [-m<url>] [-s|-l]\n\n");

	printf("Options:\n");
	printf("  -h, --help		print this help and exit\n");
	printf("  -V, --version		print version and exit\n");
	printf("  -v, --verbose		verbose output\n");
	printf("  -D, --debug		debug level for syslog\n");
	printf("  -l, --land		gps should only travel on land\n");
	printf("  -s, --sea		gps should only travel on water\n");
	printf("  -d, --device		use this device as GPS\n");
	printf("  -m, --mapserver	use this server for map tiles\n\n");

	printf("Copyright (C) 2016 Carnegie Mellon University\n\n");
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
	#ifndef _WIN32
	syslog(level, buffer);
	#else
	printf("gelled: %s\n", buffer);
	#endif
}

/**
 *      @brief Signal handler which causes program to exit
 *      @return void - calls exit()
 */
void signal_handler(void)
{
	print_debug(LOG_INFO, "signal handler invoked\n");

	running = 0;
}


/**
 *	@brief get osm map image file from web server
 *	downloads the specified OSM tile and saves it at the specified location
 *	used curl to access a map server via http
 *	@param x - the x-coordinate of the tile
 *	@param y - the y-coordinate of the tile
 *	@param zoom - the zoom level of the tile
 *	@param filename - location at which to save the tile
 *	@return - 0 on success and -1 on error
 */
int download_osm_tile(int x, int y, int zoom, char *filename)
{
	FILE *png_write;
	char url[1024];
	CURL *curl;
	CURLcode res;

	curl = curl_easy_init();

	if (curl) {
		png_write = fopen(filename, "wb");
		if (png_write == NULL) {
			print_debug(LOG_ERR, "Error: failed to open %s for writing\n", filename);
			return -1;
		}
		snprintf(url, 1024, "http://%s/%d/%d/%d.png",
			mapserver, zoom, x, y);

		print_debug(LOG_DEBUG, "downloading tile from %s\n", url);

		curl_easy_setopt(curl, CURLOPT_URL, url);
		curl_easy_setopt(curl, CURLOPT_URL, url);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, png_write);
		res = curl_easy_perform(curl);
		curl_easy_cleanup(curl);
		fclose(png_write);
		if (res) {
			print_debug(LOG_ERR, "Error: curl failed to retrieve tile\n");
			return -1;
		}
	} else {
		print_debug(LOG_ERR, "Error: failed to initialize curl.\n");
		return -1;
	}

	return 0;
}

/**
 *	@brief reads RGBA pixel data from the specifed .png file
 *	this tells us the color of our location on the map
 *	@param pixels - the structure in which to store the RGBA pixel data
 *	@param filename - the .png file from which to read data
 *	@return - 0 on sucess and -1 on error
 */
int read_png(png_data *pixels, char *filename)
{
	FILE *png_read;
	png_byte color_type;
	png_byte bit_depth;
	png_structp png;
	png_infop info;
	int i;

	png_read = fopen(filename, "r");
	if (png_read == NULL) {
		print_debug(LOG_ERR, "Error: failed to open %s for reading\n", filename);
		return -1;
	}

	png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png) {
		print_debug(LOG_ERR, "Error: failed to create read struct for png\n");
		fclose(png_read);
		return -1;
	}

	info = png_create_info_struct(png);
	if (!info) {
		print_debug(LOG_ERR, "Error: failed to create info struct for png\n");
		png_destroy_read_struct(&png, NULL, NULL);
		fclose(png_read);
		return -1;
	}

	if (setjmp(png_jmpbuf(png))) {
		print_debug(LOG_ERR, "Error: a png function failed\n");
		png_destroy_read_struct(&png, &info, NULL);
		fclose(png_read);
		return -1;
	}
	png_init_io(png, png_read);
	png_read_info(png, info);

	pixels->width = png_get_image_width(png, info);
	pixels->height = png_get_image_height(png, info);
	color_type = png_get_color_type(png, info);
	bit_depth = png_get_bit_depth(png, info);

	if (bit_depth == 16)
		png_set_strip_16(png);

	if (color_type == PNG_COLOR_TYPE_PALETTE)
		png_set_palette_to_rgb(png);

	/* PNG_COLOR_TYPE_GRAY_ALPHA is always 8 or 16 bit depth */
	if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
		png_set_expand_gray_1_2_4_to_8(png);

	if (png_get_valid(png, info, PNG_INFO_tRNS))
		png_set_tRNS_to_alpha(png);

	/* These color_type don't have an alpha channel
	 * fill it with 0xff
	 */
	if (color_type == PNG_COLOR_TYPE_RGB ||
		color_type == PNG_COLOR_TYPE_GRAY ||
		color_type == PNG_COLOR_TYPE_PALETTE)
		png_set_filler(png, 0xFF, PNG_FILLER_AFTER);

	if (color_type == PNG_COLOR_TYPE_GRAY ||
		color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
		png_set_gray_to_rgb(png);

	png_read_update_info(png, info);

	pixels->row_pointers = (png_bytep *)malloc(
		sizeof(png_bytep) * pixels->height);

	for (i = 0; i < pixels->height; i++) {
		pixels->row_pointers[i] = (png_byte *)malloc(
		png_get_rowbytes(png, info));
	}

	png_read_image(png, pixels->row_pointers);
	png_destroy_read_struct(&png, &info, NULL);
	fclose(png_read);

	return 0;
}


/**
 *	@brief determines whether the specified color is on land or sea
 *	this is used to stop a ship when crossing from sea to land or from
 *	land to sea.
 *	@param red - the red component of the color
 *	@param green - the green component of the color
 *	@param blue - the blue component of the color
 *	@return - 1 if at sea, 0 if on land, -1 on unknown/error
 */
int land_or_sea_check_color(int red, int green, int blue)
{

	if (red == 181 && green == 208 && blue == 208) {
		/* sea, blue */
		print_debug(LOG_DEBUG, "This tile is Sea\n");
		return 1;
	} else if (red == 181 && green == 253 && blue == 253) {
		/* boundary, purple, shade 1 */
		return -1;
	} else if (red == 181 && green == 92 && blue == 162) {
		/* boundary, purple, shade 2 */
		return -1;
	} else if (red == 181 && green == 213 && blue == 213) {
		/* text */
		return -1;
	} else if (red == 181 && green == 174 && blue == 174) {
		/* text */
		return -1;
	} else if (red == 181 && green == 105 && blue == 252) {
		/* blue boundary */
		return -1;
	} else if (red == 181 && green == 122 && blue == 245) {
		/* boundary */
		return -1;
	} else if (red == 181 && green == 110 && blue == 249) {
		/* boundary */
		return -1;
	} else if (red == 181 && green == 157 && blue == 187) {
		/* text */
		return -1;
	} else if (red == 181 && green == 104 && blue == 166) {
		/* boundary? */
		return -1;
	} else if (red == 181 && green == 102 && blue == 253) {
		/* boundary? */
		return -1;
	} else if (red == 197 && green == 216 && blue == 214) {
		/* boundary with land? */
		print_debug(LOG_DEBUG, "This tile is Land\n");
		return 0;
	} else if (red == 241 && green == 238 && blue == 232) {
		/* boundary with land? */
		print_debug(LOG_DEBUG, "This tile is Land\n");
		return 0;
	} else if (red == 181 && green == 238 && blue == 232) {
		/* land */
		print_debug(LOG_DEBUG, "This is tile is Land\n");
		return 0;
	} else if (red == 242 && green == 239 && blue == 233) {
		/* land */
		print_debug(LOG_DEBUG, "This tile is Land\n");
		return 0;
	}

	print_debug(LOG_DEBUG, "Tile Red: %d, Green: %d, Blue: %d\n", red, green, blue);
	return -1;
}


/**
 *	@brief checks whether position is sea or land
 *	calls download_osm_tile and land_or_sea_check_color
 *	@param lat - double decimal degrees
 *	@param lon - double decimal degrees
 *	@return - 1 if at sea, 0 if on land, -1 on unknown/error
 */
int check_if_sea(double lat, double lon)
{

	double x, y;
	int xtile, ytile, xpixel, ypixel, ret, i;
	png_data pixels;
	png_bytep px;

	x = ((lon + 180) / 360) * pow(2, ZOOM);
	xtile = (int)floor(x);
	xpixel = (int)floor((x - xtile) * 256);
	y = (1 - log(tan(DEG2RAD(lat))
		+ 1 / cos(DEG2RAD(lat))) / M_PI) / 2 * pow(2, ZOOM);
	ytile = (int)floor(y);
	ypixel = (int)floor((y - ytile) * 256);

	/* TODO:
	 * rework download_osm_tile and read_png to cache the
	 * tiles locally and prevent multiple requests for the same tile
	 */
	if (download_osm_tile(xtile, ytile, ZOOM, "/tmp/map.png"))
		return -1;

	if (read_png(&pixels, "/tmp/map.png"))
		return -1;

	/* Check pixel color */
	px = &((pixels.row_pointers[ypixel])[xpixel * 4]);
	ret = land_or_sea_check_color(px[0], px[1], px[2]);

	for (i = 0; i < pixels.height; i++)
		free(pixels.row_pointers[i]);

	free(pixels.row_pointers);

	return ret;
}

/**
 *      @brief Get data from RMC NMEA sentence
 *	RMC is required minimum navigation data
 *	@param line - buffer containing GPRMC
 *	updates velocity, lat and lon
 *	@return - void
 */
void process_rmc(char *line)
{
	int i;
	char *token;
	char temp[10];
	char digits[4];
	char minutes[9];

	/* take bearing from rmc */
	i = 0;
	token = strsep(&line, ",");

	while (token != NULL) {
		if (i == 3) {
			snprintf(digits, 3, "%s", token);
			lat = atof(digits);
			/* thats degrees, now convert minutes */
			snprintf(minutes, 7, "%s", token + 2);
			lat += atof(minutes) * 0.0166666;
		} else if (i == 4) {
			if (strncmp("S", token, 1) == 0)
				lat *= -1;
		} else if (i == 5) {
			snprintf(digits, 4, "%s", token);
			lon = atof(digits);
			/* thats degrees, now convert minutes */
			snprintf(minutes, 9, "%s", token + 3);
			lon += atof(minutes) * 0.01666667;
		} else if (i == 6) {
			if (strncmp("W", token, 1) == 0)
				lon *= -1;
		} else if (i == 7) {
			velocity = atof(token);
		}
		token = strsep(&line, ",");
		i++;
	}
}

/**
 *	@brief stop movement if we crashed
 *      sends velocity change of 0 to wmasterd
 *      to be used when we move from land to sea or sea to land
 *      this is basically gelled-ctrl
 *	TODO: adjust altitude if we have a plane crash
 */
void send_stop(void)
{
	if (verbose) {
		printf("stopping the vm\n");
	}

	pid_t pid;
	struct stat buf_stat;

	/* fork */
	pid = fork();

	if (pid == -1) {
		perror("fork");
	} else if (pid == 0) {
		/* child */
		if (stat("/bin/gelled-ctrl", &buf_stat) == 0) {
			if (verbose) {
				execl("/bin/gelled-ctrl", "gelled-ctrl",
					"-v", "-k", "0", NULL);
			} else {
				execl("/bin/gelled-ctrl", "gelled-ctrl",
					"-k", "0", NULL);
			}
		} else {
			printf("couldnt find gelled-ctrl\n");
			_exit(EXIT_FAILURE);
		}
	} else {
		/* parent - wait on child */
		waitpid(pid, NULL, 0);
	}

	return;
}

/**
 *      @brief parse vmci data from wmastered.
 *      @return void
 */
void recv_from_master(void)
{
	char buf[VMCI_BUFF_LEN];
	struct sockaddr_vm cliaddr_vmci;
	struct sockaddr_in cliaddr;
	socklen_t addrlen;
	struct timeval tv; /* timer to break us out of the recvfrom function */
	int bytes;
	char *srchost;
	FILE *fp;
	int ret;
	struct stat buf_stat;

	addrlen = sizeof(struct sockaddr);
	memset(&cliaddr_vmci, 0, sizeof(cliaddr_vmci));
	memset(&cliaddr, 0, sizeof(cliaddr));
	memset(buf, 0, VMCI_BUFF_LEN);

	tv.tv_sec = 10;
	tv.tv_usec = 0;

	#ifdef _WIN32
	if (setsockopt(myservfd, SOL_SOCKET, SO_RCVTIMEO, (const char *) &tv,
			sizeof(tv)) < 0) {
		printf("setsockopt: %ld\n", WSAGetLastError());
	}
	#else
	if (setsockopt(myservfd, SOL_SOCKET, SO_RCVTIMEO, &tv,
			 sizeof(tv)) < 0) {
		perror("setsockopt");
		print_debug(LOG_ERR, "Error: could not set server socket timeout");
	}
	#endif

	/* receive packets from wmasterd */
	bytes = recvfrom(myservfd, (char *)buf, VMCI_BUFF_LEN, 0,
			(struct sockaddr *)&cliaddr_vmci, &addrlen);

	if (bytes < 0) {
		if (verbose) {
			#ifdef _WIN32
			printf("setsockopt: %ld\n", WSAGetLastError());
			#else
			perror("recvfrom");
			print_debug(LOG_ERR, "Error: recvfrom failed");
			#endif
		}
		goto out;
	}

	print_debug(LOG_INFO, "received %d bytes packet from src host: %u\n",
			bytes, cliaddr_vmci.svm_cid);

	print_debug(LOG_DEBUG, "Received NMEA: '%s'\n", buf);

	/* write to device */
	if (stat(dev_path, &buf_stat) < 0) {
		perror("stat");
		print_debug(LOG_ERR, "Error: stat failed: %s does not exist",
				dev_path);
		goto out;
	}

	#ifdef _WIN32
	fp = CreateFile(dev_path, GENERIC_WRITE, 0,
		NULL, OPEN_EXISTING, 0, NULL);
	if (!fp) {
		printf("CreateFile: %d\n", GetLastError());
		goto out;
	}

	COMMTIMEOUTS timeouts;
	OVERLAPPED osWrite = {0};

	/* milliseconds per charactcer */
	timeouts.WriteTotalTimeoutMultiplier = 1;
	/* milliseconds added to product of bytes and multiplier */
	timeouts.WriteTotalTimeoutConstant = 1;

	if (SetCommTimeouts(fp, &timeouts) == 0)
		printf("WriteFile: %d\n", GetLastError());

	if (WriteFile(fp, buf, bytes, NULL, &osWrite) == 0)
		printf("WriteFile: %d\n", GetLastError());

	if (WriteFile(fp, "\n", 1, NULL, &osWrite) == 0)
		printf("WriteFile: %d\n", GetLastError());

	CloseHandle(fp);

	#else
	fp = fopen(dev_path, "w");
	if (!fp) {
		perror("fopen");
		goto out;
	} else {
		ret = fprintf(fp, "%s\n", buf);
		if (ret == EOF) {
			perror("fprintf");
			print_debug(LOG_ERR, "Error: couldnt open %s\n", dev_path);
		}
		fclose(fp);
	}
	#endif

	/* check if we should stop moving based on land or sea */
	if (check_position && strncmp(buf, "$GPRMC", 6) == 0) {
		process_rmc(buf);
		if (velocity != 0) {
			if (verbose) {
				printf("velocity: %f\n", velocity);
			}
			ret = check_if_sea(lat, lon);
			if (ret < 0) {
				print_debug(LOG_ERR,
						"Error: check_if_sea failed\n");
			} else if (land && ret) {
				send_stop();
			} else if (sea && !ret) {
				send_stop();
			}
		}
	}
	/* TODO: check for crash if altitude < elevation */
out:
	if (verbose)
		printf("#################### master recv done #####################\n");
}

/***
 *      @brief send status message to wmasterd at some interval
 *	this is a heartbeat to let wmasterd know we are still since gelled
 *	rarely sends data back to wmasterd
 *      @return void
 */
void *send_status(void *arg)
{
	/* send up notification to wmasterd */
	char *msg;
	int msg_len;
	int bytes;

	/* TODO: add debug detail to message. wmastered would require an
	 * adjustment to the size check it performs. this info could be:
	 */

	msg_len = 2;
	msg = malloc(msg_len);
	memset(msg, 0, msg_len);
	snprintf(msg, msg_len, "UP");

	while (running) {

		bytes = sendto(sockfd, msg, msg_len, 0,
				(struct sockaddr *)&servaddr_vmci,
				sizeof(struct sockaddr));

		/* this should be 8 bytes */
		if (bytes != msg_len) {
			perror("sendto");
			print_debug(LOG_ERR, "Up notification failed");
		} else {
			print_debug(LOG_DEBUG, "Up notification sent to wmasterd\n");
		}
		sleep(10);
	}

	free(msg);

	return ((void *)0);
}

/**
 *      @brief main function
 */
int main(int argc, char *argv[])
{
	int af;
	int opt;
	int ret;
	int long_index;
	char *msg;
	int msg_len;
	int bytes;
	int err;
	int ioctl_fd;

	verbose = 0;
	running = 1;
	af = 0;
	long_index = 0;
	#ifdef _WIN32
	dev_path = "COM2";
	#else
	dev_path = "/dev/ttyUSB0";
	#endif
	mapserver = "127.0.0.1";
	sea = 0;
	land = 0;
	err = 0;
	ioctl_fd = 0;
	loglevel = -1;

	static struct option long_options[] = {
		{"help",	no_argument, 0, 'h'},
		{"version",     no_argument, 0, 'V'},
		{"verbose",     no_argument, 0, 'v'},
		{"debug",       required_argument, 0, 'D'},
		{"device",	required_argument, 0, 'd'},
		{"land",	no_argument, 0, 'l'},
		{"sea",		no_argument, 0, 's'},
		{"mapserver",	required_argument, 0, 'm'},
	};

	while ((opt = getopt_long(argc, argv, "hVvlsm:d:D:", long_options,
			&long_index)) != -1) {
		switch (opt) {
		case 'h':
			show_usage(EXIT_SUCCESS);
			break;
		case 'V':
			/* allow help2man to read this */
			setbuf(stdout, NULL);
			printf("gelled version %s\n", VERSION_STR);
			_exit(EXIT_SUCCESS);
			break;
		case 'd':
			dev_path = optarg;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'D':
			loglevel = atoi(optarg);
			printf("gelled: syslog level set to %d\n", loglevel);
			break;
		case 's':
			sea = 1;
			break;
		case 'l':
			land = 1;
			break;
		case 'm':
			mapserver = optarg;
			break;
		case '?':
			printf("Error - No such option: `%c'\n\n",
				optopt);
			show_usage(EXIT_FAILURE);
			break;
		}

	}
	if (optind < argc)
		show_usage(EXIT_FAILURE);

	#ifndef _WIN32
	if (loglevel >= 0)
		openlog("gelled", LOG_PID, LOG_USER);
	#endif

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
		print_debug(LOG_ERR, "could not open /dev/vsock\n");
		_exit(EXIT_FAILURE);
	}
	err = ioctl(ioctl_fd, IOCTL_VM_SOCKETS_GET_LOCAL_CID, &cid);
	if (err < 0) {
		perror("ioctl: Cannot get local CID");
		print_debug(LOG_ERR, "could not get local CID");
	} else {
		printf("CID: %u\n", cid);
	}
	#endif

	if (sea || land)
		check_position = 1;
	else
		check_position = 0;

	/* Handle signals */
	signal(SIGINT, (void *)signal_handler);
	signal(SIGTERM, (void *)signal_handler);
	signal(SIGQUIT, (void *)signal_handler);
	signal(SIGHUP, SIG_IGN);

	#ifdef _WIN32
	WSAStartup(MAKEWORD(1,1), &wsa_data);
	#endif

	/* setup vmci client socket */
	sockfd = socket(af, SOCK_DGRAM, 0);
	/* we can initalize this struct because it never changes */
	memset(&servaddr_vmci, 0, sizeof(servaddr_vmci));
	servaddr_vmci.svm_cid = VMADDR_CID_HOST;
	servaddr_vmci.svm_port = SEND_PORT_G;
	servaddr_vmci.svm_family = af;
	if (sockfd < 0) {
		perror("socket");
		print_debug(LOG_ERR, "failed to bind client socket");
		_exit(EXIT_FAILURE);
	}

	/* create vmci server socket */
	myservfd = socket(af, SOCK_DGRAM, 0);

	/* we can initalize this struct because it never changes */
	memset(&myservaddr_vmci, 0, sizeof(myservaddr_vmci));
	myservaddr_vmci.svm_cid = VMADDR_CID_ANY;
	myservaddr_vmci.svm_port = RECV_PORT_G;
	myservaddr_vmci.svm_family = af;
	ret = bind(myservfd, (struct sockaddr *)&myservaddr_vmci,
		sizeof(struct sockaddr));

	if (ret < 0) {
		perror("bind");
		close(sockfd);
		print_debug(LOG_ERR, "failed to bind server socket");
		_exit(EXIT_FAILURE);
	}

	/* send up notification to wmasterd */
	msg_len = 2;
	msg = malloc(msg_len);
	memset(msg, 0, msg_len);
	snprintf(msg, msg_len, "UP");

	bytes = sendto(sockfd, msg, msg_len, 0,
			(struct sockaddr *)&servaddr_vmci,
			sizeof(struct sockaddr));

	free(msg);

	/* this should be 2 bytes */
	if (bytes != msg_len) {
		perror("sendto");
		print_debug(LOG_ERR, "Up notification failed");
	} else {
		print_debug(LOG_DEBUG, "Up notification sent to wmasterd\n");
	}

	if (verbose)
		printf("################################################################################\n");

	/* start thread to transmit up status message to wmasterd */
	ret = pthread_create(&status_tid, NULL, send_status, NULL);
	if (ret < 0) {
		perror("pthread_create");
		print_debug(LOG_ERR, "error: pthread_create send_status");
		running = 0;
	}

	/* We wait for incoming msg*/
	while (running) {
		recv_from_master();
	}

	/* code below here executes after signal */

	print_debug(LOG_DEBUG, "Shutting down...\n");

	pthread_cancel(status_tid);
	
	pthread_join(status_tid, NULL);

	print_debug(LOG_DEBUG, "Threads have been cancelled\n");

	close(sockfd);
	close(myservfd);

	print_debug(LOG_DEBUG, "Sockets have been closed\n");

	print_debug(LOG_DEBUG, "Memory has been cleared\n");

	print_debug(LOG_NOTICE, "Exiting");

	_exit(EXIT_SUCCESS);
}

