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
#include <dirent.h>
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
#define WMASTERD_PORT		1111
/** Port used to receive NMEA sentences from wmasterd */
#define WMASTERD_PORT_GELLED	3333
/** Buffer size for datagrams */
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

/** address family for mockfd */
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
/** FD for vmci receive */
int myservfd;
/** sockaddr_vm for vmci send */
struct sockaddr_vm servaddr_vm;
/** sockaddr_vm for ipv4 send */
struct sockaddr_in servaddr_in;
/** sockaddr_vm for vmci receive */
struct sockaddr_vm myservaddr_vm;
/** sockaddr_vm for ipv4 receive */
struct sockaddr_in myservaddr_in;
/** server address for OSM tiles */
char *mapserver;
/** device mask for write */
char *dev_mask;
/** device path for write */
char dev_path[256];
/** for tracking our VMCI CID and sending in velocty change */
int cid;
/** whether our gps device needs to stay at sea */
int water;
/** whether our gps device needs to stay on land */
int land;
/** whether or not we want to check land or sea position */
int check_position;
/** thread ID for keepalive thread */
pthread_t status_tid;
/** for the desired log level */
int loglevel;
/** the radio id to receive location info from */
int radio_id;
/** netns of this process */
long int mynetns;
/** whether this process is inside a netns */
int inside_netns;

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

	printf("Usage: gelled [-hVv] [-r<radio_id>] [-d<device>] [-m<url>] [-w|-l]\n\n");

	printf("Options:\n");
	printf("  -h, --help		print this help and exit\n");
	printf("  -V, --version		print version and exit\n");
	printf("  -v, --verbose		verbose output\n");
	printf("  -r, --radio		radio id\n");
	printf("  -D, --debug		debug level for syslog\n");
	printf("  -l, --land		gps should only travel on land\n");
	printf("  -w, --water		gps should only travel on water\n");
	printf("  -d, --devicemask	serial device to write NMEA GPS data\n");
	printf("  -s, --server		wmasterd server address\n");
	printf("  -p, --port		wmasterd server port\n");
	printf("  -m, --mapserver	use this server for map tiles\n\n");

	printf("Copyright (C) 2015-2024 Carnegie Mellon University\n\n");
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
 *      @brief Signal handler which causes program to exit
 *      @return void - calls exit()
 */
void signal_handler(void)
{
	print_debug(LOG_INFO, "signal handler invoked");

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
			print_debug(LOG_ERR, "failed to open %s for writing", filename);
			return -1;
		}

		if (strncmp("http", mapserver, 4) == 0) {
			snprintf(url, 1024, "%s/%d/%d/%d.png",
					mapserver, zoom, x, y);
		} else {
			snprintf(url, 1024, "http://%s/%d/%d/%d.png",
					mapserver, zoom, x, y);
		}

		print_debug(LOG_DEBUG, "downloading tile from %s", url);

		curl_easy_setopt(curl, CURLOPT_URL, url);
		if (verbose) {
			curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
		}
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, png_write);
		curl_easy_setopt(curl, CURLOPT_USERAGENT, "curl/8.0.1");
		res = curl_easy_perform(curl);
		curl_easy_cleanup(curl);
		fclose(png_write);
		if (res != CURLE_OK) {
			print_debug(LOG_ERR, "curl failed to retrieve tile");
			print_debug(LOG_DEBUG, "%s", curl_easy_strerror(res));
			return -1;
		}
	} else {
		print_debug(LOG_ERR, "failed to initialize curl");
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
		print_debug(LOG_ERR, "failed to open %s for reading", filename);
		return -1;
	}

	png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png) {
		print_debug(LOG_ERR, "failed to create read struct for png");
		fclose(png_read);
		return -1;
	}

	info = png_create_info_struct(png);
	if (!info) {
		print_debug(LOG_ERR, "failed to create info struct for png");
		png_destroy_read_struct(&png, NULL, NULL);
		fclose(png_read);
		return -1;
	}

	if (setjmp(png_jmpbuf(png))) {
		print_debug(LOG_ERR, "a png function failed");
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
	int ret = -1;

	if (red == 181 && green == 208 && blue == 208) {
		/* sea, blue */
		ret = 1;
	} else if (red == 170 && green == 211 && blue == 223) {
		/* sea, blue */
		ret = 1;
	} else if (red == 181 && green == 253 && blue == 253) {
		/* boundary, purple, shade 1 */
		ret = -1;
	} else if (red == 181 && green == 92 && blue == 162) {
		/* boundary, purple, shade 2 */
		ret = -1;
	} else if (red == 166 && green == 169 && blue == 192) {
		/* boundary, purple */
		ret = -1;
	} else if (red == 170 && green == 170 && blue == 170) {
		/* gray like a dock at a port */
		ret = -1;
	} else if (red == 181 && green == 213 && blue == 213) {
		/* text */
		ret = -1;
	} else if (red == 181 && green == 174 && blue == 174) {
		/* text */
		ret = -1;
	} else if (red == 181 && green == 105 && blue == 252) {
		/* blue boundary */
		ret = -1;
	} else if (red == 181 && green == 122 && blue == 245) {
		/* boundary */
		ret = -1;
	} else if (red == 181 && green == 110 && blue == 249) {
		/* boundary */
		ret = -1;
	} else if (red == 181 && green == 157 && blue == 187) {
		/* text */
		ret = -1;
	} else if (red == 181 && green == 104 && blue == 166) {
		/* boundary? */
		ret = -1;
	} else if (red == 181 && green == 102 && blue == 253) {
		/* boundary? */
		ret = -1;
	} else if (red == 197 && green == 216 && blue == 214) {
		/* boundary with land? */
		ret = 0;
	} else if (red == 241 && green == 238 && blue == 232) {
		/* boundary with land? */
		ret = 0;
	} else if (red == 217 && green == 208 && blue == 201) {
		/* grey like a building */
		ret = 0;
	} else if (red == 181 && green == 238 && blue == 232) {
		/* land */
		ret = 0;
	} else if (red == 242 && green == 239 && blue == 233) {
		/* land */
		ret = 0;
	}

	if (ret == 0) {
		print_debug(LOG_DEBUG, "This tile is land");
	} else if (ret == 1) {
		print_debug(LOG_DEBUG, "This tile is water");
	} else {
		print_debug(LOG_INFO, "Unkown tile: Red: %d, Green: %d, Blue: %d", red, green, blue);
	}
	return ret;
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
	char pngfile[256];
	char zoomdir[256];
	char xdir[256];
	struct stat statbuf;
	char *cachedir = "/tmp/gelled";
	int exists;

	x = ((lon + 180) / 360) * pow(2, ZOOM);
	xtile = (int)floor(x);
	xpixel = (int)floor((x - xtile) * 256);
	y = (1 - log(tan(DEG2RAD(lat))
		+ 1 / cos(DEG2RAD(lat))) / M_PI) / 2 * pow(2, ZOOM);
	ytile = (int)floor(y);
	ypixel = (int)floor((y - ytile) * 256);

	snprintf(pngfile, 255, "%s/%d/%d/%d.png", cachedir, ZOOM, xtile, ytile);
	print_debug(LOG_DEBUG, "pngfile %s", pngfile);

#ifndef _WIN32
	if (mkdir(cachedir, 0755) && errno != EEXIST) {
		print_debug(LOG_ERR, "cannot create dir %s", cachedir);
		return -1;
	}

	snprintf(zoomdir, 255, "%s/%d", cachedir, ZOOM);
	if (mkdir(zoomdir, 0755) && errno != EEXIST) {
		print_debug(LOG_ERR, "cannot create dir %s", zoomdir);
		return -1;
	}

	snprintf(xdir, 255, "%s/%d/%d", cachedir, ZOOM, xtile);
   	if (mkdir(xdir, 0755) && errno != EEXIST) {
		print_debug(LOG_ERR, "cannot create dir %s", xdir);
		return -1;
	}
#else
	if (mkdir(cachedir) && errno != EEXIST) {
		print_debug(LOG_ERR, "cannot create dir %s", cachedir);
		return -1;
	}

	snprintf(zoomdir, 255, "%s/%d", cachedir, ZOOM);
	if (mkdir(zoomdir) && errno != EEXIST) {
		print_debug(LOG_ERR, "cannot create dir %s", zoomdir);
		return -1;
	}

	snprintf(xdir, 255, "%s/%d/%d", cachedir, ZOOM, xtile);
	if (mkdir(xdir) && errno != EEXIST) {
		print_debug(LOG_ERR, "cannot create dir %s", xdir);
		return -1;
	}
#endif

	stat(pngfile, &statbuf);
	if (!S_ISREG(statbuf.st_mode)) {
		if (download_osm_tile(xtile, ytile, ZOOM, pngfile)) {
			print_debug(LOG_ERR, "could not download tile");
			return -1;
		}
	} else {
		print_debug(LOG_DEBUG, "file already in cache");
	}

	if (read_png(&pixels, pngfile)) {
		print_debug(LOG_ERR, "could not open tile");
		return -1;
	}

	/* Check pixel color */
	px = &((pixels.row_pointers[ypixel])[xpixel * 4]);
	ret = land_or_sea_check_color(px[0], px[1], px[2]);

	for (i = 0; i < pixels.height; i++) {
		free(pixels.row_pointers[i]);
	}

	free(pixels.row_pointers);

	return ret;
}

#ifndef _WIN32
/**
 *	@brief stop movement if we crashed
 *      sends velocity change of 0 to wmasterd
 *      to be used when we move from land to sea or sea to land
 *      this is basically gelled-ctrl
 *	TODO: adjust altitude if we have a plane crash
 */
void send_stop(int radio)
{
	if (verbose) {
		print_debug(LOG_INFO, "stopping the radio %d", radio);
	}

	pid_t pid;
	struct stat buf_stat;
	char *args[10];
	char radio_str[6];
	char port_str[6];
	int index;

	snprintf(radio_str, 5, "%d", radio);
	snprintf(port_str, 5, "%d", port);

	for (index = 0; index < 10; index++) {
		args[index] = NULL;
	}
	index = 0;

	args[0] = "gelled-ctrl";
	args[1] = "-k";
	args[2] = "0";
	args[3] = "-r";
	args[4] = (char *)&radio_str;
	args[5] = "-P";
	args[6] = (char *)&port_str;
	index = 6;
	if (!vsock) {
		args[7] = "-s";
		args[8] = inet_ntoa(wmasterd_address);
		index = 8;
	}
	if (verbose) {
		index++;
		args[index] = "-v";
	}

	// TODO use popen for windows
	/* fork */
	pid = fork();

	if (pid == -1) {
		perror("fork");
		print_debug(LOG_ERR, "could not fork");
	} else if (pid == 0) {
		/* child */
		if (stat("/bin/gelled-ctrl", &buf_stat) == 0) {
			/*
			if (verbose) {
				execl("/bin/gelled-ctrl", "gelled-ctrl",
					"-v", "-k", "0", "-r", radio_str, NULL);
			} else {
				execl("/bin/gelled-ctrl", "gelled-ctrl",
					"-k", "0", "-r", radio_str, NULL);
			}
			*/
			execv("/bin/gelled-ctrl", args);
		} else {
			print_debug(LOG_ERR, "could not find gelled-ctrl");
			_exit(EXIT_FAILURE);
		}
	} else {
		/* parent - wait on child */
		waitpid(pid, NULL, 0);
	}

	return;
}
#endif

/**
 *  @brief Get data from RMC NMEA sentence
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
	int ret;
	double lat;
	double lon;
	double velocity;

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

	if (velocity != 0) {
		if (verbose) {
			printf("- velocity:  %f\n", velocity);
			printf("- latitude:  %f\n", lat);
			printf("- longitude: %f\n", lon);
		}
		ret = check_if_sea(lat, lon);
#ifndef _WIN32
		if (ret < 0) {
			print_debug(LOG_ERR, "check_if_sea failed");
		} else if (land && (ret == 1)) {
			print_debug(LOG_DEBUG, "hit water, stopping");
			send_stop(radio_id);
		} else if (water && (ret == 0)) {
			print_debug(LOG_DEBUG, "hit land, stopping");
			send_stop(radio_id);
		}
#endif
	}
}

void send_notification(int radio_id, int netnsid, int cmd)
{
	if (!sockfd) {
		print_debug(LOG_INFO, "socket %d not yet configured %d", sockfd);
		return;
	}

	if (radio_id < 0 || radio_id > 99) {
		print_debug(LOG_ERR, "send_notification invalid radio %d", radio_id);
		return;
	}

	int bytes;
	int msg_len;
	struct message_hdr hdr = {};

	msg_len = sizeof(struct message_hdr);
	memcpy(hdr.name, "welled", 6);
	strncpy(hdr.version, (const char *)VERSION_STR, 8);
	hdr.src_radio_id = radio_id;
	hdr.len = sizeof(struct message_hdr);
	hdr.netns = netnsid;
	hdr.cmd = cmd;

	if (vsock) {
		bytes = sendto(sockfd, (void *)&hdr, msg_len, 0,
				(struct sockaddr *)&servaddr_vm,
				sizeof(struct sockaddr));
	} else {
		bytes = sendto(sockfd, (void *)&hdr, msg_len, 0,
				(struct sockaddr *)&servaddr_in,
				sizeof(struct sockaddr));
	}
		/* this should be 8 bytes */
	if (bytes != msg_len) {
		perror("sendto");
		print_debug(LOG_ERR, "notification failed for radio %d", radio_id);
	}
}

/**
 *      @brief parse vmci data from wmastered.
 *      @return void
 */
void recv_from_master(void)
{
	char buf[BUFF_LEN];
	struct sockaddr_vm cliaddr_vm;
	struct sockaddr_in cliaddr_in;
	socklen_t addrlen;
	struct timeval tv; /* timer to break us out of the recvfrom function */
	int bytes;
	char *srchost;
	FILE *fp;
	int ret;
	struct stat buf_stat;

	addrlen = sizeof(struct sockaddr);
	memset(&cliaddr_vm, 0, sizeof(cliaddr_vm));
	memset(&cliaddr_in, 0, sizeof(cliaddr_in));
	memset(buf, 0, BUFF_LEN);

	tv.tv_sec = 1;
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
	if (vsock) {
		bytes = recvfrom(myservfd, (char *)buf, BUFF_LEN, 0,
				(struct sockaddr *)&cliaddr_vm, &addrlen);
	} else {
		bytes = recvfrom(myservfd, (char *)buf, BUFF_LEN, 0,
				(struct sockaddr *)&cliaddr_in, &addrlen);
	}
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

	struct message_hdr *hdr = (struct message_hdr *)buf;
	char *nmea = buf + sizeof(struct message_hdr);
	if (strncmp(hdr->name, "gelled", 6) == 0) {
		print_debug(LOG_DEBUG, "gelled %s", hdr->version);
	} else {
		print_debug(LOG_ERR, "invalid message received");
		goto out;
	}

	print_debug(LOG_INFO, "received %d bytes packet from wmasterd for radio: %3d",
			bytes, hdr->dest_radio_id);

	print_debug(LOG_DEBUG, "Received NMEA: '%s'", nmea);

	/* write to device */
	snprintf(dev_path, 256, "%s%d", dev_mask, hdr->dest_radio_id * 2);
	if (stat(dev_path, &buf_stat) < 0) {
		print_debug(LOG_ERR, "Error: stat failed: %s does not exist",
				dev_path);
		goto out;
	}
	print_debug(LOG_DEBUG, "writing to device %s", dev_path);

#ifdef _WIN32
	// TODO determine pattern that would be used
	snprintf(dev_path, 256, "%s%d", dev_mask, hdr->dest_radio_id + 2);
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

	if (WriteFile(fp, nmea, bytes, NULL, &osWrite) == 0)
		printf("WriteFile: %d\n", GetLastError());

	if (WriteFile(fp, "\n", 1, NULL, &osWrite) == 0)
		printf("WriteFile: %d\n", GetLastError());

	CloseHandle(fp);

#else
	fp = fopen(dev_path, "w");
	if (!fp) {
		perror("fopen");
		print_debug(LOG_ERR, "could not open %s", dev_path);
		goto out;
	} else {
		ret = fprintf(fp, "%s\n", nmea);
		if (ret == EOF) {
			perror("fprintf");
			print_debug(LOG_ERR, "could not write to %s", dev_path);
		}
		fclose(fp);
	}
#endif

#ifndef _WIN32
	/* check if we should stop moving based on land or sea */
	if (check_position && (strncmp(nmea, "$GPRMC", 6) == 0)) {
		process_rmc(nmea);
	}
#endif
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
	int msg_len;
	int bytes;
	struct device_node *node;

	/* TODO: add debug detail to message. wmastered would require an
	 * adjustment to the size check it performs. this info could be:
	 *   number of interfaces
	 *   interface mac addresses
	 *   perm_addrs
	 *   state of interfaces (monitor, up, down)
	 */

	while (running) {

		sleep(1);

		struct message_hdr hdr = {};
		msg_len = sizeof(struct message_hdr);
		memcpy(hdr.name, "gelled", 6);
		strncpy(hdr.version, (const char *)VERSION_STR, 8);
		hdr.src_radio_id = radio_id;
		hdr.len = sizeof(struct message_hdr);
		hdr.netns = mynetns;
		hdr.cmd = WMASTERD_UPDATE;

		if (vsock) {
			bytes = sendto(sockfd, (void *)&hdr, msg_len, 0,
					(struct sockaddr *)&servaddr_vm,
					sizeof(struct sockaddr));
		} else {
			bytes = sendto(sockfd, (void *)&hdr, msg_len, 0,
					(struct sockaddr *)&servaddr_in,
					sizeof(struct sockaddr));
		}

		if (bytes != msg_len) {
			perror("sendto");
			print_debug(LOG_ERR, "up notification failed");
		}
		sleep(9);
	}

	print_debug(LOG_DEBUG, "send_status returning");
	return ((void *)0);
}

#ifndef _WIN32
/**
 * 	\brief returns the netns of the current process
*/
int get_mynetns(void)
{
	char *nspath = "/proc/self/ns/net";
	char *pathbuf = calloc(256, 1);
	int len = readlink(nspath, pathbuf, 256);
	if (len < 0) {
		perror("readlink");
		return -1;
	}
	if (sscanf(pathbuf, "net:[%ld]", &mynetns) < 0) {
		perror("sscanf");
		return -1;
	}
	free(pathbuf);
	print_debug(LOG_DEBUG, "mynetns: %ld", mynetns);
	return 0;
}
#endif

/**
 *      @brief main function
 */
int main(int argc, char *argv[])
{
	int opt;
	int ret;
	int long_index;
	int err;
	int ioctl_fd;

	verbose = 0;
	running = 1;
	af = 0;
	long_index = 0;
#ifdef _WIN32
	dev_mask = "COM"; // COM2
#else
	dev_mask = "/dev/ttyUSB"; // /dev/ttyUSB0
#endif
	mapserver = "127.0.0.1";
	water = 0;
	land = 0;
	err = 0;
	ioctl_fd = 0;
	loglevel = -1;
	vsock = 1;
	port = WMASTERD_PORT;
	radio_id = 0;

	static struct option long_options[] = {
		{"help",		no_argument, 0, 'h'},
		{"version",     no_argument, 0, 'V'},
		{"verbose",     no_argument, 0, 'v'},
		{"land",	no_argument, 0, 'l'},
		{"water",	no_argument, 0, 'w'},
		//{"cid",	no_argument, 0, 'c'},
		{"server",	required_argument, 0, 's'},
		{"port",	required_argument, 0, 'p'},
		{"radio",	required_argument, 0, 'r'},
		{"mapserver",	required_argument, 0, 'm'},
		{"debug",		required_argument, 0, 'D'},
		{"device",		required_argument, 0, 'd'}
	};

	while ((opt = getopt_long(argc, argv, "hVvlws:p:m:D:d:r:", long_options,
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
			dev_mask = optarg;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'D':
			loglevel = atoi(optarg);
			printf("gelled: syslog level set to %d\n", loglevel);
			break;
		case 'r':
			radio_id = atoi(optarg);
			break;
		case 'w':
			water = 1;
			break;
		case 'l':
			land = 1;
			break;
		case 'm':
			mapserver = optarg;
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
		case '?':
			printf("Error - No such option: `%c'\n\n",
				optopt);
			show_usage(EXIT_FAILURE);
			break;
		}
	}

	if (optind < argc)
		show_usage(EXIT_FAILURE);

	if (radio_id < 0 || radio_id > 99) {
		printf("Error - radio must be 0-99\n");
		show_usage(EXIT_FAILURE);
	}

#ifndef _WIN32
	if (loglevel >= 0)
		openlog("gelled", LOG_PID, LOG_USER);
#endif

	if (vsock) {
#ifdef _WIN32
		/* old code for vmci_sockets.h */
		af = VMCISock_GetAFValue();
		cid = VMCISock_GetLocalCID();
		print_debug(LOG_INFO, "CID: %d", cid);
#else
		/* new code for vm_sockets */
		af = AF_VSOCK;
		ioctl_fd = open("/dev/vsock", 0);
		if (ioctl_fd < 0) {
			perror("open");
			print_debug(LOG_ERR, "could not open /dev/vsock");
			_exit(EXIT_FAILURE);
		}
		err = ioctl(ioctl_fd, IOCTL_VM_SOCKETS_GET_LOCAL_CID, &cid);
		if (err < 0) {
			perror("ioctl: Cannot get local CID");
			print_debug(LOG_ERR, "could not get local CID");
		} else {
			print_debug(LOG_INFO, "CID: %u", cid);
		}
#endif
	} else {
		af = AF_INET;
	}

	if (water || land) {
		check_position = 1;
	} else {
		check_position = 0;
	}

	inside_netns = 0;

#ifndef _WIN32
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

			if (inode == mynetns) {
				inside_netns = 1;
			} else {
				print_debug(LOG_INFO, "found netns %ld present on system named: %s", inode, file->d_name);
			}
			close(fd);
			free(statbuf);
			free(fullpath);
		}
		closedir(d);
	} else {
		print_debug(LOG_ERR, "cannot open /run/netns");
	}
#endif

	if (inside_netns) {
		print_debug(LOG_INFO, "running inside netns %ld", mynetns);
	} else {
		print_debug(LOG_INFO, "not runnning inside netns");
	}

	/* Handle signals */
	//signal(SIGINT, (void *)signal_handler);
	//signal(SIGTERM, (void *)signal_handler);
	//signal(SIGQUIT, (void *)signal_handler);
	signal(SIGHUP, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);

	#ifdef _WIN32
	WSAStartup(MAKEWORD(1,1), &wsa_data);
	#endif

	/* setup wmasterd details */
	memset(&servaddr_vm, 0, sizeof(servaddr_vm));
	servaddr_vm.svm_cid = VMADDR_CID_HOST;
	servaddr_vm.svm_port = htons(port);
	servaddr_vm.svm_family = af;

	memset(&servaddr_in, 0, sizeof(servaddr_in));
	servaddr_in.sin_addr = wmasterd_address;
	servaddr_in.sin_port = htons(port);
	servaddr_in.sin_family = AF_INET;

	/* setup socket to send data */
	if (vsock) {
		sockfd = socket(af, SOCK_DGRAM, 0);
	} else {
		sockfd = socket(af, SOCK_DGRAM, IPPROTO_UDP);
	}
	if (sockfd < 0) {
		perror("socket");
		_exit(EXIT_FAILURE);
	}

	/* create server socket to receive data */
	if (vsock) {
		myservfd = socket(af, SOCK_DGRAM, 0);
	} else {
		myservfd = socket(af, SOCK_DGRAM, IPPROTO_UDP);
	}

	/* we can initalize this struct because it never changes */
	memset(&myservaddr_vm, 0, sizeof(myservaddr_vm));
	myservaddr_vm.svm_cid = VMADDR_CID_ANY;
	myservaddr_vm.svm_port = htons(WMASTERD_PORT_GELLED);
	myservaddr_vm.svm_family = af;

	memset(&myservaddr_in, 0, sizeof(myservaddr_vm));
	myservaddr_in.sin_addr.s_addr = INADDR_ANY;
	myservaddr_in.sin_port = htons(WMASTERD_PORT_GELLED);
	myservaddr_in.sin_family = AF_INET;

	if (vsock) {
		ret = bind(myservfd, (struct sockaddr *)&myservaddr_vm,
				sizeof(struct sockaddr));
	} else {
		ret = bind(myservfd, (struct sockaddr *)&myservaddr_in,
				sizeof(struct sockaddr));
	}
	if (ret < 0) {
		perror("bind");
		close(sockfd);
		close(myservfd);
		_exit(EXIT_FAILURE);
	}

	/* send up notification to wmasterd */
	send_notification(radio_id, mynetns, WMASTERD_ADD);

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

	print_debug(LOG_DEBUG, "Shutting down...");

	pthread_cancel(status_tid);

	pthread_join(status_tid, NULL);

	print_debug(LOG_DEBUG, "Threads have been cancelled");

	close(sockfd);
	close(myservfd);

	print_debug(LOG_DEBUG, "Sockets have been closed");

	print_debug(LOG_DEBUG, "Memory has been cleared");

	print_debug(LOG_NOTICE, "Exiting");

	_exit(EXIT_SUCCESS);
}
