NULL =

man_MANS = 				\
	gnome-screensaver.1		\
	gnome-screensaver-command.1	\
	$(NULL)

@INTLTOOL_XML_NOMERGE_RULE@

EXTRA_DIST = 				\
	$(man_MANS)			\
	gnome-screensaver		\
	$(NULL)

DISTCLEANFILES = 			\
	$(desktop_DATA)			\
	$(NULL)

MAINTAINERCLEANFILES =			\
	*~				\
	Makefile.in

install-data-hook:
	@system=`uname`; \
	if test -f /usr/include/security/pam_appl.h; then \
	  if test '!' -d $(DESTDIR)$(PAM_PREFIX)/pam.d; then \
		$(mkinstalldirs) $(DESTDIR)$(PAM_PREFIX)/pam.d; \
		chmod 755 $(DESTDIR)$(PAM_PREFIX)/pam.d; \
	   fi; \
	   if test $$system = Linux; then \
		if test '!' -f $(DESTDIR)$(PAM_PREFIX)/pam.d/gnome-screensaver; then \
		     $(INSTALL_DATA) gnome-screensaver $(DESTDIR)$(PAM_PREFIX)/pam.d/gnome-screensaver; \
		fi; \
		if test "x$(PAM_PREFIX)" != "x/etc/pam.d"; then \
	             echo "***" ; \
	             echo "***  Warning: gnome-screensaver has been compiled with support for" ; \
	             echo "***           Pluggable Authentication Modules (PAM).  However, you may need to" ; \
	             echo "***           manually install $(PAM_PREFIX)/pam.d/gnome-screensaver" ; \
	             echo "***           to /etc/pam.d/gnome-screensaver.  Otherwise, unlocking might" ; \
	             echo "***           not work." ; \
	             echo "***" ; \
	             echo "***           Note: If you are using SuSE/Novell you may have to modify this file." ; \
	             echo "***" ; \
		fi; \
	   fi; \
	   if test $$system = SunOS; then \
	        echo "***" ; \
	        echo "***  Warning: gnome-screensaver has been compiled with support for" ; \
	        echo "***           Pluggable Authentication Modules (PAM).  However, you" ; \
	        echo "***           need to manually add authentication for gnome-screensaver in" ; \
	        echo "***           $(PAM_PREFIX)/pam.conf or /etc/pam.conf." ; \
	        echo "***           Otherwise, unlocking might not work." ; \
	        echo "***" ; \
	   fi; \
	fi

-include $(top_srcdir)/git.mk
