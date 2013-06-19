#!/bin/sh
# Run this to generate all the initial makefiles, etc.

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

PKG_NAME=light-locker

(test -f $srcdir/configure.ac.in \
  && test -f $srcdir/autogen.sh \
  && test -d $srcdir/src \
  && test -f $srcdir/src/light-locker.c) || {
    echo -n "**Error**: Directory "\`$srcdir\'" does not look like the"
    echo " top-level $PKG_NAME directory"
    exit 1
}

which xdt-autogen && {
    XDT_AUTOGEN_REQUIRED_VERSION="4.7.2" exec xdt-autogen $@
}

# Remove xdt-autogen m4 macro
sed -e "/XDT_I18N/d" configure.ac.in > configure.ac

which gnome-autogen.sh || {
    echo "You need to install either xfce4-dev-tools from http://www.xfce.org/"
    echo "or gnome-common from the GNOME CVS"
    exit 1
}

REQUIRED_AUTOMAKE_VERSION=1.9 GNOME_DATADIR="$gnome_datadir" . gnome-autogen.sh
