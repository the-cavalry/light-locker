# This file will be processed with automake-1.7 to create Makefile.in

AUTOMAKE_OPTIONS = 1.7

NULL =

SUBDIRS =			\
	src			\
	po 			\
	data			\
	$(NULL)

EXTRA_DIST =			\
	COPYING			\
	COPYING.LIB		\
	AUTHORS			\
	INSTALL			\
	README			\
	ChangeLog		\
	NEWS			\
	HACKING			\
	intltool-extract.in     \
	intltool-merge.in       \
	intltool-update.in	\
	$(NULL)

DISTCLEANFILES =		\
	intltool-extract	\
	intltool-merge		\
	intltool-update		\
	./po/.intltool-merge-cache	\
	$(NULL)

MAINTAINERCLEANFILES =		\
	*~			\
	intltool-*.in		\
	compile			\
	configure		\
	configure.ac		\
	INSTALL			\
	install-sh		\
	missing			\
	mkinstalldirs		\
	config.guess		\
	ltmain.sh		\
	config.sub		\
	depcomp			\
	Makefile.in		\
	config.h.*		\
	aclocal.m4		\
	acinclude.m4		\
	ChangeLog		\
	$(NULL)


.PHONY: ChangeLog

ChangeLog: Makefile
	(GIT_DIR=$(top_srcdir)/.git git log > .changelog.tmp \
	&& mv .changelog.tmp ChangeLog; rm -f .changelog.tmp) \
	|| (touch ChangeLog; echo 'Git directory not found: installing possibly empty changelog.' >&2)

dist-hook: ChangeLog


-include $(top_srcdir)/git.mk
