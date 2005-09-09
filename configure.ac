dnl -*- mode: m4 -*-

AC_INIT([gnome-screensaver],
        [0.0.13],
        [http://bugzilla.gnome.org/enter_bug.cgi?product=gnome-screensaver])

AC_CONFIG_SRCDIR(src/gnome-screensaver.c)

AM_INIT_AUTOMAKE
AM_CONFIG_HEADER(config.h)

AM_MAINTAINER_MODE

AC_PROG_INTLTOOL([0.29])
AC_ISC_POSIX
AC_PROG_CC
AC_STDC_HEADERS
AC_PROG_LIBTOOL

AC_HEADER_STDC

AC_SUBST(VERSION)

# Save flags to aclocal
ACLOCAL="$ACLOCAL $ACLOCAL_FLAGS"

GETTEXT_PACKAGE=gnome-screensaver
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", [Name of default gettext domain])

ALL_LINGUAS="bg de en_CA en_GB es fi fr ja pt_BR sv uk zh_CN zh_TW"
AM_GLIB_GNU_GETTEXT

# Custom Junk

AC_DEFUN([AC_DEFINE_PATH], [
  test "x$prefix" = xNONE && prefix="$ac_default_prefix"
  test "x$exec_prefix" = xNONE && exec_prefix='${prefix}'
  ac_define_path=`eval echo [$]$2`
  ac_define_path=`eval echo [$]ac_define_path`
  $1="$ac_define_path"
  AC_SUBST($1)
  ifelse($3, ,
    AC_DEFINE_UNQUOTED($1, "$ac_define_path"),
    AC_DEFINE_UNQUOTED($1, "$ac_define_path", $3))
])

# Dependencies

DBUS_REQUIRED_VERSION=0.30
LIBXML_REQUIRED_VERSION=2.6.0
GCONF_REQUIRED_VERSION=2.6.1
GDK_REQUIRED_VERSION=2.7.0
GTK_REQUIRED_VERSION=2.7.0
GNOME_VFS_REQUIRED_VERSION=2.6.0
LIBGNOMEUI_REQUIRED_VERSION=2.6.0
GLADE_REQUIRED_VERSION=2.5.0

AC_CHECK_HEADERS(unistd.h)
AC_CHECK_HEADERS(crypt.h sys/select.h)
AC_CHECK_FUNCS(select fcntl uname nice setpriority getcwd getwd putenv sbrk)
AC_CHECK_FUNCS(sigaction syslog realpath setrlimit)

AC_CHECK_FUNCS([setresuid setenv unsetenv clearenv])
PKG_CHECK_MODULES(GNOME_SCREENSAVER,
        gtk+-2.0 >= $GTK_REQUIRED_VERSION
        libxml-2.0 >= $LIBXML_REQUIRED_VERSION
        dbus-glib-1 >= $DBUS_REQUIRED_VERSION
        gconf-2.0 >= $GCONF_REQUIRED_VERSION)
AC_SUBST(GNOME_SCREENSAVER_CFLAGS)
AC_SUBST(GNOME_SCREENSAVER_LIBS)

PKG_CHECK_MODULES(GNOME_SCREENSAVER_DIALOG,
        gtk+-2.0 >= $GTK_REQUIRED_VERSION)
AC_SUBST(GNOME_SCREENSAVER_DIALOG_CFLAGS)
AC_SUBST(GNOME_SCREENSAVER_DIALOG_LIBS)

PKG_CHECK_MODULES(GNOME_SCREENSAVER_CAPPLET,
	libgnomeui-2.0 >= $LIBGNOMEUI_REQUIRED_VERSION
        libglade-2.0 >= $GLADE_REQUIRED_VERSION
        gtk+-2.0 >= $GTK_REQUIRED_VERSION)
AC_SUBST(GNOME_SCREENSAVER_CAPPLET_CFLAGS)
AC_SUBST(GNOME_SCREENSAVER_CAPPLET_LIBS)

PKG_CHECK_MODULES(GNOME_SCREENSAVER_COMMAND,
        dbus-glib-1 >= $DBUS_REQUIRED_VERSION)
AC_SUBST(GNOME_SCREENSAVER_COMMAND_CFLAGS)
AC_SUBST(GNOME_SCREENSAVER_COMMAND_LIBS)

PKG_CHECK_MODULES(GNOME_SCREENSAVER_SAVER,
        gthread-2.0
        gtk+-2.0 >= $GTK_REQUIRED_VERSION)
AC_SUBST(GNOME_SCREENSAVER_SAVER_CFLAGS)
AC_SUBST(GNOME_SCREENSAVER_SAVER_LIBS)

AC_PATH_XTRA
AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)
AC_PATH_PROG(GCONFTOOL, gconftool-2)

AM_GCONF_SOURCE_2

GNOME_COMPILE_WARNINGS(yes)

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

# Determine PAM prefix

withval=""
AC_ARG_WITH(pam-prefix,
[  --with-pam-prefix=<prefix>   specify where pam files go],[
if test x$withval != x; then
   AC_MSG_RESULT("PAM files will be installed in prefix ${withval}.")
fi])
if test x$withval != x; then
	PAM_PREFIX_UNEXPANDED="$withval"
else
	PAM_PREFIX_UNEXPANDED="$sysconfdir"
fi
PAM_PREFIX=`eval echo $PAM_PREFIX_UNEXPANDED`
AC_SUBST(PAM_PREFIX)

# Fast User Switching

PKG_CHECK_MODULES(FAST_USER_SWITCH,
        gtk+-2.0 >= $GTK_REQUIRED_VERSION
        gnome-vfs-2.0 >= $GNOME_VFS_REQUIRED_VERSION)

AC_SUBST(FAST_USER_SWITCH_CFLAGS)
AC_SUBST(FAST_USER_SWITCH_LIBS)

# GDM Config File

GDMCONFIGFILE=

AC_ARG_WITH([gdm_config],
	    AC_HELP_STRING([--with-gdm-config=file],
			   [The location of the GDM configuration file]),
	    [gdmconfigfile="$withval"])

if test "x$gdmconfigfile" == "x" -o "x$gdmconfigfile" == "xno"; then
	AC_MSG_CHECKING([for gdm.conf])

	if test -f "/etc/gdm/gdm.conf"; then
		gdmconfigfile="/etc/gdm/gdm.conf"
	elif test -f "/etc/X11/gdm/gdm.conf"; then
		gdmconfigfile="/etc/X11/gdm/gdm.conf"
	elif test -f "/etc/opt/gnome/gdm/gdm.conf"; then
		gdmconfigfile="/etc/opt/gnome/gdm/gdm.conf"
	else
		gdmconfigfile=""
	fi

	AC_MSG_RESULT([$gdmconfigfile])
else
	AC_MSG_RESULT([using "$gdmconfigfile" as the gdm.conf location])
fi

AC_DEFINE_PATH(GDMCONFIGFILE, "$gdmconfigfile", [The location of the GDM configuration file])

enable_user_switching=no
if test "x$gdmconfigfile" != "x" -a "x$gdmconfigfile" != "xno"; then
	enable_user_switching=yes
	AC_DEFINE(HAVE_USER_SWITCHING, 1, [Define if user switching is available])
fi

AM_CONDITIONAL(HAVE_USER_SWITCHING, test x$enable_user_switching = xyes)
AC_SUBST(HAVE_USER_SWITCHING)

###############################################################################
#
#       Functions to check for various X11 things
#
###############################################################################

AC_DEFUN(AC_XPOINTER,
 [AC_CACHE_CHECK([for XPointer], ac_cv_xpointer,
                 [AC_TRY_X_COMPILE([#include <X11/Xlib.h>],
                                   [XPointer foo = (XPointer) 0;],
                                   [ac_cv_xpointer=yes],
                                   [ac_cv_xpointer=no])])
  if test "$ac_cv_xpointer" != yes; then
   AC_DEFINE(XPointer,[char*])
  fi])


# Random special-cases for X on certain pathological OSes.

AC_DEFUN(AC_X_RANDOM_PATHS,
 [case "$host" in
    *-hpux*)

      # The following arcana was gleaned from conversations with
      # Eric Schwartz <erics@col.hp.com>:
      #
      # On HPUX 10.x, the parts of X that HP considers "standard" live in
      # /usr/{include,lib}/X11R6/.  The parts that HP doesn't consider
      # "standard", notably, Xaw and Xmu, live in /usr/contrib/X11R6/.
      # Yet /usr/contrib/X11R6/ comes preinstalled on all HPUX systems.
      # Also, there are symlinks from /usr/include/ and /usr/lib/ into
      # /usr/{include,lib}/X11R6/, so that (if you don't use Xmu at all)
      # you don't need any -I or -L arguments.
      #
      # On HPUX 9.x, /usr/{include,lib}/X11R5/ and /usr/contrib/X11R5/
      # are the same division as 10.x.  However, there are no symlinks to
      # the X stuff from /usr/include/ and /usr/lib/, so -I and -L
      # arguments are always necessary.
      #
      # However, X11R6 was available on HPUX 9.x as a patch: if that
      # patch was installed, then all of X11R6 went in to
      # /usr/contrib/X11R6/ (there was no /usr/{include,lib}/X11R6/.)
      #
      # HPUX 8.x was the same as 9.x, but was X11R4 instead (I don't know
      # whether R5 was available as a patch; R6 undoubtedly was not.)
      #
      # So.  We try and use the highest numbered pair of
      # /usr/{include,lib}/X11R?/ and /usr/contrib/X11R?/{include,lib}/
      # that are available.  We do not mix and match different versions
      # of X.
      #
      # Question I still don't know the answer to: (do you?)
      #
      #   * On HPUX 9.x, where /usr/include/X11R5/ was standard, and
      #     /usr/contrib/X11R6/ could be installed as a patch, what was in
      #     that contrib directory?  Did it contain so-called "standard"
      #     X11R6, or did it include Xaw and Xmu as well?  If the former,
      #     where did one find Xaw and Xmu on 9.x R6 systems?  Would this
      #     be a situation where one had to reach into the R5 headers and
      #     libs to find Xmu?  That is, must both R6 and R5 directories
      #     be on the -I and -L lists in that case?
      #
      for version in X11R6 X11R5 X11R4 ; do
        # if either pair of directories exists...
        if test -d /usr/include/$version || test -d /usr/contrib/$version/include
        then
           # if contrib exists, use it...
           if test -d /usr/contrib/$version/include ; then
             X_CFLAGS="$X_CFLAGS -I/usr/contrib/$version/include"
             X_LIBS="$X_LIBS -L/usr/contrib/$version/lib"
           fi
           # if the "standard" one exists, use it.
           if test -d /usr/include/$version ; then
             X_CFLAGS="$X_CFLAGS -I/usr/include/$version"
             X_LIBS="$X_LIBS -L/usr/lib/$version"
           fi
           # since at least one of the pair exists, go no farther.
           break
        fi
      done

      # Now find Motif.  Thanks for not making xmkmf find this by
      # default, you losers.
      #
      if test -d /usr/include/Motif2.1 ; then
        X_CFLAGS="$X_CFLAGS -I/usr/include/Motif2.1"
        X_LIBS="$X_LIBS -L/usr/lib/Motif2.1"
      elif test -d /usr/include/Motif1.2 ; then
        X_CFLAGS="$X_CFLAGS -I/usr/include/Motif1.2"
        X_LIBS="$X_LIBS -L/usr/lib/Motif1.2"
      elif test -d /usr/include/Motif1.1 ; then
        X_CFLAGS="$X_CFLAGS -I/usr/include/Motif1.1"
        X_LIBS="$X_LIBS -L/usr/lib/Motif1.1"
      fi

      # Now let's check for the pseudo-standard locations for OpenGL and XPM.
      #
      if test -d /opt/graphics/OpenGL/include ; then
        # HP-UX 10.20 puts it here
        X_CFLAGS="-I/opt/graphics/OpenGL/include $X_CFLAGS"
        X_LIBS="-L/opt/graphics/OpenGL/lib $X_LIBS"
      elif test -d /opt/Mesa/lib ; then
        X_CFLAGS="-I/opt/Mesa/include $X_CFLAGS"
        X_LIBS="-L/opt/Mesa/lib $X_LIBS"
      fi


      if test -d /opt/xpm/lib/X11 ; then
        X_CFLAGS="-I/opt/xpm/include $X_CFLAGS"
        X_LIBS="-L/opt/xpm/lib/X11 $X_LIBS"
      fi

      # On HPUX, default to installing in /opt/xscreensaver/ instead of
      # in /usr/local/, unless there is already an xscreensaver in
      # /usr/local/bin/.  This can be overridden with the --prefix arg
      # to configure.  I'm not sure this is the right thing to do, but
      # Richard Lloyd says so...
      #
      if test \! -x /usr/local/bin/xscreensaver ; then
        ac_default_prefix=/opt/xscreensaver
      fi

    ;;
    *-solaris*)

      # Thanks for not making xmkmf find this by default, pinheads.
      # And thanks for moving things around again, too.  Is this
      # really the standard location now?  What happened to the
      # joke that this kind of thing went in /opt?
      # cthomp says "answer: CDE (Common Disorganized Environment)"
      #
      if test -f /usr/dt/include/Xm/Xm.h ; then
        X_CFLAGS="$X_CFLAGS -I/usr/dt/include"
        MOTIF_LIBS="$MOTIF_LIBS -L/usr/dt/lib -R/usr/dt/lib"

        # Some versions of Slowlaris Motif require -lgen.  But not all.  Why?
        AC_CHECK_LIB(gen, regcmp, [MOTIF_LIBS="$MOTIF_LIBS -lgen"])
      fi

    ;;
    *-darwin*)

      # On MacOS X (10.x with "fink"), many things are under /sw/.
      #
      if test -d /sw/include ; then
        X_CFLAGS="-I/sw/include $X_CFLAGS"
        X_LIBS="-L/sw/lib $X_LIBS"
      fi
    ;;
  esac])


###############################################################################
#
#       Some utility functions to make checking for X things easier.
#
###############################################################################

# Like AC_CHECK_HEADER, but it uses the already-computed -I directories.
#
AC_DEFUN(AC_CHECK_X_HEADER, [
  ac_save_CPPFLAGS="$CPPFLAGS"
  if test \! -z "$includedir" ; then 
    CPPFLAGS="$CPPFLAGS -I$includedir"
  fi
  CPPFLAGS="$CPPFLAGS $X_CFLAGS"
  AC_CHECK_HEADER([$1],[$2],[$3],[$4])
  CPPFLAGS="$ac_save_CPPFLAGS"])

# Like AC_EGREP_HEADER, but it uses the already-computed -I directories.
#
AC_DEFUN(AC_EGREP_X_HEADER, [
  ac_save_CPPFLAGS="$CPPFLAGS"
  if test \! -z "$includedir" ; then 
    CPPFLAGS="$CPPFLAGS -I$includedir"
  fi
  CPPFLAGS="$CPPFLAGS $X_CFLAGS"
  AC_EGREP_HEADER([$1], [$2], [$3], [$4])
  CPPFLAGS="$ac_save_CPPFLAGS"])

# Like AC_TRY_COMPILE, but it uses the already-computed -I directories.
#
AC_DEFUN(AC_TRY_X_COMPILE, [
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
AC_DEFUN(AC_CHECK_X_LIB, [
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
  LDFLAGS="$LDFLAGS $X_LIBS $X_EXTRA_LIBS"

  AC_CHECK_LIB([$1], [$2], [$3], [$4], [$5])
  CPPFLAGS="$ac_save_CPPFLAGS"
  LDFLAGS="$ac_save_LDFLAGS"
#  LIBS="$ac_save_LIBS"
  ])

# Like AC_TRY_RUN, but it uses the already-computed -I directories.
# (But not the -L directories!)
#
AC_DEFUN(AC_TRY_X_RUN, [
  ac_save_CPPFLAGS="$CPPFLAGS"
  if test \! -z "$includedir" ; then 
    CPPFLAGS="$CPPFLAGS -I$includedir"
  fi
  CPPFLAGS="$CPPFLAGS $X_CFLAGS"
  AC_TRY_RUN([$1], [$2], [$3], [$4])
  CPPFLAGS="$ac_save_CPPFLAGS"])



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
AC_DEFUN(HANDLE_X_PATH_ARG, [
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


###############################################################################
#
#       Check for the SGI SCREEN_SAVER server extension.
#
###############################################################################

have_sgi=no
with_sgi_req=unspecified
AC_ARG_WITH(sgi-ext,
[Except where noted, all of the --with options below can also take a
directory argument: for example, `--with-motif=/opt/Motif'.  That would
cause /opt/Motif/include/ to be added to the -I list, and /opt/Motif/lib/
to be added to the -L list, assuming those directories exist.  

By default, support for each of these options will be built in, if the
relevant library routines exist.  At run time, they will then be used
only if the X server being used supports them.  Each --with option has
a corresponding --without option, to override building support for them
at all.

Screen blanking and idle-detection options:

  --with-sgi-ext          Include support for the SGI SCREEN_SAVER extension.],
  [with_sgi="$withval"; with_sgi_req="$withval"],[with_sgi=yes])

HANDLE_X_PATH_ARG(with_sgi, --with-sgi-ext, SGI SCREEN_SAVER)

if test "$with_sgi" = yes; then
  AC_CHECK_X_HEADER(X11/extensions/XScreenSaver.h,
                    [have_sgi=yes
                     AC_DEFINE(HAVE_SGI_SAVER_EXTENSION, 1, [Define if the SGI screensaver extension is available])],,
                    [#include <X11/Xlib.h>])

elif test "$with_sgi" != no; then
  echo "error: must be yes or no: --with-sgi-ext=$with_sgi"
  exit 1
fi


###############################################################################
#
#       Check for the MIT-SCREEN-SAVER server extension.
#
###############################################################################

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
  # scrnsaver.h, but doesn't include the code in libXext.a, the idiots!
  #
  if test "$have_mit" = yes; then
    AC_CHECK_X_LIB(Xext, XScreenSaverRegister, [true], [have_mit=no], -lm)

    if test "$have_mit" = no; then
      # Fuck!  Looks like XF86Free-3.3 actually puts it in XExExt instead
      # of in Xext.  Thank you master, may I have another.
      AC_CHECK_X_LIB(XExExt, XScreenSaverRegister,
                     [have_mit=yes; SAVER_LIBS="$SAVER_LIBS -lXExExt"],
                     [true], -lX11 -lXext -lm)
    fi

    if test "$have_mit" = no; then
      # Double fuck!  Looks like some versions of XFree86 (whichever version
      # it is that comes with RedHat Linux 2.0 -- I can't find a version 
      # number) put this garbage in Xss instead of Xext.  Thank you master,
      #  may I have another.
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


###############################################################################
#
#       Check for the XIDLE server extension.
#
###############################################################################

have_xidle=no
with_xidle_req=unspecified
AC_ARG_WITH(xidle-ext,
[  --with-xidle-ext        Include support for the XIDLE extension.],
  [with_xidle="$withval"; with_xidle_req="$withval"],[with_xidle=yes])

HANDLE_X_PATH_ARG(with_xidle, --with-xidle-ext, XIDLE)

if test "$with_xidle" = yes; then
  AC_CHECK_X_HEADER(X11/extensions/xidle.h,
                    [have_xidle=yes
                     AC_DEFINE(HAVE_XIDLE_EXTENSION, 1, [Define if the XIDLE extension is available])],,
                    [#include <X11/Xlib.h>])
elif test "$with_xidle" != no; then
  echo "error: must be yes or no: --with-xidle-ext=$with_xidle"
  exit 1
fi


###############################################################################
#
#       Check for the SGI-VIDEO-CONTROL server extension.
#
###############################################################################

have_sgivc=no
with_sgivc_req=unspecified
AC_ARG_WITH(sgivc-ext,
[  --with-sgivc-ext        Include support for the SGI-VIDEO-CONTROL extension.],
  [with_sgivc="$withval"; with_sgivc_req="$withval"],[with_sgivc=yes])

HANDLE_X_PATH_ARG(with_sgivc, --with-sgivc-ext, SGI-VIDEO-CONTROL)

if test "$with_sgivc" = yes; then

  # first check for XSGIvc.h
  AC_CHECK_X_HEADER(X11/extensions/XSGIvc.h, [have_sgivc=yes],,
                    [#include <X11/Xlib.h>])

  # if that succeeded, then check for the -lXsgivc
  if test "$have_sgivc" = yes; then
    have_sgivc=no
    AC_CHECK_X_LIB(Xsgivc, XSGIvcQueryGammaMap,
                  [have_sgivc=yes; SAVER_LIBS="$SAVER_LIBS -lXsgivc"], [true],
                  -lXext -lX11)
  fi

  # if that succeeded, then we've really got it.
  if test "$have_sgivc" = yes; then
    AC_DEFINE(HAVE_SGI_VC_EXTENSION, 1, [Define if the SGI VC extension is available])
  fi

elif test "$with_sgivc" != no; then
  echo "error: must be yes or no: --with-sgivc-ext=$with_sgivc"
  exit 1
fi


###############################################################################
#
#       Check for the DPMS server extension.
#
###############################################################################

have_dpms=no
with_dpms_req=unspecified
AC_ARG_WITH(dpms-ext,
[  --with-dpms-ext         Include support for the DPMS extension.],
  [with_dpms="$withval"; with_dpms_req="$withval"],[with_dpms=yes])

HANDLE_X_PATH_ARG(with_dpms, --with-dpms-ext, DPMS)

if test "$with_dpms" = yes; then

  # first check for dpms.h
  AC_CHECK_X_HEADER(X11/extensions/dpms.h, [have_dpms=yes],,
                    [#include <X11/Xlib.h>
		     #include <X11/Xmd.h>])

  # if that succeeded, then check for the DPMS code in the libraries
  if test "$have_dpms" = yes; then

    # first look in -lXext (this is where it is with XFree86 4.0)
    have_dpms=no
    AC_CHECK_X_LIB(Xext, DPMSInfo, [have_dpms=yes], [true], -lXext -lX11)

    # if that failed, look in -lXdpms (this is where it was in XFree86 3.x)
    if test "$have_dpms" = no; then
      AC_CHECK_X_LIB(Xdpms, DPMSInfo,
                    [have_dpms=yes; XDPMS_LIBS="-lXdpms"], [true],
                    -lXext -lX11)
    fi
  fi


  # if that succeeded, then we've really got it.
  if test "$have_dpms" = yes; then
    AC_DEFINE(HAVE_DPMS_EXTENSION, 1, [Define if the DPMS extension is available])
  fi

elif test "$with_dpms" != no; then
  echo "error: must be yes or no: --with-dpms-ext=$with_dpms"
  exit 1
fi


###############################################################################
#
#       Check for the XINERAMA server extension.
#
###############################################################################

have_xinerama=no
with_xinerama_req=unspecified
AC_ARG_WITH(xinerama-ext,
[  --with-xinerama-ext     Include support for the XINERAMA extension.],
  [with_xinerama="$withval"; with_xinerama_req="$withval"],[with_xinerama=yes])

HANDLE_X_PATH_ARG(with_xinerama, --with-xinerama-ext, XINERAMA)

if test "$with_xinerama" = yes; then

  # first check for Xinerama.h
  AC_CHECK_X_HEADER(X11/extensions/Xinerama.h, [have_xinerama=yes],,
                    [#include <X11/Xlib.h>])

  # if that succeeded, then check for the XINERAMA code in the libraries
  if test "$have_xinerama" = yes; then

    # first look in -lXext
    have_xinerama=no
    AC_CHECK_X_LIB(Xext, XineramaQueryScreens, [have_xinerama=yes], [true],
                  -lXext -lX11)

    # if that failed, look in -lXinerama (this is where it is in XFree86 4.1.)
    if test "$have_xinerama" = no; then
      AC_CHECK_X_LIB(Xinerama, XineramaQueryScreens,
                     [have_xinerama=yes; SAVER_LIBS="$SAVER_LIBS -lXinerama"],
                     [true], -lXext -lX11)
    fi
  fi

  # if that succeeded, then we've really got it.
  if test "$have_xinerama" = yes; then
    AC_DEFINE(HAVE_XINERAMA, 1, [Define if XINERAMA is available])
  fi

elif test "$with_xinerama" != no; then
  echo "error: must be yes or no: --with-xinerama-ext=$with_xinerama"
  exit 1
fi


###############################################################################
#
#       Check for the XF86VMODE server extension (for virtual screens.)
#
###############################################################################

have_xf86vmode=no
with_xf86vmode_req=unspecified
AC_ARG_WITH(xf86vmode-ext,
[  --with-xf86vmode-ext    Include support for XFree86 virtual screens.],
  [with_xf86vmode="$withval"; with_xf86vmode_req="$withval"],
  [with_xf86vmode=yes])

HANDLE_X_PATH_ARG(with_xf86vmode, --with-xf86vmode-ext, xf86vmode)

VIDMODE_LIBS=""

if test "$with_xf86vmode" = yes; then

  # first check for xf86vmode.h
  AC_CHECK_X_HEADER(X11/extensions/xf86vmode.h, [have_xf86vmode=yes],,
                    [#include <X11/Xlib.h>])

  # if that succeeded, then check for the -lXxf86vm
  if test "$have_xf86vmode" = yes; then
    have_xf86vmode=no
    AC_CHECK_X_LIB(Xxf86vm, XF86VidModeGetViewPort,
                  [have_xf86vmode=yes;
                   VIDMODE_LIBS="-lXxf86vm";
                   SAVER_LIBS="$SAVER_LIBS $VIDMODE_LIBS"],
                   [true], -lXext -lX11)
  fi

  # if that succeeded, then we've really got it.
  if test "$have_xf86vmode" = yes; then
    AC_DEFINE(HAVE_XF86VMODE, 1, [Define if XF86VMODE is available])
  fi

elif test "$with_xf86vmode" != no; then
  echo "error: must be yes or no: --with-xf86vmode-ext=$with_xf86vmode"
  exit 1
fi


###############################################################################
#
#       Check for the XF86VMODE server extension (for gamma fading.)
#
###############################################################################

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


###############################################################################
#
#       Check for the RANDR (Resize and Rotate) server extension.
#
#       We need this to detect when the resolution of the desktop
#       has changed out from under us (this is a newer, different
#       mechanism than the XF86VMODE virtual viewports.)
#
###############################################################################

have_randr=no
with_randr_req=unspecified
AC_ARG_WITH(randr-ext,
[  --with-randr-ext        Include support for the X Resize+Rotate extension.],
  [with_randr="$withval"; with_randr_req="$withval"],[with_randr=yes])

HANDLE_X_PATH_ARG(with_randr, --with-randr-ext, RANDR)

if test "$with_randr" = yes; then

  # first check for Randr.h
  AC_CHECK_X_HEADER(X11/extensions/Xrandr.h, [have_randr=yes],,
                    [#include <X11/Xlib.h>])

  # if that succeeded, then check for the XRR code in the libraries
  if test "$have_randr" = yes; then

    # RANDR probably needs -lXrender
    xrender_libs=
    AC_CHECK_X_LIB(Xrender, XRenderSetSubpixelOrder,
                   [xrender_libs="-lXrender"], [true], -lXext -lX11)

    # first look for RANDR in -lXext
    have_randr=no
    AC_CHECK_X_LIB(Xext, XRRGetScreenInfo,
                   [have_randr=yes; SAVER_LIBS="$SAVER_LIBS $xrender_libs"],
                   [true], $xrender_libs -lXext -lX11)

    # if that failed, look in -lXrandr
    if test "$have_randr" = no; then
      AC_CHECK_X_LIB(Xrandr, XRRGetScreenInfo,
             [have_randr=yes; SAVER_LIBS="$SAVER_LIBS -lXrandr $xrender_libs"],
                     [true], $xrender_libs -lXext -lX11)
    fi
  fi

  # if that succeeded, then we've really got it.
  if test "$have_randr" = yes; then
    AC_DEFINE(HAVE_RANDR, 1, [Define if the RANDR extension is available])
  fi

elif test "$with_randr" != no; then
  echo "error: must be yes or no: --with-randr-ext=$with_randr"
  exit 1
fi


###############################################################################
#
#       Check for XF86MiscSetGrabKeysState (but only bother if we are already
#	using other XF86 stuff.)
#
###############################################################################

have_xf86miscsetgrabkeysstate=no
if test "$have_xf86gamma" = yes -o "$have_xf86vmode" = yes; then
  AC_CHECK_X_LIB(Xxf86misc, XF86MiscSetGrabKeysState,
                [have_xf86miscsetgrabkeysstate=yes],
                [true], -lXext -lX11)
  if test "$have_xf86miscsetgrabkeysstate" = yes ; then
    SAVER_LIBS="$SAVER_LIBS -lXxf86misc"
    AC_DEFINE(HAVE_XF86MISCSETGRABKEYSSTATE, 1, [Define if XF86MiscSetGrabKeyState is available])
  fi
fi


###############################################################################
#
#       Check for HP XHPDisableReset and XHPEnableReset.
#
###############################################################################

AC_MSG_CHECKING([for XHPDisableReset in X11/XHPlib.h])
AC_EGREP_X_HEADER(XHPDisableReset, X11/XHPlib.h,
                  [AC_DEFINE(HAVE_XHPDISABLERESET, 1, [Define if XHPDisableReset is available])
                   SAVER_LIBS="-lXhp11 $SAVER_LIBS"
                   AC_MSG_RESULT(yes)],
                  [AC_MSG_RESULT(no)])


###############################################################################
#
#       Check for /proc/interrupts.
#
###############################################################################

have_proc_interrupts=no
with_proc_interrupts_req=unspecified
AC_ARG_WITH(proc-interrupts,
[  --with-proc-interrupts  Include support for consulting the /proc/interrupts
                          file to notice keyboard activity.],
  [with_proc_interrupts="$withval"; with_proc_interrupts_req="$withval"],
  [with_proc_interrupts=yes])

if test "$with_proc_interrupts" = yes; then

   AC_CACHE_CHECK([whether /proc/interrupts contains keyboard data],
    ac_cv_have_proc_interrupts,
    [ac_cv_have_proc_interrupts=no
     if grep keyboard /proc/interrupts >/dev/null 2>&1 ; then
       ac_cv_have_proc_interrupts=yes
     fi
    ])
   have_proc_interrupts=$ac_cv_have_proc_interrupts

  if test "$have_proc_interrupts" = yes; then
    AC_DEFINE(HAVE_PROC_INTERRUPTS, 1, [Define if /proc/interrupts are available])
  fi

elif test "$with_proc_interrupts" != no; then
  echo "error: must be yes or no: --with-proc-interrupts=$with_proc_interrupts"
  exit 1
fi


###############################################################################
#
#       The --enable-locking option
#
###############################################################################

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

# We can't lock on MacOS X, so don't even bother compiling in support for it.
#
if test "$ac_macosx" = yes; then
  if test "$enable_locking" = yes; then
    AC_MSG_RESULT(locking disabled: it doesn't work on MacOS X)
    enable_locking=no
    AC_DEFINE(NO_LOCKING, 1, [Define if screen locking support is disabled])
  fi
fi

# Check whether to use a xscreensaver hacks configuration directory
AC_MSG_CHECKING([whether to use xscreensaver hacks configuration])
AC_ARG_WITH(xscreensaverdir, AC_HELP_STRING([--with-xscreensaverdir=dir], [Use xscreensaver hack configs found in directory],))
xscreensaverdir=
if test "x$with_xscreensaverdir" = "xyes"; then
  if test -d /usr/share/xscreensaver/config; then
    xscreensaverdir=/usr/share/xscreensaver/config
  elif test -d /etc/xscreensaver; then
    xscreensaverdir=/etc/xscreensaver
  fi
elif test "x$with_xscreensaverdir" = "x" -o "x$with_xscreensaverdir" = "xno"; then
  xscreensaverdir=
else
  xscreensaverdir="$with_xscreensaverdir"
fi

if test -d "$xscreensaverdir"; then
  AC_DEFINE_UNQUOTED(XSCREENSAVER_CONFIG_DIR, "$xscreensaverdir", [Define to the directory containing XScreensaver configuration files])
  AC_MSG_RESULT([$xscreensaverdir])
else
  AC_MSG_RESULT([no])
fi

# Path to xscreensaver hack executables
AC_ARG_WITH(xscreensaverhackdir, AC_HELP_STRING([--with-xscreensaverhackdir=dir], [Look for xscreensaver hack executables in directory],))
if test -d "$xscreensaverdir"; then
  AC_MSG_CHECKING([for location of xscreensaver hack executables])
  xscreensaverhackdir=
  if test "x$with_xscreensaverhackdir" = "xyes" -o "x$with_xscreensaverhackdir" = "x"; then
    if test -d /usr/X11R6/lib/xscreensaver; then
      xscreensaverhackdir=/usr/X11R6/lib/xscreensaver
    elif test -d $prefix/libexec/xscreensaver; then
      xscreensaverhackdir=$prefix/libexec/xscreensaver
    elif test -d /usr/libexec/xscreensaver; then
      xscreensaverhackdir=/usr/libexec/xscreensaver
    elif test -d /usr/lib/xscreensaver; then
      xscreensaverhackdir=/usr/lib/xscreensaver
    fi
  elif test "x$with_xscreensaverhackdir" != "xno"; then
    xscreensaverhackdir="$with_xscreensaverhackdir"
  fi
  if test -d "$xscreensaverhackdir"; then
    AC_DEFINE_UNQUOTED(XSCREENSAVER_HACK_DIR, "$xscreensaverhackdir", [Define to the directory containing XScreensaver hack executables])
    AC_MSG_RESULT([$xscreensaverhackdir])
  else
    AC_MSG_RESULT([no])
    AC_MSG_WARN([using XScreensaver config files, but could not find directory containing hacks])
  fi
else
  AC_MSG_WARN([ignoring --with-xscreensaverhackdir because directory for XScreensaver configuration files was not found])
fi

#       Check for PAM.

have_pam=no
AC_ARG_ENABLE(pam, AC_HELP_STRING([--enable-pam],
                   [Enable PAM support @<:@default=auto@:>@],
                   ),,enable_pam=auto)

if test "x$enable_locking" = "xyes" -a  "x$enable_pam" != "xno"; then
AC_CHECK_LIB(pam, pam_start, have_pam=yes)
fi
if test "x$have_pam" = "xyes"; then
    AUTH_LIBS="${AUTH_LIBS} -lpam"
    AC_DEFINE(HAVE_PAM, 1, [Define if PAM support is included])

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
 
fi
AM_CONDITIONAL(HAVE_PAM, test x$have_pam = xyes)
AC_SUBST(HAVE_PAM)


#       Check for the nine billion variants of shadow passwords...

need_setuid=no

have_shadow=no
have_shadow_enhanced=no
have_shadow_adjunct=no
have_shadow_hpux=no
have_passwd_helper=no

with_shadow_req=unspecified

AC_ARG_WITH(shadow,
[  --with-shadow           Include support for shadow password authentication.],
  [with_shadow="$withval"; with_shadow_req="$withval"],[with_shadow=yes])

HANDLE_X_PATH_ARG(with_shadow, --with-shadow, shadow password)

if test "$enable_locking" = no ; then
  with_shadow_req=no
  with_shadow=no
fi


#       Check for Sun "adjunct" passwords.

if test "$with_shadow" = yes ; then
  AC_CACHE_CHECK([for Sun-style shadow passwords], ac_cv_sun_adjunct,
                 [AC_TRY_X_COMPILE([#include <stdlib.h>
                                    #include <unistd.h>
                                    #include <sys/types.h>
                                    #include <sys/label.h>
                                    #include <sys/audit.h>
                                    #include <pwdadj.h>],
                      [struct passwd_adjunct *p = getpwanam("nobody");
                       const char *pw = p->pwa_passwd;],
                      [ac_cv_sun_adjunct=yes],
                      [ac_cv_sun_adjunct=no])])
  if test "$ac_cv_sun_adjunct" = yes; then
    have_shadow_adjunct=yes
    have_shadow=yes
    need_setuid=yes
  fi
fi


#       Check for DEC and SCO so-called "enhanced" security.

if test "$with_shadow" = yes ; then
  AC_CACHE_CHECK([for DEC-style shadow passwords], ac_cv_enhanced_passwd,
                 [AC_TRY_X_COMPILE([#include <stdlib.h>
                                    #include <unistd.h>
                                    #include <sys/types.h>
                                    #include <pwd.h>
                                    #include <sys/security.h>
                                    #include <prot.h>],
                      [struct pr_passwd *p;
                       const char *pw;
                       set_auth_parameters(0, 0);
                       check_auth_parameters();
                       p = getprpwnam("nobody");
                       pw = p->ufld.fd_encrypt;],
                      [ac_cv_enhanced_passwd=yes],
                      [ac_cv_enhanced_passwd=no])])
  if test $ac_cv_enhanced_passwd = yes; then
    have_shadow_enhanced=yes
    have_shadow=yes
    need_setuid=yes

    # On SCO, getprpwnam() is in -lprot (which uses nap() from -lx)
    # (I'm told it needs -lcurses too, but I don't understand why.)
    # But on DEC, it's in -lsecurity.
    #
    AC_CHECK_LIB(prot, getprpwnam, 
                 [PASSWD_LIBS="$PASSWD_LIBS -lprot -lcurses -lx"],
                 [AC_CHECK_LIB(security, getprpwnam, 
                               [PASSWD_LIBS="$PASSWD_LIBS -lsecurity"])],
                 [-lx])
  fi
fi

#       Check for HP's entry in the "Not Invented Here" Sweepstakes.

if test "$with_shadow" = yes ; then
  AC_CACHE_CHECK([for HP-style shadow passwords], ac_cv_hpux_passwd,
                 [AC_TRY_X_COMPILE([#include <stdlib.h>
                                    #include <unistd.h>
                                    #include <sys/types.h>
                                    #include <pwd.h>
                                    #include <hpsecurity.h>
                                    #include <prot.h>],
                      [struct s_passwd *p = getspwnam("nobody");
                       const char *pw = p->pw_passwd;],
                      [ac_cv_hpux_passwd=yes],
                      [ac_cv_hpux_passwd=no])])
  if test "$ac_cv_hpux_passwd" = yes; then
    have_shadow_hpux=yes
    have_shadow=yes
    need_setuid=yes

    # on HPUX, bigcrypt is in -lsec
    AC_CHECK_LIB(sec, bigcrypt, [PASSWD_LIBS="$PASSWD_LIBS -lsec"])
  fi
fi


#       Check for FreeBSD-style shadow passwords.
#
#       On FreeBSD, getpwnam() and friends work just like on non-shadow-
#       password systems -- except you only get stuff in the pw_passwd field
#       if the running program is setuid.  So, guess that we've got this
#       lossage to contend with if /etc/master.passwd exists, and default to
#       a setuid installation.

if test "$with_shadow" = yes ; then
  AC_CACHE_CHECK([for FreeBSD-style shadow passwords], ac_cv_master_passwd,
                 [if test -f /etc/master.passwd ; then
                    ac_cv_master_passwd=yes
                  else
                    ac_cv_master_passwd=no
                  fi])
  if test "$ac_cv_master_passwd" = yes; then
    need_setuid=yes
  fi
fi


#       Check for traditional (ha!) shadow passwords.

if test "$with_shadow" = yes ; then
  AC_CACHE_CHECK([for generic shadow passwords], ac_cv_shadow,
                 [AC_TRY_X_COMPILE([#include <stdlib.h>
                                    #include <unistd.h>
                                    #include <sys/types.h>
                                    #include <pwd.h>
                                    #include <shadow.h>],
                      [struct spwd *p = getspnam("nobody");
                       const char *pw = p->sp_pwdp;],
                      [ac_cv_shadow=yes],
                      [ac_cv_shadow=no])])
  if test "$ac_cv_shadow" = yes; then
    have_shadow=yes
    need_setuid=yes

    # On some systems (UnixWare 2.1), getspnam() is in -lgen instead of -lc.
    have_getspnam=no
    AC_CHECK_LIB(c, getspnam, [have_getspnam=yes])
    if test "$have_getspnam" = no ; then
      AC_CHECK_LIB(gen, getspnam,
                   [have_getspnam=yes; PASSWD_LIBS="$PASSWD_LIBS -lgen"])
    fi
  fi
fi


#       Check for other libraries needed for non-shadow passwords.

if test "$enable_locking" = yes ; then

  # On some systems (UnixWare 2.1), crypt() is in -lcrypt instead of -lc.
  have_crypt=no
  AC_CHECK_LIB(c, crypt, [have_crypt=yes])
  if test "$have_crypt" = no ; then
    AC_CHECK_LIB(crypt, crypt,
                 [have_crypt=yes; AUTH_LIBS="${AUTH_LIBS} -lcrypt"])
  fi
fi

# Most of the above shadow mechanisms will have set need_setuid to yes,
# if they were found.  But, on some systems, we need setuid even when
# using plain old vanilla passwords.
#
if test "$enable_locking" = yes ; then
  case "$host" in
    *-hpux* | *-aix* | *-netbsd* | *-freebsd* | *-openbsd* )
      need_setuid=yes
    ;;
  esac
fi

if test "$have_shadow_adjunct" = yes ; then
  AC_DEFINE(HAVE_ADJUNCT_PASSWD, 1, [Define if system uses adjunct shadow passwords])
elif test "$have_shadow_enhanced" = yes ; then
  AC_DEFINE(HAVE_ENHANCED_PASSWD, 1, [Define if system uses enhanced shadow passwords])
elif test "$have_shadow_hpux" = yes ; then
  AC_DEFINE(HAVE_HPUX_PASSWD, 1, [Define if system uses HPUX shadow passwords])
elif test "$have_shadow" = yes ; then
  AC_DEFINE(HAVE_SHADOW_PASSWD, 1, [Define if system uses traditional shadow passwords])
fi

#       Check for external password helper
#       On SuSE, instead of having xscreensaver be a setuid program, they
#       fork an external program that takes the password on stdin, and
#       returns true if that password is a valid one.  Then only that
#       smaller program needs to be setuid.
#
#       (Note that this external program is not a GUI: the GUI is still
#       all in xscreensaver itself; the external program just does auth.)

have_passwd_helper=no
with_passwd_helper_req=unspecified

AC_ARG_WITH(passwd-helper,
[  --with-passwd-helper    Include support for an external password
                          verification helper program.],
  [with_passwd_helper="$withval"; with_passwd_helper_req="$withval"],[with_passwd_helper=no])
# no HANDLE_X_PATH_ARG for this one

if test "$enable_locking" = no ; then
  with_passwd_helper_req=no
  with_passwd_helper=no
fi

case "$with_passwd_helper" in
  ""|no) : ;;
  /*)
    AC_DEFINE_UNQUOTED(PASSWD_HELPER_PROGRAM, "$with_passwd_helper", [Full pathname of password helper application])
    have_passwd_helper=yes;;
  *)
    echo "error: --with-passwd-helper needs full pathname of helper (not '$with_passwd_helper')." >&2
    exit 1
esac
AM_CONDITIONAL(HAVE_PASSWD_HELPER, test x$have_passwd_helper = xyes)
AC_SUBST(HAVE_PASSWD_HELPER)

if test "$need_setuid" = yes -a "$have_pam" != yes ; then
  NEED_SETUID=yes
else
  NEED_SETUID=no
fi
AC_SUBST(NEED_SETUID)

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
if test -f $srcdir/CVSVERSION; then
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

# Flags

AC_SUBST(CFLAGS)
AC_SUBST(CPPFLAGS)
AC_SUBST(LDFLAGS)

# Saver engine stuff
SLIDESHOW_COSMOS_DIR="${EXPANDED_DATADIR}/pixmaps/backgrounds/cosmos"

AC_SUBST(SLIDESHOW_COSMOS_DIR)

# Files

AC_OUTPUT([
Makefile
po/Makefile.in
src/Makefile
src/cut-n-paste/Makefile
data/Makefile
data/images/Makefile
data/images/cosmos/Makefile
savers/Makefile
savers/cosmos-slideshow.xml
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
        PAM prefix:               ${PAM_PREFIX}
	source code location:	  ${srcdir}
	compiler:		  ${CC}
	cflags:		          ${CFLAGS}
        Maintainer mode:          ${USE_MAINTAINER_MODE}

        Screen locking enabled:   ${enable_locking}
        Power management enabled: ${have_dpms}
        User switching enabled:   ${enable_user_switching}
        PAM support:              ${have_pam}
        Have shadow passords:     ${have_shadow}
        Have adjunct shadow:      ${have_shadow_adjunct}
        Have enhanced shadow:     ${have_shadow_enhanced}
        Have HPUX shadow:         ${have_shadow_hpux}
        Have password helper:     ${have_passwd_helper}"

if test "x$need_setuid" = "xyes" -a "x$have_pam" != "xyes" ; then
echo \
"        Need setuid dialog:       yes
"
else
echo \
"        Need setuid dialog:       no
"
fi


echo ""
