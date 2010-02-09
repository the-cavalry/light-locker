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
	cosmos-slideshow.desktop.in.in 	 \
	footlogo-floaters.desktop.in.in	 \
	personal-slideshow.desktop.in.in \
	popsquares.desktop.in.in	 \
	$(NULL)

cosmos-slideshow.desktop.in: cosmos-slideshow.desktop.in.in Makefile
	$(edit) $< >$@
footlogo-floaters.desktop.in: footlogo-floaters.desktop.in.in Makefile
	$(edit) $< >$@
personal-slideshow.desktop.in: personal-slideshow.desktop.in.in Makefile
	$(edit) $< >$@
popsquares.desktop.in: popsquares.desktop.in.in Makefile
	$(edit) $< >$@

@INTLTOOL_DESKTOP_RULE@

edit = sed \
	-e 's|@SLIDESHOW_COSMOS_DIR[@]|$(SLIDESHOW_COSMOS_DIR)|g' \
	-e 's|@FLOATERS_FOOT_LOGO_IMAGE[@]|$(FLOATERS_FOOT_LOGO_IMAGE)|g' \
	-e 's|@SAVERDIR[@]|$(privlibexecdir)|g'

themesdir = $(datadir)/applications/screensavers
themes_in_files = 			\
	cosmos-slideshow.desktop.in	\
	footlogo-floaters.desktop.in    \
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
	floaters	\
	popsquares	\
	slideshow	\
	$(NULL)

floaters_SOURCES =	\
	floaters.c	\
	$(NULL)

floaters_LDADD =		       \
	libgs-theme-engine.a		\
	$(GNOME_SCREENSAVER_SAVER_LIBS) \
	-lm                             \
	$(NULL)

popsquares_SOURCES = 		\
	gste-popsquares.c	\
	gste-popsquares.h	\
	popsquares.c		\
	$(NULL)

popsquares_LDADD =			\
	libgs-theme-engine.a 		\
	$(GNOME_SCREENSAVER_SAVER_LIBS)	\
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
	cosmos-slideshow.desktop.in	\
	footlogo-floaters.desktop.in	\
	popsquares.desktop.in		\
	personal-slideshow.desktop.in	\
	$(NULL)

DISTCLEANFILES = 			\
	$(themes_DATA)			\
	$(NULL)

MAINTAINERCLEANFILES =			\
	*~				\
	Makefile.in			\
	cosmos-slideshow.desktop	\
	footlogo-floaters.desktop	\
	personal-slideshow.desktop	\
	popsquares.desktop		\
	$(NULL)
