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

Name:		welled
Version:	3.0.0
Release:	1%{?dist}
Summary:	wireless emulation

Group:		Applications/Emulators
License:	GPLv2
URL:		http://www.cert.org
SOURCE0 :	%{name}-%{version}.tar.gz

#Requires:	

%description
wireless emulation link layer exchange daemon

%global debug_package %{nil}

%prep
%autosetup
#%setup -q


%build


%install
mkdir -p %{buildroot}/bin/
mkdir -p %{buildroot}/etc/
mkdir -p %{buildroot}/lib/systemd/system/
mkdir -p %{buildroot}/usr/local/share/man/man8/
install -m 755 bin/welled %{buildroot}/bin/
install -m 755 bin/gelled %{buildroot}/bin/
install -m 755 bin/gelled-ctrl %{buildroot}/bin/
install -m 755 lib/systemd/system/welled.path %{buildroot}/lib/systemd/system/
install -m 755 lib/systemd/system/welled.service %{buildroot}/lib/systemd/system/
install -m 755 lib/systemd/system/gelled.service %{buildroot}/lib/systemd/system/
install -m 644 etc/welled.conf %{buildroot}/etc/
install -m 444 usr/local/share/man/man8/welled.8.gz %{buildroot}/usr/local/share/man/man8/
install -m 444 usr/local/share/man/man8/gelled.8.gz %{buildroot}/usr/local/share/man/man8/
install -m 444 usr/local/share/man/man8/gelled-ctrl.8.gz %{buildroot}/usr/local/share/man/man8/

%files
%config /etc/welled.conf
/bin/welled
/bin/gelled
/bin/gelled-ctrl
/lib/systemd/system/gelled.service
/lib/systemd/system/welled.path
/lib/systemd/system/welled.service
%doc %attr(0444,root,root) /usr/local/share/man/man8/welled.8.gz
%doc %attr(0444,root,root) /usr/local/share/man/man8/gelled.8.gz
%doc %attr(0444,root,root) /usr/local/share/man/man8/gelled-ctrl.8.gz

%clean
rm -rf %{buildroot}

%changelog

