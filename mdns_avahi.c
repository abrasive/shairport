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

#include <pthread.h>
#include <stdlib.h>

#include "config.h"

#include "common.h"
#include "mdns.h"
#include "rtsp.h"
#ifdef CONFIG_DACP
#include "dacp.h"
#endif
#include <string.h>

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/thread-watch.h>

#include <avahi-client/lookup.h>
#include <avahi-common/alternative.h>

typedef struct {
  AvahiThreadedPoll *service_poll;
  AvahiClient *service_client;
  AvahiServiceBrowser *service_browser;
} dacp_browser_struct;

// static AvahiServiceBrowser *sb = NULL;
static AvahiClient *client = NULL;
// static AvahiClient *service_client = NULL;
static AvahiEntryGroup *group = NULL;
static AvahiThreadedPoll *tpoll = NULL;
// static AvahiThreadedPoll *service_poll = NULL;

static char *service_name = NULL;
static int port = 0;

static void resolve_callback(AvahiServiceResolver *r, AVAHI_GCC_UNUSED AvahiIfIndex interface,
                             AVAHI_GCC_UNUSED AvahiProtocol protocol, AvahiResolverEvent event,
                             const char *name, const char *type, const char *domain,
                             const char *host_name, const AvahiAddress *address, uint16_t port,
                             AvahiStringList *txt, AvahiLookupResultFlags flags, void *userdata) {
  assert(r);
  
  rtsp_conn_info *conn = (rtsp_conn_info *)userdata;
  dacp_browser_struct *dbs = (dacp_browser_struct *)conn->mdns_private_pointer;

  /* Called whenever a service has been resolved successfully or timed out */
  switch (event) {
  case AVAHI_RESOLVER_FAILURE:
    debug(3, "(Resolver) Failed to resolve service '%s' of type '%s' in domain '%s': %s.", name,
          type, domain, avahi_strerror(avahi_client_errno(avahi_service_resolver_get_client(r))));
    break;
  case AVAHI_RESOLVER_FOUND: {
    char a[AVAHI_ADDRESS_STR_MAX], *t;
    // debug(1, "Resolve callback: Service '%s' of type '%s' in domain '%s':", name, type, domain);
    char *dacpid = strstr(name, "iTunes_Ctrl_");
    if (dacpid) {
      dacpid += strlen("iTunes_Ctrl_");
      if (strcmp(dacpid, conn->dacp_id) == 0) {
        if (conn->dacp_port != port) {
          debug(3, "Client's DACP port: %u.", port);
          conn->dacp_port = port;
#if defined(HAVE_DBUS) || defined(HAVE_MPRIS)
          set_dacp_server_information(conn);
#endif
#ifdef CONFIG_METADATA
          char portstring[20];
          memset(portstring, 0, sizeof(portstring));
          sprintf(portstring, "%u", port);
          send_ssnc_metadata('dapo', strdup(portstring), strlen(portstring), 0);
#endif
        }
      }
    } else {
      debug(1, "Resolve callback: Can't see a DACP string in a DACP Record!");
    }
  }
  }
  avahi_service_resolver_free(r);
}
static void browse_callback(AvahiServiceBrowser *b, AvahiIfIndex interface, AvahiProtocol protocol,
                            AvahiBrowserEvent event, const char *name, const char *type,
                            const char *domain, AVAHI_GCC_UNUSED AvahiLookupResultFlags flags,
                            void *userdata) {
  rtsp_conn_info *conn = (rtsp_conn_info *)userdata;
  dacp_browser_struct *dbs = (dacp_browser_struct *)conn->mdns_private_pointer;
  assert(b);
  /* Called whenever a new services becomes available on the LAN or is removed from the LAN */
  switch (event) {
  case AVAHI_BROWSER_FAILURE:
    warn("avahi: browser failure.",
         avahi_strerror(avahi_client_errno(avahi_service_browser_get_client(b))));
    avahi_threaded_poll_quit(tpoll);
    break;
  case AVAHI_BROWSER_NEW:
    debug(3, "(Browser) NEW: service '%s' of type '%s' in domain '%s'.", name, type, domain);
    /* We ignore the returned resolver object. In the callback
       function we free it. If the server is terminated before
       the callback function is called the server will free
       the resolver for us. */
    if (!(avahi_service_resolver_new(dbs->service_client, interface, protocol, name, type, domain,
                                     AVAHI_PROTO_UNSPEC, 0, resolve_callback, userdata)))
      debug(1, "Failed to resolve service '%s': %s.", name,
            avahi_strerror(avahi_client_errno(dbs->service_client)));
    break;
  case AVAHI_BROWSER_REMOVE:
    debug(3, "(Browser) REMOVE: service '%s' of type '%s' in domain '%s'.", name, type, domain);
    char *dacpid = strstr(name, "iTunes_Ctrl_");
    if (dacpid) {
      dacpid += strlen("iTunes_Ctrl_");
      if (strcmp(dacpid, conn->dacp_id) == 0) {
        if (conn->dacp_id != 0) {
          debug(1, "Client's DACP status withdrawn.");
          conn->dacp_port = 0;
#if defined(HAVE_DBUS) || defined(HAVE_MPRIS)          
          set_dacp_server_information(conn); // this will have the effect of telling the scanner that the DACP server is no longer working
#endif
        }
      }
    } else {
      debug(1, "Browse callback: Can't see a DACP string in a DACP Record!");
    }

    break;
  case AVAHI_BROWSER_ALL_FOR_NOW:
  case AVAHI_BROWSER_CACHE_EXHAUSTED:
    // debug(1, "(Browser) %s.", event == AVAHI_BROWSER_CACHE_EXHAUSTED ? "CACHE_EXHAUSTED" :
    // "ALL_FOR_NOW");
    break;
  }
}

static void register_service(AvahiClient *c);

static void egroup_callback(AvahiEntryGroup *g, AvahiEntryGroupState state,
                            AVAHI_GCC_UNUSED void *userdata) {
  switch (state) {
  case AVAHI_ENTRY_GROUP_ESTABLISHED:
    /* The entry group has been established successfully */
    debug(1, "avahi: service '%s' successfully added.", service_name);
    break;

  case AVAHI_ENTRY_GROUP_COLLISION: {
    char *n;

    /* A service name collision with a remote service
     * happened. Let's pick a new name */
    n = avahi_alternative_service_name(service_name);
    avahi_free(service_name);
    service_name = n;

    debug(2, "avahi: service name collision, renaming service to '%s'", service_name);

    /* And recreate the services */
    register_service(avahi_entry_group_get_client(g));
    break;
  }

  case AVAHI_ENTRY_GROUP_FAILURE:
    debug(1, "avahi: entry group failure: %s",
          avahi_strerror(avahi_client_errno(avahi_entry_group_get_client(g))));
    break;

  case AVAHI_ENTRY_GROUP_UNCOMMITED:
    debug(2, "avahi: service '%s' group is not yet committed.", service_name);
    break;

  case AVAHI_ENTRY_GROUP_REGISTERING:
    debug(2, "avahi: service '%s' group is registering.", service_name);
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
      ret = avahi_entry_group_add_service(group, selected_interface, AVAHI_PROTO_UNSPEC, 0,
                                          service_name, config.regtype, NULL, NULL, port,
                                          MDNS_RECORD_WITH_METADATA, NULL);
      if (ret == 0)
        debug(1, "avahi: request to add \"%s\" service with metadata", config.regtype);
    } else {
#endif
      ret = avahi_entry_group_add_service(group, selected_interface, AVAHI_PROTO_UNSPEC, 0,
                                          service_name, config.regtype, NULL, NULL, port,
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
    debug(2, "avahi: state is AVAHI_CLIENT_S_COLLISION...needs a rename: %s", service_name);
    break;

  case AVAHI_CLIENT_CONNECTING:
    debug(2, "avahi: received AVAHI_CLIENT_CONNECTING");
    break;

  default:
    debug(1, "avahi: unexpected and unhandled avahi client state: %d", state);
    break;
  }
}

static void service_client_callback(AvahiClient *c, AvahiClientState state, void *userdata) {
  int err;

  rtsp_conn_info *conn = (rtsp_conn_info *)userdata;
  dacp_browser_struct *dbs = (dacp_browser_struct *)conn->mdns_private_pointer;

  switch (state) {
  case AVAHI_CLIENT_S_REGISTERING:
    break;

  case AVAHI_CLIENT_S_RUNNING:
    break;

  case AVAHI_CLIENT_FAILURE:
    err = avahi_client_errno(c);
    debug(1, "avahi: service client failure: %s", avahi_strerror(err));

    if (err == AVAHI_ERR_DISCONNECTED) {
      /* We have been disconnected, so lets reconnect */
      avahi_client_free(c);
      c = NULL;

      if (!(dbs->service_client =
                avahi_client_new(avahi_threaded_poll_get(dbs->service_poll), AVAHI_CLIENT_NO_FAIL,
                                 service_client_callback, userdata, &err))) {
        warn("avahi: failed to create service client object: %s", avahi_strerror(err));
        avahi_threaded_poll_quit(dbs->service_poll);
      }
    } else {
      warn("avahi: service client failure: %s", avahi_strerror(err));
      avahi_threaded_poll_quit(dbs->service_poll);
    }
    break;

  case AVAHI_CLIENT_S_COLLISION:
    debug(2, "avahi: service client state is AVAHI_CLIENT_S_COLLISION...needs a rename: %s",
          service_name);
    break;

  case AVAHI_CLIENT_CONNECTING:
    debug(2, "avahi: service client received AVAHI_CLIENT_CONNECTING");
    break;

  default:
    debug(1, "avahi: unexpected and unhandled avahi service client state: %d", state);
    break;
  }
}

static int avahi_register(char *srvname, int srvport) {
  debug(1, "avahi: avahi_register.");
  service_name = strdup(srvname);
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

  if (service_name)
    free(service_name);
  service_name = NULL;
}

int avahi_dacp_monitor(rtsp_conn_info *conn) {

  dacp_browser_struct *dbs = (dacp_browser_struct *)malloc(sizeof(dacp_browser_struct));
  

  if (dbs == NULL)
    die("can not allocate a dacp_browser_struct.");
    
  conn->mdns_private_pointer = (void *)dbs;

  // create the threaded poll code
  int err;
  if (!(dbs->service_poll = avahi_threaded_poll_new())) {
    warn("couldn't create avahi threaded service_poll!");
    if (dbs) {
      free((char *)dbs);
    }
    conn->mdns_private_pointer = NULL;
    return -1;
  }

  // create the service client
  if (!(dbs->service_client =
            avahi_client_new(avahi_threaded_poll_get(dbs->service_poll), AVAHI_CLIENT_NO_FAIL,
                             service_client_callback, (void *)conn, &err))) {
    warn("couldn't create avahi service client: %s!", avahi_strerror(err));
    if (dbs) { // should free the threaded poll code
      avahi_threaded_poll_free(dbs->service_poll);
      free((char *)dbs);
    }
    conn->mdns_private_pointer = NULL;
    return -1;
  }

  /* Create the service browser */
  if (!(dbs->service_browser =
            avahi_service_browser_new(dbs->service_client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
                                      "_dacp._tcp", NULL, 0, browse_callback, (void *)conn))) {
    warn("Failed to create service browser: %s\n",
         avahi_strerror(avahi_client_errno(dbs->service_client)));
    if (dbs) { // should free the threaded poll code and the service client
      avahi_client_free(dbs->service_client);
      avahi_threaded_poll_free(dbs->service_poll);
      free((char *)dbs);
    }
    conn->mdns_private_pointer = NULL;
    return -1;
  }
  // start the polling thread
  if (avahi_threaded_poll_start(dbs->service_poll) < 0) {
    warn("couldn't start avahi service_poll thread");
    if (dbs) { // should free the threaded poll code and the service client and the service browser
      avahi_service_browser_free(dbs->service_browser);
      avahi_client_free(dbs->service_client);
      avahi_threaded_poll_free(dbs->service_poll);
      free((char *)dbs);
    }
    conn->mdns_private_pointer = NULL;
    return -1;
  }
  return 0;
}

void avahi_dacp_dont_monitor(rtsp_conn_info *conn) {
  dacp_browser_struct *dbs = (dacp_browser_struct *)conn->mdns_private_pointer;
  if (dbs) {
    // stop and dispose of everything
    if ((dbs)->service_poll)
      avahi_threaded_poll_stop((dbs)->service_poll);
    if ((dbs)->service_browser)
      avahi_service_browser_free((dbs)->service_browser);
    if ((dbs)->service_client)
      avahi_client_free((dbs)->service_client);
    if ((dbs)->service_poll)
      avahi_threaded_poll_free((dbs)->service_poll);
    free((char *)(dbs));
    conn->mdns_private_pointer = NULL;
  } else {
    debug(1, "DHCP Monitor is not running.");
  }
}

mdns_backend mdns_avahi = {.name = "avahi",
                           .mdns_register = avahi_register,
                           .mdns_unregister = avahi_unregister,
                           .mdns_dacp_monitor = avahi_dacp_monitor,
                           .mdns_dacp_dont_monitor = avahi_dacp_dont_monitor};
