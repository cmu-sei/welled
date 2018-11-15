#!/bin/sh

cp wmasterd.tgz /bootbank/wmasterd.tgz
tar -xzvf /bootbank/wmasterd.tgz -C /
grep wmasterd.tgz /bootbank/boot.cfg >/dev/null
if [ $? -eq 1 ]; then
  echo "Adding wmasterd.tgz to /bootbank/boot.cfg"
  sed -i "s/state.tgz/wmasterd.tgz --- state.tgz/" /bootbank/boot.cfg
  /etc/init.d/wmasterd enable
  esxcli network firewall refresh
fi
/etc/init.d/wmasterd restart
/etc/init.d/wmasterd status
