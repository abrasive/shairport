/*
 * Embedded Avahi client. This file is part of Shairport.
 * Copyright (c) James Laird 2013
 * Additions for metadata and for detecting IPv6 Copyright (c) Mike Brady 2015
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

#include "common.h"
#include "mdns.h"
#include <string.h>

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/thread-watch.h>

#include <avahi-client/lookup.h>
#include <avahi-common/alternative.h>

static AvahiServiceBrowser *sb = NULL;
static AvahiClient *client = NULL;
static AvahiEntryGroup *group = NULL;
static AvahiThreadedPoll *tpoll = NULL;

static char *name = NULL;
static int port = 0;

static void resolve_callback(AvahiServiceResolver *r, AVAHI_GCC_UNUSED AvahiIfIndex interface,
                             AVAHI_GCC_UNUSED AvahiProtocol protocol, AvahiResolverEvent event,
                             const char *name, const char *type, const char *domain,
                             const char *host_name, const AvahiAddress *address, uint16_t port,
                             AvahiStringList *txt, AvahiLookupResultFlags flags,
                             AVAHI_GCC_UNUSED void *userdata) {

  assert(r);

  /* Called whenever a service has been resolved successfully or timed out */

  switch (event) {
  case AVAHI_RESOLVER_FAILURE:
    debug(2, "(Resolver) Failed to resolve service '%s' of type '%s' in domain '%s': %s\n", name,
          type, domain, avahi_strerror(avahi_client_errno(avahi_service_resolver_get_client(r))));
    break;

  case AVAHI_RESOLVER_FOUND: {
    if (flags & AVAHI_LOOKUP_RESULT_OUR_OWN) {
      char a[AVAHI_ADDRESS_STR_MAX], *t;

      //              debug(1, "avahi: service '%s' of type '%s' in domain '%s' added.", name, type,
      //              domain);
      avahi_address_snprint(a, sizeof(a), address);
      debug(1, "avahi: address advertised is: \"%s\".", a);
      /*
      t = avahi_string_list_to_string(txt);
      debug(1,
              "\t%s:%u (%s)\n"
              "\tTXT=%s\n"
              "\tcookie is %u\n"
              "\tis_local: %i\n"
              "\tour_own: %i\n"
              "\twide_area: %i\n"
              "\tmulticast: %i\n"
              "\tcached: %i\n",
              host_name, port, a,
              t,
              avahi_string_list_get_service_cookie(txt),
              !!(flags & AVAHI_LOOKUP_RESULT_LOCAL),
              !!(flags & AVAHI_LOOKUP_RESULT_OUR_OWN),
              !!(flags & AVAHI_LOOKUP_RESULT_WIDE_AREA),
              !!(flags & AVAHI_LOOKUP_RESULT_MULTICAST),
              !!(flags & AVAHI_LOOKUP_RESULT_CACHED));

      avahi_free(t);
      */
    }
  }
  }

  avahi_service_resolver_free(r);
}

static void browse_callback(AvahiServiceBrowser *b, AvahiIfIndex interface, AvahiProtocol protocol,
                            AvahiBrowserEvent event, const char *name, const char *type,
                            const char *domain, AVAHI_GCC_UNUSED AvahiLookupResultFlags flags,
                            void *userdata) {

  AvahiClient *c = userdata;
  assert(b);

  /* Called whenever a new services becomes available on the LAN or is removed from the LAN */

  switch (event) {
  case AVAHI_BROWSER_FAILURE:

    //            debug(1, "(Browser) %s\n",
    //            avahi_strerror(avahi_client_errno(avahi_service_browser_get_client(b))));
    return;

  case AVAHI_BROWSER_NEW:
    //            debug(1, "(Browser) NEW: service '%s' of type '%s' in domain '%s'\n", name, type,
    //            domain);

    /* We ignore the returned resolver object. In the callback
       function we free it. If the server is terminated before
       the callback function is called the server will free
       the resolver for us. */

    if (!(avahi_service_resolver_new(c, interface, protocol, name, type, domain, AVAHI_PROTO_UNSPEC,
                                     0, resolve_callback, c)))
      debug(1, "Failed to resolve service '%s': %s\n", name, avahi_strerror(avahi_client_errno(c)));

    break;

  case AVAHI_BROWSER_REMOVE:
    //            debug(1, "(Browser) REMOVE: service '%s' of type '%s' in domain '%s'\n", name,
    //            type, domain);
    break;

  case AVAHI_BROWSER_ALL_FOR_NOW:
  case AVAHI_BROWSER_CACHE_EXHAUSTED:
    //            debug(1, "(Browser) %s\n", event == AVAHI_BROWSER_CACHE_EXHAUSTED ?
    //            "CACHE_EXHAUSTED" : "ALL_FOR_NOW");
    break;
  }
}

static void register_service(AvahiClient *c);

static void egroup_callback(AvahiEntryGroup *g, AvahiEntryGroupState state,
                            AVAHI_GCC_UNUSED void *userdata) {
  switch (state) {
  case AVAHI_ENTRY_GROUP_ESTABLISHED:
    /* The entry group has been established successfully */
    debug(1, "avahi: service '%s' successfully added.", name);
    break;

  case AVAHI_ENTRY_GROUP_COLLISION: {
    char *n;

    /* A service name collision with a remote service
     * happened. Let's pick a new name */
    n = avahi_alternative_service_name(name);
    avahi_free(name);
    name = n;

    debug(2, "avahi: service name collision, renaming service to '%s'", name);

    /* And recreate the services */
    register_service(avahi_entry_group_get_client(g));
    break;
  }

  case AVAHI_ENTRY_GROUP_FAILURE:
    debug(1, "avahi: entry group failure: %s",
          avahi_strerror(avahi_client_errno(avahi_entry_group_get_client(g))));
    break;

  case AVAHI_ENTRY_GROUP_UNCOMMITED:
    debug(2, "avahi: service '%s' group is not yet commited.", name);
    break;

  case AVAHI_ENTRY_GROUP_REGISTERING:
    debug(2, "avahi: service '%s' group is registering.", name);
    break;

  default:
    debug(1, "avahi: unhandled egroup state: %d", state);
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
    AvahiIfIndex selected_interface;
    if (config.interface != NULL)
      selected_interface = config.interface_index;
    else
      selected_interface = AVAHI_IF_UNSPEC;
#ifdef CONFIG_METADATA
    if (config.metadata_enabled) {
      ret = avahi_entry_group_add_service(group, selected_interface, AVAHI_PROTO_UNSPEC, 0, name,
                                          config.regtype, NULL, NULL, port,
                                          MDNS_RECORD_WITH_METADATA, NULL);
      if (ret == 0)
        debug(1, "avahi: request to add \"%s\" service with metadata", config.regtype);
    } else {
#endif
      ret = avahi_entry_group_add_service(group, selected_interface, AVAHI_PROTO_UNSPEC, 0, name,
                                          config.regtype, NULL, NULL, port,
                                          MDNS_RECORD_WITHOUT_METADATA, NULL);
      if (ret == 0)
        debug(1, "avahi: request to add \"%s\" service without metadata", config.regtype);
#ifdef CONFIG_METADATA
    }
#endif

    if (ret < 0)
      debug(1, "avahi: avahi_entry_group_add_service failed");
    else {
      ret = avahi_entry_group_commit(group);
      if (ret < 0)
        debug(1, "avahi: avahi_entry_group_commit failed");
    }
  }
}

static void client_callback(AvahiClient *c, AvahiClientState state,
                            AVAHI_GCC_UNUSED void *userdata) {
  int err;

  switch (state) {
  case AVAHI_CLIENT_S_REGISTERING:
    if (group)
      avahi_entry_group_reset(group);
    break;

  case AVAHI_CLIENT_S_RUNNING:
    register_service(c);
    break;

  case AVAHI_CLIENT_FAILURE:
    err = avahi_client_errno(c);
    debug(1, "avahi: client failure: %s", avahi_strerror(err));

    if (err == AVAHI_ERR_DISCONNECTED) {
      /* We have been disconnected, so lets reconnect */
      avahi_client_free(c);
      c = NULL;
      group = NULL;

      if (!(client = avahi_client_new(avahi_threaded_poll_get(tpoll), AVAHI_CLIENT_NO_FAIL,
                                      client_callback, userdata, &err))) {
        warn("avahi: failed to create client object: %s", avahi_strerror(err));
        avahi_threaded_poll_quit(tpoll);
      }
    } else {
      warn("avahi: client failure: %s", avahi_strerror(err));
      avahi_threaded_poll_quit(tpoll);
    }
    break;

  case AVAHI_CLIENT_S_COLLISION:
    debug(2, "avahi: state is AVAHI_CLIENT_S_COLLISION...needs a rename: %s", name);
    break;

  case AVAHI_CLIENT_CONNECTING:
    debug(2, "avahi: received AVAHI_CLIENT_CONNECTING");
    break;

  default:
    debug(1, "avahi: unexpected and unhandled avahi client state: %d", state);
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
  if (!(client = avahi_client_new(avahi_threaded_poll_get(tpoll), AVAHI_CLIENT_NO_FAIL,
                                  client_callback, NULL, &err))) {
    warn("couldn't create avahi client: %s!", avahi_strerror(err));
    return -1;
  }

  // we need this to detect the IPv6 number we're advertising...
  // if (!(sb = avahi_service_browser_new(client, AVAHI_IF_UNSPEC,  AVAHI_PROTO_UNSPEC,
  // config.regtype, NULL, 0, browse_callback, client))) {
  //     warn("Failed to create service browser: %s\n", avahi_strerror(avahi_client_errno(client)));
  //     return -1;
  // }

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
