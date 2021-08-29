/*
 * EEPROM
 * ------
 * this section deals with writing the persistent parameters
 * to the EEPROM and reading them on subsequent reboots
 * E.g. WIFI credentials
 */
 
#include <EEPROM.h>
#include "bt_eepromlib.h"

/*
 * place to hold the settings for network, mqtt, calibration, etc.
 */
struct net_config mon_config;

/*
 * this section deals with getting the user input to
 * potentially change the eeprom parameter values
 * (e.g. change the WIFI credentials)
 * NOTE: cannot be merged with the net_config structure because
 * the structure determines the contents of the eeprom data.
 */
struct eeprom_in  {
  char prompt[64];  /* user visible prompt */
  char label[32];   /* label when echoing contents of eeprom */
  char *value;      /* pointer to the data in net_config (mon_config) */
  int  buflen;      /* length of size in EEPROM */
};

#define EEPROM_ITEMS 11
struct eeprom_in eeprom_input[EEPROM_ITEMS] {
  {"",                                       "Validation",    mon_config.valid,            sizeof(mon_config.valid)},
  {"Enter WIFI SSID",                        "WIFI SSID",     mon_config.wlan_ssid,        sizeof(mon_config.wlan_ssid)},
  {"Enter WIFI Password",                    "WIFI Password", mon_config.wlan_pass,        sizeof(mon_config.wlan_pass)},
  {"Enter mqtt server IP address (x.x.x.x)", "mqtt server",   mon_config.mqtt_server,      sizeof(mon_config.mqtt_server)},
  {"Enter mqtt server port",                 "mqtt port",     mon_config.mqtt_server_port, sizeof(mon_config.mqtt_server_port)},
  {"Enter location",                         "location",      mon_config.mqtt_location,    sizeof(mon_config.mqtt_location)},
  {"Enter GMT offset (+/- secs)",            "GMT offset",    mon_config.tz_offset_gmt,    sizeof(mon_config.tz_offset_gmt)},
  {"Enter cal data: Temp Offset (+/- degC)", "Temp Offset",   mon_config.temp_offset,      sizeof(mon_config.temp_offset)},
  {"Enter cal data: Humidity Offset (+/-%)", "Hum Offset",    mon_config.hum_offset,       sizeof(mon_config.hum_offset)},
  {"Enter cal data: ACS758 Offset (mV@0A)",  "ACS758 Offset", mon_config.acs758_offset,    sizeof(mon_config.acs758_offset)},
  {"Enter debug level (0 -> 9)",             "debug level",   mon_config.debug_level,      sizeof(mon_config.debug_level)},
};

void init_eeprom_input()  {
    eeprom_input[0].value = mon_config.valid;
    eeprom_input[1].value = mon_config.wlan_ssid;
    eeprom_input[2].value = mon_config.wlan_pass;
    eeprom_input[3].value = mon_config.mqtt_server;
    eeprom_input[4].value = mon_config.mqtt_server_port;
    eeprom_input[5].value = mon_config.mqtt_location;
    eeprom_input[6].value = mon_config.tz_offset_gmt;
    eeprom_input[7].value = mon_config.temp_offset;
    eeprom_input[8].value = mon_config.hum_offset;
    eeprom_input[9].value = mon_config.acs758_offset;
    eeprom_input[10].value = mon_config.debug_level;
}

/*
 * break the data compartmentalization a little and allow the calling
 * data space to access mon_config directly.  I'm hoping this will be read-only.
 */
net_config *get_mon_config_ptr(void) {
	return(&mon_config);
}

/*
 * prompt for and set one input in eeprom_input[].value.
 * return: that which comes back from l_read_string()
 */
int getone_eeprom_input(int i)  {
  char inbuf[64];
  int  insize;

  /*
   * if there is no prompt associated with the subject
   * parameter, skip it
   */
  if(eeprom_input[i].prompt[0] != '\0')  {
    Serial.print(eeprom_input[i].prompt);
    Serial.print("[");Serial.print(eeprom_input[i].value);Serial.print("]");
    Serial.print("(max ");Serial.print(eeprom_input[i].buflen - 1);Serial.print(" chars):");
    if((insize = l_read_string(inbuf, sizeof(inbuf), true)) > 0)  {
      if(insize < (eeprom_input[i].buflen))
        strcpy(eeprom_input[i].value, inbuf);
      else  {
        Serial.println(); 
        Serial.println("Error: too many characters; value will be unchanged");
      }
    }
    Serial.println();
  }
  return(insize);
}

void getall_eeprom_inputs()  {
  int i;
  int ret;
  
  Serial.println();    
  Serial.println("Press <enter> alone to accept previous EEPROM value shown");
  Serial.println("Press <esc> as the first character to skip to the end");
  Serial.println();

  /*
   * loop through getting all of the EEPROM parameter user inputs.
   * if <esc> (indicated by -2) is pressed, skip the balance.
   */
  i = 0;
  ret = 0;
  while((i < EEPROM_ITEMS) && (ret != -2))  {
    ret = getone_eeprom_input(i);
    i++;
  }
}

void dispall_eeprom_parms()  {
  
  Serial.println();    
  Serial.print("Local copy of EEPROM contents(");
  Serial.print(sizeof(mon_config));Serial.print(" of ");
  Serial.print(EEPROM_RESERVE); Serial.println(" bytes used):");

  /*
   * loop through getting all of the EEPROM parameter user inputs.
   * if <esc> (indicated by -2) is pressed, skip the balance.
   */
  for(int i = 0; i < EEPROM_ITEMS; i++)  {
    Serial.print(eeprom_input[i].label);
    Serial.print(" ->"); Serial.print(eeprom_input[i].value); Serial.println("<-");
  }
}

/*
 * read exactly the number of bytes from eeprom 
 * that match[] is long and compare to see if the eeprom has
 * ever been written with a valid set of data from this
 * exact revision.
 * 
 * returns the value of strcmp()
 */
bool eeprom_validation(char match[])  {
  int mlen;
  char ebuf[32];
  char in;
  int i;

  mlen = strlen(match);

  for(i = 0; i < mlen; i++)
    ebuf[i] = EEPROM.get(i, in);
  ebuf[i] = '\0';
  
#ifdef FL_DEBUG_MSG
  Serial.print("match ->");Serial.print(match); Serial.println("<-");
  Serial.print("ebuf ->");Serial.print(ebuf); Serial.println("<-");
#endif
  
  return(strcmp(match, ebuf));
}

/*
 * OK, I just got tired of trying to figure out the available libraries.
 * This function reads characters from the Serial port until an end-of-line
 * of some sort is encountered.
 * 
 * I used minicom under ubuntu to interact with this function successfully.
 * 
 * buf : is a buffer to which to store the read data
 * blen : is meant to indicate the size of buf
 * echo : whether to echo the characters or not 
 * 
 * Return: (n)  the number of characters read, not counting the end of line,
 *              which is over-written with a string terminator ('\0')
 *         (-1) if the buffer was overflowed
 *         (-2) if the <esc> was entered as the first character
 * 
 */
int l_read_string(char *buf, int blen, bool echo)  {
  int count = 0;
  bool out = false;

  while((out == false) && (count < blen))  {
    if(Serial.available() > 0)  {
      *buf = Serial.read();
#ifdef FL_DEBUG_MSG
      Serial.print("char=");Serial.print(*buf);Serial.println(*buf, HEX);
#endif
      /*
       * echo if commanded to do so by the state of the echo argument.
       * don't echo the <esc>.
       */
      if((echo == true) && (*buf != '\x1B'))
        Serial.print(*buf);
      switch(*buf)  {
        /*
         * terminate the string and get out
         */
        case '\n':
          *buf = '\0';
          out = true;
        break;

        case '\r':
          *buf = '\0';
          out = true;
        break;

        /*
         * <escape> was entered
         * ignored if not the first character
         */
        case '\x1B':
          if(count == 0)  {
            out = true;
            count = -2;
          }
        break;

        /* 
         * backspace: don't increment the buffer pointer which
         * allows this character to be written over by the next
         */
        case '\b':
          if(count > 0)
            buf--;
            count--;
            Serial.print(" \b");  /* blank out the character */
          break;

        /*          
         * normal character
         */
        default:
          buf++;
          count++;
        break;
      }
    }
  }

  if(out == true) /* legitimate exit */
    return(count);
  else if (count == blen)  /* buffer size exceeded */
    return(-1);
}


/*
 * EEPROM init, read, write
 */
void eeprom_begin(void) {
	EEPROM.begin(EEPROM_RESERVE);
}

void eeprom_get(void) {
	EEPROM.get(0, mon_config);
}

void eeprom_put(void) {
	EEPROM.put(0, mon_config);
	EEPROM.commit();
}


