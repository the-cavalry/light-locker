## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

SUBDIRS = \
	cut-n-paste

NULL =

saverdir = $(libexecdir)/gnome-screensaver
themesdir = $(pkgdatadir)/themes

INCLUDES =							\
	-I.							\
	-I$(srcdir)						\
	-I$(top_srcdir)/src/cut-n-paste				\
	-DGMENU_I_KNOW_THIS_IS_UNSTABLE				\
	$(GNOME_SCREENSAVER_CFLAGS)				\
	$(GNOME_SCREENSAVER_DIALOG_CFLAGS)			\
	$(GNOME_SCREENSAVER_CAPPLET_CFLAGS)			\
	$(DISABLE_DEPRECATED_CFLAGS)				\
	-DBINDIR=\""$(bindir)"\"				\
	-DLIBEXECDIR=\""$(libexecdir)"\"			\
	-DSAVERDIR=\""$(saverdir)"\"				\
	-DTHEMESDIR=\""$(themesdir)"\"				\
	-DGNOMELOCALEDIR=\""$(prefix)/$(DATADIRNAME)/locale"\"	\
	-DGLADEDIR=\"$(pkgdatadir)\"				\
	-DPAM_SERVICE_NAME=\""gnome-screensaver"\"		\
	$(WARN_CFLAGS)						\
	$(AUTH_CFLAGS)						\
	$(DEBUG_CFLAGS)						\
	$(DBUS_CFLAGS)						\
	$(FAST_USER_SWITCH_CFLAGS)				\
	$(NULL)

bin_PROGRAMS = \
	gnome-screensaver		\
	gnome-screensaver-command	\
	gnome-screensaver-preferences

libexec_PROGRAMS = \
	gnome-screensaver-dialog

noinst_PROGRAMS = \
	test-passwd

gnome_screensaver_command_SOURCES = 	\
	gnome-screensaver-command.c

gnome_screensaver_command_LDADD =	\
	$(GNOME_SCREENSAVER_COMMAND_LIBS)

AUTH_SOURCES = 			\
	passwd.c		\
	passwd.h		\
	passwd-pwent.c		\
	passwd-pwent.h

if HAVE_PAM
AUTH_SOURCES += 		\
	passwd-pam.c		\
	passwd-pam.h
endif

if HAVE_PASSWD_HELPER
AUTH_SOURCES += 		\
	passwd-helper.c		\
	passwd-helper.h
endif

test_passwd_SOURCES = 			\
	test-passwd.c 			\
	$(AUTH_SOURCES)			\
	setuid.c			\
	setuid.h			\
	subprocs.c			\
	subprocs.h			\
	$(NULL)

test_passwd_LDADD =			\
	$(GNOME_SCREENSAVER_LIBS)	\
	$(AUTH_LIBS)			\
	$(NULL)

gnome_screensaver_dialog_SOURCES = 	\
	gnome-screensaver-dialog.c	\
	gs-lock-plug.c			\
	gs-lock-plug.h			\
	gs-grab-x11.c			\
	gs-grab.h			\
	setuid.c			\
	setuid.h			\
	subprocs.c			\
	subprocs.h			\
	$(AUTH_SOURCES)			\
	$(NULL)

gnome_screensaver_dialog_LDADD =	\
	$(GNOME_SCREENSAVER_DIALOG_LIBS)\
	$(FAST_USER_SWITCH_LIBS)	\
	$(AUTH_LIBS)			\
        $(top_builddir)/src/cut-n-paste/libfast-user-switch.a \
	$(NULL)

gnome_screensaver_SOURCES =	\
	gnome-screensaver.c	\
	gnome-screensaver.h	\
	gs-monitor.c		\
	gs-monitor.h		\
	gs-watcher-x11.c	\
	gs-watcher-x11.h	\
	gs-listener-dbus.c	\
	gs-listener-dbus.h	\
	gs-manager.c		\
	gs-manager.h		\
	gs-window-x11.c		\
	gs-window.h		\
	gs-prefs.c		\
	gs-prefs.h		\
	gs-job.c		\
	gs-job.h		\
	subprocs.c		\
	subprocs.h		\
	gs-grab-x11.c		\
	gs-grab.h		\
	dpms.c			\
	dpms.h			\
	$(NULL)

gnome_screensaver_LDADD =		\
	$(GNOME_SCREENSAVER_LIBS)	\
	$(SAVER_LIBS)			\
	$(X_LIBS)			\
	$(NULL)

gnome_screensaver_LDFLAGS = -export-dynamic

gnome_screensaver_preferences_SOURCES =	\
	gnome-screensaver-preferences.c	\
	file-transfer-dialog.c		\
	file-transfer-dialog.h		\
	gs-job.c			\
	gs-job.h			\
	subprocs.c			\
	subprocs.h			\
	$(NULL)

gnome_screensaver_preferences_LDADD =	\
	$(GNOME_SCREENSAVER_CAPPLET_LIBS)	\
	$(NULL)

EXTRA_DIST =

MAINTAINERCLEANFILES =			\
	*~				\
	Makefile.in

install-exec-hook:
	@if [ "x@NEED_SETUID@" = "xyes" ]; then \
	    echo "***" ; \
	    echo "***  Warning: gnome-screensaver has been compiled with support for" ; \
	    echo "***           shadow passwords.  If your system actually uses shadow" ; \
	    echo "***           passwords then it must be installed as a setuid root" ; \
	    echo "***           program in order for locking to work.  To do this, you" ; \
	    echo "***           must run:" ; \
	    echo "***" ; \
	    echo "***           chown root $(DESTDIR)$(libexecdir)/gnome-screensaver-dialog" ; \
	    echo "***           chmod +s $(DESTDIR)$(libexecdir)/gnome-screensaver-dialog" ; \
	    echo "***" ; \
	    echo "***           For now, it will be installed non-setuid, which" ; \
	    echo "***           means that locking might not work." ; \
	    echo "***" ; \
	fi
