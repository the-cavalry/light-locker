NULL =

SPEC_XML_FILES =			\
	gnome-screensaver.xml		\
	dbus-interface.xml		\
	$(NULL)

if DOCBOOK_DOCS_ENABLED

htmldocdir = $(DOCDIR)/spec
htmldoc_DATA = gnome-screensaver.html

gnome-screensaver.html : dbus-interface.xml
	$(XMLTO) html-nochunks -m config.xsl gnome-screensaver.xml

endif # DOCBOOK_DOCS_ENABLED

EXTRA_DIST =				\
	gnome-screensaver.xml.in	\
	config.xsl			\
	docbook.css			\
	$(SPEC_XML_FILES)
	$(NULL)

clean-local:
	rm -f *~
	rm -f gnome-screensaver.html
