/*
 *  Application note: ESP32-CAPTIVE-PORTAL
 *  Version 0.2
 *  Copyright (C) 2021  
 *  McBoeck
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/  

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h> 
#include <WebServer.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <Update.h>
#include <EEPROM.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#define ESP_getChipId()   ((uint32_t)ESP.getEfuseMac())

static const byte WiFiPwdLen = 25;
static const byte APSTANameLen = 20;
static const byte HostNameLen =20;

/*____Captiveportal____*/
const char CPHTTP_HEAD[] PROGMEM            = "<!DOCTYPE html><html lang=\"en\"><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/><title>{v}</title>";
const char CPHTTP_STYLE[] PROGMEM           = "<style>.c{text-align: center;} div,input{padding:5px;font-size:1em;} input{width:95%;} body{text-align: center;font-family:verdana;} button{border:0;border-radius:0.3rem;background-color:#1fa3ec;color:#fff;line-height:2.4rem;font-size:1.2rem;width:100%;} .q{float: right;width: 64px;text-align: right;} .l{background: url(\"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAACAAAAAgCAMAAABEpIrGAAAALVBMVEX///8EBwfBwsLw8PAzNjaCg4NTVVUjJiZDRUUUFxdiZGSho6OSk5Pg4eFydHTCjaf3AAAAZElEQVQ4je2NSw7AIAhEBamKn97/uMXEGBvozkWb9C2Zx4xzWykBhFAeYp9gkLyZE0zIMno9n4g19hmdY39scwqVkOXaxph0ZCXQcqxSpgQpONa59wkRDOL93eAXvimwlbPbwwVAegLS1HGfZAAAAABJRU5ErkJggg==\") no-repeat left center;background-size: 1em;}</style>";
const char CPHTTP_SCRIPT[] PROGMEM          = "<script>function c(l){document.getElementById('s').value=l.innerText||l.textContent;document.getElementById('p').focus();}function hC(cb){this.open('?'+'sip='+cb.checked,'_self');}</script>";
const char CPHTTP_HEAD_END[] PROGMEM        = "</head><body><div style='text-align:left;display:inline-block;min-width:260px;'>";
const char CPHTTP_PORTAL_OPTIONS[] PROGMEM  = "<form action=\"/wifi\" method=\"get\"><button>Configure WiFi</button></form><br/><form action=\"/0wifi\" method=\"get\"><button>Configure WiFi (No Scan)</button></form><br/><form action=\"/reset\" method=\"get\"><button>Reset to default</button></form><br/>";
const char CPHTTP_ITEM[] PROGMEM            = "<div><a href='#p' onclick='c(this)'>{v}</a>&nbsp;<span class='q {i}'>{r}%</span></div>";
const char CPHTTP_FORM_START[] PROGMEM      = "<label><input style='width:10%' type='checkbox' onclick='hC(this)'> Static IP</label><form method='get' action='wifisave'><label><input style='width:10%' type='checkbox' id='ap' name='ap'> AP Mode</label><input id='s' name='s' length=32 placeholder='SSID'><br/><input id='p' name='p' length=64 type='password' placeholder='password'><br/><br/><input id='h' name='h' length=20 placeholder='hostname'><br/>";
const char CPHTTP_FORM_PARAM[] PROGMEM      = "<br/><input id='{i}' name='{n}' length={l} placeholder='{p}' value='{v}'>";
const char CPHTTP_FORM_END[] PROGMEM        = "<br/><button type='submit'>save</button></form>";
const char CPHTTP_SCAN_LINK[] PROGMEM       = "<br/><div class=\"c\"><a href=\"/wifi\">Scan</a></div>";
const char CPHTTP_END[] PROGMEM             = "</div></body></html>";

struct WiFiEEPromData{
  bool APSTA = true;            // Access Point or Station Mode - true AP Mode
  bool PwDReq = false;          // PasswordRequired
  bool CapPortal = true;        // CaptivePortal on in AP Mode
  char APSTAName[APSTANameLen]; // STATION /AP Point Name TO cONNECT, if definded   
  char WiFiPwd[WiFiPwdLen];     // WiFiPAssword, if definded
  char HostName[HostNameLen];   // Hostname, if defined
  byte StaticIP = 0;            // Static IP 0=off, 1=on, 2=IP without DNS valid, 3=IP incl. DNS Valid, 4=IP settings invalid
  IPAddress IPAdd;              // IP for Static IP
  IPAddress Gate;               // Gateway for Static IP
  IPAddress SubNet;             // Subnet for Static IP
  IPAddress DNS;                // DNS for Static IP
  char ConfigValid[3];          //If Config is Vaild, Tag "TK" is required"
};

// WiFiUpdate
const String PROGMEM serverUpdate = "<form method='POST' action='/upgrade' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>";

// hostname for mDNS
String ESPHostname = "ESP_" + String((uint32_t)ESP.getEfuseMac(), HEX);

// DNS server
const byte DNS_PORT = 53;
DNSServer dnsServer;

//Conmmon Paramenters
bool SoftAccOK  = false;

// Web server
WebServer server(80);

/* Soft AP network parameters */
IPAddress CPapIP(172, 20, 0, 1);
IPAddress CPnetMsk(255, 255, 255, 0);
IPAddress EmptyIP(0, 0, 0, 0);

WiFiEEPromData MyWiFiConfig;

unsigned long currentMillis = 0;
unsigned long startMillis;

/** Current WLAN status */
short status = WL_IDLE_STATUS;

// Is this an IP?
boolean isIp(String str) {
  for (int i = 0; i < str.length(); i++) {
    int c = str.charAt(i);
    if (c != '.' && (c < '0' || c > '9')) {
      return false;
    }
  }
  return true;
}

// convert IP to String
String toStringIp(IPAddress ip) {
  String res = "";
  for (int i = 0; i < 3; i++) {
    res += String((ip >> (8 * i)) & 0xFF) + ".";
  }
  res += String(((ip >> 8 * 3)) & 0xFF);
  return res;
}

// Show Wifi quality
int getRSSIasQuality(int RSSI) {
  int quality = 0;

  if (RSSI <= -100) {
    quality = 0;
  } else if (RSSI >= -50) {
    quality = 100;
  } else {
    quality = 2 * (RSSI + 100);
  }
  return quality;
}

// Store WLAN credentials to EEPROM
int saveCredentials() {
  int RetValue;
  // Check logical Errors
  RetValue = 4;
  if  (MyWiFiConfig.APSTA == true ) //AP Mode
    {
    if (MyWiFiConfig.PwDReq and (sizeof(String(MyWiFiConfig.WiFiPwd)) < 8))
      {    
        RetValue = 2;  // Invalid Config Password to short
      }
    if (sizeof(String(MyWiFiConfig.APSTAName)) < 1)
      {
        RetValue = 3;  // Invalid Config AP Name to short
      }
    } 
  if (RetValue == 4)
    {
    EEPROM.begin(512);
    for (int i = 0 ; i < sizeof(MyWiFiConfig) ; i++) 
      {
        EEPROM.write(i, 0);
      }
    strncpy( MyWiFiConfig.ConfigValid , "TK", sizeof(MyWiFiConfig.ConfigValid) );
    EEPROM.put(0, MyWiFiConfig);
    EEPROM.commit();
    EEPROM.end();
    RetValue = 1;
    }
  return RetValue;
}

//  Captive Portal
void handleCP() {  
  String page = FPSTR(CPHTTP_HEAD);
  page.replace("{v}", "Options");
  page += FPSTR(CPHTTP_SCRIPT);
  page += FPSTR(CPHTTP_STYLE);
  page += FPSTR(CPHTTP_HEAD_END);
  page += "<h1>";
  page += MyWiFiConfig.HostName;
  page += "</h1>";
  page += F("<h3>WiFiManager</h3>");
  page += FPSTR(CPHTTP_PORTAL_OPTIONS);
  page += FPSTR(CPHTTP_END);

  server.sendHeader("Content-Length", String(page.length()));
  server.send(200, "text/html", page);
}

// Redirect to captive portal if we got a request for another domain. Return true in that case so the page handler do not try to handle the request again.
boolean captivePortal() {
  if ((!isIp(server.hostHeader()) && server.hostHeader() != (String(MyWiFiConfig.HostName)+".local")) || server.hostHeader()==toStringIp(CPapIP)) {
    Serial.println("Request redirected to captive portal");  
    handleCP();
    return true;
  }
  return false;
}
 
// Reset settings to default
void handleReset() {
  byte len;
  String page = "Success! Reboot in 2s.";
  MyWiFiConfig.APSTA = true;
  MyWiFiConfig.PwDReq = true;  // default PW required
  MyWiFiConfig.CapPortal = true;
  strncpy( MyWiFiConfig.APSTAName, "ESP_Config", sizeof(MyWiFiConfig.APSTAName) );
  len = strlen(MyWiFiConfig.APSTAName);
  MyWiFiConfig.APSTAName[len+1] = '\0';   
  strncpy( MyWiFiConfig.WiFiPwd, "12345678", sizeof(MyWiFiConfig.WiFiPwd) );
  len = strlen(MyWiFiConfig.WiFiPwd);
  MyWiFiConfig.WiFiPwd[len+1] = '\0';  
  strncpy( MyWiFiConfig.HostName, ESPHostname.c_str(), sizeof(MyWiFiConfig.HostName) );
  len = strlen(MyWiFiConfig.HostName);
  MyWiFiConfig.HostName[len+1] = '\0';
  strncpy( MyWiFiConfig.ConfigValid, "TK", sizeof(MyWiFiConfig.ConfigValid) );
  len = strlen(MyWiFiConfig.ConfigValid);
  MyWiFiConfig.ConfigValid[len+1] = '\0'; 
  MyWiFiConfig.IPAdd = EmptyIP;
  MyWiFiConfig.Gate = EmptyIP;
  MyWiFiConfig.SubNet = EmptyIP;
  MyWiFiConfig.DNS = EmptyIP;
  saveCredentials();
  server.sendHeader("Content-Length", String(page.length()));
  server.send(200, "text/html", page);
  Serial.println(F("Reset WiFi Credentials. Reboot in 2s"));
  delay(2000);
  ESP.restart(); 
}

//  Main Page
void handleRoot() {  
  if (captivePortal()) { // If captive portal redirect instead of displaying the page.
    return;
  }
  String page = FPSTR(CPHTTP_HEAD);
  page.replace("{v}", "Options");
  page += FPSTR(CPHTTP_SCRIPT);
  page += FPSTR(CPHTTP_STYLE);
  page += FPSTR(CPHTTP_HEAD_END);
  page += "<h1>";
  page += MyWiFiConfig.HostName;
  page += "</h1>";
  page += F("<h3>WiFiManager</h3>");
  page += FPSTR(CPHTTP_PORTAL_OPTIONS);
  page += FPSTR(CPHTTP_END);

  //server.sendHeader("Content-Length", String(page.length()));
  server.send(200, "text/plain", "ROOT");
}

// OTA
void handleUpdate() {  
  server.sendHeader("Content-Length", String(serverUpdate.length()));
  server.send(200, "text/html", serverUpdate);
}

// Handle unknown Pages
void handleNotFound() { 
  if (captivePortal()) { // If captive portal redirect instead of displaying the error page.
      return;
    } 
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
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.sendHeader("Content-Length", String(message.length()));
  server.send ( 404, "text/plain", message );
}

// Wifi config page handler
void handleWifi(boolean scan) {
  String sip = server.arg("sip").c_str();
  if (sip == "true") {
    MyWiFiConfig.StaticIP = 1;
  }
  else if(sip == "false") {
    MyWiFiConfig.StaticIP = 0;
  }
  String page = FPSTR(CPHTTP_HEAD);
  page.replace("{v}", "Config ESP");
  page += FPSTR(CPHTTP_SCRIPT);
  page += FPSTR(CPHTTP_STYLE);
  //page += _customHeadElement;
  page += FPSTR(CPHTTP_HEAD_END);

  if (scan) {
    int n = WiFi.scanNetworks();
    if (n == 0) {
      page += F("No networks found. Refresh to scan again.");
    } else {

      //sort networks
      int indices[n];
      for (int i = 0; i < n; i++) {
        indices[i] = i;
      }

      // RSSI SORT

      // old sort
      for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
          if (WiFi.RSSI(indices[j]) > WiFi.RSSI(indices[i])) {
            std::swap(indices[i], indices[j]);
          }
        }
      }

      // remove duplicates ( must be RSSI sorted )
      String cssid;
      for (int i = 0; i < n; i++) {
        if (indices[i] == -1) continue;
        cssid = WiFi.SSID(indices[i]);
        for (int j = i + 1; j < n; j++) {
          if (cssid == WiFi.SSID(indices[j])) {
            indices[j] = -1; // set dup aps to index -1
          }
        }
      }   

      //display networks in page
      for (int i = 0; i < n; i++) {
        if (indices[i] == -1) continue; // skip dups
        int quality = getRSSIasQuality(WiFi.RSSI(indices[i]));

        if (-1 < quality) {
          String item = FPSTR(CPHTTP_ITEM);
          String rssiQ;
          rssiQ += quality;
          item.replace("{v}", WiFi.SSID(indices[i]));
          item.replace("{r}", rssiQ);
          if (WiFi.encryptionType(indices[i]) != WIFI_AUTH_OPEN) {
                        item.replace("{i}", "l");
          } else {
            item.replace("{i}", "");
          }
          //DEBUG_WM(item);
          page += item;
          delay(0);
        } 
      }
      page += "<br/>";
    }
  }

  if (MyWiFiConfig.StaticIP > 0) {
    String item = FPSTR(CPHTTP_FORM_START);
    String temp = MyWiFiConfig.APSTAName;
    item.replace("> S", " checked> S");
    if (strcmp(MyWiFiConfig.APSTAName, "") != 0) {
      item.replace("'SSID'", "'SSID' value='" + temp + "'");  
    }
    if (strcmp(MyWiFiConfig.HostName, ESPHostname.c_str()) != 0) {
      temp = MyWiFiConfig.HostName;
      item.replace("'hostname'", "'hostname' value='" + temp + "'");
    }
    page += item;
  }
  else {
    String item = FPSTR(CPHTTP_FORM_START);
    String temp = MyWiFiConfig.APSTAName;
    if (strcmp(MyWiFiConfig.APSTAName, "") != 0) {
      item.replace("'SSID'", "'SSID' value='" + temp + "'");
    }
    if (strcmp(MyWiFiConfig.HostName, ESPHostname.c_str()) != 0) {
      temp = MyWiFiConfig.HostName;
      item.replace("'hostname'", "'hostname' value='" + temp + "'");
    }
    page += item;
  }

  if (MyWiFiConfig.StaticIP > 0) {

    String item = FPSTR(CPHTTP_FORM_PARAM);
    item.replace("{i}", "ip");
    item.replace("{n}", "ip");
    item.replace("{p}", "Static IP");
    item.replace("{l}", "15");
    if (MyWiFiConfig.IPAdd != EmptyIP) {
      item.replace("{v}", toStringIp(MyWiFiConfig.IPAdd));
    }
    else {
      item.replace("{v}", "");
    }
    
    page += item;

    item = FPSTR(CPHTTP_FORM_PARAM);
    item.replace("{i}", "gw");
    item.replace("{n}", "gw");
    item.replace("{p}", "Static Gateway");
    item.replace("{l}", "15");
    if (MyWiFiConfig.Gate != EmptyIP) {
      item.replace("{v}", toStringIp(MyWiFiConfig.Gate));
    }
    else {
      item.replace("{v}", "");
    }

    page += item;

    item = FPSTR(CPHTTP_FORM_PARAM);
    item.replace("{i}", "sn");
    item.replace("{n}", "sn");
    item.replace("{p}", "Subnet");
    item.replace("{l}", "15");
    if (MyWiFiConfig.SubNet != EmptyIP) {
      item.replace("{v}", toStringIp(MyWiFiConfig.SubNet));
    }
    else {
      item.replace("{v}", "");
    }

    page += item;

    item = FPSTR(CPHTTP_FORM_PARAM);
    item.replace("{i}", "dns");
    item.replace("{n}", "dns");
    item.replace("{p}", "DNS");
    item.replace("{l}", "15");
    if (MyWiFiConfig.DNS != EmptyIP) {
      item.replace("{v}", toStringIp(MyWiFiConfig.DNS));
    }
    else {
      item.replace("{v}", "");
    }

    page += item;
    page += "<br/>";
  }

  page += FPSTR(CPHTTP_FORM_END);
  page += FPSTR(CPHTTP_SCAN_LINK);

  page += FPSTR(CPHTTP_END);

  server.sendHeader("Content-Length", String(page.length()));
  server.send(200, "text/html", page);
}

// Wifi handler with scan
void handleWifi1(){
  handleWifi(true);
}

// Wifi handler without scan
void handleWifi0(){
  handleWifi(false);
}

// Safe Settings of Portal
void handleWifiSave(){
  String _ap = server.arg("ap").c_str();
  String _ssid = server.arg("s").c_str();
  String _pass = server.arg("p").c_str();
  String _host = server.arg("h").c_str();
  String _ip = server.arg("ip").c_str();
  String _gw = server.arg("gw").c_str();
  String _sn = server.arg("sn").c_str();
  String _dns = server.arg("dns").c_str(); 
  String page;
  char temp[] = "";
  int ret_val = 0;
  byte len;
  MyWiFiConfig.APSTA = false;
  if (_ap == "on") {
    MyWiFiConfig.APSTA = true;
    if (_pass == ""){
      MyWiFiConfig.PwDReq = false;
    }
  }
  if (_ssid != "") {
    _ssid.toCharArray(temp, _ssid.length() + 1);
    strncpy(MyWiFiConfig.APSTAName, temp, sizeof(MyWiFiConfig.APSTAName));
    len = strlen(MyWiFiConfig.APSTAName);
    MyWiFiConfig.APSTAName[len+1] = '\0';  
  }
  if (_pass != "" || !MyWiFiConfig.PwDReq) {
    _pass.toCharArray(temp, _pass.length() + 1);
    strncpy(MyWiFiConfig.WiFiPwd, temp, sizeof(MyWiFiConfig.WiFiPwd));
    len = strlen(MyWiFiConfig.WiFiPwd);
    MyWiFiConfig.WiFiPwd[len+1] = '\0';  
  }
  if (_host!= "") {
    _host.toCharArray(temp, _host.length() + 1);
    strncpy(MyWiFiConfig.HostName, temp, sizeof(MyWiFiConfig.HostName));
    len = strlen(MyWiFiConfig.HostName);
    MyWiFiConfig.HostName[len+1] = '\0';
  }
  if (MyWiFiConfig.StaticIP > 0) {
    MyWiFiConfig.StaticIP = 4; // Set to invalid IP-config
    // Static IP needs at least IP; GW and SUBNET 
    if (_ip != "" && isIp(_ip) && _gw != "" && isIp(_gw) && _sn != "" && isIp(_sn)){
      MyWiFiConfig.IPAdd.fromString(_ip);
      Serial.println("IP: " + toStringIp(MyWiFiConfig.IPAdd));
      MyWiFiConfig.Gate.fromString(_gw);
      Serial.println("GW: " + toStringIp(MyWiFiConfig.Gate));
      MyWiFiConfig.SubNet.fromString(_sn);
      Serial.println("SN: " + toStringIp(MyWiFiConfig.SubNet));
      MyWiFiConfig.StaticIP = 2;
    }
    if (_dns != "" && isIp(_dns) && MyWiFiConfig.StaticIP == 2) {
      MyWiFiConfig.DNS.fromString(_dns);
      Serial.println("DNS: " + toStringIp(MyWiFiConfig.DNS));
      MyWiFiConfig.StaticIP = 3;
    }
  }
  else{
    MyWiFiConfig.IPAdd = EmptyIP;
    MyWiFiConfig.Gate = EmptyIP;
    MyWiFiConfig.SubNet = EmptyIP;
    MyWiFiConfig.DNS = EmptyIP;
  }
  if (MyWiFiConfig.StaticIP < 4){
    ret_val = saveCredentials();
  }
  switch (ret_val){
    case 0:
      page = "IP config invalid!";
      break;
    case 1: 
      page = "Success! Rebooting in 2s";
      break;
    case 2:
      page = "The Password needs at least 8 Characters";
      break;
    case 3:
      page = "AP name is invalid";
      break;
    case 4:
      page = "EEPROM error";
      break;
  }
  server.sendHeader("Content-Length", String(page.length()));
  server.send(200, "text/html", page);
  if (ret_val == 1){
    delay(2000);
    ESP.restart();
  }
}

void InitalizeHTTPServer() {
  // Setup web pages: root, wifi config pages, SO captive portal detectors and not found.
  server.on("/", handleRoot);
  server.on("/wifi", handleWifi1);
  server.on("/0wifi", handleWifi0);
  server.on("/wifisave", handleWifiSave);
  server.on("/reset", handleReset);
  server.on("/update", handleUpdate);
  server.on("/upgrade", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
    }, []() {
      HTTPUpload& upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) {
        Serial.setDebugOutput(true);
        Serial.printf("Update: %s\n", upload.filename.c_str());
        if (!Update.begin()) { //start with max available size
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) { //true to set the size to the current progress
          Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
        } else {
          Update.printError(Serial);
        }
        Serial.setDebugOutput(false);
      } else {
        Serial.printf("Update Failed Unexpectedly (likely broken connection): status=%d\n", upload.status);
      }
    });

  if (MyWiFiConfig.CapPortal) { server.on("/generate_204", handleCP); } //Android captive portal. Maybe not needed. Might be handled by notFound handler.
  if (MyWiFiConfig.CapPortal) { server.on("/favicon.ico", handleCP); }   //Another Android captive portal. Maybe not needed. Might be handled by notFound handler. Checked on Sony Handy
  if (MyWiFiConfig.CapPortal) { server.on("/fwlink", handleCP); }  //Microsoft captive portal. Maybe not needed. Might be handled by notFound handler.
  server.onNotFound ( handleNotFound );
  server.begin(); // Web server start
}

boolean CreateWifiSoftAP() {
  WiFi.disconnect();
  Serial.print(F("Initalize SoftAP "));
  if (MyWiFiConfig.PwDReq) {
      SoftAccOK  =  WiFi.softAP(MyWiFiConfig.APSTAName, MyWiFiConfig.WiFiPwd); // Passwordlength at least 8 char
    } 
    else {
      SoftAccOK  =  WiFi.softAP(MyWiFiConfig.APSTAName); // Access Point WITHOUT Password
    }
  delay(2000); // Delay to set the correct IP address
  WiFi.softAPConfig(CPapIP, CPapIP, CPnetMsk);
  if (SoftAccOK) {
  /* Setup the DNS server redirecting all the domains to the CPapIP */  
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", CPapIP);
  Serial.println(F("successful."));
  } 
  else {
  Serial.println(F("Soft AP Error."));
  Serial.println(MyWiFiConfig.APSTAName);
  Serial.println(MyWiFiConfig.WiFiPwd);
  }
  return SoftAccOK;
}

byte ConnectWifiAP() {
  Serial.println(F("Initalizing Wifi Client."));  
  byte connRes = 0;
  byte retry = 0;
  WiFi.disconnect();
  WiFi.softAPdisconnect(true); // Function will set currently configured SSID and password of the soft-AP to null values. The parameter  is optional. If set to true it will switch the soft-AP mode off.
  delay(500);  
  switch (MyWiFiConfig.StaticIP) {
    case 2:
      WiFi.config(MyWiFiConfig.IPAdd, MyWiFiConfig.Gate, MyWiFiConfig.SubNet);
      break;
    case 3:
      WiFi.config(MyWiFiConfig.IPAdd, MyWiFiConfig.Gate, MyWiFiConfig.SubNet, MyWiFiConfig.DNS);
      break;
    default:
      break;
  }
  WiFi.begin(MyWiFiConfig.APSTAName, MyWiFiConfig.WiFiPwd);
  connRes  = WiFi.waitForConnectResult();
  while (( connRes == 0 ) and (retry != 10))  //if connRes == 0  "IDLE_STATUS - change Status"
    { 
      connRes  = WiFi.waitForConnectResult();
      delay(2000);
      retry++;
      Serial.print(F("."));
      // statement(s)
    }
  while (( connRes == 1 ) and (retry != 10))  //if connRes == 1  NO_SSID_AVAILin - SSID cannot be reached
    { 
      connRes  = WiFi.waitForConnectResult();
      delay(2000);
      retry++;
      Serial.print(F("."));
    }
  if (connRes == 3 ) { 
    WiFi.setAutoReconnect(true); // Set whether module will attempt to reconnect to an access point in case it is disconnected.
    // Setup MDNS responder
    if (!MDNS.begin(MyWiFiConfig.HostName)) {
      Serial.println(F("Error: MDNS"));
    } 
    else {
      MDNS.addService("http", "tcp", 80); 
    }
  }
  while (( connRes == 4 ) and (retry != 10))  //if connRes == 4  Bad Password. Sometimes happens this with corrct PWD
    { 
      WiFi.begin(MyWiFiConfig.APSTAName, MyWiFiConfig.WiFiPwd); 
      connRes = WiFi.waitForConnectResult();
      delay(3000);
      retry++;
      Serial.print(F("."));                 
    }
  if (connRes == 4 ) {  
    Serial.println(F("STA Pwd Err"));                     
    Serial.println(MyWiFiConfig.APSTAName);
    Serial.println(MyWiFiConfig.WiFiPwd); 
    WiFi.disconnect();
  }
  Serial.println("");
  return connRes;
}

// Load WLAN credentials from EEPROM
bool loadCredentials() {
  bool RetValue;
  EEPROM.begin(512);
  EEPROM.get(0, MyWiFiConfig);
  EEPROM.end();
  if (String(MyWiFiConfig.ConfigValid) == String("TK")) {
    RetValue = true;
  } 
  else {
    RetValue = false; // WLAN Settings not found.
  }
  return RetValue;
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //disable brownout detector
  bool CPConnectSuccess = false;
  bool CPCreateSoftAPSucc  = false;
  byte len; 
  Serial.begin(115200);
   while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB
  }
  Serial.println(F("Serial Interface initalized at 115200 Baud. v0.2")); 
  WiFi.setAutoReconnect (false);
  WiFi.persistent(false);
  WiFi.disconnect(); 
  WiFi.setHostname(MyWiFiConfig.HostName); // Set the DHCP hostname assigned to ESP station.
  if (loadCredentials()) // Load WLAN credentials for WiFi Settings
  { 
     Serial.println(F("Valid Credentials found."));   
     if (MyWiFiConfig.APSTA == true)  // AP Mode
      { 
        Serial.println(F("Access Point Mode selected.")); 
        Serial.println(MyWiFiConfig.APSTA);
        len = strlen(MyWiFiConfig.APSTAName);
        MyWiFiConfig.APSTAName[len+1] = '\0'; 
        len = strlen(MyWiFiConfig.WiFiPwd);
        MyWiFiConfig.WiFiPwd[len+1] = '\0';  
        CPCreateSoftAPSucc = CreateWifiSoftAP();
      } else
      {
        Serial.println(F("Station Mode selected."));       
        len = strlen(MyWiFiConfig.APSTAName);
        MyWiFiConfig.APSTAName[len+1] = '\0'; 
        len = strlen(MyWiFiConfig.WiFiPwd);
        MyWiFiConfig.WiFiPwd[len+1] = '\0';
        len = ConnectWifiAP();     
        if ( len == 3 ) { CPConnectSuccess = true; } else { CPConnectSuccess = false; }     
      }
  } else
  { //Set default Config - Create AP
     Serial.println(F("NO Valid Credentials found.")); 
     handleReset();  
  }
  if ((CPConnectSuccess or CPCreateSoftAPSucc))
    {         
      Serial.print (F("IP Address: "));
      if (CPCreateSoftAPSucc) { Serial.println(WiFi.softAPIP());}   
      if (CPConnectSuccess) { Serial.println(WiFi.localIP());}
      InitalizeHTTPServer();     
    }
    else
    {
      Serial.setDebugOutput(true); //Debug Output for WLAN on Serial Interface.
      Serial.println(F("Error: Cannot connect to WLAN. Set DEFAULT Configuration."));
      handleReset(); 
    } 
}

void loop() {  
  if (SoftAccOK)
  {
    dnsServer.processNextRequest(); //DNS
  }
  //HTTP
  server.handleClient();  
}
