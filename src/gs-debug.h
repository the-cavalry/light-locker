/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2005 William Jon McCann <mccann@jhu.edu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Authors: William Jon McCann <mccann@jhu.edu>
 *
 */

#ifndef __GS_DEBUG_H
#define __GS_DEBUG_H

#include <stdarg.h>
#include <glib.h>

G_BEGIN_DECLS

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define gs_debug(...) gs_debug_real (__func__, __FILE__, __LINE__, __VA_ARGS__)
#elif defined(__GNUC__) && __GNUC__ >= 3
#define gs_debug(...) gs_debug_real (__FUNCTION__, __FILE__, __LINE__, __VA_ARGS__)
#else
#define gs_debug(...)
#endif

void gs_debug_init             (gboolean debug,
                                gboolean to_file);
gboolean gs_debug_enabled      (void);
void gs_debug_shutdown         (void);
void gs_debug_real             (const char *func,
                                const char *file,
                                int         line,
                                const char *format, ...);

#ifdef ENABLE_PROFILING
#ifdef G_HAVE_ISO_VARARGS
#define gs_profile_start(...) _gs_profile_log (G_STRFUNC, "start", __VA_ARGS__)
#define gs_profile_end(...)   _gs_profile_log (G_STRFUNC, "end", __VA_ARGS__)
#define gs_profile_msg(...)   _gs_profile_log (NULL, NULL, __VA_ARGS__)
#elif defined(G_HAVE_GNUC_VARARGS)
#define gs_profile_start(format...) _gs_profile_log (G_STRFUNC, "start", format)
#define gs_profile_end(format...)   _gs_profile_log (G_STRFUNC, "end", format)
#define gs_profile_msg(format...)   _gs_profile_log (NULL, NULL, format)
#endif
#else
#define gs_profile_start(...)
#define gs_profile_end(...)
#define gs_profile_msg(...)
#endif

void            _gs_profile_log    (const char *func,
                                    const char *note,
                                    const char *format,
                                    ...) G_GNUC_PRINTF (3, 4);

G_END_DECLS

#endif /* __GS_DEBUG_H */
