## Process this file with automake to produce makefile.in

AUTOMAKE_OPTIONS = 1.7

AM_CFLAGS =				\
	$(WARNING_CFLAGS)

MAINTAINERCLEANFILES =                  \
	*~				\
        Makefile.in

noinst_LIBRARIES =			\
	libfast-user-switch.a

libfast_user_switch_a_CPPFLAGS =				\
	-DDATADIR=\""$(datadir)"\"				\
	-DG_LOG_DOMAIN=\""Screensaver-Fast-User-Switch"\"	\
	$(FAST_USER_SWITCH_CFLAGS)

libfast_user_switch_a_SOURCES =		\
        fusa-user.h                     \
        fusa-user-menu-item.h           \
        fusa-user-private.h             \
        fusa-display.h                  \
        fusa-display-private.h          \
	fusa-display.c			\
        fusa-manager.h                  \
        fusa-manager-private.h          \
	fusa-manager.c			\
	fusa-user.c			\
	fusa-user-menu-item.c		\
        fusa-utils.h                    \
	fusa-utils.c			\
        gdm-queue.h			\
	gdm-queue.c
