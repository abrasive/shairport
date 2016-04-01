/*
 * Embedded Avahi client. This file is part of Shairport.
 * Copyright (c) James Laird 2013
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdlib.h>

#include <string.h>
#include "common.h"
#include "mdns.h"

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/thread-watch.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>


static AvahiClient *client = NULL;
static AvahiEntryGroup *group = NULL;
static AvahiThreadedPoll *tpoll = NULL;

static char *name = NULL;
static int port = 0;

static void register_service( AvahiClient *c );

static void egroup_callback(AvahiEntryGroup *g, AvahiEntryGroupState state,
                            AVAHI_GCC_UNUSED void *userdata) {
   switch ( state )
   {
      case AVAHI_ENTRY_GROUP_ESTABLISHED:
         /* The entry group has been established successfully */
         debug(1,"avahi: service '%s'  successfully added.", name );
         break;

      case AVAHI_ENTRY_GROUP_COLLISION:
      {
         char *n;

         /* A service name collision with a remote service
          * happened. Let's pick a new name */
         n = avahi_alternative_service_name( name );
         avahi_free( name );
         name = n;

         debug(2,"avahi: service name collision, renaming service to '%s'", name );

         /* And recreate the services */
         register_service( avahi_entry_group_get_client( g ) );
         break;
      }

      case AVAHI_ENTRY_GROUP_FAILURE:
        debug(1,"avahi: entry group failure: %s", avahi_strerror( avahi_client_errno( avahi_entry_group_get_client( g ) ) ) );
        break;

      case AVAHI_ENTRY_GROUP_UNCOMMITED:
         debug(2,"avahi: service '%s' group is not yet commited.", name );
         break;

      case AVAHI_ENTRY_GROUP_REGISTERING:
         debug(2,"avahi: service '%s' group is registering.", name );
         break;

      default:
         debug(1,"avahi: unhandled egroup state: %d", state );
         break;
   }
}

static void register_service(AvahiClient *c) {
  debug(1, "avahi: register_service.");
  if (!group)
    group = avahi_entry_group_new(c, egroup_callback, NULL);
  if (!group)
    debug(2, "avahi: avahi_entry_group_new failed");
  else {

    if (!avahi_entry_group_is_empty(group))
      return;

    int ret;
  #ifdef CONFIG_METADATA
    if (config.metadata_enabled) {
      ret = avahi_entry_group_add_service(group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, 0, name,
                                          config.regtype, NULL, NULL, port, MDNS_RECORD_WITH_METADATA,
                                          NULL);
    if (ret==0)
      debug(1, "avahi: request to add \"%s\" service with metadata",config.regtype);
    } else {
  #endif
      ret = avahi_entry_group_add_service(group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, 0, name,
                                          config.regtype, NULL, NULL, port,
                                          MDNS_RECORD_WITHOUT_METADATA, NULL);
    if (ret==0)
      debug(1, "avahi: request to add \"%s\" service without metadata",config.regtype);
 #ifdef CONFIG_METADATA
    }
  #endif

    if (ret < 0)
      debug(1,"avahi: avahi_entry_group_add_service failed");
    else {
      ret = avahi_entry_group_commit(group);
      if (ret < 0)
        debug(1,"avahi: avahi_entry_group_commit failed");
    }
  }
}

static void client_callback(AvahiClient *c, AvahiClientState state,
                            AVAHI_GCC_UNUSED void *userdata) {
  switch (state) {
     case AVAHI_CLIENT_S_REGISTERING:
       if (group)
         avahi_entry_group_reset(group);
       break;

     case AVAHI_CLIENT_S_RUNNING:
       register_service(c);
       break;

     case AVAHI_CLIENT_FAILURE:
       debug(1,"avahi: client failure");
       break;

     case AVAHI_CLIENT_S_COLLISION:
       debug(2, "avahi: state is AVAHI_CLIENT_S_COLLISION...needs a rename: %s", name );
       break;

     case AVAHI_CLIENT_CONNECTING:
       debug(2, "avahi: received AVAHI_CLIENT_CONNECTING" );
       break;

     default:
       debug(1,"avahi: unexpected and unhandled avahi client state: %d", state );
       break;
  }
}

static int avahi_register(char *srvname, int srvport) {
  debug(1, "avahi: avahi_register.");
  name = strdup(srvname);
  port = srvport;

  int err;
  if (!(tpoll = avahi_threaded_poll_new())) {
    warn("couldn't create avahi threaded tpoll!");
    return -1;
  }
  if (!(client =
            avahi_client_new(avahi_threaded_poll_get(tpoll), 0, client_callback, NULL, &err))) {
    warn("couldn't create avahi client: %s!", avahi_strerror(err));
    return -1;
  }

  if (avahi_threaded_poll_start(tpoll) < 0) {
    warn("couldn't start avahi tpoll thread");
    return -1;
  }

  return 0;
}

static void avahi_unregister(void) {
  debug(1, "avahi: avahi_unregister.");
  if (tpoll)
    avahi_threaded_poll_stop(tpoll);
  tpoll = NULL;

  if (name)
    free(name);
  name = NULL;
}

mdns_backend mdns_avahi = {
    .name = "avahi", .mdns_register = avahi_register, .mdns_unregister = avahi_unregister};
