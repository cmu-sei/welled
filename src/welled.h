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

/**
 * enum hwsim_tx_control_flags - flags to describe transmission info/status
 *
 * These flags are used to give the wmediumd extra information in order to
 * modify its behavior for each frame
 *
 * HWSIM_TX_CTL_REQ_TX_STATUS: require TX status callback for this frame.
 * HWSIM_TX_CTL_NO_ACK: tell the wmediumd not to wait for an ack
 * HWSIM_TX_STAT_ACK: Frame was acknowledged
 *
 */
enum hwsim_tx_control_flags {
	HWSIM_TX_CTL_REQ_TX_STATUS	      = BIT(0),
	HWSIM_TX_CTL_NO_ACK		     = BIT(1),
	HWSIM_TX_STAT_ACK		       = BIT(2),
};

/**
 * enum hwsim_commands - supported hwsim commands
 *
 * HWSIM_CMD_UNSPEC: unspecified command to catch errors
 *
 * HWSIM_CMD_REGISTER: request to register and received all broadcasted
 *      frames by any mac80211_hwsim radio device.
 * HWSIM_CMD_FRAME: send/receive a broadcasted frame from/to kernel/user
 * space, uses:
 *      %HWSIM_ATTR_ADDR_TRANSMITTER, %HWSIM_ATTR_ADDR_RECEIVER,
 *      %HWSIM_ATTR_FRAME, %HWSIM_ATTR_FLAGS, %HWSIM_ATTR_RX_RATE,
 *      %HWSIM_ATTR_SIGNAL, %HWSIM_ATTR_COOKIE, %HWSIM_ATTR_FREQ (optional)
 * HWSIM_CMD_TX_INFO_FRAME: Transmission info report from user space to
 * kernel, uses:
 *      %HWSIM_ATTR_ADDR_TRANSMITTER, %HWSIM_ATTR_FLAGS,
 *      %HWSIM_ATTR_TX_INFO, %HWSIM_ATTR_SIGNAL, %HWSIM_ATTR_COOKIE
 * HWSIM_CMD_NEW_RADIO: create a new radio with the given parameters,
 *      returns the radio ID (>= 0) or negative on errors, if successful
 *      then multicast the result
 * HWSIM_CMD_DEL_RADIO: destroy a radio, reply is multicasted
 * HWSIM_CMD_GET_RADIO: fetch information about existing radios, uses:
 *      %HWSIM_ATTR_RADIO_ID
 * __HWSIM_CMD_MAX: enum limit
 */
enum {
	HWSIM_CMD_UNSPEC,
	HWSIM_CMD_REGISTER,
	HWSIM_CMD_FRAME,
	HWSIM_CMD_TX_INFO_FRAME,
	HWSIM_CMD_NEW_RADIO,
	HWSIM_CMD_DEL_RADIO,
	HWSIM_CMD_GET_RADIO,
	__HWSIM_CMD_MAX,
};

#define HWSIM_CMD_MAX (_HWSIM_CMD_MAX - 1)
#define HWSIM_CMD_CREATE_RADIO   HWSIM_CMD_NEW_RADIO
#define HWSIM_CMD_DESTROY_RADIO  HWSIM_CMD_DEL_RADIO

/**
 * enum hwsim_attrs - hwsim netlink attributes
 *
 * HWSIM_ATTR_UNSPEC: unspecified attribute to catch errors
 *
 * HWSIM_ATTR_ADDR_RECEIVER: MAC address of the radio device that
 *      the frame is broadcasted to
 * HWSIM_ATTR_ADDR_TRANSMITTER: MAC address of the radio device that
 *      the frame was broadcasted from
 * HWSIM_ATTR_FRAME: Data array
 * HWSIM_ATTR_FLAGS: mac80211 transmission flags, used to process
	properly the frame at user space
 * HWSIM_ATTR_RX_RATE: estimated rx rate index for this frame at user
	space
 * HWSIM_ATTR_SIGNAL: estimated RX signal for this frame at user
	space
 * HWSIM_ATTR_TX_INFO: ieee80211_tx_rate array
 * HWSIM_ATTR_COOKIE: sk_buff cookie to identify the frame
 * HWSIM_ATTR_CHANNELS: u32 attribute used with the %HWSIM_CMD_CREATE_RADIO
 *      command giving the number of channels supported by the new radio
 * HWSIM_ATTR_RADIO_ID: u32 attribute used with %HWSIM_CMD_DESTROY_RADIO
 *      only to destroy a radio
 * HWSIM_ATTR_REG_HINT_ALPHA2: alpha2 for regulatoro driver hint
 *      (nla string, length 2)
 * HWSIM_ATTR_REG_CUSTOM_REG: custom regulatory domain index (u32 attribute)
 * HWSIM_ATTR_REG_STRICT_REG: request REGULATORY_STRICT_REG (flag attribute)
 * HWSIM_ATTR_SUPPORT_P2P_DEVICE: support P2P Device virtual interface (flag)
 * HWSIM_ATTR_USE_CHANCTX: used with the %HWSIM_CMD_CREATE_RADIO
 *      command to force use of channel contexts even when only a
 *      single channel is supported
 * HWSIM_ATTR_DESTROY_RADIO_ON_CLOSE: used with the %HWSIM_CMD_CREATE_RADIO
 *      command to force radio removal when process that created the radio dies
 * HWSIM_ATTR_RADIO_NAME: Name of radio, e.g. phy666
 * HWSIM_ATTR_NO_VIF:  Do not create vif (wlanX) when creating radio.
 * HWSIM_ATTR_FREQ: Frequency at which packet is transmitted or received.
 * __HWSIM_ATTR_MAX: enum limit
 */

enum {
	HWSIM_ATTR_UNSPEC,
	HWSIM_ATTR_ADDR_RECEIVER,
	HWSIM_ATTR_ADDR_TRANSMITTER,
	HWSIM_ATTR_FRAME,
	HWSIM_ATTR_FLAGS,
	HWSIM_ATTR_RX_RATE,
	HWSIM_ATTR_SIGNAL,
	HWSIM_ATTR_TX_INFO,
	HWSIM_ATTR_COOKIE,
	HWSIM_ATTR_CHANNELS,
	HWSIM_ATTR_RADIO_ID,
	HWSIM_ATTR_REG_HINT_ALPHA2,
	HWSIM_ATTR_REG_CUSTOM_REG,
	HWSIM_ATTR_REG_STRICT_REG,
	HWSIM_ATTR_SUPPORT_P2P_DEVICE,
	HWSIM_ATTR_USE_CHANCTX,
	HWSIM_ATTR_DESTROY_RADIO_ON_CLOSE,
	HWSIM_ATTR_RADIO_NAME,
	HWSIM_ATTR_NO_VIF,
	HWSIM_ATTR_FREQ,
	__HWSIM_ATTR_MAX,
};

#define HWSIM_ATTR_MAX (__HWSIM_ATTR_MAX - 1)


#define VERSION_NR 1

#include <netlink/netlink.h>

/**
 * struct hwsim_tx_rate - rate selection/status
 *
 * idx: rate index to attempt to send with
 * count: number of tries in this rate before going to the next rate
 *
 * A value of -1 for idx indicates an invalid rate and, if used
 * in an array of retry rates, that no more rates should be tried.
 *
 * When used for transmit status reporting, the driver should
 * always report the rate and number of retries used.
 *
 */
struct hwsim_tx_rate {
	signed char idx;
	unsigned char count;
};

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
void recv_from_master(void);
void dellink(struct nlmsghdr *);
void newlink(struct nlmsghdr *);
static int process_nl_route_event(struct nl_msg *, void *);
//void process_event(int);
void *monitor_devices(void *);
void *monitor_hwsim(void *);
static int finish_handler(struct nl_msg *, void *);
static int list_interface_handler(struct nl_msg *, void *);
int nl80211_get_interface(int);
static void generate_ack_frame(uint32_t, struct ether_addr*, struct ether_addr*);
void mac_address_to_string(char *, struct ether_addr *);
void print_debug(int, char *, ...);

#endif /* WELLED_H_ */
