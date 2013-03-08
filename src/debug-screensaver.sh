#!/bin/sh

# This is probably linux only at the moment

if [ -z "${DBUS_SESSION_BUS_ADDRESS}" ]; then
    pid=`pgrep -u $USER "light-locker|x-session-manager"`
    if [ "x$pid" != "x" ]; then
        env_address=`(cat /proc/$pid/environ; echo) | tr "\000" "\n" | grep '^DBUS_SESSION_BUS_ADDRESS='`
        env_display=`(cat /proc/$pid/environ; echo) | tr "\000" "\n" | grep '^DISPLAY='`
        env_xdg_cookie=`(cat /proc/$pid/environ; echo) | tr "\000" "\n" | grep '^XDG_SESSION_COOKIE='`
        env_path=`(cat /proc/$pid/environ; echo) | tr "\000" "\n" | grep '^PATH='`
        if [ "x$env_address" != "x" ]; then
            echo "Setting $env_address"
            echo "Setting $env_display"
            echo "Setting $env_path"
            echo "Setting $env_xdg_cookie"
            eval "export $env_address"
            eval "export $env_display"
            eval "export $env_path"
            eval "export $env_xdg_cookie"
        fi
    fi
fi

if [ -z "${DBUS_SESSION_BUS_ADDRESS}" ]; then
    echo "Could not determine DBUS_SESSION_BUS_ADDRESS"
    exit 1
fi

export G_DEBUG=fatal_criticals

# kill the existing daemon
light-locker-command --exit

# run the daemon in the debugger
#gdb --args light-locker --no-daemon --debug --sync

# or if that isn't helpful just get the debug output
#light-locker --no-daemon --debug > /tmp/gs-debug-log.txt 2>&1

# or just run it with debugging on
light-locker --no-daemon --debug
