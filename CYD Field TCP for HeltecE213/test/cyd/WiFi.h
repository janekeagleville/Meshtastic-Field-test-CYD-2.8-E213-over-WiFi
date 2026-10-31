#pragma once
// WiFi na hoscie dla CYD: stan sieci i polaczenia TCP sterowany z testu; strumien bajtow to gRx / gTx z probe_sim.
#include "Arduino.h"
#define WIFI_STA 1
#define WL_CONNECTED 3
#define WL_DISCONNECTED 6
struct IPAddress{uint8_t b[4]={0,0,0,0};IPAddress(){}IPAddress(uint8_t a,uint8_t c,uint8_t d,uint8_t e){b[0]=a;b[1]=c;b[2]=d;b[3]=e;}uint8_t operator[](int i)const{return b[i];}};
struct SimWifi{bool assoc=true,serverUp=true,tcp=false;int begins=0,connects=0;IPAddress lastIp;unsigned lastPort=0;std::string ssid,pass;};
extern SimWifi gWifi;
struct WiFiClass{void mode(int){}void setAutoReconnect(bool){}void begin(const char* s,const char* p){gWifi.ssid=s;gWifi.pass=p;++gWifi.begins;}void disconnect(){}
 int status(){return gWifi.assoc?WL_CONNECTED:WL_DISCONNECTED;}int RSSI(){return -48;}};
extern WiFiClass WiFi;
int simAvail();int simRead();size_t simWrite(const uint8_t*,size_t);
struct WiFiClient{int connect(IPAddress ip,unsigned port,int){++gWifi.connects;gWifi.lastIp=ip;gWifi.lastPort=port;gWifi.tcp=gWifi.assoc&&gWifi.serverUp;return gWifi.tcp;}
 bool connected(){return gWifi.tcp&&gWifi.assoc;}void stop(){gWifi.tcp=false;}int setNoDelay(bool){return 0;}
 int available(){return connected()?simAvail():0;}int read(){return simRead();}size_t write(const uint8_t* p,size_t n){return connected()?simWrite(p,n):0;}};
