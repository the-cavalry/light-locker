#!/bin/sh
# Run this to generate all the initial makefiles, etc.

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

PKG_NAME=gnome-screensaver

(test -f $srcdir/configure.ac \
  && test -f $srcdir/autogen.sh \
  && test -d $srcdir/src \
  && test -f $srcdir/src/gnome-screensaver.c) || {
    echo -n "**Error**: Directory "\`$srcdir\'" does not look like the"
    echo " top-level $PKG_NAME directory"
    exit 1
}

which gnome-autogen.sh || {
    echo "You need to install gnome-common from the GNOME CVS"
    exit 1
}

REQUIRED_AUTOMAKE_VERSION=1.9 GNOME_DATADIR="$gnome_datadir" . gnome-autogen.sh
