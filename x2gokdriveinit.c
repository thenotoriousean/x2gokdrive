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
#include "glx_extinit.h"
#include "remote.h"

extern Window EphyrPreExistingHostWin;
extern Bool EphyrWantGrayScale;
extern Bool EphyrWantResize;
extern Bool EphyrWantNoHostGrab;
extern Bool kdHasPointer;
extern Bool kdHasKbd;

#if XORG_VERSION_CURRENT < 11999901
#ifdef KDRIVE_EVDEV
extern KdPointerDriver LinuxEvdevMouseDriver;
extern KdKeyboardDriver LinuxEvdevKeyboardDriver;
#endif /* KDRIVE_EVDEV */
#endif /* XORG_VERSION_CURRENT */

void processScreenOrOutputArg(const char *screen_size, const char *output, char *parent_id);
void processOutputArg(const char *output, char *parent_id);
void processScreenArg(const char *screen_size, char *parent_id);

int
main(int argc, char *argv[], char *envp[])
{
//     hostx_use_resname(basename(argv[0]), 0);
    return dix_main(argc, argv, envp);
}


void
InitCard(char *name)
{
    EPHYR_DBG("mark");
    KdCardInfoAdd(&ephyrFuncs, 0);
}

#if XORG_VERSION_CURRENT < 11999901

static const ExtensionModule ephyrExtensions[] = {
#ifdef GLXEXT
 { GlxExtensionInit, "GLX", &noGlxExtension },
#endif
};

static
void ephyrExtensionInit(void)
{
    LoadExtensionList(ephyrExtensions, ARRAY_SIZE(ephyrExtensions), TRUE);
}

#endif /* XORG_VERSION_CURRENT */

void
InitOutput(ScreenInfo * pScreenInfo, int argc, char **argv)
{
#if XORG_VERSION_CURRENT < 11999901
    if (serverGeneration == 1)
        ephyrExtensionInit();
#endif /* XORG_VERSION_CURRENT */

    if (serverGeneration == 1)
    {
        remote_selection_init();
    }

    KdInitOutput(pScreenInfo, argc, argv);
}

void
InitInput(int argc, char **argv)
{
    KdKeyboardInfo *ki;
    KdPointerInfo *pi;

#if XORG_VERSION_CURRENT < 11999901
#ifdef KDRIVE_EVDEV
    KdAddKeyboardDriver(&LinuxEvdevKeyboardDriver);
    KdAddPointerDriver(&LinuxEvdevMouseDriver);
#endif /* KDRIVE_EVDEV */
#endif /* XORG_VERSION_CURRENT */

    if (!SeatId) {
        KdAddKeyboardDriver(&EphyrKeyboardDriver);
        KdAddPointerDriver(&EphyrMouseDriver);

        if (!kdHasKbd) {
            ki = KdNewKeyboard();
            if (!ki)
                FatalError("Couldn't create Xephyr keyboard\n");
            ki->driver = &EphyrKeyboardDriver;
            KdAddKeyboard(ki);
        }

        if (!kdHasPointer) {
            pi = KdNewPointer();
            if (!pi)
                FatalError("Couldn't create Xephyr pointer\n");
            pi->driver = &EphyrMouseDriver;
            KdAddPointer(pi);
        }
    }

    KdInitInput();
}

void
CloseInput(void)
{
    KdCloseInput();
}

#if INPUTTHREAD
/** This function is called in Xserver/os/inputthread.c when starting
    the input thread. */
void
ddxInputThreadInit(void)
{
}
#endif

#ifdef DDXBEFORERESET
void
ddxBeforeReset(void)
{
}
#endif

void
ddxUseMsg(void)
{
    KdUseMsg();
}

void
processScreenOrOutputArg(const char *screen_size, const char *output, char *parent_id)
{
    KdCardInfo *card;

    InitCard(0);                /*Put each screen on a separate card */
    card = KdCardInfoLast();

    if (card) {
        KdScreenInfo *screen;
        unsigned long p_id = 0;
        Bool use_geometry;

        screen = KdScreenInfoAdd(card);
        KdParseScreen(screen, screen_size);
        screen->driver = calloc(1, sizeof(EphyrScrPriv));
        if (!screen->driver)
            FatalError("Couldn't alloc screen private\n");

        if (parent_id) {
            p_id = strtol(parent_id, NULL, 0);
        }

        use_geometry = (strchr(screen_size, '+') != NULL);
        EPHYR_DBG("screen number:%d, size: %s, output %s\n", screen->mynum, screen_size, output);
//         hostx_add_screen(screen, p_id, screen->mynum, use_geometry, output);
    }
    else {
        ErrorF("No matching card found!\n");
    }
}

void
processScreenArg(const char *screen_size, char *parent_id)
{
    processScreenOrOutputArg(screen_size, NULL, parent_id);
}

void
processOutputArg(const char *output, char *parent_id)
{
    processScreenOrOutputArg("100x100+0+0", output, parent_id);
}

int
ddxProcessArgument(int argc, char **argv, int i)
{
    static char *parent = NULL;
    EPHYR_DBG("mark argv[%d]='%s'", i, argv[i]);

    if (!strcmp(argv[i], "-geometry"))
    {
        if ((i + 1) < argc)
        {
            //compat with nxagent
            return 2;
        }

        UseMsg();
        exit(1);
    }
    else if (!strcmp(argv[i], "-name"))
    {
        if ((i + 1) < argc)
        {
            //compat with nxagent
            return 2;
        }

        UseMsg();
        exit(1);
    }
    else if (!strcmp(argv[i], "-D")) {
        //compat with nxagent
        return 1;
    }
    else if (!strcmp(argv[i], "-K")) {
        //compat with nxagent
        return 1;
    }

    return KdProcessArgument(argc, argv, i);
}

void
OsVendorInit(void)
{
    EPHYR_DBG("mark");

    restartTimerOnInit();

/*     if (SeatId)
         hostx_use_sw_cursor();
*/

//     if (hostx_want_host_cursor())
        ephyrFuncs.initCursor = &ephyrCursorInit;

#if XORG_VERSION_CURRENT < 11999901
    KdOsInit(&EphyrOsFuncs);
#endif
    if (serverGeneration == 1) {
        if (!KdCardInfoLast()) {
            processScreenArg("800x600", NULL);
        }
        remote_init();
    }
}

KdCardFuncs ephyrFuncs = {
    ephyrCardInit,              /* cardinit */
    ephyrScreenInitialize,      /* scrinit */
    ephyrInitScreen,            /* initScreen */
    ephyrFinishInitScreen,      /* finishInitScreen */
    ephyrCreateResources,       /* createRes */
#if XORG_VERSION_CURRENT < 11999901
    ephyrPreserve,              /* preserve */
    ephyrEnable,                /* enable */
    ephyrDPMS,                  /* dpms */
    ephyrDisable,               /* disable */
    ephyrRestore,               /* restore */
#endif /* XORG_VERSION_CURRENT */
    ephyrScreenFini,            /* scrfini */
    ephyrCardFini,              /* cardfini */

    0,                          /* initCursor */
#if XORG_VERSION_CURRENT < 11999901
    0,                          /* enableCursor */
    0,                          /* disableCursor */
    0,                          /* finiCursor */
    0,                          /* recolorCursor */
#endif /* XORG_VERSION_CURRENT */

    0,                          /* initAccel */
    0,                          /* enableAccel */
    0,                          /* disableAccel */
    0,                          /* finiAccel */

    ephyrGetColors,             /* getColors */
    ephyrPutColors,             /* putColors */

    ephyrCloseScreen,           /* closeScreen */
};

#if XORG_VERSION_CURRENT < 11999901
/*
 * Fake nearly empty EphyrOsFuncs for builds against X.Org << 1.19.99.901
 */
int
ephyrInitFake(void) { return 1; }

KdOsFuncs EphyrOsFuncs = {
    .Init = ephyrInitFake,
};
#endif /* XORG_VERSION_CURRENT */
