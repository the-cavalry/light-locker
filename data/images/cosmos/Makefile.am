NULL =

BACKGROUNDS = \
	blue-marble-west.jpg	\
	cloud.jpg 		\
	comet.jpg 		\
	earth-horizon.jpg	\
	galaxy-ngc3370.jpg	\
	helix-nebula.jpg	\
	jupiter.jpg		\
	sombrero.jpg		\
	whirlpool.jpg		\
	$(NULL)

imagesdir   = $(SLIDESHOW_COSMOS_DIR)
images_DATA =			\
	$(BACKGROUNDS)		\
	background-1.xml	\
	$(NULL)

cosmos.xml.in: cosmos.xml.in.in Makefile
	$(edit) $< >$@

background-1.xml: background-1.xml.in Makefile
	$(edit) $< >$@

metadata_in_files = cosmos.xml.in
metadatadir = $(datadir)/gnome-background-properties
metadata_DATA = $(metadata_in_files:.xml.in=.xml)
@INTLTOOL_XML_RULE@

edit = sed \
	-e 's|@IMAGESDIR[@]|$(imagesdir)|g'

EXTRA_DIST =			\
	CREDITS 		\
	$(BACKGROUNDS)		\
	background-1.xml.in	\
	cosmos.xml.in.in	\
	$(NULL)

CLEANFILES = \
	cosmos.xml.in			\
	$(NULL)

DISTCLEANFILES = \
	cosmos.xml			\
	background-1.xml		\
	$(NULL)

MAINTAINERCLEANFILES =                  \
	*~				\
	Makefile.in			\
	$(NULL)
