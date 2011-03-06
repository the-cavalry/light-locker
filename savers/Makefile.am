## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

NULL =

INCLUDES =							\
	-I.							\
	-I$(srcdir)						\
	$(GNOME_SCREENSAVER_SAVER_CFLAGS)			\
	$(DISABLE_DEPRECATED_CFLAGS)				\
	-DLIBEXECDIR=\""$(libexecdir)"\"			\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"		\
	-DDATADIR=\""$(datadir)"\"				\
	$(WARN_CFLAGS)						\
	$(NULL)

DESKTOP_IN_IN_FILES = 			 \
	personal-slideshow.desktop.in.in \
	$(NULL)

personal-slideshow.desktop.in: personal-slideshow.desktop.in.in Makefile
	$(edit) $< >$@

@INTLTOOL_DESKTOP_RULE@

edit = sed \
	-e 's|@SAVERDIR[@]|$(privlibexecdir)|g'

themesdir = $(datadir)/applications/screensavers
themes_in_files = 			\
	personal-slideshow.desktop.in	\
	$(NULL)
themes_DATA = $(themes_in_files:.desktop.in=.desktop)

BUILT_SOURCES = 			\
	gs-theme-engine-marshal.c	\
	gs-theme-engine-marshal.h	\
	$(NULL)

gs-theme-engine-marshal.c: gs-theme-engine-marshal.list
	echo "#include \"gs-theme-engine-marshal.h\"" > $@ && \
	@GLIB_GENMARSHAL@ $< --prefix=gs_theme_engine_marshal --body >> $@

gs-theme-engine-marshal.h: gs-theme-engine-marshal.list
	@GLIB_GENMARSHAL@ $< --prefix=gs_theme_engine_marshal --header > $@

noinst_LIBRARIES =			\
	libgs-theme-engine.a

libgs_theme_engine_a_CPPFLAGS =					\
	-DDATADIR=\""$(datadir)"\"				\
	$(NULL)

libgs_theme_engine_a_SOURCES =		\
	$(BUILT_SOURCES)		\
	gs-theme-window.h		\
	gs-theme-window.c		\
	gs-theme-engine.c		\
	gs-theme-engine.h		\
	$(NULL)

saverdir = $(libexecdir)/gnome-screensaver
saver_PROGRAMS = 	\
	slideshow	\
	$(NULL)

slideshow_SOURCES =   \
	gste-slideshow.c	\
	gste-slideshow.h	\
	xdg-user-dir-lookup.c	\
	xdg-user-dir-lookup.h	\
	slideshow.c		\
	$(NULL)

slideshow_LDADD =     \
	libgs-theme-engine.a 		\
	$(GNOME_SCREENSAVER_SAVER_LIBS)	\
	$(NULL)

EXTRA_DIST =				\
	gs-theme-engine-marshal.list	\
	$(DESKTOP_IN_IN_FILES)		\
	$(themes_in_files)		\
	$(NULL)

CLEANFILES = 				\
	$(BUILT_SOURCES)		\
	personal-slideshow.desktop.in	\
	$(NULL)

DISTCLEANFILES = 			\
	$(themes_DATA)			\
	$(NULL)

MAINTAINERCLEANFILES =			\
	*~				\
	Makefile.in			\
	personal-slideshow.desktop	\
	$(NULL)
