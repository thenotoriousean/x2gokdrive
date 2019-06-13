/*
 * X2GoKdrive - A kdrive X server for X2Go (based on Xephyr)
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

#ifdef HAVE_CONFIG_H
#include <dix-config.h>
#endif
#include "x2gokdrive.h"
#include "x2gokdrivelog.h"
#include "remote.h"
#include "cursorstr.h"
#include <xcb/render.h>
#include <xcb/xcb_renderutil.h>

static DevPrivateKeyRec ephyrCursorPrivateKey;

typedef struct _ephyrCursor {
    xcb_cursor_t cursor;
} ephyrCursorRec, *ephyrCursorPtr;

static ephyrCursorPtr
ephyrGetCursor(CursorPtr cursor)
{
    return dixGetPrivateAddr(&cursor->devPrivates, &ephyrCursorPrivateKey);
}


static void
ephyrRealizeCoreCursor(EphyrScrPriv *scr, CursorPtr cursor)
{
}

static void
ephyrRealizeARGBCursor(EphyrScrPriv *scr, CursorPtr cursor)
{
}

static Bool
can_argb_cursor(void)
{
    return TRUE;
}

static Bool
ephyrRealizeCursor(DeviceIntPtr dev, ScreenPtr screen, CursorPtr cursor)
{
    KdScreenPriv(screen);
    KdScreenInfo *kscr = pScreenPriv->screen;
    EphyrScrPriv *scr = kscr->driver;
    return TRUE;
}

static Bool
ephyrUnrealizeCursor(DeviceIntPtr dev, ScreenPtr screen, CursorPtr cursor)
{
    ephyrCursorPtr hw = ephyrGetCursor(cursor);

    if (hw->cursor) {
        remote_removeCursor(cursor->serialNumber);
        hw->cursor = None;
    }

    return TRUE;
}

static void
ephyrSetCursor(DeviceIntPtr dev, ScreenPtr screen, CursorPtr cursor, int x,
               int y)
{
    KdScreenPriv(screen);
    KdScreenInfo *kscr = pScreenPriv->screen;
    EphyrScrPriv *scr = kscr->driver;
    uint32_t attr = None;
    if(cursor)
        remote_sendCursor(cursor);
}

static void
ephyrMoveCursor(DeviceIntPtr dev, ScreenPtr screen, int x, int y)
{
}

static Bool
ephyrDeviceCursorInitialize(DeviceIntPtr dev, ScreenPtr screen)
{
    return TRUE;
}

static void
ephyrDeviceCursorCleanup(DeviceIntPtr dev, ScreenPtr screen)
{
}

miPointerSpriteFuncRec EphyrPointerSpriteFuncs = {
    ephyrRealizeCursor,
    ephyrUnrealizeCursor,
    ephyrSetCursor,
    ephyrMoveCursor,
    ephyrDeviceCursorInitialize,
    ephyrDeviceCursorCleanup
};

Bool
ephyrCursorInit(ScreenPtr screen)
{
    if (!dixRegisterPrivateKey(&ephyrCursorPrivateKey, PRIVATE_CURSOR_BITS,
                               sizeof(ephyrCursorRec)))
        return FALSE;

    miPointerInitialize(screen,
                        &EphyrPointerSpriteFuncs,
                        &ephyrPointerScreenFuncs, FALSE);

    return TRUE;
}
