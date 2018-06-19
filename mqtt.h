#ifndef MQTT_H
#define MQTT_H
#include <stdint.h>
#include <mosquitto.h>


int initialise_mqtt();
void mqtt_process_metadata(uint32_t type, uint32_t code, char *data, uint32_t length);
void mqtt_publish(char* topic, char* data, uint32_t length);
void mqtt_setup();
void on_connect(struct mosquitto* mosq, void* userdata, int rc);
void on_disconnect(struct mosquitto* mosq, void* userdata, int rc);
void on_message(struct mosquitto* mosq, void* userdata, const struct mosquitto_message* msg);
void _cb_log(struct mosquitto *mosq, void *userdata, int level, const char *str);
#endif /* #ifndef MQTT_H */
