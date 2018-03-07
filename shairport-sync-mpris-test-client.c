#include "mpris-interface.h"
#include "mpris-player-interface.h"
#include <popt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

GMainLoop *loop;

void on_properties_changed(__attribute__((unused)) GDBusProxy *proxy, GVariant *changed_properties,
                           const gchar *const *invalidated_properties,
                           __attribute__((unused)) gpointer user_data) {
  /* Note that we are guaranteed that changed_properties and
   * invalidated_properties are never NULL
   */

  if (g_variant_n_children(changed_properties) > 0) {
    GVariantIter *iter;
    const gchar *key;
    GVariant *value;

    g_print(" *** Properties Changed:\n");
    g_variant_get(changed_properties, "a{sv}", &iter);
    while (g_variant_iter_loop(iter, "{&sv}", &key, &value)) {
      gchar *value_str;
      value_str = g_variant_print(value, TRUE);
      g_print("      %s -> %s\n", key, value_str);
      g_free(value_str);
    }
    g_variant_iter_free(iter);
  }

  if (g_strv_length((GStrv)invalidated_properties) > 0) {
    guint n;
    g_print(" *** Properties Invalidated:\n");
    for (n = 0; invalidated_properties[n] != NULL; n++) {
      const gchar *key = invalidated_properties[n];
      g_print("      %s\n", key);
    }
  }
}

pthread_t dbus_thread;
void *dbus_thread_func(__attribute__((unused)) void *arg) {

  loop = g_main_loop_new(NULL, FALSE);

  g_main_loop_run(loop);
  return NULL; // this is just to quieten a compiler warning.
}

int main(int argc, char *argv[]) {
  GBusType gbus_type_selected = G_BUS_TYPE_SYSTEM; // set default
  // get the options --system or --session for system bus or session bus
  signed char c;      /* used for argument parsing */
  poptContext optCon; /* context for parsing command-line options */

  struct poptOption optionsTable[] = {
      {"system", '\0', POPT_ARG_VAL, &gbus_type_selected, G_BUS_TYPE_SYSTEM,
       "Listen on the D-Bus system bus -- pick this option or the \'--session\' option, but not "
       "both. This is the default if no option is chosen.",
       NULL},
      {"session", '\0', POPT_ARG_VAL, &gbus_type_selected, G_BUS_TYPE_SESSION,
       "Listen on the D-Bus session bus -- pick this option or the \'--system\' option, but not "
       "both.",
       NULL},
      POPT_AUTOHELP{NULL, 0, 0, NULL, 0, NULL, NULL}};

  optCon = poptGetContext(NULL, argc, (const char **)argv, optionsTable, 0);
  poptSetOtherOptionHelp(optCon, "[--system | --session]");

  if (argc > 2) {
    poptPrintHelp(optCon, stderr, 0);
    exit(1);
  }

  /* Now do options processing */
  while ((c = poptGetNextOpt(optCon)) >= 0) {
  }

  if (c < -1) {
    /* an error occurred during option processing */
    fprintf(stderr, "%s: %s\n", poptBadOption(optCon, POPT_BADOPTION_NOALIAS), poptStrerror(c));
    return 1;
  }

  poptFreeContext(optCon);

  printf("Listening on the D-Bus %s bus...\n",
         (gbus_type_selected == G_BUS_TYPE_SYSTEM) ? "system" : "session");

  pthread_create(&dbus_thread, NULL, &dbus_thread_func, NULL);

  MediaPlayer2Player *proxy;

  GError *error = NULL;

  proxy = media_player2_player_proxy_new_for_bus_sync(gbus_type_selected, G_DBUS_PROXY_FLAGS_NONE,
                                                      "org.mpris.MediaPlayer2.ShairportSync",
                                                      "/org/mpris/MediaPlayer2", NULL, &error);
  g_signal_connect(proxy, "g-properties-changed", G_CALLBACK(on_properties_changed), NULL);
  // g_main_loop_quit(loop);
  pthread_join(dbus_thread, NULL);
  printf("exiting program.\n");

  g_object_unref(proxy);

  return 0;
}
