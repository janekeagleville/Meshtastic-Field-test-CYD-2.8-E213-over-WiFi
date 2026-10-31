#pragma once
// WiFi na hoscie: punkt dostepowy i jedno wirtualne polaczenie TCP sterowane z testu (gNet).
#include "Arduino.h"
#include <deque>
#include <vector>
#include <string>
#define WIFI_OFF 0
#define WIFI_STA 1
#define WIFI_AP 2
struct IPAddress{uint8_t b[4]={0,0,0,0};IPAddress(){}IPAddress(uint8_t a,uint8_t c,uint8_t d,uint8_t e){b[0]=a;b[1]=c;b[2]=d;b[3]=e;}uint8_t operator[](int i)const{return b[i];}};
struct SimNet{int mode=0;bool apUp=false,srvUp=false;std::string ssid,pass;int channel=0,maxConn=0;
 bool pending=false,connected=false;int connId=0,accepts=0;std::deque<uint8_t> toServer;std::vector<uint8_t> fromServer;IPAddress peer{192,168,4,2};};
extern SimNet gNet;
class WiFiClient{int id=0;
public:WiFiClient(){}explicit WiFiClient(int i):id(i){}
 operator bool(){return connected();}   // jak w Arduino ESP32: prawda tylko dla polaczonego klienta
 bool connected(){return id&&gNet.connected&&id==gNet.connId;}
 int available(){return connected()?int(gNet.toServer.size()):0;}
 int read(){if(!connected()||gNet.toServer.empty())return -1;uint8_t v=gNet.toServer.front();gNet.toServer.pop_front();return v;}
 size_t write(const uint8_t* p,size_t n){if(!connected())return 0;gNet.fromServer.insert(gNet.fromServer.end(),p,p+n);return n;}
 void stop(){if(connected())gNet.connected=false;id=0;}
 int setNoDelay(bool){return 0;}
 IPAddress remoteIP(){return gNet.peer;}
};
class WiFiServer{unsigned port;
public:explicit WiFiServer(unsigned p):port(p){}
 void begin(){gNet.srvUp=true;}void end(){gNet.srvUp=false;}void setNoDelay(bool){}
 WiFiClient available(){if(gNet.srvUp&&gNet.pending){gNet.pending=false;gNet.connected=true;++gNet.connId;++gNet.accepts;gNet.toServer.clear();return WiFiClient(gNet.connId);}return WiFiClient();}
};
struct WiFiClass{
 bool mode(int m){gNet.mode=m;return true;}
 bool softAP(const char* s,const char* p,int ch=1,int=0,int mc=4){gNet.ssid=s;gNet.pass=p;gNet.channel=ch;gNet.maxConn=mc;gNet.apUp=true;return true;}
 bool softAPdisconnect(bool){gNet.apUp=false;gNet.connected=false;return true;}
};
extern WiFiClass WiFi;
