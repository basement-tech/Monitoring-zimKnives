/*
 * bt_mqttlib.c
 * a wrapper for pubsub and a simple json parser
 */

#include "bt_mqttlib.h"
#include "Arduino.h"

/*
 * buffer for the incoming mqtt message; used by the json parser
 */
char mqtt_incoming[JSON_LVBUF_SIZE];

/*
 * as it says, a place to put the components of a deconstructed json string
 */
struct json_parts json_parts[JSON_DEPTH_LMT];
int    json_level = 0;

/*
 * convert the string based value from the json string
 * to the appropriate type.
 * Note: collection of three functions based on parameter type
 * returns -1: if the topic is not found or the value didn't make sense
 */
int parm_to_value(char *topic, int *num_value)  {
  int status = -1;
  for(int i = 0; parameters[i].parm_type != PARM_UND; i++) {
    if(strcmp(topic, parameters[i].topic) == 0)  {
      *num_value = atoi(parameters[i].value);
      status = 1;
    }
  }
  return(status);
}
int parm_to_value(char *topic, uint8_t *num_value)  {
  int status = -1;
  for(int i = 0; parameters[i].parm_type != PARM_UND; i++) {
    if(strcmp(topic, parameters[i].topic) == 0)  {
      *num_value = atoi(parameters[i].value);
      status = 1;
    }
  }
  return(status);
}
int parm_to_value(char *topic, float *num_value)  {
  int status = -1;
  for(int i = 0; parameters[i].parm_type != PARM_UND; i++) {
    if(strcmp(topic, parameters[i].topic) == 0)  {
      *num_value = atof(parameters[i].value);
      status = 1;
    }
  }
  return(status);
}
int parm_to_value(char *topic, char *num_value)  {
  int status = -1;
  for(int i = 0; parameters[i].parm_type != PARM_UND; i++) {
    if(strcmp(topic, parameters[i].topic) == 0)  {
      strcpy(num_value, parameters[i].value);
      status = 1;
    }
  }
  return(status);
}
int parm_to_value(char *topic, bool *num_value)  {
  int status = -1;
  for(int i = 0; parameters[i].parm_type != PARM_UND; i++) {
    if(strcmp(topic, parameters[i].topic) == 0)  {
      status = 1; /* the topic was found ... so far, so good */
      if(strcmp(parameters[i].value, "false") == 0)
        *num_value = false;
      else if(strcmp(parameters[i].value, "true") == 0)
        *num_value = true;
      else  /* bad value string */
        status = -1;
    }
  }
  return(status);
}


/*
 * based on the topic, set the string based value in the parameter[]
 * returns -1: if the topic is not found
 */
int set_parm_stvalue(char *topic, char *value)  {
  int status = -1;
  for(int i = 0; parameters[i].parm_type != PARM_UND; i++) {
    if(strcmp(topic, parameters[i].topic) == 0)  {
      strcpy(parameters[i].value, value);
      status = 1;
    }
  }
  return(status);
}

/*
 * based on the topic, set the data valid flag
 * returns -1: if the topic is not found
 */
int set_parm_valid(char *topic, bool valid)  {
  int status = -1;
  for(int i = 0; parameters[i].parm_type != PARM_UND; i++) {
    if(strcmp(topic, parameters[i].topic) == 0)  {
      parameters[i].valid = true;
      status = 1;
    }
  }
  return(status);
}

/*
 * based on the topic, set the data valid flag
 * returns -1: if the topic is not found
 */
int get_parm_valid(char *topic, bool *valid)  {
  int status = -1;
  for(int i = 0; parameters[i].parm_type != PARM_UND; i++) {
    if(strcmp(topic, parameters[i].topic) == 0)  {
      *valid = parameters[i].valid;
      status = 1;
    }
  }
  return(status);
}


/*
 * mqtt incoming (subscribed) message handling
 * NOTE: all incoming messages are decoded as json strings
 * NOTE: currently all incoming messages are expected to be neopxl_mode
 */
void callback(char* topic, byte* payload, unsigned int length) {
  int i = 0;

  Serial.print("Message back from broker, topic:"); Serial.println(topic);

  for (i=0;i<length;i++) {
    mqtt_incoming[i] = payload[i];
  }
  mqtt_incoming[i] = '\0';
  Serial.print("Payload ->");Serial.print(mqtt_incoming);Serial.println("<-");


  /*
   * parse the json string and remember the deepest level
   */
  json_level = simple_json_parser(mqtt_incoming);

#ifdef FL_DEBUG_MSG
  /*
   * burp out the json parts structure
   */
  Serial.print("json_level = ");Serial.println(json_level);
  for (i = 0; i < JSON_DEPTH_LMT; i++)  {
    Serial.print("json_parts[");Serial.print(i);Serial.print("].label = ->");
    Serial.print(json_parts[i].label);Serial.print("<-");
    Serial.print(" value = ->");Serial.print(json_parts[i].value);Serial.println("<-");
  }
#endif
  /* successful parsing, set the value in the parameters array */
  if(json_level >= 0) {
    if(set_parm_stvalue(topic, json_parts[json_level].value) < 0) {
      Serial.print("callback():error setting value in parameters[], topic: ->");
      Serial.print(topic);Serial.println("<-");
    }
    /*
     * put this here instead of inside set_parm_stvalue() so that
     * it takes an mqtt message to cause the valid flag to be set
     */
    else
      set_parm_valid(topic, true);
  }
  /* error parsing */
  else
      Serial.println("callback(): error in json string parsing");
}


// Convert a mac address to a string
String macToStr(const uint8_t* mac)
{
  String result;
  for (int i = 0; i < 6; ++i) {
    result += String(mac[i], 16);
    if (i < 5)
      result += ':';
  }
  return result;
}

/*
 * Subscribe to all topics in the mqtt_subs[] list
 *
 * Try to subscribe to all and return false if any fail
 */
bool MQTT_Subscribe()  {
  bool status = true;
  int i = 0;

  while(parameters[i].parm_type != PARM_UND)  {
    if(mqtt.subscribe(parameters[i].topic) == false)  {
      status = false;
      Serial.print("MQTT subscribe failed for: "); Serial.println(parameters[i].topic);
    }
    else  {
      Serial.print("MQTT subscribe succeeded for: "); Serial.println(parameters[i].topic);
    }
    i++;
  }
  return(status);
}


/*
 * Connect or re-connect to the mqtt data broker
 */
bool LMQTTConnect(bool first, char *mqtt_server)  {

  bool status = false;

  // Generate client name based on MAC address and last 8 bits of microsecond counter
  String clientName;
  clientName += "esp8266-";
  uint8_t mac[6];
  WiFi.macAddress(mac);
  clientName += macToStr(mac);
  clientName += "-";
  clientName += String(micros() & 0xff, 16);

  if(first == true)
    Serial.print("Attempting connection to MQTT ");
  else
    Serial.print("Attempting re-connection to MQTT ");
  Serial.print(mqtt_server);
  Serial.print(" as ");
  Serial.println(clientName);

  if (mqtt.connect((char*) clientName.c_str())) {
    Serial.println("Connected to MQTT broker");
    Serial.print("Topic is: ");
    Serial.println(TOPIC_TEST);

    if (mqtt.publish(TOPIC_TEST, "hello from ESP8266")) {
      Serial.println("Initial Publish ok");
    }
    else {
      Serial.println("Initial Publish failed");
      status = false;
    }
  }
  else {
    Serial.println("MQTT connect failed");
    Serial.println("Will try again...");
    status = false;
  }
  return(status);
}



/*
 * Form up a json payload.  Overload for three parm types.
 * (could have found a json library, but this seemed pretty trivial)
 */
String json_sample(String parm, float value, String location, String tstamp)  {

  String enviro;

  enviro =          "{ \"" + parm + "\":";
  enviro = enviro +   "{ " + "\"value\": " + String(value) + ",";
  enviro = enviro +          "\"location\": \"" + location + "\",";
  enviro = enviro +          "\"tstamp\": \"" + tstamp + "\"";
  enviro = enviro +   "}";
  enviro = enviro + "}";

  return(enviro);
}
String json_sample(String parm, long value, String location, String tstamp)  {

  String enviro;

  enviro =          "{ \"" + parm + "\":";
  enviro = enviro +   "{ " + "\"value\": " + String(value) + ",";
  enviro = enviro +          "\"location\": \"" + location + "\",";
  enviro = enviro +          "\"tstamp\": \"" + tstamp + "\"";
  enviro = enviro +   "}";
  enviro = enviro + "}";

  return(enviro);
}
String json_sample(String parm, String value, String location, String tstamp)  {

  String enviro;

  enviro =          "{ \"" + parm + "\":";
  enviro = enviro +   "{ " + "\"value\": \"" + value + "\",";
  enviro = enviro +          "\"location\": \"" + location + "\",";
  enviro = enviro +          "\"tstamp\": \"" + tstamp + "\"";
  enviro = enviro +   "}";
  enviro = enviro + "}";

  return(enviro);
}
String json_sample(String parm, int value, String location, String tstamp)  {

  String enviro;

  enviro =          "{ \"" + parm + "\":";
  enviro = enviro +   "{ " + "\"value\": \"" + value + "\",";
  enviro = enviro +          "\"location\": \"" + location + "\",";
  enviro = enviro +          "\"tstamp\": \"" + tstamp + "\"";
  enviro = enviro +   "}";
  enviro = enviro + "}";

  return(enviro);
}




/*
 * this is a super-simple json parser.
 * it expects a global structure of the type json_parts to be declared.
 * it cannot handle ","'s so only one value is supported.
 *
 * char buffer [] : the json string to parse
 * returns:  max_depth: the lowest level index (not count) into json_parts[]
 *          -1:         if an error occured
 *
 * checks to make sure all levels are closed and returns error if not
 */
int simple_json_parser(char jbuffer[])
{

  char *pbuffer = jbuffer;
  int depth = -1; /* how far deep into the string and buffer */
  int max_depth = 0;
  int i = 0, j = 0, k = 0;

  /*
   * initialize the array of structures tha will hold
   * the resultant pieces of the json string
   */
  for(i = 0; i < JSON_DEPTH_LMT; i++)  {
    json_parts[i].pvalue = json_parts[i].label;
    json_parts[i].label[0] = '\0';
    json_parts[i].value[0] = '\0';
    json_parts[i].closed = true;
  }
#ifdef FFL_DEBUG_MSG
  Serial.println("json_parts[] initialized to:");
  for (i = 0; i < JSON_DEPTH_LMT; i++)  {
    Serial.print("json_parts[");Serial.print(i);Serial.print("].label = ->");
    Serial.print(json_parts[i].label);Serial.print("<-");
    Serial.print(" value = ->");Serial.print(json_parts[i].value);Serial.println("<-");
  }
#endif
  while(*pbuffer != '\0')  {
#ifdef FFL_DEBUG_MSG
    Serial.print("Processing :");Serial.println(*pbuffer);
#endif
    switch(*pbuffer)  {
      case ' ':
        break;
      case '{':
        depth++; /* starting a new label for a new pair */
#ifdef FFL_DEBUG_MSG
        Serial.print("opening level:");Serial.println(depth);
#endif
        if(depth > max_depth)  /* for the return value */
          max_depth = depth;
        json_parts[depth].pvalue = json_parts[depth].label;  /* technically not necessary */
        json_parts[depth].closed = false;  /* this level is open */

        /*
         * fill the character into the value string for all to the start
         * don't put it in the current value string
         */
        k = depth - 1;
        while(k >= 0)  {
          *(json_parts[k].pvalue++) = *pbuffer;
          k--;
        }

        break;

      case '}':  /* always terminating a value */
#ifdef FFL_DEBUG_MSG
        Serial.print("closing level:");Serial.println(depth);
#endif
        *(json_parts[depth].pvalue) = '\0';
        json_parts[depth].closed = true;

        /*
         * fill the character into the value string for all to the start
         * don't put it in the current value string
         */
        k = depth - 1;
        while(k >= 0)  {
          *(json_parts[k].pvalue)++ = *pbuffer;
          k--;
        }

        depth--;

        break;

      case ':':  /* colon will always terminate label */

        *(json_parts[depth].pvalue) = '\0';  /* terminate the label */

        /*
         * fill the character into the value string for all to the start
         * don't put it in the current value string
         */
        k = depth - 1;
        while(k >= 0)  {
          *(json_parts[k].pvalue)++ = *pbuffer;
          k--;
        }

        json_parts[depth].pvalue = json_parts[depth].value;
#ifdef FFL_DEBUG_MSG
        Serial.print("switched to .value on level:");Serial.println(depth);
#endif
        break;

      default:
        /*
         * fill in all of the active .value buffers
         */
        for(int k = depth; k >= 0; k--)  {
          *(json_parts[k].pvalue++) = *pbuffer;
        }
        break;
    };

    pbuffer++;
  }
  for(i = 0; i < JSON_DEPTH_LMT; i++)  {
    if(json_parts[i].closed == false)
      return(-1);
  }
#ifdef FFL_DEBUG_MSG
    Serial.println("json_parts[] after parsing:");
    for (i = 0; i < JSON_DEPTH_LMT; i++)  {
      Serial.print("json_parts[");Serial.print(i);Serial.print("].label = ->");
      Serial.print(json_parts[i].label);Serial.print("<-");
      Serial.print(" value = ->");Serial.print(json_parts[i].value);Serial.println("<-");
    }
#endif
  return(max_depth);
}

