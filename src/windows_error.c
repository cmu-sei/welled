/*
 *      Copyright 2018 Carnegie Mellon University. All Rights Reserved.
 *
 *      NO WARRANTY. THIS CARNEGIE MELLON UNIVERSITY AND SOFTWARE ENGINEERING
 *      INSTITUTE MATERIAL IS FURNISHED ON AN "AS-IS" BASIS. CARNEGIE MELLON
 *      UNIVERSITY MAKES NO WARRANTIES OF ANY KIND, EITHER EXPRESSED OR IMPLIED,
 *      AS TO ANY MATTER INCLUDING, BUT NOT LIMITED TO, WARRANTY OF FITNESS FOR
 *      PURPOSE OR MERCHANTABILITY, EXCLUSIVITY, OR RESULTS OBTAINED FROM USE OF
 *      THE MATERIAL. CARNEGIE MELLON UNIVERSITY DOES NOT MAKE ANY WARRANTY OF
 *      ANY KIND WITH RESPECT TO FREEDOM FROM PATENT, TRADEMARK, OR COPYRIGHT
 *      INFRINGEMENT.
 *
 *      Released under a GNU GPL 2.0-style license, please see license.txt or
 *      contact permission@sei.cmu.edu for full terms.
 *
 *      [DISTRIBUTION STATEMENT A] This material has been approved for public
 *      release and unlimited distribution.  Please see Copyright notice for
 *      non-US Government use and distribution. Carnegie Mellon® and CERT® are
 *      registered in the U.S. Patent and Trademark Office by Carnegie Mellon
 *      University.
 *
 *      This Software includes and/or makes use of the following Third-Party
 *      Software subject to its own license:
 *      1. wmediumd (https://github.com/bcopeland/wmediumd)
 *              Copyright 2011 cozybit Inc..
 *      2. mac80211_hwsim (https://github.com/torvalds/linux/blob/master/drivers/net/wireless/mac80211_hwsim.c)
 *              Copyright 2008 Jouni Malinen <j@w1.fi>
 *              Copyright (c) 2011, Javier Lopez <jlopex@gmail.com>
 *
 *      DM17-0952
 */

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <winsock2.h>
#include <ws2tcpip.h>

void windows_error(char *msg)
{
	int err = WSAGetLastError();
	switch(err) {
        case WSABASEERR:
                printf("%s: WSABASEERR\n", msg);
                break;
        case WSAEINTR:
                printf("%s: WSAEINTR\n", msg);
                break;
        case WSAEBADF:
                printf("%s: WSAEBADF\n", msg);
                break;
        case WSAEACCES:
                printf("%s: WSAEACCES\n", msg);
                break;
        case WSAEFAULT:
                printf("%s: WSAEFAULT\n", msg);
                break;
        case WSAEINVAL:
                printf("%s: WSAEINVAL\n", msg);
                break;
        case WSAEMFILE:
                printf("%s: WSAEMFILE\n", msg);
                break;
        case WSAEWOULDBLOCK:
                printf("%s: WSAEWOULDBLOCK\n", msg);
                break;
        case WSAEINPROGRESS:
                printf("%s: WSAEINPROGRESS\n", msg);
                break;
        case WSAEALREADY:
                printf("%s: WSAEALREADY\n", msg);
                break;
        case WSAENOTSOCK:
                printf("%s: WSAENOTSOCK\n", msg);
                break;
        case WSAEDESTADDRREQ:
                printf("%s: WSAEDESTADDRREQ\n", msg);
                break;
        case WSAEMSGSIZE:
                printf("%s: WSAEMSGSIZE\n", msg);
                break;
        case WSAEPROTOTYPE:
                printf("%s: WSAEPROTOTYPE\n", msg);
                break;
        case WSAENOPROTOOPT:
                printf("%s: WSAENOPROTOOPT\n", msg);
                break;
        case WSAEPROTONOSUPPORT:
                printf("%s: WSAEPROTONOSUPPORT\n", msg);
                break;
        case WSAESOCKTNOSUPPORT:
                printf("%s: WSAESOCKTNOSUPPORT\n", msg);
                break;
        case WSAEOPNOTSUPP:
                printf("%s: WSAEOPNOTSUPP\n", msg);
                break;
        case WSAEPFNOSUPPORT:
                printf("%s: WSAEPFNOSUPPORT\n", msg);
                break;
        case WSAEAFNOSUPPORT:
                printf("%s: WSAEAFNOSUPPORT\n", msg);
                break;
        case WSAEADDRINUSE:
                printf("%s: WSAEADDRINUSE\n", msg);
                break;
        case WSAEADDRNOTAVAIL:
                printf("%s: WSAEADDRNOTAVAIL\n", msg);
                break;
        case WSAENETDOWN:
                printf("%s: WSAENETDOWN\n", msg);
                break;
        case WSAENETUNREACH:
                printf("%s: WSAENETUNREACH\n", msg);
                break;
        case WSAENETRESET:
                printf("%s: WSAENETRESET\n", msg);
                break;
        case WSAECONNABORTED:
                printf("%s: WSAECONNABORTED\n", msg);
                break;
        case WSAECONNRESET:
                printf("%s: WSAECONNRESET\n", msg);
                break;
        case WSAENOBUFS:
                printf("%s: WSAENOBUFS\n", msg);
                break;
        case WSAEISCONN:
                printf("%s: WSAEISCONN\n", msg);
                break;
        case WSAENOTCONN:
                printf("%s: WSAENOTCONN\n", msg);
                break;
        case WSAESHUTDOWN:
                printf("%s: WSAESHUTDOWN\n", msg);
                break;
        case WSAETOOMANYREFS:
                printf("%s: WSAETOOMANYREFS\n", msg);
                break;
        case WSAETIMEDOUT:
                printf("%s: WSAETIMEDOUT\n", msg);
                break;
        case WSAECONNREFUSED:
                printf("%s: WSAECONNREFUSED\n", msg);
                break;
        case WSAELOOP:
                printf("%s: WSAELOOP\n", msg);
                break;
        case WSAENAMETOOLONG:
                printf("%s: WSAENAMETOOLONG\n", msg);
                break;
        case WSAEHOSTDOWN:
                printf("%s: WSAEHOSTDOWN\n", msg);
                break;
        case WSAEHOSTUNREACH:
                printf("%s: WSAEHOSTUNREACH\n", msg);
                break;
        case WSAENOTEMPTY:
                printf("%s: WSAENOTEMPTY\n", msg);
                break;
        case WSAEPROCLIM:
                printf("%s: WSAEPROCLIM\n", msg);
                break;
        case WSAEUSERS:
                printf("%s: WSAEUSERS\n", msg);
                break;
        case WSAEDQUOT:
                printf("%s: WSAEDQUOT\n", msg);
                break;
        case WSAESTALE:
                printf("%s: WSAESTALE\n", msg);
                break;
        case WSAEREMOTE:
                printf("%s: WSAEREMOTE\n", msg);
                break;
        case WSASYSNOTREADY:
                printf("%s: WSASYSNOTREADY\n", msg);
                break;
        case WSAVERNOTSUPPORTED:
                printf("%s: WSAVERNOTSUPPORTED\n", msg);
                break;
        case WSANOTINITIALISED:
                printf("%s: WSANOTINITIALISED\n", msg);
                break;
        case WSAEDISCON:
                printf("%s: WSAEDISCON\n", msg);
                break;
        case WSAENOMORE:
                printf("%s: WSAENOMORE\n", msg);
                break;
        case WSAECANCELLED:
                printf("%s: WSAECANCELLED\n", msg);
                break;
        case WSAEINVALIDPROCTABLE:
                printf("%s: WSAEINVALIDPROCTABLE\n", msg);
                break;
        case WSAEINVALIDPROVIDER:
                printf("%s: WSAEINVALIDPROVIDER\n", msg);
                break;
        case WSAEPROVIDERFAILEDINIT:
                printf("%s: WSAEPROVIDERFAILEDINIT\n", msg);
                break;
        case WSASYSCALLFAILURE:
                printf("%s: WSASYSCALLFAILURE\n", msg);
                break;
        case WSASERVICE_NOT_FOUND:
                printf("%s: WSASERVICE_NOT_FOUND\n", msg);
                break;
        case WSATYPE_NOT_FOUND:
                printf("%s: WSATYPE_NOT_FOUND\n", msg);
                break;
        case WSA_E_NO_MORE:
                printf("%s: WSA_E_NO_MORE\n", msg);
                break;
        case WSA_E_CANCELLED:
                printf("%s: WSA_E_CANCELLED\n", msg);
                break;
        case WSAEREFUSED:
                printf("%s: WSAEREFUSED\n", msg);
                break;
        case WSAHOST_NOT_FOUND:
                printf("%s: WSAHOST_NOT_FOUND\n", msg);
                break;
        case WSATRY_AGAIN:
                printf("%s: WSATRY_AGAIN\n", msg);
                break;
        case WSANO_RECOVERY:
                printf("%s: WSANO_RECOVERY\n", msg);
                break;
        case WSANO_DATA:
                printf("%s: WSANO_DATA\n", msg);
                break;
        case WSA_QOS_RECEIVERS:
                printf("%s: WSA_QOS_RECEIVERS\n", msg);
                break;
        case WSA_QOS_SENDERS:
                printf("%s: WSA_QOS_SENDERS\n", msg);
                break;
        case WSA_QOS_NO_SENDERS:
                printf("%s: WSA_QOS_NO_SENDERS\n", msg);
                break;
        case WSA_QOS_NO_RECEIVERS:
                printf("%s: WSA_QOS_NO_RECEIVERS\n", msg);
                break;
        case WSA_QOS_REQUEST_CONFIRMED:
                printf("%s: WSA_QOS_REQUEST_CONFIRMED\n", msg);
                break;
        case WSA_QOS_ADMISSION_FAILURE:
                printf("%s: WSA_QOS_ADMISSION_FAILURE\n", msg);
                break;
        case WSA_QOS_POLICY_FAILURE:
                printf("%s: WSA_QOS_POLICY_FAILURE\n", msg);
                break;
        case WSA_QOS_BAD_STYLE:
                printf("%s: WSA_QOS_BAD_STYLE\n", msg);
                break;
        case WSA_QOS_BAD_OBJECT:
                printf("%s: WSA_QOS_BAD_OBJECT\n", msg);
                break;
        case WSA_QOS_TRAFFIC_CTRL_ERROR:
                printf("%s: WSA_QOS_TRAFFIC_CTRL_ERROR\n", msg);
                break;
        case WSA_QOS_GENERIC_ERROR:
                printf("%s: WSA_QOS_GENERIC_ERROR\n", msg);
                break;
        case WSA_QOS_ESERVICETYPE:
                printf("%s: WSA_QOS_ESERVICETYPE\n", msg);
                break;
        case WSA_QOS_EFLOWSPEC:
                printf("%s: WSA_QOS_EFLOWSPEC\n", msg);
                break;
        case WSA_QOS_EPROVSPECBUF:
                printf("%s: WSA_QOS_EPROVSPECBUF\n", msg);
                break;
        case WSA_QOS_EFILTERSTYLE:
                printf("%s: WSA_QOS_EFILTERSTYLE\n", msg);
                break;
        case WSA_QOS_EFILTERTYPE:
                printf("%s: WSA_QOS_EFILTERTYPE\n", msg);
                break;
        case WSA_QOS_EFILTERCOUNT:
                printf("%s: WSA_QOS_EFILTERCOUNT\n", msg);
                break;
        case WSA_QOS_EOBJLENGTH:
                printf("%s: WSA_QOS_EOBJLENGTH\n", msg);
                break;
        case WSA_QOS_EFLOWCOUNT:
                printf("%s: WSA_QOS_EFLOWCOUNT\n", msg);
                break;
        case WSA_QOS_EUNKOWNPSOBJ:
                printf("%s: WSA_QOS_EUNKOWNPSOBJ\n", msg);
                break;
        case WSA_QOS_EPOLICYOBJ:
                printf("%s: WSA_QOS_EPOLICYOBJ\n", msg);
                break;
        case WSA_QOS_EFLOWDESC:
                printf("%s: WSA_QOS_EFLOWDESC\n", msg);
                break;
        case WSA_QOS_EPSFLOWSPEC:
                printf("%s: WSA_QOS_EPSFLOWSPEC\n", msg);
                break;
        case WSA_QOS_EPSFILTERSPEC:
                printf("%s: WSA_QOS_EPSFILTERSPEC\n", msg);
                break;
        case WSA_QOS_ESDMODEOBJ:
                printf("%s: WSA_QOS_ESDMODEOBJ\n", msg);
                break;
        case WSA_QOS_ESHAPERATEOBJ:
                printf("%s: WSA_QOS_ESHAPERATEOBJ\n", msg);
                break;
        case WSA_QOS_RESERVED_PETYPE:
                printf("%s: WSA_QOS_RESERVED_PETYPE\n", msg);
                break;
	}
}
