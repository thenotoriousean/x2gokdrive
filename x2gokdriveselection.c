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
#include <kdrive-config.h>
#endif

#include "x2gokdriveselection.h"

#include "selection.h"
#include <X11/Xatom.h>
#include "propertyst.h"


RemoteHostVars *remoteVars;

static Atom atomPrimary, atomClipboard, atomTargets, atomString, atomUTFString;
static Atom imageAtom=0;


static int (*proc_send_event_orig)(ClientPtr);
static int (*proc_convert_selection_orig)(ClientPtr);
static int (*proc_change_property_orig)(ClientPtr);

static int create_selection_window()
{
    int result;
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

    EPHYR_DBG("!!!!!Selection window created %d, %p",remoteVars->selstruct.clipWinId, remoteVars->selstruct.clipWinPtr);
    return Success;
}


static void request_selection(Atom selection, Atom rtype)
{
    xEvent ev;
    Selection *selPtr;
    int rc;

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
    Atom at;
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

static Bool prop_has_atom(Atom atom, const Atom list[], size_t size)
{
    size_t i;

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
    int rc;

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
            //requesting pixmap only for clipboard
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
        //read incrementinal data only for clipboard
        if(!strcmp( NameForAtom(prop->type), "INCR") && selection==atomClipboard)
        {
            EPHYR_DBG("GOT INCR PROPERTY: %d",*((int*)prop->data));
            remoteVars->selstruct.readingIncremental=TRUE;
            pthread_mutex_lock(&remoteVars->sendqueue_mutex);

            remoteVars->selstruct.clipboardSize=*((int*)prop->data);
            remoteVars->selstruct.clipboard=malloc(remoteVars->selstruct.clipboardSize);
            remoteVars->selstruct.incrementalPosition=0;
            remoteVars->selstruct.clipBoardMimeData=format;
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
        if(selection==atomClipboard)
        {
            if(remoteVars->selstruct.clipboard)
            {
                free(remoteVars->selstruct.clipboard);
            }
            remoteVars->selstruct.clipboardSize=prop->size;
            remoteVars->selstruct.clipboard=malloc(remoteVars->selstruct.clipboardSize);
            memcpy(remoteVars->selstruct.clipboard, prop->data, prop->size);
            remoteVars->selstruct.clipBoardChanged=TRUE;
            remoteVars->selstruct.clipBoardMimeData=format;
            //             EPHYR_DBG("Have new Clipboard %s %d",remoteVars->selstruct.clipboard, remoteVars->selstruct.clipboardSize);
        }
        if(selection==atomPrimary)
        {
            if(remoteVars->selstruct.selection)
            {
                free(remoteVars->selstruct.selection);
            }
            remoteVars->selstruct.selectionSize=prop->size;
            remoteVars->selstruct.selection=malloc(prop->size);
            memcpy(remoteVars->selstruct.selection, prop->data, prop->size);
            remoteVars->selstruct.selectionChanged=TRUE;
            remoteVars->selstruct.selectionMimeData=format;
            //             EPHYR_DBG("Have new Selection %s %d",remoteVars->selstruct.selection, remoteVars->selstruct.selectionSize);
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


static int proc_convert_selection(ClientPtr client)
{

    EPHYR_DBG("PROC CONVERT!!!!");
    /*    Bool paramsOkay;
     *    WindowPtr pWin;
     *    Selection *pSel;
     *    int rc;
     *
     *    REQUEST(xConvertSelectionReq);
     *    REQUEST_SIZE_MATCH(xConvertSelectionReq);
     *
     *    rc = dixLookupWindow(&pWin, stuff->requestor, client, DixSetAttrAccess);
     *    if (rc != Success)
     *        return rc;
     *
     *    paramsOkay = ValidAtom(stuff->selection) && ValidAtom(stuff->target);
     *    paramsOkay &= (stuff->property == None) || ValidAtom(stuff->property);
     *    if (!paramsOkay) {
     *        client->errorValue = stuff->property;
     *        return BadAtom;
}
*/
    return proc_convert_selection_orig(client);
}


static int proc_change_property(ClientPtr client)
{
    int rc;
    BOOL incRead;
    PropertyPtr prop;



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
        EPHYR_DBG("READ %d FROM %d", remoteVars->selstruct.incrementalPosition, remoteVars->selstruct.clipboardSize);
        remoteVars->selstruct.readingIncremental=FALSE;
        if(remoteVars->selstruct.incrementalPosition == remoteVars->selstruct.clipboardSize)
        {
            remoteVars->selstruct.clipBoardChanged=TRUE;
            pthread_cond_signal(&remoteVars->have_sendqueue_cond);
        }
    }
    else
    {
        memcpy(remoteVars->selstruct.clipboard+remoteVars->selstruct.incrementalPosition, prop->data, prop->size);
        remoteVars->selstruct.incrementalPosition+=prop->size;
    }

    pthread_mutex_unlock(&remoteVars->sendqueue_mutex);


    DeleteProperty(serverClient,remoteVars->selstruct.clipWinPtr, stuff->property);


    return rc;
}

void selection_init(struct RemoteHostVars *obj)
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


void install_selection_callbacks()
{
    atomPrimary = MakeAtom("PRIMARY", 7, TRUE);
    atomClipboard = MakeAtom("CLIPBOARD", 9, TRUE);

    atomTargets = MakeAtom("TARGETS", 7, TRUE);
    //     atomTimestamp = MakeAtom("TIMESTAMP", 9, TRUE);
    atomString = MakeAtom("STRING", 6, TRUE);
    //     xaTEXT = MakeAtom("TEXT", 4, TRUE);
    atomUTFString = MakeAtom("UTF8_STRING", 11, TRUE);

    remoteVars->selstruct.clipWinPtr=0;

    //try to dele callback to avaid double call
    DeleteCallback(&SelectionCallback, selection_callback, 0);

    if (!AddCallback(&SelectionCallback, selection_callback, 0))
        FatalError("Failed to install callback\n");
    else
        EPHYR_DBG("Selection callback installed");

}
