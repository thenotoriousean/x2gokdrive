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


#ifndef _XLIBS_STUFF_H_
#define _XLIBS_STUFF_H_

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

#include "x2gokdriveselection.h"


#define EPHYR_WANT_DEBUG 1
#warning DEBUG ENABLED


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

enum msg_type{FRAME,DELETED,CURSOR, DELETEDCURSOR, SELECTION};
enum AgentState{STARTING, RUNNING, RESUMING, SUSPENDING, SUSPENDED, TERMINATING, TERMINATED};
enum Compressions{JPEG,PNG};
enum SelectionType{PRIMARY,CLIPBOARD};
enum SelectionMime{STRING,UTF_STRING,PIXMAP};

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

typedef struct
{
    unsigned char* data;
    uint32_t size;
    int mimeData;
    uint32_t position;
}inputBuffer;

typedef struct
{
    unsigned char* data;
    uint32_t size;
    int mimeData;
    BOOL changed;

}outputBuffer;

typedef struct
{
    BOOL readingIncremental;
    uint32_t incrementalPosition;
    Window clipWinId;
    WindowPtr clipWinPtr;
    BOOL callBackInstalled;
//Output selection
    outputBuffer clipboard;
    outputBuffer selection;
//Input selection
    BOOL readIngInputBuffer;
    inputBuffer inBuffer;
    inputBuffer inSelection;
    inputBuffer inClipboard;
}SelectionStructure;


struct RemoteHostVars
{
    unsigned char compression;
    OsTimerPtr checkConnectionTimer;
    int agentState;
    BOOL nxagentMode;
    char optionsFile[256];
    char stateFile[256];
    char acceptAddr[256];
    char cookie[33];
    int listenPort;
    int jpegQuality;
    uint32_t framenum;
    uint32_t framenum_sent;
    uint32_t eventnum;
    uint32_t eventbytes;

    long send_thread_id;

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

    #warning remove this hack
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

    SelectionStructure selstruct;
};
typedef struct RemoteHostVars RemoteHostVars;




int send_selection(int sel, char* data, uint32_t length, uint32_t mimeData);

void clear_cache_data(uint32_t maxsize);
void clear_frame_cache(uint32_t max_elements);

uint32_t calculate_crc(uint32_t width, uint32_t height, int32_t dx, int32_t dy);


void readOptionsFromFile();
unsigned int checkSocketConnection(OsTimerPtr timer, CARD32 time, void* args);

void restartTimerOnInit();

void open_socket();

void setAgentState(int state);

void terminateServer(int exitStatus);



unsigned char* image_compress(uint32_t image_width, uint32_t image_height,
                             unsigned char* RGBA_buffer, uint32_t* compressed_size, int bpp, char* fname);



unsigned char* jpeg_compress(int quality, uint32_t image_width, uint32_t image_height,
                             unsigned char* RGBA_buffer, uint32_t* jpeg_size, int bpp, char* fname);

unsigned char* png_compress(uint32_t image_width, uint32_t image_height,
                             unsigned char* RGBA_buffer, uint32_t* png_size);

void clientReadNotify(int fd, int ready, void *data);

void add_frame(uint32_t width, uint32_t height, int32_t x, int32_t y, uint32_t crc, uint32_t size);




void disconnect_client();


void remote_handle_signal(int signum);


void remote_sendCursor(CursorPtr cursor);

void remote_removeCursor(uint32_t serialNumber);
void remote_send_main_image();


int remote_init(void);

void remote_selection_init(void);


void *remote_screen_init(KdScreenInfo *screen,
                         int x, int y,
                         int width, int height, int buffer_height,
                         int *bytes_per_line, int *bits_per_pixel);

void
remote_paint_rect(KdScreenInfo *screen,
                  int sx, int sy, int dx, int dy, int width, int height);


#endif /*_XLIBS_STUFF_H_*/
