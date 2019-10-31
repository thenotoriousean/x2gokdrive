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

#ifdef HAVE_CONFIG_H
#include <dix-config.h>

#if XORG_VERSION_CURRENT < 11999901
#include <kdrive-config.h>
#endif /* XORG_VERSION_CURRENT */

#endif

#include "x2gokdriveselection.h"

#include "selection.h"
#include <X11/Xatom.h>
#include "propertyst.h"
#include "xace.h"

static struct _remoteHostVars *remoteVars = NULL;

static Atom atomPrimary, atomClipboard, atomTargets, atomString, atomUTFString, atomTimestamp = {0};
static Atom imageAtom = 0;
static Atom atomJPEG, atomJPG = {0};

static int (*proc_send_event_orig)(ClientPtr);
static int (*proc_convert_selection_orig)(ClientPtr);
static int (*proc_change_property_orig)(ClientPtr);

static int create_selection_window(void);

int own_selection(int target)
{
    Selection *pSel = NULL;
    SelectionInfoRec info = {0};
    Atom selection=atomPrimary;
    int rc;

    if(remoteVars->selstruct.selectionMode == CLIP_SERVER || remoteVars->selstruct.selectionMode == CLIP_NONE)
    {
        EPHYR_DBG("CLIENT selection disabled, not accepting selection event");
        return Success;
    }

    if(target==CLIPBOARD)
        selection=atomClipboard;


    rc = create_selection_window();

    if (rc != Success)
        return rc;

    rc = dixLookupSelection(&pSel, selection, serverClient, DixSetAttrAccess);
    if (rc == Success)
    {
        if (pSel->client && (pSel->client != serverClient))
        {
            xEvent event =
            {
                .u.selectionClear.time = currentTime.milliseconds,
                .u.selectionClear.window = pSel->window,
                .u.selectionClear.atom = pSel->selection
            };
            event.u.u.type = SelectionClear;
            WriteEventsToClient(pSel->client, 1, &event);
        }
    }
    else if (rc == BadMatch)
    {
        pSel = dixAllocateObjectWithPrivates(Selection, PRIVATE_SELECTION);
        if (!pSel)
            return BadAlloc;

        pSel->selection = selection;

        rc = XaceHookSelectionAccess(serverClient, &pSel,
                                     DixCreateAccess | DixSetAttrAccess);
        if (rc != Success)
        {
            free(pSel);
            return rc;
        }

        pSel->next = CurrentSelections;
        CurrentSelections = pSel;
    }
    else
        return rc;

    pSel->lastTimeChanged = currentTime;
    pSel->window = remoteVars->selstruct.clipWinId;
    pSel->pWin = remoteVars->selstruct.clipWinPtr;
    pSel->client = serverClient;

    EPHYR_DBG("OWN selection: %s", NameForAtom(selection));

    info.selection = pSel;
    info.client = serverClient;
    info.kind = SelectionSetOwner;
    CallCallbacks(&SelectionCallback, &info);

    return Success;
}


static int create_selection_window(void)
{
    int result = -1;

    if(remoteVars->selstruct.clipWinPtr)
        return Success;


    remoteVars->selstruct.clipWinId = FakeClientID(0);

    remoteVars->selstruct.clipWinPtr = CreateWindow(remoteVars->selstruct.clipWinId, remoteVars->ephyrScreen->pScreen->root,
                                         0, 0, 100, 100, 0, InputOnly,
                                         0, NULL, 0, serverClient,
                                         CopyFromParent, &result);
    if (!remoteVars->selstruct.clipWinPtr)
    {
        EPHYR_DBG("Can't create selection window");
        return result;
    }

    if (!AddResource(remoteVars->selstruct.clipWinPtr->drawable.id, RT_WINDOW, remoteVars->selstruct.clipWinPtr))
        return BadAlloc;

    EPHYR_DBG("Selection window created %d, %p",remoteVars->selstruct.clipWinId, remoteVars->selstruct.clipWinPtr);
    return Success;
}


static void request_selection(Atom selection, Atom rtype)
{
    xEvent ev = {{0}};
    Selection *selPtr = NULL;
    int rc = -1;

    rc = create_selection_window();
    if (rc != Success)
    {
        EPHYR_DBG("ERROR! Can't create selection Window");
        return;
    }

    /*EPHYR_DBG("Request: %s, %s",
     *              NameForAtom(selection), NameForAtom(rtype));*/

    rc = dixLookupSelection(&selPtr, selection, serverClient, DixGetAttrAccess);
    if (rc != Success)
        return;
    ev.u.u.type = SelectionRequest;
    ev.u.selectionRequest.owner = selPtr->window;
    ev.u.selectionRequest.time = currentTime.milliseconds;
    ev.u.selectionRequest.requestor = remoteVars->selstruct.clipWinId;
    ev.u.selectionRequest.selection = selection;
    ev.u.selectionRequest.target = rtype;
    ev.u.selectionRequest.property = rtype;
    WriteEventsToClient(selPtr->client, 1, &ev);
}



static void selection_callback(CallbackListPtr *callbacks,
                               void * data, void * args)
{
    SelectionInfoRec *info = (SelectionInfoRec *) args;

    if (info->kind != SelectionSetOwner)
        return;
    if (info->client == serverClient)
        return;

    /*
     *    EPHYR_DBG("Selection owner changed: %s",
     *              NameForAtom(info->selection->selection));*/

    if ((info->selection->selection != atomPrimary) &&
        (info->selection->selection != atomClipboard))
        return;

    if(remoteVars->selstruct.selectionMode == CLIP_BOTH || remoteVars->selstruct.selectionMode == CLIP_SERVER)
       request_selection(info->selection->selection, atomTargets);
}

static Atom find_atom_by_name(const char* name, const Atom list[], size_t size)
{
    for (int i = 0;i < size;i++)
    {
        if(!strcmp(name, NameForAtom (list[i])))
        {
            EPHYR_DBG("Found IMAGE ATOM %s:%d", NameForAtom (list[i]), list[i]);
            return list [i];
        }
    }
    return 0;
}

static BOOL find_image_atom(const Atom list[], size_t size)
{
    Atom at = {0};
    at=find_atom_by_name("image/jpg",list,size);
    if(at)
    {
        imageAtom=at;
        return TRUE;
    }
    at=find_atom_by_name("image/jpeg",list,size);
    if(at)
    {
        imageAtom=at;
        return TRUE;
    }
    at=find_atom_by_name("image/png",list,size);
    if(at)
    {
        imageAtom=at;
        return TRUE;
    }
    at=find_atom_by_name("image/bmp",list,size);
    if(at)
    {
        imageAtom=at;
        return TRUE;
    }
    at=find_atom_by_name("image/xpm",list,size);
    if(at)
    {
        imageAtom=at;
        return TRUE;
    }
    at=find_atom_by_name("PIXMAP",list,size);
    if(at)
    {
        imageAtom=at;
        return TRUE;
    }
    at=find_atom_by_name("image/ico",list,size);
    if(at)
    {
        imageAtom=at;
        return TRUE;
    }

    imageAtom=0;
    return FALSE;

}

//static void listAtoms(const Atom list[], size_t size)
//{
//    size_t i;
//
//    for (i = 0;i < size;i++)
//    {
//        EPHYR_DBG("%d:%s", list[i], NameForAtom( list[i]));
//    }
//}

static Bool prop_has_atom(Atom atom, const Atom list[], size_t size)
{
    size_t i = 0;

    for (i = 0;i < size;i++) {
        if (list[i] == atom)
            return TRUE;
    }

    return FALSE;
}

static void process_selection(Atom selection, Atom target,
                              Atom property, Atom requestor,
                              TimeStamp time)
{
    PropertyPtr prop;
    int rc = -1;

    rc = dixLookupProperty(&prop, remoteVars->selstruct.clipWinPtr, property,
                           serverClient, DixReadAccess);

    if(rc== BadMatch)
    {
        EPHYR_DBG("BAD MATCH!!!");
    }

    if (rc != Success)
        return;

    EPHYR_DBG("Selection notification for %s (target %s, property %s, type %s)",
              NameForAtom(selection), NameForAtom(target),
              NameForAtom(property), NameForAtom(prop->type));

    if (target != property)
        return;

    if (target == atomTargets)
    {
        if (prop->format != 32)
            return;
        if (prop->type != XA_ATOM)
            return;

//        listAtoms((const Atom*)prop->data, prop->size);


        if(prop_has_atom(atomUTFString, (const Atom*)prop->data, prop->size))
        {
            request_selection(selection, atomUTFString);
        }
        else if(prop_has_atom(atomString, (const Atom*)prop->data, prop->size))
        {
            request_selection(selection, atomString);
        }
        else
        {
            /* requesting pixmap only for clipboard */
            if((selection == atomClipboard) && find_image_atom((const Atom*)prop->data, prop->size) && imageAtom)
            {
                request_selection(selection, imageAtom);
            }
        }
    }
    else if (target == atomString || target == atomUTFString  || (target==imageAtom && imageAtom) )
    {
        int format=STRING;
        if(target==atomUTFString)
        {
            format=UTF_STRING;
        }else if(target == imageAtom)
        {
            format=PIXMAP;
        }
        /* read incrementinal data only for clipboard */
        if(!strcmp( NameForAtom(prop->type), "INCR") && selection==atomClipboard)
        {
            EPHYR_DBG("GOT INCR PROPERTY: %d",*((int*)prop->data));
            remoteVars->selstruct.readingIncremental=TRUE;
            pthread_mutex_lock(&remoteVars->sendqueue_mutex);

            remoteVars->selstruct.clipboard.size=*((int*)prop->data);
            remoteVars->selstruct.clipboard.data=malloc(remoteVars->selstruct.clipboard.size);
            remoteVars->selstruct.clipboard.mimeData=format;
            remoteVars->selstruct.incrementalPosition=0;
            pthread_mutex_unlock(&remoteVars->sendqueue_mutex);

            DeleteProperty(serverClient,remoteVars->selstruct.clipWinPtr, property);
            return;
        }

        if (prop->format != 8)
            return;
        if (prop->type != atomString && prop->type != atomUTFString && prop->type!=imageAtom)
            return;

        pthread_mutex_lock(&remoteVars->sendqueue_mutex);

        remoteVars->selstruct.readingIncremental=FALSE;
        if(selection==atomClipboard || selection ==atomPrimary)
        {
            outputBuffer* buff;
            if(selection==atomClipboard)
                buff=&remoteVars->selstruct.clipboard;
            else
                buff=&remoteVars->selstruct.selection;
            if(buff->data)
            {
                free(buff->data);
            }
            buff->size=prop->size;
            buff->data=malloc(buff->size);
            memcpy(buff->data, prop->data, prop->size);
            buff->changed=TRUE;
            buff->mimeData=format;
//            EPHYR_DBG("Have new Clipboard %s %d",remoteVars->selstruct.clipboard, remoteVars->selstruct.clipboardSize);
        }
        pthread_cond_signal(&remoteVars->have_sendqueue_cond);
        pthread_mutex_unlock(&remoteVars->sendqueue_mutex);
    }
    DeleteProperty(serverClient,remoteVars->selstruct.clipWinPtr, property);
}




#define SEND_EVENT_BIT 0x80

static int send_event(ClientPtr client)
{
    REQUEST(xSendEventReq);
    REQUEST_SIZE_MATCH(xSendEventReq);

    stuff->event.u.u.type &= ~(SEND_EVENT_BIT);


    if (stuff->event.u.u.type == SelectionNotify &&
        stuff->event.u.selectionNotify.requestor == remoteVars->selstruct.clipWinId)
    {
        TimeStamp time;
        time = ClientTimeToServerTime(stuff->event.u.selectionNotify.time);

        process_selection(stuff->event.u.selectionNotify.selection,
                          stuff->event.u.selectionNotify.target,
                          stuff->event.u.selectionNotify.property,
                          stuff->event.u.selectionNotify.requestor,
                          time);
    }

    return proc_send_event_orig(client);
}

static int convert_selection(ClientPtr client, Atom selection,
                               Atom target, Atom property,
                               Window requestor, CARD32 time)
{
    Selection *pSel = NULL;
    WindowPtr pWin = {0};
    int rc = -1;

    Atom realProperty = {0};
    xEvent event = {{0}};

    inputBuffer* buff=&remoteVars->selstruct.inSelection;
    if(selection==atomClipboard)
        buff=&remoteVars->selstruct.inClipboard;

//    EPHYR_DBG("Selection request for %s (type %s)",  NameForAtom(selection), NameForAtom(target));

    rc = dixLookupSelection(&pSel, selection, client, DixGetAttrAccess);
    if (rc != Success)
        return rc;

    rc = dixLookupWindow(&pWin, requestor, client, DixSetAttrAccess);
    if (rc != Success)
        return rc;

    if (property != None)
        realProperty = property;
    else
        realProperty = target;


    if (target == atomTargets)
    {
        Atom string_targets[] = { atomTargets, atomTimestamp, atomUTFString };
        Atom pixmap_targets[] = { atomTargets, atomTimestamp, atomJPEG, atomJPG };
        if(buff->mimeData == PIXMAP)
        {
            rc = dixChangeWindowProperty(serverClient, pWin, realProperty,
                                         XA_ATOM, 32, PropModeReplace,
                                         sizeof(pixmap_targets)/sizeof(pixmap_targets[0]),
                                         pixmap_targets, TRUE);
        }
        else
        {
            rc = dixChangeWindowProperty(serverClient, pWin, realProperty,
                                         XA_ATOM, 32, PropModeReplace,
                                         sizeof(string_targets)/sizeof(string_targets[0]),
                                         string_targets, TRUE);
        }
        if (rc != Success)
            return rc;
    }
    else if (target == atomTimestamp)
    {
        rc = dixChangeWindowProperty(serverClient, pWin, realProperty,
                                     XA_INTEGER, 32, PropModeReplace, 1,
                                     &pSel->lastTimeChanged.milliseconds,
                                     TRUE);
        if (rc != Success)
            return rc;
    }
    else if (target == atomUTFString || target == atomJPEG || target == atomJPG )
    {
        rc = dixChangeWindowProperty(serverClient, pWin, realProperty,
                                     target, 8, PropModeReplace,
                                     buff->size, buff->data, TRUE);
        if (rc != Success)
            return rc;
    }
    else
    {
        return BadMatch;
    }

    event.u.u.type = SelectionNotify;
    event.u.selectionNotify.time = time;
    event.u.selectionNotify.requestor = requestor;
    event.u.selectionNotify.selection = selection;
    event.u.selectionNotify.target = target;
    event.u.selectionNotify.property = property;
    WriteEventsToClient(client, 1, &event);
    return Success;
}


static int proc_convert_selection(ClientPtr client)
{
    Bool paramsOkay;
    WindowPtr pWin = {0};
    Selection *pSel = NULL;
    int rc = -1;

    REQUEST(xConvertSelectionReq);
    REQUEST_SIZE_MATCH(xConvertSelectionReq);
    rc = dixLookupWindow(&pWin, stuff->requestor, client, DixSetAttrAccess);
    if (rc != Success)
       return rc;
    paramsOkay = ValidAtom(stuff->selection) && ValidAtom(stuff->target);
    paramsOkay &= (stuff->property == None) || ValidAtom(stuff->property);
    if (!paramsOkay)
    {
        client->errorValue = stuff->property;
        return BadAtom;
    }

    rc = dixLookupSelection(&pSel, stuff->selection, client, DixReadAccess);
    if (rc == Success && pSel->client == serverClient &&  pSel->window == remoteVars->selstruct.clipWinId)
    {
        rc = convert_selection(client, stuff->selection,
                                 stuff->target, stuff->property,
                                 stuff->requestor, stuff->time);
        if (rc != Success)
        {
            xEvent event;
            memset(&event, 0, sizeof(xEvent));
            event.u.u.type = SelectionNotify;
            event.u.selectionNotify.time = stuff->time;
            event.u.selectionNotify.requestor = stuff->requestor;
            event.u.selectionNotify.selection = stuff->selection;
            event.u.selectionNotify.target = stuff->target;
            event.u.selectionNotify.property = None;
            WriteEventsToClient(client, 1, &event);
        }

        return Success;
    }

    return proc_convert_selection_orig(client);
}


static int proc_change_property(ClientPtr client)
{
    int rc = -1;
    BOOL incRead;
    PropertyPtr prop = {0};

    REQUEST(xChangePropertyReq);

    REQUEST_AT_LEAST_SIZE(xChangePropertyReq);

    rc=proc_change_property_orig(client);

    if(rc!=Success)
        return rc;


    pthread_mutex_lock(&remoteVars->sendqueue_mutex);
    incRead=remoteVars->selstruct.readingIncremental;
    pthread_mutex_unlock(&remoteVars->sendqueue_mutex);


    if(stuff->window == remoteVars->selstruct.clipWinId && incRead &&
        ((imageAtom && imageAtom==stuff->type)||(stuff->type == atomUTFString) || (stuff->type == atomString)) )
        EPHYR_DBG("HAVE NEW DATA for %d: %s %s", stuff->window, NameForAtom(stuff->property), NameForAtom(stuff->type));
    else
        return rc;



    rc = dixLookupProperty(&prop, remoteVars->selstruct.clipWinPtr, stuff->property,
                           serverClient, DixReadAccess);

    if(rc== BadMatch)
    {
        EPHYR_DBG("BAD MATCH!!!");
        return Success;
    }

    if (rc != Success)
        return rc;

    EPHYR_DBG("Have %d bytes for %s ",
              prop->size, NameForAtom(stuff->property));

    pthread_mutex_lock(&remoteVars->sendqueue_mutex);
    if(prop->size==0)
    {
        EPHYR_DBG("READ %d FROM %d", remoteVars->selstruct.incrementalPosition, remoteVars->selstruct.clipboard.size);
        remoteVars->selstruct.readingIncremental=FALSE;
        if(remoteVars->selstruct.incrementalPosition == remoteVars->selstruct.clipboard.size)
        {
            remoteVars->selstruct.clipboard.changed=TRUE;
            pthread_cond_signal(&remoteVars->have_sendqueue_cond);
        }
    }
    else
    {
        memcpy(remoteVars->selstruct.clipboard.data+remoteVars->selstruct.incrementalPosition, prop->data, prop->size);
        remoteVars->selstruct.incrementalPosition+=prop->size;
    }

    pthread_mutex_unlock(&remoteVars->sendqueue_mutex);


    DeleteProperty(serverClient,remoteVars->selstruct.clipWinPtr, stuff->property);


    return rc;
}

void selection_init(struct _remoteHostVars *obj)
{
    EPHYR_DBG("INITIALIZING selections");
    remoteVars=obj;
    proc_convert_selection_orig = ProcVector[X_ConvertSelection];
    proc_send_event_orig = ProcVector[X_SendEvent];
    proc_change_property_orig = ProcVector[X_ChangeProperty];
    ProcVector[X_ConvertSelection] = proc_convert_selection;
    ProcVector[X_SendEvent] = send_event;
    ProcVector[X_ChangeProperty] = proc_change_property;
    remoteVars->selstruct.callBackInstalled=TRUE;
    install_selection_callbacks();
}


void install_selection_callbacks(void)
{
    if(remoteVars->selstruct.selectionMode == CLIP_CLIENT || remoteVars->selstruct.selectionMode == CLIP_NONE)
    {
        EPHYR_DBG("SERVER CLIPBOARD disabled, not installing callbacks");
        return;
    }

    atomPrimary = MakeAtom("PRIMARY", 7, TRUE);
    atomClipboard = MakeAtom("CLIPBOARD", 9, TRUE);

    atomTargets = MakeAtom("TARGETS", 7, TRUE);
    atomTimestamp = MakeAtom("TIMESTAMP", 9, TRUE);
    atomString = MakeAtom("STRING", 6, TRUE);
    atomUTFString = MakeAtom("UTF8_STRING", 11, TRUE);

    atomJPEG = MakeAtom("image/jpeg",487,TRUE);
    atomJPG = MakeAtom("image/jpg",488,TRUE);

    remoteVars->selstruct.clipWinPtr=0;

    /* try to dele callback to avaid double call */
    DeleteCallback(&SelectionCallback, selection_callback, 0);

    if (!AddCallback(&SelectionCallback, selection_callback, 0))
        FatalError("Failed to install callback\n");
    else
        EPHYR_DBG("Selection callback installed");

}
