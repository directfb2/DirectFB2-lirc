/*
   This file is part of DirectFB.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
*/

#include <core/input_driver.h>
#include <direct/thread.h>
#include <directfb_keynames.h>
#ifdef HAVE_LIRC_CLIENT
#include <lirc/lirc_client.h>
#endif

D_DEBUG_DOMAIN( LIRC, "Input/LIRC", "LIRC Input Driver" );

DFB_INPUT_DRIVER( lirc )

/**********************************************************************************************************************/

typedef struct {
     CoreInputDevice *device;

     int              fd;

     DirectThread    *thread;
} LircData;

static DirectFBKeySymbolNames(keynames);

/**********************************************************************************************************************/

static int
keyname_compare( const void *name,
                 const void *symbol_name )
{
     return strcmp( name, ((struct DFBKeySymbolName*) symbol_name)->name );
}

static int
keynames_sort_compare( const void *symbol_name1,
                       const void *symbol_name2 )
{
     return strcmp( ((struct DFBKeySymbolName*) symbol_name1)->name, ((struct DFBKeySymbolName*) symbol_name2)->name );
}

/**********************************************************************************************************************/

static void *
lirc_event_thread( DirectThread *thread,
                   void         *arg )
{
     LircData                *data    = arg;
     int                      repeats = 0;
     DFBInputDeviceKeySymbol  symbol  = DIKS_NULL;

     D_DEBUG_AT( LIRC, "%s()\n", __FUNCTION__ );

     while (1) {
          DFBInputEvent            evt = { .type = DIET_UNKNOWN };
          fd_set                   set;
          struct timeval           timeout;
          int                      status;
          char                     line[128];
#ifdef HAVE_LIRC_CLIENT
          char                    *code;
#endif
          char                    *s, *name;
          struct DFBKeySymbolName *symbol_name = NULL;

          FD_ZERO( &set );
          FD_SET( data->fd, &set );

          timeout.tv_sec  = 0;
          timeout.tv_usec = 200000;

          status = select( data->fd + 1, &set, NULL, NULL, &timeout );

          if (status < 0 && errno != EINTR)
               break;

          if (status < 0)
               continue;

          /* Check timeout, release last key. */
          if (status == 0) {
               if (symbol != DIKS_NULL) {
                    evt.flags      = DIEF_KEYSYMBOL;
                    evt.type       = DIET_KEYRELEASE;
                    evt.key_symbol = symbol;

                    dfb_input_dispatch( data->device, &evt );

                    symbol = DIKS_NULL;
               }

               continue;
          }

          /* Read data. */
#ifdef HAVE_LIRC_CLIENT
          if (lirc_nextcode( &code ) == -1)
               continue;
          strncpy( line, code, sizeof(line) );
          free( code );
#else
          if (read( data->fd, line, sizeof(line) ) < 1)
               continue;
#endif

          /* Get new key. */
          s = strchr( line, ' ' );
          if (!s || !s[1])
               continue;

          s = strchr( ++s, ' ' );
          if (!s|| !s[1])
               continue;

          name = ++s;

          s = strchr( name, ' ' );
          if (s)
               *s = '\0';

          if (strlen( name ))
               symbol_name = bsearch( name, keynames, D_ARRAY_SIZE(keynames), sizeof(keynames[0]), keyname_compare );
          else
               continue;

          if (!symbol_name)
               continue;

          /* Check repeated key. */
          if (symbol_name->symbol == symbol) {
               /* Swallow the first three repeats. */
               if (++repeats < 4)
                    continue;
          }
          else {
               /* Reset repeat counter. */
               repeats = 0;

               /* Release last key if it is not released yet. */
               if (symbol != DIKS_NULL) {
                    evt.flags      = DIEF_KEYSYMBOL;
                    evt.type       = DIET_KEYRELEASE;
                    evt.key_symbol = symbol;

                    dfb_input_dispatch( data->device, &evt );
               }
          }

          /* Press key. */
          evt.flags      = DIEF_KEYSYMBOL;
          evt.type       = DIET_KEYPRESS;
          evt.key_symbol = symbol_name->symbol;

          dfb_input_dispatch( data->device, &evt );

          /* Remember last key. */
          symbol = symbol_name->symbol;
     }

     D_DEBUG_AT( LIRC, "LIRC Event thread terminated\n" );

     return NULL;
}

/**********************************************************************************************************************/

static int
driver_get_available()
{
#ifndef HAVE_LIRC_CLIENT
     int                fd;
     struct sockaddr_un addr;
#endif

#ifdef HAVE_LIRC_CLIENT
     if (lirc_init( "LIRC", 0 ) == -1)
          return 0;

     lirc_deinit();
#else
     /* Create socket. */
     fd = socket( PF_UNIX, SOCK_STREAM, 0 );
     if (fd < 0)
          return 0;

     /* Initiate connection */
     addr.sun_family = AF_UNIX;
     direct_snputs( addr.sun_path, "/var/run/lirc/lircd", sizeof(addr.sun_path) );

     if (connect( fd, (struct sockaddr*) &addr, sizeof(addr) ) < 0) {
          close( fd );
          return 0;
     }

     close( fd );
#endif

     return 1;
}

static void
driver_get_info( InputDriverInfo *driver_info )
{
     driver_info->version.major = 0;
     driver_info->version.minor = 1;

     snprintf( driver_info->name,   DFB_INPUT_DRIVER_INFO_NAME_LENGTH,   "LIRC" );
     snprintf( driver_info->vendor, DFB_INPUT_DRIVER_INFO_VENDOR_LENGTH, "DirectFB" );
}

static DFBResult
driver_open_device( CoreInputDevice  *device,
                    unsigned int      number,
                    InputDeviceInfo  *device_info,
                    void            **driver_data )
{
     LircData           *data;
     int                 fd;
#ifndef HAVE_LIRC_CLIENT
     struct sockaddr_un  addr;
#endif

     D_DEBUG_AT( LIRC, "%s()\n", __FUNCTION__ );

#ifdef HAVE_LIRC_CLIENT
     fd = lirc_init( "LIRC", 0 );
     if (fd == -1) {
          D_ERROR( "Input/LIRC: Could not connect to lircd socket!\n" );
          return DFB_INIT;
     }
#else
     /* Create socket. */
     fd = socket( PF_UNIX, SOCK_STREAM, 0 );
     if (fd < 0) {
          D_PERROR( "Input/LIRC: Could not create socket!\n" );
          return DFB_INIT;
     }

     /* Initiate connection */
     addr.sun_family = AF_UNIX;
     direct_snputs( addr.sun_path, "/var/run/lirc/lircd", sizeof(addr.sun_path) );
     if (connect( fd, (struct sockaddr*) &addr, sizeof(addr) ) < 0) {
          D_PERROR( "Input/LIRC: Could not connect the socket!\n" );
          close( fd );
          return DFB_INIT;
     }
#endif

     /* Fill device information. */
     device_info->prefered_id = DIDID_REMOTE;
     device_info->desc.type   = DIDTF_REMOTE;
     device_info->desc.caps   = DICAPS_KEYS;
     snprintf( device_info->desc.name,   DFB_INPUT_DEVICE_DESC_NAME_LENGTH,   "Remote Control" );
     snprintf( device_info->desc.vendor, DFB_INPUT_DEVICE_DESC_VENDOR_LENGTH, "LIRC" );

     /* Allocate and fill private data. */
     data = D_CALLOC( 1, sizeof(LircData) );
     if (!data) {
          close( fd );
          return D_OOM();
     }

     data->device = device;
     data->fd     = fd;

     qsort( keynames, D_ARRAY_SIZE(keynames), sizeof(keynames[0]), keynames_sort_compare );

     /* Start lirc event thread. */
     data->thread = direct_thread_create( DTT_INPUT, lirc_event_thread, data, "LIRC Event" );

     *driver_data = data;

     return DFB_OK;
}

static DFBResult
driver_get_keymap_entry( CoreInputDevice           *device,
                         void                      *driver_data,
                         DFBInputDeviceKeymapEntry *entry )
{
     return DFB_UNSUPPORTED;
}

static void
driver_close_device( void *driver_data )
{
     LircData *data = driver_data;

     D_DEBUG_AT( LIRC, "%s()\n", __FUNCTION__ );

     /* Terminate the lirc event thread. */
     direct_thread_cancel( data->thread );

     direct_thread_join( data->thread );
     direct_thread_destroy( data->thread );

     close( data->fd );

#ifdef HAVE_LIRC_CLIENT
     lirc_deinit();
#endif

     D_FREE( data );
}
