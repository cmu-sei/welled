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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <ifaddrs.h>
#include <netlink/route/link.h>
#include <linux/if_arp.h>
#include <linux/nl80211.h>

#include "nodes.h"

struct device_node *head;

/**
 *      @brief Adds a node to the linked list
 *      @param node - the new device node
 *      @return void - success always assumed
 */
void add_device_node(struct device_node *node)
{
	struct device_node *curr;

	curr = NULL;

	/* create new node */
	if (head == NULL) {
		/* add first node */
		head = node;
	} else {
		/* traverse to end of list */
		curr = head;
		while (curr->next != NULL)
			curr = curr->next;
		/* add to node to end of list */
		curr->next = node;
	}
}

/**
 *      @brief Searches the linked list for a given node
 *      @param name - name of interface we are searching for
 *      @return returns a pointer to the node
 */
struct device_node *get_device_node_by_name(char *name)
{
	struct device_node *curr;

	curr = head;

	while (curr != NULL) {
		if (strcmp(curr->name, name) == 0)
			return curr;

		curr = curr->next;
	}

	return NULL;
}

/**
 *      @brief Searches the linked list for a given node
 *      @param index - index of interface we are searching for
 *      @return returns a pointer to the node
 */
struct device_node *get_device_node_by_index(int index)
{
	struct device_node *curr;

	curr = head;

	while (curr != NULL) {
		if (curr->index == index)
			return curr;

		curr = curr->next;
	}

	return NULL;
}

/**
 *      @brief Searches the linked list for a given node
 *      @param index - index of interface we are searching for
 *      @return returns a pointer to the node
 */
struct device_node *get_device_node_by_radio_id(int id)
{
	struct device_node *curr;

	curr = head;

	while (curr != NULL) {
		if (curr->radio_id == id)
			return curr;

		curr = curr->next;
	}

	return NULL;
}

/**
 *      @brief Searches the linked list for a given node
 *      @param index - index of interface we are searching for
 *      @return returns a pointer to the node
 */
struct device_node *get_device_node_by_perm_addr(char *perm_addr)
{
	struct device_node *curr;

	int radio = perm_addr[4];

	curr = head;

	while (curr != NULL) {
		if (curr->radio_id == radio)
			return curr;

		curr = curr->next;
	}

	return NULL;
}

/**
 *      @brief Searches the linked list for a given node
 *      @param index - index of interface we are searching for
 *      @return returns a pointer to the node
 */
struct device_node *get_device_node_by_wiphy(int id)
{
	struct device_node *curr;

	curr = head;

	while (curr != NULL) {
		if (curr->wiphy == id)
			return curr;

		curr = curr->next;
	}

	return NULL;
}

/**
 *      @brief Searches the linked list for a given node
 *      @param pos - position of interface we are searching for
 *      @return returns a pointer to the node
 */
struct device_node *get_device_node_by_pos(int pos)
{
	struct device_node *curr;
	int i;

	curr = head;
	i = 0;

	while (curr != NULL) {
		if (i == pos)
			return curr;
		i++;
		curr = curr->next;
	}

	return NULL;
}

/**
 *	@brief Searches the linked list until a monitor mode device is found
 *	@return true if device is in monitor mode
 */
int monitor_mode_active(void)
{
	struct device_node *curr;

	curr = head;

	while (curr != NULL) {
		if (curr->iftype == NL80211_IFTYPE_MONITOR)
			return 1;
		curr = curr->next;
	}

    return 0;
}

/**
 *      @brief Lists the nodes in the linked list
 *      @return void
 */
void list_device_nodes(void)
{
	struct device_node *curr;
	int i;

	i = 0;

	curr = head;

	while (curr != NULL) {
		print_device_node(i, curr);
		i++;
		curr = curr->next;
	}
}

/**	@brief print device node data
 *	@param i - index to be displayed in print
 *	@param curr - pointer to the node
 *	@return void - assumes success
 */
void print_device_node(int i, struct device_node *curr)
{
	printf("N[%d]:name:      %s\n", i, curr->name);
	printf("N[%d]:index:     %d\n", i, curr->index);
	printf("N[%d]:radio_id   %d\n", i, curr->radio_id);
	printf("N[%d]:wiphy      %d\n", i, curr->wiphy);
	printf("N[%d]:netnsid    %ld\n", i, curr->netnsid);
	switch (curr->iftype) {
		case 2:
			printf("M[%d]:iftype:    NL80211_IFTYPE_STATION\n", i);
			break;
		case 3:
			printf("M[%d]:iftype:    NL80211_IFTYPE_AP\n", i);
			break;
		case 6:
			printf("M[%d]:iftype:    NL80211_IFTYPE_MONITOR\n", i);
			break;
		case 7:
			printf("M[%d]:iftype:    NL80211_IFTYPE_MESH_POINT\n", i);
			break;
		default:
			printf("M[%d]:iftype:    %d\n", i, curr->iftype);
			break;
	}
	printf("N[%d]:address:   %02X:%02X:%02X:%02X:%02X:%02X\n", i,
		curr->address[0], curr->address[1],
		curr->address[2], curr->address[3],
		curr->address[4], curr->address[5]);
	printf("N[%d]:perm_addr: %02X:%02X:%02X:%02X:%02X:%02X\n", i,
		curr->perm_addr[0], curr->perm_addr[1],
		curr->perm_addr[2], curr->perm_addr[3],
		curr->perm_addr[4], curr->perm_addr[5]);
}

/**
 *      @brief Removes a node from the linked list
 *      @param name - name of interface to be removed
 *      @return success or failure
 */
int remove_device_node_by_name(char *name)
{
	struct device_node *curr;
	struct device_node *prev;

	curr = head;
	prev = NULL;

	/* traverse while not null and no match */
	while (curr != NULL && !(strcmp(curr->name, name))) {
		prev = curr;
		curr = curr->next;
	}

	/* exit if we hit the end of the list */
	if (curr == NULL)
		return 0;

	/* delete node */
	if (prev == NULL)
		head = curr->next;
	else
		prev->next = curr->next;

	free(curr->name);
	free(curr);

	return 1;
}

/**
 *      @brief Removes a node from the linked list
 *      @param index - index of interface to be removed
 *      @return success or failure
 */
int remove_device_node_by_index(int index)
{
	struct device_node *curr;
	struct device_node *prev;

	curr = head;
	prev = NULL;

	/* traverse while not null and no match */
	while (curr != NULL && (curr->index != index)) {
		prev = curr;
		curr = curr->next;
	}

	/* exit if we hit the end of the list */
	if (curr == NULL)
		return 0;

	/* delete node */
	if (prev == NULL)
		head = curr->next;
	else
		prev->next = curr->next;

	free(curr->name);
	free(curr);

	return 1;
}

/**
 *      @brief Frees all of the nodes in the linked list
 *      @return void - assumes success
 */
void free_list(void)
{
	struct device_node *temp = NULL;
	struct device_node *curr = NULL;

	curr = head;

	while (curr != NULL) {
		temp = curr;
		curr = curr->next;
		free(temp->name);
		free(temp);
	}

	head = NULL;
}

