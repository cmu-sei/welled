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
  SERVICE_STATUS	  g_ServiceStatus = {0};
  SERVICE_STATUS_HANDLE   g_StatusHandle = NULL;
  HANDLE		  g_ServiceStopEvent = INVALID_HANDLE_VALUE;
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

/** broadcast address for main interface */
char broadcast_addr[16];
/** Whether to broadcast frames to host subnet */
int broadcast;
/** Port used to receive frames from welled or gelled */
#define RECV_PORT       1111
/** Port used to send frames to welled */
#define SEND_PORT       2222
/** Port used to send frames to gelled */
#define SEND_PORT_G     3333
/** Buffer size for VMCI datagrams */
#define VMCI_BUFF_LEN   10000
/** Buffer size for UDP datagrams */
#define BUFF_LEN	10000
/** Buffer size for VMX config file */
#define LINE_BUF	8192

/** mutex for linked list access */
pthread_mutex_t list_mutex;
/** mutex for cache file access */
pthread_mutex_t file_mutex;
/** thread id for sending nmea */
pthread_t nmea_tid;
/** thread id for receving broadcast frames */
pthread_t hosts_tid;

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

int af;
int sockfd;
int myservfd;
/** sockaddr_vm for vmci send */
struct sockaddr_vm servaddr_vm;
/** sockaddr_vm for vmci receive */
struct sockaddr_vm myservaddr_vm;

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

	printf("Usage: wmasterd [-hVvbrud] [-D <level>] [-c <file>]\n\n");

	printf("Options:\n");
	printf("  -h, --help		print this help and exit\n");
	printf("  -V, --version		print version and exit\n");
	printf("  -v, --verbose		verbose output\n");
	printf("  -b, --broadcast       broadcast frames to other hosts\n");
	printf("  -r, --no-room-check	do not check room id\n");
	printf("  -u, --update-room     update room on receipt\n");
	printf("  -d, --distance	prepend distance to frames\n");
	printf("  -D, --debug		debug level for syslog\n");
	printf("  -c, --cache		file to save location data\n\n");

	printf("Copyright (C) 2015 Carnegie Mellon University\n\n");
	printf("License GPLv2: GNU GPL version 2 <http://gnu.org/licenses/gpl.html>\n");
	printf("This is free software; you are free to change and redistribute it.\n");
	printf("There is NO WARRANTY, to the extent permitted by law.\n\n");

	printf("Report bugs to <arwelle@cert.org>\n\n");

	exit(exval);
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
	syslog(level, "%s", buffer);
	#else
	printf("wmasterd: %s\n", buffer);
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
		if (min < 0)
			min *= -1;
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
		if (min < 0)
			min *= -1;
		snprintf(temp, len, "%03d%07.4f,%c",
				abs(deg), min, ch);
		strncpy(dest, temp, len);
		break;
	}
	free(temp);
	return;
}

/**
 *
 */
void update_cache_file_info(struct client *node)
{
	char buf[1024];
	unsigned int cid;
	int ret;
	int line_num;
	long pos;
	int matched;

	if (node == NULL)
		return;

	if (!cache)
		return;

	pthread_mutex_lock(&file_mutex);

	fseek(cache_fp, 0, SEEK_SET);
	pos = 0;
	line_num = 0;
	matched = 0;

	while ((fgets(buf, 1024, cache_fp)) != NULL) {
		ret = sscanf(buf, "%d ", &cid);

		if (ret != 1) {
			print_debug(LOG_ERR, "error: did not parseline for '%s'\n", buf);
		} else if (cid == node->cid) {
			matched = 1;
			break;
		}
		line_num++;
		pos = ftell(cache_fp);
	}
	/* end of file reached */

	/* check for match */
	if (matched) {
		fseek(cache_fp, pos, SEEK_SET);

		fprintf(cache_fp,
			"%-11d %-36s %-9.6f %-10.6f %-6.0f %-8.2f %-6.2f %-6.2f %-32s",
			node->cid, node->room,
			node->loc.latitude, node->loc.longitude,
			node->loc.altitude,
			node->loc.velocity,
			node->loc.heading,
			node->loc.pitch,
			node->name);
		fflush(cache_fp);
	} else {
		print_debug(LOG_ERR, "error: no match for node %d in cache file\n", node->cid);
	}

	pthread_mutex_unlock(&file_mutex);
}

/**
 *	update the cache file which stores location data
 */
void update_cache_file_location(struct client *node)
{
	char buf[1024];
	unsigned int cid;
	int ret;
	int line_num;
	long pos;
	int matched;

	if (!cache)
		return;

	if (node == NULL)
		return;

	print_debug(LOG_DEBUG, "updating cache file for node location\n");

	pthread_mutex_lock(&file_mutex);

	fseek(cache_fp, 0, SEEK_SET);
	pos = 0;
	line_num = 0;
	matched = 0;

	while ((fgets(buf, 1024, cache_fp)) != NULL) {
		ret = sscanf(buf, "%d ", &cid);

		if (ret != 1) {
			print_debug(LOG_ERR, "error: did not parseline for '%s'\n", buf);
		} else if (cid == node->cid) {
			matched = 1;
			break;
		}
		line_num++;
		pos = ftell(cache_fp);
	}
	/* wmasterd: end of file reached */

	/* check for match */
	if (matched) {
		fseek(cache_fp, pos, SEEK_SET);

		fprintf(cache_fp,
			"%-11d %-36s %-9.6f %-10.6f %-6.0f %-8.2f %-6.2f %-6.2f %-32s",
			node->cid, node->room,
			node->loc.latitude, node->loc.longitude,
			node->loc.altitude,
			node->loc.velocity,
			node->loc.heading,
			node->loc.pitch,
			node->name);
		fflush(cache_fp);
	} else {
		print_debug(LOG_WARNING, "no match for node %d in cache file\n",
				node->cid);
	}

	pthread_mutex_unlock(&file_mutex);
}

/**
 *	find and update follows of this vm by name
 */
void update_followers(struct client *node)
{
	struct client *curr;

	if (node == NULL)
		return;

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
			print_debug(LOG_NOTICE, "follower %s synced to master %s\n",
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

	/* radius of earth in meters */
	r = 6378137;

	/* a passed location means update data */
	if (data) {
		print_debug(LOG_DEBUG, "processing update");

		/* check for follow and set in node */
		if (strnlen(data->follow, FOLLOW_LEN) > 0) {
			print_debug(LOG_DEBUG, "follow exists in update");
			if (strncmp(data->follow, "CLEAR", 5) == 0) {
				print_debug(LOG_NOTICE, "clearing follow on %d\n", node->cid);
				memset(node->loc.follow, 0, FOLLOW_LEN);
			} else {
				strncpy(node->loc.follow,
						data->follow, FOLLOW_LEN - 1);
				print_debug(LOG_NOTICE, "set follow to %s on %d\n",
						node->loc.follow, node->cid);
			}
		}

		if (strnlen(node->loc.follow, FOLLOW_LEN) > 0) {
			print_debug(LOG_DEBUG, "we need to update a master instead of this node");
			struct client *master = search_node_name(node->loc.follow);
			//node = search_node_name(node->loc.follow);

			/*
			 * TODO: check windows
			 * it will fail name check unless set
			 */

			if (!master) {
				print_debug(LOG_INFO, "no master for follow on %d\n", node->cid);
			} else {
				print_debug(LOG_DEBUG, "node set to follow %s", node->loc.follow);
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
		if (data->velocity != -1)
			node->loc.velocity = data->velocity;

		/* update heading */
		if (data->heading != -1)
			node->loc.heading = data->heading;

		/* update pitch angle */
		if (data->pitch != -1)
			node->loc.pitch = data->pitch;

		/* update coordinates */
		if ((data->latitude >= -90) &&
				(data->latitude <= 90))
			node->loc.latitude = data->latitude;
		if ((data->longitude >= -180) &&
				(data->longitude <= 180))
			node->loc.longitude = data->longitude;

		/* update altitude */
		if (data->altitude != -1)
			node->loc.altitude = data->altitude;

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

		int age = time(NULL) - node->time;

		print_debug(LOG_NOTICE,
			"%-11d %-36s %-4d %-9.6f %-10.6f %-6.0f %-8.2f %-6.2f %-6.2f %-s\n",
			node->cid, node->room, age,
			node->loc.latitude, node->loc.longitude,
			node->loc.altitude,
			node->loc.velocity,
			node->loc.heading,
			node->loc.pitch,
			node->name);

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
		/* printf("wmasterd: crossing north pole\n"); */
		overage = node->loc.latitude - 90;
		node->loc.latitude = 90 - overage;
		if (node->loc.heading < 90)
			node->loc.heading += 180;
		else if (node->loc.heading > 270)
			node->loc.heading -= 180;
		if (node->loc.longitude < 0)
			node->loc.longitude += 180;
		else
			node->loc.longitude -= 180;
	}

	/* adjust heading, lat and lon as we cross south pole */
	if (node->loc.latitude < -90) {
		/* printf("wmasterd: crossing south pole\n"); */
		overage = node->loc.latitude + 90;
		node->loc.latitude = -90 - overage;
		if (node->loc.heading > 90)
			node->loc.heading += 180;
		else if (node->loc.heading < 270)
			node->loc.heading -= 180;
		if (node->loc.longitude < 0)
			node->loc.longitude += 180;
		else
			node->loc.longitude -= 180;
	}

	/* adjust longitude as we cross east to west */
	if (node->loc.longitude > 180) {
		/* printf("wmasterd: crossing prime meridian\n"); */
		node->loc.longitude -= 360;
	}
	/* adjust longitude as we cross west to east */
	if (node->loc.longitude < -180) {
		/* printf("wmasterd: crossing dateline\n"); */
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
	print_debug(LOG_NOTICE,
		"%-11d %-36s %-4d %-9.6f %-10.6f %-6.0f %-8.2f %-6.2f %-6.2f %-s\n",
		node->cid, node->room, age,
		node->loc.latitude, node->loc.longitude, node->loc.altitude,
		node->loc.velocity, node->loc.heading, node->loc.pitch,
		node->name);

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
	snprintf(temp, NMEA_LEN, "GPGGA,%s,%s,%s,2,09,1.0,%05.2f,M,0,M,0,0", timestamp, lat, lon, node->loc.altitude);
	checksum = nmea_checksum(temp);
	snprintf(node->loc.nmea_gga, NMEA_LEN, "$%s*%2X", temp, checksum);

	if (send_pashr) {
		/* PASHR */
		memset(temp, 0, NMEA_LEN);
		snprintf(temp, NMEA_LEN, "PASHR,%s,%.2f,T,000.00,%03.2f,000.00,0.000,0.000,0.000,0,0", timestamp, node->loc.heading, node->loc.pitch);
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
		send_gps_to_nodes();
		sleep(1);
	}
	return ((void *)0);
}

/**
 *	Send NMEA sentence for current location to all nodes
 */
void send_gps_to_nodes(void)
{
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
		servaddr_vm.svm_cid = curr->cid;
		servaddr_vm.svm_port = SEND_PORT_G;
		servaddr_vm.svm_family = af;

		/* send frame to this welled client */
		ret = sendto(sockfd, (char *)buf, bytes, 0,
				(struct sockaddr *)&servaddr_vm,
				sizeof(struct sockaddr));
		if (ret < 0) {
			print_debug(LOG_NOTICE, "del: %11d room: %36s time: %d name: %s", curr->cid, curr->room, curr->time, curr->name);

			if (verbose)
				sock_error("wmasterd: sendto");

			/*
			 * since powering off a VM results in this error
			 * we remove the node from list
			 */
			temp = curr->next;
			remove_node_vmci(curr->cid);
			curr = temp;
		} else {
			print_debug(LOG_DEBUG, "sent %d/%d bytes: %s", ret, bytes, buf);
			/* transmission successful, send gga */
			/* gga provides altitude */
			buf = curr->loc.nmea_gga;
			bytes = strlen(buf);
			sendto(sockfd, (char *)buf, bytes, 0,
				(struct sockaddr *)&servaddr_vm,
				sizeof(struct sockaddr));

			if (send_pashr) {
				/* send the PASHR message with pitch */
				buf = curr->loc.nmea_pashr;
				bytes = strlen(buf);
				sendto(sockfd, (char *)buf, bytes, 0,
					(struct sockaddr *)&servaddr_vm,
					sizeof(struct sockaddr));
			}
		}
		if (curr != NULL)
			curr = curr->next;
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

	print_debug(LOG_DEBUG, "%d meters between %s and %s \n",
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

	if (sigemptyset(&intmask) == -1)
		perror("wmasterd: sigemptyset");
	else if (sigaddset(&intmask, SIGUSR1) == -1)
		perror("wmasterd: sigaddset");
	else if (sigprocmask(SIG_BLOCK, &intmask, NULL) < 0)
		perror("wmasterd: sigprocmask");
}

/**
 *	@brief Restores usr1 for use during restartable syscalls
 *	@return void
 */
void unblock_signal(void)
{
	sigset_t intmask;

	if (sigemptyset(&intmask) == -1)
		perror("wmasterd: sigemptyset");
	else if (sigaddset(&intmask, SIGUSR1) == -1)
		perror("wmasterd: sigaddset");
	else if (sigprocmask(SIG_UNBLOCK, &intmask, NULL) < 0)
		perror("wmasterd: sigprocmask");
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
 *	and guestinfo lines
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
		print_debug(LOG_ERR, "could not open vmx %s\n", vmx_file);
		list_nodes_vmci();
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
		print_debug(LOG_DEBUG, "cid %11d is a match for name %s\n",
				cid, name);
		/* guestinfo variables not found (they override annotation */
		if (!room_found) {
			/* parse the room id out of the annotation line */
			if (!parse_annotation(annotation, room)) {
				/* set room to 0 if not found */
				strncpy(room, "0", 2);
			}
		}
		print_debug(LOG_DEBUG, "cid %11d is in room %s\n", cid, room);
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
			printf("wmasterd: does not open vmx on linux\n");
		/* TODO: implement check for linux host
		 * this would be a machine running workstation
		 */
		pipe = NULL;
	}
#endif

	if (!pipe) {
		perror("wmasterd: pipe");
		print_debug(LOG_ERR, "error: popen failed to produce pipe\n");
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
		if (strncmp(line, "Total", 5) == 0)
			continue;
		line[strnlen(line, 1024) - 1] = '\0';
		snprintf(vmx, 1024, "\"%s\"", line);
		if (parse_vmx(vmx, srchost, room, name, uuid))
			room_found = 1;
#else
		/* continue if room found or this is not a config file line */
		if (strncmp(line, "   Config File:", 15) != 0)
			continue;
		line_len = strnlen(line, 1024);
		strncpy(vmx, line + 16, line_len - 17);
		if (parse_vmx(vmx, srchost, room, name, uuid))
			room_found = 1;
#endif
		if (room_found)
			break;
	}
	if (!room_found) {
		strncpy(room, "0", 2);
		print_debug(LOG_ERR, "error: no room found for %d", srchost);
	}

#ifdef _WIN32
	ret = _pclose(pipe);
#else
	ret = pclose(pipe);
#endif

	if (ret < 0)
		perror("wmasterd: pclose");
}

/**
 *	@brief Adds a node to the linked list
 *	@param srchost - the CID of the guest node vm
 *	@param room - the room uuid of the vm
 *	@param vm_name - the name of the vm
 *	@param uuid - the uuid of the vm
 *	@return void
 */
void add_node_vmci(unsigned int srchost, char *vm_room, char *vm_name, char *uuid)
{
	print_debug(LOG_NOTICE, "adding node");

	struct client *node;
	struct client *curr;
	char buf[1024];
	unsigned int cid;
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

	matched = 0;

	/* create new node */
	node = malloc(sizeof(struct client));
	node->cid = srchost;
	node->next = NULL;
	node->time = time(NULL);
	memset(&node->loc, 0, sizeof(struct location));
	memset(node->name, 0, NAME_LEN);
	memset(node->uuid, 0, UUID_LEN);
	memset(node->room, 0, UUID_LEN);
	strncpy(node->name, vm_name, NAME_LEN - 1);
	strncpy(node->uuid, uuid, UUID_LEN - 1);
	strncpy(node->room, vm_room, UUID_LEN - 1);

	/* default to med sea */
	node->loc.latitude = 35;	/* DD.DDDD*/
	node->loc.longitude = 35;       /* DD.DDDD*/
	node->loc.altitude = 0;		/* meters */
	node->loc.velocity = 0;		/* knots */
	node->loc.heading = 0;		/* degrees */
	node->loc.altitude = 0;		/* meters */
	node->loc.pitch = 0;		/* degrees */

	/* add node to cache file */
	if (cache) {
		pthread_mutex_lock(&file_mutex);

		fseek(cache_fp, 0, SEEK_SET);

		/* TODO: update the cache file to include alt and pitch */

		while (((fgets(buf, 1024, cache_fp)) != NULL) && (!matched)) {
			ret = sscanf(buf, "%d %s %lf %lf %lf %lf %lf %lf %s",
				&cid, room, &lat, &lon, &alt, &sog, &cog,
				&pit, name);
			if ((ret != 8) && (ret != 9)) {
				print_debug(LOG_ERR, "error: did not parseline for '%s'\n", buf);
				printf("only matched %d variables:\n", ret);
				printf("cid:  %d\n", cid);
				printf("room: %s\n", room);
				printf("lat:  %f\n", lat);
				printf("lon:  %f\n", lon);
				printf("alt:  %f\n", alt);
				printf("sog:  %f\n", sog);
				printf("cog:  %f\n", cog);
				printf("pit:  %f\n", pit);
				printf("name: %s\n", name);
				continue;
			} else if (cid == srchost) {
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
			}
		}

		if (!matched) {
			print_debug(LOG_DEBUG,
					"adding node to the cache file\n");
			fseek(cache_fp, 0, SEEK_END);
			fprintf(cache_fp,
				"%-11d %-36s %-9.6f %-10.6f %-6.0f %-8.2f %-6.2f %-6.2f %-32s\n",
				node->cid, node->room,
				node->loc.latitude, node->loc.longitude,
				node->loc.altitude,
				node->loc.velocity,
				node->loc.heading,
				node->loc.pitch,
				node->name);
			fflush(cache_fp);
		}

		pthread_mutex_unlock(&file_mutex);
	}


	if (head == NULL) {
		/* add first node */
		head = node;
		print_debug(LOG_NOTICE, "add: %11d room: %36s time: %d name: %s", srchost, node->room, node->time, node->name);
	} else {
		/* traverse to end of list */
		curr = head;
		while (curr->next != NULL)
			curr = curr->next;
		/* add to node to end of list */
		curr->next = node;
		print_debug(LOG_NOTICE, "add: %11d room: %36s time: %d name: %s", srchost, node->room, node->time, node->name);
	}
}

/**
 *	@brief Removes old, presumably inactive nodes
 *	@return void
 */
void clear_inactive_nodes(void)
{
	struct client *curr;
	struct client *prev;
	int age;
	int now;

	age = 0;
	curr = head;
	prev = NULL;
	now = time(NULL);

	while (curr != NULL) {
		age = now - curr->time;
		if (age > 300) {
			print_debug(LOG_INFO, "node: %11d stale at %d sec",
					curr->cid, age);
			/* delete node */
			if (prev == NULL)
				head = curr->next;
			else
				prev->next = curr->next;

			print_debug(LOG_NOTICE, "del: %11d room: %36s time: %d name: %s", curr->cid, curr->room, curr->time, curr->name);
			free(curr);
			print_debug(LOG_DEBUG, "removed stale node");
			return;
		}
		prev = curr;
		curr = curr->next;
	}
}

/**
 *	@brief Searches the linked list for a given node
 *	@param srchost - CID of the guest
 *	@return returns the node
 */
struct client *search_node_vmci(unsigned int srchost)
{
	struct client *curr;

	curr = head;

	while (curr != NULL) {
		if (curr->cid == srchost) {
			curr->time = time(NULL);
			return curr;
		}
		curr = curr->next;
	}

	return NULL;
}

/**
 *
 */
struct client *search_node_name(char *name)
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

void print_node(struct client *node)
{
	printf("%d\n", node->cid);
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
void list_nodes_vmci(void)
{
	struct client *curr;
	FILE *fp;
	int age;

	curr = head;
	fp = fopen("/tmp/wmasterd.status", "w");

	if (!fp && esx) {
		perror("wmasterd: fopen");
		print_debug(LOG_WARNING, "could not open /tmp/wmasterd.status\n");
	}

	printf("node:       room:				age: lat:      lon:       alt:   sog:       cog: pitch: name:\n");

	if (fp)
		fprintf(fp, "node:       room:				age: lat:      lon:       alt:   sog:     cog:   pitch: name:\n");

	while (curr != NULL) {
		age = time(NULL) - curr->time;
		printf("%-11d %-36s %-4d %-9.6f %-10.6f %-6.0f %-8.2f %-6.2f %-6.2f %-s\n",
			curr->cid, curr->room, age,
			curr->loc.latitude, curr->loc.longitude,
			curr->loc.altitude,
			curr->loc.velocity,
			curr->loc.heading,
			curr->loc.pitch,
			curr->name);
		if (fp) {
			fprintf(fp, "%-11d %-36s %-4d %-9.6f %-10.6f %-6.0f %-8.2f %-6.2f %-6.2f %-s\n",
				curr->cid, curr->room, age,
				curr->loc.latitude, curr->loc.longitude,
				curr->loc.altitude,
				curr->loc.velocity,
				curr->loc.heading,
				curr->loc.pitch,
				curr->name);
		}
		curr = curr->next;
	}

	if (fp)
		fclose(fp);
}

/**
 *	@brief Removed a node from the linked list
 *	@param dsthost - CID of the guest
 *	@return void
 */
void remove_node_vmci(unsigned int dsthost)
{
	struct client *curr;
	struct client *prev;
	char name[NAME_LEN];
	char room[UUID_LEN];
	int time;

	time = 0;
	memset(name, 0, NAME_LEN);
	memset(room, 0, UUID_LEN);
	curr = head;
	prev = NULL;

	/* traverse while not null and no match */
	while (curr != NULL && !(curr->cid == dsthost)) {
		prev = curr;
		curr = curr->next;
	}

	/* exit if we hit the end of the list */
	if (curr == NULL) {
		print_debug(LOG_INFO, "node not found: %11d\n", dsthost);
		return;
	}

	strncpy(room, curr->room, strnlen(curr->room, UUID_LEN - 1));
	strncpy(name, curr->name, strnlen(curr->name, NAME_LEN - 1));
	time = curr->time;

	/* delete node */
	if (prev == NULL)
		head = curr->next;
	else
		prev->next = curr->next;

	free(curr);

	print_debug(LOG_NOTICE, "del: %11d room: %36s time: %d name: %s",
		dsthost, room, time, name);
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
	int i;
	unsigned char buff[17];
	unsigned char *pc;

	pc = (unsigned char *)addr;

	/* loop through all bytes */
	for (i = 0; i < len; i++) {
		/* new line every 16 bytes */
		if ((i % 16) == 0) {
			/* dont print ascii for the zeroth line */
			if (i != 0)
				printf("  %s\n", buff);
			/* print offset */
			printf("  %04x ", i);
		}

		/* print the hex for the byte */
		printf(" %02x", pc[i]);

		/* store ASCII character for later */
		if ((pc[i] < 0x20) || (pc[i] > 0x7e))
			buff[i % 16] = '.';
		else
			buff[i % 16] = pc[i];
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
	if (!esx || !broadcast)
		return;

	char *buffer;
	char temp[UUID_LEN + 1];
	struct sockaddr_in dest_addr;
	int sock_opts;
	int sockfd;
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
	sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR|SO_BROADCAST,
		(const char *)&sock_opts, sizeof(int));
	if (sockfd < 0) {
		sock_error("wmasterd: socket");
		print_debug(LOG_ERR, "error: could not create udp socket\n");
		free(buffer);
		return;
	}

	/* send packet */
	bytes_sent = sendto(sockfd, buffer, bytes + UUID_LEN, 0,
			(const struct sockaddr *)&dest_addr,
			sizeof(dest_addr));
	if (bytes_sent < 0)
		sock_error("wmasterd: sendto\n");
	else
		print_debug(LOG_DEBUG, "sent %d bytes to %s\n",
				bytes_sent, broadcast_addr);

	/* cleanup */
	close(sockfd);
	free(buffer);
}
#endif

/**
 *	@brief Sends message to all nodes in linked list
 *	@param buf - message data
 *	@param bytes - size of message data
 *	@param room - the uuid of the room
 *	@param node - the struct client node that sent the data
 *	@return void - assumes success
 */
void send_to_nodes_vmci(char *buf, int bytes, struct client *node)
{
	struct client *curr;
	struct client *temp;
	int age;
	int now;
	int distance;
	char *send_buf;
	int bytes_sent;

	curr = head;
	age = 0;
	now = time(NULL);

	print_debug(LOG_DEBUG, "sending to nodes in room %s", node->room);

	while (curr != NULL) {
		age = now - curr->time;

		/* skip node if room differs */
		if (check_room && (strncmp(
				curr->room, node->room, UUID_LEN - 1) != 0)) {
			print_debug(LOG_INFO, "skipped: %11d room: %36s\n", curr->cid, curr->room);
			curr = curr->next;
			continue;
		} else if (age > 300) {
			/* save our place */
			temp = curr->next;

			/* remove stale node */
			print_debug(LOG_DEBUG, "stale node found\n");
			remove_node_vmci(curr->cid);

			/* go to next node */
			curr = temp;
			continue;
		}

		memset(&servaddr_vm, 0, sizeof(servaddr_vm));
		servaddr_vm.svm_cid = curr->cid;
		servaddr_vm.svm_port = SEND_PORT;
		servaddr_vm.svm_family = af;

		/* send frame to this welled client */
		if (send_distance) {
			/* determine distance between the nodes */
			if (curr == node)
				distance = 0;
			else
				distance = get_distance(curr, node);

			/* skip nodes out of range */
			if ((distance > 2500) || (distance < 0)) {
				curr = curr->next;
				continue;
			}

			char temp[13];
			/* add the distance to the buf */
			send_buf = malloc(bytes + 12);
			memset(send_buf, 0, bytes + 12);
			memcpy(send_buf, buf, bytes);
			snprintf(temp, sizeof(temp), "welled:%04d:", distance);
			memcpy(send_buf + bytes, temp, 12);
			bytes_sent = sendto(sockfd, (char *)send_buf,
				bytes + 12, 0,
				(struct sockaddr *)&servaddr_vm,
				sizeof(struct sockaddr));
			free(send_buf);
		} else {
			/* send frame to this welled client */
			bytes_sent = sendto(sockfd, (char *)buf, bytes, 0,
				(struct sockaddr *)&servaddr_vm,
				sizeof(struct sockaddr));
		}
		if (bytes_sent < 0) {
			if (verbose) {
				sock_error("wmasterd: sendto");
				print_debug(LOG_ERR, "error: name %s cid %d bytes %d\n", curr->name, curr->cid, bytes + 12);
				print_node(curr);
			}
			/* since powering off a VM results in this error
			 * we should remove the node from list
			 */
			temp = curr->next;
			remove_node_vmci(curr->cid);
			curr = temp;
		} else {
			print_debug(LOG_DEBUG, "sent %d bytes to node: %11d room: %s", bytes, curr->cid, curr->room);

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

	char *buffer;
	struct client node;
	char src_host[16];
	char buf[BUFF_LEN];
	int sockfd;
	int bytes;
	struct sockaddr_in bindaddr;
	struct sockaddr_in cliaddr;
	socklen_t addrlen;

	addrlen = sizeof(struct sockaddr);
	memset(&cliaddr, 0, sizeof(cliaddr));
	memset(&bindaddr, 0, sizeof(bindaddr));

	// create socket
	sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sockfd < 0) {
		sock_error("wmasterd: socket");
		printf("wmasterd: could not create udp listen socket\n");
		exit(EXIT_FAILURE);
	}

	bindaddr.sin_family = AF_INET;
	bindaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	bindaddr.sin_port = htons(2018);

	// bind
	if (bind(sockfd, (struct sockaddr *)&bindaddr, sizeof(bindaddr)) < 0) {
		sock_error("wmasterd: bind");
	}

	while (running) {

		// recv packet
		bytes = recvfrom(sockfd, (char *)buf, BUFF_LEN, 0,
				(struct sockaddr *)&cliaddr, &addrlen);
		if (bytes < 0)
			continue;

		inet_ntop(AF_INET, &cliaddr.sin_addr, src_host, addrlen);
		if (verbose) {
			print_debug(LOG_DEBUG, "received %d bytes from src host: %d",
				bytes, src_host);
		}
		/* parse out room */
		strncpy(node.room, buf + bytes - UUID_LEN + 1, UUID_LEN);
		buffer = malloc(bytes - UUID_LEN - 1);

		/* lock list and send */
		pthread_mutex_lock(&list_mutex);
		send_to_nodes_vmci(buffer, bytes, &node);
		pthread_mutex_unlock(&list_mutex);

		/* cleanup */
		close(sockfd);
		free(buffer);
	}
	return ((void *)0);
}
#endif

/**
 *	@brief receive and process vmci packets from client nodes
 *	@return void - assumes success
 */
void recv_from_welled_vmci(void)
{
	char buf[VMCI_BUFF_LEN];
	struct sockaddr_vm cliaddr_vmci;
	socklen_t addrlen;
	int bytes;
	unsigned int src_cid;
	char room[UUID_LEN];
	char old_room[UUID_LEN];
	char name[NAME_LEN];
	char uuid[UUID_LEN];
	struct client *node;

	addrlen = sizeof(struct sockaddr);
	memset(&cliaddr_vmci, 0, sizeof(cliaddr_vmci));

	bytes = recvfrom(myservfd, (char *)buf, BUFF_LEN, 0,
			(struct sockaddr *)&cliaddr_vmci, &addrlen);
	if (bytes < 0)
		return;
	src_cid = (unsigned int)cliaddr_vmci.svm_cid;
	print_debug(LOG_DEBUG, "received %d bytes from src host: %d",
			bytes, src_cid);

	memset(room, 0, UUID_LEN);
	memset(old_room, 0, UUID_LEN);
	memset(name, 0, NAME_LEN);
	memset(uuid, 0, UUID_LEN);

	node = search_node_vmci(src_cid);
	if (!node) {
		print_debug(LOG_DEBUG, "node %11d does not exist", src_cid);
		get_vm_info(src_cid, room, name, uuid);
		add_node_vmci(src_cid, room, name, uuid);
		/* make sure we added the node */
		node = search_node_vmci(src_cid);
		if (!node) {
			print_debug(LOG_ERR, "error: adding node %11d",
					src_cid);
			return;
		}
	} else if (update_room && check_room) {
		print_debug(LOG_DEBUG, "checking vmx for room update\n");
		/* check for room change if room enforced */
		strncpy(old_room, node->room, UUID_LEN - 1);
		get_vm_info(src_cid, room, name, uuid);
		if (strncmp(old_room, room, UUID_LEN  - 1) != 0) {
			remove_node_vmci(src_cid);
			add_node_vmci(src_cid, room, name, uuid);
			/* make sure we updated the node */
			node = search_node_vmci(src_cid);
			if (!node) {
				print_debug(LOG_ERR, "error: updating node %11d\n", src_cid);
				return;
			}
		}
	}

	if ((bytes == 2) || (bytes == 5)) {
		print_debug(LOG_INFO, "node %11d has sent status",
					src_cid);
		return;
	}

	/*
	 * format of this message:
	 * gelled:<struct client>
	 */
	/* check for gelled updates from gelled-ctrl */
	if (strncmp(buf, "gelled:", 7) == 0) {

		/*
		 * if new size, copy into new buf
		 * if old size, copy into old buf, then copy
		 *	from the old into the new buf
		 */

		print_debug(LOG_INFO, "gelled update received from %11d",
				src_cid);

		struct update_2 data_2;

		/*
		 * update_1 is size 1100
		 * update_2 is size 1136
		 */

		if (bytes == (sizeof(struct update) + 7)) {
			print_debug(LOG_INFO, "update version 1 from %11d", src_cid);
			struct update data_1;
			/* pull loc from the buffer */
			memcpy(&data_1, buf + 7, sizeof(struct update));
			/* convert update to update_2 */
			strncpy(data_2.version, "1", 8);
			strncpy(data_2.follow, data_1.follow, FOLLOW_LEN - 1);
			data_2.latitude = data_1.latitude;
			data_2.longitude = data_1.longitude;
			data_2.altitude = data_1.altitude;
			data_2.velocity = data_1.velocity;
			data_2.heading = data_1.heading;
			snprintf(data_2.room, UUID_LEN, "%d", data_1.room_id);
			strncpy(data_2.name, data_1.name, NAME_LEN - 1);
			data_2.cid = data_1.cid;
		} else if (bytes == (sizeof(struct update_2) + 7)) {
			print_debug(LOG_INFO, "update version 2 from %11d", src_cid);
			print_debug(LOG_DEBUG, "copying %d bytes from buf to data_2 which is size %d",
					sizeof(struct update_2), sizeof(data_2));
			/* pull loc from the buffer */
			memcpy(&data_2, buf + 7, sizeof(struct update_2));
		} else {
			print_debug(LOG_ERR, "update version unknown from %11d", src_cid);
			return;
		}
		update_node_info(node, &data_2);
		update_node_location(node, &data_2);
		return;
	}

#ifndef _WIN32
	/* send to other wmasterd hosts */
	send_to_hosts(buf, bytes, node->room);
#endif

	/* not a status message or an update, relay */
	send_to_nodes_vmci(buf, bytes, node);
}

/**
 *	@brief main function
 */
int main(int argc, char *argv[])
{
	int opt;
	int cid;
	struct timeval tv;
	int ret;
	int long_index;
	fd_set fds;
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

	static struct option long_options[] = {
		{"help",		no_argument, 0, 'h'},
		{"version",		no_argument, 0, 'V'},
		{"verbose",		no_argument, 0, 'v'},
		{"no-check-room",	no_argument, 0, 'r'},
		{"update-room",		no_argument, 0, 'u'},
		{"distance",		no_argument, 0, 'd'},
		{"pashr",		no_argument, 0, 'p'},
		{"debug",		required_argument, 0, 'D'},
		{"cache",		required_argument, 0, 'c'}
	};

	while ((opt = getopt_long(argc, argv, "hVvbrudpD:c:", long_options,
			&long_index)) != -1) {
		switch (opt) {
		case 'h':
			show_usage(EXIT_SUCCESS);
			break;
		case 'V':
			/* allow help2man to read this */
			setbuf(stdout, NULL);
			printf("wmasterd version %s\n", VERSION_STR);
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
			printf("wmasterd: syslog level set to %d\n", loglevel);
			break;
		case 'p':
			send_pashr = 1;
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
		case '?':
			printf("wmasterd: Error - No such option: `%c'\n\n",
				optopt);
			show_usage(EXIT_FAILURE);
			break;
		}
	}

	if (optind < argc)
		show_usage(EXIT_FAILURE);

	#ifdef _WIN32
	WSAStartup(MAKEWORD(1,1), &wsa_data);
	if (broadcast) {
		printf("broadcast not implemented on windows\n");
		show_usage(EXIT_FAILURE);
	}
	/* TODO: use vm_sockets and ioctl to get cid */
	af = VMCISock_GetAFValue();
	cid = VMCISock_GetLocalCID();

	#else
	uname(&uts_buf);
	if (strncmp(uts_buf.sysname, "VMkernel", 8) == 0) {
		esx = 1;
	}

	#ifndef _WIN32
	if (loglevel >= 0)
		openlog("wmasterd", LOG_PID, LOG_USER);
	#endif

	print_debug(LOG_NOTICE, "Starting, version %s\n", VERSION_STR);

	/* TODO: add check for other hypervisors */

	vsock_dev_fd = open("/dev/vsock", 0);
	if (vsock_dev_fd < 0) {
		sock_error("wmasterd: open");
		print_debug(LOG_ERR, "could not open /dev/vsock\n");
		_exit(EXIT_FAILURE);
	}
	if (ioctl(vsock_dev_fd, IOCTL_VM_SOCKETS_GET_LOCAL_CID, &cid) < 0)
		perror("wmasterd: ioctl IOCTL_VM_SOCKETS_GET_LOCAL_CID");

	if (ioctl(vsock_dev_fd, IOCTL_VMCI_SOCKETS_GET_AF_VALUE, &af) < 0) {
		perror("wmasterd: ioctl IOCTL_VMCI_SOCKETS_GET_AF_VALUE");
		af = -1;
	}

	if (af == -1) {
		/* take a guess */
		if (esx)
			af = 53;
		else
			af = 40;
	}
	#endif
	printf("wmasterd: CID: %u\n", cid);

	/*Handle kill signals*/
	running = 1;
	#ifndef _WIN32
	signal(SIGINT, (void *)signal_handler);
	signal(SIGTERM, (void *)signal_handler);
	signal(SIGQUIT, (void *)signal_handler);
	signal(SIGUSR1, (void *)usr1_handler);
	signal(SIGHUP, SIG_IGN);
	#endif

#ifndef _WIN32
	char udp_int[8];
	if (esx)
		strncpy(udp_int, "vmk0", 8);
	else
		strncpy(udp_int, "ens33", 8);

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
#endif

	/* setup vmci client socket */
	sockfd = socket(af, SOCK_DGRAM, 0);
	if (sockfd < 0) {
		sock_error("wmasterd: socket");
		print_debug(LOG_ERR, "error: cannot open SOCK_DGRAM client");
		return EXIT_FAILURE;
	}

	/* TODO: add a udp socket for nmea stream/coordinate input */

	/* create vmci server socket */
	myservfd = socket(af, SOCK_DGRAM, 0);
	if (myservfd < 0) {
		sock_error("wmasterd: socket");
		print_debug(LOG_ERR, "error: cannot open SOCK_DGRAM server");
		return EXIT_FAILURE;
	}

	/* we can initalize this struct because it never changes */
	memset(&myservaddr_vm, 0, sizeof(myservaddr_vm));
	myservaddr_vm.svm_cid = VMADDR_CID_ANY;
	myservaddr_vm.svm_port = RECV_PORT;
	myservaddr_vm.svm_family = af;
	ret = bind(myservfd, (struct sockaddr *)&myservaddr_vm,
		sizeof(struct sockaddr));

	if (ret < 0) {
		sock_error("wmasterd: myservaddr");
		print_debug(LOG_ERR, "error: invalid address");
		return EXIT_FAILURE;
	}

	pthread_mutex_init(&list_mutex, NULL);
	pthread_mutex_init(&file_mutex, NULL);

	/* start thread to send nmea */
	ret = pthread_create(&nmea_tid, NULL, produce_nmea, NULL);
	if (ret < 0) {
		sock_error("wmasterd: pthread_create produce_nmea");
		print_debug(LOG_ERR, "error: pthread_create produce_nmea");
		exit(EXIT_FAILURE);
	}

#ifndef _WIN32
	/* start thread to receive from other hosts */
	if (esx && broadcast) {
		ret = pthread_create(&hosts_tid, NULL, recv_from_hosts, NULL);
		if (ret < 0) {
			perror("wmasterd: pthread_create recv_from_hosts");
			print_debug(LOG_ERR, "error: pthread_create recv_from_hosts");
			exit(EXIT_FAILURE);
		}
	}
#endif

	/* We wait for incoming msg unless kill signal received */
	while (running) {
		FD_ZERO(&fds);
		FD_SET(myservfd, &fds);

		ret = 0;

		/* we need a timer to break us out of the recvfrom function */
		tv.tv_sec = 0; /* seconds */
		tv.tv_usec = 500000; /* microseconds */

		ret = select(myservfd + 1, &fds, NULL, NULL, &tv);
		if (ret < 0) {
			/* timer error */
			continue;
		} else if (ret == 0) {
			/*
			 * timer expired
			 * only check for stale nodes when timer expires
			 * which is after .5 seconds of inactivity. when an
			 * AP is broadcasting, this should never happen.
			 */

			#ifndef _WIN32
			/* block signals while we perform node identification */
			block_signal();
			#endif

			pthread_mutex_lock(&list_mutex);
			clear_inactive_nodes();
			pthread_mutex_unlock(&list_mutex);

			#ifndef _WIN32
			/* unblock signal */
			unblock_signal();
			#endif
		} else {
			/* data recived */

			#ifndef _WIN32
			/* block signals while we perform node identification */
			block_signal();
			#endif

			pthread_mutex_lock(&list_mutex);
			recv_from_welled_vmci();
			pthread_mutex_unlock(&list_mutex);

			#ifndef _WIN32
			/* unblock signal */
			unblock_signal();
			#endif
		}

		/* print status is requested by usr1 signal */
		if (print_status) {
			pthread_mutex_lock(&list_mutex);
			print_debug(LOG_INFO, "Status requested\n");
			list_nodes_vmci();
			pthread_mutex_unlock(&list_mutex);
			print_status = 0;
		}
	}

	print_debug(LOG_INFO, "Shutting down...\n");

	pthread_cancel(nmea_tid);
	pthread_join(nmea_tid, NULL);

	print_debug(LOG_INFO, "Threads have been cancelled\n");

	/* cleanup */
	free_list();

	pthread_mutex_destroy(&list_mutex);
	pthread_mutex_destroy(&file_mutex);

	print_debug(LOG_INFO, "Mutices have been destroyed\n");

	if (cache_fp)
		fclose(cache_fp);

	/* close sockets*/
	close(myservfd);
	#ifndef _WIN32
	close(vsock_dev_fd);
	#endif

	print_debug(LOG_NOTICE, "Exiting\n");

	return EXIT_SUCCESS;
}

