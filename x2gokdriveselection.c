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
#include <zlib.h>

#ifdef HAVE_CONFIG_H
#include <dix-config.h>

#if XORG_VERSION_CURRENT < 11999901
#include <kdrive-config.h>
#endif // XORG_VERSION_CURRENT

#endif
#include "x2gokdriveselection.h"

#define SELECTION_DELAY 30000 //timeout for selection operation
#define INCR_SIZE 256*1024 //size of part for incr selection incr selection

static struct _remoteHostVars *remoteVars = NULL;

//internal atoms
static xcb_atom_t ATOM_ATOM;
static xcb_atom_t ATOM_CLIPBOARD;
static xcb_atom_t ATOM_TARGETS;
static xcb_atom_t ATOM_TIMESTAMP;
static xcb_atom_t ATOM_INCR;
static xcb_atom_t ATOM_XT_SELECTION;
static xcb_atom_t ATOM_QT_SELECTION;


//text atoms
static xcb_atom_t ATOM_UTF8_STRING;
static xcb_atom_t ATOM_TEXT_PLAIN_UTF;
static xcb_atom_t ATOM_STRING;
static xcb_atom_t ATOM_TEXT;
static xcb_atom_t ATOM_TEXT_PLAIN;

//image atoms
static xcb_atom_t ATOM_IMAGE_PNG;
static xcb_atom_t ATOM_IMAGE_XPM;
static xcb_atom_t ATOM_IMAGE_JPG;
static xcb_atom_t ATOM_IMAGE_JPEG;
static xcb_atom_t ATOM_PIXMAP;
static xcb_atom_t ATOM_IMAGE_BMP;


uint32_t max_chunk(void)
{
    if(remoteVars->selstruct.clientSupportsExetndedSelection)
        return 1024*100/4; //100KB
    else
        return 10*1024*1024/4; //10MB
}

void init_atoms(void)
{
    //init intern atoms

    ATOM_ATOM=string_to_atom("ATOM");
    ATOM_CLIPBOARD=string_to_atom("CLIPBOARD");
    ATOM_TARGETS=string_to_atom("TARGETS");
    ATOM_TIMESTAMP=string_to_atom("TIMESTAMP");
    ATOM_INCR=string_to_atom("INCR");

    ATOM_XT_SELECTION=string_to_atom("_XT_SELECTION_0");
    ATOM_QT_SELECTION=string_to_atom("_QT_SELECTION");


    ATOM_UTF8_STRING=string_to_atom("UTF8_STRING");
    ATOM_TEXT_PLAIN_UTF=string_to_atom("text/plain;charset=utf-8");
    ATOM_STRING=string_to_atom("STRING");
    ATOM_TEXT=string_to_atom("TEXT");
    ATOM_TEXT_PLAIN=string_to_atom("text/plain");

    ATOM_IMAGE_PNG=string_to_atom("image/png");
    ATOM_IMAGE_XPM=string_to_atom("image/xpm");
    ATOM_IMAGE_JPG=string_to_atom("image/jpg");
    ATOM_IMAGE_JPEG=string_to_atom("image/jpeg");
    ATOM_PIXMAP=string_to_atom("PIXMAP");
    ATOM_IMAGE_BMP=string_to_atom("image/bmp");
}

struct DelayedRequest* discard_delayed_request(struct DelayedRequest* d, struct DelayedRequest* prev)
{
    //this function finalizing delayed request and destroys XCB request and event
    //removing the request from list and returning the pointer of the next element for iteration
    struct DelayedRequest* next=d->next;
    xcb_send_event(remoteVars->selstruct.xcbConnection, FALSE, d->request->requestor, XCB_EVENT_MASK_NO_EVENT, (char*)d->event);
    xcb_flush(remoteVars->selstruct.xcbConnection);
    free(d->event);
    free((xcb_generic_event_t *)(d->request));

    //remove element from list
    if(prev)
        prev->next=next;

    if(d==remoteVars->selstruct.firstDelayedRequest)
        remoteVars->selstruct.firstDelayedRequest=next;

    if(d==remoteVars->selstruct.lastDelayedRequest)
        remoteVars->selstruct.lastDelayedRequest=prev;

    free(d);

    return next;
}

void destroy_incr_transaction(struct IncrTransaction* tr, struct IncrTransaction* prev)
{
    //destroy incr transaction
    struct IncrTransaction* next=tr->next;

    const uint32_t mask[] = { XCB_EVENT_MASK_NO_EVENT };
    //don't resive property notify events for this window anymore
    xcb_change_window_attributes(remoteVars->selstruct.xcbConnection, tr->requestor,
                                 XCB_CW_EVENT_MASK, mask);
    xcb_flush(remoteVars->selstruct.xcbConnection);

    //free data
    free(tr->data);

    //remove element from list
    if(prev)
        prev->next=next;

    if(tr==remoteVars->selstruct.firstIncrTransaction)
        remoteVars->selstruct.firstIncrTransaction=next;

    if(tr==remoteVars->selstruct.lastIncrTransaction)
        remoteVars->selstruct.lastIncrTransaction=prev;

    free(tr);
}



BOOL check_req_sanity(xcb_selection_request_event_t* req)
{
    //check if the requested mime can be delivered
    //inmutex should be locked from calling function

    enum SelectionType sel=selection_from_atom(req->selection);
    if(remoteVars->selstruct.inSelection[sel].mimeData==UTF_STRING)
    {
        //if it's one of supported text formats send without convertion
        if(is_string_atom(req->target))
        {
            return TRUE;
        }
        else
        {
            EPHYR_DBG("Unsupported property requested %d", req->target);
            return FALSE;
        }
    }
    else
    {
        if(!is_image_atom(req->target))
        {
            EPHYR_DBG("Unsupported property requested %d", req->target);
            return FALSE;
        }
        return TRUE;
    }
}


void remove_obsolete_incr_transactions( BOOL checkTs)
{
    //remove_obsolete_incr_transactions
    //if checkTS true, check timestamp and destroy only if ts exceed delay

    struct DelayedRequest* prev=NULL;
    struct DelayedRequest* d=remoteVars->selstruct.firstDelayedRequest;
    while(d)
    {
        if(!checkTs || (currentTime.milliseconds > (d->request->time + SELECTION_DELAY)))
        {
            d=discard_delayed_request(d, prev);
        }
        else
        {
            prev=d;
            d=d->next;
        }
    }
}


void process_delayed_requests(void)
{
    //process delayed requests

    enum SelectionType selection;

    struct DelayedRequest* prev=NULL;
    struct DelayedRequest* d=remoteVars->selstruct.firstDelayedRequest;
    while(d)
    {
        selection = selection_from_atom( d->request->selection);
        if(currentTime.milliseconds > (d->request->time + SELECTION_DELAY))
        {
            EPHYR_DBG("timeout selection: %d",selection);
            d=discard_delayed_request(d, prev);
            continue;
        }
        pthread_mutex_lock(&remoteVars->selstruct.inMutex);
        if(!remoteVars->selstruct.inSelection[selection].owner)
        {

            pthread_mutex_unlock(&remoteVars->selstruct.inMutex);
            EPHYR_DBG("we are not owner of requested selection %d",selection);
            //we are not anymore owners of this selection
            d=discard_delayed_request(d, prev);
            continue;
        }
        if(remoteVars->selstruct.inSelection[selection].timestamp > d->request->time )
        {

            pthread_mutex_unlock(&remoteVars->selstruct.inMutex);
            EPHYR_DBG("selection request for %d is too old", selection);
            //requested selection is older than the current one
            d=discard_delayed_request(d, prev);
            continue;
        }
        if(!check_req_sanity(d->request))
        {
            pthread_mutex_unlock(&remoteVars->selstruct.inMutex);
//             EPHYR_DBG("can't convert selection %d to requested myme type %d",selection, d->request->property);
            //our selection don't support requested mime type
            d=discard_delayed_request(d, prev);
            continue;
        }
        if(remoteVars->selstruct.inSelection[selection].state != COMPLETED)
        {

            pthread_mutex_unlock(&remoteVars->selstruct.inMutex);
            //we don't have the data yet
            prev=d;
            d=d->next;
            continue;
        }
        //data is ready, send data to requester and discard the request
        //inMutex need be locked for sending data
        d->event->property=send_data(d->request);

        pthread_mutex_unlock(&remoteVars->selstruct.inMutex);
        d=discard_delayed_request(d, prev);
    }
}

void process_incr_transaction_property(xcb_property_notify_event_t * pn)
{
    //process incr transactions

    struct IncrTransaction* prev=NULL;
    struct IncrTransaction* tr=remoteVars->selstruct.firstIncrTransaction;
    uint32_t left, sendingBytes;
    while(tr)
    {
        if((tr->requestor == pn->window) && (tr->property == pn->atom ) && ( pn->state == XCB_PROPERTY_DELETE) )
        {
            //requestor ready for the new portion of data
            left=tr->size-tr->sentBytes;
            if(!left)
            {
//                 EPHYR_DBG("all INCR data sent to %d",tr->requestor);
                //all data sent, sending NULL data and destroying transaction
                xcb_change_property(remoteVars->selstruct.xcbConnection, XCB_PROP_MODE_REPLACE, tr->requestor, tr->property,
                                    tr->target, 8, 0, NULL);
                xcb_flush(remoteVars->selstruct.xcbConnection);
                destroy_incr_transaction(tr, prev);
                return;
            }
            sendingBytes=(INCR_SIZE< left)?INCR_SIZE:left;

            xcb_change_property(remoteVars->selstruct.xcbConnection, XCB_PROP_MODE_REPLACE, tr->requestor, tr->property,
                                tr->target, 8, sendingBytes, tr->data + tr->sentBytes);
            xcb_flush(remoteVars->selstruct.xcbConnection);
            tr->sentBytes+=sendingBytes;
            tr->timestamp=currentTime.milliseconds;
            return;
        }
        prev=tr;
        tr=tr->next;
    }
    //notify event doesn't belong to any of started incr transactions or it's notification for new property
    return;
}



int own_selection(enum SelectionType selection)
{
    //owning selection
    //remoteVars.selstruct.inMutex locked in the calling function
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
    remoteVars->selstruct.inSelection[selection].timestamp=currentTime.milliseconds;

//     EPHYR_DBG("own selection %d", currentTime.milliseconds);
    return 0;
}

void selection_init(struct _remoteHostVars *obj)
{
    remoteVars=obj;
    return;
}


xcb_atom_t string_to_atom(const char* name)
{
    //get atom for the name, return 0 if not found
    xcb_intern_atom_cookie_t cookie;
    xcb_intern_atom_reply_t *reply;
    xcb_atom_t a=0;
    cookie = xcb_intern_atom(remoteVars->selstruct.xcbConnection, 0, strlen(name), name);
    if ((reply = xcb_intern_atom_reply(remoteVars->selstruct.xcbConnection, cookie, NULL)))
    {
        a=reply->atom;
        free(reply);
    }
    EPHYR_DBG("The %s atom has ID %u", name, a);
    return a;
}

char *atom_to_string(xcb_atom_t xatom)
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

static xcb_atom_t target_has_atom(xcb_atom_t* list, size_t size, xcb_atom_t a)
{
    //check if the atom which represents "name" is in the list of supported mime types
    size_t i = 0;

    for (i = 0;i < size;i++)
    {
        if (list[i]==a)
            return a;
    }
    return 0;
}

int is_string_atom( xcb_atom_t at)
{
    //check if selection data is string/text
    if((at==ATOM_UTF8_STRING) || (at==ATOM_TEXT_PLAIN_UTF) ||
        (at==ATOM_STRING) || (at==ATOM_TEXT) || (at==ATOM_TEXT_PLAIN))
            return 1;
    return 0;
}

int is_image_atom( xcb_atom_t at)
{
    //check if selection data is image
    if( at == ATOM_IMAGE_PNG ||
        at == ATOM_IMAGE_XPM ||
        at == ATOM_IMAGE_JPEG ||
        at == ATOM_IMAGE_JPG ||
        at == ATOM_PIXMAP ||
        at == ATOM_IMAGE_BMP)
        return 1;
    return 0;
}

static xcb_atom_t best_atom_from_target(xcb_atom_t* list, size_t size)
{
    //here we chose the best of supported formats for selection
    xcb_atom_t a;
    //selecting utf formats first
    if((a=target_has_atom(list, size, ATOM_UTF8_STRING)))
    {
//         EPHYR_DBG("selecting mime type UTF8_STRING");
        return a;
    }
    if((a=target_has_atom(list, size, ATOM_TEXT_PLAIN_UTF)))
    {
//         EPHYR_DBG("selecting mime type text/plain;charset=utf-8");
        return a;
    }
    if((a=target_has_atom(list, size, ATOM_STRING)))
    {
//         EPHYR_DBG( "selecting mime type STRING");
        return a;
    }
    if((a=target_has_atom(list, size, ATOM_TEXT)))
    {
//         EPHYR_DBG( "selecting mime type TEXT");
        return a;
    }
    if((a=target_has_atom(list, size, ATOM_TEXT_PLAIN)))
    {
//         EPHYR_DBG( "selecting mime type text/plain");
        return a;
    }

    //selecting loseless formats first
    if((a=target_has_atom(list, size, ATOM_IMAGE_PNG)))
    {
//         EPHYR_DBG( "selecting mime type image/png");
        return a;
    }
    if((a=target_has_atom(list, size, ATOM_IMAGE_XPM)))
    {
//         EPHYR_DBG( "selecting mime type image/xpm");
        return a;
    }
    if((a=target_has_atom(list, size, ATOM_PIXMAP)))
    {
//         EPHYR_DBG( "selecting mime type PIXMAP");
        return a;
    }
    if((a=target_has_atom(list, size, ATOM_IMAGE_BMP)))
    {
//         EPHYR_DBG( "selecting mime type image/bmp");
        return a;
    }
    if((a=target_has_atom(list, size, ATOM_IMAGE_JPG)))
    {
//         EPHYR_DBG( "selecting mime type image/jpg");
        return a;
    }
    if((a=target_has_atom(list, size, ATOM_IMAGE_JPEG)))
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
    xcb_atom_t data_atom;
    unsigned int bytes_left, bytes_read=0;
    xcb_get_property_cookie_t cookie;
    xcb_get_property_reply_t *reply;
    struct OutputChunk* chunk;
    unsigned char* compressed_data;
    uint32_t compressed_size;


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
            if(reply->type == ATOM_INCR)
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
            if(reply->type == ATOM_ATOM)
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
                    {
                        if(remoteVars->client_os == OS_WINDOWS && selection_from_atom(selection) == PRIMARY)
                        {
//                             EPHYR_DBG("client doesn't support PRIMARY selection");
                        }
                        else
                        {
                            if(remoteVars->selstruct.clientSupportsOnDemandSelection)
                            {
                                //don't ask for data yet, only send notification that we have a selection to client
//                                 EPHYR_DBG("client supports onDemand selection, notify client");
                                send_notify_to_client(selection, data_atom);
                                //save the data atom for possible data demand
                                remoteVars->selstruct.best_atom[selection_from_atom(selection)]=data_atom;
                            }
                            else
                            {
                                //request selection data
//                                 EPHYR_DBG("client not supports onDemand selection, request data");
                                request_selection_data( selection, data_atom, data_atom, 0);
                            }
                        }
                    }
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


                        chunk=malloc(sizeof(struct OutputChunk));

                        memset((void*)chunk,0,sizeof(struct OutputChunk));

                        if(xcb_get_property_value_length(reply))
                        {
                            chunk->size=xcb_get_property_value_length(reply);
                            chunk->data=(unsigned char*)malloc(chunk->size);
                            memcpy(chunk->data, xcb_get_property_value(reply),chunk->size);
                        }

                        chunk->compressed_size=0;
                        if(is_string_atom(property))
                        {
                            chunk->mimeData=UTF_STRING;
                            //for text chunks > 1K using zlib compression if client supports it
                            if(remoteVars->selstruct.clientSupportsExetndedSelection && chunk->size > 1024)
                            {
                                compressed_data=zcompress(chunk->data, chunk->size, &compressed_size);
                                if(compressed_data && compressed_size)
                                {
                                    free(chunk->data);
                                    chunk->data=compressed_data;
                                    chunk->compressed_size=compressed_size;
//                                     EPHYR_DBG("compressed chunk from %d to %d", chunk->size, chunk->compressed_size);
                                }
                            }
                        }
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
//                          EPHYR_DBG( "read chunk of selection - size %d, total read %d,  left %d, first:%d, last:%d", xcb_get_property_value_length(reply), bytes_read, bytes_left, chunk->firstChunk, chunk->lastChunk);


                        pthread_mutex_lock(&remoteVars->sendqueue_mutex);
                        //attach chunk to the end of output chunk queue
                        if(!remoteVars->selstruct.lastOutputChunk)
                        {
                            remoteVars->selstruct.lastOutputChunk=remoteVars->selstruct.firstOutputChunk=chunk;
                        }
                        else
                        {
                            remoteVars->selstruct.lastOutputChunk->next=chunk;
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
                    EPHYR_DBG("Not supported mime type:%d",reply->type);
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

void send_notify_to_client(xcb_atom_t selection, xcb_atom_t mime)
{
    //creating the selection chunk with no data, which notifyes client that we have a selection
    struct OutputChunk* chunk= malloc(sizeof(struct OutputChunk));
//     EPHYR_DBG("send selection notify to client");


    memset((void*)chunk,0,sizeof(struct OutputChunk));

    if(is_string_atom(mime))
        chunk->mimeData=UTF_STRING;
    else
        chunk->mimeData=PIXMAP;

    chunk->selection=selection_from_atom(selection);
    chunk->totalSize=0;
    chunk->firstChunk=chunk->lastChunk=TRUE;

    pthread_mutex_lock(&remoteVars->sendqueue_mutex);

    //attach chunk to the end of output chunk queue
    if(!remoteVars->selstruct.lastOutputChunk)
    {
        remoteVars->selstruct.lastOutputChunk=remoteVars->selstruct.firstOutputChunk=chunk;
    }
    else
    {
        remoteVars->selstruct.lastOutputChunk->next=chunk;
        remoteVars->selstruct.lastOutputChunk=chunk;
    }
    pthread_cond_signal(&remoteVars->have_sendqueue_cond);


    pthread_mutex_unlock(&remoteVars->sendqueue_mutex);
}

void process_selection_notify(xcb_generic_event_t *e)
{
    xcb_selection_notify_event_t *sel_event;

    enum SelectionType selection;

//     EPHYR_DBG("selection notify");
    sel_event=(xcb_selection_notify_event_t *)e;
    selection=selection_from_atom(sel_event->selection);

    if(sel_event->property== XCB_NONE && sel_event->target==XCB_NONE)
    {
        //have data demand from client
        request_selection_data( sel_event->selection, remoteVars->selstruct.best_atom[selection],remoteVars->selstruct.best_atom[selection], 0);
        return;
    }

    if(sel_event->property== sel_event->selection && sel_event->target==sel_event->selection)
    {
        //have data ready from server. We don't need to do anything here. This event interrrupted the waiting procedure and the delayed requests are already processed
//         EPHYR_DBG("Have DATA READY event");
        return;
    }

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
//             EPHYR_DBG( "NO SELECTION");
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
        //this property doesn't belong to our window;
        //let's check if it's not the property corresponding to one of incr transactions
        process_incr_transaction_property(pn);
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

    pthread_mutex_lock(&remoteVars->selstruct.inMutex);
    remoteVars->selstruct.inSelection[selection].owner=FALSE;


    pthread_mutex_unlock(&remoteVars->selstruct.inMutex);

    //get supported mime types
    request_selection_data( notify_event->selection, ATOM_TARGETS, ATOM_TARGETS, 0);

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

    init_atoms();

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
        process_delayed_requests();
        remove_obsolete_incr_transactions(TRUE);
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
                if(!process_selection_request(e))
                {
                    //we delayed this request, not deleteing the event yet
                    continue;
                }
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

void client_sel_request_notify(enum SelectionType sel)
{
    //this function will be used from main thread to send the event which will
    // notify selection thread that client want us to send data for selection sel

    xcb_selection_notify_event_t* event= (xcb_selection_notify_event_t*)calloc(32, 1);
    event->response_type = XCB_SELECTION_NOTIFY;
    event->requestor = remoteVars->selstruct.clipWinId;
    event->selection = atom_from_selection(sel);
    event->target    = XCB_NONE;
    event->property  = XCB_NONE;
    event->time      = XCB_TIME_CURRENT_TIME;

    xcb_send_event(remoteVars->selstruct.xcbConnection, FALSE, remoteVars->selstruct.clipWinId, XCB_EVENT_MASK_NO_EVENT, (char*)event);
    xcb_flush(remoteVars->selstruct.xcbConnection);
    free(event);
}

void client_sel_data_notify(enum SelectionType sel)
{
    //this function will be used from main thread to send the event which will
    // notify selection thread that client sent us data for selection sel

    xcb_selection_notify_event_t* event= (xcb_selection_notify_event_t*)calloc(32, 1);
    event->response_type = XCB_SELECTION_NOTIFY;
    event->requestor = remoteVars->selstruct.clipWinId;
    event->selection = atom_from_selection(sel);
    event->target    = atom_from_selection(sel);
    event->property  = atom_from_selection(sel);
    event->time      = XCB_TIME_CURRENT_TIME;

    xcb_send_event(remoteVars->selstruct.xcbConnection, FALSE, remoteVars->selstruct.clipWinId, XCB_EVENT_MASK_NO_EVENT, (char*)event);
    xcb_flush(remoteVars->selstruct.xcbConnection);
    free(event);
}


BOOL process_selection_request(xcb_generic_event_t *e)
{
    //processing selection request.
    //return true if the processing is finishing after return
    //false if data is not ready and we are delaying processing of this request
    //in this case calling function SHOULD NOT destroy the request neither event should not be destroyed
    //we'll free this objects after processing of the request when the data is available

    xcb_selection_request_event_t *req=(xcb_selection_request_event_t*)e;

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

    //synchronize with main thread

    pthread_mutex_lock(&remoteVars->selstruct.inMutex);
    if(! remoteVars->selstruct.inSelection[sel].owner)
    {


        pthread_mutex_unlock(&remoteVars->selstruct.inMutex);
        //we don't own this selection
//         EPHYR_DBG("not our selection");
        xcb_send_event(remoteVars->selstruct.xcbConnection, FALSE, req->requestor, XCB_EVENT_MASK_NO_EVENT, (char*)event);
        xcb_flush(remoteVars->selstruct.xcbConnection);
        free(event);
        return TRUE;
    }

    if(remoteVars->selstruct.inSelection[sel].timestamp > req->time)
    {


        pthread_mutex_unlock(&remoteVars->selstruct.inMutex);
        //selection changed after request
//         EPHYR_DBG("requested selection doesn't exist anymore");
        xcb_send_event(remoteVars->selstruct.xcbConnection, FALSE, req->requestor, XCB_EVENT_MASK_NO_EVENT, (char*)event);
        xcb_flush(remoteVars->selstruct.xcbConnection);
        free(event);
        return TRUE;
    }
    if(req->target==ATOM_TIMESTAMP)
    {
        event->property=property;
//         EPHYR_DBG("requested TIMESTAMP");
        xcb_change_property(remoteVars->selstruct.xcbConnection, XCB_PROP_MODE_REPLACE, req->requestor,
                            property, XCB_ATOM_INTEGER, 32, 1, &remoteVars->selstruct.inSelection[sel].timestamp);

    }
    else if(req->target==ATOM_TARGETS)
    {
        event->property=property;
//         EPHYR_DBG("requested TARGETS");
        send_mime_types(req);
    }
    else
    {
        if(remoteVars->selstruct.inSelection[sel].state==COMPLETED)
            event->property=send_data(req);
        else
        {
//             EPHYR_DBG("the data for %d is not ready yet",sel);
            delay_selection_request(req,event);


            pthread_mutex_unlock(&remoteVars->selstruct.inMutex);
            return FALSE;
        }
    }


    pthread_mutex_unlock(&remoteVars->selstruct.inMutex);
    xcb_send_event(remoteVars->selstruct.xcbConnection, FALSE, req->requestor, XCB_EVENT_MASK_NO_EVENT, (char*)event);
    xcb_flush(remoteVars->selstruct.xcbConnection);
    free(event);
    return TRUE;
}

xcb_atom_t atom_from_selection(enum SelectionType sel)
{
    if(sel==PRIMARY)
        return XCB_ATOM_PRIMARY;
    return ATOM_CLIPBOARD;
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
    uint32_t mcount=0;

    targets[mcount++]=ATOM_TARGETS;
    targets[mcount++]=ATOM_TIMESTAMP;

    if(remoteVars->selstruct.inSelection[sel].mimeData==PIXMAP)
    {

        //only supporting PNG here at the moment
        targets[mcount++]=ATOM_IMAGE_PNG;

/*
        //return PNG mime, if our data in PNG format, otherwise return JPEG
        if((a=atom("image/png")) && is_png(remoteVars->selstruct.inSelection[sel].data, remoteVars->selstruct.inSelection[sel].size))
        {
            //our buffer is PNG file
            targets[mcount++]=a;
            EPHYR_DBG("SENDING PNG ATOMS");
        }
        else
        {
            EPHYR_DBG("SENDING JPG ATOMS");
            if((a=atom("image/jpg")))
                targets[mcount++]=a;
            if((a=atom("image/jpeg")))
                targets[mcount++]=a;
        }*/
    }
    else
    {
//         EPHYR_DBG("SENDING STRING ATOMS");
        targets[mcount++]=ATOM_UTF8_STRING;
        targets[mcount++]=ATOM_TEXT_PLAIN_UTF;
        targets[mcount++]=ATOM_STRING;
        targets[mcount++]=ATOM_TEXT;
        targets[mcount++]=ATOM_TEXT_PLAIN;
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
            EPHYR_DBG("unsupported property requested: %d",req->target);
            return XCB_NONE;
        }
    }
    else
    {
        if(!is_image_atom(req->target))
        {
            EPHYR_DBG("unsupported property requested: %d",req->target);
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
            if(req->target!=ATOM_IMAGE_PNG)
            {
                EPHYR_DBG("unsupported property requested: %d",req->target);
                return XCB_NONE;
            }
        }
        else
        {
            if((req->target!=ATOM_IMAGE_JPEG)&&(req->target!=ATOM_IMAGE_JPG))
            {
                EPHYR_DBG("unsupported property requested: %d",req->target);
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
    BOOL support_incr=TRUE;

    //this types of application not supporting incr selection
    if(req->property == ATOM_XT_SELECTION|| req->property == ATOM_QT_SELECTION )
    {
        EPHYR_DBG("property %d doesn't support INCR",req->property);
        support_incr=FALSE;
    }

    //check if we are sending incr
    if(!support_incr)
    {
        if(size < xcb_get_maximum_request_length(remoteVars->selstruct.xcbConnection) * 4 - 24)
        {
            //         EPHYR_DBG( "sending %d bytes, property %d, target %d", size, req->property, req->target);

            xcb_change_property(remoteVars->selstruct.xcbConnection, XCB_PROP_MODE_REPLACE, req->requestor, req->property, req->target,
                                8, size, (const void *)data);

            xcb_flush(remoteVars->selstruct.xcbConnection);
            return req->property;
        }
        //the data is to big to sent in one property and requestor doesn't support INCR
        EPHYR_DBG("data is too big");
        return XCB_NONE;
    }
    if(size < INCR_SIZE)
    {
        //if size is < 256K send in one property
        xcb_change_property(remoteVars->selstruct.xcbConnection, XCB_PROP_MODE_REPLACE, req->requestor, req->property, req->target,
                            8, size, (const void *)data);

        xcb_flush(remoteVars->selstruct.xcbConnection);
        return req->property;
    }
    //sending INCR atom to let requester know that we are starting data incrementally
//     EPHYR_DBG("starting INCR send of size %d  for win ID %d" ,size, req->requestor);
    xcb_change_property(remoteVars->selstruct.xcbConnection, XCB_PROP_MODE_REPLACE, req->requestor, req->property,
                        ATOM_INCR, 32, 1, (const void *)&size);

    start_incr_transaction(req->requestor, req->property, req->target, data, size);


    xcb_flush(remoteVars->selstruct.xcbConnection);
    return req->property;
}


void start_incr_transaction(xcb_window_t requestor, xcb_atom_t property, xcb_atom_t target, unsigned char* data, uint32_t size)
{
    //creating INCR transaction
    //inmutex is locked from parent thread

    const uint32_t mask[] = { XCB_EVENT_MASK_PROPERTY_CHANGE };

    struct IncrTransaction* tr=malloc( sizeof(struct IncrTransaction));
    tr->requestor=requestor;
    tr->property=property;
    tr->target=target;
    tr->sentBytes=0;
    tr->timestamp=currentTime.milliseconds;
    tr->data=malloc(size);
    tr->size=size;
    tr->next=NULL;
    memcpy(tr->data, data, size);

    //add new transaction to the list
    if(!remoteVars->selstruct.firstIncrTransaction)
    {
        remoteVars->selstruct.firstIncrTransaction=remoteVars->selstruct.lastIncrTransaction=tr;
    }
    else
    {
        remoteVars->selstruct.lastIncrTransaction->next=tr;
        remoteVars->selstruct.lastIncrTransaction=tr;
    }


    //we'll recive property change events for requestor window from now
    xcb_change_window_attributes(remoteVars->selstruct.xcbConnection, requestor,
                                 XCB_CW_EVENT_MASK, mask);
}


BOOL is_png(unsigned char* data, uint32_t size)
{
    if( size<8)
        return FALSE;
    return !png_sig_cmp(data, 0, 8);
}

unsigned char* zcompress(unsigned char *inbuf, uint32_t size, uint32_t* compress_size)
{
    //compressing the data with zlib
    //return compressed data, storing the size of compressed data in compress_size
    //caller function should chek result of compression and free the output buffer


    //out buffer at least the size of input buffer
    unsigned char* out=malloc(size);

    z_stream stream;
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;

    stream.avail_in = size;
    stream.next_in = inbuf;
    stream.avail_out = size;
    stream.next_out = out;

    deflateInit(&stream, Z_BEST_COMPRESSION);
    deflate(&stream, Z_FINISH);
    deflateEnd(&stream);

    if(!stream.total_out || stream.total_out >= size)
    {
        EPHYR_DBG("zlib compression failed");
        free(out);
        *compress_size=0;
        return NULL;
    }
    *compress_size=stream.total_out;
    return out;
}

void delay_selection_request( xcb_selection_request_event_t *request, xcb_selection_notify_event_t* event)
{
    //delay the request for later processing when data will be ready
    //inmutex is locked in the caller function
    enum SelectionType sel=selection_from_atom(request->selection);
    struct DelayedRequest* dr = malloc( sizeof(struct DelayedRequest));
    dr->event=event;
    dr->request=request;
    dr->next=NULL;

    //add new request to the queue
    if(!remoteVars->selstruct.firstDelayedRequest)
    {
        remoteVars->selstruct.firstDelayedRequest=remoteVars->selstruct.lastDelayedRequest=dr;
    }
    else
    {
        remoteVars->selstruct.lastDelayedRequest->next=dr;
        remoteVars->selstruct.lastDelayedRequest=dr;
    }

    if(remoteVars->selstruct.inSelection[sel].state==NOTIFIED)
    {
        //if we didn't request the data yet, let's do it now
//         EPHYR_DBG("requesting data");

        pthread_mutex_lock(&remoteVars->sendqueue_mutex);
        remoteVars->selstruct.requestSelection[sel] = TRUE;
        pthread_cond_signal(&remoteVars->have_sendqueue_cond);


        pthread_mutex_unlock(&remoteVars->sendqueue_mutex);
        remoteVars->selstruct.inSelection[sel].state=REQUESTED;

    }
}

