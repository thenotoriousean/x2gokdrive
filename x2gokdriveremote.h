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


#ifndef X2GOKDRIVE_REMOTE_H
#define X2GOKDRIVE_REMOTE_H

#include <X11/X.h>
#include <X11/Xmd.h>
#include <xcb/xcb.h>
#include <xcb/render.h>
#include "x2gokdrive.h"
#include "kdrive.h"

#include "cursorstr.h"
#include "input.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>

#include <string.h>             /* for memset */
#include <errno.h>
#include <time.h>
#include <err.h>

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/time.h>


#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>


#include <X11/keysym.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_aux.h>
#include <xcb/shm.h>
#include <xcb/xcb_image.h>
#include <xcb/shape.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/randr.h>
#include <xcb/xkb.h>
#include "x2gokdrivelog.h"


#include <jpeglib.h>
#include <png.h>
#include <zlib.h>


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#if XORG_VERSION_CURRENT < 11900000
#include <poll.h>
#endif /* XORG_VERSION_CURRENT */


//FEATURE_VERSION is not cooresponding to actual version of server
//it used to tell server which features are supported by server
//Changes 0 - 1: sending and recieving client and OS version
//Changes 1 - 2: supporting extended selection and sending selection on demand
#define FEATURE_VERSION 2


#define EPHYR_WANT_DEBUG 1
// #warning DEBUG ENABLED


#if (EPHYR_WANT_DEBUG)
#define EPHYR_DBG(x, a...) \
fprintf(stderr, __FILE__ ":%d,%s() " x "\n", __LINE__, __func__, ##a)
#else
#define EPHYR_DBG(x, a...) do {} while (0)
#endif


#define MAXMSGSIZE 1024*16

//port to listen by default
#define DEFAULT_PORT 15000

#define ACCEPT_TIMEOUT 30000 //msec

//if true, will save compressed jpg in file
#define JPGDEBUG FALSE

//it define how close should be two pages to search common regions (see find_best_match)
#define MAX_MATCH_VAL 51

#define JPG_QUALITY 80



//always 4
#define XSERVERBPP 4

enum msg_type{FRAME,DELETED, CURSOR, DELETEDCURSOR, SELECTION, SERVERVERSION, DEMANDCLIENTSELECTION};
enum AgentState{STARTING, RUNNING, RESUMING, SUSPENDING, SUSPENDED, TERMINATING, TERMINATED};
enum Compressions{JPEG,PNG};
enum SelectionType{PRIMARY,CLIPBOARD};
enum SelectionMime{STRING,UTF_STRING,PIXMAP};
enum ClipboardMode{CLIP_NONE,CLIP_CLIENT,CLIP_SERVER,CLIP_BOTH};
enum OS_VERSION{OS_LINUX, OS_WINDOWS, OS_DARWIN};

#define DEFAULT_COMPRESSION JPEG

// could be 3 or 4
// 3 saves some memory because not keeping A-value, which is always 0 anyway.
// 4 doesn't need to do transformation between XImage and internal data.
// I think we should always use 3
#define CACHEBPP 3


#define CACHEMAXSIZE 50*1024*1024 //50MB
#define CACHEMAXELEMENTS 200 //store max 200 elements in cache

//Events
#define KEYPRESS 2
#define KEYRELEASE 3
#define MOUSEPRESS 4
#define MOUSERELEASE 5
#define MOUSEMOTION 6
#define GEOMETRY 7
#define UPDATE 8
#define SELECTIONEVENT 9
#define CLIENTVERSION 10
#define DEMANDSELECTION 11

#define EVLENGTH 41


typedef struct
{
    int32_t x;
    int32_t y;
}
point_t;

typedef struct
{
    int32_t width;
    int32_t height;
}rect_size;

typedef struct
{
    point_t lt_corner;
    rect_size size;
}rectangle;

struct cache_elem;

// represents frame regions for frames with multiply regions
struct frame_region
{
    uint8_t* compressed_data;
    uint32_t size;
    rectangle rect;
    uint32_t source_crc;
    point_t source_coordinates;
};


//we can find out cursor type by analyzing size of data.
//size = 0 - cursor is already sent to client
//size = width*height*4 - RGBA cursor
//size = width*height/8*2 - Core cursor
struct cursorFrame
{
    uint32_t size;
    char* data;
    uint16_t width;
    uint16_t height;
    uint16_t xhot;
    uint16_t yhot;

    uint8_t forR, forG, forB;
    uint8_t backR, backG, backB;

    struct cursorFrame* next;
    uint32_t serialNumber;

};

struct sentCursor
{
    uint32_t serialNumber;
    struct sentCursor* next;
};

//we need to delete cursor on client when XServer deleting cursor
struct deletedCursor
{
    struct deletedCursor* next;
    uint32_t serialNumber;
};


//this structure represent elements in cash
struct cache_elem
{
    struct cache_elem* next;
    struct cache_elem* prev;
    uint32_t crc;
    uint8_t* data;
    uint32_t size;
    uint32_t width;
    uint32_t height;
    uint32_t compressed_size;
    uint32_t rval, gval, bval;
    struct cache_elem* source; //element on which one of regions is based. It should be cleared after sending
    struct frame_region regions[9]; //this should be inited before add client to queue and deleted after sending
    BOOL sent; //if the element already sent to client
    uint32_t busy; //if the element is busy (for example in sending queue and can't be deleted)
    //or if the element referenced by another element which not sent yet. Every time the value will be incremented
    // when referenced element is ent, this value will be decremented
};

//this structure represents a deleted cash elements. It need to be sent to client to release deleted elements
struct deleted_elem
{
    struct deleted_elem* next;
    uint32_t crc;
};

struct sendqueue_element
{
    struct cache_elem* frame;
    int32_t x, y;
    uint32_t width, height;
    uint32_t crc;
    struct sendqueue_element* next;
};

//chunk of data with output selection
struct OutputChunk
{
    unsigned char* data; //data
    uint32_t size; //size of chunk in B
    enum SelectionMime mimeData; //UTF_STRING or PIXMAP (text or image)
    uint32_t compressed_size; // if chunk is compressed the size of compressed data, otherwise 0
    BOOL firstChunk; // if it's a first chunk in selection
    BOOL lastChunk;  // if it's a last chunk in selection
    enum SelectionType selection; //PRIMARY or CLIPBOARD
    uint32_t totalSize; //the total size of the selection data
    struct OutputChunk* next; //next chunk in the queue
};

//input selection
struct InputBuffer
{
    unsigned char* data; //data
    uint32_t size; //total size of selection
    uint32_t bytesReady; //how many bytes already read
    uint32_t currentChunkSize; //size of chunk we reading now;
    uint32_t currentChunkBytesReady; //how many bytes of current chunk are ready;
    uint32_t currentChunkCompressedSize; //if chunk is compressed, size of compressed data
    unsigned char* currentChunkCompressedData; //if chunk is compressed, compressed dat will be stored here
    enum SelectionMime mimeData; //UTF_STRING or PIXMAP
    xcb_timestamp_t timestamp; //ts when we own selection
    BOOL owner; //if we are the owners of selection
    enum {NOTIFIED, REQUESTED, COMPLETED} state;
};

//requests which processing should be delayed till we get data from client
struct DelayedRequest
{
    xcb_selection_request_event_t *request; // request from X-client
    xcb_selection_notify_event_t* event; // event which we are going to send to X-client
    struct DelayedRequest* next;
};

//save running INCR transactions in this struct
struct IncrTransaction
{
    xcb_window_t requestor;
    xcb_atom_t property;
    xcb_atom_t target;
    char* data;
    uint32_t size;
    uint32_t sentBytes;
    xcb_timestamp_t timestamp;
    struct IncrTransaction* next;
};


struct SelectionStructure
{
    unsigned long selThreadId; //id of selection thread
    enum ClipboardMode selectionMode; //CLIP_NONE, CLIP_CLIENT, CLIP_SERVER, CLIP_BOTH

    //output selection members
    uint32_t incrementalSize; //the total size of INCR selection we are currently reading
    uint32_t incrementalSizeRead; //bytes already read
    xcb_window_t clipWinId; // win id of clipboard window
    xcb_connection_t* xcbConnection; //XCB connection
    BOOL threadStarted; //if selection thread already started
    BOOL clientSupportsExetndedSelection; //if client supports extended selection - sending selection in several chunks for big size data
    BOOL clientSupportsOnDemandSelection; //if client supports selection on demand - sending data only if client requests it
    xcb_atom_t incrAtom; //mime type of the incr selection we are reading
    xcb_atom_t currentSelection; //selection we are currently reading
    struct OutputChunk* firstOutputChunk; //the first and last elements of the
    struct OutputChunk* lastOutputChunk;  //queue of selection chunks
    xcb_atom_t best_atom[2]; //the best mime type for selection to request on demand sel request from client
    BOOL requestSelection[2]; //selection thread will set it to TRUE if the selection need to be requested

    //Input selection members
    int readingInputBuffer; //which selection are reading input buffer at the moments: PRIMARY, CLIPBOARD or -1 if none
    struct InputBuffer inSelection[2]; //PRIMARY an CLIPBOARD selection buffers

    //list of delayed requests
    struct DelayedRequest* firstDelayedRequest;
    struct DelayedRequest* lastDelayedRequest;

    //list of INCR transactions
    struct IncrTransaction* firstIncrTransaction;
    struct IncrTransaction* lastIncrTransaction;

    pthread_mutex_t inMutex; //mutex for synchronization of incoming selection
};


struct _remoteHostVars
{
    unsigned char compression;
    OsTimerPtr checkConnectionTimer;
    int agentState;
    BOOL nxagentMode;
    char optionsFile[256];
    char stateFile[256];
    char acceptAddr[256];
    char cookie[33];
    char displayName[256];
    int listenPort;
    int jpegQuality;
    uint32_t framenum;
    uint32_t framenum_sent;
    uint32_t eventnum;
    uint32_t eventbytes;
    char eventBuffer[EVLENGTH*100];
    uint32_t evBufferOffset;

    unsigned long send_thread_id;

    //client information
    enum OS_VERSION client_os;
    uint16_t client_version;
    BOOL server_version_sent;

    //for control
    uint32_t cache_elements;
    uint32_t cache_size;
    uint32_t con_start_time;
    uint32_t data_sent;
    uint32_t data_copy;

    unsigned char* main_img;
    unsigned char* second_buffer;
    KdScreenInfo* ephyrScreen;
    uint32_t main_img_height, main_img_width;

//    #warning remove this hack
    int numofimg;

    int clientsock, serversock;


    struct cache_elem* first_cache_element;
    struct cache_elem* last_cache_element;


    struct sendqueue_element* first_sendqueue_element;
    struct sendqueue_element* last_sendqueue_element;


    struct deleted_elem* first_deleted_elements;
    struct deleted_elem* last_deleted_elements;
    uint32_t deleted_list_size;


    struct deletedCursor* first_deleted_cursor;
    struct deletedCursor* last_deleted_cursor;
    uint32_t deletedcursor_list_size;


    int maxfr;

    struct cursorFrame* firstCursor;
    struct cursorFrame* lastCursor;

    struct sentCursor* sentCursorsHead;
    struct sentCursor* sentCursorsTail;

    pthread_mutex_t sendqueue_mutex;
    pthread_mutex_t mainimg_mutex;
    pthread_cond_t have_sendqueue_cond;

    socklen_t addrlen;
    struct sockaddr_in address;

    BOOL client_connected;
    BOOL client_initialized;

    struct SelectionStructure selstruct;
} RemoteHostVars;

int send_selection_chunk(int sel, unsigned char* data, uint32_t length, uint32_t format, BOOL first, BOOL last, uint32_t compressed, uint32_t total);
int send_output_selection(struct OutputChunk* chunk);

void set_client_version(uint16_t ver, uint16_t os);

void readInputSelectionBuffer(char* buff);
void readInputSelectionHeader(char* buff);

#if XORG_VERSION_CURRENT < 11900000
void pollEvents(void);
#endif /* XORG_VERSION_CURRENT */
void clear_cache_data(uint32_t maxsize);
void clear_frame_cache(uint32_t max_elements);

uint32_t calculate_crc(uint32_t width, uint32_t height, int32_t dx, int32_t dy);


void readOptionsFromFile(void);
unsigned int checkSocketConnection(OsTimerPtr timer, CARD32 time, void* args);

void restartTimerOnInit(void);

void open_socket(void);

void setAgentState(int state);

void terminateServer(int exitStatus);


void unpack_current_chunk_to_buffer(struct InputBuffer* selbuff);

unsigned char* image_compress(uint32_t image_width, uint32_t image_height,
                             unsigned char* RGBA_buffer, uint32_t* compressed_size, int bpp, char* fname);



unsigned char* jpeg_compress(int quality, uint32_t image_width, uint32_t image_height,
                             unsigned char* RGBA_buffer, uint32_t* jpeg_size, int bpp, char* fname);

unsigned char* png_compress(uint32_t image_width, uint32_t image_height,
                             unsigned char* RGBA_buffer, uint32_t* png_size);

void clientReadNotify(int fd, int ready, void *data);

void add_frame(uint32_t width, uint32_t height, int32_t x, int32_t y, uint32_t crc, uint32_t size);


void clear_output_selection(void);

void disconnect_client(void);


void remote_handle_signal(int signum);


void remote_sendCursor(CursorPtr cursor);

void remote_removeCursor(uint32_t serialNumber);
void remote_send_main_image(void);

void remote_sendVersion(void);

int remote_init(void);

void remote_selection_init(void);

void remote_set_display_name(const char* name);

void *remote_screen_init(KdScreenInfo *screen,
                         int x, int y,
                         int width, int height, int buffer_height,
                         int *bytes_per_line, int *bits_per_pixel);

void
remote_paint_rect(KdScreenInfo *screen,
                  int sx, int sy, int dx, int dy, int width, int height);

void request_selection_from_client(enum SelectionType selection);

#endif /* X2GOKDRIVE_REMOTE_H */
