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

#under Device Drivers, Misc devices on Linux
define KernelPackage/vmw_vmci
  SUBMENU:=$(VIRTUAL_MENU)
  TITLE:=VMCI
  DEPENDS:=@(TARGET_x86_64||TARGET_x86_vmware_guest)
  KCONFIG:=CONFIG_VMWARE_VMCI=m
  FILES:=$(LINUX_DIR)/drivers/misc/vmw_vmci/vmw_vmci.ko
  AUTOLOAD:=$(call AutoLoad,51,vmw_vmci)
endef

define KernelPackage/vmw_vmci/description
  VMCI driver
endef

$(eval $(call KernelPackage,vmw_vmci))

#under Networking Support, Networking Options on Linux
define KernelPackage/vsock
  SUBMENU:=$(VIRTUAL_MENU)
  TITLE:=VSOCK
  DEPENDS:=@(TARGET_x86_64||TARGET_x86_vmware_guest)
  KCONFIG:=CONFIG_VSOCKETS=m
  FILES:=$(LINUX_DIR)/net/vmw_vsock/vsock.ko
  AUTOLOAD:=$(call AutoLoad,52,vsock)
endef

define KernelPackage/vmw_vsock/description
  VMWare VSOCK
endef

$(eval $(call KernelPackage,vsock))

#under Networking Support, Networking Options on Linux
define KernelPackage/vmw_vsock_vmci_transport
  SUBMENU:=$(VIRTUAL_MENU)
  TITLE:=VSOCK VMCI Transport
  DEPENDS:=@(TARGET_x86_64||TARGET_x86_vmware_guest) +kmod-vmw_vmci +kmod-vsock
  KCONFIG:=CONFIG_VMWARE_VMCI_VSOCKETS=m
  FILES:=$(LINUX_DIR)/net/vmw_vsock/vmw_vsock_vmci_transport.ko
  AUTOLOAD:=$(call AutoLoad,53,vmw_vsock_vmci_transport)
endef

define KernelPackage/vmw_vsock_vmci_transport/description
  VMWare VSOCK transport driver
endef

$(eval $(call KernelPackage,vmw_vsock_vmci_transport))

