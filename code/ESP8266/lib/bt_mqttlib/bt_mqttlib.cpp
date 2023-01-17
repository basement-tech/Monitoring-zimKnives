/*
 * bt_mqttlib.c
 * a wrapper for pubsub and a simple json parser
 *
 * bt_mqttlib was originally written to implement a monitoring application 
 * where physical sensor values are json encoded and sent to a mosquitto 
 * data broker to eventually be displayed using nodered.  Other topics are 
 * subscribed to for control.
 *
 * LMQTTConnect() is called in the sketches setup() portion to establish the connection
 * to the data broker.  The argument first is set to true on the first callin setup().
 * After that the function is expected to be called in several second intervals with the
 * argument first set to false to check the connection status and attempt reconnects if
 * necessary.
 *
 * "subscribed" (i.e. data flows from the data broker to this lib) parameter management:
 * The main application is expected to fill up the struct parameter[] with a list 
 * of topics to which to subscribe.  The callback() function copies json decoded,
 * using simple_json_parser(), character string values to the parameter[].value string.
 * parm_to_value() is an overloaded (based on parameter.parm_type), set of functions
 * for getting values from the parameters[] structure referenced by topic.
 * The array of structures is terminated by a final entry with the parm_type set to PARM_UND.
 * MQTT_Subscribe() must be called in the setup() portion of the sketch.
 *
 * "published" topics are created on the broker when the first instance of the value is sent.
 * The overloaded json_sample() creates json encoded strings, using brute force string 
 * concatination, to be sent to the broker in the loop() part of the broker.  There is no
 * function provided to send them in mass since different intervals and timings may be desired.
 *
 * Note: the simple_json_parser() is super-simple.
 *
 */

#include "bt_mqttlib.h"
#include "Arduino.h"

#define FL_DEBUG_MSG
//#define FFL_DEBUG_MSG

/*
 * buffer for the incoming mqtt message; used by the json parser
 */
char mqtt_incoming[JSON_LVBUF_SIZE];



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
#ifdef FFL_DEBUG_MSG
  Serial.print("Setting >"); Serial.print(topic); Serial.print("< to >");
  Serial.print(value); Serial.println("<");
#endif

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
 * return the index of the child at the depth given
 * of the "value" value 
 */
int find_value_among_children(json_parts_c_t *json_parts, int depth)  {
  int status = -1;
  int j = 0;

  /*
   * find the source of the value among the children
   */
  while((json_parts[depth].child[j].label[0] != '\0') && (j < JSON_CHILDREN))  {
     if(strcmp("\"value\"", json_parts[depth].child[j].label) == 0)
        status = j;
     j++;
  }
#ifdef FFL_DEBUG_MSG
  Serial.print("find_value_among_children returning "); Serial.println(status);
#endif
  return(status);
}

/*
 * mqtt incoming (subscribed) message handling
 * NOTE: all incoming messages are decoded as json strings
 * NOTE: currently all incoming messages are expected to be neopxl_mode
 */
void callback(char* topic, byte* payload, unsigned int length) {
  int i = 0;
  int n_child = 0;
 
  json_parts_c_t json_parts_c[JSON_DEPTH_LMT]; /* a place to put the components of a deconstructed json string */
  int    json_level = 0;

  Serial.print("Message back from broker, topic:"); Serial.println(topic);

  for (i=0;i<length;i++) {
    mqtt_incoming[i] = payload[i];
  }
  mqtt_incoming[i] = '\0';
  Serial.print("Payload ->");Serial.print(mqtt_incoming);Serial.println("<-");


  /*
   * parse the json string and remember the deepest level
   */
  json_level = simple_json_parser_children(json_parts_c, mqtt_incoming);

#ifdef FL_DEBUG_MSG
  /*
   * burp out the json parts structure
   */
  Serial.print("json_level = "); Serial.println(json_level);
  for (int i = 0; i < JSON_DEPTH_LMT; i++)  {
    for(int j = 0; j < JSON_CHILDREN; j++)  {
       Serial.print("json_parts["); Serial.print(i); Serial.print("].child["); Serial.print(j);
       Serial.print("].label = ->"); Serial.print(json_parts_c[i].child[j].label); Serial.print("<-");
       Serial.print("  value = ->"); Serial.print(json_parts_c[i].child[j].value); Serial.println("<-");
    }
  }
#endif
  /* successful parsing, set the value in the parameters array */
  if(json_level >= 0) {

    /*
     * find the value json label among the children
     */
    if((n_child = find_value_among_children(json_parts_c, json_level)) < 0)  {
      Serial.print("callback():error finding value in json_parts[], topic: ->");
      Serial.print(topic);Serial.println("<-");
    }

    /*
     * find the destination of the value for the subject
     * topic and set its value
     */
    else if(set_parm_stvalue(topic, json_parts_c[json_level].child[n_child].value) < 0) {
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

  Serial.println("Exiting mqtt callback");
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
bool LMQTTConnect(bool first, char *mqtt_server, char *nodeid)  {

  bool status = false;

  // Generate client name based on MAC address and last 8 bits of microsecond counter
  String clientName;
  clientName += "esp8266-";
  clientName += nodeid;
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



#ifdef INC_DEPRECATED
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
#endif

/*
 * this is a super-simple json parser.
 * it expects a global structure of the type json_parts to be declared.
 *
 * results are deposited in the structure json_parts of type json_parts_t.
 * if the top leval has children, they are deposited one level lower in the structure:
 * 
 * strcpy(buffer, "{ \"temp\":{ \"value\": 8.23,\"location\": \"garage\",\"tstamp\": \"22\\:59\\:55\"}}");
 * simple_json_parser_children(buffer);
 * yields:
 * parser returned 1
 * json_parts[0].child[0].label = ->"temp"<- value = -><-
 * json_parts[0].child[1].label = -><- value = -><-
 * json_parts[0].child[2].label = -><- value = -><-
 * json_parts[0].child[3].label = -><- value = -><-
 * json_parts[0].child[4].label = -><- value = -><-
 * json_parts[1].child[0].label = ->"value"<- value = ->8.23<-
 * json_parts[1].child[1].label = ->"location"<- value = ->"garage"<-
 * json_parts[1].child[2].label = ->"tstamp"<- value = ->"22:59:55"<-
 *
 * NOTE: special characters can be escaped with a leading '\'
 *
 * char buffer [] : the json string to parse
 * returns:  max_depth: the lowest level index (not count) into json_parts[]
 *          -1:         if an error occured
 *
 * checks to make sure all levels are closed and returns error if not
 */
int simple_json_parser_children(json_parts_c_t *json_parts, char jbuffer[])
{

  char *pbuffer = jbuffer; /* string to be parsed */
  char *pos = NULL; /* pointer to where we are in the string during parsing */
  int depth = -1; /* how far deep into the string and buffer */
  int cchild = 0; /* child counter */
  uint8_t echar = false; /* pending escaped character */
  int max_depth = 0;
  int i = 0, j = 0, k = 0;

  /*
   * initialize the array of structures tha will hold
   * the resultant pieces of the json string
   */
  for(i = 0; i < JSON_DEPTH_LMT; i++)  {
    json_parts[i].closed = true;
    for(j = 0; j < JSON_CHILDREN; j++)  {
       json_parts[i].child[j].label[0] = '\0';
       json_parts[i].child[j].value[0] = '\0';
    }
  }


  /*
   * parse the buffer
   */
  i = 0; j = 0; k = 0;  /* just for good measure */
  while(*pbuffer != '\0')  {
#ifdef FFL_DEBUG_MSG
    Serial.print("Processing :");Serial.println(*pbuffer);
#endif
    /*
     * process the escaped character if pending
     */
    if(echar == true)  {
      echar = false; /* clear the escaped character flag */
#ifdef FFL_DEBUG_MSG
      printf("Escaping >%c<\n", *pbuffer);
#endif
      *pos++ = *pbuffer;
    }
    else  {
      switch(*pbuffer)  {

        case ' ': /* skip blanks */
          break;
        
        case '\\': /* escape the next character */
          echar = true;
          break;

        case '{':
          depth++; /* starting a new label for a new pair */
#ifdef FFL_DEBUG_MSG
          Serial.print("opening level:");Serial.println(depth);
          printf("opening level: %d\n", depth);
#endif
          json_parts[depth].closed = false;
          if(depth > max_depth)  /* for the return value */
            max_depth = depth;
          pos = json_parts[depth].child[cchild].label;  /* move to the label field */

          break;


        case ',':  /* comma will always terminate a value, but not a level */
#ifdef FFL_DEBUG_MSG
          Serial.print("closing child:");Serial.println(j);
#endif
          *pos = '\0';  /* terminate the value */
          cchild++; /* move to the next child in the level */
#ifdef FFL_DEBUG_MSG
          printf("Moving to level %d child %d\n", depth, cchild);
#endif
          pos = json_parts[depth].child[cchild].label;  /* move to the label field */

          break;


        case '}':  /* always terminating a value */
#ifdef FFL_DEBUG_MSG
          Serial.print("closing level:");Serial.println(depth);
#endif
          *pos = '\0'; /* terminate over the '}' */
          json_parts[depth].closed = true;

          depth--;

          break;

        case ':':  /* colon will always terminate label */

          *pos = '\0';  /* terminate the label */
          pos = json_parts[depth].child[cchild].value;  /* move to the value field */

          break;

        /*
        * non-special character: fill it in to the currently active buffer
        */
        default:
#ifdef FFL_DEBUG_MSG
          printf("*pbuffer = %c\n", *pbuffer);
#endif
          *pos++ = *pbuffer;
          break;
      };  /* of switch */
    } /* if echar */
    pbuffer++;
  }  /* of while */
  for(i = 0; i < JSON_DEPTH_LMT; i++)  {
    if(json_parts[i].closed == false)
      return(-1);
  }

  return(max_depth);
}
