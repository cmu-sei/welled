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

#ifndef WMASTERD_H_

/** Buffer size for NMEA sentences */
#define NMEA_LEN	100
/** Buffer size for follow name */
#define FOLLOW_LEN	1024
/** Buffer size for client vm name */
#define NAME_LEN	1024
/** Buffer size for GUID and UUID */
#define UUID_LEN	37
/** Buffer size for connection to wmasterd and clients */
#define WMASTERD_BUFF_LEN		64000

#ifdef _WIN32
#define LOG_EMERG       0       /* system is unusable */
#define LOG_ALERT       1       /* action must be taken immediately */
#define LOG_CRIT        2       /* critical conditions */
#define LOG_ERR         3       /* error conditions */
#define LOG_WARNING     4       /* warning conditions */
#define LOG_NOTICE      5       /* normal but significant condition */
#define LOG_INFO        6       /* informational */
#define LOG_DEBUG       7       /* debug-level messages */
#endif

/* values for wmasterd commands */
#define WMASTERD_ADD	10
#define WMASTERD_DELETE	20
#define WMASTERD_UPDATE	30
#define WMASTERD_FRAME	40

/**
 *  Structure for tracking welled node locations
 *	latitude and longitude are stored in degrees decimal
 *	velocity is in knots
 *	heading is in degrees
 */
struct location {
	char follow[FOLLOW_LEN];
	float latitude;
	float longitude;
	float altitude;
	float velocity;
	float heading;
	float pitch;
	char nmea_zda[NMEA_LEN];
	char nmea_gga[NMEA_LEN];
	char nmea_rmc[NMEA_LEN];
	char nmea_pashr[NMEA_LEN];
/*
	char nmea_gsa[NMEA_LEN];
	char nmea_gbs[NMEA_LEN];
	char nmea_gsv1[NMEA_LEN];
	char nmea_gsv2[NMEA_LEN];
	char nmea_gsv3[NMEA_LEN];
*/
};

struct update_2 {
	char version[8];
	char follow[FOLLOW_LEN];
	float latitude;
	float longitude;
	float altitude;
	float velocity;
	float heading;
	float pitch;
	char room[UUID_LEN];
	char name[NAME_LEN];
	char isolation_tag[UUID_LEN];
	unsigned int address;
};

/**
 *      \brief Structure for messages from from clients
 *      This will have client app name, version, message length,
 *      network namespace, radio id, and distance from TX to RX
 */
struct message_hdr {
	/* app name */
	char name[6];
	/* app version */
	char version[8];
	/* cmd */
	int cmd;
	/* sources address of vm */
	unsigned int src_addr;
	/* radio_id sending the message */
	int src_radio_id;
	/* radio_id receiving the message */
	int dest_radio_id;
	/* netns of the app */
	int netns;
	/* distance from receiver */
	int distance;
	/* length of message */
	int len;
};

/**
 *      \brief Structure for tracking welled nodes
 *
 *      This is the node used to track welled clients in a linked list
 *      We will retransmit all frames and netlink data to these clients
 */
struct client {
	/** address */
	unsigned int address;
	/* radio_id */
	int radio_id;
	/* netns */
	int netns;
	/** Isolation Tag */
	char isolation_tag[UUID_LEN];
	/** GUID for Room */
	char room[UUID_LEN];
	/** VM name */
	char name[NAME_LEN];
	/** VM UUID, unused */
	char uuid[UUID_LEN];
	/** epoch time stamp of last access */
	int time;
	/** GPS location data */
	struct location loc;
	/** Pointer to next node */
	struct client *next;
};

void show_usage(int);
void block_signal(void);
void print_node(struct client *);
void unblock_signal(void);
int parse_vmx(char *, unsigned int, char *, char *, char *);
void get_vm_info(unsigned int, char *, char *, char *);
void clear_inactive_nodes(void);
struct client *get_node_by_address(unsigned int);
struct client *get_node_by_radio(unsigned int, int);
struct client *get_node_by_name(char *);
void list_nodes(void);
void remove_node(unsigned int, int);
void send_to_hosts(char *, int, char *);
void relay_to_nodes(char *, int, struct client *);
void send_nmea_to_nodes(void);
void *produce_nmea(void *);
void free_list(void);
void usr1_handler(void);
void signal_handler(void);
void recv_from_welled(void);
void *recv_from_hosts(void *);
void update_node_location(struct client *, struct update_2 *);
void update_node_info(struct client *, struct update_2 *);
void update_cache_file_info(struct client *);
void update_cache_file_location(struct client *);
void update_followers(struct client *);
int get_distance(struct client *, struct client *);
unsigned int nmea_checksum(char *);
void create_new_sentences(struct client *);
double rad2deg(double);
double deg2rad(double);
void dec_deg_to_dec_min(float, char *, int);
void print_debug(int, char *, ...);
void remove_newline(char *);
void add_node(unsigned int, char *, char *, char *, int);

#endif  /* WMASTERD_H_ */

