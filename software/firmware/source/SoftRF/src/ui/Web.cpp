/*
 * WebHelper.cpp
 * Copyright (C) 2016-2021 Linar Yusupov
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "../system/SoC.h"

#if defined(EXCLUDE_WIFI)
void Web_setup()    {}
void Web_loop()     {}
void Web_fini()     {}
#else

#include <Arduino.h>

#if defined(ESP32)
#include "SPIFFS.h"
#endif

#include "../system/SoC.h"
#include "../driver/Battery.h"
#include "../driver/RF.h"
#include "Web.h"
#include "../driver/EEPROM.h"
#include "../driver/OLED.h"
#include "../driver/Baro.h"
#include "../driver/LED.h"
#include "../driver/Buzzer.h"
#include "../driver/Voice.h"
#include "../driver/Bluetooth.h"
#include "../TrafficHelper.h"
#include "../protocol/radio/Legacy.h"
#include "../protocol/data/NMEA.h"
#include "../protocol/data/GDL90.h"
#include "../protocol/data/D1090.h"

#if defined(ENABLE_AHRS)
#include "../driver/AHRS.h"
#endif /* ENABLE_AHRS */

static uint32_t prev_rx_pkt_cnt = 0;

//static const char Logo[] PROGMEM = {
//#include "../Logo.h"
//    } ;

// #include "jquery_min_js.h"     - skipped to save space

byte getVal(char c)
{
   if(c >= '0' && c <= '9')
     return (byte)(c - '0');
   else
     return (byte)(toupper(c)-'A'+10);
}

#if DEBUG
void Hex2Bin(String str, byte *buffer)
{
  char hexdata[2 * PKT_SIZE + 1];
  
  str.toCharArray(hexdata, sizeof(hexdata));
  for(int j = 0; j < PKT_SIZE * 2 ; j+=2)
  {
    buffer[j>>1] = getVal(hexdata[j+1]) + (getVal(hexdata[j]) << 4);
  }
}
#endif

// Compiler said:
//  Global variables use 63296 bytes of dynamic memory, maximum is 327680 bytes.
// Saw this in serial output:
//  handleRoot()...
//  Free memory: 19904
//  Stopping Bluetooth for web access
//  Free memory: 68892
// But in v112 it is over 150000 - why?

static bool BTpaused = false;
static bool reboot_pending = false;

void stop_bluetooth()
{
  uint32_t freemem = ESP.getFreeHeap();
  Serial.print(F("Free memory: "));  Serial.println(freemem);
  if (!BTactive || BTpaused || freemem > 60000)
      return;
  if (settings->bluetooth != BLUETOOTH_OFF) {
      if (SoC->Bluetooth_ops) {
          Serial.println(F("Stopping Bluetooth for web access"));
          SoC->Bluetooth_ops->fini();
          BTpaused = true;
          OLED_msg("BT", "OFF");
      }
  }
  yield();
  Serial.print(F("Free memory (BT off): "));
  Serial.println(ESP.getFreeHeap());
}

static const char about_html[] PROGMEM = "<html>\
  <head>\
    <meta name='viewport' content='width=device-width, initial-scale=1'>\
    <title>About</title>\
  </head>\
<body>\
<h1>About</h1>\
<h4>&nbsp;&nbsp;&nbsp;&nbsp;This version of SoftRF by Moshe Braner</h4>\
<h4>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;http://github.com/moshe-braner/SoftRF</h4>\
<h4>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;mo\
she.bra\
ner@\
gm\
ail.\
com</h4>\
<h4>&nbsp;&nbsp;&nbsp;&nbsp;Based on the SoftRF project by Linar Yusupov</h4>\
<h4>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;http://github.com/lyusupov/SoftRF</h4>\
<br>\
<h2>Credits</h2>\
<p>(in historical order)</p>\
<table width=100%%>\
<tr><th align=left>Ivan Grokhotkov</th><td align=left>Arduino core for ESP8266</td></tr>\
<tr><th align=left>Zak Kemble</th><td align=left>nRF905 library</td></tr>\
<tr><th align=left>Stanislaw Pusep</th><td align=left>flarm_decode</td></tr>\
<tr><th align=left>Paul Stoffregen</th><td align=left>Arduino Time Library</td></tr>\
<tr><th align=left>Mikal Hart</th><td align=left>TinyGPS++ and PString Libraries</td></tr>\
<tr><th align=left>Phil Burgess</th><td align=left>Adafruit NeoPixel Library</td></tr>\
<tr><th align=left>Andy Little</th><td align=left>Aircraft and MAVLink Libraries</td></tr>\
<tr><th align=left>Peter Knight</th><td align=left>TrueRandom Library</td></tr>\
<tr><th align=left>Matthijs Kooijman</th><td align=left>IBM LMIC and Semtech Basic MAC frameworks for Arduino</td></tr>\
<tr><th align=left>David Paiva</th><td align=left>ESP8266FtpServer</td></tr>\
<tr><th align=left>Lammert Bies</th><td align=left>Lib_crc</td></tr>\
<tr><th align=left>Pawel Jalocha</th><td align=left>OGN library</td></tr>\
<tr><th align=left>Timur Sinitsyn, Tobias Simon, Ferry Huberts</th><td align=left>NMEA library</td></tr>\
<tr><th align=left>yangbinbin (yangbinbin_ytu@163.com)</th><td align=left>ADS-B encoder C++ library</td></tr>\
<tr><th align=left>Hristo Gochkov</th><td align=left>Arduino core for ESP32</td></tr>\
<tr><th align=left>Evandro Copercini</th><td align=left>ESP32 BT SPP library</td></tr>\
<tr><th align=left>Limor Fried and Ladyada</th><td align=left>Adafruit BMP085 library</td></tr>\
<tr><th align=left>Kevin Townsend</th><td align=left>Adafruit BMP280 library</td></tr>\
<tr><th align=left>Limor Fried and Kevin Townsend</th><td align=left>Adafruit MPL3115A2 library</td></tr>\
<tr><th align=left>Oliver Kraus</th><td align=left>U8g2 LCD, OLED and eInk library</td></tr>\
<tr><th align=left>Michael Miller</th><td align=left>NeoPixelBus library</td></tr>\
<tr><th align=left>Shenzhen Xin Yuan (LilyGO) ET company</th><td align=left>TTGO T-Beam and T-Watch</td></tr>\
<tr><th align=left>JS Foundation</th><td align=left>jQuery library</td></tr>\
<tr><th align=left>XCSoar team</th><td align=left>EGM96 data</td></tr>\
<tr><th align=left>Mike McCauley</th><td align=left>BCM2835 C library</td></tr>\
<tr><th align=left>Dario Longobardi</th><td align=left>SimpleNetwork library</td></tr>\
<tr><th align=left>Benoit Blanchon</th><td align=left>ArduinoJson library</td></tr>\
<tr><th align=left>flashrom.org project</th><td align=left>Flashrom library</td></tr>\
<tr><th align=left>Robert Wessels and Tony Cave</th><td align=left>EasyLink library</td></tr>\
<tr><th align=left>Oliver Jowett</th><td align=left>Dump978 library</td></tr>\
<tr><th align=left>Phil Karn</th><td align=left>FEC library</td></tr>\
<tr><th align=left>Lewis He</th><td align=left>AXP20X library</td></tr>\
<tr><th align=left>Bodmer</th><td align=left>TFT library</td></tr>\
<tr><th align=left>Michael Kuyper</th><td align=left>Basic MAC library</td></tr>\
<tr><th align=left>Tim Eckel and Horst Reiterer</th><td align=left>ToneAC library</td></tr>\
<tr><th align=left>Moshe Braner</th><td align=left>Collision algorithm for circling aircraft</td></tr>\
</table>\
<hr>\
Copyright (C) 2015-2021 &nbsp;&nbsp;&nbsp; Linar Yusupov\
</body>\
</html>";

#if defined(ESP32)

static const char upload_html[] PROGMEM =
"<html>\
 <head>\
 <meta http-equiv='Content-Type' content='text/html; charset=utf-8'>\
 </head>\
 <p>Select and upload waves.tar (read instructions first)</p>\
 <form method='POST' action='/dowavupld' enctype='multipart/form-data'>\
 <input type='file' name='name'><input type='submit' value='Upload' title='Upload'>\
 </form>\
 </html>";

static File UploadFile;
static const char *textplain = "text/plain";

void wavUpload()
{
  HTTPUpload& uploading = server.upload();

  if(uploading.status == UPLOAD_FILE_START)
  {
    Serial.println(F("Replacing waves.tar in SPIFFS..."));
    clear_waves();
    //SPIFFS.remove("/waves.tar");
    UploadFile = SPIFFS.open("/waves.tar", "w");
       // ignore the source file name, always save it in SPIFFS as waves.tar
    if(! UploadFile) {
      Serial.println(F("Failed to create waves.tar in SPIFFS..."));
      return;
    }
  }
  else if (uploading.status == UPLOAD_FILE_WRITE)
  {
    if(UploadFile) {
      size_t loaded = UploadFile.write(uploading.buf, uploading.currentSize);
      // Serial.print(F("... bytes: ")); Serial.println(loaded);
    }
  } 
  else if (uploading.status == UPLOAD_FILE_END)
  {
    if(UploadFile) {
      UploadFile.close();
      Serial.print(F("Upload Size: ")); Serial.println(uploading.totalSize);
      parse_wav_tar();
      if (uploading.totalSize > 0) {
        server.sendHeader("Location","/");      // Redirect the client to the status page
        server.send(303);
      } else {
        server.send(500, textplain, "500: uploaded zero bytes");
      }
    } else {
      server.send(500, textplain, "500: couldn't create file");
    }
  }
  yield();
}

void alarmlogfile(){
    if (AlarmLogOpen) {
      AlarmLog.close();
      AlarmLogOpen = false;
    }
    if (! SPIFFS.exists("/alarmlog.txt")) {
        server.send(404, textplain, "Alarm log file does not exist");
        return;
    }
    AlarmLog = SPIFFS.open("/alarmlog.txt", "r");
    if (AlarmLog) {
      server.sendHeader("Content-Type", "text/text");
      server.sendHeader("Content-Disposition", "attachment; filename=alarmlog.txt");
      server.sendHeader("Connection", "close");
      server.streamFile(AlarmLog, "application/octet-stream");
      AlarmLog.close();
    }
}

#endif   // ESP32

void handleSettings() {

  Serial.println(F("handleSettings()..."));

  stop_bluetooth();

  bool is_prime_mk2 = false;
  if (hw_info.model == SOFTRF_MODEL_PRIME_MK2 /* && hw_info.revision >= 5 */)
    is_prime_mk2 = true;

  size_t size = 12900;
  char *offset;
  size_t len = 0;
  char *Settings_temp = (char *) malloc(size);

  if (Settings_temp == NULL) {
    Serial.println(F(">>> not enough RAM"));
    return;
  }

  Serial.println(F("Constructing settings page..."));

  offset = Settings_temp;

  /* Common part 1 */
  snprintf_P ( offset, size,
    PSTR("<html>\
<head>\
<meta name='viewport' content='width=device-width, initial-scale=1'>\
<title>Settings</title>\
</head>\
<body>\
<h1 align=center>SoftRF Settings</h1>\
<h4 align=center>%s</h4>\
<h4 align=center>If changed, click 'Save and restart' at the bottom</h4>\
<form action='/input' method='GET'>\
<table width=100%%>\
<tr>\
<th align=left>Mode</th>\
<td align=right>\
<select name='mode'>\
<option %s value='%d'>Normal</option>\
<!-- <option %s value='%d'>Tx/Rx Test</option> -->\
<option %s value='%d'>Bridge</option>\
<option %s value='%d'>UAV</option>\
</select>\
</td>\
</tr>"),
  (default_settings_used? "Warning: reverted to default settings" : "Restored user settings on boot"),
  (settings->mode == SOFTRF_MODE_NORMAL ? "selected" : "") , SOFTRF_MODE_NORMAL,
  (settings->mode == SOFTRF_MODE_TXRX_TEST ? "selected" : ""), SOFTRF_MODE_TXRX_TEST,
  (settings->mode == SOFTRF_MODE_BRIDGE ? "selected" : ""), SOFTRF_MODE_BRIDGE,
  (settings->mode == SOFTRF_MODE_UAV ? "selected" : ""), SOFTRF_MODE_UAV
/*  (settings->mode == SOFTRF_MODE_WATCHOUT ? "selected" : ""), SOFTRF_MODE_WATCHOUT, */
  );

  len = strlen(offset);
  offset += len;
  size -= len;

    snprintf_P ( offset, size,
      PSTR("\
<tr>\
<th align=left>Device ID</th>\
<td align=right>%06x\
</td>\
</tr>"),SoC->getChipId() & 0x00FFFFFF);
    
  len = strlen(offset);
  offset += len;
  size -= len;    

    snprintf_P ( offset, size,
      PSTR("\
<tr>\
<th align=left>ICAO ID (6 HEX digits)</th>\
<td align=right>\
<INPUT type='text' name='aircraft_id' maxlength='6' size='6' value='%06X'>\
</td>\
</tr>"),
  settings->aircraft_id);

  len = strlen(offset);
  offset += len;
  size -= len;

    snprintf_P ( offset, size,
      PSTR("\
<tr>\
<th align=left>ID type to use:</th>\
<td align=right>\
<select name='id_method'>\
<option %s value='%d'>Random</option>\
<option %s value='%d'>ICAO</option>\
<option %s value='%d'>Device</option>\
<option %s value='%d'>Anonymous</option>\
</select>\
</td>\
</tr>"),
    (settings->id_method == ADDR_TYPE_RANDOM ? "selected" : ""),    ADDR_TYPE_RANDOM,
    (settings->id_method == ADDR_TYPE_ICAO ? "selected" : ""),      ADDR_TYPE_ICAO,
    (settings->id_method == ADDR_TYPE_FLARM ? "selected" : ""),     ADDR_TYPE_FLARM,
    (settings->id_method == ADDR_TYPE_ANONYMOUS ? "selected" : ""), ADDR_TYPE_ANONYMOUS
    );
  
  len = strlen(offset);
  offset += len;
  size -= len;

    snprintf_P ( offset, size,
      PSTR("\
<tr>\
<th align=left>Aircraft ID to ignore</th>\
<td align=right>\
<INPUT type='text' name='ignore_id' maxlength='6' size='6' value='%06X'>\
</td>\
</tr>"),
  settings->ignore_id);

  len = strlen(offset);
  offset += len;
  size -= len;

    snprintf_P ( offset, size,
      PSTR("\
<tr>\
<th align=left>Aircraft ID to follow</th>\
<td align=right>\
<INPUT type='text' name='follow_id' maxlength='6' size='6' value='%06X'>\
</td>\
</tr>"),
  settings->follow_id);

  len = strlen(offset);
  offset += len;
  size -= len;

  yield();

  /* Radio specific part 1 */
  if (hw_info.rf == RF_IC_SX1276 || hw_info.rf == RF_IC_SX1262) {
    snprintf_P ( offset, size,
      PSTR("\
<tr>\
<th align=left>Protocol</th>\
<td align=right>\
<select name='protocol'>\
<option %s value='%d'>%s</option>\
<option %s value='%d'>%s</option>\
<option %s value='%d'>%s</option>\
<option %s value='%d'>%s</option>\
<option %s value='%d'>%s</option>\
</select>\
</td>\
</tr>"),
    (settings->rf_protocol == RF_PROTOCOL_LEGACY ? "selected" : ""),
     RF_PROTOCOL_LEGACY, legacy_proto_desc.name,
    (settings->rf_protocol == RF_PROTOCOL_LATEST ? "selected" : ""),
     RF_PROTOCOL_LATEST, "Latest",
    (settings->rf_protocol == RF_PROTOCOL_OGNTP ? "selected" : ""),
     RF_PROTOCOL_OGNTP, ogntp_proto_desc.name,
    (settings->rf_protocol == RF_PROTOCOL_P3I ? "selected" : ""),
     RF_PROTOCOL_P3I, p3i_proto_desc.name,
    (settings->rf_protocol == RF_PROTOCOL_FANET ? "selected" : ""),
     RF_PROTOCOL_FANET, fanet_proto_desc.name
    );
  } else {
    snprintf_P ( offset, size,
      PSTR("\
<tr>\
<th align=left>Protocol</th>\
<td align=right>%s\
</td>\
</tr>"),
    (settings->rf_protocol == RF_PROTOCOL_LEGACY   ? legacy_proto_desc.name :
    (settings->rf_protocol == RF_PROTOCOL_LATEST   ? "Latest" :
    (
#if !defined(EXCLUDE_UAT978)
     settings->rf_protocol == RF_PROTOCOL_ADSB_UAT ? uat978_proto_desc.name :
#endif
    (settings->rf_protocol == RF_PROTOCOL_FANET    ? fanet_proto_desc.name  :
     "UNK"))))
    );
  }
  len = strlen(offset);
  offset += len;
  size -= len;

  /* Common part 2 */
  snprintf_P ( offset, size,
    PSTR("\
<tr>\
<th align=left>Region</th>\
<td align=right>\
<select name='band'>\
<!--<option %s value='%d'>AUTO</option>-->\
<option %s value='%d'>EU (868.2 MHz)</option>\
<option %s value='%d'>RU (868.8 MHz)</option>\
<option %s value='%d'>CN (470 MHz)</option>\
<option %s value='%d'>US/CA (915 MHz)</option>\
<option %s value='%d'>NZ (869.25 MHz)</option>\
<option %s value='%d'>AU (921 MHz)</option>\
<option %s value='%d'>IN (866 MHz)</option>\
<option %s value='%d'>KR (920.9 MHz)</option>\
<option %s value='%d'>IL (916.2 MHz)</option>\
<option %s value='%d'>UK P3I (869.52)</option>\
</select>\
</td>\
</tr>\
<tr>\
<th align=left>Aircraft type</th>\
<td align=right>\
<select name='acft_type'>\
<option %s value='%d'>Glider</option>\
<option %s value='%d'>Towplane</option>\
<option %s value='%d'>Powered</option>\
<option %s value='%d'>Helicopter</option>\
<option %s value='%d'>UAV</option>\
<option %s value='%d'>Hangglider</option>\
<option %s value='%d'>Paraglider</option>\
<option %s value='%d'>Balloon</option>\
<option %s value='%d'>Static</option>\
<option %s value='%d'>Winch</option>\
</select>\
</td>\
</tr>\
<tr>\
<th align=left>Alarm trigger</th>\
<td align=right>\
<select name='alarm'>\
<option %s value='%d'>None</option>\
<option %s value='%d'>Distance</option>\
<option %s value='%d'>Vector</option>\
<option %s value='%d'>Legacy</option>\
</select>\
</td>\
</tr>\
<tr>\
<th align=left>Tx Power</th>\
<td align=right>\
<select name='txpower'>\
<option %s value='%d'>Full</option>\
<option %s value='%d'>Low</option>\
<option %s value='%d'>Off</option>\
</select>\
</td>\
</tr>\
<tr>\
<th align=left>Buzzer</th>\
<td align=right>\
<select name='volume'>\
<option %s value='%d'>Off</option>\
<option %s value='%d'>Soft</option>\
<option %s value='%d'>Loud</option>\
<option %s value='%d'>External</option>\
</select>\
</td>\
</tr>\
<tr>\
<th align=left>Strobe</th>\
<td align=right>\
<select name='strobe'>\
<option %s value='%d'>Off</option>\
<option %s value='%d'>Alarm</option>\
<option %s value='%d'>Airborne</option>\
<option %s value='%d'>Always</option>\
</select>\
</td>\
</tr>\
<tr>\
<th align=left>LED ring direction</th>\
<td align=right>\
<select name='pointer'>\
<option %s value='%d'>CoG Up</option>\
<option %s value='%d'>North Up</option>\
<option %s value='%d'>Off</option>\
</select>\
</td>\
</tr>"),
  (settings->band == RF_BAND_AUTO ? "selected" : ""), RF_BAND_AUTO,
  (settings->band == RF_BAND_EU ? "selected" : ""), RF_BAND_EU,
  (settings->band == RF_BAND_RU ? "selected" : ""), RF_BAND_RU,
  (settings->band == RF_BAND_CN ? "selected" : ""), RF_BAND_CN,
  (settings->band == RF_BAND_US ? "selected" : ""), RF_BAND_US,
  (settings->band == RF_BAND_NZ ? "selected" : ""), RF_BAND_NZ,
  (settings->band == RF_BAND_AU ? "selected" : ""), RF_BAND_AU,
  (settings->band == RF_BAND_IN ? "selected" : ""), RF_BAND_IN,
  (settings->band == RF_BAND_KR ? "selected" : ""), RF_BAND_KR,
  (settings->band == RF_BAND_IL ? "selected" : ""), RF_BAND_IL,
  (settings->band == RF_BAND_UK ? "selected" : ""), RF_BAND_UK,
  (settings->aircraft_type == AIRCRAFT_TYPE_GLIDER ? "selected" : ""),  AIRCRAFT_TYPE_GLIDER,
  (settings->aircraft_type == AIRCRAFT_TYPE_TOWPLANE ? "selected" : ""),  AIRCRAFT_TYPE_TOWPLANE,
  (settings->aircraft_type == AIRCRAFT_TYPE_POWERED ? "selected" : ""),  AIRCRAFT_TYPE_POWERED,
  (settings->aircraft_type == AIRCRAFT_TYPE_HELICOPTER ? "selected" : ""),  AIRCRAFT_TYPE_HELICOPTER,
  (settings->aircraft_type == AIRCRAFT_TYPE_UAV ? "selected" : ""),  AIRCRAFT_TYPE_UAV,
  (settings->aircraft_type == AIRCRAFT_TYPE_HANGGLIDER ? "selected" : ""),  AIRCRAFT_TYPE_HANGGLIDER,
  (settings->aircraft_type == AIRCRAFT_TYPE_PARAGLIDER ? "selected" : ""),  AIRCRAFT_TYPE_PARAGLIDER,
  (settings->aircraft_type == AIRCRAFT_TYPE_BALLOON ? "selected" : ""),  AIRCRAFT_TYPE_BALLOON,
  (settings->aircraft_type == AIRCRAFT_TYPE_STATIC ? "selected" : ""),  AIRCRAFT_TYPE_STATIC,
  (settings->aircraft_type == AIRCRAFT_TYPE_WINCH ? "selected" : ""),  AIRCRAFT_TYPE_WINCH,
  (settings->alarm == TRAFFIC_ALARM_NONE ? "selected" : ""),  TRAFFIC_ALARM_NONE,
  (settings->alarm == TRAFFIC_ALARM_DISTANCE ? "selected" : ""),  TRAFFIC_ALARM_DISTANCE,
  (settings->alarm == TRAFFIC_ALARM_VECTOR ? "selected" : ""),  TRAFFIC_ALARM_VECTOR,
  (settings->alarm == TRAFFIC_ALARM_LEGACY ? "selected" : ""),  TRAFFIC_ALARM_LEGACY,
  (settings->txpower == RF_TX_POWER_FULL ? "selected" : ""),  RF_TX_POWER_FULL,
  (settings->txpower == RF_TX_POWER_LOW ? "selected" : ""),  RF_TX_POWER_LOW,
  (settings->txpower == RF_TX_POWER_OFF ? "selected" : ""),  RF_TX_POWER_OFF,
  (settings->volume == BUZZER_OFF ? "selected" : ""), BUZZER_OFF,
  (settings->volume == BUZZER_VOLUME_LOW ? "selected" : ""), BUZZER_VOLUME_LOW,
  (settings->volume == BUZZER_VOLUME_FULL ? "selected" : ""), BUZZER_VOLUME_FULL,
  (settings->volume == BUZZER_EXT ? "selected" : ""), BUZZER_EXT,
  (settings->strobe == STROBE_OFF ? "selected" : ""), STROBE_OFF,
  (settings->strobe == STROBE_ALARM ? "selected" : ""), STROBE_ALARM,
  (settings->strobe == STROBE_AIRBORNE ? "selected" : ""), STROBE_AIRBORNE,
  (settings->strobe == STROBE_ALWAYS ? "selected" : ""), STROBE_ALWAYS,
  (settings->pointer == DIRECTION_TRACK_UP ? "selected" : ""), DIRECTION_TRACK_UP,
  (settings->pointer == DIRECTION_NORTH_UP ? "selected" : ""), DIRECTION_NORTH_UP,
  (settings->pointer == LED_OFF ? "selected" : ""), LED_OFF
  );

#if 0
<tr>\
<th align=left>Demo Buzzer & Strobe</th>\
<td align=right>\
<input type='radio' name='alarm_demo' value='0' %s>Off\
<input type='radio' name='alarm_demo' value='1' %s>On\
</td>\
</tr>\
//  (!settings->alarm_demo ? "checked" : "") , (settings->alarm_demo ? "checked" : ""),
#endif

  len = strlen(offset);
  offset += len;
  size -= len;

  yield();

  /* SoC specific part 1 */
  if (SoC->id == SOC_ESP32) {
    snprintf_P ( offset, size,
      PSTR("\
<tr>\
<th align=left>Voice Warnings</th>\
<td align=right>\
<select name='voice'>\
<option %s value='%d'>Off</option>\
<option %s value='%d'>Internal DAC</option>\
<option %s value='%d'>External I2S</option>\
</select>\
</td>\
</tr>\
<tr>\
<th align=left>Built-in Bluetooth</th>\
<td align=right>\
<select name='bluetooth'>\
<option %s value='%d'>Off</option>\
<option %s value='%d'>SPP</option>\
<option %s value='%d'>LE</option>\
</select>\
</td>\
</tr>\
<tr>\
<th align=left>TCP mode (if used)</th>\
<td align=right>\
<select name='tcpmode'>\
<option %s value='%d'>Server</option>\
<option %s value='%d'>Client</option>\
</select>\
</td>\
</tr>\
<tr>\
<th align=left>&nbsp;&nbsp;&nbsp;&nbsp;Host IP (if TCP client):</th>\
<td align=right>\
<INPUT type='text' name='host_ip' maxlength='15' value='%s' size='16' ></td>\
</tr>\
<tr>\
<th align=left>&nbsp;&nbsp;&nbsp;&nbsp;Host port (if TCP client):</th>\
<td align=right>\
<select name='tcpport'>\
<option %s value='%d'>2000</option>\
<option %s value='%d'>8880</option>\
</select>\
</td>\
</tr>"),
    (settings->voice == VOICE_OFF ? "selected" : ""), VOICE_OFF,
    (settings->voice == VOICE_INT ? "selected" : ""), VOICE_INT,
    (settings->voice == VOICE_EXT ? "selected" : ""), VOICE_EXT,
    (settings->bluetooth == BLUETOOTH_OFF ? "selected" : ""), BLUETOOTH_OFF,
    (settings->bluetooth == BLUETOOTH_SPP ? "selected" : ""), BLUETOOTH_SPP,
    (settings->bluetooth == BLUETOOTH_LE_HM10_SERIAL ? "selected" : ""), BLUETOOTH_LE_HM10_SERIAL,
    (settings->tcpmode == TCP_MODE_SERVER ? "selected" : ""), TCP_MODE_SERVER,
    (settings->tcpmode == TCP_MODE_CLIENT ? "selected" : ""), TCP_MODE_CLIENT,
     settings->host_ip,
    (settings->tcpport == 0 ? "selected" : ""), 0,
    (settings->tcpport == 1 ? "selected" : ""), 1
    );

    len = strlen(offset);
    offset += len;
    size -= len;
  }

  yield();

  /* Common part 3 */
  snprintf_P ( offset, size,
    PSTR("\
<tr>\
<th align=left>External WiFi (optional):</th>\
</tr>\
<tr>\
<th align=left>&nbsp;&nbsp;&nbsp;&nbsp;SSID:</th>\
<td align=right>\
<INPUT type='text' name='ssid' maxlength='18' value='%s' size='16' ></td>\
</tr>\
<tr>\
<th align=left>&nbsp;&nbsp;&nbsp;&nbsp;PSK:</th>\
<td align=right>\
<INPUT type='password' name='psk' maxlength='16' value='%s' size='16' ></td>\
</tr>\
<tr>\
<th align=left>NMEA primary output</th>\
<td align=right>\
<select name='nmea_out'>\
<option %s value='%d'>Off</option>\
<option %s value='%d'>UDP</option>\
<option %s value='%d'>Serial</option>"),
  settings->ssid, "hidepass",
  (settings->nmea_out == DEST_NONE  ? "selected" : ""), DEST_NONE,
  (settings->nmea_out == DEST_UDP   ? "selected" : ""), DEST_UDP,
  (settings->nmea_out == DEST_UART  ? "selected" : ""), DEST_UART);

  len = strlen(offset);
  offset += len;
  size -= len;

  /* SoC specific part 2 */
  if (SoC->id == SOC_ESP32) {
    if (is_prime_mk2) {
     snprintf_P ( offset, size,
       PSTR("\
<option %s value='%d'>Serial 2</option>\
<option %s value='%d'>Bluetooth</option>\
<option %s value='%d'>TCP</option>"),
     (settings->nmea_out == DEST_UART2 ? "selected" : ""), DEST_UART2,
     (settings->nmea_out == DEST_BLUETOOTH ? "selected" : ""), DEST_BLUETOOTH,
     (settings->nmea_out == DEST_TCP       ? "selected" : ""), DEST_TCP);
    } else {
     snprintf_P ( offset, size,
       PSTR("\
<option %s value='%d'>Bluetooth</option>\
<option %s value='%d'>TCP</option>"),
     (settings->nmea_out == DEST_BLUETOOTH ? "selected" : ""), DEST_BLUETOOTH,
     (settings->nmea_out == DEST_TCP       ? "selected" : ""), DEST_TCP);
    }
    len = strlen(offset);
    offset += len;
    size -= len;
  }

    snprintf_P ( offset, size,
      PSTR("\
<tr>\
<th align=left>&nbsp;&nbsp;&nbsp;&nbsp;Sentences:</th>\
</tr>\
<tr>\
<th align=left>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;GNSS</th>\
<td align=right>\
<input type='radio' name='nmea_g' value='0' %s>Off\
<input type='radio' name='nmea_g' value='1' %s>On\
</td>\
</tr>\
<tr>\
<th align=left>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Private</th>\
<td align=right>\
<input type='radio' name='nmea_p' value='0' %s>Off\
<input type='radio' name='nmea_p' value='1' %s>On\
</td>\
</tr>\
<tr>\
<th align=left>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Legacy</th>\
<td align=right>\
<input type='radio' name='nmea_l' value='0' %s>Off\
<input type='radio' name='nmea_l' value='1' %s>On\
</td>\
</tr>\
<tr>\
<th align=left>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Sensors</th>\
<td align=right>\
<input type='radio' name='nmea_s' value='0' %s>Off\
<input type='radio' name='nmea_s' value='1' %s>On\
</td>\
</tr>\
<tr>\
<th align=left>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Debug</th>\
<td align=right>\
<input type='radio' name='nmea_d' value='0' %s>Off\
<input type='radio' name='nmea_d' value='1' %s>On\
</td>\
</tr>\
<tr>\
<th align=left>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;External</th>\
<td align=right>\
<input type='radio' name='nmea_e' value='0' %s>Off\
<input type='radio' name='nmea_e' value='1' %s>On\
</td>\
</tr>"),
  (!settings->nmea_g ? "checked" : "") , (settings->nmea_g ? "checked" : ""),
  (!settings->nmea_p ? "checked" : "") , (settings->nmea_p ? "checked" : ""),
  (!settings->nmea_l ? "checked" : "") , (settings->nmea_l ? "checked" : ""),
  (!settings->nmea_s ? "checked" : "") , (settings->nmea_s ? "checked" : ""),
  (!settings->nmea_d ? "checked" : "") , (settings->nmea_d ? "checked" : ""),
  (!settings->nmea_e ? "checked" : "") , (settings->nmea_e ? "checked" : ""));

    len = strlen(offset);
    offset += len;
    size -= len;

  /* second NMEA output route */
  snprintf_P ( offset, size,
    PSTR("\
<tr>\
<th align=left>NMEA second output</th>\
<td align=right>\
<select name='nmea_out2'>\
<option %s value='%d'>Off</option>\
<option %s value='%d'>UDP</option>\
<option %s value='%d'>Serial</option>"),
  (settings->nmea_out2 == DEST_NONE  ? "selected" : ""), DEST_NONE,
  (settings->nmea_out2 == DEST_UDP   ? "selected" : ""), DEST_UDP,
  (settings->nmea_out2 == DEST_UART  ? "selected" : ""), DEST_UART);

  len = strlen(offset);
  offset += len;
  size -= len;


  if (SoC->id == SOC_ESP32) {
    if (is_prime_mk2) {
     snprintf_P ( offset, size,
       PSTR("\
<option %s value='%d'>Serial 2</option>\
<option %s value='%d'>Bluetooth</option>\
<option %s value='%d'>TCP</option>"),
     (settings->nmea_out2 == DEST_UART2 ? "selected" : ""), DEST_UART2,
     (settings->nmea_out2 == DEST_BLUETOOTH ? "selected" : ""), DEST_BLUETOOTH,
     (settings->nmea_out2 == DEST_TCP       ? "selected" : ""), DEST_TCP);
    } else {
     snprintf_P ( offset, size,
       PSTR("\
<option %s value='%d'>Bluetooth</option>\
<option %s value='%d'>TCP</option>"),
     (settings->nmea_out2 == DEST_BLUETOOTH ? "selected" : ""), DEST_BLUETOOTH,
     (settings->nmea_out2 == DEST_TCP       ? "selected" : ""), DEST_TCP);
    }
    len = strlen(offset);
    offset += len;
    size -= len;
  }

// Serial.println(F("Settings page part 4 done"));

    snprintf_P ( offset, size,
      PSTR("\
<tr>\
<th align=left>&nbsp;&nbsp;&nbsp;&nbsp;Sentences:</th>\
</tr>\
<tr>\
<th align=left>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;GNSS</th>\
<td align=right>\
<input type='radio' name='nmea2_g' value='0' %s>Off\
<input type='radio' name='nmea2_g' value='1' %s>On\
</td>\
</tr>\
<tr>\
<th align=left>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Private</th>\
<td align=right>\
<input type='radio' name='nmea2_p' value='0' %s>Off\
<input type='radio' name='nmea2_p' value='1' %s>On\
</td>\
</tr>\
<tr>\
<th align=left>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Legacy</th>\
<td align=right>\
<input type='radio' name='nmea2_l' value='0' %s>Off\
<input type='radio' name='nmea2_l' value='1' %s>On\
</td>\
</tr>\
<tr>\
<th align=left>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Sensors</th>\
<td align=right>\
<input type='radio' name='nmea2_s' value='0' %s>Off\
<input type='radio' name='nmea2_s' value='1' %s>On\
</td>\
</tr>\
<tr>\
<th align=left>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Debug</th>\
<td align=right>\
<input type='radio' name='nmea2_d' value='0' %s>Off\
<input type='radio' name='nmea2_d' value='1' %s>On\
</td>\
</tr>\
<tr>\
<th align=left>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;External</th>\
<td align=right>\
<input type='radio' name='nmea2_e' value='0' %s>Off\
<input type='radio' name='nmea2_e' value='1' %s>On\
</td>\
</tr>\
<tr>\
<th align=left>Serial Output Baud Rate:</th>\
<td align=right>\
<select name='baud_rate'>\
<option %s value='%d'>Default</option>\
<option %s value='%d'>4800</option>\
<option %s value='%d'>9600</option>\
<option %s value='%d'>19200</option>\
<option %s value='%d'>38400</option>\
<option %s value='%d'>57600</option>\
<option %s value='%d'>115200</option>\
</select>\
</td>\
</tr>"),
  (!settings->nmea2_g ? "checked" : "") , (settings->nmea2_g ? "checked" : ""),
  (!settings->nmea2_p ? "checked" : "") , (settings->nmea2_p ? "checked" : ""),
  (!settings->nmea2_l ? "checked" : "") , (settings->nmea2_l ? "checked" : ""),
  (!settings->nmea2_s ? "checked" : "") , (settings->nmea2_s ? "checked" : ""),
  (!settings->nmea2_d ? "checked" : "") , (settings->nmea2_d ? "checked" : ""),
  (!settings->nmea2_e ? "checked" : "") , (settings->nmea2_e ? "checked" : ""),
  (settings->baud_rate == BAUD_DEFAULT ? "selected" : ""), BAUD_DEFAULT,
  (settings->baud_rate == BAUD_4800    ? "selected" : ""), BAUD_4800,
  (settings->baud_rate == BAUD_9600    ? "selected" : ""), BAUD_9600,
  (settings->baud_rate == BAUD_19200   ? "selected" : ""), BAUD_19200,
  (settings->baud_rate == BAUD_38400   ? "selected" : ""), BAUD_38400,
  (settings->baud_rate == BAUD_57600   ? "selected" : ""), BAUD_57600,
  (settings->baud_rate == BAUD_115200  ? "selected" : ""), BAUD_115200);

    len = strlen(offset);
    offset += len;
    size -= len;

  if (SoC->id == SOC_ESP32) {
    if (is_prime_mk2) {
  snprintf_P ( offset, size,
    PSTR("\
<tr>\
<th align=left>Serial Port RX pin:</th>\
<td align=right>\
<select name='altpin0'>\
<option %s value='%d'>\"RX\"</option>\
<option %s value='%d'>\"VP\"</option>\
</select>\
</td>\
</tr>\
<tr>\
<th align=left>Aux Serial Port Baud Rate:</th>\
<td align=right>\
<select name='baudrate2'>\
<option %s value='%d'>Disabled</option>\
<option %s value='%d'>4800</option>\
<option %s value='%d'>9600</option>\
<option %s value='%d'>19200</option>\
<option %s value='%d'>38400</option>\
<option %s value='%d'>57600</option>\
<option %s value='%d'>115200</option>\
</select>\
</td>\
</tr>\
<tr>\
<th align=left>Aux Serial Port Logic:</th>\
<td align=right>\
<select name='invert2'>\
<option %s value='%d'>Normal</option>\
<option %s value='%d'>Inverted</option>\
</select>\
</td>\
</tr>\
<tr>\
<th align=left>UDP Port for NMEA output:</th>\
<td align=right>\
<select name='alt_udp'>\
<option %s value='%d'>10110</option>\
<option %s value='%d'>10111</option>\
</select>\
</td>\
</tr>"),
    (!settings->altpin0 ? "selected" : ""), 0,
    ( settings->altpin0 ? "selected" : ""), 1,
    (settings->baudrate2 == BAUD_DEFAULT ? "selected" : ""), BAUD_DEFAULT,
    (settings->baudrate2 == BAUD_4800    ? "selected" : ""), BAUD_4800,
    (settings->baudrate2 == BAUD_9600    ? "selected" : ""), BAUD_9600,
    (settings->baudrate2 == BAUD_19200   ? "selected" : ""), BAUD_19200,
    (settings->baudrate2 == BAUD_38400   ? "selected" : ""), BAUD_38400,
    (settings->baudrate2 == BAUD_57600   ? "selected" : ""), BAUD_57600,
    (settings->baudrate2 == BAUD_115200  ? "selected" : ""), BAUD_115200,
    (!settings->invert2 ? "selected" : ""), 0,
    ( settings->invert2 ? "selected" : ""), 1,
    (!settings->alt_udp ? "selected" : ""), 0,
    ( settings->alt_udp ? "selected" : ""), 1
    );
  
  len = strlen(offset);
  offset += len;
  size -= len;

  }
  }

  yield();

  /* Common part 4 */
  // actually for now only on T-Beam
  if (SoC->id == SOC_ESP32) {
    if (is_prime_mk2) {

  snprintf_P ( offset, size,
    PSTR("\
</select>\
</td>\
</tr>\
<tr>\
<th align=left>Internal ADS-B Receiver</th>\
<td align=right>\
<select name='rx1090'>\
<option %s value='%d'>None</option>\
<option %s value='%d'>GNS5892</option>\
</tr>\
</tr>\
<tr>\
<th align=left>GDL90 Input</th>\
<td align=right>\
<select name='gdl90_in'>\
<option %s value='%d'>Off</option>\
<option %s value='%d'>Serial</option>\
<option %s value='%d'>Serial2</option>\
<option %s value='%d'>UDP</option>\
<option %s value='%d'>Bluetooth</option>\
<option %s value='%d'>TCP</option>"),
  (settings->rx1090 == ADSB_RX_NONE    ? "selected" : ""), ADSB_RX_NONE,
  (settings->rx1090 == ADSB_RX_GNS5892 ? "selected" : ""), ADSB_RX_GNS5892,
  (settings->gdl90_in == DEST_NONE  ? "selected" : ""), DEST_NONE,
  (settings->gdl90_in == DEST_UART  ? "selected" : ""), DEST_UART,
  (settings->gdl90_in == DEST_UART2 ? "selected" : ""), DEST_UART2,
  (settings->gdl90_in == DEST_UDP   ? "selected" : ""), DEST_UDP,
  (settings->gdl90_in == DEST_BLUETOOTH ? "selected" : ""), DEST_BLUETOOTH,
  (settings->gdl90_in == DEST_TCP       ? "selected" : ""), DEST_TCP);

  len = strlen(offset);
  offset += len;
  size -= len;

    }
  }

  /* common */
  snprintf_P ( offset, size,
    PSTR("\
</tr>\
<tr>\
<th align=left>GDL90 output</th>\
<td align=right>\
<select name='gdl90'>\
<option %s value='%d'>Off</option>\
<option %s value='%d'>Serial</option>\
<option %s value='%d'>UDP</option>"),
  (settings->gdl90 == DEST_NONE ? "selected" : ""), DEST_NONE,
  (settings->gdl90 == DEST_UART ? "selected" : ""), DEST_UART,
  (settings->gdl90 == DEST_UDP  ? "selected" : ""), DEST_UDP);

  len = strlen(offset);
  offset += len;
  size -= len;

  /* SoC specific part 5 */
  if (SoC->id == SOC_ESP32) {
    if (is_prime_mk2) {
      snprintf_P ( offset, size,
        PSTR("<option %s value='%d'>Serial 2</option>"),
        (settings->gdl90 == DEST_UART2 ? "selected" : ""), DEST_UART2);
      len = strlen(offset);
      offset += len;
      size -= len;
    }
    snprintf_P ( offset, size,
      PSTR("\
<option %s value='%d'>Bluetooth</option>\
<option %s value='%d'>TCP</option>"),
  (settings->gdl90 == DEST_BLUETOOTH ? "selected" : ""), DEST_BLUETOOTH,
  (settings->gdl90 == DEST_TCP ? "selected" : ""), DEST_TCP);
    len = strlen(offset);
    offset += len;
    size -= len;
  }

#if !defined(EXCLUDE_D1090)
  /* Common part 5 */
  snprintf_P ( offset, size,
    PSTR("\
</select>\
</td>\
</tr>\
<tr>\
<th align=left>Dump1090 output</th>\
<td align=right>\
<select name='d1090'>\
<option %s value='%d'>Off</option>\
<option %s value='%d'>Serial</option>"),
  (settings->d1090 == DEST_NONE ? "selected" : ""), DEST_NONE,
  (settings->d1090 == DEST_UART ? "selected" : ""), DEST_UART,
  (settings->d1090 == DEST_UDP  ? "selected" : ""), DEST_UDP);

  len = strlen(offset);
  offset += len;
  size -= len;

  /* SoC specific part 4 */
  if (SoC->id == SOC_ESP32) {
    if (is_prime_mk2) {
      snprintf_P ( offset, size,
        PSTR("<option %s value='%d'>Serial 2</option>"),
        (settings->d1090 == DEST_UART2 ? "selected" : ""), DEST_UART2);
      len = strlen(offset);
      offset += len;
      size -= len;
    }
    snprintf_P ( offset, size,
      PSTR("\
<option %s value='%d'>Bluetooth</option>\
<option %s value='%d'TCP</option>"),
  (settings->d1090 == DEST_BLUETOOTH ? "selected" : ""), DEST_BLUETOOTH,
  (settings->d1090 == DEST_TCP ? "selected" : ""), DEST_TCP);

    len = strlen(offset);
    offset += len;
    size -= len;
  }
#endif

  /* Common part 6 */
  snprintf_P ( offset, size,
    PSTR("\
</select>\
</td>\
</tr>\
<tr>\
<th align=left>Power save</th>\
<td align=right>\
<select name='power_save'>\
<option %s value='%d'>Disabled</option>\
<option %s value='%d'>WiFi OFF (10 min.)</option>"
//<option %s value='%d'>GNSS</option>
"</select>\
</td>\
</tr>\
<tr>\
<th align=left>Shutdown if no external power</th>\
<td align=right>\
<input type='radio' name='power_external' value='0' %s>No\
<input type='radio' name='power_external' value='1' %s>Yes\
</td>\
</tr>\
<tr>\
<th align=left>Air-Relay</th>\
<td align=right>\
<select name='relay'>\
<option %s value='%d'>None</option>\
<option %s value='%d'>Landed</option>\
<option %s value='%d'>All</option>\
<option %s value='%d'>Only</option>\
</select>\
</td>\
</tr>\
<tr>\
<th align=left>Stealth</th>\
<td align=right>\
<input type='radio' name='stealth' value='0' %s>Off\
<input type='radio' name='stealth' value='1' %s>On\
</td>\
</tr>\
<tr>\
<th align=left>No track</th>\
<td align=right>\
<input type='radio' name='no_track' value='0' %s>Off\
<input type='radio' name='no_track' value='1' %s>On\
</td>\
</tr>"),
  (settings->power_save == POWER_SAVE_NONE ? "selected" : ""), POWER_SAVE_NONE,
  (settings->power_save == POWER_SAVE_WIFI ? "selected" : ""), POWER_SAVE_WIFI,
//(settings->power_save == POWER_SAVE_GNSS ? "selected" : ""), POWER_SAVE_GNSS,
  (!settings->power_external ? "checked" : "") , (settings->power_external ? "checked" : ""),
  (settings->relay==RELAY_OFF    ? "selected" : ""), RELAY_OFF,
  (settings->relay==RELAY_LANDED ? "selected" : ""), RELAY_LANDED,
  (settings->relay==RELAY_ALL    ? "selected" : ""), RELAY_ALL,
  (settings->relay==RELAY_ONLY   ? "selected" : ""), RELAY_ONLY,
  (!settings->stealth  ? "checked" : "") , (settings->stealth  ? "checked" : ""),
  (!settings->no_track ? "checked" : "") , (settings->no_track ? "checked" : "")
  );

  len = strlen(offset);
  offset += len;
  size -= len;

  /* Radio specific part 2 */
  if (rf_chip && rf_chip->type == RF_IC_SX1276) {
    snprintf_P ( offset, size,
      PSTR("\
<tr>\
<th align=left>Radio CF correction (&#177;, kHz)</th>\
<td align=right>\
<INPUT type='number' name='rfc' min='-30' max='30' value='%d'>\
</td>\
</tr>"),
    settings->freq_corr);

    len = strlen(offset);
    offset += len;
    size -= len;
  }

  /* whether T-Beam v0.7 has wire added from PPS to GPIO37 */
  if (hw_info.model == SOFTRF_MODEL_PRIME_MK2 && hw_info.revision < 8) {
    snprintf_P ( offset, size,
      PSTR("\
<tr>\
<th align=left>PPS wire hardware mod:</th>\
<td align=right>\
<input type='radio' name='ppswire' value='0' %s>Absent\
<input type='radio' name='ppswire' value='1' %s>Present\
</td>\
</tr>"),
  (!settings->ppswire ? "checked" : "") , (settings->ppswire ? "checked" : ""));

    len = strlen(offset);
    offset += len;
    size -= len;
  }

  if (SoC->id == SOC_ESP32) {
    snprintf_P ( offset, size,
      PSTR("\
<tr>\
<th align=left>Alarms Log</th>\
<td align=right>\
<select name='alarmlog'>\
<option %s value='%d'>Disabled</option>\
<option %s value='%d'>Enabled</option>"
"</select>\
</td>\
</tr>\
<tr>\
<th align=left>Debug flags (2 HEX digits)</th>\
<td align=right>\
<INPUT type='text' name='debug_flags' maxlength='2' size='2' value='%02X'>\
</td>\
</tr>"),
    (settings->logalarms==false ? "selected" : ""), 0,
    (settings->logalarms==true  ? "selected" : ""), 1,
    settings->debug_flags);

    len = strlen(offset);
    offset += len;
    size -= len;
  }

#if defined(USE_OGN_ENCRYPTION)
  if (settings->rf_protocol == RF_PROTOCOL_OGNTP) {
    snprintf_P ( offset, size,
      PSTR("\
<tr>\
<th align=left>IGC key (HEX)</th>\
<td align=right>\
<INPUT type='text' name='igc_key' maxlength='32' size='32' value='%08X%08X%08X%08X'>\
</td>\
</tr>"),
  // settings->igc_key[0], settings->igc_key[1], settings->igc_key[2], settings->igc_key[3]);
    settings->igc_key[0]? 0x88888888 : 0,
    settings->igc_key[1]? 0x88888888 : 0,
    settings->igc_key[2]? 0x88888888 : 0,
    settings->igc_key[3]? 0x88888888 : 0);
       /* mask the key from prying eyes */

    len = strlen(offset);
    offset += len;
    size -= len;
  }
#endif

  yield();

  /* Common part 7 */
  snprintf_P ( offset, size,
    PSTR("\
</table>\
<p align=center><INPUT type='submit' value='Save and restart'></p>\
</form>\
</body>\
</html>")
  );

  len = strlen(offset);
  offset += len;
  Serial.print(F("Settings page size: ")); Serial.println(offset-Settings_temp);

  SoC->swSer_enableRx(false);
  server.sendHeader(String(F("Cache-Control")), String(F("no-cache, no-store, must-revalidate")));
  server.sendHeader(String(F("Pragma")), String(F("no-cache")));
  server.sendHeader(String(F("Expires")), String(F("-1")));
  server.send ( 200, "text/html", Settings_temp );
  SoC->swSer_enableRx(true);
  Serial.print(F("Free memory (settings page staging): "));
  Serial.println(ESP.getFreeHeap());
  free(Settings_temp);
}

void handleRoot() {

  Serial.println(F("handleRoot()..."));

  stop_bluetooth();

  int sec = millis() / 1000;
  int min = sec / 60;
  int hr = min / 60;

  float vdd = Battery_voltage() ;
  bool low_voltage = (Battery_voltage() <= Battery_threshold());

  time_t timestamp = ThisAircraft.timestamp;
  unsigned int sats = gnss.satellites.value(); // Number of satellites in use (u32)
  char str_lat[16];
  char str_lon[16];
  char str_alt[16];
  char str_Vcc[8];

  char *Root_temp = (char *) malloc(2900);
  if (Root_temp == NULL) {
    Serial.println(F(">>> not enough RAM"));
    return;
  }

  dtostrf(ThisAircraft.latitude,  8, 4, str_lat);
  dtostrf(ThisAircraft.longitude, 8, 4, str_lon);
  dtostrf(ThisAircraft.altitude,  7, 1, str_alt);
  dtostrf(vdd, 4, 2, str_Vcc);

  snprintf_P ( Root_temp, 3000,
    PSTR("<html>\
  <head>\
    <meta name='viewport' content='width=device-width, initial-scale=1'>\
    <title>SoftRF status</title>\
  </head>\
<body>\
 <table width=100%%>\
  <tr><!-- <td align=left><h1>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</h1></td> -->\
  <td align=center><h1>SoftRF status</h1></td>\
  <!-- <td align=right><img src='/logo.png'></td> --></tr>\
 %s\
 %s\
 </table>\
 <table width=100%%>\
  <tr><th align=left>Device Id</th><td align=right>%06X</td></tr>\
  <tr><th align=left>Software Version</th><td align=right>%s&nbsp;&nbsp;%s</td></tr>"
#if !defined(ENABLE_AHRS)
 "</table><table width=100%%>\
  <tr><td align=left><table><tr><th align=left>GNSS&nbsp;&nbsp;</th><td align=right>%s</td></tr></table></td>\
  <td align=center><table><tr><th align=left>Radio&nbsp;&nbsp;</th><td align=right>%s</td></tr></table></td>\
  <td align=right><table><tr><th align=left>Baro&nbsp;&nbsp;</th><td align=right>%s</td></tr></table></td></tr>\
  </table><table width=100%%>"
#else
 "<tr><td align=left><table><tr><th align=left>GNSS&nbsp;&nbsp;</th><td align=right>%s</td></tr></table></td>\
  <td align=right><table><tr><th align=left>Radio&nbsp;&nbsp;</th><td align=right>%s</td></tr></table></td></tr>\
  <tr><td align=left><table><tr><th align=left>Baro&nbsp;&nbsp;</th><td align=right>%s</td></tr></table></td>\
  <td align=right><table><tr><th align=left>AHRS&nbsp;&nbsp;</th><td align=right>%s</td></tr></table></td></tr>"
#endif /* ENABLE_AHRS */
 "<tr><th align=left>Uptime</th><td align=right>%02d:%02d:%02d</td></tr>\
  <tr><th align=left>Free memory</th><td align=right>%u</td></tr>\
  <tr><th align=left>Battery voltage</th><td align=right><font color=%s>%s</font></td></tr>\
 </table>\
 <table width=100%%>\
   <tr><th align=left>Packets</th>\
    <td align=right><table><tr>\
     <th align=left>Tx&nbsp;&nbsp;</th><td align=right>%u</td>\
     <th align=left>&nbsp;&nbsp;&nbsp;&nbsp;Rx&nbsp;&nbsp;</th><td align=right>%u</td>\
   </tr></table></td></tr>\
 </table>\
 <hr>\
 <h3 align=center>Most recent GNSS fix</h3>\
 <table width=100%%>\
  <tr><th align=left>Time</th><td align=right>%u</td></tr>\
  <tr><th align=left>Satellites</th><td align=right>%d</td></tr>\
  <tr><th align=left>Latitude</th><td align=right>%s</td></tr>\
  <tr><th align=left>Longitude</th><td align=right>%s</td></tr>\
  <tr><td align=left><b>Altitude</b>&nbsp;&nbsp;(above MSL)</td><td align=right>%s</td></tr>\
 </table>\
 <hr>\
 <table width=100%%>\
  <tr>\
    <td><input type=button onClick=\"location.href='/settings'\" value='Settings'></td>\
    <td><input type=button onClick=\"location.href='/firmware'\" value='Firmware update'></td>\
    <td><input type=button onClick=\"location.href='/reboot'\" value='Reboot'></td>\
    <td><input type=button onClick=\"location.href='/about'\" value='About'></td>\
  </tr>\
 </table>\
 <hr>\
 <table width=100%%>\
  <tr>\
    <td>%d WAV files found</td>\
    <td><input type=button onClick=\"location.href='/wavupload'\" value='Upload waves.tar'></td>\
    <td><input type=button onClick=\"location.href='/format'\" value='Clear ALL files'></td>\
  </tr>\
 </table>\
 <hr>\
 <table width=100%%>\
  <tr>\
    <td>Alarm Log:</td>\
    <td><input type=button onClick=\"location.href='/alarmlog'\" value='Download'></td>\
    <td><input type=button onClick=\"location.href='/clearlog'\" value='Clear'></td>\
  </tr>\
 </table>\
</body>\
</html>"),
    (default_settings_used ?
       "<tr><td align=center><h4>(Warning: reverted to default settings)</h4></td></tr>" : ""),
    (BTpaused ?
       "<tr><td align=center><h4>(Bluetooth paused, reboot to resume)</h4></td></tr>" : ""),
    ThisAircraft.addr, SOFTRF_FIRMWARE_VERSION,
    (SoC == NULL ? "NONE" : SoC->name),
    GNSS_name[hw_info.gnss],
    (rf_chip   == NULL ? "NONE" : rf_chip->name),
    (baro_chip == NULL ? "NONE" : baro_chip->name),
#if defined(ENABLE_AHRS)
    (ahrs_chip == NULL ? "NONE" : ahrs_chip->name),
#endif /* ENABLE_AHRS */
    hr, min % 60, sec % 60, ESP.getFreeHeap(),
    low_voltage ? "red" : "green", str_Vcc,
    tx_packets_counter, rx_packets_counter,
    timestamp, sats, str_lat, str_lon, str_alt,
    num_wav_files
  );
  SoC->swSer_enableRx(false);
  server.sendHeader(String(F("Cache-Control")), String(F("no-cache, no-store, must-revalidate")));
  server.sendHeader(String(F("Pragma")), String(F("no-cache")));
  server.sendHeader(String(F("Expires")), String(F("-1")));
  server.send ( 200, "text/html", Root_temp );
  SoC->swSer_enableRx(true);
  free(Root_temp);
  if (!SPIFFS.begin(true)) {
      Serial.println(F("Failed to start SPIFFS"));
      return;
  }
  File root = SPIFFS.open("/");
  if (! root) {
      Serial.println(F("Cannot open SPIFFS root"));
      return;
  }
  Serial.println(F("Files in SPIFFS:"));
  File file = root.openNextFile();
  while(file){
      Serial.print("... ");
      Serial.println(file.name());
      Serial.print("  [");
      Serial.print(file.size());
      Serial.println(" bytes]");
      file = root.openNextFile();
  }
  file.close();
  root.close();
  Serial.println(F("... end of files in SPIFFS"));
}

void handleInput() {

  char idbuf[6 + 1];

  for ( uint8_t i = 0; i < server.args(); i++ ) {
    if (server.argName(i).equals("mode")) {
      settings->mode = server.arg(i).toInt();
    } else if (server.argName(i).equals("protocol")) {
      settings->rf_protocol = server.arg(i).toInt();
    } else if (server.argName(i).equals("band")) {
      settings->band = server.arg(i).toInt();
    } else if (server.argName(i).equals("acft_type")) {
      settings->aircraft_type = server.arg(i).toInt();
    } else if (server.argName(i).equals("alarm")) {
      settings->alarm = server.arg(i).toInt();
    } else if (server.argName(i).equals("txpower")) {
      settings->txpower = server.arg(i).toInt();
    } else if (server.argName(i).equals("volume")) {
      settings->volume = server.arg(i).toInt();
    } else if (server.argName(i).equals("strobe")) {
      settings->strobe = server.arg(i).toInt();
//    } else if (server.argName(i).equals("alarm_demo")) {
//      settings->alarm_demo = server.arg(i).toInt();
    } else if (server.argName(i).equals("pointer")) {
      settings->pointer = server.arg(i).toInt();
    } else if (server.argName(i).equals("voice")) {
      settings->voice = server.arg(i).toInt();
    } else if (server.argName(i).equals("bluetooth")) {
      settings->bluetooth = server.arg(i).toInt();
    } else if (server.argName(i).equals("tcpmode")) {
      settings->tcpmode = server.arg(i).toInt();
    } else if (server.argName(i).equals("tcpport")) {
      settings->tcpport = server.arg(i).toInt();
    } else if (server.argName(i).equals("ssid")) {
      strncpy(settings->ssid, server.arg(i).c_str(), sizeof(settings->ssid)-1);
      settings->ssid[sizeof(settings->ssid)-1] = '\0';
    } else if (server.argName(i).equals("psk")) {
        //if (strcmp(server.arg(i).c_str()),"hidepass")
        if (! server.arg(i).equals("hidepass")) {
            strncpy(settings->psk, server.arg(i).c_str(), sizeof(settings->psk)-1);
            settings->psk[sizeof(settings->psk)-1] = '\0';
        }
    } else if (server.argName(i).equals("host_ip")) {
      strncpy(settings->host_ip, server.arg(i).c_str(), sizeof(settings->host_ip)-1);
      settings->host_ip[sizeof(settings->host_ip)-1] = '\0';
    } else if (server.argName(i).equals("nmea_out")) {
      settings->nmea_out = server.arg(i).toInt();
    } else if (server.argName(i).equals("nmea_g")) {
      settings->nmea_g = server.arg(i).toInt();
    } else if (server.argName(i).equals("nmea_p")) {
      settings->nmea_p = server.arg(i).toInt();
    } else if (server.argName(i).equals("nmea_l")) {
      settings->nmea_l = server.arg(i).toInt();
    } else if (server.argName(i).equals("nmea_s")) {
      settings->nmea_s = server.arg(i).toInt();
    } else if (server.argName(i).equals("nmea_d")) {
      settings->nmea_d = server.arg(i).toInt();
    } else if (server.argName(i).equals("nmea_e")) {
      settings->nmea_e = server.arg(i).toInt();
    } else if (server.argName(i).equals("nmea_out2")) {
      settings->nmea_out2 = server.arg(i).toInt();
    } else if (server.argName(i).equals("nmea2_g")) {
      settings->nmea2_g = server.arg(i).toInt();
    } else if (server.argName(i).equals("nmea2_p")) {
      settings->nmea2_p = server.arg(i).toInt();
    } else if (server.argName(i).equals("nmea2_l")) {
      settings->nmea2_l = server.arg(i).toInt();
    } else if (server.argName(i).equals("nmea2_s")) {
      settings->nmea2_s = server.arg(i).toInt();
    } else if (server.argName(i).equals("nmea2_d")) {
      settings->nmea2_d = server.arg(i).toInt();
    } else if (server.argName(i).equals("nmea2_e")) {
      settings->nmea2_e = server.arg(i).toInt();
    } else if (server.argName(i).equals("baud_rate")) {
      settings->baud_rate = server.arg(i).toInt();
    } else if (server.argName(i).equals("altpin0")) {
      settings->altpin0 = server.arg(i).toInt();
    } else if (server.argName(i).equals("baudrate2")) {
      settings->baudrate2 = server.arg(i).toInt();
    } else if (server.argName(i).equals("invert2")) {
      settings->invert2 = server.arg(i).toInt();
    } else if (server.argName(i).equals("alt_udp")) {
      settings->alt_udp = server.arg(i).toInt();
    } else if (server.argName(i).equals("rx1090")) {
      settings->rx1090 = server.arg(i).toInt();
    } else if (server.argName(i).equals("gdl90_in")) {
      settings->gdl90_in = server.arg(i).toInt();
    } else if (server.argName(i).equals("gdl90")) {
      settings->gdl90 = server.arg(i).toInt();
    } else if (server.argName(i).equals("d1090")) {
      settings->d1090 = server.arg(i).toInt();
    } else if (server.argName(i).equals("relay")) {
      settings->relay = server.arg(i).toInt();
    } else if (server.argName(i).equals("stealth")) {
      settings->stealth = server.arg(i).toInt();
    } else if (server.argName(i).equals("no_track")) {
      settings->no_track = server.arg(i).toInt();
    } else if (server.argName(i).equals("power_save")) {
      settings->power_save = server.arg(i).toInt();
    } else if (server.argName(i).equals("power_external")) {
      settings->power_external = server.arg(i).toInt();
    } else if (server.argName(i).equals("rfc")) {
      settings->freq_corr = server.arg(i).toInt();
    } else if (server.argName(i).equals("id_method")) {
      settings->id_method = server.arg(i).toInt();
    } else if (server.argName(i).equals("aircraft_id")) {
      server.arg(i).toCharArray(idbuf, sizeof(idbuf));
      settings->aircraft_id = strtoul(idbuf, NULL, 16);
    } else if (server.argName(i).equals("ignore_id")) {
      server.arg(i).toCharArray(idbuf, sizeof(idbuf));
      settings->ignore_id = strtoul(idbuf, NULL, 16);
    } else if (server.argName(i).equals("follow_id")) {
      server.arg(i).toCharArray(idbuf, sizeof(idbuf));
      settings->follow_id = strtoul(idbuf, NULL, 16);
    } else if (server.argName(i).equals("alarmlog")) {
      server.arg(i).toCharArray(idbuf, sizeof(idbuf));
      settings->logalarms = strtoul(idbuf, NULL, 16);
    } else if (server.argName(i).equals("ppswire")) {
      settings->ppswire = server.arg(i).toInt();
    } else if (server.argName(i).equals("debug_flags")) {
      server.arg(i).toCharArray(idbuf, 3);
      settings->debug_flags = strtoul(idbuf, NULL, 16) & 0x3F;

#if defined(USE_OGN_ENCRYPTION)
    } else if (server.argName(i).equals("igc_key")) {
        char buf[32 + 1];
        uint32_t key;
        server.arg(i).toCharArray(buf, sizeof(buf));
        key = strtoul(buf + 24, NULL, 16);
        if (key != 0x88888888)  settings->igc_key[3] = key;
        buf[24] = 0;
        key = strtoul(buf + 16, NULL, 16);
        if (key != 0x88888888)  settings->igc_key[2] = key;
        buf[16] = 0;
        key = strtoul(buf + 8, NULL, 16);
        if (key != 0x88888888)  settings->igc_key[1] = key;
        buf[8] = 0;
        key = strtoul(buf + 0, NULL, 16);
        if (key != 0x88888888)  settings->igc_key[0] = key;
#endif
    }
    yield();
  }                // end for (server.args())

  /* enforce some restrictions on input and output routes */
  int nmea1 = settings->nmea_out;
  int nmea2 = settings->nmea_out2;
  Serial.print(F("NMEA_Output1 (given) = ")); Serial.println(nmea1);
  Serial.print(F("NMEA_Output2 (given) = ")); Serial.println(nmea2);
  if (hw_info.model == SOFTRF_MODEL_PRIME_MK2) {
    if (nmea1==DEST_USB)    nmea1==DEST_UART;
    if (nmea2==DEST_USB)    nmea2==DEST_UART;   // same thing
  }
  if (nmea2 == nmea1)    nmea2 = DEST_NONE;
  if (settings->gdl90_in == DEST_UDP) {
      if (settings->gdl90 == DEST_UDP) {
          settings->gdl90 = DEST_NONE;
          Serial.println(F("GDL input from UDP, GDL output turned OFF"));
      }
  }
//  bool wireless1 = (nmea1==DEST_UDP || nmea1==DEST_TCP || nmea1==DEST_BLUETOOTH);
//  bool wireless2 = (nmea2==DEST_UDP || nmea2==DEST_TCP || nmea2==DEST_BLUETOOTH);
  bool wifi1 = (nmea1==DEST_UDP || nmea1==DEST_TCP);
  bool wifi2 = (nmea2==DEST_UDP || nmea2==DEST_TCP);
// >>> try and allow Bluetooth along with WiFi:
//  if (wifi1 && nmea2==DEST_BLUETOOTH)
//        nmea2 = DEST_NONE;      // only one wireless output type possible
//  if (wifi2 && nmea1==DEST_BLUETOOTH)
//        nmea2 = DEST_NONE;
  Serial.print(F("NMEA_Output1 (adjusted) = ")); Serial.println(nmea1);
  settings->nmea_out  = nmea1;
  Serial.print(F("NMEA_Output2 (adjusted) = ")); Serial.println(nmea2);
  settings->nmea_out2 = nmea2;
  //if (nmea1==DEST_BLUETOOTH || nmea2==DEST_BLUETOOTH
  //      || settings->d1090 == DEST_BLUETOOTH || settings->gdl90 == DEST_BLUETOOTH) {
  //    if (settings->bluetooth == BLUETOOTH_OFF)
  //        settings->bluetooth = BLUETOOTH_SPP;
  //}
#if !defined(EXCLUDE_D1090)
  if (nmea1 != DEST_BLUETOOTH && nmea2 != DEST_BLUETOOTH
          && settings->d1090 != DEST_BLUETOOTH && settings->gdl90 != DEST_BLUETOOTH) {
      settings->bluetooth = BLUETOOTH_OFF;
  }
#else
  if (nmea1 != DEST_BLUETOOTH && nmea2 != DEST_BLUETOOTH && settings->gdl90 != DEST_BLUETOOTH) {
      settings->bluetooth = BLUETOOTH_OFF;
  }
#endif
  Serial.print(F("Bluetooth (adjusted) = ")); Serial.println(settings->bluetooth);

  /* enforce some hardware restrictions */
  if (hw_info.model == SOFTRF_MODEL_PRIME_MK2) {
      if (settings->rx1090 != ADSB_RX_NONE) {
          // dedicate Serial2 to the ADS-B receiver module
          settings->baudrate2 = BAUD_DEFAULT;       // will actually use 921600
          settings->invert2 = false;
          if (settings->nmea_out  == DEST_UART2)
              settings->nmea_out   = DEST_NONE;
          if (settings->nmea_out2 == DEST_UART2)
              settings->nmea_out2  = DEST_NONE;
          if (settings->gdl90     == DEST_UART2)
              settings->gdl90      = DEST_NONE;
          if (settings->gdl90_in  == DEST_UART2)
              settings->gdl90_in   = DEST_NONE;
          if (settings->d1090     == DEST_UART2)
              settings->d1090      = DEST_NONE;
      }
      if (settings->voice == VOICE_EXT)
          settings->volume = BUZZER_OFF;  // free up pins 14 & 15 for I2S use
  } else {
      settings->voice = VOICE_OFF;
  }

  /* if winch mode, use full transmission power */
  if (settings->aircraft_type == AIRCRAFT_TYPE_WINCH && settings->txpower == RF_TX_POWER_LOW)
      settings->txpower == RF_TX_POWER_FULL;

  /* show new settings before rebooting */
  size_t size = 3800;
  char *Input_temp = (char *) malloc(size);
  if (Input_temp != NULL) {
    snprintf_P ( Input_temp, size,
PSTR("<html>\
<head>\
<meta http-equiv='refresh' content='15; url=/'>\
<meta name='viewport' content='width=device-width, initial-scale=1'>\
<title>SoftRF Settings</title>\
</head>\
<body>\
<h1 align=center>New settings:</h1>\
<table width=100%%>\
<tr><th align=left>Mode</th><td align=right>%d</td></tr>\
<tr><th align=left>Aircraft ID</th><td align=right>%06X</td></tr>\
<tr><th align=left>ID method</th><td align=right>%d</td></tr>\
<tr><th align=left>Ignore ID</th><td align=right>%06X</td></tr>\
<tr><th align=left>Follow ID</th><td align=right>%06X</td></tr>\
<tr><th align=left>Protocol</th><td align=right>%d</td></tr>\
<tr><th align=left>Band</th><td align=right>%d</td></tr>\
<tr><th align=left>Aircraft type</th><td align=right>%d</td></tr>\
<tr><th align=left>Alarm trigger</th><td align=right>%d</td></tr>\
<tr><th align=left>Tx Power</th><td align=right>%d</td></tr>\
<tr><th align=left>Volume</th><td align=right>%d</td></tr>\
<tr><th align=left>Strobe</th><td align=right>%d</td></tr>\
<tr><th align=left>LED pointer</th><td align=right>%d</td></tr>\
<tr><th align=left>Voice</th><td align=right>%d</td></tr>\
<tr><th align=left>Baud 1</th><td align=right>%d</td></tr>\
<tr><th align=left>Alt RX pin</th><td align=right>%d</td></tr>\
<tr><th align=left>Baud 2</th><td align=right>%d</td></tr>\
<tr><th align=left>Invert 2</th><td align=right>%d</td></tr>\
<tr><th align=left>Alt UDP port</th><td align=right>%d</td></tr>\
<tr><th align=left>Bluetooth</th><td align=right>%d</td></tr>\
<tr><th align=left>TCP mode</th><td align=right>%d</td></tr>\
<tr><th align=left>TCP port</th><td align=right>%d</td></tr>\
<tr><th align=left>SSID</th><td align=right>%s</td></tr>\
<tr><th align=left>Host IP</th><td align=right>%s</td></tr>\
<tr><th align=left>NMEA Out 1</th><td align=right>%d</td></tr>\
<tr><th align=left>NMEA GNSS</th><td align=right>%s</td></tr>\
<tr><th align=left>NMEA Private</th><td align=right>%s</td></tr>\
<tr><th align=left>NMEA Legacy</th><td align=right>%s</td></tr>\
<tr><th align=left>NMEA Sensors</th><td align=right>%s</td></tr>\
<tr><th align=left>NMEA Debug</th><td align=right>%s</td></tr>\
<tr><th align=left>NMEA External</th><td align=right>%s</td></tr>\
<tr><th align=left>NMEA Out 2</th><td align=right>%d</td></tr>\
<tr><th align=left>NMEA2 GNSS</th><td align=right>%s</td></tr>\
<tr><th align=left>NMEA2 Private</th><td align=right>%s</td></tr>\
<tr><th align=left>NMEA2 Legacy</th><td align=right>%s</td></tr>\
<tr><th align=left>NMEA2 Sensors</th><td align=right>%s</td></tr>\
<tr><th align=left>NMEA2 Debug</th><td align=right>%s</td></tr>\
<tr><th align=left>NMEA2 External</th><td align=right>%s</td></tr>\
<tr><th align=left>ADS-B Receiver</th><td align=right>%d</td></tr>\
<tr><th align=left>GDL90 in</th><td align=right>%d</td></tr>\
<tr><th align=left>GDL90 out</th><td align=right>%d</td></tr>\
<tr><th align=left>DUMP1090</th><td align=right>%d</td></tr>\
<tr><th align=left>Air-Relay</th><td align=right>%d</td></tr>\
<tr><th align=left>Stealth</th><td align=right>%s</td></tr>\
<tr><th align=left>No track</th><td align=right>%s</td></tr>\
<tr><th align=left>Power save</th><td align=right>%d</td></tr>\
<tr><th align=left>Power external</th><td align=right>%d</td></tr>\
<tr><th align=left>Freq. correction</th><td align=right>%d</td></tr>\
<tr><th align=left>Alarm Log</th><td align=right>%d</td></tr>\
<tr><th align=left>PPS wire</th><td align=right>%d</td></tr>\
<tr><th align=left>debug_flags</th><td align=right>%02X</td></tr>\
<tr><th align=left>IGC key</th><td align=right>%08X%08X%08X%08X</td></tr>\
</table>\
<hr>\
  <p align=center><h1 align=center>Restart is in progress... Please, wait!</h1></p>\
</body>\
</html>"),
    settings->mode, settings->aircraft_id, settings->id_method,
    settings->ignore_id, settings->follow_id,
    settings->rf_protocol, settings->band,
    settings->aircraft_type, settings->alarm, settings->txpower,
    settings->volume, settings->strobe, settings->pointer, settings->voice,
    settings->baud_rate, settings->altpin0, settings->baudrate2,
    settings->invert2, settings->alt_udp, settings->bluetooth,
    settings->tcpmode, settings->tcpport, settings->ssid, settings->host_ip,
    settings->nmea_out,
    BOOL_STR(settings->nmea_g), BOOL_STR(settings->nmea_p), BOOL_STR(settings->nmea_l),
    BOOL_STR(settings->nmea_s), BOOL_STR(settings->nmea_d), BOOL_STR(settings->nmea_e),
    settings->nmea_out2,
    BOOL_STR(settings->nmea2_g), BOOL_STR(settings->nmea2_p), BOOL_STR(settings->nmea2_l),
    BOOL_STR(settings->nmea2_s), BOOL_STR(settings->nmea2_d), BOOL_STR(settings->nmea2_e),
    settings->rx1090, settings->gdl90_in, settings->gdl90, settings->d1090,
    settings->relay, BOOL_STR(settings->stealth), BOOL_STR(settings->no_track),
    settings->power_save, settings->power_external,
    settings->freq_corr, settings->logalarms, settings->ppswire, settings->debug_flags,
  //  settings->igc_key[0], settings->igc_key[1], settings->igc_key[2], settings->igc_key[3]
    (settings->igc_key[0]? 0x88888888 : 0),
    (settings->igc_key[1]? 0x88888888 : 0),
    (settings->igc_key[2]? 0x88888888 : 0),
    (settings->igc_key[3]? 0x88888888 : 0)
        /* do not show the existing secret key */
    );
    server.send ( 200, "text/html", Input_temp );
    delay(1000);
    free(Input_temp);
  }

  Serial.println(F("New settings:"));
  show_settings_serial();

  SoC->WDT_fini();
  if (SoC->Bluetooth_ops) { SoC->Bluetooth_ops->fini(); }
  EEPROM_store();
  reboot();
}

void handleNotFound() {

  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += ( server.method() == HTTP_GET ) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for ( uint8_t i = 0; i < server.args(); i++ ) {
    message += " " + server.argName ( i ) + ": " + server.arg ( i ) + "\n";
  }

  server.send ( 404, textplain, message );
}

bool handleFileRead(String path) { // send the requested file to the client (if it exists)
  Serial.println("handleFileRead: " + path);
  if (path.endsWith("/"))
    return false;
  if (! path.startsWith("/"))
    path = "/" + path;
  String contentType = "application/x-object";             // fake the MIME type
  if (SPIFFS.exists(path)) {                               // If the file exists
    File file = SPIFFS.open(path, "r");                    // Open the file
    size_t sent = server.streamFile(file, contentType);    // Send it to the client
    file.close();                                          // Close the file again
    Serial.println(String(F("\tSent file: ")) + path);
    return true;
  }
  Serial.println(String(F("\tFile Not Found: ")) + path);
  return false;
}

void serve_P_html(const char *html)
{
    SoC->swSer_enableRx(false);
    server.sendHeader(String(F("Cache-Control")), String(F("no-cache, no-store, must-revalidate")));
    server.sendHeader(String(F("Pragma")), String(F("no-cache")));
    server.sendHeader(String(F("Expires")), String(F("-1")));
    server.send_P ( 200, PSTR("text/html"), html);
    SoC->swSer_enableRx(true);
}

void Web_setup()
{
  server.on ( "/", handleRoot );

  server.on ( "/settings", handleSettings );

  server.on ( "/reboot", []() {
    reboot();
  } );

  server.on ( "/about", []() {
    serve_P_html(about_html);
  } );

#if defined(ESP32)
  server.on ( "/wavupload", []() {
    serve_P_html(upload_html);
  } );

  server.on("/dowavupld", HTTP_POST,  // if the client posts to the upload page
    [](){ server.send(200); },        // Send 200 to tell the client we are ready to receive
    wavUpload                         // Receive and save the file
  );

  server.on( "/format", []() {
    clear_waves();
    Serial.println(F("Formatting spiffs..."));
    SPIFFS.format();
    server.send(200, textplain, "SPIFFS cleared");
  } );

  server.on ( "/alarmlog", alarmlogfile );

  server.on( "/clearlog", []() {
    if (SPIFFS.exists("/alarmlog.txt")) {
        if (AlarmLogOpen) {
          AlarmLog.close();
          AlarmLogOpen = false;
        }
        SPIFFS.remove("/alarmlog.txt");
    }
    server.send(200, textplain, "Alarm Log cleared");
  } );
#endif

  server.on ( "/input", handleInput );

  server.onNotFound([]() {                              // If the client requests any URI
    if (!handleFileRead(server.uri()))                  // send it if it exists
        handleNotFound();
  });

  server.on("/firmware", HTTP_GET, [](){
    SoC->swSer_enableRx(false);
    stop_bluetooth();
    server.sendHeader(String(F("Connection")), String(F("close")));
    server.sendHeader(String(F("Access-Control-Allow-Origin")), String(F("*")));
    server.send_P(200,
      PSTR("text/html"),
      PSTR("\
<html>\
  <head>\
    <meta name='viewport' content='width=device-width, initial-scale=1'>\
    <title>Firmware update</title>\
  </head>\
<body>\
<body>\
 <h1 align=center>Firmware update</h1>\
 <hr>\
 <table width=100%%>\
  <tr>\
    <td align=center>\
<form method='POST' action='/update' enctype='multipart/form-data' id='upload_form'>\
    <input type='file' name='update'>\
    <input type='submit' value='Update'>\
</form>\
    </td>\
  </tr>\
 </table>\
 <h3 align=center>Be patient...</h3>\
</body>\
</html>")
    );
  SoC->swSer_enableRx(true);
  });

  server.on("/update", HTTP_POST, [](){
    SoC->swSer_enableRx(false);
    server.sendHeader(String(F("Connection")), String(F("close")));
    server.sendHeader(String(F("Access-Control-Allow-Origin")), "*");
    server.send(200, textplain, (Update.hasError())?"UPDATE FAILED":"UPDATE DONE, REBOOTING");
//    SoC->swSer_enableRx(true);
    Buzzer_fini();
    Voice_fini();
    RF_Shutdown();
    delay(1000);
    reboot_pending = true;   // will reboot 2 seconds later in Web_loop()
    //SoC->reset();
  },[](){
    HTTPUpload& upload = server.upload();
    if(upload.status == UPLOAD_FILE_START){
      Serial.setDebugOutput(true);
      SoC->WiFiUDP_stopAll();
      SoC->WDT_fini();
      Serial.printf("Update: %s\r\n", upload.filename.c_str());
      uint32_t maxSketchSpace = SoC->maxSketchSpace();
      if (Update.begin(maxSketchSpace)) {   //start with max available size
#if defined(ESP32)
        blue_LED_1hz();
        OLED_msg("UPDATE", "...");
#endif
      } else {
        Update.printError(Serial);
#if defined(ESP32)
        blue_LED_4hz();
        OLED_msg("UPDATE", "FAILED");
#endif
        Serial.println("update.begin failed");
      }
    } else if(upload.status == UPLOAD_FILE_WRITE){
      if(Update.write(upload.buf, upload.currentSize) != upload.currentSize){
        Update.printError(Serial);
#if defined(ESP32)
        blue_LED_4hz();
        OLED_msg("UPDATE", "FAILED");
#endif
        Serial.println("\r\nupdate.write failed");
      } else {
        static uint16_t i = 0;
        static uint16_t j = 0;
        ++i;
        if ((i >> 4) != j) { 
          j = (i >> 4);
          Serial.print(".");
          if ((j & 31) == 0)
              Serial.print("\r\n");
          char buf[8];
#if defined(ESP32)
          snprintf(buf,7,"..%d..",j);
          OLED_msg("UPDATE", buf);
#endif
        }
      }
    } else if(upload.status == UPLOAD_FILE_END){
      if(Update.end(true)){ //true to set the size to the current progress
        Serial.printf("\r\nUpdate Success: %u\r\nRebooting...\r\n", upload.totalSize);
#if defined(ESP32)
        blue_LED_on();
        OLED_msg("UPDATE", "SUCCESS");
#endif
      } else {
        Update.printError(Serial);
#if defined(ESP32)
        blue_LED_4hz();
        OLED_msg("UPDATE", "FAILED");
#endif
        Serial.println("\r\nupdate.end failed");
      }
      Serial.setDebugOutput(false);
    }
    yield();
  });

//  server.on ( "/logo.png", []() {
//    server.send_P ( 200, "image/png", Logo, sizeof(Logo) );   - skipped to save space
//  } );

//  server.on ( "/jquery.min.js", []() { ... } );   - skipped to save space

  server.begin();
  Serial.println (F("HTTP server has started at port: 80"));

  delay(1000);
}

void Web_loop()
{
  server.handleClient();
  if (reboot_pending) {
    delay(2000);
    SoC->reset();
  }
}

void Web_fini()
{
  server.stop();
}

#endif /* EXCLUDE_WIFI */
