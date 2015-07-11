NULL =

schema_in_files=apps.light-locker.gschema.xml.in
gsettings_SCHEMAS = $(schema_in_files:.xml.in=.xml)
@GSETTINGS_RULES@

man_MANS = 				\
	light-locker.1			\
	light-locker-command.1		\
	$(NULL)

@INTLTOOL_XML_NOMERGE_RULE@

EXTRA_DIST = 				\
	$(man_MANS)			\
	usr.bin.light-locker		\
	usr.bin.light-locker-command	\
	$(NULL)

DISTCLEANFILES = 			\
	$(desktop_DATA)			\
	$(gsettings_SCHEMAS)		\
	$(NULL)

MAINTAINERCLEANFILES =			\
	*~				\
	apps.light-locker.gschema.xml.in\
	Makefile.in

-include $(top_srcdir)/git.mk
