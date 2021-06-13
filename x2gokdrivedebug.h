/*
 * X2GoKDrive - A kdrive X server for X2Go (based on Xephyr)
 *             Author Oleksandr Shneyder <o.shneyder@phoca-gmbh.de>
 *
 * Copyright © 2018 phoca-GmbH
 *
 *
 *
 * Xephyr - A kdrive X server thats runs in a host X window.
 *          Authored by Matthew Allum <mallum@o-hand.com>
 *
 * Copyright © 2004 Nokia
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef X2GOKDRIVEDEBUG_H
#define X2GOKDRIVEDEBUG_H

// # warning DEBUG ENABLED
// #define EPHYR_WANT_DEBUG

#ifdef EPHYR_WANT_DEBUG
#define EPHYR_DBG(x, a...) \
if(pthread_self()==debug_sendThreadId)\
fprintf(stderr,"SEND:"__FILE__ ":%d,%s() " x "\n", __LINE__, __func__, ##a);\
else if (pthread_self()==debug_selectThreadId)\
fprintf(stderr,"SEL:"__FILE__ ":%d,%s() " x "\n", __LINE__, __func__, ##a);\
else \
fprintf(stderr,"MAIN:"__FILE__ ":%d,%s() " x "\n", __LINE__, __func__, ##a)
#else
#define EPHYR_DBG(x, a...) do {} while (0)
#endif

#ifdef EPHYR_WANT_DEBUG
unsigned long long int debug_sendThreadId;
unsigned long long int debug_selectThreadId;
#endif /* EPHYR_WANT_DEBUG */

#endif /* X2GOKDRIVE_H */
