## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

NULL =

saverdir = $(libexecdir)/light-locker

INCLUDES =							\
	-I.							\
	-I$(srcdir)						\
	$(LIGHT_LOCKER_CFLAGS)					\
	$(DISABLE_DEPRECATED_CFLAGS)				\
	-DPREFIX=\""$(prefix)"\" 				\
	-DBINDIR=\""$(bindir)"\"				\
	-DLIBDIR=\""$(libdir)"\" 				\
	-DLIBEXECDIR=\""$(libexecdir)"\"			\
	-DDATADIR=\""$(datadir)"\"				\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 			\
	-DLOCALEDIR=\""$(datadir)/locale"\"			\
	-DSAVERDIR=\""$(saverdir)"\"				\
	-DGTKBUILDERDIR=\"$(pkgdatadir)\"			\
	-DPAM_SERVICE_NAME=\""light-locker"\"			\
	$(WARN_CFLAGS)						\
	$(DEBUG_CFLAGS)						\
	$(DBUS_CFLAGS)						\
	$(LIBNOTIFY_CFLAGS)					\
	$(SYSTEMD_CFLAGS)					\
	$(NULL)

bin_PROGRAMS = \
	light-locker		\
	light-locker-command	\
	$(NULL)

desktopdir = $(datadir)/applications
desktop_in_files = light-locker.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

light_locker_command_SOURCES = 	\
	light-locker-command.c	\
	gs-bus.h			\
	$(NULL)

light_locker_command_LDADD =	\
	$(LIGHT_LOCKER_COMMAND_LIBS)	\
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

light_locker_SOURCES =	\
	light-locker.c		\
	light-locker.h		\
	gs-bus.h		\
	gs-monitor.c		\
	gs-monitor.h		\
	gs-listener-dbus.c	\
	gs-listener-dbus.h	\
	gs-manager.c		\
	gs-manager.h		\
	gs-window-x11.c		\
	gs-window.h		\
	gs-debug.c		\
	gs-debug.h		\
	gs-grab-x11.c		\
	gs-grab.h		\
	$(BUILT_SOURCES)	\
	$(NULL)

light_locker_LDADD =		\
	$(LIGHT_LOCKER_LIBS)	\
	$(SAVER_LIBS)			\
	$(SYSTEMD_LIBS)                 \
	$(NULL)

light_locker_LDFLAGS = -export-dynamic

EXTRA_DIST =				\
	debug-screensaver.sh		\
	gs-marshal.list			\
	light-locker.desktop.in		\
	$(NULL)

CLEANFILES = 				\
	$(desktop_DATA) 		\
	light-locker.desktop.in		\
	$(BUILT_SOURCES)

MAINTAINERCLEANFILES =			\
	*~				\
	Makefile.in

-include $(top_srcdir)/git.mk
