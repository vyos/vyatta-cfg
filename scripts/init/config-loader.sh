#!/bin/vbash

. /lib/lsb/init-functions

: ${vyatta_env:=/etc/default/vyatta}
source $vyatta_env

GROUP=vyattacfg
BOOTFILE=$vyatta_sysconfdir/config/config.boot

if ! grep -q -w no-vyos-configure /proc/cmdline; then
    if [ -f /etc/default/vyatta-load-boot ]; then
        # build-specific environment for boot-time config loading
        source /etc/default/vyatta-load-boot
    fi
    sg ${GROUP} -c "$vyatta_sbindir/vyatta-boot-config-loader $BOOTFILE" &
fi

while ps -ef | grep my_commit | grep -v grep; do
    sleep 1
done
