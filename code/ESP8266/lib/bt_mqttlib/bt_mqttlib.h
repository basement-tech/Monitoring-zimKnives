/*
 * bt_mqttlib.h
 * a wrapper for pubsub and a simple json parser
 */
#ifndef __BT_MQTTLIB_H__
#define  __BT_MQTTLIB_H__

#include "Arduino.h"
#include <PubSubClient.h>
#include <bt_eepromlib.h>

#define TOPIC_TEST "PamTest-esp8266"

/*
 * list of topics to which to subscribe
 */
#define PARM_UND   -1  /* undefined */
#define PARM_INT    0
#define PARM_FLOAT  1
#define PARM_BOOL   2
#define PARM_STRING 3

/*
 * structure to hold the topics to which to subscribe on 
 * the mosquitto data broker
 */
struct parameter {
  char topic[64];  /* used as the key into this list */
  char value[64];  /* value as string */
  int  parm_type;  /* convert from string according to this */
  bool valid;      /* set to true once a value has been received */
};

extern parameter parameters[];

int parm_to_value(char *topic, int *num_value);
int parm_to_value(char *topic, uint8_t *num_value);
int parm_to_value(char *topic, float *num_value);
int parm_to_value(char *topic, char *num_value);
int parm_to_value(char *topic, bool *num_value);
int set_parm_stvalue(char *topic, char *value);
int set_parm_valid(char *topic, bool valid);
int get_parm_valid(char *topic, bool *valid);

extern class PubSubClient mqtt;

void callback(char* topic, byte* payload, unsigned int length);
String macToStr(const uint8_t* mac);
bool MQTT_Subscribe();
bool LMQTTConnect(bool first, char *mqtt_server, char *nodeid);
String json_sample(String parm, float value, String location, String tstamp);
String json_sample(String parm, long value, String location, String tstamp);
String json_sample(String parm, String value, String location, String tstamp);
String json_sample(String parm, int value, String location, String tstamp);
int simple_json_parser(char buffer[]);

/*
 * Some parameters for simple json parsing ... VERY LIMITED
 */
#define JSON_DEPTH_LMT 5    /* how many layers can the json string have */
#define JSON_LVBUF_SIZE 64  /* label and value string buffer size */

struct json_parts {
  char label[JSON_LVBUF_SIZE];
  char value[JSON_LVBUF_SIZE];
  char *pvalue;  /* switch between the label and value buffers */
  bool closed; /* has this level been closed i.e. {}, used for error checking */
};
#endif
