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

#ifndef NODES_H_
#define NODES_H_

#include <linux/if_ether.h>
#include <linux/sockios.h>
#include <sys/ioctl.h>

#define MAX_ADDR_LEN	32

struct device_node {
	char *name;
	int index;
	int iftype;
	long int netnsid;
	int radio_id;
	int wiphy;
	unsigned char address[ETH_ALEN];
	unsigned char perm_addr[ETH_ALEN];
	struct device_node *next;
};

int monitor_mode(void);
int remove_node_by_name(char *);
int remove_node_by_index(int);

struct device_node *get_node_by_name(char *);
struct device_node *get_node_by_index(int);
struct device_node *get_node_by_radio_id(int);
struct device_node *get_node_by_pos(int);
struct device_node *get_node_by_wiphy(int);
struct device_node *get_node_by_perm_addr(char *);

void add_node(struct device_node *);
void list_nodes(void);
void print_node(int, struct device_node *);
void free_list(void);

struct device_node *get_node_by_name(char *);
struct device_node *get_node_by_index(int);
struct device_node *get_node_by_pos(int);

#endif /* NODES_H_ */

