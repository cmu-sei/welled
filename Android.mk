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

LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE_TAGS := eng debug
LOCAL_MODULE := welled
welled_version := 3.0.0
VERSION_STR := $(welled_version)
LOCAL_SRC_FILES := \
	src/welled.c \
	src/nodes.c
LOCAL_C_INCLUDES := external/libnl/include/
LOCAL_LDLIBS := -lnl
LOCAL_CFLAGS := -DVERSION_STR=\"$(VERSION_STR)\"  -D_ANDROID
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE_TAGS := eng debug
LOCAL_MODULE := gelled-ctrl
welled_version := 2.2.1
VERSION_STR := $(welled_version)
LOCAL_SRC_FILES := src/gelled-ctrl.c
LOCAL_CFLAGS := -DVERSION_STR=\"$(VERSION_STR)\"  -D_ANDROID
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := init.welled.rc
LOCAL_MODULE_CLASS := ETC
LOCAL_SRC_FILES := android/init.welled-$(TARGET_ARCH).rc
LOCAL_MODULE_TAGS := eng
LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT)
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := wpa_supplicant.conf
LOCAL_MODULE_CLASS := ETC
LOCAL_SRC_FILES := android/$(LOCAL_MODULE)
LOCAL_MODULE_TAGS := eng
LOCAL_MODULE_PATH := $(TARGET_OUT)/etc/wifi/
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := welled_prep
LOCAL_MODULE_CLASS := ETC
LOCAL_SRC_FILES := android/$(LOCAL_MODULE)
LOCAL_MODULE_TAGS := eng
LOCAL_MODULE_PATH := $(TARGET_OUT)/bin/
include $(BUILD_PREBUILT)

# begin extra packages
include $(CLEAR_VARS)
LOCAL_MODULE := bc24da93.0
LOCAL_MODULE_CLASS := ETC
LOCAL_SRC_FILES := android/certs/$(LOCAL_MODULE)
LOCAL_MODULE_TAGS := eng
LOCAL_MODULE_PATH := $(TARGET_OUT)/etc/security/cacerts/
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := exercise_prep
LOCAL_MODULE_CLASS := ETC
LOCAL_SRC_FILES := android/$(LOCAL_MODULE)
LOCAL_MODULE_TAGS := eng
LOCAL_MODULE_PATH := $(TARGET_OUT)/bin/
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := step-startup.sh
LOCAL_MODULE_CLASS := ETC
LOCAL_SRC_FILES := android/$(LOCAL_MODULE)
LOCAL_MODULE_TAGS := eng
LOCAL_MODULE_PATH := $(TARGET_OUT)/bin/
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := init.step-startup.rc
LOCAL_MODULE_CLASS := ETC
LOCAL_SRC_FILES := android/$(LOCAL_MODULE)
LOCAL_MODULE_TAGS := eng
include $(BUILD_PREBUILT)

# for m -j12 iso
iso_img: welled

# for m welled and mm
welled: welled-binaries welled-depends welled-files init-rc-update

welled-binaries:
	test -d external/welled/dist/$(TARGET_PRODUCT)/ || mkdir external/welled/dist/$(TARGET_PRODUCT)/
	cp $(TARGET_OUT)/bin/welled external/welled/dist/$(TARGET_PRODUCT)/
	cp $(TARGET_OUT)/bin/gelled-ctrl external/welled/dist/$(TARGET_PRODUCT)/
	cp $(TARGET_OUT)/bin/welled_prep external/welled/dist/$(TARGET_PRODUCT)/

welled-depends:
	cp out/target/product/$(TARGET_ARCH)/obj/kernel/drivers/misc/vmw_vmci/vmw_vmci.ko external/welled/dist/$(TARGET_PRODUCT)/
	cp out/target/product/$(TARGET_ARCH)/obj/kernel/net/vmw_vsock/vsock.ko external/welled/dist/$(TARGET_PRODUCT)/
	cp out/target/product/$(TARGET_ARCH)/obj/kernel/net/vmw_vsock/vmw_vsock_virtio_transport_common.ko external/welled/dist/$(TARGET_PRODUCT)/
	cp out/target/product/$(TARGET_ARCH)/obj/kernel/net/vmw_vsock/vmw_vsock_virtio_transport.ko external/welled/dist/$(TARGET_PRODUCT)/
	cp out/target/product/$(TARGET_ARCH)/obj/kernel/net/vmw_vsock/vmw_vsock_vmci_transport.ko external/welled/dist/$(TARGET_PRODUCT)/

welled-files:
	cp $(TARGET_ROOT_OUT)/init.welled.rc external/welled/dist/$(TARGET_PRODUCT)/init.welled-$(TARGET_ARCH).rc

init-rc-update:
	echo "import /init.welled.rc" >> $(TARGET_ROOT_OUT)/init.rc

