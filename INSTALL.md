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

This project contains files necessary to compile four programs:
`wmasterd`
`welled`
`gelled`
`gelled-ctrl`

Some of these four programs can be compiled to run on multiple operating systems:
* OpenWrt
* Android
* Fedora
* Ubuntu
* Kali
* Vyos
* Windows 7
* ESXi


## Prerequisites for installing welled
First, you need a recent Linux kernel with the `mac80211_hwsim` module
available. 
Second, you will also need to have VMCI Sockets enabled in your kernel. On the
OpenWRT VM, the following modules are required: `vmw_vmci`, `vsock`, and 
`vmw_vsock_vmci_transport`.
Finally, the `welled` program also requires `libnl-3` to be installed.

## Prerequisites for installing gelled and gelled-ctrl
You need to have VMCI Sockets enabled in your kernel. This requires `vmw_vmci`,
`vsock` and `vmw_vsock_vmci_transport` kernels modules. You will also need to
have `libpng` and `libglib2` installed.


## Prerequisites for installing wmasterd
None at the moment. `wmasterd` has been run on:
* ESXi 5.5
* ESXi 6.0
* Windows 7 with VMware Workstation 12 and 14


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
```
/bin/gelled-ctrl -n kali-local -r 55
```

Of course, all of these tools have usage statements, so RTFUS (read the fine
usage statement). Man pages are a work in progress because `help2man` cannot
read from STDOUT on a few of the binaries... strange. Please help fix this!!

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


## gelled
`gelled`, the GPS emulation link layer exchange daemon, can be compiled when a 
simulated GPS feed (NMEA sentences) us desired. It is designed to receive these
NMEA sentences from `wmasterd` and write them to a serial device provided by
`tty0tty` or our recompiled copy of the driver named `gpstty`, currently located
under the `gelled` project. On Windows, you may use a similar program called
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

## Using gelled over VMCI with verbosity
```
./gelled -v -d /dev/ttyUSB0
```

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


Copyright 2015-2018 Carnegie Mellon University. See LICENSE.txt for terms.

