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

VERSION_STR="\"3.0.0\""
VERSION=3.0.0

# check whether we are cross compiling for openwrt
ifdef CROSS
OUTDIR := .
MAN := man/
else
OUTDIR := ../bin
MAN := ../man
endif

# define the compile and linker for each target
wmasterd-i686-w64-mingw32: CC=i686-w64-mingw32-gcc
wmasterd-i686-w64-mingw32: LD=i686-w64-mingw32-ld
wmasterd-x86_64-w64-mingw32: CC=x86_64-w64-mingw32-gcc
wmasterd-x86_64-w64-mingw32: LD=x86_64-w64-mingw32-ld
gelled-i686-w64-mingw32: CC=i686-w64-mingw32-gcc
gelled-i686-w64-mingw32: LD=i686-w64-mingw32-ld
gelled-x86_64-w64-mingw32: CC=x86_64-w64-mingw32-gcc
gelled-x86_64-w64-mingw32: LD=x86_64-w64-mingw32-ld
gelled-ctrl-i686-w64-mingw32: CC=i686-w64-mingw32-gcc
gelled-ctrl-i686-w64-mingw32: LD=i686-w64-mingw32-ld
gelled-ctrl-x86_64-w64-mingw32: CC=x86_64-w64-mingw32-gcc
gelled-ctrl-x86_64-w64-mingw32: LD=x86_64-w64-mingw32-ld
rpm-x86_64-Linux: ARCH=x86_64
rpm-i386-Linux: ARCH=i386
debian-x86_64-Linux: ARCH=amd64
debian-i386-Linux: ARCH=i386

# on kali, install:
# libnl-genl-3-dev libnl-3-dev libnl-genl-3 libnl-route-3-dev

# TODO: remove this libnl stuff
# Look for libnl libraries
PKG_CONFIG ?= pkg-config
NL3FOUND := $(shell $(PKG_CONFIG) --atleast-version=3 libnl-3.0 && echo Y)
NL31FOUND := $(shell $(PKG_CONFIG) --exact-version=3.1 libnl-3.1 && echo Y)
NL3xFOUND := $(shell $(PKG_CONFIG) --atleast-version=3.2 libnl-3.0 && echo Y)

CFLAGS = -g -Wall -O2 -Wformat -Wformat-security
LDFLAGS =

ifeq ($(NL3xFOUND),Y)
# libnl 3.2 might be found as 3.2 and 3.0
NL3FOUND = N
CFLAGS += -DCONFIG_LIBNL30
LDFLAGS += -lnl-genl-3
NLLIBNAME = libnl-3.0
endif

ifeq ($(NL3FOUND),Y)
CFLAGS += -DCONFIG_LIBNL30
LDFLAGS += -lnl-genl
NLLIBNAME = libnl-3.0
endif

# nl-3.1 has a broken libnl-gnl-3.1.pc file
# as show by pkg-config --debug --libs --cflags --exact-version=3.1 libnl-genl-3.1;echo $?
ifeq ($(NL31FOUND),Y)
CFLAGS += -DCONFIG_LIBNL30
LDFLAGS += -lnl-genl
NLLIBNAME = libnl-3.1
endif

ifeq ($(NLLIBNAME),)
$(warning Cannot find development files for any supported version of libnl)
endif

LDFLAGS += $(shell $(PKG_CONFIG) --libs $(NLLIBNAME))
CFLAGS += $(shell $(PKG_CONFIG) --cflags $(NLLIBNAME))
NLVERSION :=$(shell $(PKG_CONFIG) --print-provides $(NLLIBNAME))

CFLAGS+=-DVERSION_STR=$(VERSION_STR)

DISTRO := $(shell if [ -f "/etc/redhat-release" ]; then echo rhel; elif [ -f "/etc/debian_version" ]; then echo debian; fi)

LDFLAGS += -lpthread

.PHONY: all wireless gps openwrt dist clean doc esx source install-wireless install-gps uninstall-wireless uninstall-gps install-driver uninstall-driver rpm debian

# does not include openwrt
all: esx linux windows vyos offline-bundle

linux: x86_64-Linux #i386-Linux

i386-Linux: welled-i386-Linux gelled-i386-Linux gelled-ctrl-i386-Linux wmasterd-i386-Linux

x86_64-Linux: welled gelled gelled-ctrl wmasterd

# vyos 1.1.7 differs due to older libc version
x86_64-Linux-vyos: welled-x86_64-Linux-vyos gelled-ctrl-x86_64-Linux-vyos

windows: i686-w64-mingw32 x86_64-w64-mingw32

i686-w64-mingw32: gelled-i686-w64-mingw32 gelled-ctrl-i686-w64-mingw32 wmasterd-i686-w64-mingw32

x86_64-w64-mingw32: gelled-x86_64-w64-mingw32 gelled-ctrl-x86_64-w64-mingw32 wmasterd-x86_64-w64-mingw32

# called from the openwrt build system
openwrt: welled-$(CROSS:-=) gelled-ctrl-$(CROSS:-=)
	test -d ../../../package/welled/dist/$(CROSS:-=)/ || mkdir ../../../package/welled/dist/$(CROSS:-=)/
	cp $(OUTDIR)/welled ../../../package/welled/dist/$(CROSS:-=)/
	cp $(OUTDIR)/gelled-ctrl ../../../package/welled/dist/$(CROSS:-=)/
	if [ -f ../../../bin/x86/packages/base/welled_$(VERSION)_$(ARCH).ipk ]; then cp ../../../bin/x86/packages/base/welled_$(VERSION)_$(ARCH).ipk ../../../package/welled/dist/$(CROSS:-=)/; cp ../../../bin/x86/packages/base/welled_$(VERSION)_$(ARCH).ipk ../../../package/welled/dist/$(CROSS:-=)/welled_$(VERSION)_$(CROSS:-=).ipk; fi
	if [ -f ../../../bin/x86/packages/base/kmod-mac80211-hwsim*ipk ]; then cp ../../../bin/x86/packages/base/kmod-mac80211-hwsim*ipk ../../../package/welled/dist/$(CROSS:-=)/; fi
	if [ -f ../../../bin/x86/packages/kernel/kmod-mac80211-hwsim*ipk ]; then cp ../../../bin/x86/packages/kernel/kmod-mac80211-hwsim*ipk ../../../package/welled/dist/$(CROSS:-=)/; fi

# default is 64 bit
welled:
	test -d $(OUTDIR) || mkdir $(OUTDIR)
	$(CC) welled.c -c $(LDFLAGS) $(CFLAGS)
	$(CC) nodes.c -c $(LDFLAGS) $(CFLAGS)
	$(CC) -o $(OUTDIR)/welled welled.o nodes.o $(LDFLAGS) -lnl-route-3 -lm
	test -d ../dist/x86_64-Linux || mkdir ../dist/x86_64-Linux/
	cp $(OUTDIR)/welled ../dist/x86_64-Linux/

welled-x86_64-Linux-vyos:
	test -d $(OUTDIR) || mkdir $(OUTDIR)
	$(CC) welled.c -c $(LDFLAGS) $(CFLAGS) -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0
	$(CC) nodes.c -c $(LDFLAGS) $(CFLAGS)
	$(CC) -o $(OUTDIR)/welled welled.o nodes.o $(LDFLAGS) -lnl-route-3 -lm -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0
	test -d ../dist/x86_64-Linux-vyos || mkdir ../dist/x86_64-Linux-vyos/
	cp $(OUTDIR)/welled ../dist/x86_64-Linux-vyos/

welled-i386-Linux:
	test -d $(OUTDIR) || mkdir $(OUTDIR)
	$(CC) welled.c -c $(LDFLAGS) -m32 $(CFLAGS)
	$(CC) nodes.c -c $(LDFLAGS) -m32 $(CFLAGS)
	$(CC) -o $(OUTDIR)/welled welled.o nodes.o $(LDFLAGS) -lnl-route-3 -lm -m32
	test -d ../dist/i386-Linux/ || mkdir ../dist/i386-Linux/
	cp $(OUTDIR)/welled ../dist/i386-Linux/

welled-x86_64-openwrt-linux-uclibc:
	test -d $(OUTDIR) || mkdir $(OUTDIR)
	$(CC) welled.c -c $(LDFLAGS) $(CFLAGS) -DAF_VSOCK=40 -D_OPENWRT
	$(CC) nodes.c -c $(LDFLAGS) $(CFLAGS)
	$(CC) -o $(OUTDIR)/welled welled.o nodes.o $(LDFLAGS) -lnl-route-3 -lm

welled-i486-openwrt-linux-uclibc:
	test -d $(OUTDIR) || mkdir $(OUTDIR)
	$(CC) welled.c -c $(LDFLAGS) $(CFLAGS) -DAF_VSOCK=40 -D_OPENWRT
	$(CC) nodes.c -c $(LDFLAGS) $(CFLAGS)
	$(CC) -o $(OUTDIR)/welled welled.o nodes.o $(LDFLAGS) -lnl-route-3 -lm

welled-i486-openwrt-linux-musl:
	test -d $(OUTDIR) || mkdir $(OUTDIR)
	$(CC) welled.c -c $(LDFLAGS) $(CFLAGS) -DAF_VSOCK=40 -D_OPENWRT
	$(CC) nodes.c -c $(LDFLAGS) $(CFLAGS)
	$(CC) -o $(OUTDIR)/welled welled.o nodes.o $(LDFLAGS) -lnl-route-3 -lm

welled-x86_64-openwrt-linux-musl:
	test -d $(OUTDIR) || mkdir $(OUTDIR)
	$(CC) welled.c -c $(LDFLAGS) $(CFLAGS) -DAF_VSOCK=40 -D_OPENWRT
	$(CC) nodes.c -c $(LDFLAGS) $(CFLAGS)
	$(CC) -o $(OUTDIR)/welled welled.o nodes.o $(LDFLAGS) -lnl-route-3 -lm

# default is 64 bit
gelled-gui:
	test -d $(OUTDIR) || mkdir $(OUTDIR)
	$(CC) -o $(OUTDIR)/$@ $@.c $(CFLAGS) `pkg-config --cflags --libs osmgpsmap-1.0 gtk+-3.0` -lpthread -lm -lgps
	test -d ../dist/x86_64-Linux || mkdir ../dist/x86_64-Linux
	cp $(OUTDIR)/gelled-gui ../dist/x86_64-Linux/


# default is 64 bit
gelled-ctrl:
	test -d $(OUTDIR) || mkdir $(OUTDIR)
	$(CC) -o $(OUTDIR)/gelled-ctrl gelled-ctrl.c -DVERSION_STR=$(VERSION_STR) `pkg-config --cflags --libs glib-2.0`
	test -d ../dist/x86_64-Linux || mkdir ../dist/x86_64-Linux
	cp $(OUTDIR)/gelled-ctrl ../dist/x86_64-Linux/

gelled-ctrl-x86_64-Linux-vyos:
	test -d $(OUTDIR) || mkdir $(OUTDIR)
	$(CC) -o $(OUTDIR)/gelled-ctrl gelled-ctrl.c -DVERSION_STR=$(VERSION_STR) `pkg-config --cflags --libs glib-2.0` -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0
	test -d ../dist/x86_64-Linux || mkdir ../dist/x86_64-Linux
	cp $(OUTDIR)/gelled-ctrl ../dist/x86_64-Linux/

gelled-ctrl-i386-Linux:
	test -d $(OUTDIR) || mkdir $(OUTDIR)
	$(CC) -o $(OUTDIR)/gelled-ctrl gelled-ctrl.c -DVERSION_STR=$(VERSION_STR) -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include -lglib-2.0 -m32
	test -d ../dist/i386-Linux || mkdir ../dist/i386-Linux
	cp $(OUTDIR)/gelled-ctrl ../dist/i386-Linux/

gelled-ctrl-x86_64-openwrt-linux-uclibc:
	test -d $(OUTDIR) || mkdir $(OUTDIR)
	$(CC) -o $(OUTDIR)/gelled-ctrl gelled-ctrl.c -DVERSION_STR=$(VERSION_STR) `pkg-config --cflags --libs glib-2.0`

gelled-ctrl-i486-openwrt-linux-uclibc:
	test -d $(OUTDIR) || mkdir $(OUTDIR)
	$(CC) -o $(OUTDIR)/gelled-ctrl gelled-ctrl.c -DVERSION_STR=$(VERSION_STR) `pkg-config --cflags --libs glib-2.0`

gelled-ctrl-i486-openwrt-linux-musl:
	test -d $(OUTDIR) || mkdir $(OUTDIR)
	$(CC) -o $(OUTDIR)/gelled-ctrl gelled-ctrl.c -DVERSION_STR=$(VERSION_STR) `pkg-config --cflags --libs glib-2.0`

gelled-ctrl-x86_64-openwrt-linux-musl:
	test -d $(OUTDIR) || mkdir $(OUTDIR)
	$(CC) -o $(OUTDIR)/gelled-ctrl gelled-ctrl.c -DVERSION_STR=$(VERSION_STR) `pkg-config --cflags --libs glib-2.0`

gelled-ctrl-i686-w64-mingw32:
	$(CC) -o $(OUTDIR)/$@ gelled-ctrl.c -lws2_32 -DVERSION_STR=$(VERSION_STR) -g -D_POSIX -static -I/usr/i686-w64-mingw32/sys-root/mingw/include/glib-2.0/ -I/usr/i686-w64-mingw32/sys-root/mingw/lib/glib-2.0/include/ -lglib-2.0 /usr/i686-w64-mingw32/sys-root/mingw/lib/libws2_32.a -lintl -lole32 -lwinmm
	test -d ../dist/i686-w64-mingw32/ || mkdir ../dist/i686-w64-mingw32/
	cp $(OUTDIR)/gelled-ctrl-i686-w64-mingw32 ../dist/i686-w64-mingw32/

gelled-ctrl-x86_64-w64-mingw32:
	$(CC) -o $(OUTDIR)/$@ gelled-ctrl.c -I/usr/x86_64-w64-mingw32/sys-root/mingw/lib/glib-2.0/include/ -I/usr/x86_64-w64-mingw32/sys-root/mingw/include/glib-2.0/ -I/usr/x86_64-w64-mingw32/sys-root/mingw/include/glib-2.0/glib/ -lws2_32 -DVERSION_STR=$(VERSION_STR) -g -D_POSIX -static -lglib-2.0 /usr/x86_64-w64-mingw32/sys-root/mingw/lib/libws2_32.a -lintl -lole32 -lwinmm
	test -d ../dist/x86_64-w64-mingw32/ || mkdir ../dist/x86_64-w64-mingw32/
	cp $(OUTDIR)/gelled-ctrl-x86_64-w64-mingw32 ../dist/x86_64-w64-mingw32/

# default is 64 bit
gelled:
	test -d $(OUTDIR) || mkdir $(OUTDIR)
	$(CC) -o $(OUTDIR)/gelled gelled.c -DVERSION_STR=$(VERSION_STR) -lpthread -lm -lpng -lcurl
	test -d ../dist/x86_64-Linux || mkdir ../dist/x86_64-Linux
	cp $(OUTDIR)/gelled ../dist/x86_64-Linux/

gelled-i386-Linux:
	test -d $(OUTDIR) || mkdir $(OUTDIR)
	$(CC) -o $(OUTDIR)/gelled gelled.c -DVERSION_STR=$(VERSION_STR) -lpthread -lm -lpng -lcurl -m32
	test -d ../dist/i386-Linux || mkdir ../dist/i386-Linux
	cp $(OUTDIR)/gelled ../dist/i386-Linux/

gelled-x86_64-w64-mingw32:
	$(CC) -o $(OUTDIR)/$@ gelled.c -DVERSION_STR=$(VERSION_STR) -g -D_POSIX -DCURL_STATICLIB -static -lm -lpng -lcurl -lz -lssl -lcrypto -lgdi32 -lssh2 -lwldap32 -lidn -liconv -lintl /usr/x86_64-w64-mingw32/sys-root/mingw/lib/libws2_32.a
	test -d ../dist/x86_64-w64-mingw32/ || mkdir ../dist/x86_64-w64-mingw32/
	cp $(OUTDIR)/gelled-x86_64-w64-mingw32 ../dist/x86_64-w64-mingw32/

gelled-i686-w64-mingw32:
	$(CC) -o $(OUTDIR)/$@ gelled.c -DVERSION_STR=$(VERSION_STR) -g -D_POSIX -DCURL_STATICLIB -static -lm -lpng -lcurl -lz -lssl -lcrypto -lgdi32 -lssh2 -lwldap32 -lidn -liconv -lintl /usr/i686-w64-mingw32/sys-root/mingw/lib/libws2_32.a
	test -d ../dist/i686-w64-mingw32/ || mkdir ../dist/i686-w64-mingw32/
	cp $(OUTDIR)/gelled-i686-w64-mingw32 ../dist/i686-w64-mingw32/

# default is 64 bit
wmasterd:
	test -d $(OUTDIR) || mkdir $(OUTDIR)
	$(CC) -o $(OUTDIR)/wmasterd wmasterd.c $(CFLAGS) -lm -lpthread
	test -d ../dist/x86_64-Linux || mkdir ../dist/x86_64-Linux
	cp $(OUTDIR)/wmasterd ../dist/x86_64-Linux/

# differs because of older libc version
wmasterd-esx:
	test -d $(OUTDIR) || mkdir $(OUTDIR)
	$(CC) -o $(OUTDIR)/wmasterd wmasterd.c $(CFLAGS) -D_ESX -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -lm -lpthread
	test -d ../dist/esx || mkdir ../dist/esx
	cp $(OUTDIR)/wmasterd ../dist/esx/

wmasterd-i386-Linux:
	test -d $(OUTDIR) || mkdir $(OUTDIR)
	$(CC) -o $(OUTDIR)/wmasterd wmasterd.c $(CFLAGS) -lm -lpthread -m32
	test -d ../dist/i386-Linux || mkdir ../dist/i386-Linux
	cp $(OUTDIR)/wmasterd ../dist/i386-Linux/

wmasterd-x86_64-w64-mingw32:
	$(CC) -o $(OUTDIR)/wmasterd-x86_64-w64-mingw32 wmasterd.c windows_error.c -L/usr/lib/gcc/x86_64-w64-mingw32/ -lws2_32 -DVERSION_STR=$(VERSION_STR) -g -D_POSIX -lpthread -static
	test -d ../dist/x86_64-w64-mingw32/ || mkdir ../dist/x86_64-w64-mingw32/
	cp $(OUTDIR)/wmasterd-x86_64-w64-mingw32* ../dist/x86_64-w64-mingw32/

wmasterd-i686-w64-mingw32:
	$(CC) -o $(OUTDIR)/wmasterd-i686-w64-mingw32 wmasterd.c windows_error.c -L/usr/lib/gcc/i686-w64-mingw32/ -lws2_32 -DVERSION_STR=$(VERSION_STR) -g -D_POSIX -lpthread -static
	test -d ../dist/i686-w64-mingw32/ || mkdir ../dist/i686-w64-mingw32/
	cp $(OUTDIR)/wmasterd-i686-w64-mingw32* ../dist/i686-w64-mingw32/

clean:
	rm -f *.o $(OUTDIR)/* welled-*.deb ../wmasterd.tgz
	rm -rf ../html ../latex welled-*-vyos/ welled-*/ ../rpmbuild

source: clean
	cd ../..; tar --exclude=.git --exclude=dist --exclude=drivers -cjvf welled_$(VERSION)_src.tar.bz2 welled/; cd -

zip: clean
	rm ../../welled_$(VERSION)_src.zip
	cd ../..; zip --exclude=welled/.git/* --exclude=welled/dist/* --exclude=welled/drivers/* -r welled_$(VERSION)_src.zip welled/; cd -

# package esx for easier installation
esx: wmasterd-esx
	cd ..; tar --owner=0 --group=0 -czvf wmasterd.tgz bin/wmasterd etc/; cd -
	cp ../scripts/install-wmasterd-esx.sh ../install-wmasterd-esx.sh
	tar --exclude=.git -cjvf ../../wmasterd_$(VERSION)_esx.tar.bz2 ../wmasterd.tgz ../install-wmasterd-esx.sh
	rm ../install-wmasterd-esx.sh
	test -d ../dist/esx || mkdir ../dist/esx
	cp ../../wmasterd_$(VERSION)_esx.tar.bz2 ../dist/esx/
	rm ../wmasterd.tgz

vib0: wmasterd-esx
	mkdir -p ../usr/lib/vmware/wmasterd/bin/
	cp ../bin/wmasterd ../usr/lib/vmware/wmasterd/bin/wmasterd
	chmod 775 ../usr/lib/vmware/wmasterd/bin/wmasterd
	cd ..; tar --owner=0 --group=0 -cvf stage.vtar usr/ etc/; cd -
	cd ..; gzip -c stage.vtar > wmasterd; cd -

#TODO release-date needed by ./descriptor.xml and ./vibs/wmasterd-$(VERSION)-$(DATE).xml
vib: RELEASEDATE=$(shell date -u +%FT%T.000000%:z)
vib: vib0
	test -d VIB/ || rm -rf VIB/
	cp -pr ../VIB .
	mkdir -p VIB/vib20/wmasterd/
	cp ../metadata/descriptor.tmp ../descriptor.xml
	sed -i "s/VERSION/$(VERSION)/g" ../descriptor.xml
	sed -i "s/SIZE/$(shell stat --format %s ../stage.vtar)/" ../descriptor.xml
	sed -i "s/SHA256/$(shell sha256sum ../wmasterd | cut -f 1 -d ' ')/" ../descriptor.xml
	sed -i "s/SHA1/$(shell sha1sum ../stage.vtar | cut -f 1 -d ' ')/" ../descriptor.xml
	sed -i "s/RELEASEDATE/$(RELEASEDATE)/" ../descriptor.xml
	cd ..; ar r CMU_bootbank_wmasterd_$(VERSION).vib descriptor.xml sig.pkcs7 wmasterd; cd -
	cp ../CMU_bootbank_wmasterd_$(VERSION).vib ../../
	cp ../CMU_bootbank_wmasterd_$(VERSION).vib ../dist/esx/
	mv ../CMU_bootbank_wmasterd_$(VERSION).vib VIB/vib20/wmasterd/
	rm ../descriptor.xml
	cp ../metadata/vibs/wmasterd.xml ../metadata/vibs/wmasterd.tmp
	sed -i "s/RELEASEDATE/$(RELEASEDATE)/" ../metadata/vibs/wmasterd.tmp


offline-bundle: RELEASEDATE=$(shell date -u +%FT%T.000000%:z)
offline-bundle: DATE=$(shell date +%s)
offline-bundle: vib
	cp ../metadata/bulletins/wmasterd.xml ../metadata/bulletins/wmasterd-$(VERSION).xml
	sed -i "s/VERSION/$(VERSION)/g" ../metadata/bulletins/wmasterd-$(VERSION).xml
	sed -i "s/RELEASEDATE/$(RELEASEDATE)/" ../metadata/bulletins/wmasterd-$(VERSION).xml
	mv ../metadata/vibs/wmasterd.tmp ../metadata/vibs/wmasterd-$(VERSION)-$(DATE).xml
	sed -i "s/VERSION/$(VERSION)/g" ../metadata/vibs/wmasterd-$(VERSION)-$(DATE).xml
	sed -i "s/VIBSIZE/$(shell stat --format %s VIB/vib20/wmasterd/CMU_bootbank_wmasterd_$(VERSION).vib)/g" ../metadata/vibs/wmasterd-$(VERSION)-$(DATE).xml
	sed -i "s/VIBSHA256/$(shell sha256sum VIB/vib20/wmasterd/CMU_bootbank_wmasterd_$(VERSION).vib | cut -f 1 -d ' ')/g" ../metadata/vibs/wmasterd-$(VERSION)-$(DATE).xml
	sed -i "s/SIZE/$(shell stat --format %s ../stage.vtar)/" ../metadata/vibs/wmasterd-$(VERSION)-$(DATE).xml
	sed -i "s/SHA256/$(shell sha256sum ../wmasterd | cut -f 1 -d ' ')/" ../metadata/vibs/wmasterd-$(VERSION)-$(DATE).xml
	sed -i "s/SHA1/$(shell sha1sum ../stage.vtar | cut -f 1 -d ' ')/" ../metadata/vibs/wmasterd-$(VERSION)-$(DATE).xml
	cp ../metadata/vmware.tmp ../metadata/vmware.xml
	sed -i "s/RELEASEDATE/$(RELEASEDATE)/" ../metadata/vmware.xml
	sed -i "s/VERSION/$(VERSION)/g" ../metadata/vmware.xml
	sed -i "s/VIBSIZE/$(shell stat --format %s VIB/vib20/wmasterd/CMU_bootbank_wmasterd_$(VERSION).vib)/g" ../metadata/vmware.xml
	sed -i "s/VIBSHA256/$(shell sha256sum VIB/vib20/wmasterd/CMU_bootbank_wmasterd_$(VERSION).vib | cut -f 1 -d ' ')/g" ../metadata/vmware.xml
	cd ../metadata; zip -r ../metadata.zip vmware.xml vendor-index.xml bulletins/wmasterd-$(VERSION).xml vibs/wmasterd-$(VERSION)-$(DATE).xml; cd -
	mv ../metadata.zip VIB/
	cd VIB/; zip -r ../../../CMU-wmasterd-$(VERSION)-offline_bundle.zip index.xml vendor-index.xml metadata.zip vib20/; cd -
	cp ../../CMU-wmasterd-$(VERSION)-offline_bundle.zip ../dist/esx/
	rm -rf VIB ../wmasterd ../metadata/vmware.xml ../metadata/vibs/wmasterd-$(VERSION)-$(DATE).xml ../metadata/bulletins/wmasterd-$(VERSION).xml ../stage.vtar

doc:
	cd ..; doxygen Doxyfile; cd -

man: welled.8 wmasterd.8 gelled-ctrl.8 gelled.8

welled.8: welled
	test -d $(MAN) || mkdir $(MAN)
	help2man -N -s 8 -o $(MAN)/$@ -I $(MAN)/welled.in -n "emulate wireless networks" $(OUTDIR)/welled
	gzip -k -f $(MAN)/welled.8

wmasterd.8: wmasterd
	test -d $(MAN) || mkdir $(MAN)
	help2man -N -s 8 -o $(MAN)/$@ -I $(MAN)/wmasterd.in -n "wireless master daemon" $(OUTDIR)/wmasterd
	gzip -k -f $(MAN)/wmasterd.8

gelled-ctrl.8: gelled-ctrl
	test -d $(MAN) || mkdir $(MAN)
	help2man -N -s 8 -o $(MAN)/$@ -I $(MAN)/gelled-ctrl.in -n "control gps emulation" $(OUTDIR)/gelled-ctrl
	gzip -k -f $(MAN)/$@

gelled.8: gelled
	test -d $(MAN) || mkdir $(MAN)
	help2man -N -s 8 -o $(MAN)/$@ -I $(MAN)/gelled.in -n "emulate GPS data stream" $(OUTDIR)/gelled
	gzip -k -f $(MAN)/$@

dist: doc all

	cd ../..; tar --exclude=.git --exclude=android/packages -cjvf welled_$(VERSION)_dist.tar.bz2 welled/; cd -

debian:
	mkdir -p welled-$(VERSION)/DEBIAN/
	mkdir -p welled-$(VERSION)/etc/
	mkdir -p welled-$(VERSION)/bin/
	mkdir -p welled-$(VERSION)/lib/systemd/system/
	mkdir -p welled-$(VERSION)/usr/share/man/man8/
	cp ../DEBIAN/control  welled-$(VERSION)/DEBIAN/
	sed -i "s/2.2.1/$(VERSION)/"  welled-$(VERSION)/DEBIAN/control
	sed -i "s/Architecture: all/Architecture: $(ARCH)/"  welled-$(VERSION)/DEBIAN/control
	cp $(OUTDIR)/welled welled-$(VERSION)/bin/
	cp $(OUTDIR)/gelled welled-$(VERSION)/bin/
	cp $(OUTDIR)/gelled-ctrl welled-$(VERSION)/bin/
	cp ../scripts/welled.path welled-$(VERSION)/lib/systemd/system/
	cp ../scripts/welled.service welled-$(VERSION)/lib/systemd/system/
	cp ../scripts/gelled.service welled-$(VERSION)/lib/systemd/system/gelled.service
	cp ../scripts/gelled.path welled-$(VERSION)/lib/systemd/system/gelled.path
	cp ../doc/welled.conf welled-$(VERSION)/etc/
	cp ../man/welled.8.gz welled-$(VERSION)/usr/share/man/man8/
	cp ../man/gelled.8.gz welled-$(VERSION)/usr/share/man/man8/
	cp ../man/gelled-ctrl.8.gz welled-$(VERSION)/usr/share/man/man8/
	dpkg -b welled-$(VERSION)
	rm -rf welled-$(VERSION)

debian-x86_64-Linux: x86_64-Linux debian
	mv welled-$(VERSION).deb ../dist/x86_64-Linux/
	rm -rf welled-$(VERSION) welled-$(VERSION).deb

debian-i386-Linux: i386-Linux debian
	mv welled-$(VERSION).deb ../dist/i386-Linux/
	rm -rf welled-$(VERSION) welled-$(VERSION).deb

debian-vyos:
	mkdir -p welled-$(VERSION)-vyos/DEBIAN/
	mkdir -p welled-$(VERSION)-vyos/etc/
	mkdir -p welled-$(VERSION)-vyos/bin/
	mkdir -p welled-$(VERSION)-vyos/etc/init.d/
	cp ../DEBIAN/control  welled-$(VERSION)-vyos/DEBIAN/
	sed -i "s/2.2.1/$(VERSION)/"  welled-$(VERSION)-vyos/DEBIAN/control
	sed -i "s/Architecture: all/Architecture: amd64/"  welled-$(VERSION)-vyos/DEBIAN/control
	cp $(OUTDIR)/welled welled-$(VERSION)-vyos/bin/
	cp $(OUTDIR)/gelled-ctrl welled-$(VERSION)-vyos/bin/
	cp ../scripts/init-welled-vyos welled-$(VERSION)-vyos/etc/init.d/welled
	chmod +x welled-$(VERSION)-vyos/etc/init.d/welled
	install -m 644 ../doc/welled.conf welled-$(VERSION)-vyos/etc/
	dpkg -b welled-$(VERSION)-vyos

vyos: x86_64-Linux-vyos debian-vyos
	mv welled-$(VERSION)-vyos.deb ../dist/x86_64-Linux-vyos/
	rm -rf welled-$(VERSION)-vyos


.ONESHELL: rpm

rpm:
	mkdir -p ../rpmbuild/{RPMS,SRPMS,BUILD,SOURCES,SPECS,tmp}
	cp ../.rpmmacros ~/
	cd ../rpmbuild
	sed -i "s:PATH:`pwd`:" ~/.rpmmacros
	mkdir -p welled-$(VERSION)
	mkdir -p welled-$(VERSION)/etc/
	mkdir -p welled-$(VERSION)/bin/
	mkdir -p welled-$(VERSION)/usr/share/welled/pix/
	mkdir -p welled-$(VERSION)/lib/systemd/system/
	mkdir -p welled-$(VERSION)/usr/local/share/man/man8/
	install -m 755 ../bin/welled welled-$(VERSION)/bin/
	install -m 755 ../bin/gelled welled-$(VERSION)/bin/
	install -m 755 ../bin/gelled-ctrl welled-$(VERSION)/bin/
	install -m 755 ../bin/gelled-gui welled-$(VERSION)/bin/
	install -m 755 ../pix/gps.png welled-$(VERSION)/usr/share/welled/pix/
	install -m 755 ../scripts/welled.path welled-$(VERSION)/lib/systemd/system/
	install -m 755 ../scripts/welled.service welled-$(VERSION)/lib/systemd/system/
	install -m 755 ../scripts/gelled.service welled-$(VERSION)/lib/systemd/system/gelled.service
	install -m 755 ../scripts/gelled.path welled-$(VERSION)/lib/systemd/system/gelled.path
	install -m 644 ../doc/welled.conf welled-$(VERSION)/etc/
	install -m 444 ../man/welled.8.gz welled-$(VERSION)/usr/local/share/man/man8/
	install -m 444 ../man/gelled.8.gz welled-$(VERSION)/usr/local/share/man/man8/
	install -m 444 ../man/gelled-ctrl.8.gz welled-$(VERSION)/usr/local/share/man/man8/
	tar czvf welled-$(VERSION).tar.gz welled-$(VERSION)/
	mv welled-$(VERSION).tar.gz SOURCES/
	cp ../contrib/welled.spec SPECS/
	sed -i "s/2.2.1/$(VERSION)/g" SPECS/welled.spec
	rpmbuild -bb --target $(ARCH) SPECS/welled.spec
	rm -rf ../rpmbuild

rpm-x86_64-Linux: x86_64-Linux rpm
	cp ../rpmbuild/RPMS/x86_64/welled-$(VERSION)-1.fc30.x86_64.rpm ../dist/x86_64-Linux/
	rm -rf ../rpmbuild

rpm-i386-Linux: i386-Linux rpm
	cp ../rpmbuild/RPMS/i386/welled-$(VERSION)-1.fc30.i386.rpm ../dist/i386-Linux/
	rm -rf ../rpmbuild


