// Podglady ekranow CYD: prawdziwy main.cpp rysuje do bufora 320x240, teksty zapisywane osobno, tools/cyd/render_ui.py sklada PNG.
#include <cstdlib>
#include <vector>
#include <deque>
#include <assert.h>
#include <stdarg.h>
#include <string>
#include <algorithm>
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
static std::vector<uint16_t> lastRows[240];
#include <vector>
#include <deque>
#include <assert.h>
#include <stdarg.h>
static unsigned long gNow=0;static uint32_t gRand=0x1234;static std::deque<uint8_t> gRx;static std::vector<uint8_t> gTx;
long map(long x,long a,long b,long c,long d){return (x-a)*(d-c)/(b-a)+c;}
unsigned long millis(){return gNow;}void delay(unsigned long){}void delayMicroseconds(unsigned){}uint32_t esp_random(){return gRand++;}
void pinMode(int,int){}void digitalWrite(int,int){}int digitalRead(int){return 0;}
size_t strlcpy(char*d,const char*s,size_t n){size_t l=strlen(s);if(n){size_t k=l<n-1?l:n-1;memcpy(d,s,k);d[k]=0;}return l;}
SimWifi gWifi;WiFiClass WiFi;
int simAvail(){return gRx.size();}int simRead(){int v=gRx.front();gRx.pop_front();return v;}
size_t simWrite(const uint8_t*p,size_t n){gTx.insert(gTx.end(),p,p+n);return n;}
void SerialC::begin(unsigned){}int SerialC::available(){return 0;}int SerialC::read(){return 0;}int SerialC::printf(const char*,...){return 0;}SerialC Serial;
void SPIC::begin(int,int,int,int){}SPIC SPI;

std::vector<std::string> sdFiles;std::string sdData;bool sdPresent=true;
bool SDC::begin(int,SPIC&,unsigned){return sdPresent;}unsigned char SDC::cardType(){return sdPresent?1:0;}
bool SDC::exists(const char* p){for(auto&f:sdFiles)if(f==p)return true;return false;}
File SDC::open(const char* p,int){if(std::string(p).size()>14&&std::getenv("SD_NOLFN"))return File();sdFiles.push_back(p);File f;f.ok=true;return f;}
int File::printf(const char* fmt,...){char b[512];va_list ap;va_start(ap,fmt);int n=vsnprintf(b,sizeof b,fmt,ap);va_end(ap);sdData+=b;return n;}void File::println(const char* t){sdData+=t;sdData+="\n";}
SDC SD;
uint32_t Preferences::getULong(const char*,uint32_t d){return d;}void Preferences::putULong(const char*,uint32_t){}
void Preferences::begin(const char*,bool){}uint16_t Preferences::getUShort(const char*,uint16_t d){return d;}bool Preferences::getBool(const char*,bool){return true;}void Preferences::putUShort(const char*,uint16_t){}void Preferences::putBool(const char*,bool){}

// ---- TFT rysujacy do bufora (RGB565) ----
static uint16_t FB[240][320];static uint32_t tFg=0xFFFF,tBg=0;static uint8_t tDat=0;
struct TOp{int x,y,font,datum;uint16_t fg,bg;std::string s;};static std::vector<TOp> gTexts;
static inline void P(int x,int y,uint32_t c){if(x>=0&&y>=0&&x<320&&y<240)FB[y][x]=uint16_t(c);}
void TFT_eSPI::invertDisplay(bool){}void TFT_eSPI::init(){}void TFT_eSPI::setRotation(int){}
void TFT_eSPI::fillScreen(uint32_t c){for(auto& r:FB)for(auto& p:r)p=uint16_t(c);gTexts.clear();}
void TFT_eSPI::setTextColor(uint32_t f,uint32_t b){tFg=f;tBg=b;}void TFT_eSPI::setTextDatum(uint8_t d){tDat=d;}
static int cw(char c,int f){if(f==1)return 6;if(f==4)return (c=='1')?9:(c=='.'||c==':')?6:(c=='-')?9:14;return (c=='W'||c=='M')?12:(c=='i'||c=='l'||c=='.'||c==':'||c=='!'||c=='I')?4:(c==' ')?4:8;}
int TFT_eSPI::textWidth(const char*s,int f){int w=0;for(;*s;s++)w+=cw(*s,f);return w;}
static int fh(int f){return f==1?8:f==2?16:26;}
int TFT_eSPI::drawString(const char* s,int x,int y,int f){int w=textWidth(s,f),h=fh(f);int col=tDat%3,row=tDat/3;int x0=col==0?x:col==1?x-w/2:x-w,y0=row==0?y:row==1?y-h/2:y-h;
 // tlo tekstu (jak TFT_eSPI z kolorem tla), napis rysuje render_ui.py
 for(int j=0;j<h;j++)for(int i=0;i<w;i++)P(x0+i,y0+j,tBg);
 for(auto it=gTexts.begin();it!=gTexts.end();){if(it->x<x0+w&&x0<it->x+textWidth(it->s.c_str(),it->font)&&it->y<y0+h&&y0<it->y+fh(it->font))it=gTexts.erase(it);else ++it;}
 gTexts.push_back(TOp{x0,y0,f,0,uint16_t(tFg),uint16_t(tBg),s});return w;}
int TFT_eSPI::drawString(const String& s,int x,int y,int f){return drawString(s.c_str(),x,y,f);}
static void clearTextsIn(int x,int y,int w,int h){for(auto it=gTexts.begin();it!=gTexts.end();){int tw=int(it->s.size())*cw('a',it->font);if(it->x<x+w&&x<it->x+tw&&it->y<y+h&&y<it->y+fh(it->font)&&w*h>=40)it=gTexts.erase(it);else ++it;}}
void TFT_eSPI::drawPixel(int x,int y,uint32_t c){P(x,y,c);}
void TFT_eSPI::fillRect(int x,int y,int w,int h,uint32_t c){clearTextsIn(x,y,w,h);for(int j=0;j<h;j++)for(int i=0;i<w;i++)P(x+i,y+j,c);}
void TFT_eSPI::drawFastHLine(int x,int y,int w,uint32_t c){for(int i=0;i<w;i++)P(x+i,y,c);}
void TFT_eSPI::drawFastVLine(int x,int y,int h,uint32_t c){for(int j=0;j<h;j++)P(x,y+j,c);}
void TFT_eSPI::drawRect(int x,int y,int w,int h,uint32_t c){drawFastHLine(x,y,w,c);drawFastHLine(x,y+h-1,w,c);drawFastVLine(x,y,h,c);drawFastVLine(x+w-1,y,h,c);}
void TFT_eSPI::drawLine(int x0,int y0,int x1,int y1,uint32_t c){int dx=abs(x1-x0),sx=x0<x1?1:-1,dy=-abs(y1-y0),sy=y0<y1?1:-1,e=dx+dy;for(;;){P(x0,y0,c);if(x0==x1&&y0==y1)break;int e2=2*e;if(e2>=dy){e+=dy;x0+=sx;}if(e2<=dx){e+=dx;y0+=sy;}}}
void TFT_eSPI::fillCircle(int x,int y,int r,uint32_t c){for(int j=-r;j<=r;j++)for(int i=-r;i<=r;i++)if(i*i+j*j<=r*r+r)P(x+i,y+j,c);}
void TFT_eSPI::drawCircle(int x,int y,int r,uint32_t c){for(int a=0;a<360*4;a++){double t=a*M_PI/720;P(x+int(lround(r*cos(t))),y+int(lround(r*sin(t))),c);}}
static bool inRR(int i,int j,int w,int h,int r){int cx=i<r?r:i>=w-r?w-r-1:i,cy=j<r?r:j>=h-r?h-r-1:j;int dx=i-cx,dy=j-cy;return dx*dx+dy*dy<=r*r;}
void TFT_eSPI::fillRoundRect(int x,int y,int w,int h,int r,uint32_t c){clearTextsIn(x,y,w,h);for(int j=0;j<h;j++)for(int i=0;i<w;i++)if(inRR(i,j,w,h,r))P(x+i,y+j,c);}
void TFT_eSPI::drawRoundRect(int x,int y,int w,int h,int r,uint32_t c){for(int j=0;j<h;j++)for(int i=0;i<w;i++)if(inRR(i,j,w,h,r)&&!(inRR(i-1,j,w,h,r)&&inRR(i+1,j,w,h,r)&&inRR(i,j-1,w,h,r)&&inRR(i,j+1,w,h,r)))P(x+i,y+j,c);}
void TFT_eSPI::fillTriangle(int x0,int y0,int x1,int y1,int x2,int y2,uint32_t c){int mnx=std::min({x0,x1,x2}),mxx=std::max({x0,x1,x2}),mny=std::min({y0,y1,y2}),mxy=std::max({y0,y1,y2});
 auto E=[](int ax,int ay,int bx,int by,int px,int py){return (bx-ax)*(py-ay)-(by-ay)*(px-ax);};for(int y=mny;y<=mxy;y++)for(int x=mnx;x<=mxx;x++){int a=E(x0,y0,x1,y1,x,y),b=E(x1,y1,x2,y2,x,y),d=E(x2,y2,x0,y0,x,y);if((a>=0&&b>=0&&d>=0)||(a<=0&&b<=0&&d<=0))P(x,y,c);}}
// drawArc jak w TFT_eSPI: kat 0 = godzina 6, rosnie zgodnie z ruchem wskazowek
void TFT_eSPI::drawArc(int32_t x,int32_t y,int32_t r,int32_t ir,uint32_t a0,uint32_t a1,uint32_t c,uint32_t,bool){for(int j=-r;j<=r;j++)for(int i=-r;i<=r;i++){int d2=i*i+j*j;if(d2>r*r||d2<ir*ir)continue;
 double a=atan2(double(-i),double(j))*180/M_PI;if(a<0)a+=360;if(a>=a0&&a<=a1)P(x+i,y+j,c);}}
static bool gSwap=false;void TFT_eSPI::setSwapBytes(bool s){gSwap=s;}
void TFT_eSPI::pushImage(int x,int y,int w,int h,uint16_t* d){for(int j=0;j<h;j++)for(int i=0;i<w;i++)P(x+i,y+j,d[j*w+i]);}
#include "../../src/cyd/main.cpp"
#include "../../include/e213/api.h"
// ---- strumien od "E213" zbudowany prawdziwym koderem serwera E213 ----
using Bytes=std::vector<uint8_t>;
static uint32_t gFid=1;
static void sendFrom(const uint8_t* p,size_t n){gRx.push_back(0x94);gRx.push_back(0xc3);gRx.push_back(uint8_t(n>>8));gRx.push_back(uint8_t(n));for(size_t i=0;i<n;i++)gRx.push_back(p[i]);}
static void run(unsigned long t){gNow=t;loop();}
static void step(unsigned ms){unsigned long e=gNow+ms;while(gNow<e){gNow+=std::min<unsigned long>(50,e-gNow);loop();}}
static void dump(const char* name){char p[96];snprintf(p,sizeof p,"shots_cyd/%s.bin",name);FILE* f=fopen(p,"wb");fwrite(FB,sizeof FB,1,f);fclose(f);
 snprintf(p,sizeof p,"shots_cyd/%s.txt",name);f=fopen(p,"w");for(auto& t:gTexts){fprintf(f,"%d %d %d %u %u|%s\n",t.x,t.y,t.font,t.fg,t.bg,t.s.c_str());}fclose(f);}
static void shot(const char* name){uiScreen=-1;draw();dump(name);}
struct Nd{uint32_t id;const char* name;const char* sh;double lat,lon;int rssi;float snr;int hops;};
static const Nd NODES[]={{0x45126738,"Mundek Bemowo","MUN",52.2393,20.9132,-86,6.5f,0},{0x1a2b3c4d,"SP5JAN mobile","SP5J",52.2181,21.0312,-101,1.75f,0},{0x0badcafe,"Pruszkow RPT","PRSZ",52.1650,20.8020,-112,-6.5f,1},
 {0x22334455,"Zabki dom","ZAB",52.2930,21.1100,-118,-11.25f,2},{0x99887766,"Kampinos Tower","KAMP",52.3200,20.6500,-109,-3.0f,1},{0x31415926,"Wawer","WAW",52.1970,21.1690,-95,4.25f,0},
 {0x27182818,"Legionowo","LEG",52.4010,20.9260,-121,-14.5f,3},{0x16180339,"Otwock","OTW",52.1050,21.2610,-115,-9.0f,2}};
static void pkt(uint32_t from,unsigned port,const Bytes& payload,int rssi,float snr,int hops,uint32_t to=0xFFFFFFFF,uint32_t req=0){
 Bytes d;api::Buf b(nullptr,0);uint8_t db[260];api::Buf dd(db,sizeof db);dd.u(1,port);dd.bytes(2,payload.data(),payload.size());if(req)dd.fx(6,req);
 api::PktDesc p;p.from=from;p.to=to;p.id=gRand++;p.hopStart=3;p.hopLimit=unsigned(3-hops);p.snr=snr;p.rssi=rssi;p.rxTime=1761404700+gNow/1000;p.decoded=db;p.decodedLen=dd.n;
 uint8_t o[400];size_t n=api::packet(o,sizeof o,gFid++,p);sendFrom(o,n);}
int main(){
 system("mkdir -p shots_cyd");
 // ekran startowy
 for(auto& r:FB)for(auto& p:r)p=0;splash();dump("00_splash");
 // brak WiFi E213, potem WiFi bez API
 gWifi.assoc=false;setup();step(200);shot("01_wait_nowifi");
 gWifi.assoc=true;gWifi.serverUp=false;step(4000);shot("02_wait_noapi");
 gWifi.serverUp=true;step(3500);assert(radio.up);shot("03_wait_tcp");
 // konfiguracja od E213 (my_info, NodeInfo, lora, kanal, complete)
 const uint32_t ME=0x9e3779b1;uint8_t o[400];size_t n;
 n=api::myInfo(o,sizeof o,gFid++,ME);sendFrom(o,n);
 {api::NodeDesc me;me.num=ME;me.longName="POLE-1";me.shortName="POLE";me.hw=67;me.hasPos=true;me.lat=522297000;me.lon=210122000;me.alt=110;me.hasMetrics=true;me.battery=78;me.voltage=3.94f;me.chUtil=7.5f;me.airTx=0.4f;me.uptime=5400;n=api::nodeInfo(o,sizeof o,gFid++,me);sendFrom(o,n);}
 for(auto& x:NODES){api::NodeDesc d;d.num=x.id;d.longName=x.name;d.shortName=x.sh;d.hw=0;d.hasPos=true;d.lat=int32_t(lround(x.lat*1e7));d.lon=int32_t(lround(x.lon*1e7));d.snr=x.snr;d.hops=x.hops;n=api::nodeInfo(o,sizeof o,gFid++,d);sendFrom(o,n);}
 n=api::loraConfig(o,sizeof o,gFid++,0,11,250,5,869.525f,3,14);sendFrom(o,n);
 n=api::channel(o,sizeof o,gFid++,0,"LongFast",true);sendFrom(o,n);
 n=api::configComplete(o,sizeof o,gFid++,configId);sendFrom(o,n);
 step(500);assert(configured&&myId==ME);
 // ruch w sieci: pakiety z RSSI/SNR, wiadomosci, telemetria E213
 for(int round=0;round<6;round++){for(auto& x:NODES){Bytes pl={'p'};pkt(x.id,67,Bytes{0x12,0x02,0x08,0x50},x.rssi+((round*7+int(x.id))%9)-4,x.snr+float((round*3+int(x.id%5))%5)-2.f,x.hops);step(900);}}
 auto txt=[](const char* s){return Bytes(s,s+strlen(s));};
 pkt(0x1a2b3c4d,1,txt("Test zasiegu z Mokotowa, slychac?"),-101,1.5f,0);step(300);pkt(0x45126738,1,txt("Mundek: -86 dBm na Bemowie, git"),-86,6.f,0);step(300);
 pkt(0x99887766,1,txt("Kampinos: przekaznik dziala"),-109,-3.f,1);step(300);
 {uint8_t d[64];size_t dn=api::dataTelemetry(d,sizeof d,1761404700,78,3.94f,7.5f,0.4f,5460);api::PktDesc p;p.from=ME;p.to=0xFFFFFFFF;p.id=777;p.hasRx=false;p.decoded=d;p.decodedLen=dn;n=api::packet(o,sizeof o,gFid++,p);sendFrom(o,n);}
 step(2000);
 home=true;shot("10_home");
 home=false;page=0;shot("11_nodes");
 // szczegoly wezla z testem lacza (ACK przez E213)
 selected=-1;for(unsigned i=0;i<MAX_NODES;i++)if(nodes[i].id==0x1a2b3c4d)selected=int(i);assert(selected>=0);detail=true;probeStart(nodes[selected].id);
 for(int k=0;k<5;k++){step(200);auto tx=gTx;gTx.clear();
  // znajdz id wyslanego testu i odeslij ACK celu
  if(probe.pending){pkt(0x1a2b3c4d,5,Bytes{0x18,0x00},-100+k,1.0f+k*0.5f,0,ME,probe.id);}step(15500);}
 shot("12_detail_probe");
 detail=false;mapScr=true;home=false;shot("13_map");mapScr=false;
 specScr=true;shot("14_spectrum");specScr=false;
 openChat(0);shot("15_chat");leaveSub();
 fprintf(stderr,"nodes=%u probe ok=%lu lost=%lu\n",orderCount,(unsigned long)probe.ok,(unsigned long)probe.lost);
 puts("ui OK");return 0;}
