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
#include "remote.h"

/*init it in os init*/
static RemoteHostVars remoteVars;

static BOOL remoteInitialized=FALSE;


void remote_selection_init()
{
    selection_init(&remoteVars);
}


void restartTimerOnInit()
{
    if(remoteInitialized)
    {
        EPHYR_DBG("check timer");
        if(remoteVars.checkConnectionTimer)
        {
            EPHYR_DBG("restarting timer");
            remoteVars.checkConnectionTimer=TimerSet(0,0,ACCEPT_TIMEOUT, checkSocketConnection, NULL);
        }
    }
}


void cancelThreadBeforeStart()
{
    shutdown(remoteVars.serversock, SHUT_RDWR);
    close(remoteVars.serversock);
    pthread_cancel(remoteVars.send_thread_id);
    remoteVars.send_thread_id=0;
    setAgentState(SUSPENDED);
}


void remote_handle_signal(int signum)
{
    EPHYR_DBG("GOT SIGNAL %d",signum);
    if(signum == SIGTERM)
    {
        terminateServer(0);
    }
    if(signum==SIGHUP)
    {
        switch(remoteVars.agentState)
        {
            case TERMINATING:
            case TERMINATED:return;

            case STARTING:
            case RESUMING:
            {
                if(remoteVars.checkConnectionTimer)
                {
                    TimerFree(remoteVars.checkConnectionTimer);
                    remoteVars.checkConnectionTimer=0;
                }
                cancelThreadBeforeStart();
                return;
            }
            case RUNNING:
            {
                disconnect_client();
                return;
            }
            case SUSPENDED:
            {
                if(remoteVars.nxagentMode)
                    readOptionsFromFile();
                setAgentState(RESUMING);
                open_socket();
                return;
            }
        }
    }
}


int queue_elements()
{
    int elems=0;
    struct sendqueue_element* current=remoteVars.first_sendqueue_element;
    while(current)
    {
        elems++;
        current=current->next;
    }
    return elems;
}

BOOL isCursorSent(uint32_t serialNumber)
{
    struct sentCursor* current=remoteVars.sentCursorsHead;
    while(current)
    {
        if(current->serialNumber == serialNumber)
            return TRUE;
        current=current->next;
    }
    return FALSE;
}

void addSentCursor(uint32_t serialNumber)
{
    #warning check memory
    struct sentCursor* curs=malloc(sizeof(struct sentCursor));
    curs->next=0;
    curs->serialNumber=serialNumber;
    if(!remoteVars.sentCursorsTail)
    {
        remoteVars.sentCursorsTail=remoteVars.sentCursorsHead=curs;
    }
    else
    {
        remoteVars.sentCursorsTail->next=curs;
        remoteVars.sentCursorsTail=curs;
    }
}

void addCursorToQueue(struct cursorFrame* cframe)
{
    if(!remoteVars.firstCursor)
    {
        remoteVars.firstCursor=remoteVars.lastCursor=cframe;
    }
    else
    {
        remoteVars.lastCursor->next=cframe;
        remoteVars.lastCursor=cframe;
    }
}

void freeCursors()
{
    struct sentCursor* cur=remoteVars.sentCursorsHead;
    while(cur)
    {
        struct sentCursor* next=cur->next;
        free(cur);
        cur=next;
    }
    struct cursorFrame* curf=remoteVars.firstCursor;
    while(curf)
    {
        struct cursorFrame* next=curf->next;
        if(curf->data)
            free(curf->data);
        free(curf);
        curf=next;
    }
    remoteVars.sentCursorsHead=remoteVars.sentCursorsTail=0;
    remoteVars.firstCursor=remoteVars.lastCursor=0;
}

void remote_removeCursor(uint32_t serialNumber)
{
    pthread_mutex_lock(&remoteVars.sendqueue_mutex);
    struct sentCursor* cur=remoteVars.sentCursorsHead;
    struct sentCursor* prev=0;
    while(cur)
    {
        if(cur->serialNumber==serialNumber)
        {
            if(prev)
                prev->next=cur->next;
            if(cur==remoteVars.sentCursorsHead)
                remoteVars.sentCursorsHead=cur->next;
            if(cur==remoteVars.sentCursorsTail)
                remoteVars.sentCursorsTail=prev;
            free(cur);
            break;
        }
        prev=cur;
        cur=cur->next;
    }
    struct deletedCursor* dcur=malloc(sizeof(struct deletedCursor));
    dcur->serialNumber=serialNumber;
    dcur->next=0;
    if(remoteVars.last_deleted_cursor)
    {
        remoteVars.last_deleted_cursor->next=dcur;
        remoteVars.last_deleted_cursor=dcur;
    }
    else
    {
        remoteVars.first_deleted_cursor=remoteVars.last_deleted_cursor=dcur;
    }
    ++remoteVars.deletedcursor_list_size;
    pthread_mutex_unlock(&remoteVars.sendqueue_mutex);
}

void remote_sendCursor(CursorPtr cursor)
{
#warning check memory
    struct cursorFrame* cframe=malloc(sizeof(struct cursorFrame));
    cframe->serialNumber=cursor->serialNumber;
    cframe->size=0;
    cframe->data=0;
    cframe->next=0;

    BOOL cursorSent=FALSE;

    pthread_mutex_lock(&remoteVars.sendqueue_mutex);
    cursorSent=isCursorSent(cursor->serialNumber);
    pthread_mutex_unlock(&remoteVars.sendqueue_mutex);
    if(!cursorSent)
    {
        if(cursor->bits->argb)
        {
            cframe->size=cursor->bits->width*cursor->bits->height*4;
            cframe->data=malloc(cframe->size);
            memcpy(cframe->data, cursor->bits->argb, cframe->size);
        }
        else
        {
            cframe->size=cursor->bits->width*cursor->bits->height*2;
            cframe->data=malloc(cframe->size);
            memcpy(cframe->data, cursor->bits->source, cframe->size/2);
            memcpy(cframe->data+cframe->size/2, cursor->bits->mask, cframe->size/2);
        }
        cframe->width=cursor->bits->width;
        cframe->height=cursor->bits->height;
        cframe->xhot=cursor->bits->xhot;
        cframe->yhot=cursor->bits->yhot;

        //in X11 implementation we have 2bits for color, not from 0 to 255 but from 0 to 65535.
        //no idea why, RGBA is still 4bytes. I think one byte per color component is suuficient, so let's just recalculate to 1byte per component

        cframe->backR=cursor->backRed*255./65535.0;
        cframe->backG=cursor->backGreen*255./65535.0;
        cframe->backB=cursor->backBlue*255./65535.0;

        cframe->forR=cursor->foreRed*255./65535.0;
        cframe->forG=cursor->foreGreen*255./65535.0;
        cframe->forB=cursor->foreBlue*255./65535.0;

        pthread_mutex_lock(&remoteVars.sendqueue_mutex);
        addSentCursor(cursor->serialNumber);
        pthread_mutex_unlock(&remoteVars.sendqueue_mutex);
    }
    pthread_mutex_lock(&remoteVars.sendqueue_mutex);
    addCursorToQueue(cframe);
    pthread_cond_signal(&remoteVars.have_sendqueue_cond);
    pthread_mutex_unlock(&remoteVars.sendqueue_mutex);
}


int32_t send_cursor(struct cursorFrame* cursor)
{

    unsigned char buffer[64]={};
    *((uint32_t*)buffer)=CURSOR; //4B

    *((uint8_t*)buffer+4)=cursor->forR;
    *((uint8_t*)buffer+5)=cursor->forG;
    *((uint8_t*)buffer+6)=cursor->forB;

    *((uint8_t*)buffer+7)=cursor->backR;
    *((uint8_t*)buffer+8)=cursor->backG;
    *((uint8_t*)buffer+9)=cursor->backB;//10B

    *((uint16_t*)buffer+5)=cursor->width;
    *((uint16_t*)buffer+6)=cursor->height;
    *((uint16_t*)buffer+7)=cursor->xhot;
    *((uint16_t*)buffer+8)=cursor->yhot;//18B

    *((uint32_t*)buffer+5)=cursor->serialNumber;
    *((uint32_t*)buffer+6)=cursor->size;

//     EPHYR_DBG("SENDING CURSOR %d with size %d", cursor->serialNumber, cursor->size);

    #warning check this
    int ln=write(remoteVars.clientsock,buffer,56);

    int sent=0;

    while(sent<cursor->size)
    {
        int l=write(remoteVars.clientsock, cursor->data+sent,((cursor->size-sent)<MAXMSGSIZE)?(cursor->size-sent):MAXMSGSIZE);
        if(l<0)
        {
            EPHYR_DBG("Error sending cursor!!!!!");
            break;
        }
        sent+=l;
    }
    remoteVars.data_sent+=sent;
    //     EPHYR_DBG("SENT total %d", total);

    return sent;
}


int32_t send_frame(u_int32_t width, uint32_t height, uint32_t x, uint32_t y, uint32_t crc, struct frame_region* regions)
{

    uint32_t numofregions=0;


    for(int i=0;i<9;++i)
    {
        if(regions[i].rect.size.width && regions[i].rect.size.height)
            ++numofregions;
    }
    unsigned char buffer[64]={};
    *((uint32_t*)buffer)=FRAME;
    *((uint32_t*)buffer+1)=width;
    *((uint32_t*)buffer+2)=height;
    *((uint32_t*)buffer+3)=x;
    *((uint32_t*)buffer+4)=y;
    *((uint32_t*)buffer+5)=numofregions;
    *((uint32_t*)buffer+6)=crc;
/*
    if(numofregions)
        EPHYR_DBG("SENDING NEW FRAME %x", crc);
    else
        EPHYR_DBG("SENDING REFERENCE %x", crc);*/

#warning check this
    int ln=write(remoteVars.clientsock, buffer,56);
    uint32_t total=0;
    for(int i=0;i<9;++i)
    {
        if(!(regions[i].rect.size.width && regions[i].rect.size.height))
            continue;

/*        EPHYR_DBG("SENDING FRAME REGION %x %dx%d %d",regions[i].source_crc, regions[i].rect.size.width, regions[i].rect.size.height,
                    regions[i].size);
*/
        *((uint32_t*)buffer)=regions[i].source_crc;

        /*if(*((uint32_t*)buffer)=regions[i].source_crc)
            EPHYR_DBG("SENDING REFERENCE %x", *((uint32_t*)buffer)=regions[i].source_crc);*/

        *((uint32_t*)buffer+1)=regions[i].source_coordinates.x;
        *((uint32_t*)buffer+2)=regions[i].source_coordinates.y;
        *((uint32_t*)buffer+3)=regions[i].rect.lt_corner.x;
        *((uint32_t*)buffer+4)=regions[i].rect.lt_corner.y;
        *((uint32_t*)buffer+5)=regions[i].rect.size.width;
        *((uint32_t*)buffer+6)=regions[i].rect.size.height;
        *((uint32_t*)buffer+7)=regions[i].size;

        #warning check this
        int ln=write(remoteVars.clientsock, buffer, 64);

        int sent=0;


        while(sent<regions[i].size)
        {
            int l=write(remoteVars.clientsock,regions[i].compressed_data+sent,
                        ((regions[i].size-sent)<MAXMSGSIZE)?(regions[i].size-sent):MAXMSGSIZE);
            if(l<0)
            {
                EPHYR_DBG("Error sending file!!!!!");
                break;
            }
            sent+=l;
        }
        total+=sent;
//         EPHYR_DBG("SENT %d",sent);
/*
        EPHYR_DBG("\ncache elements %d, cache size %lu(%dMB), connection time=%d, sent %lu(%dMB)\n",
                 cache_elements, cache_size, (int) (cache_size/1024/1024),
                 time(NULL)-con_start_time, data_sent, (int) (data_sent/1024/1024));
*/

    }
    remoteVars.data_sent+=total;
//     EPHYR_DBG("SENT total %d", total);

    return total;
}

int send_deleted_elements()
{
    unsigned char buffer[56];
    *((uint32_t*)buffer)=DELETED;
    *((uint32_t*)buffer+1)=remoteVars.deleted_list_size;

    #warning check this
    int ln=write(remoteVars.clientsock,buffer,56);
    //     data_sent+=48;
    int sent=0;

    unsigned char* list=malloc(sizeof(uint32_t)*remoteVars.deleted_list_size);

    unsigned int i=0;
    while(remoteVars.first_deleted_elements)
    {
//         EPHYR_DBG("To DELETE FRAME %x", remoteVars.first_deleted_elements->crc);

        *((uint32_t*)list+i)=remoteVars.first_deleted_elements->crc;
        struct deleted_elem* elem=remoteVars.first_deleted_elements;
        remoteVars.first_deleted_elements=elem->next;
        free(elem);
        ++i;
    }

    remoteVars.last_deleted_elements=0l;

    //         EPHYR_DBG("SENDING IMG length - %d, number - %d\n",length,framenum_sent++);
    int length=remoteVars.deleted_list_size*sizeof(uint32_t);
    while(sent<length)
    {
        int l=write(remoteVars.clientsock,list+sent,((length-sent)<MAXMSGSIZE)?(length-sent):MAXMSGSIZE);
        if(l<0)
        {
            EPHYR_DBG("Error sending list of deleted elements!!!!!");
            break;
        }
        sent+=l;
    }
    remoteVars.deleted_list_size=0;
    return sent;
}

int send_deleted_cursors()
{
    unsigned char buffer[56];
    *((uint32_t*)buffer)=DELETEDCURSOR;
    *((uint32_t*)buffer+1)=remoteVars.deletedcursor_list_size;

    #warning check this
    int ln=write(remoteVars.clientsock,buffer,56);
    int sent=0;

    unsigned char* list=malloc(sizeof(uint32_t)*remoteVars.deletedcursor_list_size);

    unsigned int i=0;
    while(remoteVars.first_deleted_cursor)
    {
        *((uint32_t*)list+i)=remoteVars.first_deleted_cursor->serialNumber;

//         EPHYR_DBG("delete cursord %d",first_deleted_cursor->serialNumber);
        struct deletedCursor* elem=remoteVars.first_deleted_cursor;
        remoteVars.first_deleted_cursor=elem->next;
        free(elem);
        ++i;
    }

    remoteVars.last_deleted_cursor=0l;

//     EPHYR_DBG("Sending list from %d elements", deletedcursor_list_size);
    int length=remoteVars.deletedcursor_list_size*sizeof(uint32_t);
    while(sent<length)
    {
        int l=write(remoteVars.clientsock,list+sent,((length-sent)<MAXMSGSIZE)?(length-sent):MAXMSGSIZE);
        if(l<0)
        {
            EPHYR_DBG("Error sending list of deleted cursors!!!!!");
            break;
        }
        sent+=l;
    }
    remoteVars.deletedcursor_list_size=0;
    return sent;
}

int send_selection(int sel, char* data, uint32_t length, uint32_t format)
{
    unsigned char buffer[56];
    *((uint32_t*)buffer)=SELECTION;
    *((uint32_t*)buffer+1)=sel;
    *((uint32_t*)buffer+2)=format;
    *((uint32_t*)buffer+3)=length;

    #warning check this
    int ln=write(remoteVars.clientsock,buffer,56);
    int sent=0;

    while(sent<length)
    {
        int l=write(remoteVars.clientsock,data+sent,((length-sent)<MAXMSGSIZE)?(length-sent):MAXMSGSIZE);
        if(l<0)
        {
            EPHYR_DBG("Error sending selection!!!!!");
            break;
        }
        sent+=l;
    }
    return sent;
}



//sendqueue_mutex should be locked when calling this function
struct cache_elem* find_best_match(struct cache_elem* frame, unsigned int* match_val)
{
    struct cache_elem* current=frame->prev;
    struct cache_elem* best_match_frame=0;
    unsigned int distance=0;
    unsigned int best_match_value=99999;
    while(current)
    {
        if((best_match_frame&& best_match_value<=distance) || distance > MAX_MATCH_VAL)
        {
            break;
        }
        if(!current->sent || !current->data || !current->size)
        {
            current=current->prev;
            continue;
        }

        unsigned int matchVal=0;
        matchVal+=abs(current->width-frame->width)/10;
        matchVal+=abs(current->height-frame->height)/10;
        matchVal+=abs(current->rval-frame->rval);
        matchVal+=abs(current->gval-frame->gval);
        matchVal+=abs(current->bval-frame->bval);
        matchVal+=distance;

        if(!best_match_frame || (matchVal<best_match_value))
        {
            best_match_frame=current;
            best_match_value=matchVal;
        }

        distance++;
        current=current->prev;
    }
    *match_val=best_match_value;
    return best_match_frame;
}

BOOL checkShiftedRegion( struct cache_elem* src, struct cache_elem* dst,  int32_t x, int32_t y,
                  int32_t width, int32_t height, int32_t horiz_shift, int32_t vert_shift)
{

//     EPHYR_DBG("FENTER %d %d %d %d %d",x,y,width,height,shift);
    int32_t vert_point=20;
    int32_t hor_point=20;

    if(vert_point>height)
    {
        vert_point=height;
    }
    if(hor_point>width)
    {
        hor_point=width;
    }

    int32_t vert_inc=height/vert_point;
    int32_t hor_inc=width/hor_point;

    if(vert_inc<1)
    {
        vert_inc=1;
    }

    if(hor_inc<1)
    {
        hor_inc=1;
    }


    uint32_t i=0;

    for(int32_t ry=y;ry<height+y ;ry+=vert_inc)
    {
        for(int32_t rx=x+(i++)%vert_inc; rx<width+x; rx+=hor_inc)
        {
            int32_t src_ind=(ry*src->width+rx)*CACHEBPP;
            int32_t dst_ind=((ry+vert_shift)*dst->width+rx+horiz_shift)*CACHEBPP;


            if (src_ind<0 || src_ind +2 > src->size || dst_ind <0 || dst_ind > dst->size )
            {
                EPHYR_DBG("!!!!!WARNING BROKEN BOUNDARIES: sind %d, dind %d, ssize %d, dsize %d",
                src_ind, dst_ind, src->size, dst->size);

            }

//             EPHYR_DBG("Indexes %d, %d, %d, %d %d", src_ind, dst_ind, src->size, dst->size, i);
            if(src->data[src_ind]!=dst->data[dst_ind])
            {
//                 EPHYR_DBG("FEXIT");
                return FALSE;
            }
            if(src->data[src_ind+1]!=dst->data[dst_ind+1])
            {
//                 EPHYR_DBG("FEXIT");
                return FALSE;
            }
            if(src->data[src_ind+2]!=dst->data[dst_ind+2])
            {
//                 EPHYR_DBG("FEXIT");
                return FALSE;
            }
        }
    }
//     EPHYR_DBG("FEXIT");
    return TRUE;
}


int32_t checkScrollUp(struct cache_elem* source, struct cache_elem* dest)
{
//     EPHYR_DBG("checking for up scroll %u, %u, %u, %u", source->width, source->height, dest->width, dest->height);
    int32_t max_shift=source->height/3;
    int32_t x=source->width/10;
    int32_t y=source->height/10;

    int32_t width=source->width/2-source->width/5;

    if(x+width >= dest->width)
    {
         width=dest->width-x-1;
    }
    if(width<2)
    {
//         EPHYR_DBG("DST too small(w), skeep checking %d %d %d %d", source->width, source->height, dest->width, dest->height);
        return -1;
    }

    int32_t height=source->height/2-source->height/10;

    if(y+height+max_shift >=dest->height)
    {
        height=dest->height-max_shift-y;
    }
    if(height<2)
    {
//         EPHYR_DBG("DST too small(h), skeep checking %d %d %d %d", source->width, source->height, dest->width, dest->height);
        return -1;
    }

//     EPHYR_DBG(" %u %u %u %u %u",x,y,width, height, max_shift);


    for(int32_t shift=0;shift<max_shift;++shift)
    {
        if(!checkShiftedRegion( source, dest, x, y, width, height, 0, shift))
            continue;
        if(shift)
        {
//             EPHYR_DBG("Shift %d, Cursor is matched!\n",shift);
            return shift;
        }
    }
    return -1;
}


int32_t checkScrollDown(struct cache_elem* source, struct cache_elem* dest)
{
//     EPHYR_DBG("checking for down scroll %u, %u, %u, %u", source->width, source->height, dest->width, dest->height);
    int32_t max_shift=source->height/3*-1;
    int32_t x=source->width/10;
    int32_t y=source->height/2;

    int32_t width=source->width/2-source->width/5;
    int32_t height=source->height/2-source->height/10;

//     EPHYR_DBG(" %u %u %u %u %u",x,y,width, height, max_shift);

    if(x+width >= dest->width)
    {
        width=dest->width-x-1;
    }
    if(width<2)
    {
//         EPHYR_DBG("DST too small(w), skeep checking %d %d %d %d", source->width, source->height, dest->width, dest->height);
        return 1;
    }


    if(y+height+abs(max_shift) >=dest->height)
    {
        height=dest->height-abs(max_shift)-y;
    }
    if(height<2)
    {
//         EPHYR_DBG("DST too small(h), skeep checking %d %d %d %d", source->width, source->height, dest->width, dest->height);
        return 1;
    }


    for(int32_t shift=0;shift>max_shift;--shift)
    {
        if(!checkShiftedRegion( source, dest, x, y, width, height, 0,shift))
            continue;
        if(shift)
            //         EPHYR_DBG("Shift %d, Cursor is matched!\n",shift);
            return shift;
    }
    return 1;
}


int32_t checkScrollRight(struct cache_elem* source, struct cache_elem* dest)
{
    //     EPHYR_DBG("checking for up scroll %u, %u, %u, %u", source->width, source->height, dest->width, dest->height);
    int32_t max_shift=source->width/3;
    int32_t x=source->width/10;
    int32_t y=source->height/10;

    int32_t height=source->height/2-source->height/5;

    if(y+height >= dest->height)
    {
        height=dest->height-y-1;
    }
    if(height<2)
    {
//         EPHYR_DBG("DST too small(d), skeep checking %d %d %d %d", source->width, source->height, dest->width, dest->height);
        return -1;
    }

    int32_t width=source->width/2-source->width/10;

    if(x+width+max_shift >=dest->width)
    {
        width=dest->width-abs(max_shift)-x;
    }
    if(width<2)
    {
//         EPHYR_DBG("DST too small(w), skeep checking %d %d %d %d", source->width, source->height, dest->width, dest->height);
        return -1;
    }

//     EPHYR_DBG(" %u %u %u %u %u",x,y,width, height, max_shift);


    for(int32_t shift=0;shift<max_shift;++shift)
    {
        if(!checkShiftedRegion( source, dest, x, y, width, height, shift, 0 ))
            continue;
        if(shift)
        {
            //             EPHYR_DBG("Shift %d, Cursor is matched!\n",shift);
            return shift;
        }
    }
    return -1;
}



int32_t checkScrollLeft(struct cache_elem* source, struct cache_elem* dest)
{
    //     EPHYR_DBG("checking for up scroll %u, %u, %u, %u", source->width, source->height, dest->width, dest->height);
    int32_t max_shift=source->width/3*-1;
    int32_t x=source->width/2;
    int32_t y=source->height/10;

    int32_t height=source->height/2-source->height/5;

    if(y+height >= dest->height)
    {
        height=dest->height-y-1;
    }
    if(height<2)
    {
//         EPHYR_DBG("DST too small(d), skeep checking %d %d %d %d", source->width, source->height, dest->width, dest->height);
        return 1;
    }

    int32_t width=source->width/2-source->width/10;

    if(x+width+abs(max_shift) >=dest->width)
    {
        width=dest->width-abs(max_shift)-x;
    }
    if(width<2)
    {
//         EPHYR_DBG("DST too small(w), skeep checking %d %d %d %d", source->width, source->height, dest->width, dest->height);
        return 1;
    }

//     EPHYR_DBG(" %d %d %d %d %d",x,y,width, height, max_shift);


    for(int32_t shift=0;shift>max_shift;--shift)
    {
        if(!checkShiftedRegion( source, dest, x, y, width, height, shift, 0 ))
            continue;
        if(shift)
        {
//             EPHYR_DBG("Shift %d, Cursor is matched!\n",shift);
            return shift;
        }
    }
    return 1;
}



BOOL checkEquality(struct cache_elem* src, struct cache_elem* dst,
                   int32_t shift_horiz, int32_t shift_vert, rectangle* common_rect)
{

    int32_t center_x=src->width/2;
    int32_t center_y=src->height/2;

    int32_t x, y=center_y;

    int32_t right_x=src->width;
    --right_x;

    int32_t down_y=src->height;
    --down_y;

    int32_t left_x=0;
    int32_t top_y=0;

    if(center_x+shift_horiz >= dst->width  || center_y+shift_vert >=dst->height)
    {
        //dst is too small for shift
        return FALSE;
    }


    if(left_x+shift_horiz<0)
    {
        left_x=0-shift_horiz;
    }

    if(top_y+shift_vert<0)
    {
        top_y=0-shift_vert;
    }

    if( right_x+shift_horiz>=dst->width)
    {
        right_x=dst->width-shift_horiz-1;
    }

    if(down_y+shift_vert>=dst->height)
    {
        down_y=dst->height-shift_vert-1;
    }


//     EPHYR_DBG("Center: %dx%d", center_x, center_y);



//     EPHYR_DBG("initial down_right %dx%d",right_x,down_y);

    for(y=center_y;y<=down_y;++y)
    {
        for(x=center_x; x<=right_x;++x)
        {
            int32_t src_ind=(y*src->width+x)*CACHEBPP;
            int32_t dst_ind=((y+shift_vert)*dst->width+x+shift_horiz)*CACHEBPP;

//             EPHYR_DBG("%d %d %d %d %d %d", x, y, right_x, down_y, dst->height, shift_vert);

            if (src_ind<0 || src_ind +2 > src->size || dst_ind <0 || dst_ind > dst->size )
            {
                EPHYR_DBG("!!!!!WARNING BROKEN BOUNDARIES: sind %d, dind %d, ssize %d, dsize %d",
                          src_ind, dst_ind, src->size, dst->size);
                right_x=x-1;
                down_y=y-1;
                goto loop_exit_0;

            }

            if((src->data[src_ind]!=dst->data[dst_ind])||(src->data[src_ind+1]!=dst->data[dst_ind+1])||
                (src->data[src_ind+2]!=dst->data[dst_ind+2]))
            {
                int32_t hor_dist=right_x-x;
                int32_t vert_dist=down_y-y;
                if(hor_dist<vert_dist)
                {
                    right_x=x-1;
                }
                else
                {
                    down_y=y-1;
                }
//                 EPHYR_DBG("limit right down to %dx%d",right_x,down_y);
                break;
            }
        }
    }


    loop_exit_0:
//     EPHYR_DBG("initial down_left %dx%d",left_x,down_y);
    for(y=center_y;y<=down_y;++y)
    {
        for(x=center_x; x>=left_x;--x)
        {
            int32_t src_ind=(y*src->width+x)*CACHEBPP;
            int32_t dst_ind=((y+shift_vert)*dst->width+x+shift_horiz)*CACHEBPP;

            if (src_ind<0 || src_ind +2 > src->size || dst_ind <0 || dst_ind > dst->size )
            {
                EPHYR_DBG("!!!!!WARNING BROKEN BOUNDARIES: sind %d, dind %d, ssize %d, dsize %d",
                          src_ind, dst_ind, src->size, dst->size);
                left_x=x+1;
                down_y=y-1;
                goto loop_exit_1;
            }

            if((src->data[src_ind]!=dst->data[dst_ind])||(src->data[src_ind+1]!=dst->data[dst_ind+1])||
                (src->data[src_ind+2]!=dst->data[dst_ind+2]))
            {
                uint32_t hor_dist=x-left_x;
                uint32_t vert_dist=down_y-y;
                if(hor_dist<vert_dist)
                {
                    left_x=x+1;
                }
                else
                {
                    down_y=y-1;
                }
//                 EPHYR_DBG("limit left down to %dx%d",left_x,down_y);
                break;
            }
        }
    }

    loop_exit_1:

//     EPHYR_DBG("initial top_right %dx%d",right_x,top_y);

    for(y=center_y;y>=top_y;--y)
    {
        for(x=center_x; x<=right_x;++x)
        {
            int32_t src_ind=(y*src->width+x)*CACHEBPP;
            int32_t dst_ind=((y+shift_vert)*dst->width+x+shift_horiz)*CACHEBPP;

            if (src_ind<0 || src_ind +2 > src->size || dst_ind <0 || dst_ind > dst->size )
            {
                EPHYR_DBG("!!!!!WARNING BROKEN BOUNDARIES: sind %d, dind %d, ssize %d, dsize %d",
                          src_ind, dst_ind, src->size, dst->size);
                right_x=x-1;
                top_y=y+1;
                goto loop_exit_2;
            }


            if((src->data[src_ind]!=dst->data[dst_ind])||(src->data[src_ind+1]!=dst->data[dst_ind+1])||
                (src->data[src_ind+2]!=dst->data[dst_ind+2]))
            {
                uint32_t hor_dist=right_x-x;
                uint32_t vert_dist=y-top_y;
                if(hor_dist<vert_dist)
                {
                    right_x=x-1;
                }
                else
                {
                    top_y=y+1;
                }
//                 EPHYR_DBG("limit right top to %dx%d",right_x,top_y);
                break;
            }
        }
    }

    loop_exit_2:;

//     EPHYR_DBG("top_right %dx%d",right_x,top_y);



//     EPHYR_DBG("initial top_left %dx%d\n",left_x,top_y);
    for(y=center_y;y>=top_y;--y)
    {
        for(x=center_x; x>=left_x;--x)
        {
            int32_t src_ind=(y*src->width+x)*CACHEBPP;
            int32_t dst_ind=((y+shift_vert)*dst->width+x+shift_horiz)*CACHEBPP;

            if (src_ind<0 || src_ind +2 > src->size || dst_ind <0 || dst_ind > dst->size )
            {
                EPHYR_DBG("!!!!!WARNING BROKEN BOUNDARIES: sind %d, dind %d, ssize %d, dsize %d",
                          src_ind, dst_ind, src->size, dst->size);
                left_x=x+1;
                top_y=y+1;
                goto loop_exit_3;

            }


            if((src->data[src_ind]!=dst->data[dst_ind])||(src->data[src_ind+1]!=dst->data[dst_ind+1])||
                (src->data[src_ind+2]!=dst->data[dst_ind+2]))
            {
                uint32_t hor_dist=x-left_x;
                uint32_t vert_dist=y-top_y;
                if(hor_dist<vert_dist)
                {
                    left_x=x+1;
                }
                else
                {
                    top_y=y+1;
                }
//                 EPHYR_DBG("limit left down to %dx%d",left_x,down_y);
                break;
            }
        }
     }

     loop_exit_3:;

     common_rect->size.width=right_x-left_x+1;
     common_rect->size.height=down_y-top_y+1;

     if(common_rect->size.width<1 || common_rect->size.height <1)
     {
//          EPHYR_DBG("!!!!!!!NEGATIVE OR NULL GEOMETRY!!!!!!!");
         return FALSE;
     }

     common_rect->lt_corner.x=left_x;
     common_rect->lt_corner.y=top_y;
//      EPHYR_DBG("Geometry: %d:%d  %dx%d shift - %d  %d ", left_x, top_y, common_rect->size.width,
//                  common_rect->size.height, shift_horiz, shift_vert);
     return TRUE;
}


BOOL checkMovedContent(struct cache_elem* source, struct cache_elem* dest, int32_t* horiz_shift, int32_t* vert_shift)
{
//     EPHYR_DBG("checking for moved content %u, %u, %u, %u", source->width, source->height, dest->width, dest->height);
    int32_t max_shift=source->width/8;

    if(max_shift>source->height/8)
        max_shift=source->height/8;

    if(max_shift>20)
        max_shift=20;


    int32_t x=source->width/2;
    int32_t y=source->height/2;

    int32_t height=source->height/4;

    if(y+height+max_shift >= dest->height)
    {
        height=dest->height-max_shift-y-1;
    }
    if(height<2)
    {
//         EPHYR_DBG("DST too small(d), skeep checking %d %d %d %d", source->width, source->height, dest->width, dest->height);
        return FALSE;
    }

    int32_t width=source->width/4;

    if(x+width+max_shift >=dest->width)
    {
        width=dest->width-max_shift-x;
    }
    if(width<2)
    {
//         EPHYR_DBG("DST too small(w), skeep checking %d %d %d %d", source->width, source->height, dest->width, dest->height);
        return FALSE;
    }

    //     EPHYR_DBG(" %d %d %d %d %d",x,y,width, height, max_shift);


    for(int32_t hshift=0;hshift<max_shift;hshift++)
    {
        for(int32_t vshift=0;vshift<max_shift;vshift++)
        {

            if(checkShiftedRegion( source, dest, x, y, width, height, hshift, vshift ))
            {
                *horiz_shift=hshift;
                *vert_shift=vshift;
                return TRUE;
            }
            if(checkShiftedRegion( source, dest, x, y, width, height, hshift*-1, vshift*-1 ))
            {
                *horiz_shift=hshift*-1;
                *vert_shift=vshift*-1;
                return TRUE;
            }
            if(checkShiftedRegion( source, dest, x, y, width, height, hshift*-1, vshift ))
            {
                *horiz_shift=hshift*-1;
                *vert_shift=vshift;
                return TRUE;
            }
            if(checkShiftedRegion( source, dest, x, y, width, height, hshift, vshift*-1 ))
            {
                *horiz_shift=hshift;
                *vert_shift=vshift*-1;
                return TRUE;
            }
        }
    }
    return FALSE;
}

BOOL findDiff(struct cache_elem* source, struct cache_elem* dest, rectangle* diff_rect)
{
    int32_t left_x=source->width-1, top_y=source->height-1, right_x=0, bot_y=0;
    for(int32_t y=0;y<source->height;++y)
    {
        for(int32_t x=0;x<source->width;++x)
        {
            int32_t ind=(y*source->width+x)*CACHEBPP;
            if((source->data[ind] != dest->data[ind]) || (source->data[ind+1] != dest->data[ind+1])||
                (source->data[ind+2] != dest->data[ind+2]))
            {
                if(x<left_x)
                    left_x=x;
                if(x>right_x)
                    right_x=x;
                if(y<top_y)
                    top_y=y;
                if(y>bot_y)
                    bot_y=y;
            }
        }
    }

    diff_rect->size.width=right_x-left_x+1;
    diff_rect->size.height=bot_y-top_y+1;

    diff_rect->lt_corner.x=left_x;
    diff_rect->lt_corner.y=top_y;

    float eff= (float)(diff_rect->size.width*diff_rect->size.height)/ (float)(source->width*source->height);

    if(eff>0.8)
        return FALSE;

//     EPHYR_DBG("REG_GEOM: %dx%d. DIF_GEOM %d,%d - %dx%d EFF=%f", source->width, source->height, left_x,top_y,
//                diff_rect->size.width, diff_rect->size.height,eff);
    return TRUE;
}

BOOL find_common_regions(struct cache_elem* source, struct cache_elem* dest, BOOL* diff, rectangle* common_rect,
                         int32_t* hshift, int32_t* vshift)
{
    //     EPHYR_DBG("checking for common regions");




    *diff=FALSE;

    *hshift=0;
    *vshift=checkScrollDown(source,dest);
    if(*vshift<0)
    {
//         EPHYR_DBG("Found scroll down, vert shift %d %u %u %u %u" , *vshift, dest->width, dest->height, dest->crc, source->crc);
        return checkEquality(source, dest, 0, *vshift, common_rect);
    }

    *hshift=0;
    *vshift=checkScrollUp(source, dest);
    if(*vshift>0)
    {
//         EPHYR_DBG("Found scroll up, vert shift %d %u %u %u %u" , *vshift, dest->width, dest->height, dest->crc, source->crc);
        return checkEquality(source, dest, 0, *vshift, common_rect);
    }


#warning stop here for the moment, let's see later if we'll use it'
    return FALSE;


    *vshift=0;
    *hshift=checkScrollRight(source, dest);
    if(*hshift>0)
    {
        return checkEquality(source, dest, *hshift, 0, common_rect);
    }

    *vshift=0;
    *hshift=checkScrollLeft(source, dest);
    if(*hshift<0)
    {
//         EPHYR_DBG("SCROLL LEFT %d", *hshift);
        return checkEquality(source, dest, *hshift, 0, common_rect);
    }

    if((source->width != dest->width) && (source->height!=dest->height))
    {
        *vshift=0;
        *hshift=0;
        int32_t h_shift, v_shift;
        if(checkMovedContent(source, dest, &h_shift, &v_shift))
        {
            *hshift=h_shift;
            *vshift=v_shift;
//             EPHYR_DBG("found moved content %d, %d", *hshift, *vshift);
            return checkEquality(source, dest, *hshift, *vshift, common_rect);
        }
    }


    if((source->width == dest->width) && (source->height==dest->height))
    {
        *diff=TRUE;
        //         EPHYR_DBG("LOOK FOR IMAGE DIFFERENCE");
        return findDiff(source, dest, common_rect);
    }

//     EPHYR_DBG("Scroll not found %d",dest->crc);
    return FALSE;
}

//use only from send thread
void sendMainImageFromSendThread(uint32_t width, uint32_t height, int32_t dx ,int32_t dy)
{

    if(width!=0)
    {
        EPHYR_DBG("sending UPDATE- %dx%d, %d,%d",width, height,dx,dy);
    }
    else
    {
        EPHYR_DBG("sending mainImage");
    }

    pthread_mutex_lock(&remoteVars.mainimg_mutex);
    uint32_t length=0;


    char fname[255];
    char* f;

    struct frame_region regions[9];
    for(int i=0;i<9;++i)
    {
        regions[i].rect.size.width=0;
        regions[i].source_crc=0;
        regions[i].compressed_data=0;
        regions[i].size=0;
    }


    BOOL mainImage=FALSE;
    if(!width || (dx==0 && dy==0 && width==remoteVars.main_img_width && height==remoteVars.main_img_height))
    {
        mainImage=TRUE;
        dx=dy=0;
        width=remoteVars.main_img_width;
        height=remoteVars.main_img_height;
    }

    uint32_t isize=width*height*CACHEBPP;
    unsigned char* data=malloc(isize);

    u_int32_t i=0;

    for(int32_t y=0;y<height;++y)
    {
        for(int32_t x=0; x< width; ++x)
        {
            uint32_t ind=((y+dy)*remoteVars.main_img_width+dx+x)*XSERVERBPP;
            memcpy(data+i*CACHEBPP, remoteVars.main_img+ind, CACHEBPP);
            ++i;
        }
    }

    regions[0].compressed_data=image_compress(width, height, data, &(regions[0].size), CACHEBPP, 0l);
    free(data);
    length=regions[0].size;
    regions[0].rect.size.width=width;
    regions[0].rect.size.height=height;
    regions[0].rect.lt_corner.x=dx;
    regions[0].rect.lt_corner.y=dy;

    if(mainImage)
    {
        send_frame(width, height,-1,-1,0,regions);
    }
    else
    {
        send_frame(width, height,dx,dy,0,regions);
    }

    pthread_mutex_unlock(&remoteVars.mainimg_mutex);
    free(regions[0].compressed_data);
}

void *send_frame_thread (void *threadid)
{
    long tid;
    tid = (long)threadid;
    EPHYR_DBG("Started sending thread: #%ld!\n", tid);

    while (1)
    {
        EPHYR_DBG ("waiting for TCP connection\n");
        remoteVars.clientsock = accept ( remoteVars.serversock, (struct sockaddr *) &remoteVars.address, &remoteVars.addrlen );
        if (remoteVars.clientsock <= 0)
        {
            EPHYR_DBG( "ACCEPT ERROR OR CANCELD!\n");
            break;
        }
        EPHYR_DBG ("Connection from (%s)...\n", inet_ntoa (remoteVars.address.sin_addr));

        if(strlen(remoteVars.acceptAddr))
        {

            struct addrinfo hints, *res;
            int errcode;
            char addrstr[100];
            void *ptr;

            memset (&hints, 0, sizeof (hints));
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_flags |= AI_CANONNAME;

            errcode = getaddrinfo (remoteVars.acceptAddr, NULL, &hints, &res);
            if (errcode != 0 || !res)
            {
                EPHYR_DBG ("ERROR LOOKUP %d", remoteVars.acceptAddr);
                terminateServer(-1);
            }
            if(  ((struct sockaddr_in *) (res->ai_addr))->sin_addr.s_addr != remoteVars.address.sin_addr.s_addr)
            {
                EPHYR_DBG("Connection only allowed from %s",inet_ntoa ( ((struct sockaddr_in *)(res->ai_addr))->sin_addr));
                shutdown(remoteVars.clientsock, SHUT_RDWR);
                close(remoteVars.clientsock);
                continue;
            }
        }
        if(strlen(remoteVars.cookie))
        {
//             EPHYR_DBG("Checking cookie: %s",remoteVars.cookie);
            char msg[33];
            int length=32;
            int ready=0;
            while(ready<length)
            {
                int chunk=read(remoteVars.clientsock, msg+ready, 32-ready);
                if(chunk<=0)
                {
                    EPHYR_DBG("READ COOKIE ERROR");
                    shutdown(remoteVars.clientsock, SHUT_RDWR);
                    close(remoteVars.clientsock);
                    continue;
                }
                ready+=chunk;
                EPHYR_DBG("got %d COOKIE BYTES from client", ready);
            }
            if(strncmp(msg,remoteVars.cookie,32))
            {
                EPHYR_DBG("Wrong cookie");
                shutdown(remoteVars.clientsock, SHUT_RDWR);
                close(remoteVars.clientsock);
                continue;
            }
            EPHYR_DBG("Cookie approved");
        }
        else
        {
            EPHYR_DBG("Warning: not checking client's cookie");
        }



        pthread_mutex_lock(&remoteVars.sendqueue_mutex);
        //only accept one client, close server socket
        shutdown(remoteVars.serversock, SHUT_RDWR);
        close(remoteVars.serversock);
        SetNotifyFd(remoteVars.clientsock, clientReadNotify, X_NOTIFY_READ, NULL);
        remoteVars.client_connected=TRUE;
        if(remoteVars.checkConnectionTimer)
        {
            TimerFree(remoteVars.checkConnectionTimer);
            remoteVars.checkConnectionTimer=0;
        }
        remoteVars.client_initialized=FALSE;
        remoteVars.con_start_time=time(NULL);
        remoteVars.data_sent=0;
        remoteVars.data_copy=0;
        remoteVars.evBufferOffset=0;
        setAgentState(RUNNING);
        pthread_mutex_unlock(&remoteVars.sendqueue_mutex);

        while(1)
        {
            pthread_mutex_lock(&remoteVars.sendqueue_mutex);
            if(!remoteVars.client_connected)
            {
                EPHYR_DBG ("TCP connection closed\n");
                RemoveNotifyFd(remoteVars.clientsock);
                shutdown(remoteVars.clientsock, SHUT_RDWR);
                close(remoteVars.clientsock);
                pthread_mutex_unlock(&remoteVars.sendqueue_mutex);
                break;
            }
            remoteVars.client_connected=TRUE;


            if(!remoteVars.first_sendqueue_element && !remoteVars.firstCursor)
            {
                //sleep if frame queue is empty
                pthread_cond_wait(&remoteVars.have_sendqueue_cond, &remoteVars.sendqueue_mutex);
            }

            //mutex is locked on this point

            if(remoteVars.firstCursor)
            {
                //get cursor from queue, delete it from queue, unlock mutex and send cursor. After sending free cursor
                struct cursorFrame* cframe=remoteVars.firstCursor;

                if(remoteVars.firstCursor->next)
                    remoteVars.firstCursor=remoteVars.firstCursor->next;
                else
                    remoteVars.firstCursor=remoteVars.lastCursor=0;
                pthread_mutex_unlock(&remoteVars.sendqueue_mutex);
                send_cursor(cframe);
                if(cframe->data)
                    free(cframe->data);
                free(cframe);

            }
            else
            {
                pthread_mutex_unlock(&remoteVars.sendqueue_mutex);
            }

            pthread_mutex_lock(&remoteVars.sendqueue_mutex);
            if(remoteVars.selstruct.clipboard.changed)
            {
                size_t sz=remoteVars.selstruct.clipboard.size;
                char* data=malloc(sz);
                memcpy(data, remoteVars.selstruct.clipboard.data, sz);
                remoteVars.selstruct.clipboard.changed=FALSE;
                int format=remoteVars.selstruct.clipboard.mimeData;
                pthread_mutex_unlock(&remoteVars.sendqueue_mutex);
                send_selection(CLIPBOARD,data,sz, format);
                free(data);
            }
            else
                pthread_mutex_unlock(&remoteVars.sendqueue_mutex);

            pthread_mutex_lock(&remoteVars.sendqueue_mutex);
            if(remoteVars.selstruct.selection.changed)
            {
                size_t sz=remoteVars.selstruct.selection.size;
                char* data=malloc(sz);
                memcpy(data, remoteVars.selstruct.selection.data, sz);
                remoteVars.selstruct.selection.changed=FALSE;
                int format=remoteVars.selstruct.selection.mimeData;
                pthread_mutex_unlock(&remoteVars.sendqueue_mutex);
                send_selection(PRIMARY, data, sz, format);
                free(data);
            }
            else
                pthread_mutex_unlock(&remoteVars.sendqueue_mutex);

            pthread_mutex_lock(&remoteVars.sendqueue_mutex);
            if(remoteVars.first_sendqueue_element)
            {
                int elems=queue_elements();
                if(remoteVars.maxfr<elems)
                {
                    remoteVars.maxfr=elems;
                }
                //                 EPHYR_DBG("have %d max frames in queue, current %d", remoteVars.,elems);
                struct cache_elem* frame=remoteVars.first_sendqueue_element->frame;
                //delete first element from frame queue
                struct sendqueue_element* current=remoteVars.first_sendqueue_element;
                if(remoteVars.first_sendqueue_element->next)
                {
                    remoteVars.first_sendqueue_element=remoteVars.first_sendqueue_element->next;
                }
                else
                {
                    remoteVars.first_sendqueue_element=remoteVars.last_sendqueue_element=NULL;
                }
                uint32_t  x, y;
                x=current->x;
                y=current->y;
                int32_t width, height;
                width=current->width;
                height=current->height;
                free(current);

                if(frame)
                {
                    uint32_t crc = frame->crc;
                    uint32_t width=frame->width;
                    uint32_t height=frame->height;
                    BOOL show_time=FALSE;

                    //unlock sendqueue for main thread
                    pthread_mutex_unlock(&remoteVars.sendqueue_mutex);
                    send_frame(width, height, x, y, crc, frame->regions);
                }
                else
                {
                    EPHYR_DBG("Sending main image or screen update");
                    pthread_mutex_unlock(&remoteVars.sendqueue_mutex);
                    sendMainImageFromSendThread(width, height, x, y);
                }

                pthread_mutex_lock(&remoteVars.sendqueue_mutex);
                if(frame)
                {
                    frame->sent=TRUE;
                    frame->busy--;
                    if(frame->source)
                        frame->source->busy--;
                    frame->source=0;

                    for(int i=0;i<9;++i)
                    {
                        if(frame->regions[i].size)
                        {
                            free(frame->regions[i].compressed_data);
                            frame->regions[i].size=0;
                        }
                        frame->regions[i].source_crc=0;
                        frame->regions[i].rect.size.width=0;
                    }
                }


                if(remoteVars.cache_size>CACHEMAXSIZE)
                {
                    clear_cache_data(CACHEMAXSIZE);
                }
                if(remoteVars.cache_size>CACHEMAXELEMENTS)
                {
                    clear_frame_cache(CACHEMAXELEMENTS);
                }

                if(remoteVars.first_deleted_elements)
                {
                    send_deleted_elements();
                }

                if(remoteVars.first_deleted_cursor)
                {
                    send_deleted_cursors();
                }

                pthread_mutex_unlock(&remoteVars.sendqueue_mutex);
                remoteVars.framenum++;
            }
            else
            {
                pthread_mutex_unlock(&remoteVars.sendqueue_mutex);
            }
        }
        pthread_exit(0);
    }
    #warning add some conditions to exit thread properly
    pthread_exit(0);
}

//warning! sendqueue_mutex should be locked by thread calling this function!
void clear_send_queue()
{
    struct sendqueue_element* current=remoteVars.first_sendqueue_element;
    while(current)
    {
        if(current->frame)
        {
            current->frame->busy=0;
            if(current->frame->source)
                current->frame->source->busy--;
            current->frame->source=0;
        }
        struct sendqueue_element* next=current->next;
        free(current);
        current=next;
    }
    remoteVars.first_sendqueue_element=remoteVars.last_sendqueue_element=NULL;
}

//remove elements from cache and release all images if existing.
//warning! sendqueue_mutex should be locked by thread calling this function!
void clear_frame_cache(uint32_t max_elements)
{
//     EPHYR_DBG("cache elements %d, cache size %lu\n",cache_elements, cache_size);
    while(remoteVars.first_cache_element && remoteVars.cache_elements > max_elements)
    {
        //don't delete it now, return to it later;
        if(remoteVars.first_cache_element->busy)
        {
            EPHYR_DBG("%x - is busy (%d), not deleting", remoteVars.first_cache_element->crc, remoteVars.first_cache_element->busy);
            return;
        }
        struct cache_elem* next=remoteVars.first_cache_element->next;
        if(remoteVars.first_cache_element->size)
        {
            free(remoteVars.first_cache_element->data);
            remoteVars.cache_size-=remoteVars.first_cache_element->size;
        }

        if(remoteVars.client_connected)
        {
            //add deleted element to the list for sending
            struct deleted_elem* delem=malloc(sizeof(struct deleted_elem));
            ++remoteVars.deleted_list_size;
            delem->next=0l;
            delem->crc=remoteVars.first_cache_element->crc;
            //         EPHYR_DBG("delete %x",delem->crc);

            if(remoteVars.last_deleted_elements)
            {
                remoteVars.last_deleted_elements->next=delem;
            }
            else
            {
                remoteVars.first_deleted_elements=delem;
            }
            remoteVars.last_deleted_elements=delem;
        }

        if(remoteVars.first_cache_element->source)
            remoteVars.first_cache_element->source->busy--;

        free(remoteVars.first_cache_element);
        remoteVars.cache_elements--;
        remoteVars.first_cache_element=next;
        if(next)
          next->prev=NULL;
    }
    if(!remoteVars.first_cache_element)
        remoteVars.last_cache_element=NULL;
//     EPHYR_DBG("cache elements %d, cache size %d\n", cache_elements, cache_size);
}

//only release images, keep the older frames for crc check
void clear_cache_data(uint32_t maxsize)
{
    struct cache_elem* cur=remoteVars.first_cache_element;
    while(cur && remoteVars.cache_size>maxsize)
    {
        //don't delete it now, return to it later;
        if(cur->busy)
        {
            EPHYR_DBG("%x - busy (%d)", cur->crc, cur->busy);
            return;
        }
        struct cache_elem* next=cur->next;
        if(cur->size)
        {
            free(cur->data);
            remoteVars.cache_size-=cur->size;
            cur->size=0;
            cur->data=0;
        }
        cur=next;
    }
}

char* getAgentStateAsString(int state)
{
    switch(state)
    {
        case STARTING: return "STARTING";
        case RUNNING: return "RUNNING";
        case RESUMING: return "RESUMING";
        case SUSPENDING: return "SUSPENDING";
        case SUSPENDED: return "SUSPENDED";
        case TERMINATING: return "TERMINATING";
        case TERMINATED: return "TERMINATED";
    }
    return 0;
}

void setAgentState(int state)
{
    if(remoteVars.agentState == TERMINATED)
        return;
    if(remoteVars.agentState == TERMINATING && state != TERMINATED)
        return;
    EPHYR_DBG("Agent state %s",getAgentStateAsString(state));
    if(strlen(remoteVars.stateFile))
    {
        FILE* ptr=fopen(remoteVars.stateFile,"wt");
        if(ptr)
        {
            fprintf(ptr,"%s",getAgentStateAsString(state));
            fclose(ptr);
        }
        else
        {
            EPHYR_DBG("CAN'T WRITE STATE TO %s",remoteVars.stateFile);
        }
    }
    remoteVars.agentState=state;
}

void disconnect_client()
{
    EPHYR_DBG("DISCONNECTING CLIENT, DOING SOME CLEAN UP");
    pthread_mutex_lock(&remoteVars.sendqueue_mutex);
    remoteVars.client_connected=FALSE;
    setAgentState(SUSPENDED);
    clear_send_queue();
    clear_frame_cache(0);
    freeCursors();
    pthread_cond_signal(&remoteVars.have_sendqueue_cond);
    pthread_mutex_unlock(&remoteVars.sendqueue_mutex);
}

void
clientReadNotify(int fd, int ready, void *data)
{
    BOOL con;
    pthread_mutex_lock(&remoteVars.sendqueue_mutex);
    con=remoteVars.client_connected;
    pthread_mutex_unlock(&remoteVars.sendqueue_mutex);
    if(!con)
        return;

    //read max 99 events
    int length=read(remoteVars.clientsock,remoteVars.eventBuffer + remoteVars.evBufferOffset, EVLENGTH*99);


    if(length<0)
    {
        EPHYR_DBG("WRONG data - %d\n",length);
        return;
    }
    if(!length)
    {
        EPHYR_DBG("NO DATA, client disconnected\n");
        disconnect_client();
        return;
    }
    //       EPHYR_DBG("Got ev bytes - %d\n",eventnum++);


    length+=remoteVars.evBufferOffset;
    int iterations=length/EVLENGTH;


    for(int i=0;i<iterations;++i)
    {
        char* buff=remoteVars.eventBuffer+i*EVLENGTH;

        if(remoteVars.selstruct.readingInputBuffer)
        {
            int leftToRead=remoteVars.selstruct.inBuffer.size - remoteVars.selstruct.inBuffer.position;
            int chunk=(leftToRead < EVLENGTH)?leftToRead:EVLENGTH;
            memcpy(remoteVars.selstruct.inBuffer.data+remoteVars.selstruct.inBuffer.position, buff, chunk);
            remoteVars.selstruct.inBuffer.position+=chunk;
            if(! (remoteVars.selstruct.inBuffer.position < remoteVars.selstruct.inBuffer.size))
            {
                inputBuffer* selbuff;
                if(remoteVars.selstruct.inBuffer.target==PRIMARY)
                {
                    selbuff=&remoteVars.selstruct.inSelection;
                }
                else
                {
                    selbuff=&remoteVars.selstruct.inClipboard;
                }
                if(selbuff->data)
                    free(selbuff->data);
                selbuff->target=remoteVars.selstruct.inBuffer.target;
                selbuff->data=remoteVars.selstruct.inBuffer.data;
                remoteVars.selstruct.inBuffer.data=NULL;
                selbuff->size=remoteVars.selstruct.inBuffer.size;
                remoteVars.selstruct.inBuffer.size=0;
                selbuff->mimeData=remoteVars.selstruct.inBuffer.mimeData;
                remoteVars.selstruct.readingInputBuffer=FALSE;
                EPHYR_DBG("READY TARGET %d, MIME %d, Read %d from %d",remoteVars.selstruct.inBuffer.target, selbuff->mimeData,
                          remoteVars.selstruct.inBuffer.position, selbuff->size);
                own_selection(selbuff->target);
            }
//             EPHYR_DBG("CHUNK IS DONE %d",remoteVars.selstruct.readingInputBuffer);
        }
        else
        {
            uint32_t event_type=*((uint32_t*)buff);
            switch(event_type)
            {
                case MotionNotify:
                {
                    uint32_t x=*((uint32_t*)buff+1);
                    uint32_t y=*((uint32_t*)buff+2);
                    //                   EPHYR_DBG("HAVE MOTION EVENT %d, %d from client\n",x,y);
                    ephyrClientMouseMotion(x,y);
                    break;
                }
                case ButtonPress:
                case ButtonRelease:
                {
                    uint32_t state=*((uint32_t*)buff+1);
                    uint32_t button=*((uint32_t*)buff+2);
                    //                   EPHYR_DBG("HAVE BUTTON PRESS/RELEASE EVENT %d, %d from client\n",state,button);
                    ephyrClientButton(event_type,state, button);
                    break;
                }
                case KeyPress:
                {
                    uint32_t state=*((uint32_t*)buff+1);
                    uint32_t key=*((uint32_t*)buff+2);
                    /*EPHYR_DBG("HAVE KEY PRESS EVENT state: %d(%x), key: %d(%x) from client\n",state,state, key, key);
                     *
                     *                if (state & ShiftMask)
                     *                {
                     *                    EPHYR_DBG("SHIFT");
                }
                if (state & LockMask)
                {
                EPHYR_DBG("LOCK");
                }
                if (state & ControlMask)
                {
                EPHYR_DBG("CONTROL");
                }
                if (state & Mod1Mask)
                {
                EPHYR_DBG("MOD1");
                }
                if (state & Mod2Mask)
                {
                EPHYR_DBG("MOD2");
                }
                if (state & Mod3Mask)
                {
                EPHYR_DBG("MOD3");
                }
                if (state & Mod4Mask)
                {
                EPHYR_DBG("MOD4");
                }
                if (state & Mod5Mask)
                {
                EPHYR_DBG("MOD5");
                }*/

                    ephyrClientKey(event_type,state, key);
                    break;
                }
                case KeyRelease:
                {
                    uint32_t state=*((uint32_t*)buff+1);
                    uint32_t key=*((uint32_t*)buff+2);
                    /*EPHYR_DBG("HAVE KEY RELEASE EVENT state: %d(%x), key: %d(%x) from client\n",state,state, key, key);
                     *                if (state & ShiftMask)
                     *                {
                     *                    EPHYR_DBG("SHIFT");
                }
                if (state & LockMask)
                {
                EPHYR_DBG("LOCK");
                }
                if (state & ControlMask)
                {
                EPHYR_DBG("CONTROL");
                }
                if (state & Mod1Mask)
                {
                EPHYR_DBG("MOD1");
                }
                if (state & Mod2Mask)
                {
                EPHYR_DBG("MOD2");
                }
                if (state & Mod3Mask)
                {
                EPHYR_DBG("MOD3");
                }
                if (state & Mod4Mask)
                {
                EPHYR_DBG("MOD4");
                }
                if (state & Mod5Mask)
                {
                EPHYR_DBG("MOD5");
                }*/
                    ephyrClientKey(event_type,state, key);
                    break;
                }
                case GEOMETRY:
                {
                    remoteVars.client_initialized=TRUE;
                    uint16_t width=*((uint16_t*)buff+2);
                    uint16_t height=*((uint16_t*)buff+3);
                    uint8_t primaryInd=*((uint8_t*)buff+8);

                    EPHYR_DBG("Client want resize to %dx%d",width,height);

                    struct VirtScreen screens[4];
                    memset(screens,0, sizeof(struct VirtScreen)*4);
                    for(int i=0;i<4;++i)
                    {
                        char* record=buff+9+i*8;
                        screens[i].width=*((uint16_t*)record);
                        screens[i].height=*((uint16_t*)record+1);
                        screens[i].x=*((int16_t*)record+2);
                        screens[i].y=*((int16_t*)record+3);

                        if(!screens[i].width || !screens[i].height)
                        {
                            break;
                        }
                        EPHYR_DBG("SCREEN %d - (%dx%d) - %d,%d", i, screens[i].width, screens[i].height, screens[i].x, screens[i].y);
                    }
                    ephyrResizeScreen (remoteVars.ephyrScreen->pScreen,width,height, screens);
                    break;
                }
                case UPDATE:
                {
                    int32_t width=*((uint32_t*)buff+1);
                    int32_t height=*((uint32_t*)buff+2);

                    int32_t x=*((uint32_t*)buff+3);
                    int32_t y=*((uint32_t*)buff+4);

                    EPHYR_DBG("HAVE UPDATE EVENT from client %dx%d %d,%d\n",width, height, x,y );
                    pthread_mutex_lock(&remoteVars.mainimg_mutex);

                    EPHYR_DBG("DBF: %p, %d, %d",remoteVars.main_img, remoteVars.main_img_width, remoteVars.main_img_height);

                    if(remoteVars.main_img  && x+width <= remoteVars.main_img_width  && y+height <= remoteVars.main_img_height )
                    {
                        pthread_mutex_unlock(&remoteVars.mainimg_mutex);
                        add_frame(width, height, x, y, 0, 0);
                    }
                    else
                    {
                        EPHYR_DBG("UPDATE: skip request");
                        pthread_mutex_unlock(&remoteVars.mainimg_mutex);
                    }
                    break;
                }
                case SELECTIONEVENT:
                {
                    uint32_t size;
                    uint8_t destination, mime;
                    size=*((uint32_t*)buff+1);
                    destination=*((uint8_t*)buff+8);
                    mime=*((uint8_t*)buff+9);
                    EPHYR_DBG("HAVE NEW INCOMING SELECTION: %d %d %d",size, destination, mime);
                    inputBuffer* selbuff;
                    if(destination==CLIPBOARD)
                        selbuff=&(remoteVars.selstruct.inClipboard);
                    else
                        selbuff=&(remoteVars.selstruct.inSelection);
                    if(size < EVLENGTH-10)
                    {
                        if(selbuff->data)
                            free(selbuff->data);
                        selbuff->size=size;
                        selbuff->data=malloc(size);
                        selbuff->target=destination;
                        memcpy(selbuff->data, buff+10, size);
                        selbuff->mimeData=mime;
                        EPHYR_DBG("READY INCOMING SELECTION for %d",destination);
                        own_selection(selbuff->target);

                    }
                    else
                    {
                        selbuff=&(remoteVars.selstruct.inBuffer);
                        if(selbuff->data)
                            free(selbuff->data);
                        selbuff->data=malloc(size);
                        memcpy(selbuff->data, buff+10, EVLENGTH-10);
                        selbuff->mimeData=mime;
                        selbuff->target=destination;
                        selbuff->position=EVLENGTH-10;
                        selbuff->size=size;
                        remoteVars.selstruct.readingInputBuffer=TRUE;
                        EPHYR_DBG("READ INCOMING BUFFER %d from %d",EVLENGTH-10, size);
                    }
                    break;
                }
                default:
                {
                    EPHYR_DBG("UNSUPPORTED EVENT: %d",event_type);
                    //looks like we have some corrupted data, let's try to reset event buffer
                    remoteVars.evBufferOffset=0;
                    length=0;
                    break;
                }
            }
        }
        //           EPHYR_DBG("Processed event - %d %d\n",eventnum++, eventbytes);
    }
    int restDataLength=length%EVLENGTH;
    int restDataPos=length-restDataLength;

    if(restDataLength)
       memcpy(remoteVars.eventBuffer, remoteVars.eventBuffer+restDataPos, restDataLength);
    remoteVars.evBufferOffset=restDataLength;

}

unsigned int checkSocketConnection(OsTimerPtr timer, CARD32 time, void* args)
{
    EPHYR_DBG("CHECKING ACCEPTED CONNECTION");
    TimerFree(timer);
    pthread_mutex_lock(&remoteVars.sendqueue_mutex);
    remoteVars.checkConnectionTimer=0;
    if(!remoteVars.client_connected)
    {
        EPHYR_DBG("NO CLIENT CONNECTION SINCE %d seconds",ACCEPT_TIMEOUT);
        cancelThreadBeforeStart();
    }
    else
    {
        EPHYR_DBG("CLIENT CONNECTED");
    }
    pthread_mutex_unlock(&remoteVars.sendqueue_mutex);
    return 0;
}

void open_socket()
{
    ssize_t size;
    const int y = 1;
    remoteVars.serversock=socket (AF_INET, SOCK_STREAM, 0);
    setsockopt( remoteVars.serversock, SOL_SOCKET, SO_REUSEADDR, &y, sizeof(int));
    remoteVars.address.sin_family = AF_INET;
    remoteVars.address.sin_addr.s_addr = INADDR_ANY;

    if(! strlen(remoteVars.acceptAddr))
        EPHYR_DBG("Accepting connections from 0.0.0.0");
    else
        EPHYR_DBG("Accepting connections from %s", remoteVars.acceptAddr);
    if(!remoteVars.listenPort)
    {
        EPHYR_DBG("Listen on port %d", DEFAULT_PORT);
        remoteVars.address.sin_port = htons (DEFAULT_PORT);
    }
    else
    {
        EPHYR_DBG("Listen on port %d", remoteVars.listenPort);
        remoteVars.address.sin_port = htons (remoteVars.listenPort);
    }
    if (bind ( remoteVars.serversock,
        (struct sockaddr *) &remoteVars.address,
               sizeof (remoteVars.address)) != 0)
    {
        EPHYR_DBG( "PORT IN USE!\n");
        terminateServer(-1);
    }
    listen (remoteVars.serversock, 1);
    remoteVars.addrlen = sizeof (struct sockaddr_in);


    remoteVars.checkConnectionTimer=TimerSet(0,0,ACCEPT_TIMEOUT, checkSocketConnection, NULL);

    int ret = pthread_create(&remoteVars.send_thread_id, NULL, send_frame_thread, (void *)remoteVars.send_thread_id);
    if (ret)
    {
        EPHYR_DBG("ERROR; return code from pthread_create() is %d\n", ret);
        terminateServer(-1);
    }

    EPHYR_DBG("Socket is ready");
}


void terminateServer(int exitStatus)
{
    setAgentState(TERMINATING);
    if(remoteVars.client_connected)
    {
        disconnect_client();
        pthread_join(remoteVars.send_thread_id,NULL);
        remoteVars.send_thread_id=0;
    }
    if(remoteVars.send_thread_id)
    {
        pthread_cancel(remoteVars.send_thread_id);
    }

    pthread_mutex_destroy(&remoteVars.mainimg_mutex);
    pthread_mutex_destroy(&remoteVars.sendqueue_mutex);
    pthread_cond_destroy(&remoteVars.have_sendqueue_cond);

    if(remoteVars.main_img)
    {
        free(remoteVars.main_img);
        free(remoteVars.second_buffer);
    }

    if(remoteVars.selstruct.clipboard.data)
    {
        free(remoteVars.selstruct.clipboard.data);
    }

    if(remoteVars.selstruct.selection.data)
    {
        free(remoteVars.selstruct.selection.data);
    }

    if(remoteVars.selstruct.inClipboard.data)
    {
        free(remoteVars.selstruct.inClipboard.data);
    }
    if(remoteVars.selstruct.inSelection.data)
    {
        free(remoteVars.selstruct.inSelection.data);
    }
    if(remoteVars.selstruct.inBuffer.data)
    {
        free(remoteVars.selstruct.inBuffer.data);
    }
    setAgentState(TERMINATED);
    EPHYR_DBG("exit program with status %d", exitStatus);
    exit(exitStatus);
}

void processConfigFileSetting(char* key, char* value)
{
//     EPHYR_DBG("process setting %s %s", key, value);
    if(!strcmp(key, "state"))
    {
        strncpy(remoteVars.stateFile, value, 255);
        remoteVars.stateFile[255]=0;
        EPHYR_DBG("state file %s", remoteVars.stateFile);
    }
    else if(!strcmp(key, "pack"))
    {
        unsigned char quality=value[strlen(value)-1];
        if(quality>='0'&& quality<='9')
        {
            if(strncmp(value+strlen(value)-5,"png",3) ==0)
            {
                remoteVars.compression=PNG;
                EPHYR_DBG("Using PNG Compression");
            }
            else
            {
                remoteVars.compression=JPEG;
                EPHYR_DBG("Using JPEG Compression");
            }
            remoteVars.jpegQuality=(quality-'0')*10+9;
            EPHYR_DBG("Image quality: %d", remoteVars.jpegQuality);
        }
    }
    else if(!strcmp(key, "accept"))
    {
        strncpy(remoteVars.acceptAddr, value, 255);
        remoteVars.acceptAddr[255]=0;
        EPHYR_DBG("accept %s", remoteVars.acceptAddr);
    }
    else if(!strcmp(key, "cookie"))
    {
        strncpy(remoteVars.cookie, value, 32);
        remoteVars.cookie[32]=0;
    }
    else if(!strcmp(key, "listen"))
    {
        sscanf(value, "%d",&remoteVars.listenPort);
        EPHYR_DBG("listen %d", remoteVars.listenPort);
    }
}

void readOptionsFromFile()
{
    FILE *ptr=fopen(remoteVars.optionsFile,"rt");
    if(ptr)
    {
        char key[255]="";
        char value[255]="";
        BOOL readVal=FALSE;
        int ind=0;
        while(1)
        {
            int c=fgetc(ptr);
            if(c==EOF)
                break;
//             EPHYR_DBG("%c",c);
            if(c=='=')
            {
                key[ind]='\0';
                ind=0;
                readVal=TRUE;
                continue;
            }
            if(c==',' || c==':')
            {
                value[ind]='\0';
                ind=0;
                readVal=FALSE;
                processConfigFileSetting(key, value);
                if(c==':')
                    break;
                continue;
            }
            if(readVal)
                value[ind++]=(unsigned char)c;
            else
                key[ind++]=(unsigned char)c;
            /////read file and get quality and state file
        }
        fclose(ptr);
    }
    else
    {
        EPHYR_DBG("Can't open options file %s", remoteVars.optionsFile);
        terminateServer(-1);
    }
}

int
remote_init(void)
{

    /*int it in os init*/

    fclose(stdout);
    fclose(stdin);

    memset(&remoteVars,0,sizeof(RemoteHostVars));

    remoteVars.jpegQuality=JPG_QUALITY;
    remoteVars.compression=DEFAULT_COMPRESSION;


    pthread_mutex_init(&remoteVars.mainimg_mutex, NULL);
    pthread_mutex_init(&remoteVars.sendqueue_mutex,NULL);
    pthread_cond_init(&remoteVars.have_sendqueue_cond,NULL);


    char *displayVar=secure_getenv("DISPLAY");


    if(displayVar)
    {
        if(!strncmp(displayVar,"nx/nx,options=",strlen("nx/nx,options=")))
        {
            EPHYR_DBG("running in NXAGENT MODE");
            remoteVars.nxagentMode=TRUE;
            int i=strlen("nx/nx,options=");
            int j=0;
            while(displayVar[i]!=':' && i<strlen(displayVar) && j<254)
            {
                remoteVars.optionsFile[j++]=displayVar[i++];
            }
            remoteVars.optionsFile[j]='\0';
            EPHYR_DBG("read options from %s",remoteVars.optionsFile);
            readOptionsFromFile();
        }
    }

    remoteInitialized=TRUE;
    setAgentState(STARTING);
    open_socket();
    return 1;
}

static void PngWriteCallback(png_structp  png_ptr, png_bytep data, png_size_t length)
{
    struct png_data
    {
        uint32_t* size;
        unsigned char *out;
    }*outdata = (struct png_data*)png_get_io_ptr(png_ptr);

    uint32_t newLength=*(outdata->size)+length;
    outdata->out=realloc(outdata->out, newLength);

    memcpy(outdata->out+*(outdata->size),data,length);
    *(outdata->size)=newLength;
}

unsigned char* png_compress( uint32_t image_width, uint32_t image_height,
                            unsigned char* RGBA_buffer, uint32_t* png_size)
{
    png_structp p = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

    png_infop info_ptr = png_create_info_struct(p);
    setjmp(png_jmpbuf(p));
    png_set_IHDR(p, info_ptr,image_width, image_height, 8,
                         PNG_COLOR_TYPE_RGB,
                         PNG_INTERLACE_NONE,
                         PNG_COMPRESSION_TYPE_DEFAULT,
                         PNG_FILTER_TYPE_DEFAULT);



    *png_size=0;


    struct
    {
        uint32_t* size;
        unsigned char *out;
    }outdata;

    outdata.size=png_size;
    outdata.out=0;

    unsigned char** rows=calloc(sizeof(unsigned char*),image_height);
    for (uint32_t y = 0; y < image_height; ++y)
        rows[y] = (unsigned char*)RGBA_buffer + y * image_width * CACHEBPP;


    png_set_rows(p, info_ptr, &rows[0]);
    png_set_write_fn(p, &outdata, PngWriteCallback, NULL);
    png_write_png(p, info_ptr, PNG_TRANSFORM_BGR, NULL);

    png_destroy_info_struct(p, &info_ptr );
    png_destroy_write_struct(&p, NULL);
    free(rows);
    return outdata.out;
}


unsigned char* jpeg_compress (int quality, uint32_t image_width, uint32_t image_height,
                      unsigned char* RAW_buffer, uint32_t* jpeg_size, int bpp, char* fname)
{
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    JSAMPROW row_pointer[1];/* pointer to JSAMPLE row[s] */
    int row_stride;/* physical row width in image buffer */
    unsigned char* out_bufer=0;
    long unsigned int length=0;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);


    jpeg_mem_dest(&cinfo,&out_bufer, &length);

    cinfo.image_width = image_width;         /* image width and height, in pixels */
    cinfo.image_height = image_height;
    cinfo.input_components = bpp;            /* # of color components per pixel */
    if(bpp == 4)
        cinfo.in_color_space = JCS_EXT_BGRX;     /* colorspace of input image */
    else
        cinfo.in_color_space = JCS_EXT_BGR;     /* colorspace of input image */
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE); /* limit to baseline-JPEG values */

    jpeg_start_compress(&cinfo, TRUE);
    row_stride = image_width * bpp;            /* JSAMPLEs per row in image_buffer */

    while (cinfo.next_scanline < cinfo.image_height)
    {
        row_pointer[0] = & RAW_buffer[cinfo.next_scanline * row_stride];
        (void) jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }

    jpeg_finish_compress(&cinfo);

    *jpeg_size=length;
    jpeg_destroy_compress(&cinfo);



    if(fname && JPGDEBUG)
    {
        FILE * outfile;

        if ((outfile = fopen(fname, "wb")) == NULL)
        {
//             fprintf(stderr, "can't open %s\n", fname);
        }
        else
        {
            fwrite(out_bufer, 1, length, outfile);
            fclose(outfile);
        }
    }
    return out_bufer;
}


unsigned char* image_compress(uint32_t image_width, uint32_t image_height,
                              unsigned char* RGBA_buffer, uint32_t* compressed_size, int bpp, char* fname)
{
    if(remoteVars.compression==JPEG)
       return jpeg_compress(remoteVars.jpegQuality, image_width, image_height, RGBA_buffer, compressed_size, bpp, fname);
    else
       return png_compress(image_width, image_height, RGBA_buffer, compressed_size);
}



struct cache_elem* add_cache_element(uint32_t crc, int32_t dx, int32_t dy, uint32_t size, uint32_t width, uint32_t height)
{
    struct cache_elem* el=malloc(sizeof(struct cache_elem));
    el->next=0;
    el->crc=crc;
    el->sent=FALSE;
    el->busy=0;
    el->rval=el->bval=el->gval=0;
    el->width=width;
    el->height=height;
    el->source=0;


    for(int i=0;i<9;++i)
    {
        el->regions[i].compressed_data=0;
        el->regions[i].size=0;
        el->regions[i].source_crc=0;
        el->regions[i].rect.size.width=0;
    }

/*    if(CACHEBPP==4)
    {
        el->size=size;
        el->data=malloc(size);
        remoteVars.cache_size+=size;
        memcpy(el->data, data, size);
    }
    else*/
    {

        uint32_t isize=size/4*3;
        el->size=isize;
//         EPHYR_DBG("SIZE %d %d %d, %d, %d",isize, dx, dy, width, height);
        el->data=malloc(isize);
        if(!el->data)
        {
            EPHYR_DBG("error allocating data for frame");
            exit(-1);
        }
        remoteVars.cache_size+=isize;

        //copy RGB channels of every pixel and ignore A channel

        uint32_t numOfPix=size/4;
        uint32_t i=0;

        pthread_mutex_lock(&remoteVars.mainimg_mutex);



        for(int32_t y=0;y<height;++y)
        {
            for(int32_t x=0; x< width; ++x)
            {
                uint32_t ind=((y+dy)*remoteVars.main_img_width+dx+x)*XSERVERBPP;
                memcpy(el->data+i*CACHEBPP, remoteVars.main_img+ind, CACHEBPP);
                el->rval+= (unsigned char)(*(remoteVars.main_img+ind));
                el->gval+= (unsigned char)(*(remoteVars.main_img+ind+1));
                el->bval+= (unsigned char)(*(remoteVars.main_img+ind+2));
                ++i;
            }
        }
        pthread_mutex_unlock(&remoteVars.mainimg_mutex);

        el->rval/=numOfPix;
        el->gval/=numOfPix;
        el->bval/=numOfPix;
    }


    remoteVars.cache_elements++;
    el->prev=remoteVars.last_cache_element;
//     EPHYR_DBG("\ncache elements %d, cache size %u(%dMB) %u, %u, %u\n", cache_elements, cache_size, (int) (cache_size/1024/1024), el->rval,
//               el->gval, el->bval);
    if(remoteVars.last_cache_element)
    {
        remoteVars.last_cache_element->next=el;
    }
    else
    {
        remoteVars.first_cache_element=el;
    }
    remoteVars.last_cache_element=el;
    return el;
}



//this function looking for the cache elements with specified crc
//if the element is found we moving it in the end of list
//in this case we keeping the most recent elements in the tail of list for faster search
struct cache_elem* find_cache_element(uint32_t crc)
{
    struct cache_elem* current=remoteVars.last_cache_element;
    while(current)
    {
        if(current->crc==crc)
        {
            if(current==remoteVars.last_cache_element)
            {
                return current;
            }
            if(current==remoteVars.first_cache_element)
            {
                remoteVars.first_cache_element=current->next;
                remoteVars.first_cache_element->prev=0;
            }
            else
            {
                current->prev->next=current->next;
                current->next->prev=current->prev;
            }
            current->prev=remoteVars.last_cache_element;
            remoteVars.last_cache_element->next=current;
            current->next=0l;
            remoteVars.last_cache_element=current;
            return current;
        }
        current=current->prev;
    }
    return 0;
}



void initFrameRegions(struct cache_elem* frame)
{
    BOOL haveMultplyRegions=FALSE;
    int32_t length=0;

    struct frame_region* regions=frame->regions;
    BOOL diff;


    if(frame->width>4 && frame->height>4 && frame->width * frame->height > 100 )
    {
        unsigned int match_val=0;
        pthread_mutex_lock(&remoteVars.sendqueue_mutex);
        struct cache_elem* best_match = find_best_match(frame, &match_val);
        if(best_match)
        {
            best_match->busy+=1;
        }
        pthread_mutex_unlock(&remoteVars.sendqueue_mutex);

        uint32_t bestm_crc=0;
        if(best_match && best_match->width>4 && best_match->height>4 && best_match->width * best_match->height > 100 )
        {
            bestm_crc=best_match->crc;
            if(best_match && match_val<=MAX_MATCH_VAL)
            {

                clock_t t = clock();
                rectangle rect;
                int hshift, vshift;
                if(find_common_regions(best_match, frame, &diff, &rect, &hshift, &vshift))
                {
                    haveMultplyRegions=TRUE;
                    if(!diff)
                    {

/*                        EPHYR_DBG("SOURCE: %x %d:%d - %dx%d- shift %d, %d",bestm_crc,
                                  rect.lt_corner.x, rect.lt_corner.y,
                                  rect.size.width, rect.size.height, hshift, vshift);
*/
                        rectangle* base=&(regions[8].rect);
                        base->size.width=rect.size.width;
                        base->size.height=rect.size.height;

                        base->lt_corner.x=rect.lt_corner.x+hshift;
                        base->lt_corner.y=rect.lt_corner.y+vshift;


                        //regions[8] represents common with bestmatch region
                        regions[8].source_coordinates.x=rect.lt_corner.x;
                        regions[8].source_coordinates.y=rect.lt_corner.y;
                        regions[8].source_crc=bestm_crc;
                        frame->source=best_match;



                        regions[0].rect.lt_corner.x=regions[3].rect.lt_corner.x=
                        regions[5].rect.lt_corner.x=0;

                        regions[0].rect.size.width=regions[3].rect.size.width=
                        regions[5].rect.size.width=base->lt_corner.x;

                        regions[2].rect.lt_corner.x=regions[4].rect.lt_corner.x=
                        regions[7].rect.lt_corner.x=base->lt_corner.x+base->size.width;

                        regions[2].rect.size.width=regions[4].rect.size.width=
                        regions[7].rect.size.width=frame->width-regions[7].rect.lt_corner.x;

                        regions[0].rect.lt_corner.y=regions[1].rect.lt_corner.y=
                        regions[2].rect.lt_corner.y=0;

                        regions[0].rect.size.height=regions[1].rect.size.height=
                        regions[2].rect.size.height=base->lt_corner.y;

                        regions[5].rect.lt_corner.y=regions[6].rect.lt_corner.y=
                        regions[7].rect.lt_corner.y=base->lt_corner.y+base->size.height;

                        regions[5].rect.size.height=regions[6].rect.size.height=
                        regions[7].rect.size.height=frame->height-regions[7].rect.lt_corner.y;


                        regions[1].rect.lt_corner.x=regions[6].rect.lt_corner.x=base->lt_corner.x;

                        regions[1].rect.size.width=regions[6].rect.size.width=base->size.width;

                        regions[3].rect.lt_corner.y=regions[4].rect.lt_corner.y=base->lt_corner.y;
                        regions[3].rect.size.height=regions[4].rect.size.height=base->size.height;


                        int prev=-1;
                        for(int i=0;i<8;++i)
                        {
                            if(regions[i].rect.size.width && regions[i].rect.size.height)
                            {


                                if((regions[i].rect.lt_corner.y == regions[prev].rect.lt_corner.y)
                                    && (regions[prev].rect.lt_corner.x+regions[prev].rect.size.width == regions[i].rect.lt_corner.x)&&prev>0)
                                {
                                    //EPHYR_DBG("Unite %d and %d", prev,i);
                                    regions[prev].rect.size.width+=regions[i].rect.size.width;
                                    regions[i].rect.size.width=0;
                                }
                                else
                                {
                                    prev=i;
                                }
                            }
                        }
                    }
                    else
                    {
                        rectangle* base=&(regions[0].rect);
                        base->size.width=frame->width;
                        base->size.height=frame->height;

                        base->lt_corner.x=0;
                        base->lt_corner.y=0;

                        //regions[0] represents common with bestmatch region
                        regions[0].source_coordinates.x=0;
                        regions[0].source_coordinates.y=0;
                        regions[0].source_crc=bestm_crc;
                        frame->source=best_match;
                        memcpy(&(regions[1].rect), &rect, sizeof(rectangle));
                    }
                }
            }
        }
        //if we didn't find any common regions and have best match element, mark it as not busy
        pthread_mutex_lock(&remoteVars.sendqueue_mutex);
        if(best_match && frame->source != best_match)
        {
//             EPHYR_DBG("Have best mutch but not common region");
            best_match->busy-=1;
        }
        pthread_mutex_unlock(&remoteVars.sendqueue_mutex);

    }

    if(!haveMultplyRegions)
    {
        //EPHYR_DBG("HAVE SINGLE REGION");

        regions[0].compressed_data=image_compress(frame->width, frame->height,
                                                 frame->data, &(regions[0].size), CACHEBPP, 0);
        length=regions[0].size;
        regions[0].rect.size.width=frame->width;
        regions[0].rect.size.height=frame->height;
        regions[0].rect.lt_corner.x=regions[0].rect.lt_corner.y=0;
    }
    else
    {
        if(!diff)
        {
            for (int i=0;i<8;++i)
            {
                if(regions[i].rect.size.width && regions[i].rect.size.height)
                {

                    /*
                    uint32_t send_data=regions[i].rect.size.width*regions[i].rect.size.height;
                    uint32_t total_data=frame->width*frame->height;
                    EPHYR_DBG("saved space: %d%% %d %d %dx%d %dx%d", 100 - (int)((send_data*1./total_data)*100), total_data, send_data,frame->width, frame->height,
                              regions[i].rect.size.width,regions[i].rect.size.height);
                    */

                    #warning check this
                    uint8_t *data=malloc(regions[i].rect.size.width*regions[i].rect.size.height*CACHEBPP);
                    for(int line=0;line<regions[i].rect.size.height;++line)
                    {
                        memcpy(data+line*regions[i].rect.size.width*CACHEBPP,
                               frame->data+((regions[i].rect.lt_corner.y+line)*
                               frame->width+regions[i].rect.lt_corner.x)*CACHEBPP,
                               regions[i].rect.size.width*CACHEBPP);
                    }

                    regions[i].compressed_data=image_compress(regions[i].rect.size.width,
                                                             regions[i].rect.size.height,
                                                             data, &regions[i].size, CACHEBPP, 0);
                    length+=regions[i].size;
                    free(data);
                }
            }
        }
        else
        {
            #warning check this
            uint8_t *data=malloc(regions[1].rect.size.width*regions[1].rect.size.height*CACHEBPP);
            for(int line=0;line<regions[1].rect.size.height;++line)
            {
                memcpy(data+line*regions[1].rect.size.width*CACHEBPP,
                       frame->data+((regions[1].rect.lt_corner.y+line)*
                       frame->width+regions[1].rect.lt_corner.x)*CACHEBPP,
                       regions[1].rect.size.width*CACHEBPP);

            }
            char fname[255];
            sprintf(fname,"/tmp/ephyrdbg/%x-rect_inv.jpg",frame->crc);
            regions[1].compressed_data=image_compress(regions[1].rect.size.width,
                                                     regions[1].rect.size.height,
                                                     data, &regions[1].size, CACHEBPP, fname);
            length+=regions[1].size;
            free(data);
        }
    }
    frame->compressed_size=length;
}

void add_frame(uint32_t width, uint32_t height, int32_t x, int32_t y, uint32_t crc, uint32_t size)
{

    pthread_mutex_lock(&remoteVars.sendqueue_mutex);
    if(! (remoteVars.client_connected && remoteVars.client_initialized))
    {
        //don't have any clients connected, return
        pthread_mutex_unlock(&remoteVars.sendqueue_mutex);
        return;
    }


    Bool isNewElement=FALSE;

    struct cache_elem* frame=0;
    if(crc==0)
    {
        //sending main image
        pthread_mutex_unlock(&remoteVars.sendqueue_mutex);
    }
    else
    {

        frame=find_cache_element(crc);
        if(!frame)
        {
//             EPHYR_DBG("ADD NEW FRAME %x",crc);
            frame=add_cache_element(crc, x, y, size, width, height);
            isNewElement=TRUE;
        }
        else
        {
            //         EPHYR_DBG("ADD EXISTING FRAME %x",crc);
        }
        frame->busy+=1;

        pthread_mutex_unlock(&remoteVars.sendqueue_mutex);

        //if element is new find common regions and compress the data
        if(isNewElement)
        {
            //find bestmatch and lock it
            initFrameRegions(frame);
        }
    }

    pthread_mutex_lock(&remoteVars.sendqueue_mutex);
    //add element in the queue for sending
    struct sendqueue_element* element=malloc(sizeof(struct sendqueue_element));
    element->frame=frame;
    element->next=NULL;
    element->x=x;
    element->y=y;
    element->width=width;
    element->height=height;
    if(remoteVars.last_sendqueue_element)
    {
        remoteVars.last_sendqueue_element->next=element;
        remoteVars.last_sendqueue_element=element;
    }
    else
    {
        remoteVars.first_sendqueue_element=remoteVars.last_sendqueue_element=element;
    }
    pthread_cond_signal(&remoteVars.have_sendqueue_cond);
    pthread_mutex_unlock(&remoteVars.sendqueue_mutex);

    //on this point will be sent wakeup single to send mutex
}


void remote_send_main_image()
{
    add_frame(0, 0, 0, 0, 0, 0);
}

void
remote_paint_rect(KdScreenInfo *screen,
                  int sx, int sy, int dx, int dy, int width, int height)
{

    //     EphyrScrPriv *scrpriv = screen->driver;


    uint32_t size=width*height*XSERVERBPP;

    if(size)
    {
        //EPHYR_DBG("REPAINT %dx%d sx %d, sy %d, dx %d, dy %d", width, height, sx, sy, dx, dy);
        int32_t dirtyx_max=dx-1;
        int32_t dirtyy_max=dy-1;

        int32_t dirtyx_min=dx+width;
        int32_t dirtyy_min=dy+height;

        //OK, here we assuming that XSERVERBPP is 4. If not, we'll have troubles
        //but it should work faster like this

        pthread_mutex_lock(&remoteVars.mainimg_mutex);
        char maxdiff=2;
        char mindiff=-2;



        // check if updated rec really is as big
        for(int32_t y=dy; y< dy+height;++y)
        {
            uint32_t ind=(y*remoteVars.main_img_width+dx)*XSERVERBPP;
            for(int32_t x=dx;x< dx+width; ++x)
            {
                BOOL pixIsDirty=FALSE;
                //CHECK R-COMPONENT
                int16_t diff=remoteVars.main_img[ind]-remoteVars.second_buffer[ind];
                /*if(x > 250 && x<255 && y >5 && y< 7)
                 *                {
                 *                    EPHYR_DBG("rdiff %d - %u %u ", diff, remoteVars.main_img[ind],remoteVars.second_buffer[ind]);
            }*/
                if(diff>maxdiff || diff< mindiff)
                {
                    pixIsDirty=TRUE;
                }
                else
                {
                    //CHECK G_COMPONENT
                    diff=remoteVars.main_img[ind+1]-remoteVars.second_buffer[ind+1];
                    if(diff>maxdiff || diff< mindiff)
                    {
                        pixIsDirty=TRUE;
                    }
                    else
                    {
                        //CHECK B_COMPONENT
                        diff=remoteVars.main_img[ind+2]-remoteVars.second_buffer[ind+2];
                        if(diff>maxdiff || diff< mindiff)
                        {
                            pixIsDirty=TRUE;
                        }
                    }
                }
                if(pixIsDirty)
                {
                    if(x>dirtyx_max)
                    {
                        dirtyx_max=x;
                    }
                    if(y>dirtyy_max)
                    {
                        dirtyy_max=y;
                    }
                    if(x<dirtyx_min)
                    {
                        dirtyx_min=x;
                    }
                    if(y<dirtyy_min)
                    {
                        dirtyy_min=y;
                    }
                    //copy only RGB part (A always same)
                    memcpy(remoteVars.second_buffer+ind, remoteVars.main_img+ind,3);
                }
                ind+=XSERVERBPP;
            }
        }
        pthread_mutex_unlock(&remoteVars.mainimg_mutex);


        //         EPHYR_DBG("DIRTY %d,%d - %d,%d", dirtyx_min, dirtyy_min, dirtyx_max, dirtyy_max);


        width=dirtyx_max-dirtyx_min+1;
        height=dirtyy_max-dirtyy_min+1;
        int oldsize=size;
        size=width*height*XSERVERBPP;
        if(width<=0 || height<=0||size<=0)
        {
            // EPHYR_DBG("NO CHANGES DETECTED, NOT UPDATING");
            return;
        }
        dx=sx=dirtyx_min;
        dy=sy=dirtyy_min;
        /*
         *        if(size!=oldsize)
         *        {
         *            EPHYR_DBG("new update rect demensions: %dx%d", width, height);
    }*/

        add_frame(width, height, dx, dy, calculate_crc(width, height,dx,dy), size);
    }
}



uint32_t calculate_crc(uint32_t width, uint32_t height, int32_t dx, int32_t dy)
{
    uint32_t crc=adler32(0L, Z_NULL, 0);

    pthread_mutex_lock(&remoteVars.mainimg_mutex);
    for(uint32_t y=0; y< height;++y)
    {
        crc=adler32(crc,remoteVars.main_img+ ((y+dy)*remoteVars.main_img_width + dx)*XSERVERBPP, width*XSERVERBPP);
    }
    pthread_mutex_unlock(&remoteVars.mainimg_mutex);
    return crc;
}

void *
remote_screen_init(KdScreenInfo *screen,
                  int x, int y,
                  int width, int height, int buffer_height,
                  int *bytes_per_line, int *bits_per_pixel)
{

    //We should not install callback at first screen init, it was installed by selection_init
    //but we need to reinstall it by the next screen init.

    EPHYR_DBG("REMOTE SCREN INIT!!!!!!!!!!!!!!!!!!");
    if(remoteVars.selstruct.callBackInstalled)
    {
        EPHYR_DBG("SKIPPING CALLBACK INSTALL");
        remoteVars.selstruct.callBackInstalled=FALSE;
    }
    else
    {
        EPHYR_DBG("REINSTALL CALLBACKS");
        install_selection_callbacks();
    }

    EphyrScrPriv *scrpriv = screen->driver;
    EPHYR_DBG("host_screen=%p x=%d, y=%d, wxh=%dx%d, buffer_height=%d",
              screen, x, y, width, height, buffer_height);

    pthread_mutex_lock(&remoteVars.mainimg_mutex);


    if(remoteVars.main_img)
    {
        free(remoteVars.main_img);

        EPHYR_DBG("FREE DBF");
        free(remoteVars.second_buffer);
        EPHYR_DBG("DONE FREE DBF");
    }
    EPHYR_DBG("TRYING TO ALLOC  MAIN_IMG %d", width*height*XSERVERBPP);
    remoteVars.main_img=malloc(width*height*XSERVERBPP);
    EPHYR_DBG("TRYING TO ALLOC  DOUBLE BUF %d", width*height*XSERVERBPP);
    remoteVars.second_buffer=malloc(width*height*XSERVERBPP);

    memset(remoteVars.main_img,0, width*height*XSERVERBPP);
    memset(remoteVars.second_buffer,0, width*height*XSERVERBPP);
    EPHYR_DBG("ALL INITIALIZED");

    if(!remoteVars.main_img)
    {
        EPHYR_DBG("failed to init main buf");
        exit(-1);
    }
    if(!remoteVars.second_buffer)
    {
        EPHYR_DBG("failed to init second buf");
        exit(-1);
    }

    remoteVars.main_img_width=width;
    remoteVars.main_img_height=height;


    scrpriv->win_width = width;
    scrpriv->win_height = height;
    scrpriv->win_x = x;
    scrpriv->win_y = y;
    scrpriv->img=remoteVars.main_img;

    remoteVars.ephyrScreen=screen;

    *bytes_per_line = width*XSERVERBPP;
    *bits_per_pixel = 32;
    pthread_mutex_unlock(&remoteVars.mainimg_mutex);

    return remoteVars.main_img;
}
