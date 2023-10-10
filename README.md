# Introduction
This is a wireless emulation link layer exchange tool for Linux, based on the
netlink API implemented in the `mac80211_hwsim` kernel driver. Unlike the
default in-kernel forwarding mode of `mac80211_hwsim`, `welled` allows
emulation of wireless networking across VMs. This program was inspired by the
user space daemon `wmediumd` which implements loss and delay in transmission
between local radios. This daemon allows these frames to be exchanged between
VMs through an intermediary daemon running on the ESXi host. The transmission
of frames between VMs is performed through use of the Virtual Sockets API.
`gelled-ctrl` will allow the user to set the virtual location of the machine it
runs on in degrees latitute and longitude. `gelled` will receive a stream of
NMEA data that can inform a GPS utility sch as `gpsd` of the VM's virtual
location.

This project allows virtual wireless networks to be created between multiple
VMs. This enables users to train on the configuration of wireless access
points, wireless clients, wireless survey tools, and wireless penetration
testing tools inside of a completely virtual environment without ever
generating any real RF signals that might interfere with the read world.
Additionally, no hardware is required to build these virtual wireless networks.

This project contains files necessary to compile five programs:
`wmasterd`
`welled`
`gelled`
`gelled-ctrl`
`gelled-gui`

Some of these programs can be compiled to run on multiple operating systems:
* OpenWrt
* Android
* Fedora
* Ubuntu
* Kali
* Vyos
* Windows 7
* ESXi

The structure of this project's repository is as follows:
```
welled/ - files in this directory exist for building the .ipkg for OpenWrt, the .deb for Debian variants, and the .rpm for RHEL variants.
welled/64 - additional files used for 64 bit Openwrt builds
welled/android - files and notes used to compile for Android
welled/bin - binary output directory when running `make` inside of `welled/src/`
welled/DEBIAN - files used for .deb builds
welled/dist - compiled binaries and packages for various systems
welled/doc - documentation, conguration templates for OpenWrt, and resources
welled/drivers - compiled, patched versions of the `mac80211_hwsim` driver
welled/html - documentation created by Doxygen
welled/man - templates for man pages
welled/patches - patch files for `mac80211_hwsim` driver
welled/scripts - sysV init scripts, systemd unit files, and installation scripts
welled/src - source code
welled/vmware_guest - additional files used for OpenWrt builds
```

## Documentation
Documentation for these programs are provided through various means.

If you have Doxygen installed on your build system you can generate html files
which document the data structures and functions used in these programs.
```
cd src/
make doc
firefox ../html/index.html
```

If you do not have Doxygen installed, you can read the heavy documentation
contained within the source files. These comments are the source of the 
information used by Doxygen.

There are also man pages for `welled`, `gelled`, `gelled-ctrl` and `wmasterd`.
These are a work in progress and do not yet provided lengthy descriptions.

# Building the programs from source

## Building the patched driver for Linux
Using a patched `mac80211_hwsim` driver is preffered. The patches will remove
the default hwsim0 device which is supposed to be a monitor-mode device. The
device does not function as one would expect in any case, expecially when we
are using `welled` to relay the frames. Also, the patches will allow us to set
the permanent hardware address for the radios. The example below is for Linux
kernel 4.9.
```
apt install linux-source-4.9
cd /usr/src
tar xJvf linux-source-4.9.tar.xz
cd linux-source-4.9
git init
git add .
git commit -m "initial commit"
```
Now, it is expected that the patches will fail to apply when you attempt to do
this with a new kernel version. You will then need to manually adjust the code
to apply the patches, test the change, commit the change, and create a patch
file to get added to our repo.
```
cd /usr/src/linux-source-4.9/
cp /lib/modules/$(uname -r)/build/.config .config
cp /lib/modules/$(uname -r)/build/Module.symvers Module.symvers
make oldconfig && make prepare && make modules_prepare
cp /lib/modules/$(uname -r)/build/include/generated/utsrelease.h include/generated/utsrelease.h
make -j4 modules M=drivers/net/wireless/
```
If that compiles `mac80211_hwsim.ko` successfully, attempt to install it:
```
insmod ./drivers/net/wireless/mac80211_hwsim.ko
```
If that worked, attempt to apply the exsting patches:
```
git apply ~/welled/patches/$(uname -r)/*
```
If that worked, you are good to go. If not, attempt to apply the patches one by
one, making any modifications as needed.
To generate a new patch:
```
git add drivers/net/wireless/mac80211_hwsim.c
git commit -m "patch description"
git format-patch -1
```
Repeat for all patches, than add the new patches and module to the repo:
```
mkdir ~/welled/patches/$(uname -r)/
cp *patch ~/welled/patches/$(uname -r)/
mkdir ~/welled/drivers/$(uname -r)/
cp ./drivers/net/wireless/mac80211_hwsim.ko ~/welled/drivers/$(uname -r)/
cd ~/welled
git add drivers/$(uname -r)/
git add patches/$(uname -r)/
git commit -m "updates driver and patches for $(uname -r)"
```
To install the module you just compiled, run the following:
```
mv /lib/modules/$(uname -r)/kernel/drivers/net/wireless/mac80211_hwsim.ko /lib/modules/$(uname -r)/kernel/drivers/net/wireless/mac80211_hwsim.ko-orig
cp ./drivers/net/wireless/mac80211_hwsim.ko /lib/modules/$(uname -r)/kernel/drivers/net/wireless/mac80211_hwsim.ko
```

## Prerequisites for installing welled
First, you need a recent Linux kernel with the `mac80211_hwsim` module
available. 
Second, you will also need to have VMCI Sockets enabled in your kernel. On the
OpenWRT VM, the following modules are required: `vmw_vmci`, `vsock`, and 
`vmw_vsock_vmci_transport`.
Finally, the `welled` program also requires `libnl-3` to be installed.

## Prerequisites for building welled
To compile `welled` you will need `libnl-3-dev`, `libnl-genl-3-dev` and
`libnl-route-3-dev`installed on your build system. You will also need to have
`help2man` installed so that man files can be generated automatically.
`gelled-ctrl` now requires `glib2` so that it can parse a configuration file.
The Maekefile also uses `pkg-config` to determine paths and dependencies.
```
apt-get install libnl-3-dev libnl-genl-3-dev libnl-route-3-dev pkg-config
```

## Prerequisites for installing gelled and gelled-ctrl
You need to have VMCI Sockets enabled in your kernel. This requires `vmw_vmci`,
`vsock` and `vmw_vsock_vmci_transport` kernels modules. You will also need to
have `libpng` and `libglib2` installed.

## Prerequisites for building gelled and gelled-ctrl
You must have `libpng-dev` and `libglib2.0-dev` installed.
```
apt-get install libpng-dev libglib2.0-dev
```

## Prerequisites for installing wmasterd
The host must support VSOCK with SOCK_DGRAM. `wmasterd` has been run on:
* ESXi 5.5
* ESXi 6.0
* Windows 7 with VMware Workstation 12 and 14

## Building welled for Linux
`welled` has been used on:
* Kali Linux 1.1.0a
* Kali 2.0
* Kali rolling
* Fedora 23
* Fedora 27
* Ubuntu 12.04

An RPM and a Debian package are available for `welled`. It contains the three
client programs of `welled`, `gelled` and `gelled-ctrl`.

You must make sure that you are using a kernel listed under `drivers/` if you
wish to install a patched `mac80211_hwsim` driver. Otherwise, you may still use
`welled` but it may requiring toggling of the '-a' option when executed.

Example:
```
cd welled/src
make x86_64-Linux
```

### Setting up a build environment for android

See the file `android/adams_notes`.

## Installing welled on Linux
There are two `systemd` unit files for `welled` that you must install in the
correct directory. Enable the `welled.path` target rather than the
`welled.service` target in order to ensure that the `/dev/sock` device exists
prior to execution of the `welled` binary. These files should be placed inside
of `/lib/systemd/system/` and are named `welled.path` and `welled.service`.
Put the `welled` and `gelled-ctrl` binaries into `/bin`, and enable the service
with `systemctl enable welled.path`.

There is a sysVinit script for older systems without `systemd`. If needed, copy
the the sysVinti script to `/etc/init.d/`.

Copy the binaries for `welled` and `gelled-ctrl` into `/bin/`.

For some systems you may be able to install these programs by using the RPM or
Debian packages created in the `dist` folder.

To start `welled`, use the init scripts as normal:
```
/etc/init.d/welled start
```
or the newer method on systems running `systemd`:
```
systemctl start welled.path
```

Finally, if you wish to use the patched driver, make a copy of the original
kernel module and copy the new driver into its location.

Note: the patched driver takes two additional options when loaded. Make sure
that you comment out these options from the sysVinit script or unit file prior
to execution if you are not using the patched driver. If not using a patched
driver you will also want to modify the execution options passed to `welled` in
the sysVinit script or unit file to remove the `-a` option.

## Building welled with modified mac80211_hwsim driver for OpenWRT

### Setting up a build environment for openwrt

These steps will build an entier openwrt operating system, kernel and all.
If you wish to compile just `welled` binaries, you may look into using the
openwrt toolchain instead. The toolchain is compile by the `make` command seen
in the example code below.

You will need 10G of free space to compile openwrt.

The following websites provide more detail:
* http://wiki.openwrt.org/doc/howto/buildroot.exigence
* http://wiki.openwrt.org/doc/howto/build
* http://wiki.openwrt.org/doc/howto/build.a.package
* http://wiki.openwrt.org/doc/devel/patches

Once you have downloaded the OpenWRT source with `git`, place the `welled` 
source in the package directory.

You will need to install the `quilt` package if you are compiling on Fedora.
```
dnf install -y quilt
```
And possible Ubuntu.
```
sudo apt-get install quilt gawk subversion
```

As mentioned before, build the OpenWrt kernel with `vmw_vmci`, `vsock` and 
`vmw_vsock_vmci_transport`. Enable `libnl`, `mac80211_hwsim` and `welled`. Note
that `gelled-ctrl` now requires `glib2` to parse a configuration file.
Create a VMDK image file and load it onto your ESXi host. You may also wish to
set the size of the kernel partition and root partition to larger values.
You will also need to have hostapd installed to enable WPA. The following steps
will download `openwrt` and `welled`. You should also select a few additional
options inside of the `make menuconfig` step: VMDK, the size of the root and
kernel partitions, and any other tools you would like to install.
You may edit the `openwrt` configuration files under `doc/` if you would like
to build an image with a specific IP, MAC, SSID, passphrase, etc. Take note that
the `mac80211_hwsim` modprobe configuration file that gets copied to the 
`/etc/modules.d/` directory may not copy correctly and/or may get overwritten
when `mac80211_hwsim` is actually installed (this is my hunch).

Note that chaos calmer, 15.05 is used in this example.
```
#git clone git://git.openwrt.org/openwrt.git
git clone -b chaos_calmer git://github.com/openwrt/openwrt.git
cd openwrt/package
git clone https://github.com/cmu-sei/welled.git
cd ..
cat package/welled/vmw.mk >> package/kernel/linux/modules/virtual.mk
cp package/welled/patches/compat-wireless-2016-01-10/* package/kernel/mac80211/patches/
#cp package/welled/patches/compat-wireless-2015-03-09/* package/kernel/mac80211/patches/
cp -r package/welled/vmware_guest target/linux/x86/
mkdir target/linux/x86/64/profiles/
cp package/welled/64/profiles/001_welled.mk target/linux/x86/64/profiles/
sed -i "s/\(^SUBTARGETS=.*\)/\1 vmware_guest/" target/linux/x86/Makefile
touch target/linux/x86/Makefile
./scripts/feeds update
./scripts/feeds install luci-ssl
./scripts/feeds install glib2
make menuconfig #select arch x86_64 and welled profile
sed -i "s/# CONFIG_VMDK_IMAGES is not set/CONFIG_VMDK_IMAGES=y/" .config
sed -i "s/# CONFIG_TARGET_IMAGES_PAD is not set/CONFIG_TARGET_IMAGES_PAD=y/" .config
sed -i "s/CONFIG_TARGET_KERNEL_PARTSIZE=4/CONFIG_TARGET_KERNEL_PARTSIZE=1024/" .config
sed -i "s/CONFIG_TARGET_ROOTFS_PARTSIZE=48/CONFIG_TARGET_ROOTFS_PARTSIZE=4096/" .config
make target/linux/{clean,prepare} V=s QUILT=1 
make -j 8 # number of cpus * 1.5
```

## 23.05 Development notes
```
git clone --single-branch --branch openwrt-23.05 https://github.com/openwrt/openwrt.git
cd openwrt/package
git clone https://github.com/cmu-sei/welled.git
git clone https://github.com/cmu-sei/vtunnel.git
cd ..
./scripts/feeds update
./scripts/feeds install luci-ssl
./scripts/feeds install glib2
cat package/welled/vmw.mk >> package/kernel/linux/modules/virtual.mk
mkdir target/linux/x86/64/profiles/
cp package/welled/64/profiles/001_welled.mk target/linux/x86/64/profiles/
#echo CONFIG_TARGET_KERNEL_PARTSIZE=1024 >> .config
#sed -i "s/CONFIG_TARGET_KERNEL_PARTSIZE=16/CONFIG_TARGET_KERNEL_PARTSIZE=1024/" .config
sed -i "s/CONFIG_TARGET_ROOTFS_PARTSIZE=104/CONFIG_TARGET_ROOTFS_PARTSIZE=4096/" .config
touch target/linux/x86/Makefile
make target/linux/{clean,prepare} V=s QUILT=1
make -j 12 # number of cpus * 1.5
```


## 18.06 Development notes
git clone --single-branch --branch openwrt-18.06 https://github.com/openwrt/openwrt.git
cd openwrt/package
git clone https://github.com/cmu-sei/welled.git
git clone https://github.com/cmu-sei/vtunnel.git
git clone https://github.com/fangli/openwrt-vm-tools package/open-vm-tools
cd ..
./scripts/feeds update
./scripts/feeds install luci-ssl
./scripts/feeds install glib2
./scripts/feeds install softflowd
./scripts/feeds install libtirpc
cat package/welled/vmw.mk >> package/kernel/linux/modules/virtual.mk
touch target/linux/x86/Makefile
make menuconfig #select arch x86_64, enable welled and open-vm-tools and softflowd and kmod-scsi-cdrom and vsock modules and wireless-tools and vtunnel
make -j 12

Steps to rebuild `welled` package if making modifications.
```
make V=s package/welled/install
```

Once you have started the OpenWRT VM, you will probably need to use the web 
interface to configure the networking. welled should be enabled by default.
Included in this package will be sample configuration files named `network` 
and `wireless`. These were taken from a live system and may not be all that
is necessary to get the OpenWRT networking subsystem configured correctly. Use
them as a guide when configuring the system through the web interface. These
files did not entirely configure the Breaker Barrier 14.07 system but they do
appear to correctly configure the Designated Driver build. We are now using
10.10.10.1 as the IP address for the LAN network. Bridging WLAN and LAN was
not working as intended so we will just use two separate networks. The default
password for OpenWRT is no password. You will be prompted to set the password on
login to the web GUI. There also files to configure a DHCP server and a firewall
segment for the wlan network. These files are named `dhcp` and `firewall` and
will be copied into the config directory with the previously mentioned files.
These files are known to work with Designated Driver.

## Installing on Chaos Calmer 15.05
If you would like to use the standard image available online, you may!
Just follow these steps after getting the image running.
1. connect the second interface, eth1, to the internet (wan interface)
2. update the list of packages known to opkg
3. install packages to allow use of the cdrom drive
4. reboot the system and the cdrom device will appear
5. mount an iso containing the welled package
6. install the welled package
7. restart the network
8. profit

```
opkg update
opkg install kmod-scsi-core kmod-scsi-cdrom kmod-fs-isofs
reboot
mount /dev/sr0 /mnt
opkg update
opkg install /mnt/welled_2.2.1_x86_64.ipk
```

## Building wmasterd for ESXi
You must compile `wmasterd` for an x86_64 GNU/Linux system, then copy the file
to the ESXi host. The Makefile's esx target will create a tarball containing the
`wmasterd` executable, an init script, and an installation script.
```
cd src
make esx
```

## Building wmasterd for ESXi VIB
```
make offline-bundle
```


## Installing wmasterd on ESXi
Once you have placed the tarball on the ESXi host, unpack it and run the 
installation script. This will enable but not start the service. Note that you
must use the correct version when using the example below. This script will
install the service into the bookbank for persistence.
```
tar xjvf wmasterd_2.2.1_esx.tar.bz2
cd scripts
sh install-wmasterd-esx.sh
```

Start the `wmasterd` service on the ESXi host using the init script.
```
/etc/init.d/wmasterd start
```

This script also has a status argument which will write the current linked list
of `welled` client nodes to both STDOUT and /tmp/wmasterd.status.
Be advised this this script will default to enabled distance mode with `-d` and
also will enabled a "cache" file that stores the location of each `welled`
client into `/wmasterd_nodes`.

## Installing wmasterd on ESXi with a VIB
```
esxcli software acceptance set --level=CommunitySupported
esxcli software vib install -d /CMU-wmasterd-2.3.0-offline_bundle.zip
```

## Building wmasterd for Windows
You can compile `wmasterd` for Windows as well. This can be done to allow the
use of `welled` with VMCI Sockets on a Windows laptop which has VMWare
Workstation installed. You must first have a build environment setup for cross
compilation of Windows executables, or you may compile `wmasterd` using a
development environment you may already have setup for Windows executables.

To compile for Windows, you need to install mingw (works best on Fedora):
```
sudo apt-get install mingw-w64
```

Commpile for either x86 or x86_64.
```
make wmasterd-i686-w64-mingw32
make wmasterd-x86_64-w64-mingw32
```
The `wmasterd-i686-w64-mingw32` target will set the CROSS variable to allow
easier cross compilation for Windows. Once the executable has been created,
transfer it to your Windows laptop and execute it as an Administrator.
The `wmasterd-x86_64-w64-mingw32` target will do the same, although it will
output a 64 bit binary. There is also a target for `windows` which compiled both
architectures.

You may also compile Windows binaries for the `gelled` and `gelled-ctrl`
programs and may have better luck installing all of the required mingw packages
on a Fedora system than an Ubuntu system.

## Installation on Windows
Neither `wmasterd` nor `gelled` support the Windows service system at this
point in time. `welled` is dependent on the Linux kernel and is not available
for Windows. `gelled` and `gelled-ctrl` have not yet been tested on Windows.

## Using welled on Kali Linux without installation
If you haven't installed `welled`, start `welled` by loading the mac80211_hwsim 
kernel module and then execute `welled` without arguments. If you wish to see
verbose output regarding messages received and transmitted, execute `welled`
with the -v option.
To run `welled` on a system which does not have a patched driver, or a system
which has upgraded its kernel thereby removing the patch:
```
modprobe mac80211_hwsim radios=1
./welled
```
To run `welled` on a patched system where we can use any MAC address because we
have installed our patched driver:
```
modprobe mac80211_hwsim radios=1 use_hwsim_mon=0 perm_addr=00:0c:41:00:00:00
./welled -a
```

## Setting the VMs location
You must also set the location of the virtual machine if you wish to have the
signal strength adjusted by `wmasterd`. Use `gelled-ctrl` for this purpose. The
options of interest are -x for longitude and -y for latitude.
```
/bin/gelled-ctrl -x 35 -y 35
```

## Special note on wifi distances
If you are setting a VM's location with the intent of adjusting the signal
strength keep in mind that the distance between point is rather tricky to
determine on your own. This is because the he number of meters for each degree
of longitude varies depending upon the latitude. The following website will give
you the distance between a set of coordinates. Make sure to use at least one
decimal place for this website to work.

```
http://boulter.com/gps/distance/?from=40.0+-80.0&to=40.0+-80.0001&units=k
```

## Setting the VMs name, address, or room when in non-STEP environment
Take note that STEP is a reference to the deprecated STEPfwd Cyber Range Management tool that has been replaced by Crucible and Topomojo.
```
/bin/gelled-ctrl -n kali-local -r 55
```

Of course, all of these tools have usage statements, so please read themm.
Man pages are a work in progress because `help2man` cannot
read from STDOUT on a few of the binaries. Please help fix this if you can.

## Using wmasterd on ESXi/Linux without installation
If you haven't installed `wmasterd`, execute wmasterd without arguments. Just
like `welled`, `wnasterd` will display verbose output when executed with the -v
option.
```
./wmasterd
```
Using wmasterd with signal strength modifications

```
./wmasterd -d
```
Using wmasterd with a cache file to store VM locations persistently
```
./wmasterd -c <filename>
```

`wmasterd` does not show output until either  `gelled` or `welled` client
sends a packet to it. It then begins sending NMEA messages to the client and
relaying wireless frames.

When in cache file mode, `wmasterd` will attempt to look up old location and
info about the VM from the cache file.

`wmasterd` can log messages to syslog. By default the ESXi init script will set
the log level to 5, LOG_NOTICE. This will log NOTICE, WARNING and ERROR message
output from `wmasterd`. On ESXi this logs to `/scratch/log/syslog.log`. To set
this option on the command line, use the `-D` option. Example:
```
wmasterd -D 7
```
This examle sets the level to DEBUG and you will receive thhe most verbose level
of logging.

## Building gelled
`gelled`, the GPS emulation link layer exchange daemon, can be compiled when a 
simulated GPS feed (NMEA sentences) us desired. It is designed to receive these
NMEA sentences from `wmasterd` and write them to an emulated serial device. This
can be provided by `tty0tty`, our recompiled copy of the driver named `gpstty`,
currently located under the `gelled` project, or a `socat` command which could
be installed as a service. On Windows, you may use a similar program called
`com0com` as a null modem emulator to provide a pair of fake serial devices.
The 2.2.0 version of `com0com` has been tested with `gelled` on Windows 7.

You should also take note that to fully utilize a feature of `gelled` that stops
the simulated feed from crossing from land into sea, or sea into land. This is
an optional behaviour which relies on the use of an in-game map server which is
used to provide a map tile which will be used to make this determination based
on the pixel color of the bit at the current coordinates (I know, it sounds a
little crazy, but that is what the web says!).
Instructions for building a map server for Ubuntu 12.04 can be found here:
`https://switch2osm.org/serving-tiles/building-a-tile-server-from-packages/`
To setup the map server so that it does not need to access the internet, you
will need to download the javascript libraries listed in `slippymap.html` as
well as the theme's icons. The `slippymap.html` file should also be updated to
reflect the appropriate hostname for your map server.

## Example command to create serial devices
```
socat PTY,link=/dev/ttyUSB0 PTY,link=/dev/ttyUSB1
```

## Using gelled over VMCI with verbosity
```
./gelled -v -d /dev/ttyUSB0
```

## A note on network namespaces
While the `mac80211_hwsim` driver works when radios are placed in different network namespaces, `welled` will only see the radios in the network namespace it is run within. Usually, this will be the default network namespace. In order for `welled` to run in each network namespace the networking bewteen `welled` and `wmasterd` will need to be updated. Currently, the fixed VSOCK port numbers present an issue as only once instance of `welled` will be able to bind to it.

## Gotchas

### Allowable MAC addresses

The driver only allows welled to work on the second available hardware
address, which has bit 6 set in the most significant octet
(i.e. 42:00:00:xx:xx:xx, not 02:00:00:xx:xx:xx). Set this appropriately
using 'ip link set address'.

This issue was fixed in commit cd37a90b2a417e5882414e19954eeed174aa4d29
in Linux, released in kernel 4.1.0.

This commit seems to add a new function:
`get_hwsim_data_ref_from_addr()`

This new function is called by the following functions:
`hwsim_cloned_frame_received_nl()`, and `hwsim_tx_info_frame_received_nl()`

The newer version of the `get_hwsim_data_ref_from_addr()` function does not just
check data->addresses[1] as in previous versions, it instead  calls the function
`mac80211_hwsim_addr_match()` which iterates the interfaces on each radio device
and returns true if there is a match. This means that for newer kernels, but not
the kernel used in Kali 1.1.0a and Kali 2.0, (only 4.1.0+) the MAC address
assigned to either the first or second address field for the radio device would
work, not just the second address. It no longer requires the user assigned MAC
address to start with 0x42.

The functionality contained within `welled` will not actively update the MAC
addresses assigned to the hwsim radios. The driver on the system running 
`welled` should be patched with the included patch files. These patch files will
update the driver to use the current upstream address checking functions and
also allow the MAC address of the first virtual radio to be passed to the driver
as a parameter when loaded. Updates to the MAC addresses on these radios will
be detected through netlink route functionality. `welled` will detect when an
interface is in monitor mode and utilize the 00:00:00:00:00:00 address instead
of a hard-coded address. This will allow a patched driver to deliver the frame
to whatever application is monitoring the radio, for example `airodump-ng` or
`tcpdump`.

Note: the aforementioned patch to call `mac80211_hwsim_addr_match()` may be now
be reverted upstream, requiring the patch to the driver to be reinstalled if you
are building a patched driver and you want that functionality. Otherwise, you
may wish to toggle the `-a` option to `welled` and test both with and withot the
option when using a stock driver. Depending on the driver having that patch or
not you may or may not need this option.

Test whether you need the option by performing a scan and looking for your AP.
```
iw dev wlan0 scan
```

## Example setup

One ESXi server running `wmasterd` which hosts the following guests:
One OpenWRT VM running `welled`
One Kali Linux VM running `welled`
One Fedora 23 Linux VM running `welled`


Copyright 2015-2023 Carnegie Mellon University. See LICENSE.txt for terms.

