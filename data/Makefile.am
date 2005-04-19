@INTLTOOL_SCHEMAS_RULE@

schemasdir   = @GCONF_SCHEMA_FILE_DIR@
schemas_in_files = gnome-screensaver.schemas.in
schemas_DATA = $(schemas_in_files:.schemas.in=.schemas)

@INTLTOOL_DESKTOP_RULE@

desktopdir = $(datadir)/applications
desktop_in_files = gnome-screensaver-preferences.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

gladedir = $(pkgdatadir)
glade_DATA = gnome-screensaver-preferences.glade

EXTRA_DIST = $(schemas_in_files)

DISTCLEANFILES = $(schemas_DATA)

MAINTAINERCLEANFILES =			\
	*~				\
	Makefile.in

if GCONF_SCHEMAS_INSTALL
# don't do this if we are building in eg. rpm
install-data-local: $(schemas_DATA)
        if test -z "$(DESTDIR)" ; then \
                for p in $(schemas_DATA) ; do \
                        GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $(top_builddir)/data/$$p ; \
                done \
        fi
else
install-data-local:
endif
