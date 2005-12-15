## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

NULL =

INCLUDES =							\
	-I.							\
	-I$(srcdir)						\
	$(GNOME_SCREENSAVER_SAVER_CFLAGS)			\
	$(DISABLE_DEPRECATED_CFLAGS)				\
	-DLIBEXECDIR=\""$(libexecdir)"\"			\
	-DGNOMELOCALEDIR=\""$(prefix)/$(DATADIRNAME)/locale"\"	\
	-DDATADIR=\""$(datadir)"\"				\
	$(WARN_CFLAGS)						\
	$(NULL)

@INTLTOOL_DESKTOP_RULE@

themesdir = $(pkgdatadir)/themes
themes_in_files = 			\
	cosmos-slideshow.desktop.in	\
	personal-slideshow.desktop.in	\
	popsquares.desktop.in		\
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
	popsquares	\
	slideshow	\
	$(NULL)

popsquares_SOURCES = 		\
	gste-popsquares.c	\
	gste-popsquares.h	\
	popsquares.c		\
	$(NULL)

popsquares_LDADD =			\
	$(GNOME_SCREENSAVER_SAVER_LIBS)	\
	libgs-theme-engine.a 		\
	$(NULL)

slideshow_SOURCES =   \
	slideshow.c

slideshow_LDADD =     \
	$(GNOME_SCREENSAVER_SAVER_LIBS)

EXTRA_DIST =				\
	$(themes_in_files)		\
	$(NULL)

CLEANFILES = 				\
	$(BUILT_SOURCES)		\
	$(NULL)

DISTCLEANFILES = 			\
	$(themes_DATA)			\
	$(NULL)

MAINTAINERCLEANFILES =			\
	*~				\
	Makefile.in			\
	cosmos-slideshow.desktop	\
	personal-slideshow.desktop	\
	popsquares.desktop		\
	$(NULL)
