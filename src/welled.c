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
#include <pthread.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>

#ifdef _ANDROID
#include <asm-generic/errno-base.h>
#include <asm-generic/errno.h>
#include <sys/socket.h>
#include <linux/if.h>
#else
#include <errno.h>
#endif

#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/family.h>
#include <netlink/route/link.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <syslog.h>
#include <stdarg.h>

#include <linux/if_arp.h>
#include <linux/nl80211.h>
#include <linux/ethtool.h>

/* vmci_sockets removed in favor of more generic vm_scockets */
#include <linux/vm_sockets.h>

#include "welled.h"
#include "ieee80211.h"
#include "nodes.h"

/** Port used to send frames to wmasterd */
#define SEND_PORT		1111
/** Port used to receive frames from wmasterd */
#define RECV_PORT		2222
/** Address used to send frames to wmasterd */
#ifndef VMADDR_CID_HOST
	#define VMADDR_CID_HOST		2
#endif
/** Buffer size for VMCI datagrams */
#define VMCI_BUFF_LEN		4096
/** Buffer size for UDP datagrams */
#define BUFF_LEN		4096
/** Buffer size for netlink route interface list */
#define IFLIST_REPLY_BUFFER     4096

/** Whether to print verbose output */
int verbose;
/** Whether to allow any MAC address */
int any_mac;
/** Whether to break loop after signal */
int running;
/** Number of devices/interfaces */
int devices;
/** Whether we have received the end of a nl msg */
int nlmsg_end;
/** pointer for netlink socket */
struct nl_sock *sock;
/** pointer for netlink callback function */
struct nl_cb *cb;
/** For the family ID used by hwsim */
int family_id;
/** FD for vmci send */
int sockfd;
/** FD for vmci receive */
int myservfd;
/** sockaddr_vm for vmci send */
struct sockaddr_vm servaddr_vm;
/** sockaddr_vm for vmci receive */
struct sockaddr_vm myservaddr_vm;
/** mutex for linked list access */
pthread_mutex_t list_mutex;
/** mutex for driver unload/load checking */
pthread_mutex_t hwsim_mutex;
/** mutex for access to send socket */
pthread_mutex_t send_mutex;
/** thread id of heartbeat thread */
pthread_t status_tid;
/** thread id of device monitoring thread */
pthread_t dev_tid;
/** thread id of hwsim driver status monitoring thread */
pthread_t hwsim_tid;
/** thread id of thread processing data from wmasterd */
pthread_t master_tid;
/** for the desired log level */
int loglevel;

/**
 *	@brief Prints the CLI help
 *	@param exval - exit code
 *	@return - void, calls exit()
 */
void show_usage(int exval)
{
	/* allow help2man to read this */
	setbuf(stdout, NULL);

	printf("welled - wireless emulation link layer exchange daemon\n\n");

	printf("Usage: welled [-hVav] [-D <level>]\n\n");

	printf("Options:\n");
	printf("  -h, --help	print this help and exit\n");
	printf("  -V, --version	print version and exit\n");
	printf("  -a, --any	allow any mac address (patched driver)\n");
	printf("  -D, --debug   debug level for syslog\n");
	printf("  -v, --verbose	verbose output\n\n");

	printf("Copyright (C) 2015 Carnegie Mellon University\n\n");
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
	syslog(level, "%s", buffer);
	printf("welled: %s\n", buffer);
}

/**
 *	@brief Display a hex dump of the passed buffer
 *	this function was pulled off the web somehwere... seems to throw a
 *	segfault when printing some of the netlink error message data. this
 *	is used for debugging and viewing contents of messages
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

/**
 *	@brief Frees all global memory allocated during runtime
 *	If we look at valgrind, we still miss some netlink objects.
 *	this is run when exiting the program.
 *	@return void - assumes success
 */
void free_mem(void)
{
	if (verbose)
		print_debug(LOG_DEBUG, "Trying to free memory...");

	pthread_mutex_lock(&list_mutex);
	free_list();
	pthread_mutex_unlock(&list_mutex);

	nl_close(sock);
	nl_socket_free(sock);
	nl_cb_put(cb);
}

/**
 *	@brief Signal handler which causes program to exit
 *	This sets the running variable which is checked by threads when
 *	when they loop to the value indicating they should exit. It also
 *	cancels the threads.
 *	@return void - calls exit()
 */
void signal_handler(void)
{
	print_debug(LOG_DEBUG, "signal handler invoked");

	running = 0;

/*
	pthread_cancel(status_tid);
	pthread_cancel(hwsim_tid);
	pthread_cancel(dev_tid);
	pthread_cancel(master_tid);
*/
}

/**
 *	@brief Set a tx_rate struct to not valid values
 *	Taken from wmediumd.
 *	TODO: modify if we ack more accurately.
 *	@param tx_rate - struct to tract attempts and rates
 *	@return void
 */
void set_all_rates_invalid(struct hwsim_tx_rate *tx_rate)
{
	int i;

	/* Set up all unused rates to be -1 */
	for (i = 0; i < IEEE80211_MAX_RATES_PER_TX; i++) {
		tx_rate[i].idx = -1;
		tx_rate[i].count = 0;
	}
}

/**
 *      @brief Send a cloned frame to the kernel space driver.
 *	This will send a frame to the driver using netlink.
 *	It is received by hwsim with hwsim_cloned_frame_received_nl()
 *	This is taken from wmediumd and modified. It is called after the
 *	message has been received from wmasterd.
 *	@param dst - mac address of receving radio
 *	@param data - frame data
 *	@param data_len - length of frame
 *	@param rate_idx - number of attempts
 *	@param signal - signal strength
 *	@param freq - frequency
 *	@return success or failure
 */
int send_cloned_frame_msg(struct ether_addr *dst, char *data, int data_len,
		int rate_idx, int signal, uint32_t freq)
{
	int rc;
	struct nl_msg *msg;
	char addr[18];
	int bytes;

	msg = nlmsg_alloc();

	if (!msg) {
		print_debug(LOG_ERR, "Error allocating new message MSG!");
		goto out;
	}
	if (family_id < 0)
		goto out;

	genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, family_id,
		    0, NLM_F_REQUEST, HWSIM_CMD_FRAME, VERSION_NR);

	rc = nla_put(msg, HWSIM_ATTR_ADDR_RECEIVER,
		     sizeof(struct ether_addr), dst);
	rc = nla_put(msg, HWSIM_ATTR_FRAME, data_len, data);
	rc = nla_put_u32(msg, HWSIM_ATTR_RX_RATE, rate_idx);
	rc = nla_put_u32(msg, HWSIM_ATTR_SIGNAL, signal);
	if (freq)
		rc = nla_put_u32(msg, HWSIM_ATTR_FREQ, freq);
	/* this signal rate will not match the signal acked to the sender
	 * unless we set the same rate in both functions. normally,
	 * the calling function determines this signal, and could
	 * send the info back to the transmitting radio via wmasterd
	 */

	if (rc != 0) {
		print_debug(LOG_ERR, "Error filling payload in send_cloned_frame_msg");
		goto out;
	}
	/*
	printf("#### welled -> hwsim nlmsg cloned beg ####\n");
	struct nlmsghdr *nlh = nlmsg_hdr(msg);
	struct genlmsghdr *gnlh = nlmsg_data(nlh);
	struct nlattr *attrs[HWSIM_ATTR_MAX + 1];
	genlmsg_parse(nlh, 0, attrs, HWSIM_ATTR_MAX, NULL);
	nlh_print(nlh);
	gnlh_print(gnlh);
	attrs_print(attrs);
	printf("#### welled -> hwsim nlmsg cloned end ####\n");
	*/
	bytes = nl_send_auto_complete(sock, msg);
	nlmsg_free(msg);
	mac_address_to_string(addr, dst);
	print_debug(LOG_INFO, "sent %d bytes to %s", bytes, addr);

	return 0;
out:
	nlmsg_free(msg);
	return -1;
}

/**
 *	@brief Print the nlmsghdr fields
 *	Used for debugging
 *	@param nlh - pointer to netlink message header
 *	@return void
 */
void nlh_print(struct nlmsghdr *nlh)
{
	hex_dump(nlh, nlh->nlmsg_len);
	printf("len   %d\n", nlh->nlmsg_len);
	printf("type  %d\n", nlh->nlmsg_type);
	printf("flags %d\n", nlh->nlmsg_flags);
	printf("seq   %d\n", nlh->nlmsg_seq);
	printf("pid   %d\n", nlh->nlmsg_pid);
}

/**
 *	@brief Print the genlmsghdr fields
 *	Used for debugging.
 *	@param gnlh - pointer to generic netlink message header
 *	@return void
 */
void gnlh_print(struct genlmsghdr *gnlh)
{
	printf("cmd      %u\n", gnlh->cmd);
	printf("version  %u\n", gnlh->version);
	printf("reserved %u\n", gnlh->reserved);
}

/**
 *	@brief Convert mac address to string
 *	@param address  - destination string
 *	@param mac - source mac address
 *	@return void
 */
void mac_address_to_string(char *address, struct ether_addr *mac)
{
	sprintf(address, "%02X:%02X:%02X:%02X:%02X:%02X",
		mac->ether_addr_octet[0], mac->ether_addr_octet[1],
		mac->ether_addr_octet[2], mac->ether_addr_octet[3],
		mac->ether_addr_octet[4], mac->ether_addr_octet[5]);
}

/**
 *	@brief Send a tx_info frame to the kernel space. This frame indicates
 *	that the frame was transmitted/acked successfully. The ack is sent back
 *	to the driver with HWSIM_ATTR_ADDR_TRANSMITTER unmodified.
 *	This is derived form wmediumd.
 *	TODO: modify if we create more accurate acking.
 *	@param src - mac address of transmitting radio
 *	@param flags - falgs
 *	@param signal - signal strength
 *	@param tx_attempts - number of transmit attempts
 *	@param cookie - unique identifier for frame
 *	@return success or failure
 */
int send_tx_info_frame_nl(struct ether_addr *src,
			unsigned int flags, int signal,
			struct hwsim_tx_rate *tx_attempts,
			unsigned long cookie)
{
	struct nl_msg *msg;
	int rc;

	msg = nlmsg_alloc();

	if (!msg) {
		print_debug(LOG_ERR, "Error allocating new message MSG!\n");
		goto out;
	}
	if (family_id < 0)
		goto out;

	genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, family_id,
			0, NLM_F_REQUEST, HWSIM_CMD_TX_INFO_FRAME, VERSION_NR);

	/* i have to ack the src the driver expects
	 * so there are no mac address modifications here
	 */
	rc = nla_put(msg, HWSIM_ATTR_ADDR_TRANSMITTER,
			sizeof(struct ether_addr), src);
	rc = nla_put_u32(msg, HWSIM_ATTR_FLAGS, flags);
	rc = nla_put_u32(msg, HWSIM_ATTR_SIGNAL, signal);
	rc = nla_put(msg, HWSIM_ATTR_TX_INFO,
			IEEE80211_MAX_RATES_PER_TX *
			sizeof(struct hwsim_tx_rate), tx_attempts);

	rc = nla_put_u64(msg, HWSIM_ATTR_COOKIE, cookie);

	if (rc != 0) {
		print_debug(LOG_ERR, "Error filling payload\n");
		goto out;
	}
	/*
	printf("#### welled -> hwsim nlmsg tx info beg ####\n");
	struct nlmsghdr *nlh = nlmsg_hdr(msg);
	struct genlmsghdr *gnlh = nlmsg_data(nlh);
	struct nlattr *attrs[HWSIM_ATTR_MAX + 1];
	genlmsg_parse(nlh, 0, attrs, HWSIM_ATTR_MAX, NULL);
	nlh_print(nlh);
	gnlh_print(gnlh);
	attrs_print(attrs);
	printf("#### welled -> hwsim nlmsg tx info end ####\n");
	*/
	nl_send_auto_complete(sock, msg);
	nlmsg_free(msg);
	return 0;
out:
	nlmsg_free(msg);
	return -1;
}

/**
 *	@brief Print any netlink attributes when exist in the genlmsg
 *	Used for debugging.
 *	@param attrs - pointer to attributes array
 *	@return void
 */
void attrs_print(struct nlattr *attrs[])
{
	char addr[18];
	struct ether_addr *dst;
	struct ether_addr *src;
	unsigned int data_len;
	char *data;
	unsigned int flags;
	int signal;
	struct hwsim_tx_rate *tx_rates;
	unsigned long cookie;
	uint8_t freq;

	if (attrs[HWSIM_ATTR_UNSPEC]) {
		printf("HWSIM_ATTR_UNSPEC\n");
		printf("nla len   %u\n", attrs[HWSIM_ATTR_UNSPEC]->nla_len);
		printf("nla type  %u\n", attrs[HWSIM_ATTR_UNSPEC]->nla_type);
		data_len = nla_len(attrs[HWSIM_ATTR_UNSPEC]);
		data = (char *)nla_data(attrs[HWSIM_ATTR_UNSPEC]);
		hex_dump(data, data_len);
	}
	if (attrs[HWSIM_ATTR_ADDR_RECEIVER]) {
		printf("HWSIM_ATTR_ADDR_RECEIVER\n");
		printf("nla len   %u\n",
			attrs[HWSIM_ATTR_ADDR_RECEIVER]->nla_len);
		printf("nla type  %u\n",
			attrs[HWSIM_ATTR_ADDR_RECEIVER]->nla_type);
		dst = (struct ether_addr *)
				nla_data(attrs[HWSIM_ATTR_ADDR_RECEIVER]);
		mac_address_to_string(addr, dst);
		printf("dst radio: %s\n", addr);
	}
	if (attrs[HWSIM_ATTR_ADDR_TRANSMITTER]) {
		printf("HWSIM_ATTR_ADDR_TRANSMITTER\n");
		printf("nla len    %u\n",
			attrs[HWSIM_ATTR_ADDR_TRANSMITTER]->nla_len);
		printf("nla type   %u\n",
			attrs[HWSIM_ATTR_ADDR_TRANSMITTER]->nla_type);
		src = (struct ether_addr *)
			nla_data(attrs[HWSIM_ATTR_ADDR_TRANSMITTER]);
		mac_address_to_string(addr, src);
		printf("src radio: %s\n", addr);
	}
	if (attrs[HWSIM_ATTR_FRAME]) {
		printf("HWSIM_ATTR_FRAME\n");
		printf("nla len    %u\n", attrs[HWSIM_ATTR_FRAME]->nla_len);
		printf("nla type   %u\n", attrs[HWSIM_ATTR_FRAME]->nla_type);
		data_len = nla_len(attrs[HWSIM_ATTR_FRAME]);
		data = (char *)nla_data(attrs[HWSIM_ATTR_FRAME]);
		hex_dump(data, data_len);
	}
	if (attrs[HWSIM_ATTR_FLAGS]) {
		printf("HWSIM_ATTR_FLAGS\n");
		printf("nla len    %u\n", attrs[HWSIM_ATTR_FLAGS]->nla_len);
		printf("nla type   %u\n", attrs[HWSIM_ATTR_FLAGS]->nla_type);
		flags = nla_get_u32(attrs[HWSIM_ATTR_FLAGS]);
		printf("flags:     %u\n", flags);
	}
	if (attrs[HWSIM_ATTR_RX_RATE]) {
		printf("HWSIM_ATTR_RX_RATE\n");
		printf("nla len    %u\n", attrs[HWSIM_ATTR_RX_RATE]->nla_len);
		printf("nla type   %u\n", attrs[HWSIM_ATTR_RX_RATE]->nla_type);
	}
	if (attrs[HWSIM_ATTR_SIGNAL]) {
		printf("HWSIM_ATTR_SIGNAL\n");
		printf("nla len    %u\n", attrs[HWSIM_ATTR_SIGNAL]->nla_len);
		printf("nla type   %u\n", attrs[HWSIM_ATTR_SIGNAL]->nla_type);
		signal = nla_get_u32(attrs[HWSIM_ATTR_SIGNAL]);
		printf("signal:    %d\n", signal);
	}
	if (attrs[HWSIM_ATTR_TX_INFO]) {
		printf("HWSIM_ATTR_TX_INFO\n");
		printf("nla len    %u\n", attrs[HWSIM_ATTR_TX_INFO]->nla_len);
		printf("nla type   %u\n", attrs[HWSIM_ATTR_TX_INFO]->nla_type);
		tx_rates = (struct hwsim_tx_rate *)
			nla_data(attrs[HWSIM_ATTR_TX_INFO]);
		printf("tx_rates: ");
		hex_dump(tx_rates, nla_len(attrs[HWSIM_ATTR_TX_INFO]));
	}
	if (attrs[HWSIM_ATTR_COOKIE]) {
		printf("HWSIM_ATTR_COOKIE\n");
		printf("nla len    %u\n", attrs[HWSIM_ATTR_COOKIE]->nla_len);
		printf("nla type   %u\n", attrs[HWSIM_ATTR_COOKIE]->nla_type);
		cookie = nla_get_u64(attrs[HWSIM_ATTR_COOKIE]);
		printf("cookie:    %lu\n", cookie);
	}
	if (attrs[HWSIM_ATTR_CHANNELS]) {
		printf("HWSIM_ATTR_CHANNELS\n");
		printf("nla len    %u\n", attrs[HWSIM_ATTR_CHANNELS]->nla_len);
		printf("nla type   %u\n", attrs[HWSIM_ATTR_CHANNELS]->nla_type);
		data_len = nla_len(attrs[HWSIM_ATTR_CHANNELS]);
		data = (char *)nla_data(attrs[HWSIM_ATTR_CHANNELS]);
		hex_dump(data, data_len);
	}
	if (attrs[HWSIM_ATTR_RADIO_ID]) {
		printf("HWSIM_ATTR_RADIO_ID\n");
		printf("nla len    %u\n", attrs[HWSIM_ATTR_RADIO_ID]->nla_len);
		printf("nla type   %u\n", attrs[HWSIM_ATTR_RADIO_ID]->nla_type);
		data_len = nla_len(attrs[HWSIM_ATTR_RADIO_ID]);
		data = (char *)nla_data(attrs[HWSIM_ATTR_RADIO_ID]);
		hex_dump(data, data_len);
	}
	if (attrs[HWSIM_ATTR_RADIO_NAME]) {
		printf("HWSIM_ATTR_RADIO_NAME\n");
		printf("nla len    %u\n",
			attrs[HWSIM_ATTR_RADIO_NAME]->nla_len);
		printf("nla type   %u\n",
			attrs[HWSIM_ATTR_RADIO_NAME]->nla_type);
		data_len = nla_len(attrs[HWSIM_ATTR_RADIO_NAME]);
		data = (char *)nla_data(attrs[HWSIM_ATTR_RADIO_NAME]);
		hex_dump(data, data_len);
	}
	if (attrs[HWSIM_ATTR_FREQ]) {
		printf("HWSIM_ATTR_FREQ\n");
		printf("nla len    %u\n", attrs[HWSIM_ATTR_FREQ]->nla_len);
		printf("nla type   %u\n", attrs[HWSIM_ATTR_FREQ]->nla_type);
		freq = nla_get_u32(attrs[HWSIM_ATTR_FREQ]);
		printf("freq:      %i\n", freq);
	}
}

/**
 *	@brief Callback function to process messages received from kernel
 *	It processes the frames received from hwsim via netlink messages.
 *	These frames get sent via vmci to wmasterd.
 *	@param msg - pointer to netlink message
 *	@param arg - pointer to additional args
 *	@return success or failure
 */
static int process_messages_cb(struct nl_msg *msg, void *arg)
{
	print_debug(LOG_DEBUG, "process_messages_cb");
	int msg_len;
	struct nlattr *attrs[HWSIM_ATTR_MAX + 1];
	struct nlmsghdr *nlh;
	struct genlmsghdr *gnlh;
	struct nlmsgerr *err;
	struct ether_addr *src;
	unsigned int flags;
	struct hwsim_tx_rate *tx_rates;
	unsigned long cookie;
	struct hwsim_tx_rate tx_attempts[IEEE80211_MAX_RATES_PER_TX];
	int round;
	int tx_ok;
	int counter;
	int signal;
	char *data;
	struct ether_addr framesrc;
	char addr[18];
	int bytes;

	nlh = nlmsg_hdr(msg);
	gnlh = nlmsg_data(nlh);
	memset(addr, 0, 18);

	/* get message length needed for sendto */
	msg_len = nlh->nlmsg_len;

	if (nlh->nlmsg_type != family_id) {
		print_debug(LOG_ERR, "not hwsim msg");
		return 1;
	} else if (nlh->nlmsg_type == NLMSG_ERROR) {
		err = nlmsg_data(nlh);
		if (verbose)
			print_debug(LOG_ERR, "NLMSG_ERROR: %d\n", err->error);
		if (err->error == -22) {
			/* check hw - rx radio probably down */
			print_debug(LOG_ERR, "-EINVAL from hwsim\n");
			goto out;
		}
	}

	print_debug(LOG_INFO, "received %d bytes msg from hwsim", msg_len);
	/*
	if (nlh->nlmsg_type == NLMSG_NOOP)
		printf("NLMSG_NOOP\n");
	else if (nlh->nlmsg_type == NLMSG_DONE)
		printf("NLMSG_DONE\n");
	else if (nlh->nlmsg_type == NLMSG_MIN_TYPE)
		printf("NLMSG_MIN_TYPE\n");
	else if (nlh->nlmsg_type == family_id)
		printf("MAC80211_HWSIM\n");
	else
		printf("NLMSG: %d\n", nlh->nlmsg_type);
	*/

	/* ignore if anything other than a frame
	do we need to free the msg? */
	if (!(gnlh->cmd == HWSIM_CMD_FRAME)) {
		print_debug(LOG_ERR, "have gnlh cmd %d", gnlh->cmd);
		goto out;
	}

	/* processing original HWSIM_CMD_FRAME */
	genlmsg_parse(nlh, 0, attrs, HWSIM_ATTR_MAX, NULL);

	/*
	printf("#### hwsim -> welled nlmsg beg ####\n");
	nlh_print(nlh);
	gnlh_print(gnlh);
	attrs_print(attrs);
	printf("#### hwsim -> welled nlmsg end ####\n");
	*/

	/* this check was duplicated below in a second if statement, now gone */
	if (!(attrs[HWSIM_ATTR_ADDR_TRANSMITTER]))
		goto out;

	/* we get the attributes*/
	src = (struct ether_addr *)
		nla_data(attrs[HWSIM_ATTR_ADDR_TRANSMITTER]);
	flags = nla_get_u32(attrs[HWSIM_ATTR_FLAGS]);
	tx_rates = (struct hwsim_tx_rate *)nla_data(attrs[HWSIM_ATTR_TX_INFO]);
	cookie = nla_get_u64(attrs[HWSIM_ATTR_COOKIE]);

	/*
	 * we removed send_frames_to_radios_with_retries. this function would
	 * have ended here after running that one additional function.
	 * this code is taken from send_frames_to_radios_with_retries.
	 * we took out the transmit code since that needed to go in
	 * recv_from_master
	 */

	round = 0;
	tx_ok = 0;
/*
printf("###\n");
int i;
	for (i = 0; i < IEEE80211_MAX_RATES_PER_TX; i++) {
		printf("rate idx, count: %d %d\n", tx_rates[i].idx, tx_rates[i].count);
	}
*/
/*
rate idx, count: 0 1
rate idx, count: -1 0
rate idx, count: -1 0
rate idx, count: -1 0
rate idx, count: 12 0
attempt idx, count: 2 0
attempt idx, count: 0 0
attempt idx, count: 0 0
attempt idx, count: 0 0
attempt idx, count: 50 0

*/
/*
printf("###\n");
printf("setting rates invalid\n");
*/
	/* We prepare the tx_attempts struct */
	set_all_rates_invalid(tx_attempts);

	/* TODO: print tx_attempts */
/*
printf("###\n");
	for (i = 0; i < IEEE80211_MAX_RATES_PER_TX; i++) {
		printf("rate idx, count: %d %d\n", tx_rates[i].idx, tx_rates[i].count);
	}
	for (i = 0; i < IEEE80211_MAX_RATES_PER_TX; i++) {
		printf("attempt idx, count: %d %d\n", tx_attempts[i].idx, tx_attempts[i].count);
	}
*/
/*
rate idx, count: 0 1
rate idx, count: -1 0
rate idx, count: -1 0
rate idx, count: -1 0
rate idx, count: 12 0
attempt idx, count: -1 0
attempt idx, count: -1 0
attempt idx, count: -1 0
attempt idx, count: -1 0
attempt idx, count: -1 0
*/
	while (round < IEEE80211_MAX_RATES_PER_TX &&
			tx_rates[round].idx != -1 && tx_ok != 1) {

		counter = 1;

		/* tx_rates comes from the driver...
		 * that means that the receiving ends gets this info
		 * and can use it
		 */

		/* Set rate index and flags used for this round */
		tx_attempts[round].idx = tx_rates[round].idx;

		while (counter <= tx_rates[round].count && tx_ok != 1) {
			tx_attempts[round].count = counter;
			counter++;
		}
		round++;
	}
/*
printf("###\n");
printf("adjustments\n");
*/
	/* TODO: print tx_attempts */
/*
	for (i = 0; i < IEEE80211_MAX_RATES_PER_TX; i++) {
		printf("rates idx, count: %d %d\n", tx_rates[i].idx, tx_rates[i].count);
	}
	for (i = 0; i < IEEE80211_MAX_RATES_PER_TX; i++) {
		printf("attempt idx, count: %d %d\n", tx_attempts[i].idx, tx_attempts[i].count);
	}
printf("###\n");
*/
/*
rates idx, count: 0 1
rates idx, count: -1 0
rates idx, count: -1 0
rates idx, count: -1 0
rates idx, count: 12 0
attempt idx, count: 0 1
attempt idx, count: -1 0
attempt idx, count: -1 0
attempt idx, count: -1 0
attempt idx, count: -1 0
*/
	/* round -1 is the last element of the array */
	/* this is the signal sent to the sender, not the receiver */
	signal = -10;
	/* Let's flag this frame as ACK'ed */
	/* whatever that means... */
	flags |= HWSIM_TX_STAT_ACK;
	/* this has to be an ack the driver expects */
	/* what does the driver do with these values? can i remove them? */
	send_tx_info_frame_nl(src, flags, signal, tx_attempts,
			cookie);

	/*
	 * no need to send a tx info frame indicating failure with a
	 * signal of 0 - that was done in the tx code i took this from
	 * if i check for ack messages than i could add a failure message
	 */

	/* we are now done with our code addition which sends the ack */

	/* we get the attributes*/
	data = (char *)nla_data(attrs[HWSIM_ATTR_FRAME]);

	/* TODO: retrieve all other attributes not retrieved above */

	/* copy source address from frame */
	/* if we rebuild the nl msg, this can change */
	memcpy(&framesrc, data + 10, ETH_ALEN);

	if (verbose) {
		mac_address_to_string(addr, &framesrc);
		printf("frame src: %s\n", addr);
		mac_address_to_string(addr, src);
		printf("radio src: %s\n", addr);
	}

	/* compare tx src to frame src, update TX src ATTR in msg if needed */
	/* if we rebuild the nl msg, this can change */
	if (memcmp(&framesrc, src, ETH_ALEN) != 0) {
		if (verbose)
			printf("updating the TX src ATTR\n");
		/* copy dest address from frame to nlh */
		memcpy((char *)nlh + 24, &framesrc, ETH_ALEN);
	}

	pthread_mutex_lock(&send_mutex);
	bytes = sendto(sockfd, (char *)nlh, msg_len, 0,
			(struct sockaddr *)&servaddr_vm,
			sizeof(struct sockaddr));
	pthread_mutex_unlock(&send_mutex);

	if (bytes < 0) {
		/* wmasterd probably down */
		perror("sendto");
		print_debug(LOG_ERR, "ERROR: Could not TX to wmasterd via VMCI\n");
	} else {
		print_debug(LOG_INFO, "sent %d bytes to wmasterd", bytes);
	}

out:
	if (verbose)
		printf("#################### hwsim recv done ######################\n");

	return 0;
}


/**
 *	@brief Initialize netlink communications
 *	Taken from wmediumd
 *	@return void
 */
int init_netlink(void)
{
	int nlsockfd;
	struct timeval tv;

	if (cb != NULL) {
		nl_cb_put(cb);
		free(cb);
	}
	cb = nl_cb_alloc(NL_CB_CUSTOM);
	if (!cb) {
		print_debug(LOG_ERR, "Error allocating netlink callbacks\n");
		running = 0;
		return 0;
	}

	sock = nl_socket_alloc_cb(cb);
	if (!sock) {
		print_debug(LOG_ERR, "Error allocationg netlink socket\n");
		running = 0;
		return 0;
	}

	/* disable auto-ack from kernel to reduce load */
	nl_socket_disable_auto_ack(sock);
	genl_connect(sock);

	family_id = genl_ctrl_resolve(sock, "MAC80211_HWSIM");

	while ((family_id < 0) && running) {
		print_debug(LOG_INFO, "Family MAC80211_HWSIM not registered\n");
		sleep(1);
		family_id = genl_ctrl_resolve(sock, "MAC80211_HWSIM");
	}
	print_debug(LOG_DEBUG, "MAC80211_HWSIM is family id %d", family_id);
	if (!running) {
		return 0;
	}

	nl_cb_set(cb, NL_CB_MSG_IN, NL_CB_CUSTOM, process_messages_cb, NULL);
	nlsockfd = nl_socket_get_fd(sock);

	tv.tv_sec = 1;
	tv.tv_usec = 0;

	if (setsockopt(nlsockfd, SOL_SOCKET, SO_RCVTIMEO, &tv,
			 sizeof(tv)) < 0) {
		perror("setsockopt");
	}
	print_debug(LOG_DEBUG, "netlink initialized");
	return 1;
}

/**
 *	@brief Send a register message to kernel via netlink
 *	This informs hwsim we wish to receive frames
 *	Taken from wmediumd
 *	@return void
 */
int send_register_msg(void)
{
	struct nl_msg *msg;

	msg = nlmsg_alloc();

	if (!msg) {
		print_debug(LOG_ERR, "Error allocating new message MSG!\n");
		return -1;
	}

	if (family_id < 0) {
		nlmsg_free(msg);
		return -1;
	}

	genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, family_id,
		    0, NLM_F_REQUEST, HWSIM_CMD_REGISTER, VERSION_NR);

	nl_send_auto_complete(sock, msg);
	nlmsg_free(msg);

	return 0;
}

/**
 *	@brief This will create a netlink message with the frame data for an
 *	ack response and send it to wmasterd.
 *	@param freq - the frequency on which this ack is sent
 *	@param src - the src address of the data frame this is response to
 *	@param dst - the dst address which now because the source of the ack
 *	@return void
 */
static void generate_ack_frame(uint32_t freq, struct ether_addr *src,
		struct ether_addr *dst)
{
	/* turn into a netlink message's format */
	int rc;
	struct nl_msg *msg;
	int bytes;
	int msg_len;
	int ack_size;
	struct ieee80211_hdr *hdr11;
	struct nlmsghdr *nlh;

	ack_size = sizeof(struct ieee80211_hdr);

	hdr11 = (struct ieee80211_hdr *) malloc(ack_size);
	memset(hdr11, 0, ack_size);

	hdr11->frame_control = IEEE80211_FTYPE_CTL | IEEE80211_STYPE_ACK;
	memcpy(hdr11->addr1, src, ETH_ALEN);

	msg = nlmsg_alloc();

	if (!msg) {
		print_debug(LOG_ERR, "Error allocating new message MSG!\n");
		free(hdr11);
		return;
	}

	/* create my ten byte response */
/*
	char data[10];
	memset(data, 0, 10);
	data[0] = 0xd4;
	memcpy(&data[4], src, ETH_ALEN);
*/
	/* may need to spoof so it loos to welled like it came from
	 * the hwsim driver on another system? maybe not as long as it
	 * shows up in a tcpdump
	 */
	if (family_id < 0)
		goto out;

	genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, family_id,
		    0, NLM_F_REQUEST, HWSIM_CMD_FRAME, VERSION_NR);

	/* set source address */
	rc = nla_put(msg, HWSIM_ATTR_ADDR_TRANSMITTER,
		     sizeof(struct ether_addr), dst);
	rc = nla_put(msg, HWSIM_ATTR_FRAME, ack_size,
			hdr11);

	if (freq)
		rc = nla_put_u32(msg, HWSIM_ATTR_FREQ, freq);

	/* TODO: add COOKIE and TX info, signal, rate */

	if (rc != 0) {
		print_debug(LOG_ERR, "Error filling payload\n");
		goto out;
	}

	nlh = nlmsg_hdr(msg);
	msg_len = nlh->nlmsg_len;

	/* send frame to wmasterd */
	pthread_mutex_lock(&send_mutex);
	bytes = sendto(sockfd, (char *)nlh, msg_len, 0,
			(struct sockaddr *)&servaddr_vm,
			sizeof(struct sockaddr));
	pthread_mutex_unlock(&send_mutex);

	if (bytes < 0) {
		/* wmasterd probably down */
		perror("sendto");
		print_debug(LOG_ERR, "ERROR: Could not TX to wmasterd via VMCI\n");
	} else {
		print_debug(LOG_INFO, "sent %d bytes to wmasterd", bytes);
	}

	/* free stuff */
out:
	free(hdr11);
	nlmsg_free(msg);
}

/**
 *	@brief parse vmci data received from wmastered.
 *	At the moment, this will not
 *	include any txinfo messages since we immediately ack them inside
 *	the process_messages_cb function
 *	TODO: figure out what to do about tx_info data to better ack
 *	@return void
 */
void recv_from_master(void)
{
	char buf[VMCI_BUFF_LEN];
	struct sockaddr_vm cliaddr_vmci;
	struct sockaddr_in cliaddr;
	socklen_t addrlen;
	struct timeval tv; /* timer to break out of recvfrom function */
	int bytes;
	struct nlmsghdr *nlh;
	struct genlmsghdr *gnlh;
	struct nlattr *attrs[HWSIM_ATTR_MAX + 1];
	uint32_t freq;
	struct ether_addr *src;
	unsigned int data_len;
	char *data;
	int rate_idx;
	int signal;
	struct ether_addr *dst;	/* stores user mac */
	int i;
	char addr[18];
	struct ether_addr radiomac;
	struct device_node *node;
	struct ether_addr framedst;
	int should_ack;
	int retval;
	int distance;

	addrlen = sizeof(struct sockaddr);
	memset(addr, 0, sizeof(addr));
	memset(&cliaddr_vmci, 0, sizeof(cliaddr_vmci));
	memset(&cliaddr, 0, sizeof(cliaddr));

	tv.tv_sec = 1;
	tv.tv_usec = 0;

	if (setsockopt(myservfd, SOL_SOCKET, SO_RCVTIMEO, &tv,
			 sizeof(tv)) < 0) {
		perror("setsockopt");
	}

	/* receive packets from wmasterd */
	bytes = recvfrom(myservfd, (char *)buf, VMCI_BUFF_LEN, 0,
			(struct sockaddr *)&cliaddr_vmci, &addrlen);

	if (bytes < 0)
		return;

	print_debug(LOG_INFO, "received %d bytes packet from src host: %u",
			bytes, cliaddr_vmci.svm_cid);

	/* netlink header */
	nlh = (struct nlmsghdr *)buf;

	/* generic netlink header */
	gnlh = nlmsg_data(nlh);

	/*
	printf("len %d\n", nlh->nlmsg_len);
	printf("type %d\n", nlh->nlmsg_type);
	printf("flags %d\n", nlh->nlmsg_flags);
	printf("seq %d\n", nlh->nlmsg_seq);
	printf("pid %d\n", nlh->nlmsg_pid);
	*/

	/*
	printf("cmd %u\n", gnlh->cmd);
	printf("version %u\n", gnlh->version);
	printf("reserved %u\n", gnlh->reserved);
	*/

	/* exit if the message does not contain frame data */
	if (gnlh->cmd != HWSIM_CMD_FRAME) {
		print_debug(LOG_ERR, "Error - received no frame data in message");
		if (verbose) {
			hex_dump(nlh, bytes);
		}
		goto out;
	}

	/* we get the attributes*/
	genlmsg_parse(nlh, 0, attrs, HWSIM_ATTR_MAX, NULL);

	if (attrs[HWSIM_ATTR_FREQ])
		freq = nla_get_u32(attrs[HWSIM_ATTR_FREQ]);
	else
		freq = 0;

	/*
	printf("#### hwsim -> welled nlmsg beg ####\n");
	nlh_print(nlh);
	gnlh_print(gnlh);
	attrs_print(attrs);
	printf("#### hwsim -> welled nlmsg end ####\n");
	*/

	/* TODO: check whether this is an ACK FRAME
	 * it would have TX info, COOKIE, and SIGNAL data
	 * i could then sent tx info message to the driver
	 */

	/*  ignore HWSIM_CMD_TX_INFO_FRAME for now */
	if (gnlh->cmd == HWSIM_CMD_TX_INFO_FRAME) {
		print_debug(LOG_WARNING, "Ignoring HWSIM_CMD_TX_INFO_FRAME\n");
		goto out;
	}
	if (!attrs[HWSIM_ATTR_ADDR_TRANSMITTER]) {
		print_debug(LOG_WARNING, "Message does not contain tx address\n");
		goto out;
	}
	src = (struct ether_addr *)
		nla_data(attrs[HWSIM_ATTR_ADDR_TRANSMITTER]);

	data_len = nla_len(attrs[HWSIM_ATTR_FRAME]);
	data = (char *)nla_data(attrs[HWSIM_ATTR_FRAME]);

/*
 * instead of calling send_frames_to_radios_with_retries, we
 * also skip send_frame_msg_apply_prob_and_rate by
 * instead of calling send_frame_msg_apply_prob_and_rate
 * we just loop the radios and call send_cloned_frame_msg ourselves

 * if we want to get crazy, we could send an ack back to the master
 * and then the master could send an ack back to the orignal client
 * the master would have to keep track of the mac/client pairings

*/

	/* check whether we have added the distance */
	distance = -1;

	if (bytes > nlh->nlmsg_len) {
		if (verbose) {
			print_debug(LOG_DEBUG, "packet is %d bytes larger than nlmsg",
				bytes - nlh->nlmsg_len);
			print_debug(LOG_DEBUG, "nlmsg_len %d", nlh->nlmsg_len);
		}

		/* check for appended distance */
		if (strncmp(buf + nlh->nlmsg_len, "welled", 6) == 0)
			distance = atoi(buf + nlh->nlmsg_len + 7);
	}

	print_debug(LOG_DEBUG, "distance from tx to rx here: %d", distance);

	double loss;
	loss = 0;

	if (distance > 0) {
		int gain_tx;
		int gain_rx;

		gain_tx = 9;
		gain_rx = 9;

		if (freq > 0) {
			loss = 20 * log10(distance * .0001) +
				20 * log10(freq) +
				32.44 - gain_tx - gain_rx;
		} else {
			loss = 20 * log10(distance * .0001) +
				20 * log10(2412) +
				32.44 - gain_tx - gain_rx;
		}
		print_debug(LOG_DEBUG, "signal is loss: %f", loss);

		signal = -10 - loss;
		rate_idx = 0;
	} else if (distance == 0) {
		signal = -10;
		rate_idx = 7;
	} else {
		signal = -15;
		rate_idx = 7;
	}

	if (verbose) {
		printf("signal: %d\n", signal);
		printf("freq: %d\n", freq);
		printf("rate_idx: %d\n", rate_idx);
		printf("loss: %f\n", loss);
	}


	/* copy dst address from frame */
	memcpy(&framedst, data + 4, ETH_ALEN);

	if (verbose) {
		mac_address_to_string(addr, src);
                printf("frame src: %s\n", addr);
		mac_address_to_string(addr, &framedst);
		printf("frame dst: %s\n", addr);
	}

	pthread_mutex_lock(&list_mutex);
	for (i = 0; i < devices; i++) {
		should_ack = 0;

		node = get_node_by_pos(i);
		if (node == NULL)
			continue;

		if (verbose)
			printf("sending to: %s\n", node->name);

		dst = (struct ether_addr *)node->address;

		if (dst == NULL)
			continue;

		/* compare frame dst to nlmsg src */
		if (memcmp(src, dst, sizeof(struct ether_addr)) == 0) {
			/* this radio sent the frame, skip it */
			if (verbose)
				printf("- this radio sent this frame\n");
			continue;
		}

		/* we should ack if:
		 * the frame was sent to this radio's address
		 * this is not an ack packet itself
		 */
		if ((memcmp(&framedst, dst, ETH_ALEN) == 0) &&
			(nlh->nlmsg_len != 56)) {
			should_ack = 1;
		}

		if (verbose) {
			mac_address_to_string(addr, dst);
			printf("- old radio mac: %s\n", addr);
		}

		if (any_mac) {
			retval = send_cloned_frame_msg(dst, data, data_len,
				rate_idx, signal, freq);
			/*
			 * if we sent it successfully, ack it for now. this
			 * needs to get updated to check a queue and the kernel
			 * response via netlink. only ack if the kernel accepted
			 */
			if (should_ack && !retval) {
				if (verbose)
					printf("- attempting to ack\n");
				generate_ack_frame(freq, src, dst);
			}
			continue;
		}

/*
 * we have a source of error here:
 * the driver isnt expecting our user-defined mac address
 * as it is expecting 42:00:00:00:00:00 for the first radio. we need to use the
 * actual address used by hwsim, either 0x42 without my patch or whatever is
 * set as a result of my local perm_addr patch.
 *
 * we must pass the dst address expected by the kernel: data->addresses[1]
 *
 * new methods:
 *	set dst to perm_addr instead of address, src would be a perm_addr too
 *	not just perm_addr, which is data->addresses[0], but the second address
 *	which is data->addresses[1], set to data->addresses[1].addr[0] |= 0x40;
 *	this means i need to send not to perm_addr, but to
 *	perm_addr[0] |= 0x40;
 * or
 *	set radiomac to perm_addr instead of hard coding - done
 *	TODO: investigate changes mentioned above for dst/src
 */

		if (verbose)
			printf("- we are changing the address\n");

		memcpy(radiomac.ether_addr_octet, node->perm_addr, ETH_ALEN);

		/* check whether this radio mac matches our nl msg data */
		if (memcmp((char *)&radiomac, dst, ETH_ALEN) == 0) {
			if (verbose)
				printf("- radio mac: %s === dst\n", addr);

			send_cloned_frame_msg(dst, data, data_len, rate_idx,
				signal, freq);
		} else {
			/* we need to update data inside nl msg
			 * we could also look into updating the attribute
			 * using the proper functions
			 */
			memcpy((char *)nlh + 24, (char *)&radiomac, ETH_ALEN);

			if (verbose) {
				mac_address_to_string(addr, (struct ether_addr *)nla_data(attrs[HWSIM_ATTR_ADDR_TRANSMITTER]));
				printf("- new radio mac: %s\n", addr);
			}
			retval = send_cloned_frame_msg(&radiomac, data,
				data_len, rate_idx, signal, freq);

			if (should_ack && !retval) {
				if (verbose)
					printf("- attempting to ack\n");
				/* TODO: determine correct values below */
				generate_ack_frame(freq, src, dst);
			}
			continue;

		}
	}
	pthread_mutex_unlock(&list_mutex);

out:
	//free(new_buf);
	if (verbose)
		printf("#################### master recv done #####################\n");
}

/**
 *	@brief thread to receive and process frames from wmasterd
 */
void *process_master(void *arg)
{
	while (running) {
		recv_from_master();
	}
	print_debug(LOG_DEBUG, "process_master returning");
	return ((void *)0);
}

/**
 *	@brief processes RTM_DELLINK messages
 *	Detects when an interface has been removed.
 *	@param h - netlink message header
 *	@return void
 */
void dellink(struct nlmsghdr *h)
{
	struct ifinfomsg *ifi;

	ifi = NLMSG_DATA(h);

	/* require an interface name */
	if (!(ifi->ifi_flags & IFLA_IFNAME))
		return;

	/* update nodes */
	pthread_mutex_lock(&list_mutex);
	if (remove_node_by_index(ifi->ifi_index)) {
		devices--;
		if (verbose) {
			printf("############ dellink ############\n");
			printf("index:	  %d\n", ifi->ifi_index);
			printf("deleted device; now %d devices\n", devices);
			list_nodes();
			printf("#################################\n");
		}
	}
	pthread_mutex_unlock(&list_mutex);

}
/**
 *	@brief processes RTM_NEWLINK messages
 *	Detects when interfaces are created and modified
 *	@param h - netlink message header
 *	@return void
 */
void newlink(struct nlmsghdr *h)
{
	int len;
	struct rtattr *tb[IFLA_MAX + 1];
	char *name;
	struct rtattr *rta;
	struct ifinfomsg *ifi;
	unsigned char addr[ETH_ALEN];

	ifi = NLMSG_DATA(h);

	/* down - no address
	 * addr - no address
	 * up   - has address
	 */
	if (!(ifi->ifi_flags & IFLA_ADDRESS))
		return;

	/* retrieve all attributes */
	memset(tb, 0, sizeof(tb));
	rta = IFLA_RTA(ifi);
	len = h->nlmsg_len - NLMSG_LENGTH(sizeof(struct ifinfomsg));
	while (RTA_OK(rta, len)) {
		if (rta->rta_type <= IFLA_MAX)
			tb[rta->rta_type] = rta;
		rta = RTA_NEXT(rta, len);
	}

	/* require name field to be set */
	if (tb[IFLA_IFNAME]) {
		name = (char *)RTA_DATA(tb[IFLA_IFNAME]);
	} else {
		return;
	}

	if (verbose) {
		printf("############ newlink ############\n");
		printf("index:   %d\n", ifi->ifi_index);
		printf("name:    %s\n", name);

		if (ifi->ifi_family == AF_UNSPEC)
			printf("family:  AF_UNSPEC\n");
		if (ifi->ifi_family == AF_INET6)
			printf("family:  AF_INET6\n");

		/* ARPHRD_ETHER normal, ARPHRD_IEEE80211_RADIOTAP as monitor */
		if (ifi->ifi_type == ARPHRD_ETHER)
			printf("type:    ARPHRD_ETHER\n");
		else if (ifi->ifi_type == ARPHRD_IEEE80211_RADIOTAP)
			printf("type:    ARPHRD_IEEE80211_RADIOTAP\n");
		else
			printf("type:    UNKNOWN\n");
		printf("#################################\n");
	}

	/* require address field to be set */
	if (tb[IFLA_ADDRESS]) {
		memcpy((char *)addr, RTA_DATA(tb[IFLA_ADDRESS]), ETH_ALEN);
		if (verbose) {
			printf("address: %02X:%02X:%02X:%02X:%02X:%02X\n",
				addr[0], addr[1],
				addr[2], addr[3],
				addr[4], addr[5]);
		}
	} else {
		return;
	}

	nl80211_get_interface(ifi->ifi_index);

}

static int process_nl_route_event(struct nl_msg *msg, void *arg)
{
	struct nlmsghdr *nlh;
	int len;

	nlh = nlmsg_hdr(msg);
	len = nlh->nlmsg_len;

	while (NLMSG_OK(nlh, (unsigned int)len)) {
		switch (nlh->nlmsg_type) {
		case NLMSG_DONE:
			nlmsg_end = 1;
			break;
		case NLMSG_ERROR:
			perror("read_netlink");
			break;
		case RTM_NEWLINK:
			newlink(nlh);
			break;
		case RTM_DELLINK:
			dellink(nlh);
			break;
		default:
			break;
		}
		nlh = NLMSG_NEXT(nlh, len);
	}
	return 0;
}


/**
 *	@brief processes netlink route events from the kernel
 *	Determines the event type for netlink route messages (del/new).
 *	@param fd - the netlink socket
 *	@return void
 */
void process_event(int fd)
{
	int len;
	char buf[IFLIST_REPLY_BUFFER];
	struct iovec iov = { buf, sizeof(buf) };
	struct sockaddr_nl snl;
	struct msghdr msg = { (void *)&snl, sizeof(snl), &iov, 1, NULL, 0, 0 };
	struct nlmsghdr *h;

	/* read the waiting message */
	len = recvmsg(fd, &msg, 0);
	if (len < 0)
		perror("read_netlink");
	for (h = (struct nlmsghdr *)buf;
			NLMSG_OK(h, (unsigned int)len);
			h = NLMSG_NEXT(h, len)) {
/*
 * RTM_NEWLINK (kernel→user)
 * This message type is used in reply to a RTM_GETLINK request and carries the
 * configuration and statistics of a link. If multiple links need to be sent,
 * the messages will be sent in form of a multipart message.
 *
 * The message type is also used for notifications sent by the kernel to the
 * multicast group RTNLGRP_LINK to inform about various link events. It is
 * therefore recommended to always use a separate link socket for link
 * notifications in order to separate between the two message types.
 */

		switch (h->nlmsg_type) {
		case NLMSG_DONE:
			nlmsg_end = 1;
			break;
		case NLMSG_ERROR:
			perror("read_netlink");
			break;
		case RTM_NEWLINK:
			newlink(h);
			break;
		case RTM_DELLINK:
			dellink(h);
			break;
		default:
			break;
		}
	}
}

/**
 *	@brief Monitor netlink route messages
 *	This is meant to be a thread which receives link status changes
 *	and will update the linked list of devices as needed
 */
void *monitor_devices(void *arg)
{
/*
	fd_set rfds;
	int ret;
	int fd;
	struct sockaddr_nl addr;
*/
	struct timeval tv;
	struct nl_sock *sk;
	int nlsockfd;

	sk = nl_socket_alloc();
	nl_socket_disable_seq_check(sk);
	nl_socket_modify_cb(sk, NL_CB_VALID, NL_CB_CUSTOM, process_nl_route_event, NULL);
	nl_connect(sk, NETLINK_ROUTE);
	nl_socket_add_memberships(sk, RTMGRP_LINK, RTMGRP_IPV4_IFADDR, 0);

	nlsockfd = nl_socket_get_fd(sk);

	tv.tv_sec = 1;
	tv.tv_usec = 0;

	if (setsockopt(nlsockfd, SOL_SOCKET, SO_RCVTIMEO, &tv,
			 sizeof(tv)) < 0) {
		perror("setsockopt");
	}

/*
	ret = 0;

	//  create a netlink route socket
	fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);

	// joins the multicast groups for link notifications
	memset(&addr, 0, sizeof(addr));
	addr.nl_family = AF_NETLINK;
	addr.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR;
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		perror("bind");
*/
	while (running) {
		nl_recvmsgs_default(sk);
/*
		FD_ZERO(&rfds);
		FD_CLR(fd, &rfds);
		FD_SET(fd, &rfds);

		tv.tv_sec = 1;
		tv.tv_usec = 0;

		ret = select(FD_SETSIZE, &rfds, NULL, NULL, &tv);
		if (ret == -1)
			perror("select");
		else if (ret)
			process_event(fd);
		else {
			printf("old thing times out\n");
			continue;
		}
		*/
	}
	nl_close(sk);
	nl_socket_free(sk);

	print_debug(LOG_DEBUG, "monitor_devices returning");
	return ((void *)0);
}

/***
 *	@brief send status message to wmasterd at some interval
 *	This thread tells wmasterd we are alive every ten seconds while
	 our radio is in monitor mode.
 *	@return void
 */
void *send_status(void *arg)
{
	/* send up notification to wmasterd */
	char *msg;
	int msg_len;
	int bytes;

	/* TODO: add debug detail to message. wmastered would require an
	 * adjustment to the size check it performs. this info could be:
	 *   number of interfaces
	 *   interface mac addresses
	 *   perm_addrs
	 *   state of interfaces (monitor, up, down)
	 */

	msg_len = 2;
	msg = malloc(msg_len);
	memset(msg, 0, msg_len);
	memcpy(msg, "UP", msg_len);

	while (running) {

		sleep(1);

		/* determine if a device is in monitor mode */
		if (!monitor_mode())
			continue;

		pthread_mutex_lock(&send_mutex);
		bytes = sendto(sockfd, msg, msg_len, 0,
				(struct sockaddr *)&servaddr_vm,
				sizeof(struct sockaddr));
		pthread_mutex_unlock(&send_mutex);

		/* this should be 8 bytes */
		if (bytes != msg_len) {
			perror("sendto");
			print_debug(LOG_ERR, "Up notification failed");
		} else {
			print_debug(LOG_DEBUG, "Up notification sent to wmasterd");
		}
		sleep(9);
	}

	free(msg);

	print_debug(LOG_DEBUG, "send_status returning");
	return ((void *)0);
}

/**
 *	@brief this is meant to be a thread which detects removal of driver
 *	Used to suspend normal actions until driver is loaded
 *	@return void
 */
void *monitor_hwsim(void *arg)
{
	struct nl_sock *sock2;

	sock2 = nl_socket_alloc();
	genl_connect(sock2);

	while (running) {

		sleep(1);
		family_id = genl_ctrl_resolve(sock2, "MAC80211_HWSIM");

		if (family_id < 0) {
			print_debug(LOG_INFO, "Driver has been unloaded");

			/*  we call init_netlink which returns when back up */
			pthread_mutex_lock(&hwsim_mutex);
			if (!init_netlink())
				continue;
			/* Send a register msg to the kernel */
			if (send_register_msg() == 0)
				print_debug(LOG_NOTICE, "Registered with family MAC80211_HWSIM");
			pthread_mutex_unlock(&hwsim_mutex);
		}
	}
	nl_close(sock2);
	nl_socket_free(sock2);

	print_debug(LOG_DEBUG, "monitor_hwsim returning");
	return ((void *)0);
}

/**
 *	@brief Provides status code of 0 when all messages have been parsed
 *	Called by nl80211_get_interface
 *	@param msg - netlink message
 *	@param arg - pointer to store return code
 *	@return
 */
static int finish_handler(struct nl_msg *msg, void *arg)
{
	int *ret = arg;
	*ret = 0;
	return NL_SKIP;
}

/**
 *	@brief parses the list of wireless interfaces as a result of a call
 *	to the nl80211_get_interface function which uses nl80211 to run the
 *	command NL80211_CMD_GET_INTERFACE
 *	This is used to initialize the list of wireless devices when welled
 *	starts exeution. netlink route messages are used to detect changes.
 *	@param msg - netlink message
 *	@param arg - pointer to store return code
 *	@return
 */
static int list_interface_handler(struct nl_msg *msg, void *arg)
{
	struct nlattr *tb_msg[NL80211_ATTR_MAX + 1];
	struct genlmsghdr *gnlh;
	unsigned char addr[ETH_ALEN];
	char *ifname;
	int ifindex;
	struct device_node *node;
	int iftype;
	int err;
	int ioctl_fd;
	struct ethtool_perm_addr *epaddr;
	struct ifreq ifr;

	gnlh = nlmsg_data(nlmsg_hdr(msg));
	nla_parse(tb_msg, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
			genlmsg_attrlen(gnlh, 0), NULL);

	if (tb_msg[NL80211_ATTR_IFNAME])
		ifname = nla_get_string(tb_msg[NL80211_ATTR_IFNAME]);
	else
		return NL_SKIP;

	if (tb_msg[NL80211_ATTR_IFINDEX])
		ifindex = nla_get_u32(tb_msg[NL80211_ATTR_IFINDEX]);
	else
		return NL_SKIP;

	if (tb_msg[NL80211_ATTR_MAC])
		memcpy(&addr, nla_data(tb_msg[NL80211_ATTR_MAC]), ETH_ALEN);
	else
		return NL_SKIP;

	if (tb_msg[NL80211_ATTR_IFTYPE])
		iftype = nla_get_u32(tb_msg[NL80211_ATTR_IFTYPE]);
	else
		return NL_SKIP;

	/* update nodes */
	pthread_mutex_lock(&list_mutex);
	node = get_node_by_index(ifindex);
	if (node == NULL) {
		/* create node because it is a new interface */
		node = malloc(sizeof(struct device_node));
		if (node == NULL) {
			perror("malloc");
			_exit(EXIT_FAILURE);
		}
		node->next = NULL;

		/* set name */
		node->name = malloc(strlen(ifname) + 1);
		strncpy(node->name, ifname, strlen(ifname) + 1);

		/* set index */
		node->index = ifindex;

		/* set interface type */
		node->iftype = iftype;

		/* initialilze socket to be used for ioctl */
		ioctl_fd = socket(AF_INET, SOCK_STREAM, 0);
		if (ioctl_fd < 0) {
			perror("socket");
			_exit(EXIT_FAILURE);
		}

		epaddr = malloc(sizeof(epaddr) + MAX_ADDR_LEN);
		epaddr->cmd = ETHTOOL_GPERMADDR;
		epaddr->size = MAX_ADDR_LEN;
		memset(&ifr, 0, sizeof(ifr));
		strncpy(ifr.ifr_name, ifname, sizeof(node->name));
		ifr.ifr_data = (void *)epaddr;

		err = ioctl(ioctl_fd, SIOCETHTOOL, &ifr);
		if (err < 0)
			perror("error: Cannot read permanent address");
		else
			memcpy(&node->perm_addr, epaddr->data, ETH_ALEN);

		/* adjust to addresses[1] format for unpatched drivers */
		if (node->perm_addr[0] == 0x02)
			node->perm_addr[0] |= 0x40;

		free(epaddr);

		/* when the address match patch gets reverted upstream, which
		 * it now has since mid december 2015, we will need to perform
		 * a check as seen below to determine whether to adjust the
		 * netlink destination address attribute or not.
		 * if ((iftype == NL80211_IFTYPE_MONITOR) && (any_mac)) {
		 */
		/* set address unless monitor mode mode */
		if (iftype == NL80211_IFTYPE_MONITOR) {
			/* if NL80211_IFTYPE_MONITOR, set 00's */
			memset(&node->address, 0, ETH_ALEN);
		} else {
			memcpy(&node->address, &addr, ETH_ALEN);
		}

		add_node(node);
		devices++;

		if (verbose) {
			print_debug(LOG_INFO, "added node, now %d devices", devices);
			list_nodes();
		}
	} else {
		/* update existing node if type changed */
		if (node->iftype != iftype) {
			node->iftype = iftype;
			print_debug(LOG_DEBUG, "iftype changed");
		}
		/* update existing node if name changed */
		if (strncmp(node->name, ifname, strlen(ifname)) != 0) {
			/* overwrite name */
			strncpy(node->name, ifname, sizeof(node->name) + 1);
			if (verbose) {
				print_debug(LOG_DEBUG, "name changed");
				list_nodes();
			}
		}
		/* update existing node if address changed */
		if (memcmp(&node->address, &addr, ETH_ALEN) != 0) {
			if (iftype == NL80211_IFTYPE_MONITOR) {
				print_debug(LOG_DEBUG, "setting address to 00's");
				memset(&node->address, 0, ETH_ALEN);
			} else {
				memcpy(&node->address, &addr, ETH_ALEN);
			}
			if (verbose) {
				print_debug(LOG_DEBUG, "address changed");
				list_nodes();
			}
		}
	}
	if (verbose)
		printf("#################################\n");
	pthread_mutex_unlock(&list_mutex);

	return NL_SKIP;
}

/**
 *	@brief Uses nl80211 to initialize a list of wireless interfaces
 *	 Processes multiple netlink messages containing interface data
 *	@return error codes
 */
int nl80211_get_interface(int ifindex)
{
	struct nl_msg *nlmsg;
	struct nl_cb *cb;
	int err;
	int flags;

	wifi.nls = nl_socket_alloc();
	if (!wifi.nls) {
		print_debug(LOG_ERR, "Error: failed to allocate netlink socket.");
		return -ENOMEM;
	}
	nl_socket_set_buffer_size(wifi.nls, 8192, 8192);
	if (genl_connect(wifi.nls)) {
		print_debug(LOG_ERR, "Error: failed to connect to generic netlink.");
		nl_socket_free(wifi.nls);
		return -ENOLINK;
	}
	wifi.nl80211_id = genl_ctrl_resolve(wifi.nls, "nl80211");
	if (wifi.nl80211_id < 0) {
		print_debug(LOG_ERR, "Error: nl80211 not found.");
		nl_socket_free(wifi.nls);
		return -ENOENT;
	}
	nlmsg = nlmsg_alloc();
	if (!nlmsg) {
		print_debug(LOG_ERR, "Error: failed to allocate netlink message.");
		return -ENOMEM;
	}
	cb = nl_cb_alloc(NL_CB_DEFAULT);
	if (!cb) {
		print_debug(LOG_ERR, "Error: failed to allocate netlink callback.");
		nlmsg_free(nlmsg);
		return -ENOMEM;
	}
	nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, list_interface_handler, NULL);

	if (ifindex)
		flags = 0;
	else
		flags = NLM_F_DUMP;

	genlmsg_put(nlmsg, 0, 0, wifi.nl80211_id, 0,
			flags, NL80211_CMD_GET_INTERFACE, 0);

	if (ifindex)
		nla_put_u32(nlmsg, NL80211_ATTR_IFINDEX, ifindex);

	nl_send_auto_complete(wifi.nls, nlmsg);

	nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &err);

	if (ifindex) {
		nl_recvmsgs(wifi.nls, cb);
	} else {
		err = 1;
		while (err > 0)
			nl_recvmsgs(wifi.nls, cb);
	}

	nlmsg_free(nlmsg);
	nl_cb_put(cb);

	nl_socket_free(wifi.nls);

	return 0;
}

/**
 *	@brief main function
 */
int main(int argc, char *argv[])
{
	int euid;
	int af;
	int opt;
	int cid;
	int ret;
	int long_index;
	char *msg;
	int msg_len;
	int bytes;
	int err;
	int ioctl_fd;

	verbose = 0;
	any_mac = 0;
	running = 1;
	devices = 0;
	af = 0;
	long_index = 0;
	err = 0;
	ioctl_fd = 0;
	cid = 0;
	family_id = -1;
	loglevel = -1;

	/* TODO: Send syslog message indicating start time */

	static struct option long_options[] = {
		{"help",	no_argument, 0, 'h'},
		{"version",	no_argument, 0, 'V'},
		{"verbose",	no_argument, 0, 'v'},
		{"debug",       required_argument, 0, 'D'},
		{"any",		no_argument, 0, 'a'}
	};

	while ((opt = getopt_long(argc, argv, "hVavD:", long_options,
			&long_index)) != -1) {
		switch (opt) {
		case 'h':
			show_usage(EXIT_SUCCESS);
			break;
		case 'V':
			/* allow help2man to read this */
			setbuf(stdout, NULL);
			printf("welled version %s\n", VERSION_STR);
			_exit(EXIT_SUCCESS);
			break;
		case 'a':
			any_mac = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'D':
			loglevel = atoi(optarg);
			if ((loglevel < 0) || (loglevel > 7)) {
				printf("welled: syslog level must be 0-7\n");
				_exit(EXIT_FAILURE);
			}
			printf("welled: syslog level set to %d\n", loglevel);
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

	if (loglevel >= 0)
		openlog("welled", LOG_PID, LOG_USER);

        euid = geteuid();
        if (euid != 0) {
                print_debug(LOG_ERR, "must run as root");
                _exit(EXIT_FAILURE);
        }

	/* old code for vmci_sockets.h */
	//af = VMCISock_GetAFValue();
	//cid = VMCISock_GetLocalCID();
	//printf("CID: %d\n", cid);

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
		print_debug(LOG_DEBUG, "CID: %u", cid);
	}

	/* Handle signals */
	signal(SIGINT, (void *)signal_handler);
	signal(SIGTERM, (void *)signal_handler);
	signal(SIGQUIT, (void *)signal_handler);
	signal(SIGHUP, SIG_IGN);

	/* init mutex */
	pthread_mutex_init(&hwsim_mutex, NULL);
	pthread_mutex_init(&list_mutex, NULL);
	pthread_mutex_init(&send_mutex, NULL);

	/* init list of interfaces */
	nl80211_get_interface(0);

	/* init netlink will loop until driver is loaded */
	pthread_mutex_lock(&hwsim_mutex);
	ret = init_netlink();
	pthread_mutex_unlock(&hwsim_mutex);
	if (!ret) {
		print_debug(LOG_ERR, "error: could not initialize netlink");
		nl_close(sock);
		nl_socket_free(sock);
		nl_cb_put(cb);
		_exit(EXIT_FAILURE);
	}

	/* Send a register msg to the kernel */
	if (send_register_msg() == 0) {
		print_debug(LOG_NOTICE, "Registered with family MAC80211_HWSIM");
	} else {
		nl_close(sock);
		nl_socket_free(sock);
		nl_cb_put(cb);
		_exit(EXIT_FAILURE);
	}
	/* setup vmci client socket */
	sockfd = socket(af, SOCK_DGRAM, 0);
	/* we can initalize this struct because it never changes */
	memset(&servaddr_vm, 0, sizeof(servaddr_vm));
	servaddr_vm.svm_cid = VMADDR_CID_HOST;
	servaddr_vm.svm_port = SEND_PORT;
	servaddr_vm.svm_family = af;
	if (sockfd < 0) {
		perror("socket");
		free_mem();
		_exit(EXIT_FAILURE);
	}

	/* create vmci server socket */
	myservfd = socket(af, SOCK_DGRAM, 0);

	/* we can initalize this struct because it never changes */
	memset(&myservaddr_vm, 0, sizeof(myservaddr_vm));
	myservaddr_vm.svm_cid = VMADDR_CID_ANY;
	myservaddr_vm.svm_port = RECV_PORT;
	myservaddr_vm.svm_family = af;
	ret = bind(myservfd, (struct sockaddr *)&myservaddr_vm,
			sizeof(struct sockaddr));
	if (ret < 0) {
		perror("bind");
		close(sockfd);
		close(myservfd);
		free_mem();
		_exit(EXIT_FAILURE);
	}

	/* send up notification to wmasterd */
	msg_len = 2;
	msg = malloc(msg_len);
	memset(msg, 0, msg_len);
	memcpy(msg, "UP", msg_len);

	pthread_mutex_lock(&send_mutex);
	bytes = sendto(sockfd, msg, msg_len, 0,
			(struct sockaddr *)&servaddr_vm,
			sizeof(struct sockaddr));
	pthread_mutex_unlock(&send_mutex);
	free(msg);

	/* this should be 2 bytes */
	if (bytes != msg_len) {
		perror("sendto");
		print_debug(LOG_ERR, "Up notification failed");
	} else {
		print_debug(LOG_DEBUG, "Up notification sent to wmasterd");
	}
	if (verbose)
		printf("################################################################################\n");

	/* start thread to monitor devices */
	ret = pthread_create(&dev_tid, NULL, monitor_devices, NULL);
	if (ret < 0) {
		perror("pthread_create");
		print_debug(LOG_ERR, "error: pthread_create monitor_devices");
		running = 0;
	}

	/* start thread to monitor for hwsim netlink family changes */
	ret = pthread_create(&hwsim_tid, NULL, monitor_hwsim, NULL);
	if (ret < 0) {
		perror("pthread_create");
		print_debug(LOG_ERR, "error: pthread_create monitor_hwsim");
		running = 0;
	}

	/* start thread to transmit up status message to wmasterd */
	ret = pthread_create(&status_tid, NULL, send_status, NULL);
	if (ret < 0) {
		perror("pthread_create");
		print_debug(LOG_ERR, "error: pthread_create send_status");
		running = 0;
	}

	/* start thread to recv from wmasterd */
	ret = pthread_create(&master_tid, NULL, process_master, NULL);
	if (ret < 0) {
		perror("pthread_create");
		print_debug(LOG_ERR, "error: pthread_create process_master");
		running = 0;
	}

	/* We wait for incoming msg*/
	while (running) {
		pthread_mutex_lock(&hwsim_mutex);
		nl_recvmsgs_default(sock);
		pthread_mutex_unlock(&hwsim_mutex);
	}

	/* code below here executes after signal */

	pthread_join(dev_tid, NULL);
	pthread_join(hwsim_tid, NULL);
	pthread_join(status_tid, NULL);
	pthread_join(master_tid, NULL);

	print_debug(LOG_DEBUG, "Threads have been cancelled");

	close(sockfd);
	close(myservfd);
	close(ioctl_fd);

	print_debug(LOG_DEBUG, "Sockets have been closed");

	/*Free all memory*/
	free_mem();

	print_debug(LOG_DEBUG, "Memory has been cleared");

	pthread_mutex_destroy(&list_mutex);
	pthread_mutex_destroy(&hwsim_mutex);
	pthread_mutex_destroy(&send_mutex);

	print_debug(LOG_DEBUG, "Mutices have been destroyed");

	print_debug(LOG_NOTICE, "Exiting");

	_exit(EXIT_SUCCESS);
}
