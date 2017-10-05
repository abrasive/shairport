
#include "shairportsync.h"
#include <stdio.h>
#include <unistd.h>

GMainLoop *loop;

void on_properties_changed(GDBusProxy *proxy, GVariant *changed_properties,
                           const gchar *const *invalidated_properties, gpointer user_data) {
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

void notify_loudness_filter_active_callback(ShairportSync *proxy, gpointer user_data) {
  //  printf("\"notify_loudness_filter_active_callback\" called with a gpointer of
  //  %lx.\n",(int64_t)user_data);
  gboolean ebl = shairport_sync_get_loudness_filter_active(proxy);
  if (ebl == TRUE)
    printf("Client reports loudness is enabled.\n");
  else
    printf("Client reports loudness is disabled.\n");
}

void notify_loudness_threshold_callback(ShairportSync *proxy, gpointer user_data) {
  gdouble th = shairport_sync_get_loudness_threshold(proxy);
  printf("Client reports loudness threshold set to %.2f dB.\n", th);
}

void notify_volume_callback(ShairportSync *proxy, gpointer user_data) {
  gdouble th = shairport_sync_get_volume(proxy);
  printf("Client reports volume set to %.2f dB.\n", th);
}

pthread_t dbus_thread;
void *dbus_thread_func(void *arg) {

  loop = g_main_loop_new(NULL, FALSE);

  g_main_loop_run(loop);

  //  dbus_service_main(); // let it run inside a thread
}

void main(void) {

  pthread_create(&dbus_thread, NULL, &dbus_thread_func, NULL);

  ShairportSync *proxy;
  GError *error = NULL;

  proxy = shairport_sync_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE,
                                                "org.gnome.ShairportSync",
                                                "/org/gnome/ShairportSync", NULL, &error);

  // g_signal_connect(proxy, "notify::loudness-filter-active",
  // G_CALLBACK(notify_loudness_filter_active_callback), NULL);

  g_signal_connect(proxy, "g-properties-changed", G_CALLBACK(on_properties_changed), NULL);
  g_signal_connect(proxy, "notify::loudness-threshold",
                   G_CALLBACK(notify_loudness_threshold_callback), NULL);
  g_signal_connect(proxy, "notify::volume", G_CALLBACK(notify_volume_callback), NULL);

  g_print("Starting test...\n");

  shairport_sync_set_volume(SHAIRPORT_SYNC(proxy), -20.0);
  sleep(10);
  shairport_sync_set_volume(SHAIRPORT_SYNC(proxy), -10.0);
  sleep(10);
  shairport_sync_set_volume(SHAIRPORT_SYNC(proxy), 0.0);
  sleep(10);

  shairport_sync_set_loudness_filter_active(SHAIRPORT_SYNC(proxy), FALSE);
  sleep(15);
  shairport_sync_set_loudness_threshold(SHAIRPORT_SYNC(proxy), -20.0);
  shairport_sync_set_loudness_filter_active(SHAIRPORT_SYNC(proxy), TRUE);
  sleep(15);
  shairport_sync_set_loudness_filter_active(SHAIRPORT_SYNC(proxy), FALSE);
  sleep(5);
  shairport_sync_set_loudness_threshold(SHAIRPORT_SYNC(proxy), -10.0);
  shairport_sync_set_loudness_filter_active(SHAIRPORT_SYNC(proxy), TRUE);
  sleep(15);
  shairport_sync_set_loudness_filter_active(SHAIRPORT_SYNC(proxy), TRUE);
  sleep(15);
  g_print("Finished test...\n");
  g_main_loop_quit(loop);
  pthread_join(dbus_thread, NULL);
  printf("exiting program.\n");

  g_object_unref(proxy);
}
