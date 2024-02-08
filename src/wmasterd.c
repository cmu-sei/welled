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

#define _USE_MATH_DEFINES
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <math.h>
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#ifdef _WIN32
  #ifndef _WIN32_WINNT
    #define _WIN32_WINNT 0x0501  /* Windows XP. */
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include "vmci_sockets.h"
  #include "windows_error.h"
  SERVICE_STATUS g_ServiceStatus = {0};
  SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
  HANDLE g_ServiceStopEvent = INVALID_HANDLE_VALUE;
  HANDLE hThread;
  #define SERVICE_NAME  "wmasterd"
  #define sock_error windows_error
#else
  #include <syslog.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <sys/types.h>
  #include <sys/wait.h>
  #include <sys/utsname.h>
  #include <net/if.h>
  #include <sys/ioctl.h>
  #include <linux/vm_sockets.h>
  #define IOCTL_VMCI_SOCKETS_GET_AF_VALUE    0x7b8
  #define sock_error perror
#endif

#ifdef _ESX
/** ESXi 5.5 and 6.0 do not have 2.24 so we need to link older one */
__asm__(".symver memcpy,memcpy@GLIBC_2.2.5");
#endif

#include "wmasterd.h"

/** whether we are vsock or ip */
int vsock;
/** broadcast address for main interface */
char broadcast_addr[16];
/** Whether to broadcast frames to host subnet */
int broadcast;
/** Port used to receive frames from welled or gelled */
#define WMASTERD_PORT_WELLED       1111
/** Port used to send frames to gelled */
#define WMASTERD_PORT_GELLED	2222
/** Buffer size for UDP datagrams from other hosts */
#define BUFF_LEN	10000
/** Buffer size for VMX config file */
#define LINE_BUF	8192
/** for setting the max number of client connections */
#define MAX_CLIENTS 1000

/** mutex for linked list access */
pthread_mutex_t list_mutex;
/** mutex for cache file access */
pthread_mutex_t file_mutex;
/** thread id for sending nmea */
pthread_t nmea_tid;
/** thread id for receving broadcast frames */
pthread_t hosts_tid;
/** thread id for reading console */
pthread_t console_tid;

/** Whether to print verbose output */
int verbose;
/** Whether to break loop after signal */
int running;
/** Whether we are running on an ESX host */
int esx;
/** Whether to print status on next loop */
int print_status;
/** Whether to check room before transmission */
int check_room;
/** Whether to update room on each frames receipt */
int update_room;
/** Whether to prepand distance to frames */
int send_distance;
/** Whether to send PASHR statements */
int send_pashr;
/** Whether to use a cache file for locations */
int cache;
/** Filename for caching locations */
char *cache_filename;
/** file pointer for cache file */
FILE *cache_fp;
/** for the desired log level */
int loglevel;
/** for tracking client connections */
int client_socket[MAX_CLIENTS];


/** for address family */
int af;
/** for ipv4 server socket */
int welled_sockfd;
/* vmci server socket */
int myservfd;
/** sockaddr_vm for vmci clients */
struct sockaddr_vm servaddr_vm;
/** sockaddr_vm for vmci server */
struct sockaddr_vm myservaddr_vm;
/** sockaddr_in for ip clients */
struct sockaddr_in servaddr_in;
/** sockaddr_in for ip server */
struct sockaddr_in myservaddr_in;
/** for head of linked list */
struct client *head;

#ifdef _WIN32
WSADATA wsa_data;
#endif

/**
 *	@brief Prints the CLI help
 *	@param exval - exit code
 *	@return - void, calls exit()
 */
void show_usage(int exval)
{
	/* allow help2man to read this */
	setbuf(stdout, NULL);

	printf("wmasterd - wireless master daemon\n\n");

	printf("Usage: wmasterd [-hVvbrudni] [-l <port>] [-p <port>] [-D <level>] [-c <file>]\n\n");

	printf("Options:\n");
	printf("  -h, --help			print this help and exit\n");
	printf("  -V, --version			print version and exit\n");
	printf("  -v, --verbose			verbose output\n");
	printf("  -b, --broadcast       broadcast frames to other hosts\n");
	printf("  -r, --no-room-check	do not check room id\n");
	printf("  -u, --update-room     update room on receipt\n");
	printf("  -d, --distance		prepend distance to frames\n");
	printf("  -D, --debug			debug level for syslog\n");
	printf("  -c, --cache			file to save location data\n");
	printf("  -n, --nogps			disable nmea\n");
	printf("  -i, --ipv4			use ipv4\n");
	printf("  -l, --listen-port		listen port\n\n");

	printf("Copyright (C) 2015-2024 Carnegie Mellon University\n\n");
	printf("License GPLv2: GNU GPL version 2 <http://gnu.org/licenses/gpl.html>\n");
	printf("This is free software; you are free to change and redistribute it.\n");
	printf("There is NO WARRANTY, to the extent permitted by law.\n\n");

	printf("Report bugs to <arwelle@cert.org>\n\n");

	exit(exval);
}

void print_debug(int level, char *format, ...)
{
	char buffer[1024];

	if (loglevel < 0) {
		return;
	}
	if (level > loglevel) {
		return;
	}

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
#else
	time_t now;
	struct tm *mytime;
	char timebuff[128];
	time(&now);
	mytime = gmtime(&now);

	if (strftime(timebuff, sizeof(timebuff), "%Y-%m-%dT%H:%M:%SZ", mytime)) {
		printf("%s - wmasterd: %8s: %s\n", timebuff, lev, buffer);
	} else {
		printf("wmasterd: %8s: %s\n", lev, buffer);
	}
#endif
}

/**
 *      convert radians to degrees
 *      @param rad - angle in radians
 *      @return degrees
 */
double radians_to_degrees(double rad) {
	return (rad * 180 / M_PI);
}

/**
 *      convert degrees to radians
 *      @param deg - angle in degrees
 *      @return radians
 */
double degrees_to_radians(double deg) {
	return (deg * M_PI / 180);
}

/**
 *	Convert decimal degrees to decimal minutes
 *
 */
void dec_deg_to_dec_min(float orig, char *dest, int len)
{
	int deg;
	float min;
	char ch;
	char *temp = NULL;

	deg = (int)orig;
	min = (orig - deg) * 60;

	switch (len) {
	// lat
	case 12:
		temp = calloc(len + 1, 1);
		if (deg < 0) {
			ch = 'S';
		} else {
			ch = 'N';
		}
		if (min < 0) {
			min *= -1;
		}
		snprintf(temp, len, "%02d%07.4f,%c",
				abs(deg), min, ch);
		strncpy(dest, temp, len);
		break;
	// lon
	case 13:
		temp = calloc(len + 1, 1);
		if (deg < 0) {
			ch = 'W';
		} else {
			ch = 'E';
		}
		if (min < 0) {
			min *= -1;
		}
		snprintf(temp, len, "%03d%07.4f,%c",
				abs(deg), min, ch);
		strncpy(dest, temp, len);
		break;
	}
	free(temp);
	return;
}

/**
 *	@brief updates node information stored in cache file
 *	@param node - node to be updated
 */
void update_cache_file_info(struct client *node)
{
	print_debug(LOG_DEBUG, "update_cache_file_info");

	char buf[1024];
	unsigned int address;
	int ret;
	int line_num;
	long pos;
	int matched;
	struct in_addr ip;
	char ip_address[16];
	int radio_id;
	struct in_addr ipmatch;

	address = node->address;
	ip.s_addr = node->address;

	if (node == NULL) {
		return;
	}
	if (!cache) {
		return;
	}
	pthread_mutex_lock(&file_mutex);

	fseek(cache_fp, 0, SEEK_SET);
	pos = 0;
	line_num = 0;
	matched = 0;

	while ((fgets(buf, 1024, cache_fp)) != NULL) {
		if (vsock) {
			ret = sscanf(buf, "%u %d", &address, &radio_id);
		} else {
			ret = sscanf(buf, "%s %d", ip_address, &radio_id);
			inet_pton(AF_INET, ip_address, &ipmatch);
		}
		// TODO handle ip.. should this not look for 2?
		if (ret != 2) {
			remove_newline(buf);
			print_debug(LOG_ERR, "did not parseline for '%s'", buf);
		} else if (!vsock && (ip.s_addr == ipmatch.s_addr) && (radio_id == node->radio_id)) {
			matched = 1;
			break;
		} else if (vsock && (address == node->address) && (radio_id == node->radio_id)) {
			matched = 1;
			break;
		}
		if (verbose) {
			printf("'%s'\n", buf);
		}
		line_num++;
		pos = ftell(cache_fp);
	}
	/* end of file reached */
	char *format_ipv4 = "%-16s %-5d %-6d %-5d %-5d %-36s %-4d %-9.6f %-10.6f %-6.0f %-8.2f %-6.2f %-6.2f %-s\n";
	char *format_vsock = "%-16u %-5d %-6d %-5d %-5d %-36s %-4d %-9.6f %-10.6f %-6.0f %-8.2f %-6.2f %-6.2f %-s\n";

	/* check for match */
	if (matched) {
		fseek(cache_fp, pos, SEEK_SET);

		if (vsock) {
			fprintf(cache_fp,
				format_vsock,
				node->address, node->radio_id, node->room,
				node->loc.latitude, node->loc.longitude,
				node->loc.altitude,
				node->loc.velocity,
				node->loc.heading,
				node->loc.pitch,
				node->name);
		} else {
			fprintf(cache_fp,
				format_ipv4,
				inet_ntoa(ip), node->radio_id, node->room,
				node->loc.latitude, node->loc.longitude,
				node->loc.altitude,
				node->loc.velocity,
				node->loc.heading,
				node->loc.pitch,
				node->name);
		}
		fflush(cache_fp);
	} else {
		if (vsock) {
			print_debug(LOG_ERR, "no match for node %d radio %d in cache file", node->address, node->radio_id);
		} else {
			print_debug(LOG_ERR, "no match for node %s radio %d in cache file", inet_ntoa(ip), node->radio_id);
		}
	}

	pthread_mutex_unlock(&file_mutex);
}

/**
 *  remove newline from a line of text to print better on console
 *  @param str - string to be modified
*/
void remove_newline(char *str) {
    int len = strnlen(str, 1024);
    if (len > 0 && str[len - 1] == '\n') {
        str[len - 1] = '\0';
    } else {
		printf("last char is '%c", str[len - 1]);
	}
}

/**
 *	update the cache file which stores location data
 */
void update_cache_file_location(struct client *node)
{
	print_debug(LOG_DEBUG, "update_cache_file_location");

	char buf[1024];
	unsigned int address;
	int ret;
	int line_num;
	long pos;
	int matched;
	char ip_address[16];
	struct in_addr ip;
	int radio_id;
	struct in_addr ipmatch;

	address = node->address;
	ip.s_addr = node->address;

	if (!cache) {
		return;
	}
	if (node == NULL) {
		return;
	}

	pthread_mutex_lock(&file_mutex);

	fseek(cache_fp, 0, SEEK_SET);
	pos = 0;
	line_num = 0;
	matched = 0;

	while ((fgets(buf, 1024, cache_fp)) != NULL) {
		if (vsock) {
			ret = sscanf(buf, "%u %d", &address, &radio_id);
		} else {
			ret = sscanf(buf, "%s %d", ip_address, &radio_id);
			inet_pton(AF_INET, ip_address, &ipmatch);
		}
		if (ret != 2) {
			remove_newline(buf);
			print_debug(LOG_ERR, "did not parseline for '%s'", buf);
		} else if (!vsock && (ip.s_addr == ipmatch.s_addr) && (radio_id == node->radio_id)) {
			matched = 1;
			break;
		} else if (vsock && (address == node->address)) {
			matched = 1;
			break;
		}
		if (verbose) {
			printf("'%s'\n", buf);
		}
		line_num++;
		pos = ftell(cache_fp);
	}
	/* wmasterd: end of file reached */
	char *format_ipv4 = "%-16s %-5d %-6d %-5d %-5d %-36s %-4d %-9.6f %-10.6f %-6.0f %-8.2f %-6.2f %-6.2f %-s\n";
	char *format_vsock = "%-16u %-5d %-6d %-5d %-5d %-36s %-4d %-9.6f %-10.6f %-6.0f %-8.2f %-6.2f %-6.2f %-s\n";

	/* check for match */
	if (matched) {
		fseek(cache_fp, pos, SEEK_SET);

		if (vsock) {
			fprintf(cache_fp,
				format_vsock,
				node->address, node->radio_id, node->room,
				node->loc.latitude, node->loc.longitude,
				node->loc.altitude, node->loc.velocity,
				node->loc.heading, node->loc.pitch,
				node->name);
		} else {
			fprintf(cache_fp,
				format_ipv4,
				inet_ntoa(ip), node->radio_id, node->room,
				node->loc.latitude, node->loc.longitude,
				node->loc.altitude, node->loc.velocity,
				node->loc.heading, node->loc.pitch,
				node->name);
		}
		fflush(cache_fp);
	} else {
		if (vsock) {
			print_debug(LOG_ERR, "no match for node %d radio %d in cache file", node->address, node->radio_id);
		} else {
			print_debug(LOG_ERR, "no match for node %s radio %d in cache file", inet_ntoa(ip), node->radio_id);
		}
	}

	pthread_mutex_unlock(&file_mutex);
}

/**
 *	find and update follows of this vm by name
 */
void update_followers(struct client *node)
{
	struct client *curr;

	if (node == NULL) {
		return;
	}
	curr = head;

	while (curr != NULL) {
		/* name is 1024 in header file - caution */
		if ((strnlen(curr->loc.follow, FOLLOW_LEN) > 0) &&
				(strncmp(curr->loc.follow, node->name,
					FOLLOW_LEN - 1) == 0)) {
			curr->loc.latitude = node->loc.latitude;
			curr->loc.longitude = node->loc.longitude;
			curr->loc.altitude = node->loc.altitude;
			curr->loc.velocity = node->loc.velocity;
			curr->loc.heading = node->loc.heading;
			curr->loc.pitch = node->loc.pitch;
			strncpy(curr->loc.nmea_zda,
				node->loc.nmea_zda, NMEA_LEN);
			strncpy(curr->loc.nmea_gga, node->loc.nmea_gga,
				NMEA_LEN);
			strncpy(curr->loc.nmea_rmc, node->loc.nmea_rmc,
				NMEA_LEN);
			if (send_pashr) {
				strncpy(curr->loc.nmea_pashr, node->loc.nmea_rmc,
					NMEA_LEN);
			}
			update_cache_file_location(curr);
			print_debug(LOG_NOTICE, "follower %s synced to master %s",
					curr->name, curr->loc.follow);
		}
		curr = curr->next;
	}
	return;
}

/**
 *	Updates the location of a node
 *	should be called when i get a new set of coordinates
 *	or a new nmea sentence from the gelled udp socket
 *	which is not yet implemented
 */
void update_node_location(struct client *node, struct update_2 *data)
{
	/*
	 * use velocity to update coordinates
	 * loc struct may not need to passed if just using velocity
	 * velocity: 1 knot = 1.15078 mph
	 * this is not that simple because i need to convert the
	 * format of the lat and long from degrees....
	 * lat and lon are relative angular position from
	 * equator and prime meridian
	 * 1 degree = 69 miles
	 * 1 minute = 6072 feet
	 * 1 second = 101.2 feet
	 * gps uses degrees/decimal minutes system with minutes dvided
	 * by decimal instead of using seconds with are 1/60 of a minute
	 */

	double r;
	double dy;
	double dx;
	double dv;
	float angle;
	float ms;
	float overage;
	struct in_addr ip;
	ip.s_addr = node->address;

	/* radius of earth in meters */
	r = 6378137;

	/* a passed location means update data */
	if (data) {
		print_debug(LOG_DEBUG, "processing update");

		/* check for follow and set in node */
		if (strnlen(data->follow, FOLLOW_LEN) > 0) {
			print_debug(LOG_DEBUG, "follow exists in update");
			if (strncmp(data->follow, "CLEAR", 5) == 0) {
				if (vsock) {
					print_debug(LOG_NOTICE, "clearing follow on %u radio %d",
							node->address, node->port);
				} else {
					print_debug(LOG_NOTICE, "clearing follow on %s radio %d",
							inet_ntoa(ip), node->port);
				}
				memset(node->loc.follow, 0, FOLLOW_LEN);
			} else {
				strncpy(node->loc.follow,
						data->follow, FOLLOW_LEN - 1);
				if (vsock) {
					print_debug(LOG_NOTICE, "set follow to %s on %u radio %d",
							node->loc.follow, node->address, node->radio_id);
				} else {
					print_debug(LOG_NOTICE, "set follow to %s on %s radio %d",
							node->loc.follow, inet_ntoa(ip), node->radio_id);
				}
			}
		}

		if (strnlen(node->loc.follow, FOLLOW_LEN) > 0) {
			print_debug(LOG_DEBUG, "we need to update a master instead of this node");
			struct client *master = get_node_by_name(node->loc.follow);
			//node = get_node_by_name(node->loc.follow);

			/*
			 * TODO: check windows
			 * it will fail name check unless set
			 */

			if (!master) {
				if (vsock) {
					print_debug(LOG_INFO, "no master for follow on %u radio %d",
							node->address, node->radio_id);
				} else {
					print_debug(LOG_INFO, "no master for follow on %s radio %d",
							inet_ntoa(ip), node->radio_id);
				}
			} else {
				print_debug(LOG_INFO, "node set to follow %s", node->loc.follow);
				print_debug(LOG_DEBUG, "update master instead");
				node = master;
			}
		} else {
			print_debug(LOG_DEBUG, "no follow set");
		}

		if (verbose) {
			printf("old position: %.8f %.8f\n",
					node->loc.latitude, node->loc.longitude);
			printf("old heading:  %f\n", node->loc.heading);
			printf("old velocity: %f\n", node->loc.velocity);
			printf("old altitude: %f\n", node->loc.altitude);
			printf("old pitch:    %f\n", node->loc.pitch);
		}

		/* update velocity */
		if (data->velocity != -1) {
			node->loc.velocity = data->velocity;
		}
		/* update heading */
		if (data->heading != -1) {
			node->loc.heading = data->heading;
		}
		/* update pitch angle */
		if (data->pitch != -1) {
			node->loc.pitch = data->pitch;
		}
		/* update coordinates */
		if ((data->latitude >= -90) &&
				(data->latitude <= 90)) {
			node->loc.latitude = data->latitude;
		}
		if ((data->longitude >= -180) &&
				(data->longitude <= 180)) {
			node->loc.longitude = data->longitude;
		}
		/* update altitude */
		if (data->altitude != -1) {
			node->loc.altitude = data->altitude;
		}
		/* correct bad values */
		if (isnan(node->loc.heading)) {
			node->loc.heading = 0;
		}
		if (isnan(node->loc.velocity)) {
			node->loc.velocity = 0;
		}
		if (isnan(node->loc.altitude)) {
			node->loc.altitude = 0;
		}
		if (isnan(node->loc.pitch)) {
			node->loc.pitch = 0;
		}

		if (verbose) {
			printf("new position: %.8f %.8f\n",
					node->loc.latitude, node->loc.longitude);
			printf("new heading:  %f\n", node->loc.heading);
			printf("new velocity: %f\n", node->loc.velocity);
			printf("new altitude: %f\n", node->loc.altitude);
			printf("new pitch:    %f\n", node->loc.pitch);
		}

		int age = time(NULL) - node->time;

		if (vsock) {
			print_debug(LOG_NOTICE,
					"%-16d %-5d %-36s %-4d %-9.6f %-10.6f %-6.0f %-8.2f %-6.2f %-6.2f %-s",
					node->address, node->port, node->room, age,
					node->loc.latitude, node->loc.longitude,
					node->loc.altitude, node->loc.velocity,
					node->loc.heading, node->loc.pitch,
					node->name);
		} else {
			print_debug(LOG_NOTICE,
					"%-16s %-5d %-36s %-4d %-9.6f %-10.6f %-6.0f %-8.2f %-6.2f %-6.2f %-s",
					inet_ntoa(ip), node->port, node->room, age,
					node->loc.latitude, node->loc.longitude,
					node->loc.altitude, node->loc.velocity,
					node->loc.heading, node->loc.pitch,
					node->name);
		}
		update_cache_file_location(node);
		update_followers(node);

		print_debug(LOG_DEBUG, "updates from data complete");
		return;
	}

	/* followers get updated after master node, skip it here */
	if (strnlen(node->loc.follow, FOLLOW_LEN) > 0) {
		print_debug(LOG_DEBUG, "skipping update of follower %s",
				node->name);
		return;
	}

	/* do not update location if device is not moving */
	if (node->loc.velocity == 0) {
		print_debug(LOG_DEBUG, "skipping update with no velocity %s",
				node->name);
		return;
	}

	/* correct bad values */
	if (isnan(node->loc.heading)) {
		node->loc.heading = 0;
	}
	if (isnan(node->loc.velocity)) {
		node->loc.velocity = 0;
	}
	if (isnan(node->loc.altitude)) {
		node->loc.altitude = 0;
	}
	if (isnan(node->loc.pitch)) {
		node->loc.pitch = 0;
	}

	/* move location */
	angle = node->loc.heading;

	/* meters per second */
	ms = (node->loc.velocity * 1852 / 3600);

	/* convert degrees to radians */
	angle *= M_PI / 180;

	dy = ms * cosf(angle);
	dx = ms * sinf(angle);

	node->loc.latitude += (dy / r) * (180 / M_PI);
	node->loc.longitude += (dx / r) * (180 / M_PI) /
			cosf(node->loc.latitude * (M_PI / 180));

	/* adjust heading, lat and lon as we cross north pole */
	if (node->loc.latitude > 90) {
		/* print_debug(LOG_INFO, "crossing north pole"); */
		overage = node->loc.latitude - 90;
		node->loc.latitude = 90 - overage;
		if (node->loc.heading < 90) {
			node->loc.heading += 180;
		} else if (node->loc.heading > 270) {
			node->loc.heading -= 180;
		}
		if (node->loc.longitude < 0) {
			node->loc.longitude += 180;
		} else {
			node->loc.longitude -= 180;
		}
	}

	/* adjust heading, lat and lon as we cross south pole */
	if (node->loc.latitude < -90) {
		/* print_debug(LOG_INFO, "crossing south pole"); */
		overage = node->loc.latitude + 90;
		node->loc.latitude = -90 - overage;
		if (node->loc.heading > 90) {
			node->loc.heading += 180;
		} else if (node->loc.heading < 270) {
			node->loc.heading -= 180;
		}
		if (node->loc.longitude < 0) {
			node->loc.longitude += 180;
		} else {
			node->loc.longitude -= 180;
		}
	}

	/* adjust longitude as we cross east to west */
	if (node->loc.longitude > 180) {
		/* print_debug(LOG_INFO, "crossing prime meridian"); */
		node->loc.longitude -= 360;
	}
	/* adjust longitude as we cross west to east */
	if (node->loc.longitude < -180) {
		/* print_debug(LOG_INFO, "crossing dateline"); */
		node->loc.longitude += 360;
	}

	/* keep heading less than 360 */
	if (node->loc.heading >= 360)
		node->loc.heading -= 360;

	/* update altitude */
	if (node->loc.pitch) {
		angle = node->loc.pitch;
		angle *= M_PI / 180;
		dv = ms * sinf(angle);
		node->loc.altitude += dv;
	}

	int age = time(NULL) - node->time;
	if (vsock) {
		print_debug(LOG_NOTICE,
				"%-16d %-5d %-36s %-4d %-9.6f %-10.6f %-6.0f %-8.2f %-6.2f %-6.2f %-s",
				node->address, node->port, node->room, age,
				node->loc.latitude, node->loc.longitude,
				node->loc.altitude, node->loc.velocity,
				node->loc.heading, node->loc.pitch,
				node->name);
	} else {
		print_debug(LOG_NOTICE,
				"%-16s %-5d %-36s %-4d %-9.6f %-10.6f %-6.0f %-8.2f %-6.2f %-6.2f %-s",
				inet_ntoa(ip), node->port, node->room, age,
				node->loc.latitude, node->loc.longitude,
				node->loc.altitude, node->loc.velocity,
				node->loc.heading, node->loc.pitch,
				node->name);
	}

	update_cache_file_location(node);
	update_followers(node);

	return;
}

/**
 *	This function calcluates an NMEA checksum value
 *	@param input - nmea sentence
 *	@return - checksum
 */
unsigned int nmea_checksum(char *input)
{
	char c;
	unsigned int checksum;
	int len;
	int i;

	checksum = 0;
	len = strlen(input);

	for (i = 0; i < len; i++) {
		strncpy(&c, input + i, 1);
		checksum = checksum ^ c;
	}

	return checksum;
}

/**
 *	Creates an NMEA sentence based on a struct location
 *	@param node - struct client for node
 *	@return - pointer to new NMEA sentence
 */
void create_new_sentences(struct client *node)
{
	time_t now;
	struct tm *tmp;
	char timestamp[10];
	char date[11];
	char rmc_date[7];
	char temp[NMEA_LEN];
	unsigned int checksum;
	char lat[12];
	char lon[13];

	memset(node->loc.nmea_zda, 0, NMEA_LEN);
	memset(node->loc.nmea_gga, 0, NMEA_LEN);
	memset(node->loc.nmea_rmc, 0, NMEA_LEN);
	memset(node->loc.nmea_pashr, 0, NMEA_LEN);

/*
	memset(node->loc.nmea_gsa, 0, NMEA_LEN);
	memset(node->loc.nmea_gsv1, 0, NMEA_LEN);
	memset(node->loc.nmea_gsv2, 0, NMEA_LEN);
	memset(node->loc.nmea_gsv3, 0, NMEA_LEN);
*/

	/* we need the current time  to format sentences */
	now = time(NULL);
	tmp = localtime(&now);

	if (tmp == NULL) {
		perror("wmasterd: localtime");
	} else {
		strftime(timestamp, 10, "%H%M%S.00", tmp);
		strftime(date, 11, "%d,%m,%Y", tmp);
		strftime(rmc_date, 7, "%d%m%y", tmp);
	}

	update_node_location(node, NULL);

/*
$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47
Where:
     GGA	  Global Positioning System Fix Data
     123519       Fix taken at 12:35:19 UTC
     4807.038,N   Latitude 48 deg 07.038' N
     01131.000,E  Longitude 11 deg 31.000' E
     1	    Fix quality: 0 = invalid
			       1 = GPS fix (SPS)
			       2 = DGPS fix
			       3 = PPS fix
			       4 = Real Time Kinematic
			       5 = Float RTK
			       6 = estimated (dead reckoning) (2.3 feature)
			       7 = Manual input mode
			       8 = Simulation mode
     08	   Number of satellites being tracked
     0.9	  Horizontal dilution of position
     545.4,M      Altitude, Meters, above mean sea level
     46.9,M       Height of geoid (mean sea level) above WGS84
		      ellipsoid
     (empty field) time in seconds since last DGPS update
     (empty field) DGPS station ID number
     *47	  the checksum data, always begins with *


  $GPGSA,A,3,04,05,,09,12,,,24,,,,,2.5,1.3,2.1*39

Where:
     GSA      Satellite status
     A	Auto selection of 2D or 3D fix (M = manual)
     3	3D fix - values include: 1 = no fix
				       2 = 2D fix
				       3 = 3D fix
     04,05... PRNs of satellites used for fix (space for 12)
     2.5      PDOP (dilution of precision)
     1.3      Horizontal dilution of precision (HDOP)
     2.1      Vertical dilution of precision (VDOP)
     *39      the checksum data, always begins with *

  $GPGSV,2,1,08,01,40,083,46,02,17,308,41,12,07,344,39,14,22,228,45*75

Where:
      GSV	  Satellites in view
      2	    Number of sentences for full data
      1	    sentence 1 of 2
      08	   Number of satellites in view

      01	   Satellite PRN number
      40	   Elevation, degrees
      083	  Azimuth, degrees
      46	   SNR - higher is better
	   for up to 4 satellites per sentence
      *75	  the checksum data, always begins with *


$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A

Where:
     RMC	  Recommended Minimum sentence C
     123519       Fix taken at 12:35:19 UTC
     A	    Status A=active or V=Void.
     4807.038,N   Latitude 48 deg 07.038' N
     01131.000,E  Longitude 11 deg 31.000' E
     022.4	Speed over the ground in knots
     084.4	Track angle in degrees True
     230394       Date - 23rd of March 1994
     003.1,W      Magnetic Variation
     *6A	  The checksum data, always begins with *

  $GPZDA,hhmmss.ss,dd,mm,yyyy,xx,yy*CC
  $GPZDA,201530.00,04,07,2002,00,00*60

where:
	hhmmss    HrMinSec(UTC)
	dd,mm,yyy Day,Month,Year
	xx	local zone hours -13..13
	yy	local zone minutes 0..59
	*CC       checksum

PASHR
	https://docs.novatel.com/OEM7/Content/SPAN_Logs/PASHR.htm
$PASHR,123816.80,312.95,T, -0.83,-0.42, -0.01,0.234,0.224,0.298,2,1*0B
$PASHR,135920.00,  0.00,T,000.00, 0.00,000.00,0.000,0.000,0.000,0,0*2C

where
$PASHR Proprietary Heading, Roll, Pitch and Heave
HHMMSS.SSS UTC Time encoded as HH (hours, 24 hour format), MM (minutes) and SS.SSS
(seconds with 3 decimal places). This field will be blank until UTC time is
known. GPS time may be known before UTC time is known.
HHH.HH True Heading of the navigation system, from 0 to 359.99 degrees, using 2
decimal places.
T The character ‘T’ is output by the navigation system to represent that the
heading is to true north. Grid north and magnetic north are not output.
RRR.RR Roll of the navigation system, measured in degrees, with leading sign, leading
0’s where needed and 2 decimal places. Positive values mean that the left side
is up.
PPP.PP Pitch of the navigation system, measured in degrees, with leading sign, leading
0’s where needed and 2 decimal places. Positive values mean that the front is
up.
aaa.aa Heave of the navigation system, measured in metres, with leading sign, leading
0’s where needed and 2 decimal places. This is not output by the navigation
system.
r.rrr Roll accuracy in degrees and with 3 decimal places.
p.ppp Pitch accuracy in degrees and with 3 decimal places.
h.hhh Heading accuracy in degrees and with 3 decimal places.
Q1 GPS Position mode.
0 – No GPS position fix
1 – All GPS position fixes apart from “2”
2 – RTK Integer position fix
Q2 IMU Status
0 – IMU is OK
1 – IMU error
*CS Checksum separator and checksum

*/
/*
	snprintf(node->loc.nmea_gsa, NMEA_LEN, "$GPGSA,M,3,21,18,15,24,10,14,27,13,46,,,,1.7,1.0,1.4*35");
	snprintf(node->loc.nmea_gsv1, NMEA_LEN, "$GPGSV,3,1,12,21,70,221,41,18,67,322,44,15,52,050,24,24,49,124,30*7C");
	snprintf(node->loc.nmea_gsv2, NMEA_LEN, "$GPGSV,3,2,12,10,34,297,36,14,17,234,35,27,15,301,33,13,14,049,16*7A");
	snprintf(node->loc.nmea_gsv3, NMEA_LEN, "$GPGSV,3,3,12,46,36,205,37,20,39,094,11,32,64,043,39,04,67,247,*71");
*/

	dec_deg_to_dec_min(node->loc.latitude, lat, 12);
	dec_deg_to_dec_min(node->loc.longitude, lon, 13);

	/* ZDA */
	memset(temp, 0, NMEA_LEN);
	snprintf(temp, NMEA_LEN, "GPZDA,%s,%s,-05,00", timestamp, date);
	checksum = nmea_checksum(temp);
	snprintf(node->loc.nmea_zda, NMEA_LEN, "$%s*%2X", temp, checksum);

	/* RMC */
	memset(temp, 0, NMEA_LEN);
	snprintf(temp, NMEA_LEN, "GPRMC,%s,A,%s,%s,%.2f,%.2f,%s,,,D", timestamp, lat, lon, node->loc.velocity, node->loc.heading, rmc_date);
	checksum = nmea_checksum(temp);
	snprintf(node->loc.nmea_rmc, NMEA_LEN, "$%s*%2X", temp, checksum);

	/* GGA */
	memset(temp, 0, NMEA_LEN);
	snprintf(temp, NMEA_LEN, "GPGGA,%s,%s,%s,2,09,1.0,%05.2f,M,0,M,0,0",
			timestamp, lat, lon, node->loc.altitude);
	checksum = nmea_checksum(temp);
	snprintf(node->loc.nmea_gga, NMEA_LEN, "$%s*%2X", temp, checksum);

	if (send_pashr) {
		/* PASHR */
		memset(temp, 0, NMEA_LEN);
		snprintf(temp, NMEA_LEN, "PASHR,%s,%.2f,T,000.00,%03.2f,000.00,0.000,0.000,0.000,0,0",
				timestamp, node->loc.heading, node->loc.pitch);
		checksum = nmea_checksum(temp);
		snprintf(node->loc.nmea_pashr, NMEA_LEN, "$%s*%2X", temp, checksum);
	}

	return;
}

/**
 *	Thread for NMEA sentence generation and transmission
 */
void *produce_nmea(void *arg)
{
	while (running) {
		send_nmea_to_nodes();
		sleep(1);
	}
	return ((void *)0);
}

/**
 *	Thread for console read
 *  will print status when user enters 'p'
 */
void *read_console(void *arg)
{
	print_debug(LOG_DEBUG, "starting console thread");
	while (running) {
		/* read console */
		char ch;
		if (scanf("%c", &ch) && (ch == 'p')) {
			print_status = 1;
		}
	}
	return ((void *)0);
}

/**
 *	Send NMEA sentence for current location to all nodes
 */
void send_nmea_to_nodes(void)
{
	// TODO re-implement
	// we need to get the list of sockets...
	// will need to move data to global
	// will need to set a mutex to access the list
	return;
	struct client *curr;
	struct client *temp;
	int bytes;
	char *buf;
	int ret;

	pthread_mutex_lock(&list_mutex);
	curr = head;
	bytes = 0;

	while (curr != NULL) {

		/* update coordinates */
		create_new_sentences(curr);

		/* send rmc */
		buf = curr->loc.nmea_rmc;
		bytes = strlen(buf);

		/* rmc is minumum required nav data */
		memset(&servaddr_vm, 0, sizeof(servaddr_vm));
		servaddr_vm.svm_cid = curr->address;
		servaddr_vm.svm_port = WMASTERD_PORT_GELLED;
		servaddr_vm.svm_family = af;

		/* send nmea sentence to this gelled client */
		ret = sendto(welled_sockfd, (char *)buf, bytes, 0,
				(struct sockaddr *)&servaddr_vm,
				sizeof(struct sockaddr));
		if (ret < 0) {
			print_debug(LOG_NOTICE, "gelled del: %16d room: %36s time: %d name: %s", curr->address, curr->room, curr->time, curr->name);

			if (verbose)
				sock_error("wmasterd: sendto");

			/*
			 * since powering off a VM results in this error
			 * we remove the node from list
			 */
			temp = curr->next;
			remove_node(curr->address, curr->port, curr->radio_id);
			curr = temp;
		} else {
			print_debug(LOG_INFO, "sent %d/%d bytes: %s", ret, bytes, buf);
			/* transmission successful, send gga */
			/* gga provides altitude */
			buf = curr->loc.nmea_gga;
			bytes = strlen(buf);
			sendto(welled_sockfd, (char *)buf, bytes, 0,
				(struct sockaddr *)&servaddr_vm,
				sizeof(struct sockaddr));

			if (send_pashr) {
				/* send the PASHR message with pitch */
				buf = curr->loc.nmea_pashr;
				bytes = strlen(buf);
				sendto(welled_sockfd, (char *)buf, bytes, 0,
					(struct sockaddr *)&servaddr_vm,
					sizeof(struct sockaddr));
			}
		}
		if (curr != NULL) {
			curr = curr->next;
		}
	}
	pthread_mutex_unlock(&list_mutex);
}

/**
 *	Calculates the distance between two nodes
 *	@param node1 - struct for first node
 *	@param node2 - struct for second node
 */
int get_distance(struct client *node1, struct client *node2)
{
	int distance;
	double lat1;
	double lon1;
	double lat2;
	double lon2;
	double theta;
	double dist;
	double a;
	double c;

	lat1 = node1->loc.latitude;
	lon1 = node1->loc.longitude;
	lat2 = node2->loc.latitude;
	lon2 = node2->loc.longitude;

	a = degrees_to_radians(lat1);
	c = degrees_to_radians(lat2);

	theta = degrees_to_radians(lon1 - lon2);
	dist = sin(a) * sin(c) + cos(a) * cos(c) * cos(theta);
	dist = acos(dist);
	dist = radians_to_degrees(dist);
	dist = dist * 60 * 1.1515;
	dist = dist * 1.609344;
	dist = dist * 1000;

	distance = dist;

	print_debug(LOG_DEBUG, "%d meters between %s and %s",
			distance, node1->name, node2->name);

	return distance;
}

#ifndef _WIN32
/**
 *	@brief Blocks usr1 from interferring with non restartable syscalls
 *	@return void
 */
void block_signal(void)
{
	sigset_t intmask;

	if (sigemptyset(&intmask) == -1) {
		perror("wmasterd: sigemptyset");
	} else if (sigaddset(&intmask, SIGUSR1) == -1) {
		perror("wmasterd: sigaddset");
	} else if (sigprocmask(SIG_BLOCK, &intmask, NULL) < 0) {
		perror("wmasterd: sigprocmask");
	}
}

/**
 *	@brief Restores usr1 for use during restartable syscalls
 *	@return void
 */
void unblock_signal(void)
{
	sigset_t intmask;

	if (sigemptyset(&intmask) == -1) {
		perror("wmasterd: sigemptyset");
	} else if (sigaddset(&intmask, SIGUSR1) == -1) {
		perror("wmasterd: sigaddset");
	} else if (sigprocmask(SIG_UNBLOCK, &intmask, NULL) < 0) {
		perror("wmasterd: sigprocmask");
	}
}
#endif

/**
 *	@brief parses annotation field for roomid used by older STEPfwd
 *	@param annotation - pointer to annotation string
 *	@return status
 */
int parse_annotation(char *annotation, char *room)
{
	char *guestinfo;
	int line_len;

	line_len = 0;

	guestinfo = strtok(annotation, "|");
	while (guestinfo) {
		if (strncmp(guestinfo, "0Aroomid = ", 11) == 0) {
			line_len = strlen(guestinfo);
			strncpy(room, guestinfo + 11,
					line_len - 11);
			return 1;
		}
		guestinfo = strtok(NULL, "|");
	}
	return 0;
}

/*
 *	@brief Parse the vmx file for cid, annotion,
 *	roomid/isolationtag and guestinfo lines
 *	@param vmx - pointer to path of vmx file
 *	@param srchost - cid of vm
 *	@param id - will return id
 *	@param name - will return name
 *	@param uuid - will return uuid
 *	@return success/failure
 */
int parse_vmx(char *vmx, unsigned int srchost, char *room, char *name, char *uuid)
{
	FILE *fp;
	unsigned int cid;
	int value;
	char cid_string[16];
	char annotation[LINE_BUF];
	char line[LINE_BUF];
	int line_len;
	char vmx_file[1024];

	cid = 0;
	value = 0;
	line_len = 0;
	memset(line, 0, LINE_BUF);
	memset(cid_string, 0, 16);
	memset(annotation, 0, LINE_BUF);
	memset(vmx_file, 0, 1024 );

	/* remove quotes from vmx file name (windows issue) */
	int vmx_size;
	int pos;
	int newpos;
	int room_found;

	vmx_size = strnlen(vmx, 1024);
	newpos = 0;
	room_found = 0;

	for (pos = 0; pos < vmx_size; pos++) {
		if (vmx[pos] == '\"') {
			continue;
		}
		vmx_file[newpos] = vmx[pos];
		newpos++;
	}

	fp = fopen(vmx_file, "r");

	if (!fp) {
		perror("wmasterd: fopen");
		print_debug(LOG_ERR, "could not open vmx %s", vmx_file);
		return 0;
	}

	while (fgets(line, LINE_BUF, fp)) {
		if (strncmp(line, "vmci0.id = ", 11) == 0) {
			line_len = strnlen(line, LINE_BUF);
			strncpy(cid_string, line + 12, line_len - 14);
			sscanf(cid_string, "%d", &value);
			cid = (unsigned int)value;
		} else if (strncmp(line, "annotation = ", 13) == 0) {
			line_len = strnlen(line, LINE_BUF);
			strncpy(annotation, line, line_len);
		} else if (strncmp(line, "displayName = ", 14) == 0) {
			line_len = strnlen(line, LINE_BUF);
			strncpy(name, line + 15, line_len - 17);
		} else if (strncmp(line, "uuid.location = ", 16) == 0) {
			line_len = strnlen(line, LINE_BUF);
			strncpy(uuid, line + 17, line_len - 19);
		} else if (strncmp(line, "guestinfo.roomid = ", 19) == 0) {
			line_len = strnlen(line, LINE_BUF);
			strncpy(room, line + 20, line_len - 22);
			room_found = 1;
		/* isolationTag overrides roomid */
		} else if (strncmp(line,
					"guestinfo.isolationTag = ", 25) == 0) {
			line_len = strnlen(line, LINE_BUF);
			strncpy(room, line + 26, line_len - 28);
			room_found = 1;
		}
	}
	fclose(fp);

	/* find the room id */
	if (cid == srchost) {
		print_debug(LOG_DEBUG, "cid %16u is a match for name %s",
				cid, name);
		/* guestinfo variables not found (they override annotation */
		if (!room_found) {
			/* parse the room id out of the annotation line */
			if (!parse_annotation(annotation, room)) {
				/* set room to 0 if not found */
				strncpy(room, "0", 2);
			}
		}
		print_debug(LOG_DEBUG, "cid %16d is in room %s", cid, room);
		return 1;
	}

	return 0;
}

/*
 *	@brief Get roomid and vm name
 *	@param srchost - cid of welled client
 *	@param id - used to store room id of vm
 *	@param name - used to store name of vm
 *	@param uuid - used to store uuid of vm
 *	@return void
 */
void get_vm_info(unsigned int srchost, char *room, char *name, char *uuid)
{
	if (!vsock) {
		strncpy(room, "0", 2);
		return;
	}

	FILE *pipe;
	char line[1024];
	char vmx[1024];
	int line_len;
	int ret;
	int room_found;

	ret = 0;
	line_len = 0;
	memset(line, 0, 1024);
	room_found = 0;

#ifdef _WIN32
	/*
	 * the vmrun.exe command returns the list of running vms for the
	 * user which ran vmrun.exe.
	 */
	pipe = popen("\"C:/Program Files (x86)/VMware/VMware Workstation/vmrun.exe\" list", "rt");
#else
	if (esx) {
		pipe = popen("/bin/esxcli vm process list", "r");
	} else {
		if (verbose)
			print_debug(LOG_INFO, "does not open vmx on linux\n");
		/* TODO: implement check for linux host
		 * this would be a machine running workstation
		 */
		pipe = NULL;
	}
#endif

	if (!pipe) {
		perror("wmasterd: pipe");
		print_debug(LOG_ERR, "popen failed to produce pipe");
		memset(room, 0, UUID_LEN);
		return;
	}

	while (fgets(line, 1024, pipe)) {
		memset(vmx, 0, 1024);
		memset(name, 0, NAME_LEN);
		memset(uuid, 0, UUID_LEN);
		memset(room, 0, UUID_LEN);
#ifdef _WIN32

		/* continue if room found or first line of output */
		if (strncmp(line, "Total", 5) == 0) {
			continue;
		}
		line[strnlen(line, 1024) - 1] = '\0';
		snprintf(vmx, 1024, "\"%s\"", line);
		if (parse_vmx(vmx, srchost, room, name, uuid)) {
			room_found = 1;
		}
#else
		/* continue if room found or this is not a config file line */
		if (strncmp(line, "   Config File:", 15) != 0) {
			continue;
		}
		line_len = strnlen(line, 1024);
		strncpy(vmx, line + 16, line_len - 17);
		if (parse_vmx(vmx, srchost, room, name, uuid)) {
			room_found = 1;
		}
#endif
		if (room_found) {
			break;
		}
	}
	if (!room_found) {
		strncpy(room, "0", 2);
		if (check_room) {
			print_debug(LOG_INFO, "no room found for node %d", srchost);
		}
	}

#ifdef _WIN32
	ret = _pclose(pipe);
#else
	ret = pclose(pipe);
#endif

	if (ret < 0) {
		perror("wmasterd: pclose");
	}
}

/**
 *	@brief Adds a node to the linked list
 *	@param vsock - whether address is CID or IP, 1 if CID
 *	@param srchost - the CID of the guest node vm
 *	@param srcport - the port of the welled connection
 *	@param room - the room uuid of the vm
 *	@param vm_name - the name of the vm
 *	@param uuid - the uuid of the vm
 *  @param socket - the initial socket for the connection
 *  @param radio_id - the id of the radio
 *	@return void
 */
void add_node(unsigned int srchost, int srcport, char *vm_room, char *vm_name, char *uuid, int wsd, int gsd, int radio_id)
{
	print_debug(LOG_DEBUG, "add_node");

	struct client *node;
	struct client *curr;
	char buf[1024];
	unsigned int address;
	double lat;
	double lon;
	double alt;
	double sog;
	double cog;
	double pit;
	int ret;
	int matched;
	char name[NAME_LEN];
	char room[UUID_LEN];
	struct in_addr ip;
	char ip_address[16];

	ip.s_addr = srchost;
	matched = 0;
	memset(ip_address, 0, 16);

	/* create new node */
	node = malloc(sizeof(struct client));
	node->address = srchost;
	node->port = srcport;
	node->radio_id = radio_id;
	node->next = NULL;
	node->time = time(NULL);
	node->welled_socket = wsd;
	node->gelled_socket = gsd;
	memset(&node->loc, 0, sizeof(struct location));
	memset(node->name, 0, NAME_LEN);
	memset(node->uuid, 0, UUID_LEN);
	memset(node->room, 0, UUID_LEN);
	strncpy(node->name, vm_name, NAME_LEN - 1);
	strncpy(node->uuid, uuid, UUID_LEN - 1);
	strncpy(node->room, vm_room, UUID_LEN - 1);

	/* default to med sea */
	node->loc.latitude = 35;	/* DD.DDDD*/
	node->loc.longitude = 35;   /* DD.DDDD*/
	node->loc.altitude = 0;		/* meters */
	node->loc.velocity = 0;		/* knots */
	node->loc.heading = 0;		/* degrees */
	node->loc.altitude = 0;		/* meters */
	node->loc.pitch = 0;		/* degrees */

	/* add node to cache file */
	if (cache) {
		if (vsock) {
			print_debug(LOG_DEBUG, "checking cache file for node %16u radio %d", srchost, radio_id);
		} else {
			print_debug(LOG_DEBUG, "checking cache file for node %16s radio %d", inet_ntoa(ip), radio_id);
		}

		pthread_mutex_lock(&file_mutex);

		fseek(cache_fp, 0, SEEK_SET);

		while (((fgets(buf, 1024, cache_fp)) != NULL) && (!matched)) {
			if (vsock) {
				ret = sscanf(buf, "%u %d %s %lf %lf %lf %lf %lf %lf %s",
						&address, &radio_id, room, &lat, &lon, &alt, &sog, &cog, &pit, name);
			} else {
				ret = sscanf(buf, "%s %d %s %lf %lf %lf %lf %lf %lf %s",
						ip_address, &radio_id, room, &lat, &lon, &alt, &sog, &cog, &pit, name);
			}
			remove_newline(buf);
			print_debug(LOG_DEBUG, "parsed %d items for '%s'", ret, buf);
			if ((ret != 10) && (ret != 9)) {
				print_debug(LOG_ERR, "only parsed %d variables for '%s'", ret, buf);
				if (verbose) {
					if (vsock) {
						printf("addr: %u\n", address);
					} else {
						printf("addr: %s\n", ip_address);
					}
					printf("room:  %s\n", room);
					printf("radio: %d\n", radio_id);
					printf("lat:   %f\n", lat);
					printf("lon:   %f\n", lon);
					printf("alt:   %f\n", alt);
					printf("sog:   %f\n", sog);
					printf("cog:   %f\n", cog);
					printf("pit:   %f\n", pit);
					printf("name:  %s\n", name);
				}
				continue;
			} else if (vsock && ((address == srchost) && (radio_id == node->radio_id))) {
				print_debug(LOG_INFO, "vsock node found in cache file %16u radio %d", address, radio_id);
				/* load values from cache file */
				node->loc.latitude = lat;	/* DD.DDDD*/
				node->loc.longitude = lon;	/* DD.DDDD*/
				node->loc.altitude = alt;	/* meters */
				node->loc.velocity = sog;	/* knots */
				node->loc.heading = cog;	/* degrees */
				node->loc.pitch = pit;		/* degrees */
				if (strnlen(vm_name, NAME_LEN) == 0) {
					strncpy(node->name, name,
						strnlen(name, NAME_LEN - 1));
				}
				break;
			} else if (!vsock && ((strncmp(ip_address, inet_ntoa(ip), 16) == 0) && (radio_id == node->radio_id))) {
				print_debug(LOG_INFO, "ipv4 node found in cache file %16s radio %d", ip_address, radio_id);
				matched = 1;
				/* load values from cache file */
				node->loc.latitude = lat;	/* DD.DDDD*/
				node->loc.longitude = lon;	/* DD.DDDD*/
				node->loc.altitude = alt;	/* meters */
				node->loc.velocity = sog;	/* knots */
				node->loc.heading = cog;	/* degrees */
				node->loc.pitch = pit;		/* degrees */
				if (strnlen(vm_name, NAME_LEN) == 0) {
					strncpy(node->name, name,
						strnlen(name, NAME_LEN - 1));
				}
				break;
			} else {
				print_debug(LOG_DEBUG, "this line does not match the node");
				continue;
			}
		}
		char *format_ipv4 = "%-16s %-5d %-6d %-5d %-5d %-36s %-4d %-9.6f %-10.6f %-6.0f %-8.2f %-6.2f %-6.2f %-s\n";
		char *format_vsock = "%-16u %-5d %-6d %-5d %-5d %-36s %-4d %-9.6f %-10.6f %-6.0f %-8.2f %-6.2f %-6.2f %-s\n";

		if (!matched) {
			print_debug(LOG_DEBUG, "adding node to the cache file");
			fseek(cache_fp, 0, SEEK_END);
			if (vsock) {
				fprintf(cache_fp,
				format_vsock,
					node->address, node->radio_id, node->room,
					node->loc.latitude, node->loc.longitude,
					node->loc.altitude, node->loc.velocity,
					node->loc.heading, node->loc.pitch,
					node->name);
			} else {
				fprintf(cache_fp,
					format_ipv4,
					inet_ntoa(ip), node->radio_id, node->room,
					node->loc.latitude, node->loc.longitude,
					node->loc.altitude, node->loc.velocity,
					node->loc.heading, node->loc.pitch,
					node->name);
			}
			fflush(cache_fp);
		}

		pthread_mutex_unlock(&file_mutex);
	}


	if (head == NULL) {
		/* add first node */
		head = node;
	} else {
		/* traverse to end of list */
		curr = head;
		while (curr->next != NULL) {
			curr = curr->next;
		}
		/* add to node to end of list */
		curr->next = node;
	}
	if (vsock) {
		print_debug(LOG_NOTICE, "add: %16d port: %5d radio: %5d room: %36s time: %d name: %s",
				srchost, srcport, node->radio_id, node->room, node->time, node->name);
	} else {
		print_debug(LOG_NOTICE, "add: %16s port: %5d radio: %5d room: %36s time: %d name: %s",
				inet_ntoa(ip), srcport, node->radio_id, node->room, node->time, node->name);
	}
}

/**
 *	@brief Searches the linked list for a given node
 *	@param srchost - CID/IP of the guest
 *	@param srcport - port number
 *	@return returns the node
 */
struct client *get_node_by_radio(unsigned int srchost, int srcport, int radio_id)
{
	struct client *curr;

	curr = head;

	while (curr != NULL) {
		if ((curr->address == srchost) && (curr->port == srcport) && (curr->radio_id == radio_id)) {
			curr->time = time(NULL);
			return curr;
		}
		curr = curr->next;
	}

	return NULL;
}

/**
 *	@brief Searches the linked list for a given node
 *	@param srchost - CID/IP of the guest
 *	@param srcport - port number
 *	@return returns the node
 */
struct client *get_node_by_address(unsigned int srchost, int srcport)
{
	struct client *curr;

	curr = head;

	while (curr != NULL) {
		if ((curr->address == srchost) && (curr->port == srcport)) {
			curr->time = time(NULL);
			return curr;
		}
		curr = curr->next;
	}

	return NULL;
}

/**
 *      @brief Searches the linked list for a given node
 *      @param name - name of the guest vm
 *      @return returns the node
*/
struct client *get_node_by_name(char *name)
{
	struct client *curr;

	curr = head;

	print_debug(LOG_DEBUG, "searching for node with name %s", name);

	while (curr != NULL) {
		if (strncmp(curr->name, name, NAME_LEN - 1) == 0) {
			return curr;
		}
		curr = curr->next;
	}

	print_debug(LOG_DEBUG, "no node found with name %s", name);

	return NULL;
}

/**
 *      @brief Searches the linked list for a given node
 *      @param sd - socket descriptor
 *      @return returns the node
*/
struct client *get_node_by_socket(int sd)
{
	struct client *curr;

	curr = head;

	print_debug(LOG_DEBUG, "searching for node with socket %d", sd);

	while (curr != NULL) {
		if ((curr->welled_socket == sd) || (curr->gelled_socket == sd)) {
			print_debug(LOG_DEBUG, "found a node found with socket %d", sd);
			return curr;
		}
		curr = curr->next;
	}

	print_debug(LOG_DEBUG, "no node found with socket %d", sd);

	return NULL;
}

void print_node(struct client *node)
{
	printf("%d\n", node->address);
	printf("%d\n", node->port);
	printf("%d\n", node->radio_id);
	printf("%s\n", node->name);
	printf("%s\n", node->room);
	printf("%d\n", node->time);
	printf("%f\n", node->loc.latitude);
	printf("%f\n", node->loc.longitude);
	printf("%f\n", node->loc.altitude);
	printf("%f\n", node->loc.velocity);
	printf("%f\n", node->loc.heading);
	printf("%f\n", node->loc.pitch);
}

/**
 *	@brief Lists the nodes in the linked list
 *	@return void
 */
void list_nodes(void)
{
	struct client *curr;
	FILE *fp;
	int age;
	struct in_addr ip;
	char *header = "%-16s %-6s %-36s %-4s %-9s %-10s %-6s %-8s %-6s %-6s %-s\n";
	char *format_ipv4 =  "%-16s %-6d %-36s %-4d %-9.6f %-10.6f %-6.0f %-8.2f %-6.2f %-6.2f %-s\n";
	char *format_vsock = "%-16u %-6d %-36s %-4d %-9.6f %-10.6f %-6.0f %-8.2f %-6.2f %-6.2f %-s\n";

	curr = head;
	fp = fopen("/tmp/wmasterd.status", "w");

	if (!fp && esx) {
		perror("wmasterd: fopen");
		print_debug(LOG_WARNING, "could not open /tmp/wmasterd.status\n");
	}

	if (fp) {
		fprintf(fp, header, "addr:", "port:", "radio:", "wsd:", "gsd:", "room:", "age:", "lat:", "lon:", "alt:", "sog:", "cog:", "pitch:", "name:");
	} else {
		printf(header, "addr:", "port:", "radio:", "wsd:", "gsd:", "room:", "age:", "lat:", "lon:", "alt:", "sog:", "cog:", "pitch:", "name:");
	}

	while (curr != NULL) {
		age = time(NULL) - curr->time;
		if (fp) {
			if (vsock) {
				fprintf(fp, format_vsock,
					curr->address, curr->port, curr->radio_id,
					curr->welled_socket, curr->gelled_socket,
					curr->room, age,
					curr->loc.latitude, curr->loc.longitude,
					curr->loc.altitude,
					curr->loc.velocity,
					curr->loc.heading,
					curr->loc.pitch,
					curr->name);
			} else {
				ip.s_addr = curr->address;
				fprintf(fp, format_ipv4,
					inet_ntoa(ip), curr->port, curr->radio_id,
					curr->welled_socket, curr->gelled_socket,
					curr->room, age,
					curr->loc.latitude, curr->loc.longitude,
					curr->loc.altitude,
					curr->loc.velocity,
					curr->loc.heading,
					curr->loc.pitch,
					curr->name);
			}
		} else {
			if (vsock) {
				printf(format_vsock,
						curr->address, curr->port, curr->radio_id,
						curr->welled_socket, curr->gelled_socket,
						curr->room, age,
						curr->loc.latitude, curr->loc.longitude,
						curr->loc.altitude,
						curr->loc.velocity,
						curr->loc.heading,
						curr->loc.pitch,
						curr->name);
			} else {
				ip.s_addr = curr->address;
				printf(format_ipv4,
						inet_ntoa(ip), curr->port, curr->radio_id,
						curr->welled_socket, curr->gelled_socket,
						curr->room, age,
						curr->loc.latitude, curr->loc.longitude,
						curr->loc.altitude,
						curr->loc.velocity,
						curr->loc.heading,
						curr->loc.pitch,
						curr->name);
			}
		}
		curr = curr->next;
	}

	if (fp) {
		fclose(fp);
	}
}

/**
 *	@brief Removed a node from the linked list
 *	@param dsthost - CID of the guest
 *	@param dstport - port of the guest connection
 *	@param radio_id - radio to be removed
 *	@return void
 */
void remove_node(unsigned int dsthost, int dstport, int radio_id)
{
	if (vsock) {
		print_debug(LOG_DEBUG, "remove_node %d:%d radio %d", dsthost, dstport, radio_id);
	} else {
		struct in_addr ip;
		ip.s_addr = dsthost;
		print_debug(LOG_DEBUG, "remove_node %s:%d radio %d", inet_ntoa(ip), dstport, radio_id);
	}

	struct client *curr;
	struct client *prev;
	char name[NAME_LEN];
	char room[UUID_LEN];
	int time;
	struct in_addr ip;

	time = 0;
	memset(name, 0, NAME_LEN);
	memset(room, 0, UUID_LEN);
	curr = head;
	prev = NULL;

	/* traverse while not null and no match */
	while (curr != NULL && !((curr->address == dsthost) && (curr->port == dstport) && (curr->radio_id == radio_id))) {
		prev = curr;
		curr = curr->next;
	}

	/* exit if we hit the end of the list */
	if (curr == NULL) {
		print_debug(LOG_INFO, "node not found: %16d:%d",
				dsthost, dstport);
		return;
	}

	strncpy(room, curr->room, strnlen(curr->room, UUID_LEN - 1));
	strncpy(name, curr->name, strnlen(curr->name, NAME_LEN - 1));
	time = curr->time;
	radio_id = curr->radio_id;

	/* delete node */
	if (prev == NULL) {
		head = curr->next;
	} else {
		prev->next = curr->next;
	}
	free(curr);

	if (vsock) {
		print_debug(LOG_NOTICE, "del: %16d:%d radio: %d room: %36s time: %d name: %s",
				dsthost, dstport, radio_id, room, time, name);
	} else {
		ip.s_addr = dsthost;
		print_debug(LOG_NOTICE, "del: %16s:%d radio: %d room: %36s time: %d name: %s",
				inet_ntoa(ip), dstport, radio_id, room, time, name);
	}
}

/**
 *	@brief Removed all of a hosts nodes from the linked list
 *	@param socket - file descriptor for the socket
 *	@return void
 */
void remove_nodes_by_socket(int socket)
{
	print_debug(LOG_DEBUG, "remove_nodes_by_socket %d", socket);

	int ret;

	do {
		ret = remove_node_by_socket(socket);
	} while (ret);
}

/**
 *	@brief Removed a node from the linked list
 *	@param socket - file descriptor for the socket
 *	@return void
 */
int remove_node_by_socket(int socket)
{
	print_debug(LOG_DEBUG, "remove_node_by_socket %d", socket);
	struct client *curr;
	struct client *prev;
	char name[NAME_LEN];
	char room[UUID_LEN];
	unsigned int dsthost;
	unsigned int dstport;
	int time;
	struct in_addr ip;
	int radio_id;

	radio_id = -1;
	time = 0;
	memset(name, 0, NAME_LEN);
	memset(room, 0, UUID_LEN);
	curr = head;
	prev = NULL;

	/* traverse while not null and no match */
	while (curr != NULL && !(curr->welled_socket == socket) && !(curr->gelled_socket == socket)) {
		prev = curr;
		curr = curr->next;
	}

	/* exit if we hit the end of the list */
	if (curr == NULL) {
		return 0;
	}

	strncpy(room, curr->room, strnlen(curr->room, UUID_LEN - 1));
	strncpy(name, curr->name, strnlen(curr->name, NAME_LEN - 1));
	time = curr->time;
	dsthost = curr->address;
	dstport = curr->port;
	radio_id = curr->radio_id;

	/* delete node */
	if (prev == NULL) {
		head = curr->next;
	} else {
		prev->next = curr->next;
	}
	free(curr);

	if (vsock) {
		print_debug(LOG_NOTICE, "del: %16d:%d radio: %d room: %36s time: %d name: %s",
				dsthost, dstport, radio_id, room, time, name);
	} else {
		ip.s_addr = dsthost;
		print_debug(LOG_NOTICE, "del: %16s:%d radio: %d room: %36s time: %d name: %s",
				inet_ntoa(ip), dstport, radio_id, room, time, name);
	}
	return 1;
}

/**
 *      @brief Display a hex dump of the passed buffer
 *      this function was pulled off the web somehwere... seems to throw a
 *      segfault when printing some of the netlink error message data
 *      @param addr - address of buffer
 *      @param len - size of buffer
 *      @return void - assumes success
 */
void hex_dump(void *addr, int len)
{
	if (!verbose) {
		return;
	}

	int i;
	unsigned char buff[17];
	unsigned char *pc;

	pc = (unsigned char *)addr;

	/* loop through all bytes */
	for (i = 0; i < len; i++) {
		/* new line every 16 bytes */
		if ((i % 16) == 0) {
			/* dont print ascii for the zeroth line */
			if (i != 0) {
				printf("  %s\n", buff);
			}
			/* print offset */
			printf("  %04x ", i);
		}

		/* print the hex for the byte */
		printf(" %02x", pc[i]);

		/* store ASCII character for later */
		if ((pc[i] < 0x20) || (pc[i] > 0x7e)) {
			buff[i % 16] = '.';
		} else {
			buff[i % 16] = pc[i];
		}
		buff[(i % 16) + 1] = '\0';
	}

	/* pad out last line if not exactly 16 bytes */
	while ((i % 16) != 0) {
		printf("   ");
		i++;
	}

	/* print final ASCII byte */
	printf("  %s\n", buff);
}

#ifndef _WIN32
void send_to_hosts(char *buf, int bytes, char *room)
{
	print_debug(LOG_INFO, "send_to_hosts deprecated");
	return;

	if (!esx || !broadcast) {
		return;
	}

	char *buffer;
	char temp[UUID_LEN + 1];
	struct sockaddr_in dest_addr;
	int sock_opts;
	int udp_send_sockfd;
	int bytes_sent;

	/* create packet data */
	buffer = malloc(bytes + UUID_LEN);
	memcpy(buffer, buf, bytes);
	snprintf(temp, UUID_LEN + 1, ":%s", room);
	memcpy(buffer + bytes, temp, UUID_LEN);

	/* set destination address */
	memset(&dest_addr, 0, sizeof(struct sockaddr_in));
	dest_addr.sin_family = AF_INET;
	inet_pton(AF_INET, broadcast_addr, &dest_addr.sin_addr.s_addr);
	dest_addr.sin_port = htons(2018);

	/* setup socket */
	sock_opts = 1;
	udp_send_sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (udp_send_sockfd < 0) {
		sock_error("wmasterd: socket");
		print_debug(LOG_ERR, "could not create udp socket\n");
		free(buffer);
		return;
	}
	setsockopt(udp_send_sockfd, SOL_SOCKET, SO_REUSEADDR|SO_BROADCAST,
		(const char *)&sock_opts, sizeof(int));

	/* send packet */
	bytes_sent = sendto(udp_send_sockfd, buffer, bytes + UUID_LEN, 0,
			(const struct sockaddr *)&dest_addr,
			sizeof(dest_addr));
	if (bytes_sent < 0) {
		sock_error("wmasterd: sendto\n");
	} else {
		print_debug(LOG_INFO, "sent %d bytes to %s",
				bytes_sent, broadcast_addr);
	}
	/* cleanup */
	close(udp_send_sockfd);
	free(buffer);
}
#endif

/**
 *	@brief Sends message to all nodes in linked list
 *	@param buf - message data
 *	@param bytes - size of message data
*	@param node - the struct client node that sent the data
 *	@return void - assumes success
 */
void relay_to_nodes(char *buf, int bytes, struct client *node)
{

	struct client *curr;
	struct client *temp;
	int age;
	int now;
	int distance;
	int bytes_sent;
	struct in_addr ip;

	curr = head;
	age = 0;
	now = time(NULL);

	if (check_room) {
		print_debug(LOG_DEBUG, "sending to nodes in room %s", node->room);
	}

	while (curr != NULL) {
		age = now - curr->time;
		ip.s_addr = curr->address;

		/* skip node if room differs */
		if (check_room && (strncmp(
					curr->room, node->room, UUID_LEN - 1) != 0)) {
			if (vsock) {
				print_debug(LOG_INFO, "skipped: %16d:%d room: %36s",
						curr->address, curr->port, curr->room);
			} else {
				struct in_addr ip;
				print_debug(LOG_ERR, "skipped %16s:%d room: %36s",
						inet_ntoa(ip), curr->port, curr->room);
			}
			curr = curr->next;
			continue;
		} else if (age > 300) {
			// TODO deprecate the stale node test
			/* save our place */
			temp = curr->next;

			/* remove stale node */
			print_debug(LOG_DEBUG, "stale node found");
			remove_node(curr->address, curr->port, curr->radio_id);

			/* go to next node */
			curr = temp;
			continue;
		} else if ((curr->radio_id < 0) || (curr->radio_id > 99)) {
			/* invalid radio */
			curr = curr->next;
			continue;
		}

		/* send frame to this welled client */
		if (send_distance) {
			/* determine distance between the nodes */
			if (curr == node) {
				distance = 0;
			} else {
				distance = get_distance(curr, node);
			}
			/* skip nodes out of range */
			if ((distance > 2500) || (distance < 0)) {
				curr = curr->next;
				continue;
			}
		} else {
			distance = -1;
		}

		/* add the distance to the buf */
		struct message_hdr *hdr = (struct message_hdr *)buf;
		hdr->distance = distance;
		/* add the dest radio id to the message */
		hdr->dest_radio_id = curr->radio_id;

		/* send frame to this welled client */
		bytes_sent = send(curr->welled_socket, (char *)buf, bytes, 0);
		if (bytes_sent < 0) {
			if (verbose) {
				sock_error("wmasterd: sendto");
				if (vsock) {
					print_debug(LOG_ERR, "name %s cid %16d port %5d bytes %5d",
							curr->name, curr->address, curr->port, bytes);
				} else {
					print_debug(LOG_ERR, "name %s addr %16s port %5d bytes %5d",
							curr->name, inet_ntoa(ip), curr->port, bytes);
				}
				print_node(curr);
			}
			/* likely disconnected and should remove all nodes on client */
			temp = curr->next;
			remove_node(curr->address, curr->port, curr->radio_id);
			curr = temp;
		} else {
			if (vsock) {
				print_debug(LOG_INFO, "sent %5d bytes to node: %16d:%-5d room: %6s radio: %3d distance: %4d socket: %4d",
						bytes, curr->address, curr->port, curr->room, hdr->dest_radio_id, hdr->distance, curr->welled_socket);
			} else {
				print_debug(LOG_INFO, "sent %5d bytes to node: %16s:%-5d room: %6s radio: %3d distance: %4d socket: %4d",
						bytes, inet_ntoa(ip), curr->port, curr->room, hdr->dest_radio_id, hdr->distance, curr->welled_socket);
			}
			curr = curr->next;
		}
	}
}

/**
 *	@brief Frees all of the nodes in the linked list
 *	@return void - assumes success
 */
void free_list(void)
{
	struct client *temp = NULL;
	struct client *curr = NULL;

	curr = head;

	while (curr != NULL) {
		temp = curr;
		curr = curr->next;
		free(temp);
	}

	head = NULL;
}

#ifndef _WIN32
/**
 *	@brief Signal handler which prints nodes
 *	@return void
 */
void usr1_handler(void)
{
	print_status = 1;
}

/**
 *	@brief Signal handler which causes program to exit
 *	@return void - calls exit()
 */
void signal_handler(void)
{
	running = 0;
}
#endif

/**
 *
 */
void update_node_info(struct client *node, struct update_2 *data)
{
	int update_file;

	update_file = 0;

	/* TODO make sure we dont lose existing name */

	if ((strnlen(data->name, NAME_LEN) > 0) &&
			(strnlen(data->name, NAME_LEN) > 0)) {
		strncpy(node->name, data->name, NAME_LEN - 1);
		update_file = 1;
	} else {
		print_debug(LOG_DEBUG, "no name in update");
	}

	if ((strnlen(data->room, UUID_LEN) > 0) &&
			(strncmp(node->room, data->room, UUID_LEN - 1) != 0)) {
		strncpy(node->room, data->room, UUID_LEN - 1);
		update_file = 1;
	} else {
		print_debug(LOG_DEBUG, "no room in update");
	}

	if (update_file) {
		print_debug(LOG_INFO, "node name %s room %s\n", node->name, node->room);
		update_cache_file_info(node);
	}
}

#ifndef _WIN32
void *recv_from_hosts(void *arg)
{
	print_debug(LOG_INFO, "recv_from_hosts deprecated");
	return ((void *)0);

	char *buffer;
	struct client node;
	char src_host[16];
	char buf[BUFF_LEN];
	int udp_recv_sockfd;
	int bytes;
	struct sockaddr_in bindaddr;
	struct sockaddr_in cliaddr;
	socklen_t addrlen;

	addrlen = sizeof(struct sockaddr);
	memset(&cliaddr, 0, sizeof(cliaddr));
	memset(&bindaddr, 0, sizeof(bindaddr));

	// create socket
	udp_recv_sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (udp_recv_sockfd < 0) {
		sock_error("wmasterd: socket");
		print_debug(LOG_ERR, "could not create udp listen socket\n");
		exit(EXIT_FAILURE);
	}

	bindaddr.sin_family = AF_INET;
	bindaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	bindaddr.sin_port = htons(2018);

	// bind
	if (bind(udp_recv_sockfd, (struct sockaddr *)&bindaddr, sizeof(bindaddr)) < 0) {
		sock_error("wmasterd: bind");
	}

	while (running) {

		// recv packet
		bytes = recvfrom(udp_recv_sockfd, (char *)buf, BUFF_LEN, 0,
				(struct sockaddr *)&cliaddr, &addrlen);
		if (bytes < 0) {
			continue;
		}
		inet_ntop(AF_INET, &cliaddr.sin_addr, src_host, addrlen);
		print_debug(LOG_INFO, "received %d bytes from src host: %d",
				bytes, src_host);
		/* parse out room */
		strncpy(node.room, buf + bytes - UUID_LEN + 1, UUID_LEN);
		buffer = malloc(bytes - UUID_LEN - 1);

		/* lock list and send */
		pthread_mutex_lock(&list_mutex);
		relay_to_nodes(buffer, bytes, &node);
		pthread_mutex_unlock(&list_mutex);

		/* cleanup */
		close(udp_recv_sockfd);
		free(buffer);
	}
	return ((void *)0);
}
#endif


struct client *process_connection(unsigned int src_addr, int src_port, int wsd, int gsd, int radio_id)
{

	if (radio_id == -1) {
		print_debug(LOG_INFO, "cannot process connection for invalid radio id: %d", radio_id);
	}

	char room[UUID_LEN];
	char old_room[UUID_LEN];
	char name[NAME_LEN];
	char uuid[UUID_LEN];
	struct client *node;
	struct in_addr ip;
	if (!vsock) {
		ip.s_addr = src_addr;
	}

	if (vsock) {
		print_debug(LOG_NOTICE, "adding new node for src host: %16u port: %5d radio: %3d",
				src_addr, src_port, radio_id);
	} else {
		print_debug(LOG_NOTICE, "adding new node for src host: %16s port: %5d radio: %3d",
				inet_ntoa(ip), src_port, radio_id);
	}
	memset(room, 0, UUID_LEN);
	memset(old_room, 0, UUID_LEN);
	memset(name, 0, NAME_LEN);
	memset(uuid, 0, UUID_LEN);

	strncpy(room, "0", UUID_LEN);
	strncpy(name, "UNKNOWN", NAME_LEN);

	if (vsock) {
		// TODO update to send CID in the header even when IP, then we can lookup via vmx again
		get_vm_info(src_addr, room, name, uuid);
	}

	add_node(src_addr, src_port, room, name, uuid, wsd, gsd, radio_id);

	/* make sure we added the node */
	node = get_node_by_radio(src_addr, src_port, radio_id);
	if (!node) {
		if (vsock) {
			print_debug(LOG_ERR, "adding node failed %16d:%-5d radio %3d",
					src_addr, src_port, radio_id);
		} else {
			print_debug(LOG_ERR, "adding node failed %16s:%-5d radio %3d",
					inet_ntoa(ip), src_port, radio_id);
		}
		return NULL;
	}

	if (update_room && check_room && vsock) {
		// TODO update to send CID in the header even when IP, then we can lookup via vmx again
		print_debug(LOG_DEBUG, "checking vmx for room update");
		/* check for room change if room enforced */
		strncpy(old_room, node->room, UUID_LEN - 1);
		if (vsock) {
			get_vm_info(src_addr, room, name, uuid);
		}
		if (strncmp(old_room, room, UUID_LEN  - 1) != 0) {
			remove_node(src_addr, src_port, radio_id);
			add_node(src_addr, src_port, room, name, uuid, wsd, gsd, radio_id);
			/* make sure we updated the node */
			node = get_node_by_radio(src_addr, src_port, radio_id);
			if (!node) {
				if (vsock) {
					print_debug(LOG_ERR, "updating node failed %16d:%-5d radio %3d",
							src_addr, src_port, radio_id);
				} else {
					print_debug(LOG_ERR, "updating node failed %16d:%-5d radio %3d",
							inet_ntoa(ip), src_port, radio_id);
				}
				return NULL;
			}
		}
	}
	return node;
}



/**
 *	@brief main function
 */
int main(int argc, char *argv[])
{
	int opt;
	int cid;
	int port;
	struct timeval tv;
	int ret;
	int long_index;
	int gps;
#ifndef _WIN32
	struct utsname uts_buf;
	int vsock_dev_fd;

	vsock_dev_fd = 0;
#endif

	check_room = 1;
	update_room = 0;
	ret = 0;
	verbose = 0;
	head = 0;
	running = 0;
	esx = 0;
	long_index = 0;
	print_status = 0;
	send_distance = 0;
	broadcast = 0;
	loglevel = -1;
	send_pashr = 0;
	gps = 1;
	vsock = 1;
	port = WMASTERD_PORT_WELLED;

	static struct option long_options[] = {
		{"help",		no_argument, 0, 'h'},
		{"version",		no_argument, 0, 'V'},
		{"verbose",		no_argument, 0, 'v'},
		{"broadcast",		no_argument, 0, 'b'},
		{"no-check-room",	no_argument, 0, 'r'},
		{"update-room",		no_argument, 0, 'u'},
		{"nogps",		no_argument, 0, 'n'},
		{"distance",		no_argument, 0, 'd'},
		{"pashr",		no_argument, 0, 'p'},
		{"ip",		  	no_argument, 0, 'i'},
		{"listen-port",	 	required_argument, 0, 'l'},
		{"debug",		required_argument, 0, 'D'},
		{"cache-file",		required_argument, 0, 'c'}
	};

	while ((opt = getopt_long(argc, argv, "hVvbrundpil:D:c:", long_options,
			&long_index)) != -1) {
		switch (opt) {
		case 'h':
			show_usage(EXIT_SUCCESS);
			break;
		case 'V':
			/* allow help2man to read this */
			setbuf(stdout, NULL);
			printf("wmasterd: version %s\n", VERSION_STR);
			exit(EXIT_SUCCESS);
			break;
		case 'b':
			broadcast = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'r':
			check_room = 0;
			break;
		case 'u':
			update_room = 1;
			break;
		case 'd':
			send_distance = 1;
			break;
		case 'D':
			loglevel = atoi(optarg);
			if ((loglevel < 0) || (loglevel > 7)) {
				show_usage(EXIT_FAILURE);
			}
			printf("wmasterd: syslog level set to %d\n", loglevel);
			break;
		case 'p':
			send_pashr = 1;
			break;
		case 'n':
			gps = 0;
			break;
		case 'c':
			cache_filename = optarg;
			/* create or open file  for read and write */
			cache = open(cache_filename, O_CREAT|O_RDWR,
				S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
			/* open file as a stream */
			cache_fp = fdopen(cache, "r+");
			if (!cache_fp) {
				perror("wmasterd: fopen");
				show_usage(EXIT_FAILURE);
			}
			break;
		case 'i':
			vsock = 0;
			break;
		case 'l':
			port = atoi(optarg);
			if (port < 1 || port > 65535) {
				show_usage(EXIT_FAILURE);
			}
			break;
		case '?':
			printf("wmasterd: Error - No such option: `%c'\n\n",
				optopt);
			show_usage(EXIT_FAILURE);
			break;
		}
	}

	if (optind < argc) {
		show_usage(EXIT_FAILURE);
	}
#ifdef _WIN32
	WSAStartup(MAKEWORD(1,1), &wsa_data);
	if (broadcast) {
		printf("broadcast not implemented on windows\n");
		show_usage(EXIT_FAILURE);
	}
	if (vsock) {
	/* TODO: use vm_sockets and ioctl to get cid */
		af = VMCISock_GetAFValue();
		cid = VMCISock_GetLocalCID();
	} else {
		af = AF_INET;
	}
#else
	uname(&uts_buf);
	if (strncmp(uts_buf.sysname, "VMkernel", 8) == 0) {
		esx = 1;
	}

	if (loglevel >= 0) {
		openlog("wmasterd", LOG_PID, LOG_USER);
	}

	if (vsock) {
		af = -1;
		/* TODO: add check for other hypervisors */
		vsock_dev_fd = open("/dev/vsock", 0);
		if (vsock_dev_fd < 0) {
			sock_error("wmasterd: open");
			print_debug(LOG_ERR, "could not open /dev/vsock\n");
		} else {
			if (ioctl(vsock_dev_fd, IOCTL_VM_SOCKETS_GET_LOCAL_CID, &cid) < 0) {
				perror("wmasterd: ioctl IOCTL_VM_SOCKETS_GET_LOCAL_CID");
			}
			if (ioctl(vsock_dev_fd, IOCTL_VMCI_SOCKETS_GET_AF_VALUE, &af) < 0) {
				perror("wmasterd: ioctl IOCTL_VMCI_SOCKETS_GET_AF_VALUE");
				af = -1;
			}
		}
		if (af == -1) {
			/* take a guess */
			if (esx)
				af = 53;
			else
				af = 40;
		}
	} else {
		af = AF_INET;
	}
#endif
	print_debug(LOG_NOTICE, "Starting, version %s", VERSION_STR);

	/* Handle kill signals */
	running = 1;
#ifndef _WIN32
	signal(SIGINT, (void *)signal_handler);
	signal(SIGTERM, (void *)signal_handler);
	signal(SIGQUIT, (void *)signal_handler);
	signal(SIGUSR1, (void *)usr1_handler);
	signal(SIGHUP, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);
#endif

#ifndef _WIN32
	char udp_int[8];
	if (esx) {
		strncpy(udp_int, "vmk0", 8);
	} else {
		strncpy(udp_int, "ens33", 8);
	}
	/* get ip address */
	if (broadcast) {
		int fd;
		struct ifreq ifr;
		fd = socket(AF_INET, SOCK_DGRAM, 0);
		/* I want to get an IPv4 IP address */
		ifr.ifr_addr.sa_family = AF_INET;

		strncpy(ifr.ifr_name, udp_int, IFNAMSIZ - 1);

		ioctl(fd, SIOCGIFBRDADDR, &ifr);
		close(fd);

		/* display result */
		strncpy(broadcast_addr, inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr), 15);
		print_debug(LOG_NOTICE, "relay to broadcast address: %s\n", broadcast_addr);
	}
	/* TODO: add a udp socket for nmea stream/coordinate input */
#endif

	/* create server socket */
	myservfd = socket(af, SOCK_STREAM, 0);
	if (myservfd < 0) {
		sock_error("wmasterd: socket");
		print_debug(LOG_ERR, "cannot open SOCK_STREAM server");
		return EXIT_FAILURE;
	}
	opt = 1;
	if (setsockopt(myservfd, SOL_SOCKET, SO_REUSEADDR,
			(char *)&opt, sizeof(opt)) < 0) {
		sock_error("wmasterd: setsockopt");
		print_debug(LOG_ERR, "cannot set socket options");
		return EXIT_FAILURE;
	}

	/* we can initalize this struct because it never changes */
	if (vsock) {
		memset(&myservaddr_vm, 0, sizeof(myservaddr_vm));
		myservaddr_vm.svm_cid = cid;
		myservaddr_vm.svm_port = port;
		myservaddr_vm.svm_family = af; /* AF_VSOCK */
		ret = bind(myservfd, (struct sockaddr *)&myservaddr_vm,
				sizeof(struct sockaddr));
	} else {
		memset(&myservaddr_in, 0, sizeof(myservaddr_in));
		myservaddr_in.sin_addr.s_addr = INADDR_ANY;
		myservaddr_in.sin_port = port;
		myservaddr_in.sin_family = af; /* AF_INET */
		ret = bind(myservfd, (struct sockaddr *)&myservaddr_in,
				sizeof(struct sockaddr));
	}

	if (ret < 0) {
		sock_error("wmasterd: myservaddr");
		print_debug(LOG_ERR, "invalid address");
		return EXIT_FAILURE;
	}

	/* listen on vsock server for up backlog of 1000 connections */
	ret = listen(myservfd, MAX_CLIENTS);
	if (ret < 0) {
		sock_error("wmasterd: listen");
		return EXIT_FAILURE;
	}

	if (vsock) {
		print_debug(LOG_NOTICE, "listening on vsock %u:%d",
				myservaddr_vm.svm_cid, myservaddr_vm.svm_port);
	} else {
		print_debug(LOG_NOTICE, "listening on ipv4 %s:%d",
				inet_ntoa(myservaddr_in.sin_addr), myservaddr_in.sin_port);
	}

	pthread_mutex_init(&list_mutex, NULL);
	pthread_mutex_init(&file_mutex, NULL);

	if (gps) {
		/* start thread to send nmea */
		ret = pthread_create(&nmea_tid, NULL, produce_nmea, NULL);
		if (ret < 0) {
			sock_error("wmasterd: pthread_create produce_nmea");
			print_debug(LOG_ERR, "pthread_create produce_nmea");
			exit(EXIT_FAILURE);
		}
	}

	/* start thread to read console */
	ret = pthread_create(&console_tid, NULL, read_console, NULL);
	if (ret < 0) {
		sock_error("wmasterd: pthread_create read_console");
		print_debug(LOG_ERR, "pthread_create read_console");
		exit(EXIT_FAILURE);
	}

#ifndef _WIN32
	/* start thread to receive from other hosts */
	if (esx && broadcast) {
		ret = pthread_create(&hosts_tid, NULL, recv_from_hosts, NULL);
		if (ret < 0) {
			perror("wmasterd: pthread_create recv_from_hosts");
			print_debug(LOG_ERR, "pthread_create recv_from_hosts");
			exit(EXIT_FAILURE);
		}
	}
#endif
	print_debug(LOG_DEBUG, "accepting connections");

	fd_set readfds;
	int i;
	int max_sd;
	int sd;

	/* intialize client array */
	for (i = 0; i < MAX_CLIENTS; i++) {
		client_socket[i] = 0;
	}
	tv.tv_sec = 1;
	tv.tv_usec = 0;

	/* We wait for incoming msg unless kill signal received */
	while (running) {

		/* print status is requested by usr1 signal or console thread */
		if (print_status) {
			print_debug(LOG_INFO, "status requested");
			pthread_mutex_lock(&list_mutex);
			list_nodes();
			pthread_mutex_unlock(&list_mutex);
			print_status = 0;
		}

		FD_ZERO(&readfds);
		FD_SET(myservfd, &readfds);
		max_sd = myservfd;

		/* add child sockets to set */
		for (i = 0; i < MAX_CLIENTS; i++) {
			/* socket descriptor */
	    	sd = client_socket[i];

	    	// if valid socket descriptor then add to read list
			// if it is set, just re-add it
	    	if (sd > 0) {
				//print_debug(LOG_DEBUG, "adding socket %d to fd set", sd);
				FD_SET(sd, &readfds);
			}
	    		/* highest file descriptor number, need it for the select function */
	    		if (sd > max_sd) {
				max_sd = sd;
			}
		}

		ret = select(max_sd + 1, &readfds, NULL, NULL, &tv);

		if (ret < 0) {
			perror("wmasterd: select");
			continue;
		} else if (ret == 0) {
			/* timeout awaiting connection or data */
			continue;
		}

		socklen_t client_len;
		int client_fd;
		struct sockaddr_vm client_vm;
		struct sockaddr_in client_in;
		void *client_info;
		unsigned int src_addr;
		unsigned int src_port;

		if (vsock) {
			client_len = sizeof(client_vm);
			client_info = &client_vm;
		} else {
			client_len = sizeof(client_in);
			client_info = &client_in;
		}
		if (FD_ISSET(myservfd, &readfds)) {

			/* clear/reset client address information */
			memset(client_info, 0, client_len);

			client_fd = -1;

			/* accept connection */
			client_fd = accept(myservfd, (struct sockaddr *)&client_info, &client_len);

			if (client_fd < 0) {
				print_debug(LOG_ERR, "accept failed");
				sock_error("wmasterd: accept");
				continue;
			}

			if (vsock) {
				ret = getpeername(client_fd, (struct sockaddr *)&client_vm,
						(socklen_t *)&client_len);
			} else {
				ret = getpeername(client_fd, (struct sockaddr *)&client_in,
						(socklen_t *)&client_len);
			}
			if (ret < 0) {
				print_debug(LOG_ERR, "error getting peername on socket %d", sd);
			}

			if (vsock) {
				src_addr = client_vm.svm_cid;
				src_port = client_vm.svm_port;
				print_debug(LOG_INFO, "vsock connection accepted from %16u:%-5d",
						src_addr, src_port);
			} else {
				src_addr = client_in.sin_addr.s_addr;
				src_port = client_in.sin_port;
				print_debug(LOG_INFO, "ipv4 connection accepted from %16s:%-5d",
						inet_ntoa(client_in.sin_addr), client_in.sin_port);
			}

			/* add new socket to array of sockets */
			for (i = 0; i < MAX_CLIENTS; i++) {
				/* use first empty position */
				if (client_socket[i] == 0) {
					client_socket[i] = client_fd;
					break;
				}
			}
			/* we are now watching the new socket and will add a node when we get data on it */
		}

		/* check for data on connected sockets */
		for (i = 0; i < MAX_CLIENTS; i++) {
			sd = client_socket[i];

			if (FD_ISSET(sd, &readfds)) {
				print_debug(LOG_DEBUG, "got event on socket %d", sd);
				memset(&client_info, 0, sizeof(client_info));
				if (vsock) {
					ret = getpeername(sd, (struct sockaddr *)&client_vm,
		    					(socklen_t *)&client_len);
				} else {
					ret = getpeername(sd, (struct sockaddr *)&client_in,
							(socklen_t *)&client_len);
				}
				if (ret < 0) {
					print_debug(LOG_INFO, "client disconnected on socket %d", sd);
					close(sd);
					client_socket[i] = 0;
					remove_nodes_by_socket(sd);
				} else {
					/* process data on connection */
					if (vsock) {
						src_addr = client_vm.svm_cid;
						src_port = client_vm.svm_port;
						print_debug(LOG_DEBUG, "reading from %16u:%-5d on socket %d" ,
								src_addr, src_port, sd);
					} else {
						src_addr = client_in.sin_addr.s_addr;
						src_port = client_in.sin_port;
						print_debug(LOG_DEBUG, "reading from %16s:%-5d on socket %d" ,
								inet_ntoa(client_in.sin_addr), src_port, sd);
					}

					char buf[WMASTERD_BUFF_LEN];
					int bytes;

					bytes = recv(sd, (char *)buf, WMASTERD_BUFF_LEN, 0);
					if (bytes <= 0) {
						sock_error("wmasterd: recv");
						if (vsock) {
							print_debug(LOG_INFO, "client %16u:%-5d disconnected on sock %d during recv",
									src_addr, src_port, sd);
						} else {
							print_debug(LOG_INFO, "client %16s:%-5d disconnected on sock %d during recv",
									inet_ntoa(client_in.sin_addr), src_port, sd);
						}
						/* close the socket and mark for reuse */
						print_debug(LOG_INFO, "removing socket %d from sd set", sd);
						close(sd);
						client_socket[i] = 0;
						remove_nodes_by_socket(sd);
						continue;
					}

					if (vsock) {
						print_debug(LOG_INFO, "received %4d bytes from: %16u:%-5d socket: %4d",
								bytes, src_addr, src_port, sd);
					} else {
						print_debug(LOG_INFO, "received %4d bytes from: %16s:%-5d socket: %4d",
								bytes, inet_ntoa(client_in.sin_addr), src_port, sd);
					}

					struct message_hdr *hdr = (struct message_hdr *)buf;
					// TODO what if we have invalid header?
					if (hdr->src_radio_id == -1) {
						/* just an up message */
						continue;
					}

					struct client *node;
					node = get_node_by_radio(src_addr, src_port, hdr->src_radio_id);

					if (!node) {
						print_debug(LOG_DEBUG, "no nodes exist for this host with radio id: %d", hdr->src_radio_id);
						int wsd = 0;
						int gsd = 0;
						if ((strncmp(hdr->name, "gelled", 6) == 0)) {
							wsd = 0;
							gsd = sd;
						} else if ((strncmp(hdr->name, "welled", 6) == 0)) {
							wsd = sd;
							gsd = 0;
						} else {
							print_debug(LOG_ERR, "error - unknown data: hdr->name = %s", hdr->name);
							continue;
						}
						/* process new connection (add to node linked list) */
						pthread_mutex_lock(&list_mutex);
						node = process_connection(src_addr, src_port, wsd, gsd, hdr->src_radio_id);
						pthread_mutex_unlock(&list_mutex);
					}
					if (!node) {
						print_debug(LOG_ERR, "error processing connection and setting node");
						continue;
					}

					/* process gelled data */
					if (node->gelled_socket) {
						if (vsock) {
							print_debug(LOG_INFO, "node %16d:%-5d is gelled %s",
									src_addr, src_port, hdr->version);
						} else {
							print_debug(LOG_INFO, "node %s:%-5d is gelled %s",
									inet_ntoa(client_in.sin_addr), src_port, hdr->version);
						}

						/* check for gelled updates from gelled-ctrl */
						if (bytes == (sizeof(struct update_2) + sizeof(struct message_hdr))) {
							if (vsock) {
								print_debug(LOG_INFO, "gelled update version 2 received from %16d:%-5d",
										src_addr, src_port);
							} else {
								print_debug(LOG_INFO, "gelled update version 2 received from %16s:%-5d",
										inet_ntoa(client_in.sin_addr), src_port);
							}

							struct update_2 data_2;

							/* pull loc from the buffer */
							memcpy(&data_2, buf + sizeof(struct message_hdr), sizeof(struct update_2));
							// TODO update the radio
							update_node_info(node, &data_2);
							update_node_location(node, &data_2);
							continue;
						}
					}

					/* process welled data */
					if (node->welled_socket) {
						if (vsock) {
							print_debug(LOG_DEBUG, "node %16d:%-5d is welled version %s",
									src_addr, src_port, hdr->version);
						} else {
							print_debug(LOG_DEBUG, "node %s:%-5d is welled version %s",
									inet_ntoa(client_in.sin_addr), src_port, hdr->version);
						}

						if (bytes > hdr->len) {
							print_debug(LOG_DEBUG, "we have more than one message");
						} else {
							print_debug(LOG_DEBUG, "we have one message");
						}
						int processed = 0;
						while (processed < bytes) {
							print_debug(LOG_DEBUG, "next message is %d bytes", hdr->len);
							if (hdr->len == 0) {
								print_debug(LOG_ERR, "next message is missing header");
								// TODO ignore or check a queue if we create one
								_exit(EXIT_FAILURE);
							} else if ((bytes - processed) < hdr->len) {
								int missing = hdr->len - (bytes - processed);
								print_debug(LOG_ERR, "next message is missing %d bytes", missing);
								// TODO queue?
								_exit(EXIT_FAILURE);
							}
							char *message = malloc(hdr->len);
							memcpy(message, hdr, hdr->len);
							/* lock list and send to all clients */
							print_debug(LOG_DEBUG, "looking for node on this host with radio id: %d", hdr->src_radio_id);
							node = get_node_by_radio(src_addr, src_port, hdr->src_radio_id);
							if (!node) {
								print_debug(LOG_DEBUG, "no nodes exist for this host with radio id: %d", hdr->src_radio_id);
								/* process new connection (add to node linked list) */
								pthread_mutex_lock(&list_mutex);
								node = process_connection(src_addr, src_port, sd, 0, hdr->src_radio_id);
								pthread_mutex_unlock(&list_mutex);
							}
							if (hdr->cmd == WMASTERD_ADD) {
								/* should alrady be added at this point */
							} else if (hdr->cmd == WMASTERD_DELETE) {
								print_debug(LOG_INFO, "removing node with radio id: %d", hdr->src_radio_id);
								pthread_mutex_lock(&list_mutex);
								remove_node(node->address, node->port, hdr->src_radio_id);
								pthread_mutex_unlock(&list_mutex);
							} else if (hdr->cmd == WMASTERD_UPDATE) {
								print_debug(LOG_INFO, "updating node with radio id: %d", hdr->src_radio_id);
								node->time = time(NULL);
								// TODO update netnsid if tracked here
								//pthread_mutex_lock(&list_mutex);
								//pthread_mutex_unlock(&list_mutex);
							} else if (hdr->cmd == WMASTERD_FRAME) {
								pthread_mutex_lock(&list_mutex);
								relay_to_nodes(message, hdr->len, node);
								pthread_mutex_unlock(&list_mutex);
							}
							free(message);
							processed += hdr->len;
							hdr = (struct message_hdr *)(buf + processed);
							print_debug(LOG_DEBUG, "processed %d of %d bytes", processed, bytes);
						}
						print_debug(LOG_DEBUG, "processed all messages in buffer");




#ifndef _WIN32
						/* send to other wmasterd hosts */
						send_to_hosts(buf, bytes, node->room);
#endif
					}
				}
			}
		}
	}

	print_debug(LOG_INFO, "Shutting down...");

	if (gps) {
		pthread_cancel(nmea_tid);
		pthread_join(nmea_tid, NULL);
	}

	pthread_cancel(console_tid);
	pthread_join(console_tid, NULL);

	print_debug(LOG_INFO, "Threads have been cancelled");

	/* cleanup */
	free_list();

	pthread_mutex_destroy(&list_mutex);
	pthread_mutex_destroy(&file_mutex);

	print_debug(LOG_INFO, "Mutices have been destroyed\n");

	if (cache_fp) {
		fclose(cache_fp);
	}
	/* close sockets*/
	close(myservfd);
#ifndef _WIN32
	close(vsock_dev_fd);
#endif

	print_debug(LOG_NOTICE, "Exiting\n");

	return EXIT_SUCCESS;
}

