/*
 * bt_eeprom.h
 * ------
 * this section deals with writing the persistent parameters
 * to the EEPROM and reading them on subsequent reboots
 * E.g. WIFI credentials
 */

#ifndef __BT_EEPROM_H__
#define __BT_EEPROM_H__

#include <Arduino.h>

/*
 * this is the size of the EERPOM segment that is accessible
 * by the EEPROM class.
 * this has to be greater than or equal to 
 * the size of the net_config struct below.
 */
 
#define EEPROM_RESERVE 1024

/*
 * map of the parameters stored in EEPROM
 * the EEPROM class is smart enough to deal with
 * custom types, so even if things are added here,
 * the rest of the code should work.
 */
struct net_config  {
char valid[32];            /* eeprom version validation string */
char wlan_ssid[64];        /* wifi ssid */
char wlan_pass[64];        /* wifi password */
char mqtt_server[64];      /* ip address of the mqtt data broker */
char mqtt_server_port[16]; /* ip port of mqtt data broker */
char mqtt_location[64];    /* geographical location of the sensor device */
char tz_offset_gmt[8];     /* sample time offset from GMT (+/-) in seconds (e.g. -21600 = CST) */
char temp_offset[8];       /* added (subtracted if -) to HTU21DF_P based temp */
char hum_offset[8];        /* added (subtracted if -) to HTU21DF_P based humidity */
char acs758_offset[8];     /* value in mV at 0 amps for ACS758 */
char debug_level[2];       /* display messages at different levels of detail */
};
 
/*
 * function declarations
 */
net_config *get_mon_config_ptr(void);
int getone_eeprom_input(int i);
void getall_eeprom_inputs();
void dispall_eeprom_parms();
bool eeprom_validation(char match[]);
int l_read_string(char *buf, int blen, bool echo);

void eeprom_begin(void);
void eeprom_get(void);
void eeprom_put(void);

#endif
