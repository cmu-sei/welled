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

#define FOLLOW_LEN	1024
#define NAME_LEN	1024
#define UUID_LEN	37

/**
 *      Structure for tracking welled node locations
 *	latitude and longitude are stored in degrees decimal
 *	velocity is in knots
 *	heading is in degrees
 */
struct location {
	char follow[32];
	float latitude;
	float longitude;
	float altitude;
	float velocity;
	float heading;
	float climb;
	char nmea_zda[NMEA_LEN];
	char nmea_gga[NMEA_LEN];
	char nmea_rmc[NMEA_LEN];
/*
	char nmea_gsa[NMEA_LEN];
	char nmea_gbs[NMEA_LEN];
	char nmea_gsv1[NMEA_LEN];
	char nmea_gsv2[NMEA_LEN];
	char nmea_gsv3[NMEA_LEN];
*/
};

struct update {
	char follow[32];
	float latitude;
	float longitude;
	float altitude;
	float velocity;
	float heading;
	int room_id;
	char name[1024];
	char address[16];
	unsigned int cid;
};

struct update_2 {
	char version[8];
	char follow[FOLLOW_LEN];
	float latitude;
	float longitude;
	float altitude;
	float velocity;
	float heading;
	float climb;
	char room[UUID_LEN];
	char name[NAME_LEN];
	char isolation_tag[UUID_LEN];
	unsigned int cid;
};

/**
 *      \brief Structure for tracking welled nodes
 *
 *      This is the node used to track welled clients in a linked list
 *      We will retransmit all frames and netlink data to these clients
 */
struct client {
	/** CID */
	unsigned int cid;
        /** Isolation Tag */
        char isolation_tag[UUID_LEN];
	/** RoomID */
	//int room_id;
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
void add_node_vmci(unsigned int, char *, char *, char *);
void clear_inactive_nodes(void);
struct client *search_node_vmci(unsigned int);
struct client *search_node_name(char *);
void list_nodes_vmci(void);
void remove_node_vmci(unsigned int);
void send_to_hosts(char *, int, char *);
void send_to_nodes_vmci(char *, int, struct client *);
void send_gps_to_nodes(void);
void *produce_nmea(void *);
void free_list(void);
void usr1_handler(void);
void signal_handler(void);
void recv_from_welled_vmci(void);
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

#endif  /* WMASTERD_H_ */

