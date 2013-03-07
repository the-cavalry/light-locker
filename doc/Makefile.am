NULL =

SPEC_XML_FILES =			\
	light-locker.xml		\
	dbus-interface.xml		\
	$(NULL)

if DOCBOOK_DOCS_ENABLED

htmldocdir = $(DOCDIR)/spec
htmldoc_DATA = light-locker.html

light-locker.html : dbus-interface.xml
	$(XMLTO) html-nochunks -m config.xsl light-locker.xml

endif # DOCBOOK_DOCS_ENABLED

EXTRA_DIST =				\
	light-locker.xml.in		\
	config.xsl			\
	docbook.css			\
	$(SPEC_XML_FILES)
	$(NULL)

clean-local:
	rm -f *~
	rm -f light-locker.html

-include $(top_srcdir)/git.mk
