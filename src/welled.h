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

#ifdef _OPENWRT
struct ether_addr
{
  u_int8_t ether_addr_octet[ETH_ALEN];
} __attribute__ ((__packed__));
#else
#include <net/ethernet.h>
#endif

#include "ieee80211.h"

#ifndef WELLED_H_
#define WELLED_H_

static struct {
        struct nl_sock *nls;
        int nl80211_id;
} wifi;

#define BIT(x)	(1 << (x))
#define s8 __s8
#define u16 __u16
#define VERSION_NR 1
#define __packed __attribute__((packed))
#include "mac80211_hwsim.h"

#include <netlink/netlink.h>

void show_usage(int);
void hex_dump(void *, int);
void free_mem(void);
void signal_handler(void);
void set_all_rates_invalid(struct hwsim_tx_rate *);
int get_signal_by_rate(int);
void nlh_print(struct nlmsghdr *);
void gnlh_print(struct genlmsghdr *);
void attrs_print(struct nlattr *attrs[]);
static int process_messages_cb(struct nl_msg *, void *);
int init_netlink(void);
int send_register_msg(void);
int recv_from_master(void);
void dellink(struct nlmsghdr *);
void newlink(struct nlmsghdr *);
static int process_nl_route_event(struct nl_msg *, void *);
void *monitor_devices(void *);
void *monitor_hwsim(void *);
static int finish_handler(struct nl_msg *, void *);
static int list_interface_handler(struct nl_msg *, void *);
int nl80211_get_interface(int);
static void generate_ack_frame(uint32_t, struct ether_addr*, struct ether_addr*);
void mac_address_to_string(char *, struct ether_addr *);
void print_debug(int, char *, ...);

#endif /* WELLED_H_ */
