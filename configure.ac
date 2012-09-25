dnl -*- mode: m4 -*-

AC_PREREQ(2.60)
AC_INIT([gnome-screensaver],
        [3.6.1],
        [http://bugzilla.gnome.org/enter_bug.cgi?product=gnome-screensaver])

AC_CONFIG_SRCDIR(src/gnome-screensaver.c)

AM_INIT_AUTOMAKE([1.10 no-dist-gzip dist-xz tar-ustar])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AM_CONFIG_HEADER(config.h)

AM_MAINTAINER_MODE([enable])

IT_PROG_INTLTOOL([0.35.0])
AC_ISC_POSIX
AC_PROG_CC
AM_PROG_CC_C_O
AC_STDC_HEADERS
AC_PROG_LIBTOOL
AC_CANONICAL_HOST

AC_HEADER_STDC

AC_SUBST(VERSION)

# Save flags to aclocal
ACLOCAL="$ACLOCAL $ACLOCAL_FLAGS"

GETTEXT_PACKAGE=gnome-screensaver
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", [Name of default gettext domain])

AM_GLIB_GNU_GETTEXT
GLIB_GSETTINGS

# Dependencies

DBUS_REQUIRED_VERSION=0.30
GLIB_REQUIRED_VERSION=2.25.6
GTK_REQUIRED_VERSION=2.99.3
X11_REQUIRED_VERSION=1.0
GNOME_DESKTOP_REQUIRED_VERSION=3.1.91
GSETTINGS_DESKTOP_SCHEMAS_REQUIRED_VERSION=0.1.7

LIBGNOMEKBDUI_REQUIRED_VERSION=0.1

AC_CHECK_HEADERS(unistd.h)
AC_CHECK_HEADERS(crypt.h sys/select.h)
AC_CHECK_FUNCS(select fcntl uname nice setpriority getcwd getwd putenv sbrk)
AC_CHECK_FUNCS(sigaction syslog realpath setrlimit)
AC_CHECK_FUNCS(getresuid)
AC_TYPE_UID_T

AC_CHECK_FUNCS([setresuid setenv unsetenv clearenv])
PKG_CHECK_MODULES(GNOME_SCREENSAVER,
        x11 >= $X11_REQUIRED_VERSION
        gtk+-3.0 >= $GTK_REQUIRED_VERSION
        dbus-glib-1 >= $DBUS_REQUIRED_VERSION
        gnome-desktop-3.0 >= $GNOME_DESKTOP_REQUIRED_VERSION
        gsettings-desktop-schemas >= $GSETTINGS_DESKTOP_SCHEMAS_REQUIRED_VERSION)
AC_SUBST(GNOME_SCREENSAVER_CFLAGS)
AC_SUBST(GNOME_SCREENSAVER_LIBS)

PKG_CHECK_MODULES(GNOME_SCREENSAVER_DIALOG,
        gthread-2.0
        gtk+-3.0 >= $GTK_REQUIRED_VERSION)
AC_SUBST(GNOME_SCREENSAVER_DIALOG_CFLAGS)
AC_SUBST(GNOME_SCREENSAVER_DIALOG_LIBS)

PKG_CHECK_MODULES(GNOME_SCREENSAVER_CAPPLET,
        gio-2.0 >= $GLIB_REQUIRED_VERSION
        gtk+-3.0 >= $GTK_REQUIRED_VERSION
        gsettings-desktop-schemas >= $GSETTINGS_DESKTOP_SCHEMAS_REQUIRED_VERSION)
AC_SUBST(GNOME_SCREENSAVER_CAPPLET_CFLAGS)
AC_SUBST(GNOME_SCREENSAVER_CAPPLET_LIBS)

PKG_CHECK_MODULES(GNOME_SCREENSAVER_COMMAND,
        gio-2.0 >= $GLIB_REQUIRED_VERSION
        gobject-2.0 >= $GLIB_REQUIRED_VERSION)
AC_SUBST(GNOME_SCREENSAVER_COMMAND_CFLAGS)
AC_SUBST(GNOME_SCREENSAVER_COMMAND_LIBS)

AC_PATH_XTRA

ALL_X_LIBS="$X_LIBS $X_PRE_LIBS -lXext -lX11 $X_EXTRA_LIBS"
SAVER_LIBS="$ALL_X_LIBS"

AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)

GNOME_COMPILE_WARNINGS(yes)

# Optional dependencies for the theme engines

SAVER_MODULES="gthread-2.0 gtk+-3.0 >= $GTK_REQUIRED_VERSION"

PKG_CHECK_MODULES(GNOME_SCREENSAVER_SAVER, $SAVER_MODULES)
AC_SUBST(GNOME_SCREENSAVER_SAVER_CFLAGS)
AC_SUBST(GNOME_SCREENSAVER_SAVER_LIBS)

# Find out the version of DBUS we're using

dbus_version=`pkg-config --modversion dbus-1`
DBUS_VERSION_MAJOR=`echo $dbus_version | awk -F. '{print $1}'`
DBUS_VERSION_MINOR=`echo $dbus_version | awk -F. '{print $2}'`
DBUS_VERSION_MICRO=`echo $dbus_version | awk -F. '{print $3}'`
if test "z$DBUS_VERSION_MAJOR" = "z"; then
        DBUS_VERSION_MAJOR="0"
fi
if test "z$DBUS_VERSION_MINOR" = "z"; then
        DBUS_VERSION_MINOR="0"
fi
if test "z$DBUS_VERSION_MICRO" = "z"; then
        DBUS_VERSION_MICRO="0"
fi

if test "z$DBUS_VERSION_MAJOR" = "z0" -a "z$DBUS_VERSION_MINOR" = "z0" -a "z$DBUS_VERSION_MICRO" = "z0"; then        echo "Error: Couldn't determine the version of your DBUS package."
        echo "  This is probably an error in this script, please report it"
        echo "  along with the following information:"
        echo "      Base DBUS version ='$dbus_version'"
        echo "      DBUS_VERSION_MAJOR='$DBUS_VERSION_MAJOR'"
        echo "      DBUS_VERSION_MINOR='$DBUS_VERSION_MINOR'"
        echo "      DBUS_VERSION_MICRO='$DBUS_VERSION_MICRO'"
        exit 1
else

        echo "Your dbus version is $DBUS_VERSION_MAJOR,$DBUS_VERSION_MINOR,$DBUS_VERSION_MICRO."
        DBUS_CFLAGS="$DBUS_CFLAGS -DDBUS_VERSION_MAJOR=$DBUS_VERSION_MAJOR"
        DBUS_CFLAGS="$DBUS_CFLAGS -DDBUS_VERSION_MINOR=$DBUS_VERSION_MINOR"
        DBUS_CFLAGS="$DBUS_CFLAGS -DDBUS_VERSION_MICRO=$DBUS_VERSION_MICRO"

        AC_SUBST(DBUS_CFLAGS)
fi

dnl ---------------------------------------------------------------------------
dnl - Where should we put documentation ?
dnl ---------------------------------------------------------------------------
AC_ARG_WITH(doc-dir,
              [AC_HELP_STRING([--with-doc-dir=<dir>],
              [directory to install documentation])])
if ! test -z "$with_doc_dir"; then
   DOCDIR="$with_doc_dir/gnome-screensaver-$VERSION"
else
   DOCDIR="$datadir/doc/gnome-screensaver-$VERSION"
fi
AC_SUBST(DOCDIR)

dnl ---------------------------------------------------------------------------
dnl - DocBook Documentation
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(docbook-docs, [  --enable-docbook-docs   build documentation (requires xmlto)],enable_docbook_docs=$enableval,enable_docbook_docs=no)
AC_PATH_PROG(XMLTO, xmlto, no)
AC_MSG_CHECKING([whether to build DocBook documentation])
if test x$XMLTO = xno ; then
    have_docbook=no
else
    have_docbook=yes
fi
if test x$enable_docbook_docs = xauto ; then
    if test x$have_docbook = xno ; then
        enable_docbook_docs=no
    else
        enable_docbook_docs=yes
    fi
fi
if test x$enable_docbook_docs = xyes; then
    if test x$have_docbook = xno; then
	AC_MSG_ERROR([Building DocBook docs explicitly required, but DocBook not found])
    fi
fi
AM_CONDITIONAL(DOCBOOK_DOCS_ENABLED, test x$enable_docbook_docs = xyes)
AC_MSG_RESULT(yes)

dnl ---------------------------------------------------------------------------
dnl - Some utility functions to make checking for X things easier.
dnl ---------------------------------------------------------------------------

# Like AC_CHECK_HEADER, but it uses the already-computed -I directories.
#
AC_DEFUN([AC_CHECK_X_HEADER], [
  ac_save_CPPFLAGS="$CPPFLAGS"
  if test \! -z "$includedir" ; then 
    CPPFLAGS="$CPPFLAGS -I$includedir"
  fi
  CPPFLAGS="$CPPFLAGS $X_CFLAGS"
  AC_CHECK_HEADER([$1],[$2],[$3],[$4])
  CPPFLAGS="$ac_save_CPPFLAGS"])

# Like AC_TRY_COMPILE, but it uses the already-computed -I directories.
#
AC_DEFUN([AC_TRY_X_COMPILE], [
  ac_save_CPPFLAGS="$CPPFLAGS"
  if test \! -z "$includedir" ; then 
    CPPFLAGS="$CPPFLAGS -I$includedir"
  fi
  CPPFLAGS="$CPPFLAGS $X_CFLAGS"
  AC_TRY_COMPILE([$1], [$2], [$3], [$4])
  CPPFLAGS="$ac_save_CPPFLAGS"])


# Like AC_CHECK_LIB, but it uses the already-computed -I and -L directories.
# Use this sparingly; it probably doesn't work very well on X programs.
#
AC_DEFUN([AC_CHECK_X_LIB], [
  ac_save_CPPFLAGS="$CPPFLAGS"
  ac_save_LDFLAGS="$LDFLAGS"
#  ac_save_LIBS="$LIBS"

  if test \! -z "$includedir" ; then 
    CPPFLAGS="$CPPFLAGS -I$includedir"
  fi
  # note: $X_CFLAGS includes $x_includes
  CPPFLAGS="$CPPFLAGS $X_CFLAGS"

  if test \! -z "$libdir" ; then
    LDFLAGS="$LDFLAGS -L$libdir"
  fi
  # note: $X_LIBS includes $x_libraries
  LDFLAGS="$LDFLAGS $ALL_X_LIBS"

  AC_CHECK_LIB([$1], [$2], [$3], [$4], [$5])
  CPPFLAGS="$ac_save_CPPFLAGS"
  LDFLAGS="$ac_save_LDFLAGS"
#  LIBS="$ac_save_LIBS"
  ])


# Usage: HANDLE_X_PATH_ARG([variable_name],
#                          [--command-line-option],
#                          [descriptive string])
#
# All of the --with options take three forms:
#
#   --with-foo (or --with-foo=yes)
#   --without-foo (or --with-foo=no)
#   --with-foo=/DIR
#
# This function, HANDLE_X_PATH_ARG, deals with the /DIR case.  When it sees
# a directory (string beginning with a slash) it checks to see whether
# /DIR/include and /DIR/lib exist, and adds them to $X_CFLAGS and $X_LIBS
# as appropriate.
#
AC_DEFUN([HANDLE_X_PATH_ARG], [
   case "$[$1]" in
    yes) ;;
    no)  ;;

    /*)
     AC_MSG_CHECKING([for [$3] headers])
     d=$[$1]/include
     if test -d $d; then
       X_CFLAGS="-I$d $X_CFLAGS"
       AC_MSG_RESULT($d)
     else
       AC_MSG_RESULT(not found ($d: no such directory))
     fi

     AC_MSG_CHECKING([for [$3] libs])
     d=$[$1]/lib
     if test -d $d; then
       X_LIBS="-L$d $X_LIBS"
       AC_MSG_RESULT($d)
     else
       AC_MSG_RESULT(not found ($d: no such directory))
     fi

     # replace the directory string with "yes".
     [$1]_req="yes"
     [$1]=$[$1]_req
     ;;

    *)
     echo ""
     echo "error: argument to [$2] must be \"yes\", \"no\", or a directory."
     echo "       If it is a directory, then \`DIR/include' will be added to"
     echo "       the -I list, and \`DIR/lib' will be added to the -L list."
     exit 1
     ;;
   esac
  ])

dnl ---------------------------------------------------------------------------
dnl - Check for shaped window extension
dnl ---------------------------------------------------------------------------

have_shape=no
AC_CHECK_X_HEADER(X11/extensions/shape.h, [have_shape=yes],,
                    [#include <X11/Xlib.h>])
if test "$have_shape" = yes; then
  AC_DEFINE(HAVE_SHAPE_EXT, 1, [Define if shape extension is available])
fi

dnl ---------------------------------------------------------------------------
dnl - Check for the MIT-SCREEN-SAVER server extension.
dnl ---------------------------------------------------------------------------

have_mit=no
with_mit_req=unspecified
AC_ARG_WITH(mit-ext,
[  --with-mit-ext          Include support for the MIT-SCREEN-SAVER extension.],
  [with_mit="$withval"; with_mit_req="$withval"],[with_mit=yes])

HANDLE_X_PATH_ARG(with_mit, --with-mit-ext, MIT-SCREEN-SAVER)

if test "$with_mit" = yes; then
  AC_CHECK_X_HEADER(X11/extensions/scrnsaver.h, [have_mit=yes],,
                    [#include <X11/Xlib.h>])

  # Now check to see if it's really in the library; XF86Free-3.3 ships
  # scrnsaver.h, but doesn't include the code in libXext.a
  if test "$have_mit" = yes; then
    AC_CHECK_X_LIB(Xext, XScreenSaverRegister, [true], [have_mit=no], -lm)

    if test "$have_mit" = no; then
      # Looks like XF86Free-3.3 actually puts it in XExExt instead
      # of in Xext.
      AC_CHECK_X_LIB(XExExt, XScreenSaverRegister,
                     [have_mit=yes; SAVER_LIBS="$SAVER_LIBS -lXExExt"],
                     [true], -lX11 -lXext -lm)
    fi

    if test "$have_mit" = no; then
      # Looks like some versions of XFree86 (whichever version
      # it is that comes with RedHat Linux 2.0 -- I can't find a version 
      # number) put this in Xss instead of Xext.
      AC_CHECK_X_LIB(Xss, XScreenSaverRegister,
                     [have_mit=yes; SAVER_LIBS="$SAVER_LIBS -lXss"],
                     [true], -lX11 -lXext -lm)
    fi

  if test "$have_mit" = yes; then
    AC_DEFINE(HAVE_MIT_SAVER_EXTENSION, 1, [Define if the MIT screensaver extension is available])
  fi

  fi

elif test "$with_mit" != no; then
  echo "error: must be yes or no: --with-mit-ext=$with_mit"
  exit 1
fi

dnl ---------------------------------------------------------------------------
dnl - Check for the XF86VMODE server extension (for gamma fading.)
dnl ---------------------------------------------------------------------------

have_xf86vmode=no
have_xf86gamma=no
have_xf86gamma_ramp=no
with_xf86gamma_req=unspecified
AC_ARG_WITH(xf86gamma-ext,
[  --with-xf86gamma-ext    Include support for XFree86 gamma fading.],
  [with_xf86gamma="$withval"; with_xf86gamma_req="$withval"],
  [with_xf86gamma=yes])

HANDLE_X_PATH_ARG(with_xf86gamma, --with-xf86gamma-ext, xf86gamma)

if test "$with_xf86gamma" = yes; then

  # first check for xf86vmode.h, if we haven't already
  if test "$have_xf86vmode" = yes; then
    have_xf86gamma=yes
  else
    AC_CHECK_X_HEADER(X11/extensions/xf86vmode.h, [have_xf86gamma=yes],,
                      [#include <X11/Xlib.h>])
  fi

  # if that succeeded, then check for the -lXxf86vm
  if test "$have_xf86gamma" = yes; then
    have_xf86gamma=no
    AC_CHECK_X_LIB(Xxf86vm, XF86VidModeSetGamma,
                  [have_xf86gamma=yes],
                   [true], -lXext -lX11)
  fi

  # check for the Ramp versions of the functions too.
  if test "$have_xf86gamma" = yes; then
    have_xf86gamma_ramp=no
    AC_CHECK_X_LIB(Xxf86vm, XF86VidModeSetGammaRamp,
                  [have_xf86gamma_ramp=yes],
                   [true], -lXext -lX11)
  fi

  # if those tests succeeded, then we've really got the functions.
  if test "$have_xf86gamma" = yes; then
    AC_DEFINE(HAVE_XF86VMODE_GAMMA, 1, [Define if XF86VMODE Gamma is available])
  fi

  if test "$have_xf86gamma_ramp" = yes; then
    AC_DEFINE(HAVE_XF86VMODE_GAMMA_RAMP, 1, [Define if XF86VMODE Gamma Ramp is available])
  fi

  # pull in the lib, if we haven't already
  if test "$have_xf86gamma" = yes -a "$have_xf86vmode" = no; then
    SAVER_LIBS="$SAVER_LIBS -lXxf86vm"
  fi

elif test "$with_xf86gamma" != no; then
  echo "error: must be yes or no: --with-xf86gamma-ext=$with_xf86vmode"
  exit 1
fi

dnl ---------------------------------------------------------------------------
dnl - Check for XF86MiscSetGrabKeysState (but only bother if we are already
dnl - using other XF86 stuff.)
dnl ---------------------------------------------------------------------------


have_xf86miscsetgrabkeysstate=no
if test "$have_xf86gamma" = yes -o "$have_xf86vmode" = yes; then
  AC_CHECK_X_LIB(Xxf86misc, XF86MiscSetGrabKeysState,
                [have_xf86miscsetgrabkeysstate=yes],
                [true], -lXext -lX11)
  if test "$have_xf86miscsetgrabkeysstate" = yes ; then
    SAVER_LIBS="$SAVER_LIBS -lXxf86misc"
    AC_DEFINE(HAVE_XF86MISCSETGRABKEYSSTATE, , [Define this if you have the XF86MiscSetGrabKeysState function])
  fi
fi

dnl ---------------------------------------------------------------------------
dnl - The --enable-locking option
dnl ---------------------------------------------------------------------------

AC_ARG_ENABLE(locking,[
Screen locking options:

  --enable-locking        Compile in support for locking the display.
  --disable-locking       Do not allow locking at all.],
  [enable_locking="$enableval"],[enable_locking=yes])
if test "$enable_locking" = yes; then
  true
elif test "$enable_locking" = no; then
  AC_DEFINE(NO_LOCKING, 1, [Define if screen locking support is disabled])
else
  echo "error: must be yes or no: --enable-locking=$enable_locking"
  exit 1
fi

# We can't lock on MacOS X, so don't compile in support for it.
#
if test "$ac_macosx" = yes; then
  if test "$enable_locking" = yes; then
    AC_MSG_RESULT(locking disabled: it doesn't work on MacOS X)
    enable_locking=no
    AC_DEFINE(NO_LOCKING, 1, [Define if screen locking support is disabled])
  fi
fi

dnl ---------------------------------------------------------------------------
dnl - Check for bsd_auth(3) (OpenBSD)
dnl ---------------------------------------------------------------------------

have_bsdauth=no
with_bsdauth_req=unspecified
NEED_SETUID=no

case "$host" in                                                                                       
  *-openbsd*)                                                                                         
    with_bsdauth=yes
    AUTH_SCHEME=bsdauth
    NEED_SETUID=no
    if test "x$enable_locking" = "xyes"; then
      with_bsdauth_req=yes
      NEED_SETUID=yes
    fi
esac  

if test "$with_bsdauth" = yes ; then
  AC_CACHE_CHECK([for BSD Authentication], ac_cv_bsdauth,
    [AC_TRY_X_COMPILE([#include <stdlib.h>
                       #include <unistd.h>
                       #include <sys/types.h>
                       #include <bsd_auth.h>],
      [int ok = auth_userokay("x", 0, "x", "x");],
      [ac_cv_bsdauth=yes],
      [ac_cv_bsdauth=no])])
  if test "$ac_cv_bsdauth" = yes; then
    have_bsdauth=yes
  fi
fi

if test "$have_bsdauth" = yes; then
  AC_DEFINE(HAVE_BSDAUTH, 1, [Define to 1 if using bsd_auth(3) authentication])
fi

AC_SUBST(NEED_SETUID)

dnl ---------------------------------------------------------------------------
dnl - Check for PAM
dnl ---------------------------------------------------------------------------

withval=""
AC_ARG_WITH(pam-prefix,
            AS_HELP_STRING([--with-pam-prefix=<prefix>],
                           [specify where pam files go]),
            [if test x$withval != x; then
               AC_MSG_RESULT("PAM files will be installed in prefix ${withval}.")
             fi])

if test x$withval != x; then
	PAM_PREFIX="$withval"
else
	PAM_PREFIX='${sysconfdir}'
fi
AC_SUBST(PAM_PREFIX)

have_pam=no
if test "x$enable_locking" = "xyes" -a "x$have_bsdauth" = "xno"; then
AC_CHECK_LIB(pam, pam_start, have_pam=yes)
fi

if test "x$have_pam" = "xyes"; then
        AUTH_LIBS="${AUTH_LIBS} -lpam"
        AUTH_SCHEME=pam
        # On Linux, sigtimedwait() is in libc; on Solaris, it's in librt.
        have_timedwait=no
        AC_CHECK_LIB(c, sigtimedwait, [have_timedwait=yes])
        if test "$have_timedwait" = no ; then
                AC_CHECK_LIB(rt, sigtimedwait, [AUTH_LIBS="${AUTH_LIBS} -lrt"])
        fi

        AC_MSG_CHECKING(how to call pam_strerror)
        AC_CACHE_VAL(ac_cv_pam_strerror_args,
         [AC_TRY_COMPILE([#include <stdio.h>
                          #include <stdlib.h>
                          #include <security/pam_appl.h>],
                         [pam_handle_t *pamh = 0;
                          char *s = pam_strerror(pamh, PAM_SUCCESS);],
                         [ac_pam_strerror_args=2],
                         [AC_TRY_COMPILE([#include <stdio.h>
                                          #include <stdlib.h>
                                          #include <security/pam_appl.h>],
                                         [char *s =
                                           pam_strerror(PAM_SUCCESS);],
                                         [ac_pam_strerror_args=1],
                                         [ac_pam_strerror_args=0])])
          ac_cv_pam_strerror_args=$ac_pam_strerror_args])
        ac_pam_strerror_args=$ac_cv_pam_strerror_args
        if test "$ac_pam_strerror_args" = 1 ; then
          AC_MSG_RESULT(one argument)
        elif test "$ac_pam_strerror_args" = 2 ; then
          AC_DEFINE(PAM_STRERROR_TWO_ARGS, 1, [Define if pam_strerror takes two arguments])
          AC_MSG_RESULT(two arguments)
        else
          AC_MSG_RESULT(unknown)
        fi

elif test "x$have_bsdauth" = "xno"; then
	AC_MSG_ERROR("PAM libraries not found")
fi
AC_SUBST(HAVE_PAM)
AC_SUBST(AUTH_LIBS)
AC_SUBST(AUTH_SCHEME)

if test "x$have_pam" = "xyes"; then
  AC_CHECK_HEADERS([security/pam_modutil.h security/pam_ext.h])
  AC_CHECK_LIB(pam, pam_syslog, [AC_DEFINE(HAVE_PAM_SYSLOG, [], [Define to 1 if you have the pam_syslog function])])
fi

dnl test whether struct pam_message is const (Linux) or not (Sun)
if test "x$have_pam" = "xyes"; then
   pam_appl_h="$ac_pam_includes/security/pam_appl.h"
   AC_MSG_CHECKING(for const pam_message)
   AC_EGREP_HEADER([struct pam_message],
      $pam_appl_h,
      [ AC_EGREP_HEADER([const struct pam_message],
                        $pam_appl_h,
                        [AC_MSG_RESULT(["const: Linux-type PAM"]) ],
                        [AC_MSG_RESULT(["nonconst: Sun-type PAM"])
                        AC_DEFINE(PAM_MESSAGE_NONCONST, 1, [Define if your PAM support takes non-const arguments (Solaris)])]
                        )],
       [AC_MSG_RESULT(["not found - assume const, Linux-type PAM"])]
       )
fi

dnl ---------------------------------------------------------------------------
dnl ConsoleKit
dnl ---------------------------------------------------------------------------

AC_ARG_WITH(console-kit,
            AS_HELP_STRING([--with-console-kit],
                           [Add ConsoleKit support]),,
            with_console_kit=auto)

use_console_kit=no
if test "x$with_console_kit" != "xno" ; then
        use_console_kit=yes
        AC_DEFINE(WITH_CONSOLE_KIT, 1, [ConsoleKit support])
fi
AM_CONDITIONAL(WITH_CONSOLE_KIT, test x$use_console_kit = xyes)
AC_SUBST(WITH_CONSOLE_KIT)

dnl ---------------------------------------------------------------------------
dnl systemd
dnl ---------------------------------------------------------------------------

AC_ARG_WITH(systemd,
            AS_HELP_STRING([--with-systemd],
                           [Add systemd support]),
            [with_systemd=$withval], [with_systemd=auto])

PKG_CHECK_MODULES(SYSTEMD,
                  [libsystemd-login libsystemd-daemon],
                  [have_systemd=yes], [have_systemd=no])

if test "x$with_systemd" = "xauto" ; then
        if test x$have_systemd = xno ; then
                use_systemd=no
        else
                use_systemd=yes
        fi
else
	use_systemd=$with_systemd
fi

if test "x$use_systemd" = "xyes"; then
        if test "x$have_systemd" = "xno"; then
                AC_MSG_ERROR([Systemd support explicitly required, but systemd not found])
        fi

        AC_DEFINE(WITH_SYSTEMD, 1, [systemd support])
fi
AC_SUBST(SYSTEMD_CFLAGS)
AC_SUBST(SYSTEMD_LIBS)

dnl ---------------------------------------------------------------------------
dnl libgnomekbd
dnl ---------------------------------------------------------------------------

have_libgnomekbdui=no
AC_ARG_WITH(kbd-layout-indicator,[  --without-kbd-layout-indicator         disable keyboard layout indicator],
            [with_kbd_layout_indicator="$withval"],[with_kbd_layout_indicator=yes])
if test x$with_kbd_layout_indicator != xno; then
  PKG_CHECK_MODULES(LIBGNOMEKBDUI, libgnomekbdui >= $LIBGNOMEKBDUI_REQUIRED_VERSION libxklavier, have_libgnomekbdui=yes, have_libgnomekbdui=no)
fi
if test "x$have_libgnomekbdui" = "xyes"; then
  AC_SUBST(LIBGNOMEKBDUI_CFLAGS)
  AC_SUBST(LIBGNOMEKBDUI_LIBS)
  AC_DEFINE(WITH_KBD_LAYOUT_INDICATOR, 1, [Define if keyboard layout indicator should be built])
fi

dnl ---------------------------------------------------------------------------
dnl Finish
dnl ---------------------------------------------------------------------------

AC_SUBST(AUTH_LIBS)
AC_SUBST(SAVER_LIBS)


REAL_PREFIX=
if test "x$prefix" = "xNONE"; then
  REAL_PREFIX=$ac_default_prefix
else
  REAL_PREFIX=$prefix
fi

## temporarily change prefix and exec_prefix
old_prefix=$prefix
prefix=$REAL_PREFIX

if test "x$exec_prefix" = xNONE ; then
   REAL_EXEC_PREFIX=$REAL_PREFIX
else
   REAL_EXEC_PREFIX=$exec_prefix
fi
old_exec_prefix=$exec_prefix
exec_prefix=$REAL_EXEC_PREFIX

## eval everything
LOCALSTATEDIR_TMP="$localstatedir"
EXPANDED_LOCALSTATEDIR=`eval echo $LOCALSTATEDIR_TMP`
AC_SUBST(EXPANDED_LOCALSTATEDIR)

SYSCONFDIR_TMP="$sysconfdir"
EXPANDED_SYSCONFDIR=`eval echo $SYSCONFDIR_TMP`
AC_SUBST(EXPANDED_SYSCONFDIR)

BINDIR_TMP="$bindir"
EXPANDED_BINDIR=`eval echo $BINDIR_TMP`
AC_SUBST(EXPANDED_BINDIR)

LIBDIR_TMP="$libdir"
EXPANDED_LIBDIR=`eval echo $LIBDIR_TMP`
AC_SUBST(EXPANDED_LIBDIR)

DATADIR_TMP="$datadir"
EXPANDED_DATADIR=`eval echo $DATADIR_TMP`
AC_SUBST(EXPANDED_DATADIR)

## put prefix and exec_prefix back
prefix=$old_prefix
exec_prefix=$old_exec_prefix

# Turn on the additional warnings last, so -Werror doesn't affect other tests.

AC_ARG_ENABLE(more-warnings,
  AC_HELP_STRING([--enable-more-warnings], [Maximum compiler warnings]),
set_more_warnings="$enableval",[
if test -d $srcdir/.git; then
        is_cvs_version=true
        set_more_warnings=yes
else
        set_more_warnings=no
fi
])
AC_MSG_CHECKING(for more warnings)
if test "$GCC" = "yes" -a "$set_more_warnings" != "no"; then
        AC_MSG_RESULT(yes)
        CFLAGS="\
        -Wall \
        -Wchar-subscripts -Wmissing-declarations -Wmissing-prototypes \
        -Wnested-externs -Wpointer-arith \
        -Wcast-align -Wsign-compare \
        $CFLAGS"

        for option in -Wno-strict-aliasing -Wno-sign-compare; do
                SAVE_CFLAGS="$CFLAGS"
                CFLAGS="$CFLAGS $option"
                AC_MSG_CHECKING([whether gcc understands $option])
                AC_TRY_COMPILE([], [],
                        has_option=yes,
                        has_option=no,)
                if test $has_option = no; then
                        CFLAGS="$SAVE_CFLAGS"
                fi
                AC_MSG_RESULT($has_option)
                unset has_option
                unset SAVE_CFLAGS
        done
        unset option
else
        AC_MSG_RESULT(no)
fi

#
# Enable Debug
#
AC_ARG_ENABLE(debug, [AC_HELP_STRING([--enable-debug=[no/yes]], [turn on debugging])],, enable_debug=yes)
if test "$enable_debug" = "yes"; then
  DEBUG_CFLAGS="-DG_ENABLE_DEBUG"
else
  if test "x$enable_debug" = "xno"; then
    DEBUG_CFLAGS="-DG_DISABLE_ASSERT -DG_DISABLE_CHECKS"
  else
    DEBUG_CFLAGS=""
  fi
fi
AC_SUBST(DEBUG_CFLAGS)

# Flags

AC_SUBST(CFLAGS)
AC_SUBST(CPPFLAGS)
AC_SUBST(LDFLAGS)

# Files

AC_OUTPUT([
Makefile
po/Makefile.in
src/Makefile
src/gnome-screensaver.desktop.in
data/Makefile
doc/Makefile
doc/gnome-screensaver.xml
])

echo "
                    gnome-screensaver $VERSION
                    ========================

	prefix:                   ${prefix}
	exec_prefix:              ${exec_prefix}
        libdir:                   ${EXPANDED_LIBDIR}
        bindir:                   ${EXPANDED_BINDIR}
        sysconfdir:               ${EXPANDED_SYSCONFDIR}
        localstatedir:            ${EXPANDED_LOCALSTATEDIR}
        datadir:                  ${EXPANDED_DATADIR}
	source code location:	  ${srcdir}
	compiler:		  ${CC}
	cflags:		          ${CFLAGS}
        Base libs:                ${GNOME_SCREENSAVER_LIBS}
        Extension libs:           ${SAVER_LIBS}
        Maintainer mode:          ${USE_MAINTAINER_MODE}
        Docs enabled:             ${enable_docbook_docs}

        Screen locking enabled:   ${enable_locking}
        Show keyboard indicator:  ${with_kbd_layout_indicator}

        systemd:                  ${use_systemd}
        ConsoleKit:               ${use_console_kit}
"

if test "x$have_pam" = "xyes" ; then
echo "\
        PAM prefix:               ${PAM_PREFIX}

"
fi

