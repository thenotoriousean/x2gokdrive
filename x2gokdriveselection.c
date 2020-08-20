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

#include <xcb/xcb.h>
#include <xcb/xfixes.h>
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>


#ifdef HAVE_CONFIG_H
#include <dix-config.h>

#if XORG_VERSION_CURRENT < 11999901
#include <kdrive-config.h>
#endif // XORG_VERSION_CURRENT

#endif
#include "x2gokdriveselection.h"
static struct _remoteHostVars *remoteVars = NULL;

static xcb_atom_t ATOM_CLIPBOARD;

uint32_t max_chunk(void)
{
    if(remoteVars->selstruct.clientSupportsExetndedSelection)
        return 1024*100/4; //100KB
    else
        return 10*1024*1024/4; //10MB
}

int own_selection(enum SelectionType selection)
{
    xcb_atom_t sel=XCB_ATOM_PRIMARY;
    if(remoteVars->selstruct.selectionMode == CLIP_NONE || remoteVars->selstruct.selectionMode == CLIP_SERVER)
    {
        EPHYR_DBG("Client selection is disabled");
        return 0;
    }
    if(selection!=PRIMARY)
    {
        sel=ATOM_CLIPBOARD;
    }
    xcb_set_selection_owner(remoteVars->selstruct.xcbConnection, remoteVars->selstruct.clipWinId, sel, XCB_CURRENT_TIME);
    xcb_flush(remoteVars->selstruct.xcbConnection);
    remoteVars->selstruct.inSelection[selection].owner=TRUE;
    remoteVars->selstruct.inSelection[selection].timestamp=XCB_CURRENT_TIME;
    return 0;
}
void selection_init(struct _remoteHostVars *obj)
{
    remoteVars=obj;
    return;
}


xcb_atom_t atom(const char* name)
{
    //get atom for the name, return 0 if not found
    xcb_intern_atom_cookie_t cookie;
    xcb_intern_atom_reply_t *reply;
    xcb_atom_t a=0;

    cookie = xcb_intern_atom(remoteVars->selstruct.xcbConnection, 0, strlen(name), name);
    if ((reply = xcb_intern_atom_reply(remoteVars->selstruct.xcbConnection, cookie, NULL)))
    {
        a=reply->atom;
//         EPHYR_DBG("The %s atom has ID %u", name, a);
        free(reply);
    }
    return a;
}

char *atom_name(xcb_atom_t xatom)
{
    //get name for atom, don't forget to free return value
    char* name;
    xcb_get_atom_name_cookie_t cookie = xcb_get_atom_name(remoteVars->selstruct.xcbConnection, xatom);
    xcb_get_atom_name_reply_t *reply=xcb_get_atom_name_reply(remoteVars->selstruct.xcbConnection, cookie, NULL);

    if(!reply)
       return NULL;
    if(!reply->name_len)
    {
        free(reply);
        return NULL;
    }
    name=malloc( xcb_get_atom_name_name_length(reply)+1);
    strncpy(name, xcb_get_atom_name_name(reply),xcb_get_atom_name_name_length(reply));
    name [xcb_get_atom_name_name_length(reply)]='\0';
    free(reply);
    return name;
}

static xcb_atom_t target_has_atom(xcb_atom_t* list, size_t size, const char *name)
{
    //check if the atom which represents "name" is in the list of supported mime types
    size_t i = 0;
    xcb_atom_t a=atom(name);
    if(!a)
        return 0;

    for (i = 0;i < size;i++)
    {
        if (list[i]==a)
            return a;
    }
    return 0;
}

static int is_string_atom( xcb_atom_t at)
{
    //check if selection data is string/text
    if(!at)
        return 0;
    if( at == atom("UTF8_STRING") ||
        at == atom("text/plain;charset=utf-8") ||
        at == atom("STRING") ||
        at == atom("TEXT") ||
        at == atom("text/plain"))
        return 1;
    return 0;
}

static int is_image_atom( xcb_atom_t at)
{
    //check if selection data is image
    if(!at)
        return 0;
    if( at == atom("image/png") ||
        at == atom("image/xpm") ||
        at == atom("image/jpg") ||
        at == atom("image/jpeg") ||
        at == atom("PIXMAP") ||
        at == atom("image/bmp"))
        return 1;
    return 0;
}

static xcb_atom_t best_atom_from_target(xcb_atom_t* list, size_t size)
{
    //here we chose the best of supported formats for selection
    xcb_atom_t a;
    //selecting utf formats first
    if((a=target_has_atom(list, size, "UTF8_STRING")))
    {
//         EPHYR_DBG("selecting mime type UTF8_STRING");
        return a;
    }
    if((a=target_has_atom(list, size, "text/plain;charset=utf-8")))
    {
//         EPHYR_DBG("selecting mime type text/plain;charset=utf-8");
        return a;
    }
    if((a=target_has_atom(list, size, "STRING")))
    {
//         EPHYR_DBG( "selecting mime type STRING");
        return a;
    }
    if((a=target_has_atom(list, size, "TEXT")))
    {
//         EPHYR_DBG( "selecting mime type TEXT");
        return a;
    }
    if((a=target_has_atom(list, size, "text/plain")))
    {
//         EPHYR_DBG( "selecting mime type text/plain");
        return a;
    }

    //selecting loseless formats first
    if((a=target_has_atom(list, size, "image/png")))
    {
//         EPHYR_DBG( "selecting mime type image/png");
        return a;
    }
    if((a=target_has_atom(list, size, "image/xpm")))
    {
//         EPHYR_DBG( "selecting mime type image/xpm");
        return a;
    }
    if((a=target_has_atom(list, size, "PIXMAP")))
    {
//         EPHYR_DBG( "selecting mime type PIXMAP");
        return a;
    }
    if((a=target_has_atom(list, size, "image/bmp")))
    {
//         EPHYR_DBG( "selecting mime type image/bmp");
        return a;
    }
    if((a=target_has_atom(list, size, "image/jpg")))
    {
//         EPHYR_DBG( "selecting mime type image/jpg");
        return a;
    }
    if((a=target_has_atom(list, size, "image/jpeg")))
    {
//         EPHYR_DBG( "selecting mime type image/jpeg");
        return a;
    }
    return 0;
}

void request_selection_data( xcb_atom_t selection, xcb_atom_t target, xcb_atom_t property, xcb_timestamp_t t)
{
    //execute convert selection for primary or clipboard to get mimetypes or data (depends on target atom)
    if(!t)
        t=XCB_CURRENT_TIME;
    if(property)
    {
        xcb_delete_property(remoteVars->selstruct.xcbConnection,remoteVars->selstruct.clipWinId,property);
    }
    xcb_convert_selection(remoteVars->selstruct.xcbConnection,remoteVars->selstruct.clipWinId,selection, target, property, t);
    xcb_flush(remoteVars->selstruct.xcbConnection);
}

void read_selection_property(xcb_atom_t selection, xcb_atom_t property)
{
    xcb_atom_t* tg;
    char* stype, *sprop;
    xcb_atom_t data_atom;
    unsigned int bytes_left, bytes_read=0;
    xcb_get_property_cookie_t cookie;
    xcb_get_property_reply_t *reply;
    outputChunk* chunk;


    //request property which represents value of selection (data or mime types)
    //get max 100K of data, we don't need to send more than that over network for perfomance reasons
    cookie= xcb_get_property(remoteVars->selstruct.xcbConnection,FALSE, remoteVars->selstruct.clipWinId, property, XCB_GET_PROPERTY_TYPE_ANY, 0, max_chunk());
    reply=xcb_get_property_reply(remoteVars->selstruct.xcbConnection, cookie, NULL);
    if(!reply)
    {
//         EPHYR_DBG( "NULL reply");
    }
    else
    {
        if(reply->type==XCB_NONE)
        {
//             EPHYR_DBG( "NONE reply");
        }
        else
        {
            //here we have type of data

/*
            stype=atom_name(reply->type);
            sprop=atom_name(property);
            EPHYR_DBG( "Property %s type %s, format %d, length %d", sprop, stype, reply->format, reply->length);
            if(stype)
                free(stype);
            if(sprop)
                free(sprop);
*/
            //need to read property incrementally
            if(reply->type == atom("INCR"))
            {
                unsigned int sz=*((unsigned int*) xcb_get_property_value(reply));
//                 EPHYR_DBG( "have incr property size: %d", sz);
                remoteVars->selstruct.incrAtom=property;
                remoteVars->selstruct.incrementalSize=sz;
                remoteVars->selstruct.incrementalSizeRead=0;

                //deleteing property should tell the selection owner that we are ready for incremental reading of data
                xcb_delete_property(remoteVars->selstruct.xcbConnection,remoteVars->selstruct.clipWinId, property);
                xcb_flush(remoteVars->selstruct.xcbConnection);
                free(reply);
                return;
            }
            //we have supported mime types in reply
            if(reply->type == atom( "ATOM"))
            {
                if(reply->format!=32)
                {
                    EPHYR_DBG( "wrong format for TARGETS");
                }
                else
                {
//                     EPHYR_DBG( "target supports %lu mime types",xcb_get_property_value_length(reply)/sizeof(xcb_atom_t));

                    /*
                    #warning debug
                    tg=(xcb_atom_t*) xcb_get_property_value(reply);
                    for(int i=0;i<xcb_get_property_value_length(reply)/sizeof(xcb_atom_t);++i)
                    {
                        stype=atom_name(tg[i]);
                        EPHYR_DBG("selection  has target: %s, %d",stype, tg[i]);
                        if(stype)
                            free(stype);
                    }
                    //#debug
                    */

                    data_atom=0;
                    //get the best of supported mime types and request the selection in this format
                    data_atom=best_atom_from_target(xcb_get_property_value(reply), xcb_get_property_value_length(reply)/sizeof(xcb_atom_t));

                    xcb_delete_property( remoteVars->selstruct.xcbConnection, remoteVars->selstruct.clipWinId, property);
                    xcb_flush(remoteVars->selstruct.xcbConnection);

                    if(data_atom)
                        request_selection_data( selection, data_atom, data_atom, 0);
                    else
                    {
                        EPHYR_DBG( "there are no supported mime types in the target");
                    }
                }
            }
            else
            {
                //here we have selection as string or image
                if(is_image_atom( reply->type) || is_string_atom( reply->type))
                {
                    //read property data in loop in the chunks with size (100KB)
                    do
                    {
                        bytes_left=reply->bytes_after;
                        //now we can access property data

                        /*FILE* cp=fopen("/tmp/clip", "a");
                        fwrite(xcb_get_property_value(reply),1, xcb_get_property_value_length(reply),cp);
                        fclose(cp);*/


                        chunk=(outputChunk*) malloc(sizeof(outputChunk));

                        memset((void*)chunk,0,sizeof(outputChunk));

                        if(xcb_get_property_value_length(reply))
                        {
                            chunk->size=xcb_get_property_value_length(reply);
                            chunk->data=(unsigned char*)malloc(chunk->size);
                            memcpy(chunk->data, xcb_get_property_value(reply),chunk->size);
                        }

                        chunk->compressed=FALSE;
                        if(is_string_atom(property))
                            chunk->mimeData=UTF_STRING;
                        else
                            chunk->mimeData=PIXMAP;

                        chunk->selection=selection_from_atom(selection);


                        if(remoteVars->selstruct.incrementalSize && (remoteVars->selstruct.incrAtom==property))
                        {
                            //this is the total size of our selection
                            chunk->totalSize=remoteVars->selstruct.incrementalSize;
                            //we are doing incremental reading
                            if(remoteVars->selstruct.incrementalSizeRead == 0)
                            {
                                //it's the first chunk
                                chunk->firstChunk=TRUE;
                            }
                            remoteVars->selstruct.incrementalSizeRead+=xcb_get_property_value_length(reply);
                            if(!bytes_left && ! bytes_read && !xcb_get_property_value_length(reply))
                            {
                                //we got the property with 0 size it means that we recieved all data of incr property
//                                 EPHYR_DBG("INCR Property done, read %d", remoteVars->selstruct.incrementalSizeRead);
                                remoteVars->selstruct.incrAtom=0;
                                remoteVars->selstruct.incrementalSize=0;
                                //it's the last chunk
                                chunk->lastChunk=TRUE;
                            }
                        }
                        else
                        {
                            //we are doing simple read
                            if(bytes_read==0)
                            {
                                //it's the first chunk
                                chunk->firstChunk=TRUE;
                            }
                            if(bytes_left==0)
                            {
                                //the last chunk
                                chunk->lastChunk=TRUE;
                            }
                            //total size of the selection
                            chunk->totalSize=xcb_get_property_value_length(reply)+bytes_left;
                        }

                        bytes_read+=xcb_get_property_value_length(reply);
//                         EPHYR_DBG( "read chunk of selection - size %d, total read %d,  left %d, first:%d, last:%d", xcb_get_property_value_length(reply), bytes_read, bytes_left, chunk->firstChunk, chunk->lastChunk);

                        pthread_mutex_lock(&remoteVars->sendqueue_mutex);
                        //attach chunk to the end of output chunk queue
                        if(!remoteVars->selstruct.lastOutputChunk)
                        {
                            remoteVars->selstruct.lastOutputChunk=remoteVars->selstruct.firstOutputChunk=chunk;
                        }
                        else
                        {
                            remoteVars->selstruct.lastOutputChunk->next=(struct outputChunk*)chunk;
                            remoteVars->selstruct.lastOutputChunk=chunk;
                        }
//                         EPHYR_DBG(" ADD CHUNK %p %p %p", remoteVars->selstruct.firstOutputChunk, remoteVars->selstruct.lastOutputChunk, chunk);
                        pthread_cond_signal(&remoteVars->have_sendqueue_cond);
                        pthread_mutex_unlock(&remoteVars->sendqueue_mutex);

                        if(bytes_left)
                        {
                            free(reply);
                            cookie= xcb_get_property(remoteVars->selstruct.xcbConnection, 0, remoteVars->selstruct.clipWinId, property, XCB_GET_PROPERTY_TYPE_ANY, bytes_read/4, max_chunk());
                            reply=xcb_get_property_reply(remoteVars->selstruct.xcbConnection, cookie, NULL);
                            if(!reply)
                            {
                                //something is wrong
                                EPHYR_DBG("NULL reply");
                                break;
                            }
                        }
                        //read in loop till no data left
                    }while(bytes_left);
                }
                else
                {
                    stype=atom_name(reply->type);
                    EPHYR_DBG("Not supported mime type: %s, %d",stype, reply->type);
                    if(stype)
                        free(stype);
                }
            }
            if(reply)
                free(reply);
            //if reading incr property this will say sel owner that we are ready for the next chunk of data
            xcb_delete_property(remoteVars->selstruct.xcbConnection, remoteVars->selstruct.clipWinId, property);
            xcb_flush(remoteVars->selstruct.xcbConnection);
        }
    }
}

void process_selection_notify(xcb_generic_event_t *e)
{
    xcb_selection_notify_event_t *sel_event;

//     EPHYR_DBG("selection notify");
    sel_event=(xcb_selection_notify_event_t *)e;

    //processing the event which is reply for convert selection call

    remoteVars->selstruct.incrAtom=0;
    remoteVars->selstruct.incrementalSize=0;

    if (sel_event->requestor != remoteVars->selstruct.clipWinId)
    {
//         EPHYR_DBG("not our window");
        return;
    }
    else
    {
//         EPHYR_DBG("selection notify sel %d, target %d, property %d", sel_event->selection, sel_event->target, sel_event->property);
        if(sel_event->property==XCB_NONE)
        {
            EPHYR_DBG( "NO SELECTION");
        }
        else
        {
            remoteVars->selstruct.currentSelection=sel_event->selection;
            //read property
            read_selection_property(remoteVars->selstruct.currentSelection, sel_event->property);
        }
    }
}

void process_property_notify(xcb_generic_event_t *e)
{
    xcb_property_notify_event_t *pn;

//     EPHYR_DBG("property notify");

    pn = (xcb_property_notify_event_t *)e;
    if (pn->window != remoteVars->selstruct.clipWinId)
    {
//         EPHYR_DBG("not our window");
        return;
    }
//     EPHYR_DBG("property %d, state %d ", pn->atom, pn->state);
    if(pn->state==XCB_PROPERTY_NEW_VALUE)
    {
//         EPHYR_DBG( "NEW VALUE");
        if(remoteVars->selstruct.incrAtom==pn->atom && remoteVars->selstruct.incrementalSize)
        {
            //we recieveing the selection data incrementally, let's read a next chunk
//             EPHYR_DBG("reading incr property %d", pn->atom);
            read_selection_property(remoteVars->selstruct.currentSelection, pn->atom);
        }
    }
    if(pn->state==XCB_PROPERTY_DELETE)
    {
//         EPHYR_DBG( "DELETE");
    }
    if(pn->state==XCB_PROPERTY_NOTIFY)
    {
//         EPHYR_DBG( "NOTIFY");
    }
}


void process_selection_owner_notify(xcb_generic_event_t *e)
{
    enum SelectionType selection;
    xcb_xfixes_selection_notify_event_t *notify_event=(xcb_xfixes_selection_notify_event_t *)e;

//     EPHYR_DBG("SEL OWNER notify, selection: %d, window: %d, owner: %d",notify_event->selection, notify_event->window, notify_event->owner);
    if(notify_event->owner == remoteVars->selstruct.clipWinId)
    {
//         EPHYR_DBG("It's our own selection, ignoring");
        return;
    }

    if(remoteVars->selstruct.selectionMode == CLIP_NONE || remoteVars->selstruct.selectionMode == CLIP_CLIENT)
    {
        EPHYR_DBG("Server selection is disabled");
        return;
    }

    //cancel all previous incr reading
    remoteVars->selstruct.incrementalSize=remoteVars->selstruct.incrementalSizeRead=0;
    remoteVars->selstruct.incrAtom=0;


    selection=selection_from_atom(notify_event->selection);

    //we are not owners of this selction anymore
    remoteVars->selstruct.inSelection[selection].owner=FALSE;

    //get supported mime types
    request_selection_data( notify_event->selection, atom( "TARGETS"), atom( "TARGETS"), 0);

}

static
void *selection_thread (void* id)
{
    xcb_screen_t        *screen;
    uint response_type;
    xcb_generic_event_t *e=NULL;
    uint32_t             mask = 0;
    uint32_t             values[2];
    xcb_generic_error_t *error = 0;
    const xcb_query_extension_reply_t *reply;
    xcb_xfixes_query_version_cookie_t xfixes_query_cookie;
    xcb_xfixes_query_version_reply_t *xfixes_query;


    /* Create the window */
    remoteVars->selstruct.xcbConnection = xcb_connect (RemoteHostVars.displayName, NULL);
    if(xcb_connection_has_error(remoteVars->selstruct.xcbConnection))
    {
        EPHYR_DBG("Warning! can't create XCB connection to display %s, selections exchange between client and server will be disabled", RemoteHostVars.displayName);
        remoteVars->selstruct.xcbConnection=0;
        pthread_exit(0);
        return NULL;
    }
    screen = xcb_setup_roots_iterator (xcb_get_setup (remoteVars->selstruct.xcbConnection)).data;
    remoteVars->selstruct.clipWinId = xcb_generate_id (remoteVars->selstruct.xcbConnection);
    mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    values[0] = screen->white_pixel;
    values[1] = XCB_EVENT_MASK_PROPERTY_CHANGE;

    ATOM_CLIPBOARD=atom("CLIPBOARD");

    //create window which will recieve selection events and provide remote selection to X-clients
    xcb_create_window (remoteVars->selstruct.xcbConnection,
                       XCB_COPY_FROM_PARENT,
                       remoteVars->selstruct.clipWinId,
                       screen->root,
                       0, 0,
                       1, 1,
                       0,
                       XCB_WINDOW_CLASS_INPUT_OUTPUT,
                       screen->root_visual,
                       mask, values);

    xcb_flush(remoteVars->selstruct.xcbConnection);
    //check if we have xfixes, we need it to recieve selection owner events
    reply = xcb_get_extension_data(remoteVars->selstruct.xcbConnection, &xcb_xfixes_id);
    if (reply && reply->present)
    {
        xfixes_query_cookie = xcb_xfixes_query_version(remoteVars->selstruct.xcbConnection, XCB_XFIXES_MAJOR_VERSION, XCB_XFIXES_MINOR_VERSION);
        xfixes_query = xcb_xfixes_query_version_reply (remoteVars->selstruct.xcbConnection, xfixes_query_cookie, &error);
        if (!xfixes_query || error || xfixes_query->major_version < 2)
        {
            free(error);
        }
        else
        {
            //we'll recieve sel owner events for primary amd clipboard
            mask =  XCB_XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER;
            xcb_xfixes_select_selection_input_checked(remoteVars->selstruct.xcbConnection,remoteVars->selstruct.clipWinId, XCB_ATOM_PRIMARY, mask);
            xcb_xfixes_select_selection_input_checked(remoteVars->selstruct.xcbConnection, remoteVars->selstruct.clipWinId, ATOM_CLIPBOARD, mask);
        }
        free(xfixes_query);
    }
    xcb_flush(remoteVars->selstruct.xcbConnection);

    // event loop
    while ((e = xcb_wait_for_event(remoteVars->selstruct.xcbConnection)))
    {
        response_type = e->response_type & ~0x80;

        //we notified that selection is changed in primary or clipboard
        if (response_type == reply->first_event + XCB_XFIXES_SELECTION_NOTIFY)
        {
            process_selection_owner_notify(e);
        }
        else
        {
            //we notified that property is changed
            if (response_type == XCB_PROPERTY_NOTIFY)
            {
                process_property_notify(e);
            }
            //we got reply to our selection request (mime types or data)
            else if (response_type == XCB_SELECTION_NOTIFY)
            {
                process_selection_notify(e);
            }
            else if (response_type == XCB_SELECTION_REQUEST)
            {
                process_selection_request(e);
            }
            else
            {
//                 EPHYR_DBG("not processing this event %d ", response_type);
            }
        }
        free(e);
        xcb_flush(remoteVars->selstruct.xcbConnection);
    }

    pthread_exit(0);
    return NULL;
}


void install_selection_callbacks(void)
{
    int ret;
    if(remoteVars->selstruct.threadStarted)
        return;
    remoteVars->selstruct.threadStarted=TRUE;

    ret = pthread_create(&(remoteVars->selstruct.selThreadId), NULL, selection_thread, (void *)remoteVars->selstruct.selThreadId);
    if (ret)
    {
        EPHYR_DBG("ERROR; return code from pthread_create() is %d", ret);
        remoteVars->selstruct.selThreadId=0;
    }
    else
        pthread_mutex_init(&remoteVars->selstruct.inMutex, NULL);
    return;
}

void process_selection_request(xcb_generic_event_t *e)
{
    xcb_selection_request_event_t *req=(xcb_selection_request_event_t*)e;
    char *asel, *atar, *aprop;
    enum SelectionType sel=selection_from_atom(req->selection);
    xcb_atom_t property=req->property;
    xcb_atom_t target=req->target;

    xcb_selection_notify_event_t* event= (xcb_selection_notify_event_t*)calloc(32, 1);
    event->response_type = XCB_SELECTION_NOTIFY;
    event->requestor = req->requestor;
    event->selection = req->selection;
    event->target    = req->target;
    event->property  = XCB_NONE;
    event->time      = req->time;


    if(property == XCB_NONE)
        property=target;


/*
    asel= atom_name(req->selection);
    atar= atom_name(req->target);
    aprop=atom_name(req->property);

    EPHYR_DBG("selection request for %s %s %s ", asel, atar, aprop);
    if(asel)
        free(asel);
    if(aprop)
        free(aprop);
    if(atar)
        free(atar);
*/

    //synchronize with main thread
    pthread_mutex_lock(&remoteVars->selstruct.inMutex);
    if(! remoteVars->selstruct.inSelection[sel].owner)
    {
        pthread_mutex_unlock(&remoteVars->selstruct.inMutex);
        //we don't own this selection
        EPHYR_DBG("not our selection");
        xcb_send_event(remoteVars->selstruct.xcbConnection, FALSE, req->requestor, XCB_EVENT_MASK_NO_EVENT, (char*)event);
        xcb_flush(remoteVars->selstruct.xcbConnection);
        free(event);
        return;
    }

    if(remoteVars->selstruct.inSelection[sel].timestamp > req->time)
    {
        pthread_mutex_unlock(&remoteVars->selstruct.inMutex);
        //selection changed after request
        EPHYR_DBG("requested selection doesn't exist anymore");
        xcb_send_event(remoteVars->selstruct.xcbConnection, FALSE, req->requestor, XCB_EVENT_MASK_NO_EVENT, (char*)event);
        xcb_flush(remoteVars->selstruct.xcbConnection);
        free(event);
        return;
    }
    if(req->target==atom("TIMESTAMP"))
    {
        event->property=property;
//         EPHYR_DBG("requested TIMESTAMP");
        xcb_change_property(remoteVars->selstruct.xcbConnection, XCB_PROP_MODE_REPLACE, req->requestor,
                            property, XCB_ATOM_INTEGER, 32, 1, &remoteVars->selstruct.inSelection[sel].timestamp);

    }
    else if(req->target==atom("TARGETS"))
    {
        event->property=property;
//         EPHYR_DBG("requested TARGETS");
        send_mime_types(req);
    }
    else
    {
        event->property=send_data(req);
    }
    pthread_mutex_unlock(&remoteVars->selstruct.inMutex);
    xcb_send_event(remoteVars->selstruct.xcbConnection, FALSE, req->requestor, XCB_EVENT_MASK_NO_EVENT, (char*)event);
    xcb_flush(remoteVars->selstruct.xcbConnection);
    free(event);
}

enum SelectionType selection_from_atom(xcb_atom_t selection)
{
    if(selection == XCB_ATOM_PRIMARY)
        return PRIMARY;
    return CLIPBOARD;

}

void send_mime_types(xcb_selection_request_event_t* req)
{
    //inmutex is locked in the caller function
    //send supported targets
    enum SelectionType sel=selection_from_atom(req->selection);

    //we'll have max 7 mimetypes, don't forget to change this dimension if adding new mimetypes
    xcb_atom_t targets[7];
    xcb_atom_t a;
    uint32_t mcount=0;

    if((a=atom("TARGETS")))
        targets[mcount++]=a;
    if((a=atom("TIMESTAMP")))
        targets[mcount++]=a;

    if(remoteVars->selstruct.inSelection[sel].mimeData==PIXMAP)
    {
        //return PNG mime, if our data in PNG format, otherwise return JPEG
        if((a=atom("image/png")) && is_png(remoteVars->selstruct.inSelection[sel].data, remoteVars->selstruct.inSelection[sel].size))
        {
            //our buffer is PNG file
            targets[mcount++]=a;
        }
        else
        {
            if((a=atom("image/jpg")))
                targets[mcount++]=a;
            if((a=atom("image/jpeg")))
                targets[mcount++]=a;
        }
    }
    else
    {
        if((a=atom("UTF8_STRING")))
            targets[mcount++]=a;
        if((a=atom("text/plain;charset=utf-8")))
            targets[mcount++]=a;
        if((a=atom("STRING")))
            targets[mcount++]=a;
        if((a=atom("TEXT")))
            targets[mcount++]=a;
        if((a=atom("text/plain")))
            targets[mcount++]=a;
    }

    xcb_change_property(remoteVars->selstruct.xcbConnection, XCB_PROP_MODE_REPLACE, req->requestor, req->property, XCB_ATOM_ATOM,
                        32, mcount, (const void *)targets);
    xcb_flush(remoteVars->selstruct.xcbConnection);
}

xcb_atom_t send_data(xcb_selection_request_event_t* req)
{
    //inmutex is locked in the caller function
    //send data
    enum SelectionType sel=selection_from_atom(req->selection);
    char *starget;

    xcb_atom_t png=atom("image/png");
    xcb_atom_t jpg=atom("image/jpg");
    xcb_atom_t jpeg=atom("image/jpeg");

    if(remoteVars->selstruct.inSelection[sel].mimeData==UTF_STRING)
    {
        //if it's one of supported text formats send without convertion
        if(is_string_atom(req->target))
        {
//             EPHYR_DBG("sending UTF text");
            return set_data_property(req, remoteVars->selstruct.inSelection[sel].data, remoteVars->selstruct.inSelection[sel].size);
        }
        else
        {
            starget=atom_name(req->target);
            EPHYR_DBG("unsupported property requested: %s",starget);
            if(starget)
                free(starget);
            return XCB_NONE;
        }
    }
    else
    {
        if(!is_image_atom(req->target))
        {
            starget=atom_name(req->target);
            EPHYR_DBG("unsupported property requested: %s",starget);
            if(starget)
                free(starget);
            return XCB_NONE;
        }
/*
        starget=atom_name(req->target);
        EPHYR_DBG("requested %s",starget);
        if(starget)
            free(starget);
*/

        //TODO: implement convertion between different image formats
        if(is_png(remoteVars->selstruct.inSelection[sel].data, remoteVars->selstruct.inSelection[sel].size))
        {
            if(req->target!=png)
            {
                starget=atom_name(req->target);
                EPHYR_DBG("unsupported property requested: %s",starget);
                if(starget)
                    free(starget);
                return XCB_NONE;
            }
        }
        else
        {
            if((req->target!=jpg)&&(req->target!=jpeg))
            {
                starget=atom_name(req->target);
                EPHYR_DBG("unsupported property requested: %s",starget);
                if(starget)
                    free(starget);
                return XCB_NONE;
            }
        }
        return set_data_property(req, remoteVars->selstruct.inSelection[sel].data, remoteVars->selstruct.inSelection[sel].size);
    }
    return XCB_NONE;
}

xcb_atom_t set_data_property(xcb_selection_request_event_t* req, unsigned char* data, uint32_t size)
{
    //inmutex locked in parent thread
    //set data to window property

    //change when implemented
    BOOL support_incr=FALSE;

    xcb_atom_t xtsel=atom("_XT_SELECTION_0");
    xcb_atom_t qtsel=atom("_QT_SELECTION");
    //this types of application not supporting incr selection
    if(req->property == xtsel|| req->property == qtsel )
    {
        EPHYR_DBG("property %d doesn't support INCR",req->property);
        support_incr=FALSE;
    }

    //check if we are sending incr
    if(size < xcb_get_maximum_request_length(remoteVars->selstruct.xcbConnection) * 4 - 24)
    {
//         EPHYR_DBG( "sending %d bytes, property %d, target %d", size, req->property, req->target);

        xcb_change_property(remoteVars->selstruct.xcbConnection, XCB_PROP_MODE_REPLACE, req->requestor, req->property, req->target,
                            8, size, (const void *)data);

        xcb_flush(remoteVars->selstruct.xcbConnection);
        return req->property;
    }

    EPHYR_DBG("data is too big");
    return XCB_NONE;
}

BOOL is_png(unsigned char* data, uint32_t size)
{
    if( size<8)
        return FALSE;
    return !png_sig_cmp(data, 0, 8);
}
