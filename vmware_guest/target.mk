# 	Copyright 2018 Carnegie Mellon University. All Rights Reserved.
# 
# 	NO WARRANTY. THIS CARNEGIE MELLON UNIVERSITY AND SOFTWARE ENGINEERING
# 	INSTITUTE MATERIAL IS FURNISHED ON AN "AS-IS" BASIS. CARNEGIE MELLON
# 	UNIVERSITY MAKES NO WARRANTIES OF ANY KIND, EITHER EXPRESSED OR IMPLIED,
# 	AS TO ANY MATTER INCLUDING, BUT NOT LIMITED TO, WARRANTY OF FITNESS FOR
# 	PURPOSE OR MERCHANTABILITY, EXCLUSIVITY, OR RESULTS OBTAINED FROM USE OF
# 	THE MATERIAL. CARNEGIE MELLON UNIVERSITY DOES NOT MAKE ANY WARRANTY OF
# 	ANY KIND WITH RESPECT TO FREEDOM FROM PATENT, TRADEMARK, OR COPYRIGHT
# 	INFRINGEMENT.
# 
# 	Released under a GNU GPL 2.0-style license, please see license.txt or
# 	contact permission@sei.cmu.edu for full terms.
# 
# 	[DISTRIBUTION STATEMENT A] This material has been approved for public
# 	release and unlimited distribution.  Please see Copyright notice for
# 	non-US Government use and distribution. Carnegie Mellon® and CERT® are
# 	registered in the U.S. Patent and Trademark Office by Carnegie Mellon
# 	University.
# 
# 	This Software includes and/or makes use of the following Third-Party
# 	Software subject to its own license:
# 	1. wmediumd (https://github.com/bcopeland/wmediumd)
# 		Copyright 2011 cozybit Inc..
# 	2. mac80211_hwsim (https://github.com/torvalds/linux/blob/master/drivers/net/wireless/mac80211_hwsim.c)
# 		Copyright 2008 Jouni Malinen <j@w1.fi>
# 		Copyright (c) 2011, Javier Lopez <jlopex@gmail.com> 
#  
# 	DM17-0952

BOARDNAME:=VMWare Guest
FEATURES:=ext4 pci usb
DEFAULT_PACKAGES += kmod-acpi-button kmod-button-hotplug kmod-vmw_vmci kmod-vsock kmod-vmw_vsock_vmci_transport kmod-fs-isofs kmod-scsi-cdrom
CPU_TYPE := i486
