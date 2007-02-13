#!/bin/sh

# This is probably linux only at the moment

if [ -z "${DBUS_SESSION_BUS_ADDRESS}" ]; then
    pid=`pgrep "gnome-session|x-session-manager"`
    if [ "x$pid" != "x" ]; then
        env_address=`(cat /proc/$pid/environ; echo) | tr "\000" "\n" | grep '^DBUS_SESSION_BUS_ADDRESS='`
        env_display=`(cat /proc/$pid/environ; echo) | tr "\000" "\n" | grep '^DISPLAY='`
        if [ "x$env_address" != "x" ]; then
            echo "Setting $env_address"
            echo "Setting $env_display"
            eval "export $env_address"
            eval "export $env_display"
        fi
    fi
fi

if [ -z "${DBUS_SESSION_BUS_ADDRESS}" ]; then
    echo "Could not determine DBUS_SESSION_BUS_ADDRESS"
    exit 1
fi

# kill the existing daemon
gnome-screensaver-command --exit

# run the daemon in the debugger
gdb --args gnome-screensaver --no-daemon --debug --sync