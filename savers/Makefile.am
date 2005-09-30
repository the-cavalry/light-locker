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

saverdir = $(libexecdir)/gnome-screensaver
saver_PROGRAMS = 	\
	popsquares	\
	slideshow	\
	$(NULL)

popsquares_SOURCES = 	\
	popsquares.c

popsquares_LDADD =	\
	$(GNOME_SCREENSAVER_SAVER_LIBS)

slideshow_SOURCES =   \
	slideshow.c

slideshow_LDADD =     \
	$(GNOME_SCREENSAVER_SAVER_LIBS)

EXTRA_DIST =				\
	$(themes_in_files)		\
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
