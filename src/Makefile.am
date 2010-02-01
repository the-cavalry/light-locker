## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

NULL =

saverdir = $(libexecdir)/gnome-screensaver
themesdir = $(pkgdatadir)/themes

INCLUDES =							\
	-I.							\
	-I$(srcdir)						\
	-DGMENU_I_KNOW_THIS_IS_UNSTABLE				\
	$(GNOME_SCREENSAVER_CFLAGS)				\
	$(GNOME_SCREENSAVER_DIALOG_CFLAGS)			\
	$(GNOME_SCREENSAVER_CAPPLET_CFLAGS)			\
	$(DISABLE_DEPRECATED_CFLAGS)				\
	-DPREFIX=\""$(prefix)"\" 				\
	-DBINDIR=\""$(bindir)"\"				\
	-DLIBDIR=\""$(libdir)"\" 				\
	-DLIBEXECDIR=\""$(libexecdir)"\"			\
	-DDATADIR=\""$(datadir)"\"				\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 			\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"		\
	-DSAVERDIR=\""$(saverdir)"\"				\
	-DTHEMESDIR=\""$(themesdir)"\"				\
	-DGTKBUILDERDIR=\"$(pkgdatadir)\"				\
	-DPAM_SERVICE_NAME=\""gnome-screensaver"\"		\
	$(WARN_CFLAGS)						\
	$(AUTH_CFLAGS)						\
	$(DEBUG_CFLAGS)						\
	$(DBUS_CFLAGS)						\
	$(LIBGNOMEKBDUI_CFLAGS)					\
	$(LIBNOTIFY_CFLAGS)					\
	$(NULL)

bin_PROGRAMS = \
	gnome-screensaver		\
	gnome-screensaver-command	\
	gnome-screensaver-preferences	\
	$(NULL)

libexec_PROGRAMS = \
	gnome-screensaver-dialog	\
	gnome-screensaver-gl-helper	\
	$(NULL)

noinst_PROGRAMS = 	\
	test-fade	\
	test-passwd	\
	test-watcher	\
	test-window	\
	$(NULL)

noinst_LTLIBRARIES = 				\
	libscreensaver-settings-common.la	\
	libscreensaver-common.la		\
	$(NULL)

ccmodulesdir = $(CCEXTENSIONSDIR)
ccmodules_LTLIBRARIES = libgnome-screensaver.la

desktopdir = $(sysconfdir)/xdg/autostart
desktop_in_files = gnome-screensaver.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

gnome_screensaver_command_SOURCES = 	\
	gnome-screensaver-command.c	\
	$(NULL)

gnome_screensaver_command_LDADD =	\
	$(GNOME_SCREENSAVER_COMMAND_LIBS)	\
	$(NULL)

AUTH_SOURCES = 			\
	gs-auth.h		\
	gs-auth-@AUTH_SCHEME@.c	\
	$(NULL)

test_fade_SOURCES = 			\
	test-fade.c 			\
	gs-fade.c	 		\
	gs-fade.h	 		\
	$(NULL)

test_fade_LDADD =			\
	libscreensaver-common.la	\
	$(GNOME_SCREENSAVER_LIBS)	\
	$(SAVER_LIBS)			\
	$(NULL)

test_passwd_SOURCES = 			\
	test-passwd.c 			\
	$(AUTH_SOURCES)			\
	setuid.c			\
	setuid.h			\
	$(NULL)

test_passwd_LDADD =			\
	libscreensaver-common.la	\
	$(GNOME_SCREENSAVER_DIALOG_LIBS)\
	$(AUTH_LIBS)			\
	$(NULL)

test_watcher_SOURCES =			\
	test-watcher.c			\
	gs-watcher.h			\
	gs-watcher-x11.c		\
	gs-marshal.c 			\
	gs-marshal.h			\
	$(NULL)

test_watcher_LDADD =			\
	libscreensaver-common.la	\
	$(GNOME_SCREENSAVER_LIBS)	\
	$(SAVER_LIBS)			\
	$(NULL)

test_window_SOURCES =			\
	test-window.c			\
	gs-window.h			\
	gs-window-x11.c			\
	gs-grab-x11.c			\
	gs-grab.h			\
	gs-marshal.c 			\
	gs-marshal.h			\
	$(NULL)

test_window_LDADD =			\
	libscreensaver-common.la	\
	$(GNOME_SCREENSAVER_LIBS)	\
	$(SAVER_LIBS)			\
	$(NULL)

gnome_screensaver_dialog_SOURCES = 	\
	gnome-screensaver-dialog.c	\
	gs-lock-plug.c			\
	gs-lock-plug.h			\
	setuid.c			\
	setuid.h			\
	$(AUTH_SOURCES)			\
	$(NULL)

EXTRA_gnome_screensaver_dialog_SOURCES = \
	gs-auth-pam.c		\
	gs-auth-helper.c	\
	gs-auth-pwent.c		\
	$(NULL)

gnome_screensaver_dialog_LDADD =	\
	libscreensaver-common.la	\
	$(GNOME_SCREENSAVER_DIALOG_LIBS)\
	$(SAVER_LIBS)			\
	$(AUTH_LIBS)			\
	$(LIBGNOMEKBDUI_LIBS)		\
	$(LIBNOTIFY_LIBS)		\
	$(NULL)

BUILT_SOURCES = 		\
	gs-marshal.c 		\
	gs-marshal.h		\
	$(NULL)

gs-marshal.c: gs-marshal.list
	echo "#include \"gs-marshal.h\"" > $@ && \
	@GLIB_GENMARSHAL@ $< --prefix=gs_marshal --body >> $@

gs-marshal.h: gs-marshal.list
	@GLIB_GENMARSHAL@ $< --prefix=gs_marshal --header > $@

gnome_screensaver_SOURCES =	\
	gnome-screensaver.c	\
	gnome-screensaver.h	\
	gs-monitor.c		\
	gs-monitor.h		\
	gs-watcher-x11.c	\
	gs-watcher.h		\
	gs-listener-dbus.c	\
	gs-listener-dbus.h	\
	gs-manager.c		\
	gs-manager.h		\
	gs-window-x11.c		\
	gs-window.h		\
	gs-prefs.c		\
	gs-prefs.h		\
	gs-grab-x11.c		\
	gs-grab.h		\
	gs-fade.c		\
	gs-fade.h		\
	$(BUILT_SOURCES)	\
	$(NULL)

gnome_screensaver_gl_helper_LDADD =	\
	$(GNOME_SCREENSAVER_LIBS)	\
	$(GL_LIBS)			\
	$(NULL)

gnome_screensaver_gl_helper_SOURCES =	\
	gnome-screensaver-gl-helper.c	\
	gs-visual-gl.c			\
	gs-visual-gl.h			\
	$(NULL)

gnome_screensaver_LDADD =		\
	libscreensaver-common.la	\
	$(GNOME_SCREENSAVER_LIBS)	\
	$(SAVER_LIBS)			\
	$(NULL)

gnome_screensaver_LDFLAGS = -export-dynamic

libscreensaver_common_la_SOURCES =	\
	gs-theme-manager.c		\
	gs-theme-manager.h		\
	gs-job.c			\
	gs-job.h			\
	gs-debug.c			\
	gs-debug.h			\
	subprocs.c			\
	subprocs.h			\
	$(NULL)

libscreensaver_settings_common_la_SOURCES =	\
	copy-theme-dialog.c		\
	copy-theme-dialog.h		\
	$(NULL)

libgnome_screensaver_la_SOURCES =	\
	screensaver-module.c		\
	cc-screensaver-panel.c		\
	cc-screensaver-panel.h		\
	cc-screensaver-page.c		\
	cc-screensaver-page.h		\
	$(NULL)

libgnome_screensaver_la_LDFLAGS =	\
	$(CC_EXTENSION_LIBTOOL_FLAGS)	\
	$(NULL)

libgnome_screensaver_la_LIBADD =		\
	libscreensaver-settings-common.la	\
	libscreensaver-common.la		\
	$(CC_EXTENSION_LIBS)			\
	$(NULL)

libgnome_screensaver_la_CFLAGS =	\
	$(CC_EXTENSION_CFLAGS)		\
	$(NULL)

libgnome_screensaver_la_LIBTOOLFLAGS = --tag=disable-static

gnome_screensaver_preferences_SOURCES =	\
	gnome-screensaver-preferences.c	\
	$(NULL)

gnome_screensaver_preferences_LDADD =		\
	libscreensaver-settings-common.la	\
	libscreensaver-common.la		\
	$(GNOME_SCREENSAVER_CAPPLET_LIBS)	\
	$(NULL)

EXTRA_DIST =				\
	debug-screensaver.sh		\
	gs-marshal.list			\
	gnome-screensaver.desktop.in	\
	$(NULL)

CLEANFILES = 				\
	$(desktop_DATA) 		\
	gnome-screensaver.desktop.in	\
	$(BUILT_SOURCES)

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
