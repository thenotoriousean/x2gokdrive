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

#ifndef X2GOKDRIVE_H
#define X2GOKDRIVE_H
#include <stdio.h>
#include <unistd.h>
#include <libgen.h>
#include <xcb/xcb_image.h>

#include "os.h"                 /* for OsSignal() */
#include "kdrive.h"
#include "x2gokdriveremote.h"
//#include "exa.h"

#ifdef RANDR
#include "randrstr.h"
#endif

#include "damage.h"

extern char client_host_addr[64];

typedef struct _ephyrPriv {
    CARD8 *base;
    int bytes_per_line;
} EphyrPriv;


struct VirtScreen
{
    uint16_t width, height;
    int16_t x,y;
    BOOL isPrimary;
};


typedef struct _ephyrScrPriv {
    /* ephyr server info */
    struct VirtScreen* virtualScreens;
    Bool localRandrCall;
    Rotation randr;
    Bool shadow;
    DamagePtr pDamage;

    unsigned char *img;
    Bool win_explicit_position;
    int win_x, win_y;
    int win_width, win_height;
    int server_depth;
    const char *output;         /* Set via -output option */
    unsigned char *fb_data;     /* only used when host bpp != server bpp */
    xcb_shm_segment_info_t shminfo;

    KdScreenInfo *screen;
    int mynum;                  /* Screen number */
    unsigned long cmap[256];

    ScreenBlockHandlerProcPtr   BlockHandler;

} EphyrScrPriv;

extern KdCardFuncs ephyrFuncs;
extern KdKeyboardInfo *ephyrKbd;
extern KdPointerInfo *ephyrMouse;

extern miPointerScreenFuncRec ephyrPointerScreenFuncs;

Bool
ephyrResizeScreen (ScreenPtr           pScreen,
                   int                  newwidth,
                   int                  newheight, struct VirtScreen* virtualScreens);

void addOutput(ScreenPtr pScreen, char* name, int width, int height, int x, int y, BOOL primary, BOOL connected);

void updateOutput(ScreenPtr pScreen, RROutputPtr output, int width, int height, int x, int y, BOOL primary, BOOL connected);

void
 ephyrClientMouseMotion(int x,int y);

void
 ephyrClientButton(int event_type, int state, int button);

void
 ephyrClientKey(int event_type, int state, int key);

Bool
 ephyrInitialize(KdCardInfo * card, EphyrPriv * priv);

Bool
 ephyrCardInit(KdCardInfo * card);

Bool
ephyrScreenInitialize(KdScreenInfo *screen);

Bool
 ephyrInitScreen(ScreenPtr pScreen);

Bool
 ephyrFinishInitScreen(ScreenPtr pScreen);

Bool
 ephyrCreateResources(ScreenPtr pScreen);

void
 ephyrPreserve(KdCardInfo * card);

Bool
 ephyrEnable(ScreenPtr pScreen);

Bool
 ephyrDPMS(ScreenPtr pScreen, int mode);

void
 ephyrDisable(ScreenPtr pScreen);

void
 ephyrRestore(KdCardInfo * card);

void
 ephyrScreenFini(KdScreenInfo * screen);

void
ephyrCloseScreen(ScreenPtr pScreen);

void
 ephyrCardFini(KdCardInfo * card);

void
 ephyrGetColors(ScreenPtr pScreen, int n, xColorItem * pdefs);

void
 ephyrPutColors(ScreenPtr pScreen, int n, xColorItem * pdefs);

Bool
 ephyrMapFramebuffer(KdScreenInfo * screen);

void *ephyrWindowLinear(ScreenPtr pScreen,
                        CARD32 row,
                        CARD32 offset, int mode, CARD32 *size, void *closure);

void
 ephyrSetScreenSizes(ScreenPtr pScreen);

Bool
 ephyrUnmapFramebuffer(KdScreenInfo * screen);

void
 ephyrUnsetInternalDamage(ScreenPtr pScreen);

Bool
 ephyrSetInternalDamage(ScreenPtr pScreen);

Bool
 ephyrCreateColormap(ColormapPtr pmap);

#ifdef RANDR
Bool
 ephyrRandRGetInfo(ScreenPtr pScreen, Rotation * rotations);

Bool
ephyrRandRSetConfig(ScreenPtr pScreen,
                    Rotation randr, int rate, RRScreenSizePtr pSize);

Bool
ephyrRandRSetCRTC(ScreenPtr pScreen, RRCrtcPtr crtc, RRModePtr mode,
                  int x,
                  int y, Rotation randr, int numOutputs, RROutputPtr * outputs);


Bool
 ephyrRandRInit(ScreenPtr pScreen);

void
 ephyrShadowUpdate(ScreenPtr pScreen, shadowBufPtr pBuf);

#endif

void
 ephyrUpdateModifierState(unsigned int state);

extern KdPointerDriver EphyrMouseDriver;

extern KdKeyboardDriver EphyrKeyboardDriver;

#if XORG_VERSION_CURRENT < 11999901
extern KdOsFuncs EphyrOsFuncs;
#endif /* XORG_VERSION_CURRENT */

extern Bool ephyrCursorInit(ScreenPtr pScreen);

extern int ephyrBufferHeight(KdScreenInfo * screen);

/* ephyr_draw.c */

Bool
 ephyrDrawInit(ScreenPtr pScreen);

void
 ephyrDrawEnable(ScreenPtr pScreen);

void
 ephyrDrawDisable(ScreenPtr pScreen);

void
 ephyrDrawFini(ScreenPtr pScreen);

#endif /* X2GOKDRIVE_H */
