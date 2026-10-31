#pragma once
// Polaczenie CYD z E213 przez WiFi: CYD jest stacja w sieci punktu dostepowego E213 i klientem TCP API (192.168.4.1:4403).
// Interfejs jak port szeregowy (available / read / write), wiec reszta firmware (parser ramek, test lacza, log) jest bez zmian.
#include <WiFi.h>
#include "link.h"
struct TcpLink{
 enum St{ST_WIFI,ST_TCP,ST_UP};
 // tcpTry startuje "dawno temu", wiec pierwsza proba polaczenia TCP jest od razu
 WiFiClient c;St st=ST_WIFI;bool up=false;uint32_t wifiTry=0,tcpTry=uint32_t(-60000),upAt=0;unsigned connects=0,drops=0;int rssi=0;
 void begin(){WiFi.mode(WIFI_STA);WiFi.setAutoReconnect(true);WiFi.begin(LINK_SSID,LINK_PASS);wifiTry=millis();}
 // Wywolywane w petli. Zwraca true, gdy wlasnie powstalo nowe polaczenie TCP (wtedy trzeba wyslac want_config_id).
 bool tick(uint32_t now){
  const bool w=WiFi.status()==WL_CONNECTED;
  if(up&&(!w||!c.connected())){c.stop();up=false;++drops;}
  if(!w){st=ST_WIFI;rssi=0;if(now-wifiTry>15000){wifiTry=now;WiFi.disconnect();WiFi.begin(LINK_SSID,LINK_PASS);}return false;}
  rssi=WiFi.RSSI();
  if(up){st=ST_UP;return false;}
  st=ST_TCP;if(now-tcpTry<3000)return false;tcpTry=now;
  if(c.connect(IPAddress(LINK_IP[0],LINK_IP[1],LINK_IP[2],LINK_IP[3]),LINK_PORT,2000)){c.setNoDelay(true);up=true;upAt=now;++connects;st=ST_UP;return true;}
  return false;}
 int available(){return up?c.available():0;}
 int read(){return c.read();}
 size_t write(const uint8_t* p,size_t n){return up?c.write(p,n):0;}
 // Krotki opis stanu do ekranu
 const char* state()const{return st==ST_UP?"E213 linked":st==ST_TCP?"WiFi ok, no E213 API":"searching WiFi";}
};
