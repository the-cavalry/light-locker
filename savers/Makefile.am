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

saverdir = $(libexecdir)/gnome-screensaver
saver_PROGRAMS = 	\
	popsquares	\
	slideshow	\
	$(NULL)

themesdir = $(pkgdatadir)/themes
themes_DATA = 		\
	popsquares.xml	\
	slideshow.xml	\
	$(NULL)

popsquares_SOURCES = 	\
	popsquares.c

popsquares_LDADD =	\
	$(GNOME_SCREENSAVER_SAVER_LIBS)

slideshow_SOURCES =   \
        slideshow.c

slideshow_LDADD =     \
        $(GNOME_SCREENSAVER_SAVER_LIBS)

EXTRA_DIST =		\
	$(themes_DATA)

MAINTAINERCLEANFILES =			\
	*~				\
	Makefile.in
