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
#include "x2gokdriveremote.h"
#include "x2gokdriveselection.h"
#include "x2gokdrivelog.h"
#include "inputstr.h"
#include <zlib.h>
#include <propertyst.h>

#ifdef EPHYR_WANT_DEBUG
extern unsigned long long int debug_sendThreadId;
extern unsigned long long int debug_selectThreadId;
#endif /* EPHYR_WANT_DEBUG */

typedef struct {
    int32_t flags;     /* marks which fields in this structure are defined */
    Bool input;     /* does this application rely on the window manager to
    get keyboard input? */
    int initial_state;      /* see below */
    Pixmap icon_pixmap;     /* pixmap to be used as icon */
    Window icon_window;     /* window to be used as icon */
    int icon_x, icon_y;     /* initial position of icon */
    Pixmap icon_mask;       /* icon mask bitmap */
    XID window_group;       /* id of related window group */
    /* this structure may be extended in the future */
} ExWMHints;
#define ExInputHint               (1L << 0)
#define ExStateHint               (1L << 1)
#define ExIconPixmapHint          (1L << 2)
#define ExIconWindowHint          (1L << 3)
#define ExIconPositionHint        (1L << 4)
#define ExIconMaskHint            (1L << 5)
#define ExWindowGroupHint         (1L << 6)
#define ExXUrgencyHint            (1L << 8)

/* Size hints mask bits */

#define ExUSPosition	(1L << 0)	/* user specified x, y */
#define ExUSSize		(1L << 1)	/* user specified width, height */
#define ExPPosition	(1L << 2)	/* program specified position */
#define ExPSize		(1L << 3)	/* program specified size */
#define ExPMinSize	(1L << 4)	/* program specified minimum size */
#define ExPMaxSize	(1L << 5)	/* program specified maximum size */
#define ExPResizeInc	(1L << 6)	/* program specified resize increments */
#define ExPAspect		(1L << 7)	/* program specified min and max aspect ratios */
#define ExPBaseSize	(1L << 8)
#define ExPWinGravity	(1L << 9)

/* Values */

typedef struct {
    int32_t flags;		/* marks which fields in this structure are defined */
    int x, y;		/* Obsolete */
    int width, height;	/* Obsolete */
    int min_width, min_height;
    int max_width, max_height;
    int width_inc, height_inc;
    struct {
        int x;		/* numerator */
        int y;		/* denominator */
    } min_aspect, max_aspect;
    int base_width, base_height;
    int win_gravity;
    /* this structure may be extended in the future */
} ExSizeHints;


/* init it in OsInit() */
static struct _remoteHostVars remoteVars = {0};
// struct _remoteHostVars RemoteHostVars;

static BOOL remoteInitialized=FALSE;


void remote_selection_init(void)
{
    remoteVars.selstruct.readingInputBuffer=-1;
    remoteVars.selstruct.currentInputBuffer=CLIPBOARD;
    selection_init(&remoteVars);
}


void restartTimerOnInit(void)
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


static
void cancelBeforeStart(void)
{
    EPHYR_DBG("Closing server connection");
    close_server_socket();
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
                cancelBeforeStart();
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

static
int queue_elements(void)
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

static
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

static
void addSentCursor(uint32_t serialNumber)
{
//    #warning check memory
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

static
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

static
void freeCursors(void)
{
    struct sentCursor* cur = NULL;
    struct cursorFrame* curf = NULL;

    cur=remoteVars.sentCursorsHead;
    while(cur)
    {
        struct sentCursor* next=cur->next;
        free(cur);
        cur=next;
    }

    curf=remoteVars.firstCursor;
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
    struct sentCursor* cur = NULL;
    struct sentCursor* prev = NULL;
    struct deletedCursor* dcur = NULL;


    pthread_mutex_lock(&remoteVars.sendqueue_mutex);
    cur=remoteVars.sentCursorsHead;

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
    dcur=malloc(sizeof(struct deletedCursor));
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
    BOOL cursorSent=FALSE;
//    #warning check memory
    struct cursorFrame* cframe=malloc(sizeof(struct cursorFrame));
    bzero(cframe, sizeof(struct cursorFrame));

    cframe->serialNumber=cursor->serialNumber;


    pthread_mutex_lock(&remoteVars.sendqueue_mutex);
    cursorSent=isCursorSent(cursor->serialNumber);

    pthread_mutex_unlock(&remoteVars.sendqueue_mutex);
    if(!cursorSent)
    {
        if(cursor->bits->argb)
        {
            if(remoteVars.client_os == WEB)
            {
                //for web client we need to convert cursor data to PNG format
                cframe->data=(char*)png_compress( cursor->bits->width, cursor->bits->height,
                                                  (unsigned char *)cursor->bits->argb, &cframe->size, TRUE);
            }
            else
            {
                cframe->size=cursor->bits->width*cursor->bits->height*4;
                cframe->data=malloc(cframe->size);
                memcpy(cframe->data, cursor->bits->argb, cframe->size);
            }
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

        /* In X11 implementation we have 2bits for color, not from 0 to 255 but from 0 to 65535.
         * no idea why, RGBA is still 4bytes. I think one byte per color component is suuficient,
         * so let's just recalculate to 1byte per component
         */

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


void remote_sendVersion(void)
{
    unsigned char buffer[56] = {0};
    _X_UNUSED int l;


    *((uint32_t*)buffer)=SERVERVERSION; //4B
    *((uint16_t*)buffer+2)=FEATURE_VERSION;
    EPHYR_DBG("Sending server version: %d", FEATURE_VERSION);
    l=write(remoteVars.clientsock,buffer,56);
    remoteVars.server_version_sent=TRUE;
}

void request_selection_from_client(enum SelectionType selection)
{
    unsigned char buffer[56] = {0};
    _X_UNUSED int l;

    *((uint32_t*)buffer)=DEMANDCLIENTSELECTION; //4B
    *((uint16_t*)buffer+2)=(uint16_t)selection;
    l=write(remoteVars.clientsock,buffer,56);
    EPHYR_DBG("requesting selection from client");
}

static
int32_t send_cursor(struct cursorFrame* cursor)
{
    unsigned char buffer[64] = {0};
    _X_UNUSED int ln = 0;
    int l = 0;
    int sent = 0;

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

//    #warning check this
    ln=write(remoteVars.clientsock,buffer,56);

    while(sent<cursor->size)
    {
        l=write(remoteVars.clientsock, cursor->data+sent,((cursor->size-sent)<MAXMSGSIZE)?(cursor->size-sent):MAXMSGSIZE);
        if(l<0)
        {
            EPHYR_DBG("Error sending cursor!!!!!");
            break;
        }
        sent+=l;
    }
    remoteVars.data_sent+=sent;
//    EPHYR_DBG("SENT total %d", total);

    return sent;
}

static
int32_t send_frame(u_int32_t width, uint32_t height, uint32_t x, uint32_t y, uint32_t crc, struct frame_region* regions, uint32_t winId)
{
    unsigned char buffer[64] = {0};
    _X_UNUSED int ln = 0;
    int l = 0;
    int sent = 0;

    uint32_t total=0;
    uint32_t numofregions=0;

    for(int i=0;i<9;++i)
    {
        if(regions[i].rect.size.width && regions[i].rect.size.height)
            ++numofregions;
    }
    *((uint32_t*)buffer)=FRAME;
    *((uint32_t*)buffer+1)=width;
    *((uint32_t*)buffer+2)=height;
    *((uint32_t*)buffer+3)=x;
    *((uint32_t*)buffer+4)=y;
    *((uint32_t*)buffer+5)=numofregions;
    *((uint32_t*)buffer+6)=crc;
    if(remoteVars.rootless)
    {
        *((uint32_t*)buffer+7)=winId;
        /*if(winId)
        {
            EPHYR_DBG("Sending frame for Window 0x%X",winId);
        }*/
    }


//    if(numofregions)
//        EPHYR_DBG("SENDING NEW FRAME %x", crc);
//    else
//        EPHYR_DBG("SENDING REFERENCE %x", crc);

//    #warning check this
    ln=write(remoteVars.clientsock, buffer,56);
    for(int i=0;i<9;++i)
    {
        if(!(regions[i].rect.size.width && regions[i].rect.size.height))
            continue;

//        EPHYR_DBG("SENDING FRAME REGION %x %dx%d %d",regions[i].source_crc, regions[i].rect.size.width, regions[i].rect.size.height,
//                  regions[i].size);

        *((uint32_t*)buffer)=regions[i].source_crc;

//      if(*((uint32_t*)buffer)=regions[i].source_crc)
//          EPHYR_DBG("SENDING REFERENCE %x", *((uint32_t*)buffer)=regions[i].source_crc);

        *((uint32_t*)buffer+1)=regions[i].source_coordinates.x;
        *((uint32_t*)buffer+2)=regions[i].source_coordinates.y;
        *((uint32_t*)buffer+3)=regions[i].rect.lt_corner.x;
        *((uint32_t*)buffer+4)=regions[i].rect.lt_corner.y;
        *((uint32_t*)buffer+5)=regions[i].rect.size.width;
        *((uint32_t*)buffer+6)=regions[i].rect.size.height;
        *((uint32_t*)buffer+7)=regions[i].size;

        sent = 0;
//        #warning check this
        ln=write(remoteVars.clientsock, buffer, 64);

        while(sent<regions[i].size)
        {
            l=write(remoteVars.clientsock,regions[i].compressed_data+sent,
                    ((regions[i].size-sent)<MAXMSGSIZE)?(regions[i].size-sent):MAXMSGSIZE);
            if(l<0)
            {
                EPHYR_DBG("Error sending file!!!!!");
                break;
            }
            sent+=l;
        }
        total+=sent;
//        EPHYR_DBG("SENT %d",sent);
//
//        EPHYR_DBG("\ncache elements %d, cache size %lu(%dMB), connection time=%d, sent %lu(%dMB)\n",
//                  cache_elements, cache_size, (int) (cache_size/1024/1024),
//                  time(NULL)-con_start_time, data_sent, (int) (data_sent/1024/1024));
//

    }
    remoteVars.data_sent+=total;

//     EPHYR_DBG("SENT total %d", total);

    return total;
}

static
int send_deleted_elements(void)
{
    unsigned char buffer[56] = {0};
    unsigned char* list = NULL;

    _X_UNUSED int ln = 0;
    int l = 0;
    int length, sent = 0;

    unsigned int i = 0;
    struct deleted_elem* elem = NULL;

    *((uint32_t*)buffer)=DELETED;
    *((uint32_t*)buffer+1)=remoteVars.deleted_list_size;

    list=malloc(sizeof(uint32_t)*remoteVars.deleted_list_size);

//    #warning check this
    ln=write(remoteVars.clientsock,buffer,56);
//     data_sent+=48;
    while(remoteVars.first_deleted_elements)
    {
//        EPHYR_DBG("To DELETE FRAME %x", remoteVars.first_deleted_elements->crc);

        *((uint32_t*)list+i)=remoteVars.first_deleted_elements->crc;
        elem=remoteVars.first_deleted_elements;
        remoteVars.first_deleted_elements=elem->next;
        free(elem);
        ++i;
    }

    remoteVars.last_deleted_elements=0l;

//    EPHYR_DBG("SENDING IMG length - %d, number - %d\n",length,framenum_sent++);
    length=remoteVars.deleted_list_size*sizeof(uint32_t);
    while(sent<length)
    {
        l=write(remoteVars.clientsock,list+sent,((length-sent)<MAXMSGSIZE)?(length-sent):MAXMSGSIZE);
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

static
int send_deleted_cursors(void)
{
    unsigned char buffer[56] = {0};
    unsigned char* list = NULL;
    _X_UNUSED int ln = 0;
    int l = 0;
    int length, sent = 0;

    unsigned int i=0;
    struct deletedCursor* elem = NULL;

    *((uint32_t*)buffer)=DELETEDCURSOR;
    *((uint32_t*)buffer+1)=remoteVars.deletedcursor_list_size;

//    #warning check this
    ln=write(remoteVars.clientsock,buffer,56);

    list=malloc(sizeof(uint32_t)*remoteVars.deletedcursor_list_size);

    while(remoteVars.first_deleted_cursor)
    {
        *((uint32_t*)list+i)=remoteVars.first_deleted_cursor->serialNumber;

//        EPHYR_DBG("delete cursord %d",first_deleted_cursor->serialNumber);
        elem=remoteVars.first_deleted_cursor;
        remoteVars.first_deleted_cursor=elem->next;
        free(elem);
        ++i;
    }

    remoteVars.last_deleted_cursor=0l;

//    EPHYR_DBG("Sending list from %d elements", deletedcursor_list_size);
    length=remoteVars.deletedcursor_list_size*sizeof(uint32_t);
    while(sent<length)
    {
        l=write(remoteVars.clientsock,list+sent,((length-sent)<MAXMSGSIZE)?(length-sent):MAXMSGSIZE);
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

int send_output_selection(struct OutputChunk* chunk)
{
    //client supports extended selections
    if(remoteVars.selstruct.clientSupportsExetndedSelection)
    {
        //send extended selection
        return send_selection_chunk(chunk->selection, chunk->data, chunk->size, chunk->mimeData, chunk->firstChunk, chunk->lastChunk, chunk->compressed_size, chunk->totalSize);
    }
    else
    {
        //older client doesn't support only uncompressed datas in single chunk
        //not sending chunk in other case
        if(!chunk->compressed_size && chunk->firstChunk && chunk->lastChunk)
        {
            return send_selection_chunk(chunk->selection, chunk->data, chunk->size, chunk->mimeData, TRUE, TRUE, FALSE, chunk->size);
        }
        EPHYR_DBG("Client doesn't support extended selection, not sending this chunk");
    }
    return 0;
}

void send_reinit_notification(void)
{
    unsigned char buffer[56] = {0};
    _X_UNUSED int l;
    *((uint32_t*)buffer)=REINIT;
    EPHYR_DBG("SENDING REINIT NOTIFICATION");
    l=write(remoteVars.clientsock,buffer,56);
}

int send_selection_chunk(int sel, unsigned char* data, uint32_t length, uint32_t format, BOOL first, BOOL last, uint32_t compressed, uint32_t total)
{
    unsigned char buffer[56] = {0};
    _X_UNUSED int ln = 0;
    int l = 0;
    int sent = 0;


    *((uint32_t*)buffer)=SELECTION;    //0
    *((uint32_t*)buffer+1)=sel;        //4
    *((uint32_t*)buffer+2)=format;     //8
    *((uint32_t*)buffer+3)=length;     //16
    *((uint32_t*)buffer+4)=first;      //20
    *((uint32_t*)buffer+5)=last;       //24
    *((uint32_t*)buffer+6)=compressed; //28
    *((uint32_t*)buffer+7)=total; //32


//    #warning check this
    ln=write(remoteVars.clientsock,buffer,56);

    //if the data is compressed, send "compressed" amount of bytes
//     EPHYR_DBG("sending chunk. total %d, chunk %d, compressed %d", total, length, compressed);
    if(compressed)
    {
        length=compressed;
    }


    while(sent<length)
    {
        l=write(remoteVars.clientsock,data+sent,((length-sent)<MAXMSGSIZE)?(length-sent):MAXMSGSIZE);
        if(l<0)
        {
            EPHYR_DBG("Error sending selection!!!!!");
            break;
        }
        sent+=l;
    }
    return sent;
}



/*
 * sendqueue_mutex should be locked when calling this function
 */
static
struct cache_elem* find_best_match(struct cache_elem* frame, unsigned int* match_val)
{
    struct cache_elem* current = NULL;
    struct cache_elem* best_match_frame = NULL;
    unsigned int distance=0;
    unsigned int best_match_value=99999;

    current =frame->prev;
    while(current)
    {
        unsigned int matchVal=0;

        if((best_match_frame&& best_match_value<=distance) || distance > MAX_MATCH_VAL)
        {
            break;
        }
        if(!current->sent || !current->data || !current->size)
        {
            current=current->prev;
            continue;
        }

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

static
BOOL checkShiftedRegion( struct cache_elem* src, struct cache_elem* dst,  int32_t x, int32_t y,
                  int32_t width, int32_t height, int32_t horiz_shift, int32_t vert_shift)
{

//    EPHYR_DBG("FENTER %d %d %d %d %d",x,y,width,height,shift);
    int32_t vert_point=20;
    int32_t hor_point=20;
    int32_t vert_inc = 0;
    int32_t hor_inc = 0;
    uint32_t i=0;

    if(vert_point>height)
    {
        vert_point=height;
    }
    if(hor_point>width)
    {
        hor_point=width;
    }

    vert_inc=height/vert_point;
    hor_inc=width/hor_point;

    if(vert_inc<1)
    {
        vert_inc=1;
    }

    if(hor_inc<1)
    {
        hor_inc=1;
    }


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

//            EPHYR_DBG("Indices %d, %d, %d, %d %d", src_ind, dst_ind, src->size, dst->size, i);
            if(src->data[src_ind]!=dst->data[dst_ind])
            {
//                EPHYR_DBG("FEXIT");
                return FALSE;
            }
            if(src->data[src_ind+1]!=dst->data[dst_ind+1])
            {
//                EPHYR_DBG("FEXIT");
                return FALSE;
            }
            if(src->data[src_ind+2]!=dst->data[dst_ind+2])
            {
//                EPHYR_DBG("FEXIT");
                return FALSE;
            }
        }
    }
//    EPHYR_DBG("FEXIT");
    return TRUE;
}

static
int32_t checkScrollUp(struct cache_elem* source, struct cache_elem* dest)
{
//    EPHYR_DBG("checking for up scroll %u, %u, %u, %u", source->width, source->height, dest->width, dest->height);

    int32_t max_shift=source->height/3;

    int32_t x=source->width/10;
    int32_t y=source->height/10;

    int32_t width = source->width/2-source->width/5;
    int32_t height=source->height/2-source->height/10;

    if(x+width >= dest->width)
    {
         width=dest->width-x-1;
    }
    if(width<2)
    {
//        EPHYR_DBG("DST too small(w), keep checking %d %d %d %d", source->width, source->height, dest->width, dest->height);
        return -1;
    }

    if(y+height+max_shift >=dest->height)
    {
        height=dest->height-max_shift-y;
    }
    if(height<2)
    {
//        EPHYR_DBG("DST too small(h), keep checking %d %d %d %d", source->width, source->height, dest->width, dest->height);
        return -1;
    }

//    EPHYR_DBG(" %u %u %u %u %u",x,y,width, height, max_shift);


    for(int32_t shift=0;shift<max_shift;++shift)
    {
        if(!checkShiftedRegion( source, dest, x, y, width, height, 0, shift))
            continue;
        if(shift)
        {
//            EPHYR_DBG("Shift %d, Cursor is matched!\n",shift);
            return shift;
        }
    }
    return -1;
}

static
int32_t checkScrollDown(struct cache_elem* source, struct cache_elem* dest)
{
//    EPHYR_DBG("checking for down scroll %u, %u, %u, %u", source->width, source->height, dest->width, dest->height);
    int32_t max_shift=source->height/3*-1;

    int32_t x=source->width/10;
    int32_t y=source->height/2;

    int32_t width=source->width/2-source->width/5;
    int32_t height=source->height/2-source->height/10;

//    EPHYR_DBG(" %u %u %u %u %u",x,y,width, height, max_shift);

    if(x+width >= dest->width)
    {
        width=dest->width-x-1;
    }
    if(width<2)
    {
//        EPHYR_DBG("DST too small(w), keep checking %d %d %d %d", source->width, source->height, dest->width, dest->height);
        return 1;
    }

    if(y+height+abs(max_shift) >=dest->height)
    {
        height=dest->height-abs(max_shift)-y;
    }
    if(height<2)
    {
//        EPHYR_DBG("DST too small(h), keep checking %d %d %d %d", source->width, source->height, dest->width, dest->height);
        return 1;
    }

    for(int32_t shift=0;shift>max_shift;--shift)
    {
        if(!checkShiftedRegion( source, dest, x, y, width, height, 0,shift))
            continue;
        if(shift)
//            EPHYR_DBG("Shift %d, Cursor is matched!\n",shift);
            return shift;
    }
    return 1;
}

static
int32_t checkScrollRight(struct cache_elem* source, struct cache_elem* dest)
{
//    EPHYR_DBG("checking for up scroll %u, %u, %u, %u", source->width, source->height, dest->width, dest->height);
    int32_t max_shift=source->width/3;

    int32_t x=source->width/10;
    int32_t y=source->height/10;

    int32_t width=source->width/2-source->width/10;
    int32_t height=source->height/2-source->height/5;

    if(y+height >= dest->height)
    {
        height=dest->height-y-1;
    }
    if(height<2)
    {
//        EPHYR_DBG("DST too small(d), keep checking %d %d %d %d", source->width, source->height, dest->width, dest->height);
        return -1;
    }

    if(x+width+max_shift >=dest->width)
    {
        width=dest->width-abs(max_shift)-x;
    }
    if(width<2)
    {
//        EPHYR_DBG("DST too small(w), keep checking %d %d %d %d", source->width, source->height, dest->width, dest->height);
        return -1;
    }

//    EPHYR_DBG(" %u %u %u %u %u",x,y,width, height, max_shift);


    for(int32_t shift=0;shift<max_shift;++shift)
    {
        if(!checkShiftedRegion( source, dest, x, y, width, height, shift, 0 ))
            continue;
        if(shift)
        {
//            EPHYR_DBG("Shift %d, Cursor is matched!\n",shift);
            return shift;
        }
    }
    return -1;
}

static
int32_t checkScrollLeft(struct cache_elem* source, struct cache_elem* dest)
{
//    EPHYR_DBG("checking for up scroll %u, %u, %u, %u", source->width, source->height, dest->width, dest->height);
    int32_t max_shift=source->width/3*-1;

    int32_t x=source->width/2;
    int32_t y=source->height/10;

    int32_t width=source->width/2-source->width/10;
    int32_t height=source->height/2-source->height/5;

    if(y+height >= dest->height)
    {
        height=dest->height-y-1;
    }
    if(height<2)
    {
//        EPHYR_DBG("DST too small(d), keep checking %d %d %d %d", source->width, source->height, dest->width, dest->height);
        return 1;
    }

    if(x+width+abs(max_shift) >=dest->width)
    {
        width=dest->width-abs(max_shift)-x;
    }
    if(width<2)
    {
//        EPHYR_DBG("DST too small(w), keep checking %d %d %d %d", source->width, source->height, dest->width, dest->height);
        return 1;
    }

//    EPHYR_DBG(" %d %d %d %d %d",x,y,width, height, max_shift);

    for(int32_t shift=0;shift>max_shift;--shift)
    {
        if(!checkShiftedRegion( source, dest, x, y, width, height, shift, 0 ))
            continue;
        if(shift)
        {
//            EPHYR_DBG("Shift %d, Cursor is matched!\n",shift);
            return shift;
        }
    }
    return 1;
}

static
BOOL checkEquality(struct cache_elem* src, struct cache_elem* dst,
                   int32_t shift_horiz, int32_t shift_vert, rectangle* common_rect)
{

    int32_t center_x=src->width/2;
    int32_t center_y=src->height/2;

    int32_t x, y=center_y;

    int32_t right_x=src->width;

    int32_t down_y=src->height;

    int32_t left_x=0;
    int32_t top_y=0;

    --right_x;
    --down_y;

    if(center_x+shift_horiz >= dst->width  || center_y+shift_vert >=dst->height)
    {
        /* dst is too small for shift */
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

//    EPHYR_DBG("Center: %dx%d", center_x, center_y);
//    EPHYR_DBG("initial down_right %dx%d",right_x,down_y);

    for(y=center_y;y<=down_y;++y)
    {
        for(x=center_x; x<=right_x;++x)
        {
            int32_t src_ind=(y*src->width+x)*CACHEBPP;
            int32_t dst_ind=((y+shift_vert)*dst->width+x+shift_horiz)*CACHEBPP;

//            EPHYR_DBG("%d %d %d %d %d %d", x, y, right_x, down_y, dst->height, shift_vert);

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
//                EPHYR_DBG("limit right down to %dx%d",right_x,down_y);
                break;
            }
        }
    }


    loop_exit_0:
//    EPHYR_DBG("initial down_left %dx%d",left_x,down_y);
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
//                EPHYR_DBG("limit left down to %dx%d",left_x,down_y);
                break;
            }
        }
    }

    loop_exit_1:

//    EPHYR_DBG("initial top_right %dx%d",right_x,top_y);

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
//                EPHYR_DBG("limit right top to %dx%d",right_x,top_y);
                break;
            }
        }
    }

    loop_exit_2:;

//    EPHYR_DBG("top_right %dx%d",right_x,top_y);



//    EPHYR_DBG("initial top_left %dx%d\n",left_x,top_y);
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
//                EPHYR_DBG("limit left down to %dx%d",left_x,down_y);
                break;
            }
        }
     }

     loop_exit_3:;

     common_rect->size.width=right_x-left_x+1;
     common_rect->size.height=down_y-top_y+1;

     if(common_rect->size.width<1 || common_rect->size.height <1)
     {
//         EPHYR_DBG("!!!!!!!NEGATIVE OR NULL GEOMETRY!!!!!!!");
         return FALSE;
     }

     common_rect->lt_corner.x=left_x;
     common_rect->lt_corner.y=top_y;
//     EPHYR_DBG("Geometry: %d:%d  %dx%d shift - %d  %d ", left_x, top_y, common_rect->size.width,
//               common_rect->size.height, shift_horiz, shift_vert);
     return TRUE;
}

static
BOOL checkMovedContent(struct cache_elem* source, struct cache_elem* dest, int32_t* horiz_shift, int32_t* vert_shift)
{
//    EPHYR_DBG("checking for moved content %u, %u, %u, %u", source->width, source->height, dest->width, dest->height);
    int32_t max_shift=source->width/8;

    int32_t x=source->width/2;
    int32_t y=source->height/2;

    int32_t width=source->width/4;
    int32_t height=source->height/4;

    if(max_shift>source->height/8)
        max_shift=source->height/8;

    if(max_shift>20)
        max_shift=20;

    if(y+height+max_shift >= dest->height)
    {
        height=dest->height-max_shift-y-1;
    }
    if(height<2)
    {
//        EPHYR_DBG("DST too small(d), keep checking %d %d %d %d", source->width, source->height, dest->width, dest->height);
        return FALSE;
    }

    if(x+width+max_shift >=dest->width)
    {
        width=dest->width-max_shift-x;
    }
    if(width<2)
    {
//        EPHYR_DBG("DST too small(w), keep checking %d %d %d %d", source->width, source->height, dest->width, dest->height);
        return FALSE;
    }

//    EPHYR_DBG(" %d %d %d %d %d",x,y,width, height, max_shift);

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

static
BOOL findDiff(struct cache_elem* source, struct cache_elem* dest, rectangle* diff_rect)
{
    int32_t left_x=source->width-1, top_y=source->height-1, right_x=0, bot_y=0;
    float eff = 0;

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

    eff= (float)(diff_rect->size.width*diff_rect->size.height)/ (float)(source->width*source->height);

    if(eff>0.8)
        return FALSE;

//    EPHYR_DBG("REG_GEOM: %dx%d. DIF_GEOM %d,%d - %dx%d EFF=%f", source->width, source->height, left_x,top_y,
//              diff_rect->size.width, diff_rect->size.height,eff);
    return TRUE;
}

static
BOOL find_common_regions(struct cache_elem* source, struct cache_elem* dest, BOOL* diff, rectangle* common_rect,
                         int32_t* hshift, int32_t* vshift)
{
//    EPHYR_DBG("checking for common regions");

    *diff=FALSE;

    *hshift=0;
    *vshift=checkScrollDown(source,dest);
    if(*vshift<0)
    {
//        EPHYR_DBG("Found scroll down, vert shift %d %u %u %u %u" , *vshift, dest->width, dest->height, dest->crc, source->crc);
        return checkEquality(source, dest, 0, *vshift, common_rect);
    }

    *hshift=0;
    *vshift=checkScrollUp(source, dest);
    if(*vshift>0)
    {
//        EPHYR_DBG("Found scroll up, vert shift %d %u %u %u %u" , *vshift, dest->width, dest->height, dest->crc, source->crc);
        return checkEquality(source, dest, 0, *vshift, common_rect);
    }

//    #warning stop here for the moment, let's see later if we'll use it
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
//        EPHYR_DBG("SCROLL LEFT %d", *hshift);
        return checkEquality(source, dest, *hshift, 0, common_rect);
    }

    if((source->width != dest->width) && (source->height!=dest->height))
    {
        int32_t h_shift, v_shift;

        *vshift=0;
        *hshift=0;
        if(checkMovedContent(source, dest, &h_shift, &v_shift))
        {
            *hshift=h_shift;
            *vshift=v_shift;
//            EPHYR_DBG("found moved content %d, %d", *hshift, *vshift);
            return checkEquality(source, dest, *hshift, *vshift, common_rect);
        }
    }

    if((source->width == dest->width) && (source->height==dest->height))
    {
        *diff=TRUE;
        //         EPHYR_DBG("LOOK FOR IMAGE DIFFERENCE");
        return findDiff(source, dest, common_rect);
    }

//    EPHYR_DBG("Scroll not found %d",dest->crc);
    return FALSE;
}

/* use only from send thread */
static
void sendMainImageFromSendThread(uint32_t width, uint32_t height, int32_t dx ,int32_t dy, uint32_t winId)
{
    _X_UNUSED uint32_t length = 0;
    struct frame_region regions[9] = {{0}};

    uint32_t isize = 0;
    unsigned char* data = NULL;

    uint32_t i = 0;
    uint32_t ind = 0;

    BOOL mainImage=FALSE;

/*    if(width!=0)
    {
        EPHYR_DBG("sending UPDATE- %dx%d, %d,%d",width, height,dx,dy);
    }
    else
    {
        EPHYR_DBG("sending mainImage");
    }*/


    pthread_mutex_lock(&remoteVars.mainimg_mutex);

    for(int j=0;j<9;++j)
    {
        regions[j].rect.size.width=0;
        regions[j].source_crc=0;
        regions[j].compressed_data=0;
        regions[j].size=0;
    }

    if(!width || (dx==0 && dy==0 && width==remoteVars.main_img_width && height==remoteVars.main_img_height))
    {
        mainImage=TRUE;
        dx=dy=0;
        width=remoteVars.main_img_width;
        height=remoteVars.main_img_height;
    }

    isize=width*height*CACHEBPP;
    data=malloc(isize);

    for(int32_t y=0;y<height;++y)
    {
        for(int32_t x=0; x< width; ++x)
        {
            ind=((y+dy)*remoteVars.main_img_width+dx+x)*XSERVERBPP;
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
        send_frame(width, height,-1,-1,0,regions, winId);
    }
    else
    {
        send_frame(width, height,dx,dy,0,regions, winId);
    }


    pthread_mutex_unlock(&remoteVars.mainimg_mutex);
    free(regions[0].compressed_data);
}

static
void remote_send_win_updates(char* updateBuf, uint32_t bufSize)
{
    unsigned char buffer[56] = {0};
    int l = 0;
    int sent = 0;

    *((uint32_t*)buffer)=WINUPDATE;
    *((uint32_t*)buffer+1)=bufSize;

    write(remoteVars.clientsock,buffer,56);

    while(sent<bufSize)
    {
        l=write(remoteVars.clientsock,updateBuf+sent,((bufSize-sent)<MAXMSGSIZE)?(bufSize-sent):MAXMSGSIZE);
        if(l<0)
        {
            EPHYR_DBG("Error sending windows update!!!!!");
            break;
        }
        sent+=l;
    }
//     EPHYR_DBG("SENT WIN UPDATES %d",bufSize);
    free(updateBuf);
}


void remote_process_window_updates(void)
{
    /*sendqueue mutex is locked here*/
    struct remoteWindow* prev=NULL;
    struct remoteWindow* rwin=remoteVars.windowList;
    struct remoteWindow* tmp;
    int bufSize=0;
    int bufHead=0;
    char* updateBuf=NULL;
    int8_t state;
    int16_t nameSize;
    //calculate size of update buffer
    while(rwin)
    {
        if((rwin->state == CHANGED)||(rwin->state == NEW))
        {
            bufSize+=WINUPDSIZE;
            if(rwin->name)
            {
                bufSize+=strlen(rwin->name);
            }
            bufSize+=rwin->icon_size;
        }
        if(rwin->state==WDEL)
        {
            bufSize+=WINUPDDELSIZE;
        }
        rwin=rwin->next;
    }
    //copy update data to buffer
    updateBuf=malloc(bufSize);
    rwin=remoteVars.windowList;
    while(rwin)
    {
        if(rwin->state != UNCHANGED)
        {
            memcpy(updateBuf+bufHead, &(rwin->id), sizeof(uint32_t));
            bufHead+=sizeof(uint32_t);
            state=rwin->state;
            memcpy(updateBuf+bufHead, &state, sizeof(state));
            bufHead+=sizeof(state);
        }
        if((rwin->state == CHANGED)||(rwin->state==NEW))
        {
            memcpy(updateBuf+bufHead, &(rwin->parentId), sizeof(uint32_t));
            bufHead+=sizeof(uint32_t);

            memcpy(updateBuf+bufHead, &(rwin->nextSibId), sizeof(uint32_t));
            bufHead+=sizeof(uint32_t);

            memcpy(updateBuf+bufHead, &(rwin->transWinId), sizeof(uint32_t));
            bufHead+=sizeof(uint32_t);

            memcpy(updateBuf+bufHead, &(rwin->x), sizeof(int16_t));
            bufHead+=sizeof(int16_t);
            memcpy(updateBuf+bufHead, &(rwin->y), sizeof(int16_t));
            bufHead+=sizeof(int16_t);
            memcpy(updateBuf+bufHead, &(rwin->w), sizeof(uint16_t));
            bufHead+=sizeof(uint16_t);
            memcpy(updateBuf+bufHead, &(rwin->h), sizeof(uint16_t));
            bufHead+=sizeof(uint16_t);
            memcpy(updateBuf+bufHead, &(rwin->minw), sizeof(uint16_t));
            bufHead+=sizeof(uint16_t);
            memcpy(updateBuf+bufHead, &(rwin->minh), sizeof(uint16_t));
            bufHead+=sizeof(uint16_t);
            memcpy(updateBuf+bufHead, &(rwin->bw), sizeof(uint16_t));
            bufHead+=sizeof(uint16_t);
            memcpy(updateBuf+bufHead, &(rwin->visibility), sizeof(int8_t));
            bufHead+=sizeof(int8_t);
            memcpy(updateBuf+bufHead, &(rwin->winType), sizeof(int8_t));
            bufHead+=sizeof(int8_t);
            nameSize=0;
            if(rwin->name)
            {
                nameSize=strlen(rwin->name);
            }
            memcpy(updateBuf+bufHead, &nameSize, sizeof(nameSize));
            bufHead+=sizeof(nameSize);
            if(nameSize)
            {
                memcpy(updateBuf+bufHead, rwin->name, nameSize);
                bufHead+=nameSize;
            }
            memcpy(updateBuf+bufHead, &(rwin->icon_size), sizeof(uint32_t));
            bufHead+=sizeof(uint32_t);
            if(rwin->icon_size)
            {
                memcpy(updateBuf+bufHead, rwin->icon_png, rwin->icon_size);
                bufHead+=rwin->icon_size;
                //send icon data only once
                free(rwin->icon_png);
                rwin->icon_png=0;
                rwin->icon_size=0;
            }
            rwin->state=UNCHANGED;
        }
        if(rwin->state==WDEL)
        {
            //remove window from list and free resources
//             EPHYR_DBG("release window %p, %s",rwin->ptr, rwin->name);
            if(rwin==remoteVars.windowList)
            {
                remoteVars.windowList=rwin->next;
            }
            if(prev)
            {
                prev->next=rwin->next;
            }
            tmp=rwin;
            rwin=rwin->next;
            if(tmp->name)
            {
                free(tmp->name);
            }
            if(tmp->icon_png)
            {
                free(tmp->icon_png);
            }
            free(tmp);
        }
        else
        {
            prev=rwin;
            rwin=rwin->next;
        }
    }
    /*
    EPHYR_DBG("NEW LIST:");
    rwin=remoteVars.windowList;
    while(rwin)
    {
        EPHYR_DBG("=====%p",rwin->ptr);
        rwin=rwin->next;
    }*/

    //send win updates
    remoteVars.windowsUpdated=FALSE;
    pthread_mutex_unlock(&remoteVars.sendqueue_mutex);
    //send_updates
    remote_send_win_updates(updateBuf, bufSize);
    pthread_mutex_lock(&remoteVars.sendqueue_mutex);
}

static
void *send_frame_thread (void *threadid)
{
    enum SelectionType r;

#ifdef EPHYR_WANT_DEBUG
    debug_sendThreadId=pthread_self();
#endif /* EPHYR_WANT_DEBUG */

    while(1)
    {

        pthread_mutex_lock(&remoteVars.sendqueue_mutex);
        if(!remoteVars.client_connected)
        {
            EPHYR_DBG ("TCP connection closed\n");
            shutdown(remoteVars.clientsock, SHUT_RDWR);
            close(remoteVars.clientsock);

            pthread_mutex_unlock(&remoteVars.sendqueue_mutex);
            break;
        }
        remoteVars.client_connected=TRUE;

        //check if we should send the server version to client
        if(remoteVars.client_version && ! remoteVars.server_version_sent)
        {
            //the client supports versions and we didn't send our version yet
            remote_sendVersion();
        }



        if(!remoteVars.first_sendqueue_element && !remoteVars.firstCursor && !remoteVars.selstruct.firstOutputChunk &&
            !remoteVars.cache_rebuilt && !remoteVars.windowsUpdated)
        {
            /* sleep if frame queue is empty */
            pthread_cond_wait(&remoteVars.have_sendqueue_cond, &remoteVars.sendqueue_mutex);
        }

        /* mutex is locked on this point */

        //if windows list is updated send changes to client
        if(remoteVars.windowsUpdated)
        {
            remote_process_window_updates();
        }

        /*mutex still locked*/
        //send notification to client that cache is rebuilt
        if(remoteVars.cache_rebuilt)
        {
            remoteVars.cache_rebuilt=FALSE;
            pthread_mutex_unlock(&remoteVars.sendqueue_mutex);
            send_reinit_notification();
            pthread_mutex_lock(&remoteVars.sendqueue_mutex);
        }

        //only send output selection chunks if there are no frames and cursors in the queue
        //selections can take a lot of bandwidth and have less priority
        if(remoteVars.selstruct.firstOutputChunk && !remoteVars.first_sendqueue_element && !remoteVars.firstCursor)
        {
            //get chunk from queue
            struct OutputChunk* chunk=remoteVars.selstruct.firstOutputChunk;
            remoteVars.selstruct.firstOutputChunk=chunk->next;
            if(!remoteVars.selstruct.firstOutputChunk)
            {
                remoteVars.selstruct.lastOutputChunk=NULL;
            }

            pthread_mutex_unlock(&remoteVars.sendqueue_mutex);
            //send chunk
            send_output_selection(chunk);
            //free chunk and it's data
            if(chunk->data)
            {
                free(chunk->data);
            }
            //                 EPHYR_DBG(" REMOVE CHUNK %p %p %p", remoteVars.selstruct.firstOutputChunk, remoteVars.selstruct.lastOutputChunk, chunk);
            free(chunk);

            pthread_mutex_lock(&remoteVars.sendqueue_mutex);
        }

        //check if we need to request the selection from client
        if(remoteVars.selstruct.requestSelection[PRIMARY] || remoteVars.selstruct.requestSelection[CLIPBOARD])
        {
            for(r=PRIMARY; r<=CLIPBOARD; ++r)
            {
                if(remoteVars.selstruct.requestSelection[r])
                {
                    remoteVars.selstruct.requestSelection[r]=FALSE;

                    pthread_mutex_unlock(&remoteVars.sendqueue_mutex);
                    //send request for selection
                    request_selection_from_client(r);

                    pthread_mutex_lock(&remoteVars.sendqueue_mutex);
                }
            }
        }

        if(remoteVars.firstCursor)
        {
            /* get cursor from queue, delete it from queue, unlock mutex and send cursor. After sending free cursor */
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
        if(remoteVars.first_sendqueue_element)
        {
            int elems=queue_elements();
            struct cache_elem* frame = NULL;
            struct sendqueue_element* current = NULL;
            uint32_t  x=0, y = 0, winId=0;
            int32_t width, height = 0;

            if(remoteVars.maxfr<elems)
            {
                remoteVars.maxfr=elems;
            }
            //                EPHYR_DBG("have %d max frames in queue, current %d", remoteVars.,elems);
            frame=remoteVars.first_sendqueue_element->frame;

            /* delete first element from frame queue */
            current=remoteVars.first_sendqueue_element;
            if(remoteVars.first_sendqueue_element->next)
            {
                remoteVars.first_sendqueue_element=remoteVars.first_sendqueue_element->next;
            }
            else
            {
                remoteVars.first_sendqueue_element=remoteVars.last_sendqueue_element=NULL;
            }
            x=current->x;
            y=current->y;
            width=current->width;
            height=current->height;
            winId=current->winId;
            free(current);

            if(frame)
            {
                uint32_t crc = frame->crc;
                uint32_t frame_width=frame->width;
                uint32_t frame_height=frame->height;

                /* unlock sendqueue for main thread */

                pthread_mutex_unlock(&remoteVars.sendqueue_mutex);
                send_frame(frame_width, frame_height, x, y, crc, frame->regions, winId);
            }
            else
            {
//                 EPHYR_DBG("Sending main image or screen update");

                pthread_mutex_unlock(&remoteVars.sendqueue_mutex);
                sendMainImageFromSendThread(width, height, x, y, winId);
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
    EPHYR_DBG("exit sending thread");
    remoteVars.send_thread_id=0;
    pthread_exit(0);
}

/* warning! sendqueue_mutex should be locked by thread calling this function! */
void clear_output_selection(void)
{
    struct OutputChunk* chunk=remoteVars.selstruct.firstOutputChunk;
    struct OutputChunk* prev_chunk;

    while(chunk)
    {
        prev_chunk=chunk;
        chunk=chunk->next;
        if(prev_chunk->data)
        {
            free(prev_chunk->data);
        }
        free(prev_chunk);
    }
    remoteVars.selstruct.firstOutputChunk=remoteVars.selstruct.lastOutputChunk=NULL;
}

/* warning! sendqueue_mutex should be locked by thread calling this function! */
static
void clear_send_queue(void)
{
    struct sendqueue_element* current = NULL;
    struct sendqueue_element* next = NULL;

    current=remoteVars.first_sendqueue_element;
    while(current)
    {
        if(current->frame)
        {
            current->frame->busy=0;
            if(current->frame->source)
                current->frame->source->busy--;
            current->frame->source=0;
        }
        next=current->next;
        free(current);
        current=next;
    }
    remoteVars.first_sendqueue_element=remoteVars.last_sendqueue_element=NULL;
}

/*
 * remove elements from cache and release all images if existing.
 * warning! sendqueue_mutex should be locked by thread calling this function!
 */
void clear_frame_cache(uint32_t max_elements)
{
//     EPHYR_DBG("cache elements %d, cache size %lu, reducing to size: %d\n", remoteVars.cache_elements, remoteVars.cache_size, max_elements);
    while(remoteVars.first_cache_element && (remoteVars.cache_elements > max_elements))
    {
        struct cache_elem* next = NULL;

        /* don't delete it now, return to it later
         * but if max_elements is 0 we are clearing all elements
         */
        if(remoteVars.first_cache_element->busy && max_elements)
        {
            EPHYR_DBG("%x - is busy (%d), not deleting", remoteVars.first_cache_element->crc, remoteVars.first_cache_element->busy);
            return;
        }
        next = remoteVars.first_cache_element->next;
        if(remoteVars.first_cache_element->size)
        {
            free(remoteVars.first_cache_element->data);
            remoteVars.cache_size-=remoteVars.first_cache_element->size;
        }

        //add element to deleted list if client is connected and we are not deleting all frame list
        if(remoteVars.client_connected && max_elements)
        {
            /* add deleted element to the list for sending */
            struct deleted_elem* delem=malloc(sizeof(struct deleted_elem));

            ++remoteVars.deleted_list_size;
            delem->next=0l;
            delem->crc=remoteVars.first_cache_element->crc;
//            EPHYR_DBG("delete %x",delem->crc);

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
    if(!remoteVars.first_cache_element) {
        remoteVars.last_cache_element=NULL;
    }
//    EPHYR_DBG("cache elements %d, cache size %d\n", cache_elements, cache_size);
}


static
const char* getAgentStateAsString(int state)
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

void delete_all_windows(void)
{
    //sendqueue_mutex should be locked here
    struct remoteWindow* rwin=remoteVars.windowList;
    struct remoteWindow* tmp;
    while(rwin)
    {
        //remove window from list and free resources
        //EPHYR_DBG("release window %p, %s",rwin->ptr, rwin->name);
        tmp=rwin;
        rwin=rwin->next;
        if(tmp->name)
        {
            free(tmp->name);
        }
        if(tmp->icon_png)
        {
            free(tmp->icon_png);
        }
        free(tmp);
    }
    remoteVars.windowList=NULL;
}

void disconnect_client(void)
{
    EPHYR_DBG("DISCONNECTING CLIENT, DOING SOME CLEAN UP");

    pthread_mutex_lock(&remoteVars.sendqueue_mutex);
    remoteVars.client_connected=FALSE;
    setAgentState(SUSPENDED);
    delete_all_windows();
    clear_send_queue();
    clear_frame_cache(0);
    freeCursors();
    clear_output_selection();
#if XORG_VERSION_CURRENT >= 11900000
    EPHYR_DBG("Remove notify FD for client sock %d",remoteVars.clientsock);
    RemoveNotifyFd(remoteVars.clientsock);
#endif /* XORG_VERSION_CURRENT */

    pthread_cond_signal(&remoteVars.have_sendqueue_cond);


    pthread_mutex_unlock(&remoteVars.sendqueue_mutex);
}

void unpack_current_chunk_to_buffer(struct InputBuffer* selbuff)
{
    //unpacking the data from current chunk to selbuff

    z_stream stream;
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;

//     EPHYR_DBG("inflate %d bytes to %d", selbuff->currentChunkCompressedSize, selbuff->currentChunkSize);

    stream.avail_in = selbuff->currentChunkCompressedSize;
    stream.next_in = selbuff->currentChunkCompressedData;
    stream.avail_out = selbuff->currentChunkSize;
    stream.next_out = selbuff->data + selbuff->bytesReady;

    inflateInit(&stream);
    inflate(&stream, Z_NO_FLUSH);
    inflateEnd(&stream);

    if(stream.total_out != selbuff->currentChunkSize)
    {
        //something is wrong with extracting the data
        EPHYR_DBG("WARNING!!!! extracting the data failed output has %d bytes instead of %d", (uint32_t)stream.total_out, selbuff->currentChunkSize);
    }


//     EPHYR_DBG("%s", selbuff->data + selbuff->bytesReady);
    ///freeing compressed data
    free(selbuff->currentChunkCompressedData);
    selbuff->currentChunkCompressedData=NULL;

    selbuff->bytesReady+=selbuff->currentChunkSize;
    selbuff->currentChunkCompressedSize=0;
}


void readInputSelectionBuffer(char* buff)
{
    //read th rest of the chunk data

    struct InputBuffer* selbuff;
    int leftToRead, l;


    //lock input selection

    pthread_mutex_lock(&remoteVars.selstruct.inMutex);

    selbuff = &remoteVars.selstruct.inSelection[remoteVars.selstruct.currentInputBuffer];

    //if the data is not compressed read it directly to the buffer
    if(!selbuff->currentChunkCompressedSize)
    {
        leftToRead=selbuff->currentChunkSize - selbuff->currentChunkBytesReady;
        l=(leftToRead < EVLENGTH)?leftToRead:EVLENGTH;

        //copy data to selection
        memcpy(selbuff->data+selbuff->bytesReady, buff, l);
        selbuff->bytesReady+=l;
        selbuff->currentChunkBytesReady+=l;
        if(selbuff->currentChunkBytesReady==selbuff->currentChunkSize)
        {
            //selection chunk received completely, next event will start with event header
            //         EPHYR_DBG("READY Selection Chunk, read %d",selbuff->currentChunkSize);
            remoteVars.selstruct.readingInputBuffer=-1;
        }
    }
    else
    {
        //copy to the buffer for compressed data
        leftToRead=selbuff->currentChunkCompressedSize - selbuff->currentChunkBytesReady;
        l=(leftToRead < EVLENGTH)?leftToRead:EVLENGTH;

        //copy data to selection
        memcpy(selbuff->currentChunkCompressedData+selbuff->currentChunkBytesReady, buff, l);
        selbuff->currentChunkBytesReady+=l;
        if(selbuff->currentChunkBytesReady==selbuff->currentChunkCompressedSize)
        {
            //selection chunk received completely, next event will start with event header
            EPHYR_DBG("READY Selection Chunk, read %d",selbuff->currentChunkSize);
            remoteVars.selstruct.readingInputBuffer=-1;
            //unpack data to selection buffer
            unpack_current_chunk_to_buffer(selbuff);
        }
    }

    if(selbuff->bytesReady==selbuff->size)
    {
        //selection buffer received completely
//          EPHYR_DBG("READY Selection %d, MIME %d, Read %d from %d", remoteVars.selstruct.currentInputBuffer, selbuff->mimeData, selbuff->bytesReady, selbuff->size);
        //send notify to system that we are using selection
        //if state is requested we already own this selection after notify
        if(selbuff->state != REQUESTED)
            own_selection(remoteVars.selstruct.currentInputBuffer);
        selbuff->state=COMPLETED;
        //send notification event to interrupt sleeping selection thread
        client_sel_data_notify(remoteVars.selstruct.currentInputBuffer);
    }
    //unlock selection


    pthread_mutex_unlock(&remoteVars.selstruct.inMutex);
}

void readInputSelectionHeader(char* buff)
{

    //read the input selection event.
    //The event represents one chunk of imput selection
    //it has a header and some data of the chunk

    uint32_t size, totalSize;
    uint8_t destination, mime;
    struct InputBuffer* selbuff = NULL;
    BOOL firstChunk=FALSE, lastChunk=FALSE;
    uint32_t compressedSize=0;
    uint32_t headerSize=10;
    uint32_t l;

    size=*((uint32_t*)buff+1);
    destination=*((uint8_t*)buff+8);
    mime=*((uint8_t*)buff+9);

    //if client supports ext selection, read extended header
    if(remoteVars.selstruct.clientSupportsExetndedSelection)
    {
        headerSize=20;
        firstChunk=*((uint8_t*)buff + 10);
        lastChunk=*((uint8_t*)buff + 11);
        compressedSize=*((uint32_t*) (buff + 12));
        totalSize=*( (uint32_t*) (buff+16));
    }
    else
    {
        compressedSize=0;
        lastChunk=firstChunk=TRUE;
        totalSize=size;
    }

    //sanity check
    if((destination != PRIMARY)&& (destination!= CLIPBOARD))
    {
        EPHYR_DBG("WARNING: unsupported destination %d, setting to CLIPBOARD",destination);
        destination=CLIPBOARD;
    }

     EPHYR_DBG("HAVE NEW INCOMING SELECTION Chunk: sel %d size %d mime %d compressed size %d, total %d",destination, size, mime, compressedSize, totalSize);


    //lock selection
    pthread_mutex_lock(&remoteVars.selstruct.inMutex);

    remoteVars.selstruct.readingInputBuffer=-1;

    selbuff = &remoteVars.selstruct.inSelection[destination];

    //we recieved selection notify from client
    if(firstChunk && lastChunk && remoteVars.selstruct.clientSupportsExetndedSelection && (totalSize == 0) &&(size == 0))
    {
        EPHYR_DBG("Selection notify from client for %d", destination);
        if(selbuff->size && selbuff->data)
        {
            free(selbuff->data);
        }
        selbuff->size=0;
        selbuff->mimeData=mime;
        selbuff->data=0;
        selbuff->bytesReady=0;
        selbuff->state=NOTIFIED;
// own selection
        own_selection(destination);
        //unlock mutex


        pthread_mutex_unlock(&remoteVars.selstruct.inMutex);
        return;
    }
    if(firstChunk)
    {
        //if it's first chunk, initialize our selection buffer
        if(selbuff->size && selbuff->data)
        {
            free(selbuff->data);
        }
        selbuff->size=totalSize;
        selbuff->mimeData=mime;
        selbuff->data=malloc(totalSize);
        selbuff->bytesReady=0;
    }

    if(selbuff->currentChunkCompressedSize && selbuff->currentChunkCompressedData)
    {
        free(selbuff->currentChunkCompressedData);
    }
    selbuff->currentChunkCompressedData=NULL;
    selbuff->currentChunkCompressedSize=0;

    //if compressed data will be read in buffer for compressed data
    if(compressedSize)
    {
        selbuff->currentChunkCompressedData=malloc(compressedSize);
        selbuff->currentChunkCompressedSize=compressedSize;
        l=(compressedSize < EVLENGTH-headerSize)?compressedSize:(EVLENGTH-headerSize);
        memcpy(selbuff->currentChunkCompressedData, buff+headerSize, l);
    }
    else
    {
        //read the selection data from header
        l=(size < EVLENGTH-headerSize)?size:(EVLENGTH-headerSize);
        memcpy(selbuff->data+selbuff->bytesReady, buff+headerSize, l);

        selbuff->bytesReady+=l;
    }

    selbuff->currentChunkBytesReady=l;
    selbuff->currentChunkSize=size;


    if(!compressedSize)
    {
        if(selbuff->currentChunkBytesReady != selbuff->currentChunkSize)
        {
            // we didn't recieve complete chunk yet, next event will have data
            remoteVars.selstruct.currentInputBuffer=remoteVars.selstruct.readingInputBuffer=destination;
        }
    }
    else
    {
        if(selbuff->currentChunkBytesReady != selbuff->currentChunkCompressedSize)
        {
            // we didn't recieve complete chunk yet, next event will have data
            remoteVars.selstruct.currentInputBuffer=remoteVars.selstruct.readingInputBuffer=destination;
        }
        else
        {
            //we read all compressed chunk data, unpack it to sel buff
            unpack_current_chunk_to_buffer(selbuff);
        }

    }

    if(selbuff->size == selbuff->bytesReady)
    {
        //Selection is completed
//         EPHYR_DBG("READY INCOMING SELECTION for %d",destination);
        //own the selection
        //if state is requested we already own this selection after notify
        if(selbuff->state != REQUESTED)
           own_selection(destination);
        selbuff->state=COMPLETED;
        //send notification event to interrupt sleeping selection thread
        client_sel_data_notify(destination);
    }

    //unlock selection


    pthread_mutex_unlock(&remoteVars.selstruct.inMutex);
}

void
clientReadNotify(int fd, int ready, void *data)
{
    BOOL con;
    int length = 0;
    int iterations = 0;
    int restDataLength, restDataPos = 0;

    pthread_mutex_lock(&remoteVars.sendqueue_mutex);
    con=remoteVars.client_connected;


    pthread_mutex_unlock(&remoteVars.sendqueue_mutex);
    if(!con)
        return;

    /* read max 99 events */
    length=read(remoteVars.clientsock,remoteVars.eventBuffer + remoteVars.evBufferOffset, EVLENGTH*99);

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
//    EPHYR_DBG("Got ev bytes - %d\n",eventnum++);


    length+=remoteVars.evBufferOffset;
    iterations=length/EVLENGTH;

    for(int i=0;i<iterations;++i)
    {
        char* buff=remoteVars.eventBuffer+i*EVLENGTH;

        if(remoteVars.selstruct.readingInputBuffer != -1)
        {
            readInputSelectionBuffer(buff);
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

//                    EPHYR_DBG("HAVE MOTION EVENT %d, %d from client\n",x,y);
                    ephyrClientMouseMotion(x,y);
                    break;
                }
                case ButtonPress:
                case ButtonRelease:
                {
                    uint32_t state=*((uint32_t*)buff+1);
                    uint32_t button=*((uint32_t*)buff+2);

//                    EPHYR_DBG("HAVE BUTTON PRESS/RELEASE EVENT %d, %d from client\n",state,button);
                    ephyrClientButton(event_type,state, button);
                    break;
                }
                case KeyPress:
                {
                    uint32_t state=*((uint32_t*)buff+1);
                    uint32_t key=*((uint32_t*)buff+2);

//                    EPHYR_DBG("HAVE KEY PRESS EVENT state: %d(%x), key: %d(%x) from client\n",state,state, key, key);
//                    if (state & ShiftMask)
//                    {
//                        EPHYR_DBG("SHIFT");
//                    }
//                    if (state & LockMask)
//                    {
//                        EPHYR_DBG("LOCK");
//                    }
//                    if (state & ControlMask)
//                    {
//                        EPHYR_DBG("CONTROL");
//                    }
//                    if (state & Mod1Mask)
//                    {
//                        EPHYR_DBG("MOD1");
//                    }
//                    if (state & Mod2Mask)
//                    {
//                        EPHYR_DBG("MOD2");
//                    }
//                    if (state & Mod3Mask)
//                    {
//                        EPHYR_DBG("MOD3");
//                    }
//                    if (state & Mod4Mask)
//                    {
//                        EPHYR_DBG("MOD4");
//                    }
//                    if (state & Mod5Mask)
//                    {
//                        EPHYR_DBG("MOD5");
//                    }

                    ephyrClientKey(event_type,state, key);
                    break;
                }
                case KeyRelease:
                {
                    uint32_t state=*((uint32_t*)buff+1);
                    uint32_t key=*((uint32_t*)buff+2);

//                    EPHYR_DBG("HAVE KEY RELEASE EVENT state: %d(%x), key: %d(%x) from client\n",state,state, key, key);
//                    if (state & ShiftMask)
//                    {
//                        EPHYR_DBG("SHIFT");
//                    }
//                    if (state & LockMask)
//                    {
//                        EPHYR_DBG("LOCK");
//                    }
//                    if (state & ControlMask)
//                    {
//                        EPHYR_DBG("CONTROL");
//                    }
//                    if (state & Mod1Mask)
//                    {
//                        EPHYR_DBG("MOD1");
//                    }
//                    if (state & Mod2Mask)
//                    {
//                        EPHYR_DBG("MOD2");
//                    }
//                    if (state & Mod3Mask)
//                    {
//                        EPHYR_DBG("MOD3");
//                    }
//                    if (state & Mod4Mask)
//                    {
//                        EPHYR_DBG("MOD4");
//                    }
//                    if (state & Mod5Mask)
//                    {
//                        EPHYR_DBG("MOD5");
//                    }
                    ephyrClientKey(event_type,state, key);
                    break;
                }
                case GEOMETRY:
                {
                    uint16_t width=*((uint16_t*)buff+2);
                    uint16_t height=*((uint16_t*)buff+3);
                    struct VirtScreen screens[4] = {{0}};

                    remoteVars.client_initialized=TRUE;
                    EPHYR_DBG("Client want resize to %dx%d",width,height);

                    memset(screens,0, sizeof(struct VirtScreen)*4);
                    for(int j=0;j<4;++j)
                    {
                        char* record=buff+9+j*8;
                        screens[j].width=*((uint16_t*)record);
                        screens[j].height=*((uint16_t*)record+1);
                        screens[j].x=*((int16_t*)record+2);
                        screens[j].y=*((int16_t*)record+3);

                        if(!screens[j].width || !screens[j].height)
                        {
                            break;
                        }
                        EPHYR_DBG("SCREEN %d - (%dx%d) - %d,%d", j, screens[j].width, screens[j].height, screens[j].x, screens[j].y);
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
                    uint32_t winid=0;
                    if(remoteVars.rootless)
                    {
                        winid=*((uint32_t*)buff+5);
                    }

//                     EPHYR_DBG("HAVE UPDATE request from client, window 0x%X %dx%d %d,%d\n",winid, width, height, x,y );
                    pthread_mutex_lock(&remoteVars.mainimg_mutex);

//                     EPHYR_DBG("DBF: %p, %d, %d",remoteVars.main_img, remoteVars.main_img_width, remoteVars.main_img_height);

                    if(remoteVars.main_img  && x+width <= remoteVars.main_img_width  && y+height <= remoteVars.main_img_height )
                    {

                        pthread_mutex_unlock(&remoteVars.mainimg_mutex);
                        add_frame(width, height, x, y, 0, 0, winid);
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
                    readInputSelectionHeader(buff);
                    break;
                }
                case CLIENTVERSION:
                {
                    int16_t ver=*((uint16_t*)buff+2);
                    int16_t os=*((uint16_t*)buff+3);
                    set_client_version(ver, os);
                    EPHYR_DBG("Client information: vesrion %d, os %d", ver, os);
                    pthread_cond_signal(&remoteVars.have_sendqueue_cond);
                    break;
                }
                case DEMANDSELECTION:
                {
                    int16_t sel=*((uint16_t*)buff+2);
//                     EPHYR_DBG("Client requesting selection %d", sel);
                    client_sel_request_notify(sel);
                    break;
                }
                case KEEPALIVE:
                {
                    //receive keepalive event, don't need to do anything
                    break;
                }
                case CACHEREBUILD:
                {
                    //rebuild all frame and cursors caches
                    rebuild_caches();
                    break;
                }
                case WINCHANGE:
                {
                    client_win_change(buff);
                    break;
                }
                default:
                {
                    EPHYR_DBG("UNSUPPORTED EVENT: %d",event_type);
                    /* looks like we have some corrupted data, let's try to reset event buffer */
                    remoteVars.evBufferOffset=0;
                    length=0;
                    break;
                }
            }
        }
//        EPHYR_DBG("Processed event - %d %d\n",eventnum++, eventbytes);
    }
    restDataLength=length%EVLENGTH;
    restDataPos=length-restDataLength;

    if(restDataLength)
       memcpy(remoteVars.eventBuffer, remoteVars.eventBuffer+restDataPos, restDataLength);
    remoteVars.evBufferOffset=restDataLength;

}

#define SubSend(pWin) \
((pWin->eventMask|wOtherEventMasks(pWin)) & SubstructureNotifyMask)

#define StrSend(pWin) \
((pWin->eventMask|wOtherEventMasks(pWin)) & StructureNotifyMask)

#define SubStrSend(pWin,pParent) (StrSend(pWin) || SubSend(pParent))


//this function is from dix/window.c
static void
ReflectStackChange(WindowPtr pWin, WindowPtr pSib, VTKind kind)
{
    /* Note that pSib might be NULL */
    Bool WasViewable = (Bool) pWin->viewable;
    Bool anyMarked;
    WindowPtr pFirstChange;
    WindowPtr pLayerWin;
    ScreenPtr pScreen = pWin->drawable.pScreen;
    /* if this is a root window, can't be restacked */
    if (!pWin->parent)
        return;
    pFirstChange = MoveWindowInStack(pWin, pSib);
    if (WasViewable) {
        anyMarked = (*pScreen->MarkOverlappedWindows) (pWin, pFirstChange,
                                                       &pLayerWin);
        if (pLayerWin != pWin)
            pFirstChange = pLayerWin;
        if (anyMarked) {
            (*pScreen->ValidateTree) (pLayerWin->parent, pFirstChange, kind);
            (*pScreen->HandleExposures) (pLayerWin->parent);
            if (pWin->drawable.pScreen->PostValidateTree)
                (*pScreen->PostValidateTree) (pLayerWin->parent, pFirstChange,
                                              kind);
        }
    }
    if (pWin->realized)
        WindowsRestructured();
}

void client_win_close(uint32_t winId)
{
    WindowPtr pWin;
    xEvent e;
    //     EPHYR_DBG("Client request win close: 0x%x",winId);
    pWin=remote_find_window_on_screen_by_id(winId, remoteVars.ephyrScreen->pScreen->root);
    if(!pWin)
    {
        EPHYR_DBG("Window with ID 0x%X not found on current screen",winId);
        return;
    }
    e.u.u.type = ClientMessage;
    e.u.u.detail = 32;
    e.u.clientMessage.window = winId;
    e.u.clientMessage.u.l.type = MakeAtom("WM_PROTOCOLS",strlen("WM_PROTOCOLS"),FALSE);
    e.u.clientMessage.u.l.longs0 = MakeAtom("WM_DELETE_WINDOW",strlen("WM_DELETE_WINDOW"),FALSE);
    e.u.clientMessage.u.l.longs1 = 0;
    e.u.clientMessage.u.l.longs2 = 0;
    e.u.clientMessage.u.l.longs3 = 0;
    e.u.clientMessage.u.l.longs4 = 0;
    DeliverEvents(pWin, &e, 1, NullWindow);
}

void client_win_iconify(uint32_t winId)
{
    WindowPtr pWin;
    xEvent e;
    EPHYR_DBG("Client request win iconify: 0x%x",winId);
    pWin=remote_find_window_on_screen_by_id(winId, remoteVars.ephyrScreen->pScreen->root);
    if(!pWin)
    {
        EPHYR_DBG("Window with ID 0x%X not found on current screen",winId);
        return;
    }
    e.u.u.type = ClientMessage;
    e.u.u.detail = 32;
    e.u.clientMessage.window = winId;
    e.u.clientMessage.u.l.type = MakeAtom("WM_CHANGE_STATE",strlen("WM_CHANGE_STATE"),FALSE);
    e.u.clientMessage.u.l.longs0 = 3;//iconicState
    e.u.clientMessage.u.l.longs1 = 0;
    e.u.clientMessage.u.l.longs2 = 0;
    e.u.clientMessage.u.l.longs3 = 0;
    e.u.clientMessage.u.l.longs4 = 0;
    DeliverEvents(pWin->parent, &e, 1, NullWindow);
}


void client_win_change(char* buff)
{
    WindowPtr pWin, pParent, pSib;
    uint32_t winId=*((uint32_t*)(buff+4));
    uint32_t newSibId=*((uint32_t*)(buff+8));
    struct remoteWindow* rw;
    int16_t x,y;
    uint16_t w,h,bw;
    int16_t nx=*((int16_t*)(buff+12));
    int16_t ny=*((int16_t*)(buff+14));
    uint16_t nw=*((int16_t*)(buff+16));
    uint16_t nh=*((int16_t*)(buff+18));
    uint8_t focus=*((int8_t*)(buff+20));
    uint8_t newstate=*((int8_t*)(buff+21));
    BOOL move=FALSE, resize=FALSE, restack=FALSE;

//     EPHYR_DBG("Client request win change: %p %d:%d %dx%d",fptr, nx,ny,nw,nh);
    pWin=remote_find_window_on_screen_by_id(winId, remoteVars.ephyrScreen->pScreen->root);
    if(!pWin)
    {
        EPHYR_DBG("Window with ID 0x%X not found on current screen",winId);
        return;
    }

    if(newstate==WIN_DELETED)
    {
        client_win_close(winId);
        return;
    }
    if(newstate==WIN_ICONIFIED)
    {
        client_win_iconify(winId);
        ReflectStackChange(pWin, 0, VTOther);
        return;
    }

    pParent = pWin->parent;
    pSib=0;
    if(newSibId)
    {
        pSib=remote_find_window_on_screen_by_id(newSibId, remoteVars.ephyrScreen->pScreen->root);
        if(!pSib)
        {
            EPHYR_DBG("New Sibling with ID 0x%X not found on current screen, Putting bellow all siblings",newSibId);
        }
    }
    w = pWin->drawable.width;
    h = pWin->drawable.height;
    bw = pWin->borderWidth;
    if (pParent)
    {
        x = pWin->drawable.x - pParent->drawable.x - (int16_t) bw;
        y = pWin->drawable.y - pParent->drawable.y - (int16_t) bw;
    }
    else
    {
        x = pWin->drawable.x;
        y = pWin->drawable.y;
    }
    if (SubStrSend(pWin, pParent))
    {
        xEvent event = {
            .u.configureNotify.window = pWin->drawable.id,
            .u.configureNotify.aboveSibling = pSib ? pSib->drawable.id : None,
            .u.configureNotify.x = x,
            .u.configureNotify.y = y,
            .u.configureNotify.width = w,
            .u.configureNotify.height = h,
            .u.configureNotify.borderWidth = bw,
            .u.configureNotify.override = pWin->overrideRedirect
        };
        event.u.u.type = ConfigureNotify;
        #ifdef PANORAMIX
        if (!noPanoramiXExtension && (!pParent || !pParent->parent)) {
            event.u.configureNotify.x += screenInfo.screens[0]->x;
            event.u.configureNotify.y += screenInfo.screens[0]->y;
        }
        #endif
        DeliverEvents(pWin, &event, 1, NullWindow);
    }

    pthread_mutex_lock(&remoteVars.sendqueue_mutex);
    rw=remote_find_window(pWin);
    if(!rw)
    {
        EPHYR_DBG("Error!!!! window %p not found in list of ext windows",pWin);
        pthread_mutex_unlock(&remoteVars.sendqueue_mutex);
        return;
    }

    if( x!=nx || y!=ny)
    {
        if(rw)
        {
            rw->x=nx;
            rw->y=ny;
        }
        move=TRUE;
    }
    if(w!=nw || h!=nh)
    {
        if(rw)
        {
            rw->w=nw;
            rw->h=nh;
        }
        resize=TRUE;
    }
    if(pWin->nextSib!=pSib)
    {
        rw->nextSib=pSib;
        if(pSib)
        {
            rw->nextSibId=pSib->drawable.id;
        }
        else
        {
            rw->nextSibId=0;
        }
        restack=TRUE;
    }
    pthread_mutex_unlock(&remoteVars.sendqueue_mutex);
    if(move)
    {
//         EPHYR_DBG("MOVE from %d:%d to %d:%d",x,y,nx,ny);
        (*pWin->drawable.pScreen->MoveWindow) (pWin, nx, ny, pSib,VTMove);
    }
    if(resize)
    {
        //         EPHYR_DBG("RESIZE from %dx%d to %dx%d",w,h,nw,nh);
        (*pWin->drawable.pScreen->ResizeWindow) (pWin, nx, ny, nw, nh, pSib);
    }
    if(restack)
    {
//         EPHYR_DBG("Client request to move : %p on top of %p",pWin, pSib);
        ReflectStackChange(pWin, pSib, VTOther);
    }
    if(rw->hasFocus!=focus)
    {
//         EPHYR_DBG("Focus changed for 0x%X",winId);
        rw->hasFocus=focus;
        if(focus)
        {
            SetInputFocus(wClient(pWin), inputInfo.keyboard, pWin->drawable.id,  RevertToParent, CurrentTime, TRUE);
        }
    }
}

void set_client_version(uint16_t ver, uint16_t os)
{
    remoteVars.client_version=ver;
    if(os > WEB)
    {
        EPHYR_DBG("Unsupported OS, assuming OS_LINUX");
    }
    else
        remoteVars.client_os=os;
    //clients version >= 1 supporting extended selection (sending big amount of data aín several chunks)
    remoteVars.selstruct.clientSupportsExetndedSelection=(ver > 1);
    //Linux clients supporting sending selection on demand (not sending data if not needed)
    //Web client support clipboard and selection on demand starting from v.4
    remoteVars.selstruct.clientSupportsOnDemandSelection=(((ver > 1) && (os == OS_LINUX)) || ((ver > 3) && (os == WEB)));

}

#if XORG_VERSION_CURRENT < 11900000
void pollEvents(void)
{
    //EPHYR_DBG("polling events");
    struct pollfd fds[2];
    int nfds = 1;
    BOOL client = FALSE;

    memset(fds, 0 , sizeof(fds));

    pthread_mutex_lock(&remoteVars.sendqueue_mutex);

    //We are in connected state, poll client socket
    if(remoteVars.client_connected)
    {
        client = TRUE;
        fds[0].fd = remoteVars.clientsock;
    }  //we are in connecting state, poll server socket
    else if(remoteVars.serversock != -1)
    {
        fds[0].fd = remoteVars.serversock;
    }
    else //not polling any sockets
    {
        pthread_mutex_unlock(&remoteVars.sendqueue_mutex);
        return;
    }

    pthread_mutex_unlock(&remoteVars.sendqueue_mutex);

    fds[0].events = POLLIN;
    if(poll(fds, nfds, 0))
    {
        if(client)
            clientReadNotify(fds[0].fd, 0, NULL);
        else
            serverAcceptNotify(fds[0].fd, 0, NULL);
    }
}
#endif /* XORG_VERSION_CURRENT */

unsigned int checkSocketConnection(OsTimerPtr timer, CARD32 time, void* args)
{
    EPHYR_DBG("CHECKING ACCEPTED CONNECTION");
    TimerFree(timer);

    pthread_mutex_lock(&remoteVars.sendqueue_mutex);
    remoteVars.checkConnectionTimer=0;
    if(!remoteVars.client_connected)
    {
        EPHYR_DBG("NO CLIENT CONNECTION SINCE %d seconds",ACCEPT_TIMEOUT);
        cancelBeforeStart();
    }
    else
    {
        EPHYR_DBG("CLIENT CONNECTED");
    }

    pthread_mutex_unlock(&remoteVars.sendqueue_mutex);
    return 0;
}

void serverAcceptNotify(int fd, int ready_sock, void *data)
{
    int ret;
    remoteVars.clientsock = accept ( remoteVars.serversock, (struct sockaddr *) &remoteVars.address, &remoteVars.addrlen );
    if (remoteVars.clientsock <= 0)
    {
        EPHYR_DBG( "ACCEPT ERROR OR CANCELD!\n");
        return;
    }
    EPHYR_DBG ("Connection from (%s)...\n", inet_ntoa (remoteVars.address.sin_addr));

    //only accept one client, close server socket
    close_server_socket();

    if(strlen(remoteVars.acceptAddr))
    {
        struct addrinfo hints, *res;
        int errcode;

        memset (&hints, 0, sizeof (hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags |= AI_CANONNAME;

        errcode = getaddrinfo (remoteVars.acceptAddr, NULL, &hints, &res);
        if (errcode != 0 || !res)
        {
            EPHYR_DBG ("ERROR LOOKUP %s", remoteVars.acceptAddr);
            terminateServer(-1);
        }
        if(  ((struct sockaddr_in *) (res->ai_addr))->sin_addr.s_addr != remoteVars.address.sin_addr.s_addr)
        {
            EPHYR_DBG("Connection only allowed from %s",inet_ntoa ( ((struct sockaddr_in *)(res->ai_addr))->sin_addr));
            shutdown(remoteVars.clientsock, SHUT_RDWR);
            close(remoteVars.clientsock);
            return;
        }
    }
    if(strlen(remoteVars.cookie))
    {
        char msg[33];
        int length=32;
        int ready=0;

        //            EPHYR_DBG("Checking cookie: %s",remoteVars.cookie);

        while(ready<length)
        {
            int chunk=read(remoteVars.clientsock, msg+ready, 32-ready);

            if(chunk<=0)
            {
                EPHYR_DBG("READ COOKIE ERROR");
                shutdown(remoteVars.clientsock, SHUT_RDWR);
                close(remoteVars.clientsock);
                return;
            }
            ready+=chunk;

            EPHYR_DBG("got %d COOKIE BYTES from client", ready);
        }
        if(strncmp(msg,remoteVars.cookie,32))
        {
            EPHYR_DBG("Wrong cookie");
            shutdown(remoteVars.clientsock, SHUT_RDWR);
            close(remoteVars.clientsock);
            return;
        }
        EPHYR_DBG("Cookie approved");
    }
    else
    {
        EPHYR_DBG("Warning: not checking client's cookie");
    }

    pthread_mutex_lock(&remoteVars.sendqueue_mutex);
    #if XORG_VERSION_CURRENT >= 11900000
    EPHYR_DBG("Set notify FD for client sock: %d",remoteVars.clientsock);
    SetNotifyFd(remoteVars.clientsock, clientReadNotify, X_NOTIFY_READ, NULL);
    #endif /* XORG_VERSION_CURRENT */
    remoteVars.client_connected=TRUE;
    remoteVars.server_version_sent=FALSE;
    set_client_version(0,0);
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

   //here start a send thread
    ret = pthread_create(&remoteVars.send_thread_id, NULL, send_frame_thread, (void *)remoteVars.send_thread_id);
    if (ret)
    {
        EPHYR_DBG("ERROR; return code from pthread_create() is %d\n", ret);
        terminateServer(-1);
    }
}


void close_server_socket(void)
{
#if XORG_VERSION_CURRENT >= 11900000
    EPHYR_DBG("Remove notify FD for server sock %d",remoteVars.serversock);
    RemoveNotifyFd(remoteVars.serversock);
#endif /* XORG_VERSION_CURRENT */
    shutdown(remoteVars.serversock, SHUT_RDWR);
    close(remoteVars.serversock);
    remoteVars.serversock=-1;
}


void open_socket(void)
{
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

#if XORG_VERSION_CURRENT >= 11900000
    EPHYR_DBG("Set notify FD for server sock: %d",remoteVars.serversock);
    EPHYR_DBG ("waiting for TCP connection\n");
    SetNotifyFd(remoteVars.serversock, serverAcceptNotify, X_NOTIFY_READ, NULL);
#endif /* XORG_VERSION_CURRENT */

    EPHYR_DBG("Server socket is ready");
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
    if(remoteVars.selstruct.selThreadId)
    {
        pthread_cancel(remoteVars.selstruct.selThreadId);
        remove_obsolete_incr_transactions(FALSE);
        if(remoteVars.selstruct.xcbConnection)
        {
            xcb_disconnect(remoteVars.selstruct.xcbConnection);
        }
        pthread_mutex_destroy(&remoteVars.selstruct.inMutex);
    }

    pthread_mutex_destroy(&remoteVars.mainimg_mutex);
    pthread_mutex_destroy(&remoteVars.sendqueue_mutex);
    pthread_cond_destroy(&remoteVars.have_sendqueue_cond);

    if(remoteVars.main_img)
    {
        free(remoteVars.main_img);
        free(remoteVars.second_buffer);
    }


    if(remoteVars.selstruct.inSelection[0].data)
    {
        free(remoteVars.selstruct.inSelection[0].data);
    }
    if(remoteVars.selstruct.inSelection[1].data)
    {
        free(remoteVars.selstruct.inSelection[1].data);
    }
    setAgentState(TERMINATED);
    EPHYR_DBG("exit program with status %d", exitStatus);

    GiveUp(SIGTERM);
}

static
void processConfigFileSetting(char* key, char* value)
{
//    EPHYR_DBG("process setting %s %s", key, value);
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
    else if(!strcmp(key, "clipboard"))
    {
        if(!strcmp(value,"client"))
        {
            remoteVars.selstruct.selectionMode=CLIP_CLIENT;
            EPHYR_DBG("CLIPBOARD MODE: client");
        }
        else if(!strcmp(value,"server"))
        {
            remoteVars.selstruct.selectionMode=CLIP_SERVER;
            EPHYR_DBG("CLIPBOARD MODE: server");
        }
        else if(!strcmp(value,"both"))
        {
            remoteVars.selstruct.selectionMode=CLIP_BOTH;
            EPHYR_DBG("CLIPBOARD MODE: both");
        }
        else
        {
            remoteVars.selstruct.selectionMode=CLIP_NONE;
            EPHYR_DBG("CLIPBOARD MODE: disabled");
        }
    }
}

void readOptionsFromFile(void)
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
//            EPHYR_DBG("%c",c);
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
            /* read file and get quality and state file */
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

    char* displayVar = NULL;

    /*init it in OsInit*/

    fclose(stdout);
    fclose(stdin);

    remoteVars.serversock=-1;

    remoteVars.jpegQuality=JPG_QUALITY;
    remoteVars.compression=DEFAULT_COMPRESSION;
    remoteVars.selstruct.selectionMode = CLIP_BOTH;

    pthread_mutex_init(&remoteVars.mainimg_mutex, NULL);
    pthread_mutex_init(&remoteVars.sendqueue_mutex,NULL);
    pthread_cond_init(&remoteVars.have_sendqueue_cond,NULL);

    displayVar=secure_getenv("DISPLAY");

    if(displayVar)
    {
        if(!strncmp(displayVar,"nx/nx,options=",strlen("nx/nx,options=")))
        {
            int i=strlen("nx/nx,options=");
            int j=0;

            EPHYR_DBG("running in NXAGENT MODE");
            remoteVars.nxagentMode=TRUE;
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
                            unsigned char* RGBA_buffer, uint32_t* png_size, BOOL compress_cursor)
{
    struct
    {
        uint32_t* size;
        unsigned char *out;
    } outdata;
    unsigned char** rows = NULL;
    int color_type;
    int bpp;
    png_structp p;
    png_infop info_ptr;

    if(compress_cursor)
    {
        color_type = PNG_COLOR_TYPE_RGB_ALPHA;
        bpp = 4;
    }
    else
    {
        color_type = PNG_COLOR_TYPE_RGB;
        bpp = CACHEBPP;
    }

    p = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

    info_ptr = png_create_info_struct(p);
    setjmp(png_jmpbuf(p));
    png_set_IHDR(p, info_ptr,image_width, image_height, 8,
                         color_type,
                         PNG_INTERLACE_NONE,
                         PNG_COMPRESSION_TYPE_DEFAULT,
                         PNG_FILTER_TYPE_DEFAULT);


    *png_size=0;

    rows = calloc(sizeof(unsigned char*), image_height);

    outdata.size=png_size;
    outdata.out=0;

    for (uint32_t y = 0; y < image_height; ++y)
        rows[y] = (unsigned char*)RGBA_buffer + y * image_width * bpp;


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
       return png_compress(image_width, image_height, RGBA_buffer, compressed_size, FALSE);
}

static
struct cache_elem* add_cache_element(uint32_t crc, int32_t dx, int32_t dy, uint32_t size, uint32_t width, uint32_t height)
{
    struct cache_elem* el=malloc(sizeof(struct cache_elem));
    bzero(el, sizeof(struct cache_elem));
    el->crc=crc;
    el->width=width;
    el->height=height;

//    if(CACHEBPP==4)
//    {
//        el->size=size;
//        el->data=malloc(size);
//        remoteVars.cache_size+=size;
//        memcpy(el->data, data, size);
//    }
//    else
    {

        uint32_t i=0;
        uint32_t numOfPix=size/4;
        uint32_t isize=size/4*3;

        el->size=isize;
//        EPHYR_DBG("SIZE %d %d %d, %d, %d",isize, dx, dy, width, height);
        el->data=malloc(isize);
        if(!el->data)
        {
            EPHYR_DBG("error allocating data for frame");
            exit(-1);
        }
        remoteVars.cache_size+=isize;

        /* copy RGB channels of every pixel and ignore A channel */

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
//    EPHYR_DBG("\ncache elements %d, cache size %u(%dMB) %u, %u, %u\n", cache_elements, cache_size, (int) (cache_size/1024/1024), el->rval,
//              el->gval, el->bval);
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



/*
 * this function looking for the cache elements with specified crc
 * if the element is found we moving it in the end of list
 * in this case we keeping the most recent elements in the tail of
 * list for faster search
 */
static
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

static
void initFrameRegions(struct cache_elem* frame)
{
    BOOL haveMultplyRegions=FALSE;
    int32_t length=0;

    struct frame_region* regions=frame->regions;
    BOOL diff;

    if(frame->width>4 && frame->height>4 && frame->width * frame->height > 100 )
    {
        unsigned int match_val = 0;
        uint32_t bestm_crc = 0;
        struct cache_elem* best_match = NULL;


        pthread_mutex_lock(&remoteVars.sendqueue_mutex);
        best_match = find_best_match(frame, &match_val);

        if(best_match)
        {
            best_match->busy+=1;
        }

        pthread_mutex_unlock(&remoteVars.sendqueue_mutex);

        if(best_match && best_match->width>4 && best_match->height>4 && best_match->width * best_match->height > 100 )
        {
            bestm_crc=best_match->crc;
            if(best_match && match_val<=MAX_MATCH_VAL)
            {

                rectangle rect = {{0}};
                int hshift, vshift = 0;

                if(find_common_regions(best_match, frame, &diff, &rect, &hshift, &vshift))
                {
                    haveMultplyRegions=TRUE;
                    if(!diff)
                    {
                        int prev = -1;

//                        EPHYR_DBG("SOURCE: %x %d:%d - %dx%d- shift %d, %d",bestm_crc,
//                                  rect.lt_corner.x, rect.lt_corner.y,
//                                  rect.size.width, rect.size.height, hshift, vshift);

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

                        /* regions[0] represents common with bestmatch region */
                        regions[0].source_coordinates.x=0;
                        regions[0].source_coordinates.y=0;
                        regions[0].source_crc=bestm_crc;
                        frame->source=best_match;
                        memcpy(&(regions[1].rect), &rect, sizeof(rectangle));
                    }
                }
            }
        }
        /* if we didn't find any common regions and have best match element, mark it as not busy */


        pthread_mutex_lock(&remoteVars.sendqueue_mutex);
        if(best_match && frame->source != best_match)
        {
//            EPHYR_DBG("Have best mutch but not common region");
            best_match->busy-=1;
        }

        pthread_mutex_unlock(&remoteVars.sendqueue_mutex);

    }

    if(!haveMultplyRegions)
    {
//        EPHYR_DBG("HAVE SINGLE REGION");

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

//                    uint32_t send_data=regions[i].rect.size.width*regions[i].rect.size.height;
//                    uint32_t total_data=frame->width*frame->height;
//                    EPHYR_DBG("saved space: %d%% %d %d %dx%d %dx%d", 100 - (int)((send_data*1./total_data)*100), total_data, send_data,frame->width, frame->height,
//                              regions[i].rect.size.width,regions[i].rect.size.height);

//                    #warning check this
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

            char fname[255];
//            #warning check this
            uint8_t *data=malloc(regions[1].rect.size.width*regions[1].rect.size.height*CACHEBPP);

            for(int line=0;line<regions[1].rect.size.height;++line)
            {
                memcpy(data+line*regions[1].rect.size.width*CACHEBPP,
                       frame->data+((regions[1].rect.lt_corner.y+line)*
                       frame->width+regions[1].rect.lt_corner.x)*CACHEBPP,
                       regions[1].rect.size.width*CACHEBPP);

            }
            sprintf(fname,"/tmp/.x2go/x2gokdrive_dbg/%x-rect_inv.jpg",frame->crc);
            regions[1].compressed_data=image_compress(regions[1].rect.size.width,
                                                     regions[1].rect.size.height,
                                                     data, &regions[1].size, CACHEBPP, fname);
            length+=regions[1].size;
            free(data);
        }
    }
    frame->compressed_size=length;
}

void add_frame(uint32_t width, uint32_t height, int32_t x, int32_t y, uint32_t crc, uint32_t size, uint32_t winId)
{
    Bool isNewElement = FALSE;
    struct cache_elem* frame = 0;
    struct sendqueue_element* element = NULL;


    pthread_mutex_lock(&remoteVars.sendqueue_mutex);
    if(! (remoteVars.client_connected && remoteVars.client_initialized) || remoteVars.cache_rebuilt)
    {
        /* don't have any clients connected, or cache rebuild is requested, return */
        pthread_mutex_unlock(&remoteVars.sendqueue_mutex);
        return;
    }

    if(crc==0)
    {
        /* sending main image */

        pthread_mutex_unlock(&remoteVars.sendqueue_mutex);
    }
    else
    {

        frame=find_cache_element(crc);
        if(!frame)
        {
//            EPHYR_DBG("ADD NEW FRAME %x",crc);
            frame=add_cache_element(crc, x, y, size, width, height);
            isNewElement=TRUE;
        }
        else
        {
//            EPHYR_DBG("ADD EXISTING FRAME %x",crc);
        }
        frame->busy+=1;


        pthread_mutex_unlock(&remoteVars.sendqueue_mutex);

        /* if element is new find common regions and compress the data */
        if(isNewElement)
        {
            /* find bestmatch and lock it */
            initFrameRegions(frame);
        }
    }


    pthread_mutex_lock(&remoteVars.sendqueue_mutex);
    /* add element in the queue for sending */
    element=malloc(sizeof(struct sendqueue_element));
    element->frame=frame;
    element->next=NULL;
    element->x=x;
    element->y=y;
    element->width=width;
    element->height=height;
    element->winId=winId;
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
    /* on this point will be sent wakeup single to send mutex */
}


void remote_send_main_image(void)
{
    add_frame(0, 0, 0, 0, 0, 0,0);
}

struct remoteWindow* remote_find_window(WindowPtr win)
{
    struct remoteWindow* rw=remoteVars.windowList;
//     EPHYR_DBG("LOOK for %p in list",win);
    while(rw)
    {
        if(rw->ptr==win)
        {
            return rw;
        }
        rw=rw->next;
    }
//     EPHYR_DBG("WINDOW %p not found in list",win);
    return NULL;
}

void remote_check_window(WindowPtr win)
{
    char *name=NULL;
    int nameSize=0;
    char *netName=NULL;
    int netNameSize=0;
    char *dispName=NULL;
    int dispNameSize=0;
    BOOL ignore = TRUE;
    BOOL hasFocus=FALSE;
    struct remoteWindow* rwin;
    uint32_t transWinId=0;
    uint8_t winType=WINDOW_TYPE_NORMAL;
    int16_t x,y,i, minw=0, minh=0;
    uint16_t w,h,bw;
    uint32_t iw,ih;
    uint32_t max_icon_w=0, max_icon_h=0;
    unsigned char *icon_data;
    uint32_t focusWinId=0;
//     ExWMHints* wmhints;
    ExSizeHints* sizehints;
    FocusClassPtr focus = inputInfo.keyboard->focus;
    WindowPtr parPtr;
    WindowPtr nextSibPtr, tmpPtr;
    parPtr=win->parent;
    nextSibPtr=NULL;

    //if prentwindow is root, set it to 0
    if(parPtr == remoteVars.ephyrScreen->pScreen->root)
    {
        parPtr=NULL;
    }
    w = win->drawable.width;
    h = win->drawable.height;
    bw = win->borderWidth;
    if (win->parent)
    {
        x = win->drawable.x - win->parent->drawable.x - (int) bw;
        y = win->drawable.y - win->parent->drawable.y - (int) bw;
    }
    else
    {
        x = win->drawable.x;
        y = win->drawable.y;
    }

//     EPHYR_DBG("Check win %p",win);
    if(!win->optional || !win->optional->userProps || !win->mapped)
    {
        return;
    }
    //find nextSib
    tmpPtr=win->nextSib;
    while(tmpPtr)
    {
        if(remote_find_window(tmpPtr))
        {
            nextSibPtr=tmpPtr;
            break;
        }
        tmpPtr=tmpPtr->nextSib;
    }
    /*
     * if window is not in list, create and send to client. If not same, update and send to client
     * if some list windows not there anymore, delete and send notification
     */
    if (focus->win == NoneWin)
        focusWinId = None;
    else if (focus->win == PointerRootWin)
        focusWinId = PointerRoot;
    else
        focusWinId = focus->win->drawable.id;
    hasFocus=(win->drawable.id==focusWinId);
    if(win->optional && win->optional->userProps)
    {
        PropertyPtr prop=win->optional->userProps;
//         EPHYR_DBG("======%x - PARENT %p = VIS %d === MAP %d =============",win->drawable.id, parPtr, win->visibility, win->mapped);
        while(prop)
        {
            if(prop->propertyName==MakeAtom("WM_NAME", strlen("WM_NAME"),FALSE) && prop->data)
            {
                name=prop->data;
                nameSize=prop->size;
            }
            if(prop->propertyName==MakeAtom("WM_HINTS", strlen("WM_HINTS"),FALSE) && prop->data)
            {
                ignore=FALSE;
            }
            if(prop->propertyName==MakeAtom("_NET_WM_NAME", strlen("_NET_WM_NAME"),FALSE) && prop->data)
            {
                netName=prop->data;
                netNameSize=prop->size;
            }
            if(prop->propertyName==MakeAtom("WM_TRANSIENT_FOR", strlen("WM_TRANSIENT_FOR"),FALSE) && prop->data)
            {
                transWinId=((uint32_t*)prop->data)[0];
//                 EPHYR_DBG("Trans Win 0x%X = 0x%X", transWinId, win->drawable.id);
            }
            if(prop->propertyName==MakeAtom("_NET_WM_ICON", strlen("_NET_WM_ICON"),FALSE) && prop->data)
            {
//                 EPHYR_DBG("_NET_WM_ICON size: %d",prop->size);
                i=0;
                while(i<prop->size/4)
                {
                    iw=((uint32_t*)prop->data)[i++];
                    ih=((uint32_t*)prop->data)[i++];
                    if(!iw || !ih ||iw>256 ||  ih>256)
                        break;
//                     EPHYR_DBG("ICON: %dx%d", iw, ih);
                    if(iw>max_icon_w)
                    {
                        max_icon_w=iw;
                        max_icon_h=ih;
                        icon_data=(unsigned char*)prop->data+i*4;
                    }
                    i+=iw*ih;
                }

            }
            //             EPHYR_DBG("%s %s, Format %d, Size %d",NameForAtom(prop->propertyName), NameForAtom(prop->type), prop->format, prop->size);
//             if( prop->type == MakeAtom("STRING", strlen("STRING"),FALSE) || prop->type == MakeAtom("UTF8_STRING", strlen("UTF8_STRING"),FALSE))
//             {
// //                 EPHYR_DBG("-- %s",(char*)prop->data);
//             }
            if( prop->type == MakeAtom("ATOM", strlen("ATOM"),FALSE))
            {
                ATOM* at=prop->data;
//                 if(prop->propertyName==MakeAtom("_NET_WM_STATE", strlen("_NET_WM_STATE"),FALSE))
//                 {
//                     for(i=0;i<prop->size;++i)
//                     {
// //                         EPHYR_DBG("--WINDOW STATE[%d]: %s, my ID 0x%X",i, NameForAtom( at[i] ), win->drawable.id);
//                     }
//                 }
                    //                 EPHYR_DBG("--  %s",NameForAtom( at[0] ));
                if(prop->propertyName==MakeAtom("_NET_WM_WINDOW_TYPE", strlen("_NET_WM_WINDOW_TYPE"),FALSE))
                {
//                     EPHYR_DBG("WINDOW Type: %s, my ID 0x%X",NameForAtom( at[0] ), win->drawable.id);
                    if(at[0]==MakeAtom("_NET_WM_WINDOW_TYPE_NORMAL", strlen("_NET_WM_WINDOW_TYPE_NORMAL"),FALSE))
                    {
//                         EPHYR_DBG("Normal window");
                        winType=WINDOW_TYPE_NORMAL;
                    }
                    else if(at[0] ==MakeAtom("_NET_WM_WINDOW_TYPE_DIALOG", strlen("_NET_WM_WINDOW_TYPE_DIALOG"),FALSE))
                    {
//                         EPHYR_DBG("Dialog");
                        winType=WINDOW_TYPE_DIALOG;
                    }
                    else if(at[0] ==MakeAtom("_NET_WM_WINDOW_TYPE_DROPDOWN_MENU", strlen("_NET_WM_WINDOW_TYPE_DROPDOWN_MENU"),FALSE))
                    {
//                         EPHYR_DBG("Dropdown menu");
                        winType=WINDOW_TYPE_DROPDOWN_MENU;
                    }
                    else if(at[0] ==MakeAtom("_NET_WM_WINDOW_TYPE_POPUP_MENU", strlen("_NET_WM_WINDOW_TYPE_POPUP_MENU"),FALSE))
                    {
//                         EPHYR_DBG("Popup menu");
                        winType=WINDOW_TYPE_POPUP_MENU;
                    }
                    else if( at[0] ==MakeAtom("_NET_WM_WINDOW_TYPE_SPLASH", strlen("_NET_WM_WINDOW_TYPE_SPLASH"),FALSE))
                    {
                        //                         EPHYR_DBG("Splash");
                        winType=WINDOW_TYPE_SPLASH;
                    }
                    else if( at[0] ==MakeAtom("_NET_WM_WINDOW_TYPE_TOOLTIP", strlen("_NET_WM_WINDOW_TYPE_TOOLTIP"),FALSE))
                    {
                        winType=WINDOW_TYPE_TOOLTIP;
                    }
                    else if( at[0] ==MakeAtom("_NET_WM_WINDOW_TYPE_COMBO", strlen("_NET_WM_WINDOW_TYPE_COMBO"),FALSE))
                    {
                        winType=WINDOW_TYPE_COMBO;
                    }
                    else if( at[0] ==MakeAtom("_NET_WM_WINDOW_TYPE_UTILITY", strlen("_NET_WM_WINDOW_TYPE_UTILITY"),FALSE))
                    {
                        winType=WINDOW_TYPE_UTILITY;
                    }
                }
            }
//             if(prop->propertyName==MakeAtom("WM_STATE", strlen("WM_STATE"),FALSE) && prop->data)
//             {
// //                 EPHYR_DBG("-- WM_STATE: %d",*((uint32_t*)(prop->data)));
//             }
            /*            if(prop->propertyName==MakeAtom("WM_NAME", strlen("WM_NAME"),FALSE) && prop->data)
            {
//                 EPHYR_DBG("-- Name: %s",(char*)prop->data);
            }
            if(prop->propertyName==MakeAtom("WM_WINDOW_ROLE", strlen("WM_WINDOW_ROLE"),FALSE) && prop->data)
            {
//                 EPHYR_DBG("-- Role: %s",(char*)prop->data);
            }
            if(prop->propertyName==MakeAtom("WM_CLASS", strlen("WM_CLASS"),FALSE) && prop->data)
            {
//                 EPHYR_DBG("-- Class: %s",(char*)prop->data);
            }
            if(prop->propertyName==MakeAtom("WM_PROTOCOLS", strlen("WM_PROTOCOLS"),FALSE) && prop->data)
            {
                 ATOM* at=prop->data;
                 EPHYR_DBG("-- WM_PROTOCOLS: %s",NameForAtom( at[0] ));
            }
            if(prop->propertyName==MakeAtom("WM_HINTS", strlen("WM_HINTS"),FALSE))
            {
                EPHYR_DBG("--WM HINTS:");
                wmhints=(ExWMHints*)prop->data;
                if(wmhints->flags & ExInputHint)
                {
                    EPHYR_DBG("     Input: %d",wmhints->input);
                }
                if(wmhints->flags & ExStateHint)
                {
                    EPHYR_DBG("     State: %d",wmhints->initial_state);
                }
            }*/
            if(prop->propertyName==MakeAtom("WM_NORMAL_HINTS", strlen("WM_NORMAL_HINTS"),FALSE))
            {
//                 EPHYR_DBG("--SIZE HINTS:");
                sizehints=(ExSizeHints*)prop->data;
                if(sizehints[0].flags & ExPMinSize)
                {
                    minw=sizehints->min_width;
                    minh=sizehints->min_height;
//                     EPHYR_DBG("     Min Size: %dx%d",sizehints->min_width, sizehints->min_height);
                }
                if(sizehints[0].flags & ExUSSize)
                {
//                     EPHYR_DBG("     User Size: need calc");
                }
            }
            prop=prop->next;
        }
    }
    if(ignore)
    {
//         EPHYR_DBG("=======IGNORE THIS WINDOW==============");
        return;
    }
    if(netNameSize && netName)
    {
        dispName=netName;
        dispNameSize=netNameSize;
    }
    else
    {
        dispName=name;
        dispNameSize=nameSize;
    }
//     EPHYR_DBG("\n\nWIN: %p, %s, PAR: %p,  %d:%d %dx%d, border %d, visibility: %d, view %d, map %d ID %d", win, dispName, parPtr, x, y,
//               w, h, bw,  win->visibility, win->viewable, win->mapped, win->drawable.id);

    if(winType == WINDOW_TYPE_NORMAL && transWinId)
    {
        winType=WINDOW_TYPE_DIALOG;
    }
    rwin=remote_find_window(win);
    if(!rwin)
    {
        /*create new window and put it as a first element of the list*/
        rwin=malloc(sizeof(struct remoteWindow));
        rwin->state=NEW;
        rwin->ptr=win;
        rwin->next=remoteVars.windowList;
        remoteVars.windowList=rwin;
        rwin->name=NULL;
        rwin->icon_png=NULL;
        rwin->icon_size=0;
        rwin->minw=minw;
        rwin->minh=minh;
        if(max_icon_w && (winType==WINDOW_TYPE_NORMAL || winType==WINDOW_TYPE_DIALOG))
        {
            rwin->icon_png=png_compress( max_icon_w, max_icon_h,
                                         icon_data, &rwin->icon_size, TRUE);
        }
        else
            max_icon_w=0;


//         EPHYR_DBG("Add to list: %p, %s, %d:%d %dx%d, visibility: %d", rwin->ptr, rwin->name, rwin->x,rwin->y,
//                            rwin->w, rwin->h, rwin->visibility);
    }
    else
    {
//         EPHYR_DBG("found in list: %p, %s, %d:%d %dx%d, visibility: %d", rwin->ptr, rwin->name, rwin->x,rwin->y,
//                     rwin->w, rwin->h,rwin->visibility);

        if(rwin->name || dispName)
        {
            if(rwin->name==NULL && dispName)
            {
                rwin->state=CHANGED;
            }else if(strlen(rwin->name)!=dispNameSize)
            {
                rwin->state=CHANGED;
            }else if (strncmp(rwin->name,dispName, dispNameSize))
            {
                rwin->state=CHANGED;
            }
        }

        if(rwin->x != x || rwin->y !=  y ||
            rwin->w !=  w || rwin->h !=  h || rwin->bw != bw || rwin->visibility != win->visibility)
        {
            rwin->state=CHANGED;
        }
        if(rwin->parent != parPtr || rwin->nextSib != nextSibPtr)
        {
//             EPHYR_DBG("STACK order changed for %p %s old parent %p new parent %p, old nextsib %p, new nextsib %p", rwin->ptr, rwin->name, rwin->parent, parPtr, rwin->nextSib, nextSibPtr);
            rwin->state=CHANGED;
        }
        if(rwin->visibility!=win->visibility)
        {
//             EPHYR_DBG("Visibilty changed for 0x%X from %d to %d", rwin->id, rwin->visibility, win->visibility);
            rwin->state=CHANGED;
        }
        if(rwin->hasFocus!=hasFocus)
        {
//             EPHYR_DBG("Focus changed for 0x%X from %d to %d", rwin->id, rwin->hasFocus, hasFocus);
        }
    }

    rwin->hasFocus=hasFocus;


    rwin->foundInWinTree=TRUE;
    rwin->x=x;
    rwin->y=y;
    rwin->w=w;
    rwin->h=h;
    rwin->bw=bw;
    rwin->visibility=win->visibility;
    rwin->parent=parPtr;
    rwin->nextSib=nextSibPtr;
    rwin->winType=winType;
    rwin->id=win->drawable.id;
    rwin->transWinId=transWinId;
    if(rwin->parent)
        rwin->parentId=rwin->parent->drawable.id;
    else
        rwin->parentId=0;
    if(rwin->nextSib)
        rwin->nextSibId=rwin->nextSib->drawable.id;
    else
        rwin->nextSibId=0;

    if(!rwin->name || strlen(rwin->name)!=dispNameSize || strncmp(rwin->name,dispName,dispNameSize))
    {
        if(rwin->name)
        {
            free(rwin->name);
            rwin->name=NULL;
        }
        if(dispNameSize)
        {
            rwin->name=malloc(dispNameSize+1);
            strncpy(rwin->name, dispName, dispNameSize);
            rwin->name[dispNameSize]='\0';
        }
    }
    if(rwin->state != UNCHANGED)
    {
        remoteVars.windowsUpdated=TRUE;
        if(rwin->state==NEW)
        {
//             EPHYR_DBG("NEW WINDOW %p, %s, %d:%d %dx%d bw-%d, visibility: %d parent %p nextSib %p",rwin->ptr, rwin->name, rwin->x,rwin->y,
//                       rwin->w, rwin->h, rwin->bw ,rwin->visibility, rwin->parent, rwin->nextSib);
        }
        else
        {
//             EPHYR_DBG("WINDOW CHANGED:");
        }
/*        if(rwin->name &&( !strncmp (rwin->name, "xterm",4)))
        {
            EPHYR_DBG("=======WIN: %p, %s, %d:%d %dx%d bw-%d, visibility: %d parent %p nextSib %p", rwin->ptr, rwin->name, rwin->x,rwin->y,
                   rwin->w, rwin->h, rwin->bw ,rwin->visibility, rwin->parent, rwin->nextSib);

        }*/

        //only for debug purpose
        /*
        EPHYR_DBG("CURRENT STACK:");
        WindowPtr wn = remoteVars.ephyrScreen->pScreen->root->firstChild;
        while(wn)
        {
            char* nm=NULL;
            struct remoteWindow* rw=remote_find_window(wn);
            if(rw)
            {
                nm=rw->name;
            }
            if(wn->mapped)
            {
              EPHYR_DBG("%p (%s) mapped: %d",wn,nm,wn->mapped);
            }
            wn=wn->nextSib;
        }
        EPHYR_DBG("END OF STACK:");*/
        //
    }
//     EPHYR_DBG("=====FOCUS WIN ID 0x%X=====================",focusWinId);
}

void remote_check_windowstree(WindowPtr root)
{
    WindowPtr child;
    child=root->firstChild;
    while(child)
    {
        remote_check_windowstree(child);
        remote_check_window(child);
        child=child->nextSib;
    }
}

WindowPtr remote_find_window_on_screen_by_id(uint32_t winId, WindowPtr root)
{
    WindowPtr child;
    child=root->firstChild;
    while(child)
    {
        if(child->drawable.id == winId)
        {
            return child;
        }
        if(remote_find_window_on_screen_by_id(winId,child))
        {
            return child;
        }
        child=child->nextSib;
    }
    return NULL;
}

WindowPtr remote_find_window_on_screen(WindowPtr win, WindowPtr root)
{
    WindowPtr child;
    child=root->firstChild;
    while(child)
    {
        if(child == win)
        {
            return win;
        }
        if(remote_find_window_on_screen(win,child))
        {
            return win;
        }
        child=child->nextSib;
    }
    return NULL;
}

void remote_check_rootless_windows_for_updates(KdScreenInfo *screen)
{
    struct remoteWindow* rwin;
    pthread_mutex_lock(&remoteVars.sendqueue_mutex);

    //don't check windows if no client is connected
    if(remoteVars.client_connected==FALSE)
    {
        pthread_mutex_unlock(&remoteVars.sendqueue_mutex);
        return;
    }

    //         EPHYR_DBG("START TREE CHECK");
    remote_check_windowstree(screen->pScreen->root);

    //check all windows in list and mark as deleted if not found in the tree
    rwin=remoteVars.windowList;
    while(rwin)
    {
        if(!rwin->foundInWinTree)
        {
            remoteVars.windowsUpdated=TRUE;
            //mark window as deleted
            rwin->state=WDEL;
//             EPHYR_DBG("DELETED WINDOW:  %p, %s",rwin->ptr, rwin->name);
        }
        rwin->foundInWinTree=FALSE;
        rwin=rwin->next;
    }
    pthread_mutex_unlock(&remoteVars.sendqueue_mutex);
}

void
remote_paint_rect(KdScreenInfo *screen,
                  int sx, int sy, int dx, int dy, int width, int height)
{

//    EphyrScrPriv *scrpriv = screen->driver;


    uint32_t size=width*height*XSERVERBPP;
    if(remoteVars.rootless)
    {
        remote_check_rootless_windows_for_updates(screen);
    }
    if(size)
    {
        int32_t dirtyx_max = 0;
        int32_t dirtyy_max = 0;

        int32_t dirtyx_min = 0;
        int32_t dirtyy_min = 0;

        char maxdiff = 0;
        char mindiff = 0;

//        EPHYR_DBG("REPAINT %dx%d sx %d, sy %d, dx %d, dy %d", width, height, sx, sy, dx, dy);

        dirtyx_max=dx-1;
        dirtyy_max=dy-1;

        dirtyx_min=dx+width;
        dirtyy_min=dy+height;

        /*
         * OK, here we assuming that XSERVERBPP is 4. If not, we'll have troubles
         * but it should work faster like this
         */

        pthread_mutex_lock(&remoteVars.mainimg_mutex);

        maxdiff=2;
        mindiff=-2;

        /* check if updated rec really is as big */
        for(int32_t y=dy; y< dy+height;++y)
        {
            uint32_t ind=(y*remoteVars.main_img_width+dx)*XSERVERBPP;
            for(int32_t x=dx;x< dx+width; ++x)
            {
                BOOL pixIsDirty=FALSE;
                //CHECK R-COMPONENT
                int16_t diff=remoteVars.main_img[ind]-remoteVars.second_buffer[ind];

//              if(x > 250 && x<255 && y >5 && y< 7)
//              {
//                  EPHYR_DBG("rdiff %d - %u %u ", diff, remoteVars.main_img[ind],remoteVars.second_buffer[ind]);
//              }
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


//        EPHYR_DBG("DIRTY %d,%d - %d,%d", dirtyx_min, dirtyy_min, dirtyx_max, dirtyy_max);


        width=dirtyx_max-dirtyx_min+1;
        height=dirtyy_max-dirtyy_min+1;
//        int oldsize=size;
        size=width*height*XSERVERBPP;
        if(width<=0 || height<=0||size<=0)
        {
//            EPHYR_DBG("NO CHANGES DETECTED, NOT UPDATING");
            return;
        }
        dx=sx=dirtyx_min;
        dy=sy=dirtyy_min;

//        if(size!=oldsize)
//        {
//            EPHYR_DBG("new update rect dimensions: %dx%d", width, height);
//        }

        add_frame(width, height, dx, dy, calculate_crc(width, height,dx,dy), size,0);
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

void remote_set_display_name(const char* name)
{
    int max_len=256;
    if(strlen(name)<max_len)
    {
        max_len=strlen(name);
    }
    strncpy(remoteVars.displayName, name, max_len);
    remoteVars.displayName[max_len]='\0';
    EPHYR_DBG("DISPLAY name: %s",remoteVars.displayName);
}

void remote_set_rootless(void)
{
    EPHYR_DBG("Running in ROOTLESS mode");
    remoteVars.rootless=TRUE;
}

void *
remote_screen_init(KdScreenInfo *screen,
                  int x, int y,
                  int width, int height, int buffer_height,
                  int *bytes_per_line, int *bits_per_pixel)
{
    EphyrScrPriv *scrpriv = screen->driver;

    //We should not install callback at first screen init, it was installed by selection_init
    //but we need to reinstall it by the next screen init.

    EPHYR_DBG("REMOTE SCREEN INIT!!!!!!!!!!!!!!!!!!");
    if(remoteVars.selstruct.threadStarted)
    {
        EPHYR_DBG("SKIPPING Selection CALLBACK INSTALL");
//         remoteVars.selstruct.callBackInstalled=FALSE;
    }
    else
    {
        EPHYR_DBG("REINSTALL CALLBACKS");
        install_selection_callbacks();
    }

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

void rebuild_caches(void)
{
    EPHYR_DBG("CLIENT REQUESTED CLEARING ALL CACHES AND QUEUES");
    pthread_mutex_lock(&remoteVars.sendqueue_mutex);
    clear_send_queue();
    clear_frame_cache(0);
    freeCursors();
    delete_all_windows();
    remoteVars.cache_rebuilt=TRUE;
    pthread_cond_signal(&remoteVars.have_sendqueue_cond);
    pthread_mutex_unlock(&remoteVars.sendqueue_mutex);
}
