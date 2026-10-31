// LoRa Field Test na Heltec Vision Master E213 (ESP32-S3 + SX1262 + e-ink 2,13" 250x122, dwa przyciski, bez dotyku).
// Samodzielny tester z wlasnym radiem (NIE jest to firmware Meshtastic):
//   - SZUM: prawdziwy pomiar chwilowego RSSI radia (10x/s), podloga, szczyty, zajetosc kanalu, okna 1 min / 10 min / 1 h
//   - SKAN: przemiatanie pasma 863-870 MHz z zapamietaniem szczytow
//   - MESH: pasywny podsluch pakietow Meshtastic (naglowki jawne; nazwy wezlow tylko dla domyslnego klucza kanalu)
#include <Arduino.h>
#include <SPI.h>
#include <Preferences.h>
#include <RadioLib.h>
#include <time.h>
#include <sys/time.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <WiFi.h>
// Biblioteki uzywane w naglowkach include/e213 (epd_gfx.h, epd_hw.h): wymienione tutaj, zeby PlatformIO je wykryl przy wspolnym projekcie
#ifndef HOST_SIM
#include <Adafruit_GFX.h>
#include <GxEPD2_BW.h>
#endif
#include "config.h"
#include "epd_gfx.h"
#include "epd_hw.h"
#include "presets.h"
#include "noiselog.h"
#include "quality.h"
#include "meshhdr.h"
#include "link.h"
#include "api.h"

// ================= Stan globalny =================
Canvas spr(SCR_W,SCR_H);   // plotno 1-bitowe, wysylane do e-ink przez epdhw
SPIClass loraSpi(FSPI);
SX1262 radio=new Module(PIN_LORA_CS,PIN_LORA_DIO1,PIN_LORA_RST,PIN_LORA_BUSY,loraSpi);
Preferences prefs;

struct Cfg{uint8_t power=2,meshPreset=3,invert=1,flip=0,pwrHigh=0,beacon=2,autoPing=1,ble=0,wifi=1;} cfg;   // beacon: indeks odstepu (0 = wylaczony), autoPing: ping wybranego wezla co 10 s, wifi: lacze z CYD (punkt dostepowy + API TCP)
// Bluetooth (konfigurator przez Web Bluetooth): definicje ponizej, przed obsluga przyciskow
uint32_t blePin=0;volatile bool bleCfgDirty=true;void bleSet(bool on);
// Lacze WiFi z CYD: definicje ponizej (sekcja "Lacze WiFi / TCP z CYD")
void wifiSet(bool on);void apiForward(const uint8_t* b,size_t n,float rssi,float snr);
enum Screen{S_HOME,S_NOISE,S_SCAN,S_MESH,S_NODE,S_SET,S_COUNT};
enum Mode{M_MESH,M_SCAN};
const char* SCR_NAME[S_COUNT]={"HOME","NOISE","SCAN","MESH","NODE","SETUP"};
int screen=S_HOME;Mode mode=M_MESH;bool dirty=true;uint32_t lastDraw=0;

struct Prof{float mhz,bw;uint8_t sf,cr,sync;uint16_t pre;int8_t pwr;};
Prof prof={869.525f,250,11,5,MESH_SYNC,MESH_PREAMBLE,14};
bool radioOk=false;int16_t rlErr=0;

pre::Duty duty;bool dutyBlocked=false;
nz::Log nzMesh;
mh::Table mtab;mh::Air meshAir;uint32_t meshRx=0,crcErrors=0;
bool txBusy=false;uint32_t txDeadline=0;
uint32_t lastNoiseAt=0,lastBatAt=0;float batV=0;int batPct=-1;bool batChg=false;float batPrev=0;
constexpr int BAT_OFF_PCT=5;constexpr uint8_t BAT_OFF_READS=3;   // wylaczenie: ponizej 5% przez 3 kolejne odczyty (ok. 30 s), bez ladowania
uint8_t batLowCnt=0;RTC_DATA_ATTR bool rtcLowOff=false;RTC_DATA_ATTR float rtcMinV=9.f;
unsigned noiseWin=0;                       // 0 = 1 min, 1 = 10 min, 2 = 1 h
char nodeName[13]="";   // nazwa tego wezla (wpisywana przez BLE), max 12 znakow ASCII
struct Pos{int32_t lat=0,lon=0,alt=0;bool has=false;} gpos;   // wpisana recznie pozycja GPS (1e-7 stopnia)
uint32_t nextBeaconAt=0;unsigned beaconsSent=0;bool beaconPosDue=false;uint32_t beaconPosAt=0;
bool meshPicked=false;uint32_t lastPingTry=0;constexpr uint32_t AUTO_PING_MS=10000;
unsigned meshOff=0,setSel=0;uint32_t meshSelId=0,myId=0;bool scanPaused=false;
int8_t scanCur[SCAN_N],scanPeak[SCAN_N];unsigned scanIdx=0;

// Ping wezla Meshtastic (pakiet z want_ack, odpowiedz ROUTING_APP)
enum PingState{PG_IDLE,PG_WAIT,PG_ACK,PG_NAK,PG_TIMEOUT,PG_BLOCKED};
struct Ping{uint32_t target=0,id=0,sentAt=0;PingState st=PG_IDLE;unsigned sent=0,acks=0;int rssi=0,hops=-1;float snr=0;uint32_t rtt=0;uint8_t err=0;} pingSt;
constexpr uint32_t PING_TIMEOUT_MS=12000;
nz::Log& nzCur(){return nzMesh;}
int lastRssi=0;float lastSnr=0;uint32_t lastRxAt=0;bool hasRx=false;   // ostatni odebrany pakiet

// ================= Kolory (e-ink: papier / tusz / polton) =================
const uint16_t cBG=epd::PAPER,cHDR=epd::PAPER,cPANEL=epd::PAPER,cLINE=epd::INK,cBAND=epd::GREY;
// ================= E-ink: odswiezanie (czesciowe co kilka sekund, pelne co 10 odswiezen) =================

const unsigned REFRESH_S[4]={2,5,10,30};   // minimalny odstep miedzy odswiezeniami e-ink (SETUP: Screen refresh)
constexpr size_t EPD_BYTES=size_t((SCR_W+7)/8)*SCR_H;
uint8_t epdLast[EPD_BYTES];bool epdHave=false,epdChanged=false,epdUrgent=false;uint32_t epdLastAt=0;unsigned epdPartials=0;
constexpr unsigned EPD_FULL_EVERY=10;      // co tyle odswiezen czesciowych jedno pelne (usuwa smugi)
void epdShow(bool full){epdhw::show(spr.getBuffer(),cfg.invert!=0,cfg.flip!=0,!full);memcpy(epdLast,spr.getBuffer(),EPD_BYTES);epdHave=true;epdChanged=false;epdUrgent=false;epdLastAt=millis();if(full)epdPartials=0;else ++epdPartials;}
void epdMark(){epdChanged=!epdHave||memcmp(epdLast,spr.getBuffer(),EPD_BYTES)!=0;}
// Odswieza ekran, gdy tresc sie zmienila i minal odstep (po nacisnieciu przycisku odstep to ok. 1 s)
void epdTick(uint32_t now){if(!epdChanged)return;uint32_t gap=epdUrgent?1000:REFRESH_S[cfg.pwrHigh&3]*1000;if(epdHave&&now-epdLastAt<gap)return;epdShow(epdPartials>=EPD_FULL_EVERY);}

// ================= Radio =================
#define RL(x) do{int16_t e_=(x);if(e_!=RADIOLIB_ERR_NONE&&!rlErr)rlErr=e_;}while(0)
Prof profFor(Mode m){
 if(m==M_SCAN)return Prof{SCAN_START,125,7,5,SCAN_SYNC,8,2};
 const pre::Preset& p=pre::get(cfg.meshPreset);float f=pre::meshFreq(cfg.meshPreset);int pw=pre::linkPower(cfg.power);if(pw>pre::powerLimit(f))pw=pre::powerLimit(f);return Prof{f,p.bw,p.sf,p.cr,MESH_SYNC,MESH_PREAMBLE,int8_t(pw)};}
void radioApply(const Prof& p){
 if(!radioOk)return;prof=p;txBusy=false;rlErr=0;
 RL(radio.standby());RL(radio.setBandwidth(p.bw));RL(radio.setSpreadingFactor(p.sf));RL(radio.setCodingRate(p.cr));RL(radio.setSyncWord(p.sync));RL(radio.setPreambleLength(p.pre));
 RL(radio.setFrequency(p.mhz));RL(radio.setOutputPower(p.pwr));RL(radio.startReceive());}
void switchMode(Mode m){mode=m;radioApply(profFor(m));lastNoiseAt=millis();}
Mode modeFor(int s){return s==S_SCAN?M_SCAN:M_MESH;}
void radioInit(){
 loraSpi.begin(PIN_LORA_SCK,PIN_LORA_MISO,PIN_LORA_MOSI,PIN_LORA_CS);
 Prof p=profFor(M_MESH);prof=p;
 int16_t e=radio.begin(p.mhz,p.bw,p.sf,p.cr,p.sync,p.pwr,p.pre,LORA_TCXO_V,false);
 if(e!=RADIOLIB_ERR_NONE){rlErr=e;radioOk=false;Serial.printf("RADIO begin error %d\n",int(e));return;}
 radioOk=true;RL(radio.setDio2AsRfSwitch(true));RL(radio.setCurrentLimit(140.0f));RL(radio.setRxBoostedGainMode(true));RL(radio.startReceive());
 Serial.printf("RADIO OK %.3f MHz SF%u BW%.1f\n",p.mhz,p.sf,p.bw);}
uint32_t toaMs(size_t len){return uint32_t(radio.getTimeOnAir(len)/1000)+1;}
bool txStart(const uint8_t* b,size_t n,uint32_t now){
 if(!radioOk||txBusy||mode==M_SCAN)return false;uint32_t air=toaMs(n);
 if(!duty.allow(now,air,pre::dutyLimit(prof.mhz))){dutyBlocked=true;return false;}
 int16_t e=radio.startTransmit(b,n);if(e!=RADIOLIB_ERR_NONE){rlErr=e;radio.startReceive();return false;}
 dutyBlocked=false;duty.add(now,air);txBusy=true;txDeadline=now+air+250;return true;}
void txPoll(uint32_t now){if(!txBusy)return;if(digitalRead(PIN_LORA_DIO1)||int32_t(now-txDeadline)>0){radio.finishTransmit();txBusy=false;radio.startReceive();}}

// ================= Bateria =================
void batLowTrack(){bool low=batV>=2.5f&&(batPct<0||batPct<BAT_OFF_PCT)&&!batChg;if(low){if(batLowCnt<255)batLowCnt++;}else batLowCnt=0;}
void batRead(){analogSetPinAttenuation(PIN_BAT_ADC,ADC_2_5db);digitalWrite(PIN_BAT_CTRL,HIGH);delay(3);uint32_t mv=0;for(int i=0;i<8;i++)mv+=analogReadMilliVolts(PIN_BAT_ADC);digitalWrite(PIN_BAT_CTRL,LOW);
 batV=(mv/8)/1000.f*BAT_MULT;
 // no charge-status pin known: heuristic. ON: voltage rose >=20 mV since last read or >=4.18 V. OFF: fell >=20 mV, or 6 reads (60 s) without a rise while below 4.15 V
 static uint8_t flat=0;
 if(batPrev>0){float d=batV-batPrev;if(d>=0.02f||batV>=4.18f){batChg=true;flat=0;}else if(d<=-0.02f)batChg=false;else if(batChg&&batV<4.15f){if(++flat>=6)batChg=false;}else flat=0;}else batChg=batV>=4.18f;
 batPrev=batV;
 static const float V[]={4.20f,4.10f,4.00f,3.90f,3.80f,3.70f,3.60f,3.50f,3.30f};static const int P[]={100,90,80,65,50,30,15,5,0};
 if(batV<3.0f){batPct=-1;batLowTrack();return;}int p=0;if(batV>=V[0])p=100;else if(batV<=V[8])p=0;else for(int i=0;i<8;i++)if(batV<=V[i]&&batV>=V[i+1]){p=P[i+1]+int((batV-V[i+1])/(V[i]-V[i+1])*(P[i]-P[i+1]));break;}batPct=p;batLowTrack();}

// ================= Odbior pakietow =================
void onMeshFrame(const uint8_t* b,size_t n,float rssi,float snr,uint32_t now){
 mh::Hdr h;if(!mh::parse(b,n,h))return;if(h.from==myId)return;
 if(pingSt.st==PG_WAIT&&h.from==pingSt.target&&h.to==myId){uint8_t er=0;if(mh::ackFor(b,n,h,pingSt.id,&er)){pingSt.rssi=int(lroundf(rssi));pingSt.snr=snr;pingSt.rtt=now-pingSt.sentAt;pingSt.hops=mh::hops(h);pingSt.err=er;if(er==0){pingSt.st=PG_ACK;++pingSt.acks;}else pingSt.st=PG_NAK;dirty=true;
   Serial.printf("PING ack from=%08lx rssi=%.0f snr=%.1f rtt=%lu err=%u\n",(unsigned long)h.from,rssi,snr,(unsigned long)pingSt.rtt,unsigned(er));}}
 ++meshRx;hasRx=true;lastRssi=int(lroundf(rssi));lastSnr=snr;lastRxAt=now;mtab.add(h,b,n,int(lroundf(rssi)),snr,now);meshAir.add(now,uint32_t(radio.getTimeOnAir(n)));dirty=true;
 apiForward(b,n,rssi,snr);   // do CYD (jesli polaczony)
 Serial.printf("MESH from=%08lx rssi=%.0f snr=%.1f hops=%d len=%u\n",(unsigned long)h.from,rssi,snr,mh::hops(h),unsigned(n));}
void rxPoll(uint32_t now){
 if(!radioOk||txBusy||mode==M_SCAN)return;if(!digitalRead(PIN_LORA_DIO1))return;
 uint8_t buf[256];size_t len=radio.getPacketLength();int16_t e=radio.readData(buf,len>sizeof buf?sizeof buf:len);float rssi=radio.getRSSI(),snr=radio.getSNR();radio.startReceive();
 if(e==RADIOLIB_ERR_CRC_MISMATCH){++crcErrors;return;}if(e!=RADIOLIB_ERR_NONE||!len||len>sizeof buf)return;
 onMeshFrame(buf,len,rssi,snr,now);}

// ================= Skan pasma =================
void scanReset(){memset(scanCur,nz::NONE,sizeof scanCur);memset(scanPeak,nz::NONE,sizeof scanPeak);scanIdx=0;}
void scanTick(){
 if(!radioOk||scanPaused)return;
 for(int k=0;k<4;k++){float f=SCAN_START+scanIdx*SCAN_STEP;RL(radio.setFrequency(f));RL(radio.startReceive());delay(2);float r=radio.getRSSI(false);int v=int(lroundf(r));if(v<-127)v=-127;if(v>0)v=0;
  scanCur[scanIdx]=int8_t(v);if(scanPeak[scanIdx]==nz::NONE||v>scanPeak[scanIdx])scanPeak[scanIdx]=int8_t(v);scanIdx=(scanIdx+1)%SCAN_N;}
 dirty=true;}

// ================= Ping i szum =================
void pingSend(uint32_t now,bool force=false){
 if(!radioOk||mode!=M_MESH||(pingSt.st==PG_WAIT&&!force))return;lastPingTry=now;
 unsigned idx[mh::Table::MAXN];unsigned n=mtab.order(now,idx);uint32_t target=0;for(unsigned i=0;i<n;i++)if(mtab.n[idx[i]].id==meshSelId)target=meshSelId;if(!target&&n)target=mtab.n[idx[0]].id;if(!target)return;
 if(target!=pingSt.target){pingSt.sent=pingSt.acks=0;}
 pingSt.target=target;uint32_t id=esp_random();if(!id)id=1;uint8_t pkt[40];size_t len=mh::buildPing(pkt,sizeof pkt,target,myId,id,mtab.defHash,0);
 pingSt.id=id;if(txStart(pkt,len,now)){pingSt.st=PG_WAIT;pingSt.sentAt=now;++pingSt.sent;Serial.printf("PING to=%08lx id=%08lx\n",(unsigned long)target,(unsigned long)id);}else pingSt.st=PG_BLOCKED;dirty=true;}
bool selNodeKnown(){for(unsigned i=0;i<mtab.used;i++)if(mtab.n[i].id==meshSelId)return true;return false;}
bool autoPingOn(){return cfg.autoPing&&meshPicked&&meshSelId&&selNodeKnown();}
void pingTick(uint32_t now){
 const uint32_t to=autoPingOn()?AUTO_PING_MS-1000:PING_TIMEOUT_MS;   // przy pingu automatycznym brak ACK stwierdzamy przed kolejnym pingiem
 if(pingSt.st==PG_WAIT&&!txBusy&&now-pingSt.sentAt>to){pingSt.st=PG_TIMEOUT;dirty=true;}
 if(autoPingOn()&&mode==M_MESH&&!txBusy&&now-lastPingTry>=AUTO_PING_MS)pingSend(now,true);}
// ---- Beacon: rozglaszanie NODEINFO (nazwa) i POSITION (wpisana pozycja) na domyslnym kluczu kanalu, zeby inne wezly Meshtastic nas widzialy ----
bool beaconForce=false;
void beaconNames(char* lng,size_t lc,char* sh,size_t sc){char id4[8];snprintf(id4,sizeof id4,"%04lx",(unsigned long)(myId&0xFFFF));
 if(nodeName[0]){snprintf(lng,lc,"%s",nodeName);size_t k=0;for(const char* c=nodeName;*c&&k<4&&k<sc-1;c++)if(*c!=' ')sh[k++]=*c;sh[k]=0;if(!k)snprintf(sh,sc,"%s",id4);}
 else{snprintf(lng,lc,"E213 %s",id4);snprintf(sh,sc,"%s",id4);}}
void ownNames(char* lng,size_t lc,char* sh,size_t sc){beaconNames(lng,lc,sh,sc);}
bool beaconSendInfo(uint32_t now){
 char lng[44],sh[8];beaconNames(lng,sizeof lng,sh,sizeof sh);uint8_t user[80];size_t un=mh::buildUser(user,sizeof user,myId,lng,sh);if(!un)return false;
 uint32_t id=esp_random();if(!id)id=1;uint8_t pkt[120];size_t n=mh::buildBroadcast(pkt,sizeof pkt,myId,id,mtab.defHash,4,user,un);if(!n)return false;
 if(!txStart(pkt,n,now))return false;++beaconsSent;Serial.printf("BEACON nodeinfo '%s' (%s) id=%08lx\n",lng,sh,(unsigned long)id);dirty=true;return true;}
bool beaconSendPos(uint32_t now){
 if(!gpos.has)return true;uint8_t pl[32];size_t pn=mh::buildPosition(pl,sizeof pl,gpos.lat,gpos.lon,gpos.alt);uint32_t id=esp_random();if(!id)id=1;
 uint8_t pkt[80];size_t n=mh::buildBroadcast(pkt,sizeof pkt,myId,id,mtab.defHash,3,pl,pn);if(!n)return false;if(!txStart(pkt,n,now))return false;Serial.printf("BEACON position %d,%d alt %d\n",int(gpos.lat),int(gpos.lon),int(gpos.alt));return true;}
void beaconSoon(uint32_t now){if(cfg.beacon){nextBeaconAt=now+3000;beaconForce=false;}}
void beaconNow(uint32_t now){beaconForce=true;nextBeaconAt=now;dirty=true;}
void beaconTick(uint32_t now){
 if(!radioOk||mode!=M_MESH||txBusy)return;
 if(beaconPosDue&&int32_t(now-beaconPosAt)>=0){if(beaconSendPos(now)||int32_t(now-beaconPosAt)>20000)beaconPosDue=false;else beaconPosAt=now+2000;return;}
 if((cfg.beacon||beaconForce)&&int32_t(now-nextBeaconAt)>=0){
  if(beaconSendInfo(now)){beaconForce=false;nextBeaconAt=now+pre::beaconMin(cfg.beacon)*60000UL;if(!cfg.beacon)nextBeaconAt=now+3600000UL;if(gpos.has){beaconPosDue=true;beaconPosAt=now+4000;}}
  else nextBeaconAt=now+30000;}}
void noiseTick(uint32_t now){
 if(!radioOk||mode==M_SCAN||txBusy||now-lastNoiseAt<NOISE_PERIOD_MS)return;lastNoiseAt=now;
 nzCur().add(now,radio.getRSSI(false));}


// ================= Lacze WiFi / TCP z CYD (API klienta Meshtastic na porcie 4403) =================
// E213 tworzy wlasna siec WiFi (punkt dostepowy, nazwa i haslo w include/link.h) i serwer TCP. CYD laczy sie jak z wezlem Meshtastic:
// po want_config_id dostaje my_info, NodeInfo (E213 i slyszane wezly), Config.lora, kanal 0 i config_complete_id, potem kazdy
// odebrany pakiet LoRa z RSSI/SNR. Pakiety od CYD (np. test lacza z want_ack) E213 szyfruje domyslnym kluczem kanalu i nadaje.
WiFiServer apiSrv(LINK_PORT);WiFiClient apiCli;api::Framer apiFr;
// apiHas: jest zaakceptowany klient (WiFiClient::operator bool zwraca connected(), wiec wlasna flaga)
bool wifiOn=false,apiConfigured=false,apiHas=false;uint32_t apiFid=1,apiConnAt=0,apiLastRx=0,apiTelAt=0;
uint32_t apiFramesOut=0,apiFramesIn=0,apiTxOk=0,apiTxFail=0,apiSessions=0;char apiPeer[16]="";
constexpr uint32_t API_IDLE_MS=600000;            // brak ramek od klienta przez 10 min: rozlaczenie (CYD wysyla heartbeat co 30 s)
constexpr uint32_t API_TEL_MS=60000;              // telemetria E213 (bateria, zajetosc) co 60 s
constexpr uint32_t API_TXQ_MS=5000;               // pakiet klienta czeka na wolne radio najwyzej tyle
struct ApiTx{bool used=false;uint8_t pkt[256];size_t len=0;uint32_t id=0,since=0;bool wantAck=false;};ApiTx apiQ[4];
bool apiLinked(){return wifiOn&&apiHas&&apiCli.connected();}
void apiSend(const uint8_t* pl,size_t n){if(!n||!apiLinked())return;uint8_t f[600];size_t k=api::frame(pl,n,f,sizeof f);if(k){apiCli.write(f,k);++apiFramesOut;}}
uint32_t utcNow(){time_t t=time(nullptr);return api::localToUtc(t>0?uint32_t(t):0);}
void ownNames(char* lng,size_t lc,char* sh,size_t sc);
api::NodeDesc ownDesc(uint32_t now,char* lng,size_t lc,char* sh,size_t sc){ownNames(lng,lc,sh,sc);api::NodeDesc d;d.num=myId;d.longName=lng;d.shortName=sh;d.hw=67;d.lastHeard=utcNow();
 if(gpos.has){d.hasPos=true;d.lat=gpos.lat;d.lon=gpos.lon;d.alt=gpos.alt;}
 d.hasMetrics=true;d.battery=batChg?101:batPct;d.voltage=batV;d.chUtil=meshAir.pct(now);d.airTx=duty.pct(now);d.uptime=now/1000;return d;}
// Pelny zrzut konfiguracji po want_config_id (kolejnosc jak w firmware Meshtastic)
void apiSendConfig(uint32_t cid,uint32_t now){uint8_t o[300];size_t n;
 n=api::myInfo(o,sizeof o,apiFid++,myId);apiSend(o,n);
 char lng[44],sh[8];api::NodeDesc me=ownDesc(now,lng,sizeof lng,sh,sizeof sh);n=api::nodeInfo(o,sizeof o,apiFid++,me);apiSend(o,n);
 uint32_t utc=utcNow();for(unsigned i=0;i<mtab.used;i++){const mh::Node& x=mtab.n[i];if(!x.id||x.id==myId)continue;api::NodeDesc d;d.num=x.id;char nm[24],s4[6];
  d.hasUser=x.name[0]!=0;if(d.hasUser){snprintf(nm,sizeof nm,"%s",x.name);size_t k=0;for(const char* c=x.name;*c&&k<4;c++)if(*c!=' ')s4[k++]=*c;s4[k]=0;d.longName=nm;d.shortName=s4;d.hw=0;}
  d.snr=x.snr;d.hops=x.hops;d.lastHeard=utc?utc-(now-x.last)/1000:0;n=api::nodeInfo(o,sizeof o,apiFid++,d);apiSend(o,n);}
 const pre::Preset& pr=pre::get(cfg.meshPreset);const Prof p=profFor(M_MESH);
 n=api::loraConfig(o,sizeof o,apiFid++,api::presetEnum(cfg.meshPreset),pr.sf,pr.bw,pr.cr,pre::meshFreq(cfg.meshPreset),api::regionCode(pr.region),p.pwr);apiSend(o,n);
 n=api::channel(o,sizeof o,apiFid++,0,pr.name,true);apiSend(o,n);
 n=api::configComplete(o,sizeof o,apiFid++,cid);apiSend(o,n);apiConfigured=true;apiTelAt=now;
 Serial.printf("API config sent (%u nodes)\n",mtab.used);}
// Pakiet "od E213" do klienta (bez RSSI: nie przyszedl przez radio), np. blad routingu albo telemetria
void apiLocalPacket(uint32_t to,const uint8_t* data,size_t dn){api::PktDesc d;d.from=myId;d.to=to;d.id=esp_random()|1;d.hasRx=false;d.decoded=data;d.decodedLen=dn;d.hopStart=0;d.hopLimit=0;
 uint8_t o[300];size_t n=api::packet(o,sizeof o,apiFid++,d);apiSend(o,n);}
void apiRoutingError(uint32_t reqId,unsigned err){uint8_t d[24];size_t dn=api::dataRouting(d,sizeof d,reqId,err);apiLocalPacket(myId,d,dn);++apiTxFail;Serial.printf("API tx %08lx failed, routing error %u\n",(unsigned long)reqId,err);}
void apiTelemetry(uint32_t now){uint8_t d[64];size_t dn=api::dataTelemetry(d,sizeof d,utcNow(),batChg?101:batPct,batV,meshAir.pct(now),duty.pct(now),now/1000);apiLocalPacket(0xFFFFFFFFu,d,dn);}
// Kazdy odebrany pakiet LoRa: kanal z domyslnym kluczem jako odszyfrowane Data, inne kanaly jako zaszyfrowane bajty
void apiForward(const uint8_t* b,size_t n,float rssi,float snr){
 if(!apiLinked()||!apiConfigured||n<16)return;mh::Hdr h;if(!mh::parse(b,n,h))return;
 api::PktDesc d;d.from=h.from;d.to=h.to;d.id=h.id;d.hopLimit=h.hopLimit;d.hopStart=h.hopStart;d.wantAck=h.wantAck;d.viaMqtt=h.viaMqtt;d.relay=b[15];d.snr=snr;d.rssi=int(lroundf(rssi));d.rxTime=utcNow();
 uint8_t plain[240];size_t pn=n-16;bool dec=false;
 if(h.chan==mtab.defHash&&pn&&pn<=sizeof plain){mh::ctrCrypt(h.id,h.from,b+16,plain,pn);api::Reader r(plain,pn);api::Field f;bool port=false;while(r.next(f))if(f.tag==1&&f.wire==0)port=true;dec=r.ok()&&port;}
 if(dec){d.channel=0;d.decoded=plain;d.decodedLen=pn;}else{d.channel=h.chan;d.encrypted=b+16;d.encLen=pn;}
 uint8_t o[320];size_t k=api::packet(o,sizeof o,apiFid++,d);apiSend(o,k);}
// Pakiet od klienta -> LoRa. Odpowiedzi (ACK celu) wracaja do klienta jak kazdy odebrany pakiet.
void apiTransmit(const api::ToRadio& t,uint32_t now){
 uint32_t id=t.id?t.id:(esp_random()|1);
 if(!t.hasDecoded||!t.dataLen||t.dataLen>200){apiRoutingError(id,7);return;}                 // TOO_LARGE / brak danych
 if(t.to==myId||!t.to){apiRoutingError(id,8);return;}                                       // komendy do E213 (np. admin) nie sa obslugiwane: NO_RESPONSE
 if(!radioOk||mode!=M_MESH){apiRoutingError(id,4);return;}                                  // radio w trybie skanu: NO_INTERFACE
 const unsigned hop=t.hopLimit?(t.hopLimit>7?7:t.hopLimit):3;
 ApiTx* q=nullptr;for(auto& x:apiQ)if(!x.used){q=&x;break;}if(!q){apiRoutingError(id,38);return;}   // kolejka pelna: RATE_LIMIT_EXCEEDED
 uint8_t* o=q->pkt;for(unsigned i=0;i<4;i++){o[i]=uint8_t(t.to>>(8*i));o[4+i]=uint8_t(myId>>(8*i));o[8+i]=uint8_t(id>>(8*i));}
 o[12]=uint8_t(hop|(t.wantAck?8:0)|(hop<<5));o[13]=mtab.defHash;o[14]=0;o[15]=uint8_t(myId&0xFF);mh::ctrCrypt(id,myId,t.data,o+16,t.dataLen);
 q->len=16+t.dataLen;q->id=id;q->since=now;q->wantAck=t.wantAck;q->used=true;}
void apiTxTick(uint32_t now){for(auto& q:apiQ){if(!q.used)continue;
  if(txBusy||mode!=M_MESH){if(now-q.since>API_TXQ_MS){q.used=false;apiRoutingError(q.id,mode!=M_MESH?4:3);}return;}   // czeka na wolne radio; TIMEOUT / NO_INTERFACE
  if(!duty.allow(now,toaMs(q.len),pre::dutyLimit(prof.mhz))){q.used=false;dutyBlocked=true;apiRoutingError(q.id,9);return;}   // DUTY_CYCLE_LIMIT
  q.used=false;if(txStart(q.pkt,q.len,now)){++apiTxOk;Serial.printf("API tx %08lx to=%02x%02x%02x%02x len=%u\n",(unsigned long)q.id,q.pkt[3],q.pkt[2],q.pkt[1],q.pkt[0],unsigned(q.len));dirty=true;}
  else apiRoutingError(q.id,4);
  return;}}                                                                                       // jeden pakiet na obieg petli
void apiHandle(const uint8_t* p,size_t n){uint32_t now=millis();apiLastRx=now;++apiFramesIn;api::ToRadio t;if(!api::parseToRadio(p,n,t))return;
 switch(t.kind){case api::TR_WANT_CONFIG:apiSendConfig(t.configId,now);break;case api::TR_PACKET:apiTransmit(t,now);break;
  case api::TR_DISCONNECT:apiCli.stop();apiHas=false;apiConfigured=false;dirty=true;break;default:break;}}
void wifiStart(){if(wifiOn)return;WiFi.mode(WIFI_AP);bool ok=WiFi.softAP(LINK_SSID,LINK_PASS,LINK_CHANNEL,0,2);apiSrv.begin();apiSrv.setNoDelay(true);wifiOn=true;
 Serial.printf("WiFi AP %s %s, API on port %u\n",LINK_SSID,ok?"up":"FAILED",LINK_PORT);dirty=true;}
void wifiStop(){if(!wifiOn)return;if(apiHas)apiCli.stop();apiHas=false;apiSrv.end();WiFi.softAPdisconnect(true);WiFi.mode(WIFI_OFF);wifiOn=false;apiConfigured=false;for(auto& q:apiQ)q.used=false;Serial.println("WiFi off");dirty=true;}
void wifiSet(bool on){if(on)wifiStart();else wifiStop();}
void apiTick(uint32_t now){
 if(!wifiOn)return;
 WiFiClient c=apiSrv.available();
 if(c){if(apiHas)apiCli.stop();apiCli=c;apiHas=true;apiCli.setNoDelay(true);apiFr.reset();apiConfigured=false;apiConnAt=apiLastRx=now;++apiSessions;
  IPAddress ip=apiCli.remoteIP();snprintf(apiPeer,sizeof apiPeer,"%u.%u.%u.%u",ip[0],ip[1],ip[2],ip[3]);Serial.printf("API client %s connected\n",apiPeer);dirty=true;epdUrgent=true;}
 if(apiHas&&!apiCli.connected()){apiCli.stop();apiHas=false;apiConfigured=false;for(auto& q:apiQ)q.used=false;Serial.println("API client disconnected");dirty=true;epdUrgent=true;return;}
 if(!apiHas)return;
 for(int k=0;k<512&&apiHas&&apiCli.available();k++){int v=apiCli.read();if(v<0)break;apiFr.feed(uint8_t(v),now,apiHandle);}
 if(!apiHas)return;
 if(now-apiLastRx>API_IDLE_MS){Serial.println("API client idle, dropping");apiCli.stop();apiHas=false;apiConfigured=false;dirty=true;return;}
 apiTxTick(now);
 if(apiConfigured&&now-apiTelAt>=API_TEL_MS){apiTelAt=now;apiTelemetry(now);}}

// ================= Zegar (czas lokalny ustawiany z telefonu przez konfigurator) =================
bool clockGet(struct tm& t){time_t n=time(nullptr);if(n<1700000000)return false;gmtime_r(&n,&t);return true;}
void clockSet(uint32_t localEpoch){struct timeval tv;tv.tv_sec=(time_t)localEpoch;tv.tv_usec=0;settimeofday(&tv,nullptr);dirty=true;}
bool clockText(char* o,size_t cap){struct tm t;if(!clockGet(t)){snprintf(o,cap,"--:--");return false;}snprintf(o,cap,"%02d:%02d",t.tm_hour,t.tm_min);return true;}

// ================= Rysowanie (plotno 1-bitowe 250x122, e-ink: bialy papier, czarny tusz) =================
void txt(const String& s,int x,int y,int font,uint16_t fg,uint16_t bg,uint8_t datum=TL_DATUM){spr.setTextDatum(datum);spr.setTextColor(fg,bg);spr.drawStr(s.c_str(),x,y,font);spr.setTextDatum(TL_DATUM);}
void panel(int x,int y,int w,int h,uint16_t bg=cPANEL,uint16_t line=cLINE){spr.fillRoundRect(x,y,w,h,3,bg);spr.drawRoundRect(x,y,w,h,3,line);}
void chip(int x,int y,int w,const char* s,uint16_t fg,uint16_t bg){spr.fillRect(x,y,w,9,fg);txt(s,x+w/2,y+1,1,bg,fg,TC_DATUM);}   // odwrocona etykieta
unsigned linkSf(){return pre::get(cfg.meshPreset).sf;}
unsigned winSec(){static const unsigned W[]={60,600,3600};return W[noiseWin%3];}
const char* winName(){static const char* N[]={"1 min","10 min","1 h"};return N[noiseWin%3];}
// kafelek: tytul + opcjonalnie odwrocenie (np. niski stan baterii)
void tile(int x,int y,const char* title,bool inv=false){panel(x,y,80,45,inv?cLINE:cPANEL);txt(title,x+4,y+3,1,inv?cPANEL:cLINE,inv?cLINE:cPANEL);}
const int TX0[3]={2,84,166},TY0[2]={18,63};

void drawHeader(){
 const int nx[]={4,9,14},ny[]={11,5,12};spr.drawLine(nx[0],ny[0],nx[1],ny[1],cLINE);spr.drawLine(nx[1],ny[1],nx[2],ny[2],cLINE);for(int i=0;i<3;i++)spr.fillCircle(nx[i],ny[i],2,cLINE);
 txt(nodeName[0]?nodeName:"Field Test",19,1,2,cLINE,cBG);
 char c[8];clockText(c,sizeof c);txt(c,247,1,2,cLINE,cBG,TR_DATUM);
 txt(SCR_NAME[screen],150,1,1,cLINE,cBG);
 if(wifiOn){if(apiLinked())chip(184,0,20,"CYD",cLINE,cBG);else txt("AP",186,1,1,cLINE,cBG);}   // lacze z CYD
 if(!radioOk)chip(150,9,54,"RADIO ERR",cLINE,cBG);else txt(mode==M_MESH?"LISTEN":"SWEEP",150,9,1,cLINE,cBG);
 spr.drawFastHLine(0,16,SCR_W,cLINE);}
void drawFooter(const char* l,const char* r){
 spr.drawFastHLine(0,110,SCR_W,cLINE);txt(l,3,113,1,cLINE,cBG);txt(r,247,113,1,cLINE,cBG,TR_DATUM);}

void drawHome(){
 const int *X=TX0,*Y=TY0;char b[40];uint32_t now=millis();const qual::Q q=qual::quality(hasRx,lastSnr,linkSf());
 // NODES
 {tile(X[0],Y[0],"NODES HEARD");snprintf(b,sizeof b,"%u",mtab.used);txt(b,X[0]+4,Y[0]+10,4,cLINE,cPANEL);
  snprintf(b,sizeof b,"%lu pkts",(unsigned long)mtab.packets);txt(b,X[0]+4,Y[0]+29,1,cLINE,cPANEL);
  if(hasRx){snprintf(b,sizeof b,"last %lus",(unsigned long)((now-lastRxAt)/1000));txt(b,X[0]+4,Y[0]+36,1,cLINE,cPANEL);}else txt("listening",X[0]+4,Y[0]+36,1,cLINE,cPANEL);}
 // LAST PACKET
 {tile(X[1],Y[0],"LAST PACKET");if(hasRx){snprintf(b,sizeof b,"%d",lastRssi);txt(b,X[1]+4,Y[0]+10,4,cLINE,cPANEL);txt("dBm",X[1]+8+spr.textWidth(b,4),Y[0]+18,1,cLINE,cPANEL);
   snprintf(b,sizeof b,"SNR %.1f dB",lastSnr);txt(b,X[1]+4,Y[0]+29,1,cLINE,cPANEL);txt(qual::qText(q),X[1]+4,Y[0]+36,1,cLINE,cPANEL);}
  else{txt("--",X[1]+4,Y[0]+10,4,cLINE,cPANEL);txt("none yet",X[1]+4,Y[0]+29,1,cLINE,cPANEL);}}
 // NOISE
 {tile(X[2],Y[0],"NOISE");nz::Stats s=nzMesh.stats(60);if(s.has){snprintf(b,sizeof b,"%d",s.now);txt(b,X[2]+4,Y[0]+10,4,cLINE,cPANEL);txt("dBm",X[2]+8+spr.textWidth(b,4),Y[0]+18,1,cLINE,cPANEL);
   snprintf(b,sizeof b,"floor %d",s.floor);txt(b,X[2]+4,Y[0]+29,1,cLINE,cPANEL);snprintf(b,sizeof b,"peak %d",s.peak);txt(b,X[2]+4,Y[0]+36,1,cLINE,cPANEL);}
  else{txt("--",X[2]+4,Y[0]+10,4,cLINE,cPANEL);txt("measuring...",X[2]+4,Y[0]+29,1,cLINE,cPANEL);}}
 // CHANNEL BUSY
 {tile(X[0],Y[1],"CHANNEL BUSY");nz::Stats s=nzMesh.stats(60);if(s.has){snprintf(b,sizeof b,"%.0f%%",s.busy);txt(b,X[0]+4,Y[1]+10,4,cLINE,cPANEL);}else txt("--",X[0]+4,Y[1]+10,4,cLINE,cPANEL);
  txt("> floor+8dB",X[0]+4,Y[1]+29,1,cLINE,cPANEL);snprintf(b,sizeof b,"TX %.1f%%/h",duty.pct(now));txt(b,X[0]+4,Y[1]+36,1,cLINE,cPANEL);}
 // RADIO
 {const Prof p=profFor(M_MESH);snprintf(b,sizeof b,"RADIO %ddBm",int(p.pwr));tile(X[1],Y[1],b);snprintf(b,sizeof b,"%.1f",(double)p.mhz);txt(b,X[1]+4,Y[1]+10,4,cLINE,cPANEL);
  txt(pre::get(cfg.meshPreset).name,X[1]+4,Y[1]+29,1,cLINE,cPANEL);snprintf(b,sizeof b,"SF%u BW%g",p.sf,(double)p.bw);txt(b,X[1]+4,Y[1]+36,1,cLINE,cPANEL);}
 // BATTERY (niski stan: kafelek odwrocony)
 {const bool low=batPct>=0&&batPct<=10&&!batChg;const uint16_t f=low?cPANEL:cLINE,g=low?cLINE:cPANEL;tile(X[2],Y[1],"BATTERY",low);
  if(batPct>=0){snprintf(b,sizeof b,"%.2f",batV);txt(b,X[2]+4,Y[1]+10,4,f,g);txt("V",X[2]+8+spr.textWidth(b,4),Y[1]+18,1,f,g);
   if(batChg)txt("CHARGING",X[2]+4,Y[1]+29,1,f,g);else{snprintf(b,sizeof b,"~%d%%%s",batPct,low?" LOW":"");txt(b,X[2]+4,Y[1]+29,1,f,g);}}
  else{txt("--",X[2]+4,Y[1]+10,4,f,g);txt("no reading",X[2]+4,Y[1]+29,1,f,g);}}}

void drawNoise(){
 const nz::Log& L=nzMesh;nz::Stats s=L.stats(winSec());char b[32];const char* LB[]={"NOW dBm","FLOOR dBm","PEAK dBm","BUSY %"};
 for(int i=0;i<4;i++){int x=2+i*62;panel(x,18,60,28);txt(LB[i],x+30,20,1,cLINE,cPANEL,TC_DATUM);
  if(s.has){if(i==0)snprintf(b,sizeof b,"%d",s.now);else if(i==1)snprintf(b,sizeof b,"%d",s.floor);else if(i==2)snprintf(b,sizeof b,"%d",s.peak);else snprintf(b,sizeof b,"%.0f",s.busy);}else snprintf(b,sizeof b,"--");
  txt(b,x+30,29,2,cLINE,cPANEL,TC_DATUM);}
 panel(2,48,246,60);const int x0=30,x1=244,yt=53,yb=84,w=x1-x0;
 int lo=-130,hi=-60;if(s.has){lo=((s.floor-10)/10)*10-(s.floor-10<0?10:0);if(lo<-130)lo=-130;hi=((s.peak+5)/10)*10+10;if(hi>-30)hi=-30;if(hi-lo<30)hi=lo+30;}
 auto Y=[&](int v){return yb-(constrain(v,lo,hi)-lo)*(yb-yt)/(hi-lo);};
 {const int step=(hi-lo)<=70?20:40;for(int v=lo;v<=hi;v+=10){int y=Y(v);for(int x=x0;x<=x1;x+=4)spr.drawPixel(x,y,cLINE);if(((v-lo)%step)==0){snprintf(b,sizeof b,"%d",v);txt(b,27,y-3,1,cLINE,cPANEL,TR_DATUM);}}}
 unsigned win=winSec();
 for(int cx=0;cx<w;cx++){unsigned a0=unsigned(uint64_t(w-1-cx)*win/w),a1=unsigned(uint64_t(w-cx)*win/w);if(a1<=a0)a1=a0+1;if(a0>=L.used)continue;if(a1>L.used)a1=L.used;
  int mn=0,mx=-200;long sum=0;unsigned n=0;bool any=false;int bs=0;for(unsigned a=a0;a<a1;a++){int i=L.idx(a);if(L.avg[i]==nz::NONE)continue;if(!any){mn=L.lo[i];any=true;}mn=std::min<int>(mn,L.lo[i]);mx=std::max<int>(mx,L.hi[i]);sum+=L.avg[i];bs+=L.busy[i];++n;}
  if(!any)continue;int x=x0+cx;spr.drawFastVLine(x,Y(mx),std::max(1,Y(mn)-Y(mx)),cBAND);spr.drawPixel(x,Y(int(sum/n)),cLINE);spr.drawPixel(x,Y(int(sum/n))+1,cLINE);
  int p=bs/int(n);int h=p<=0?1:std::max(1,p*7/100);spr.drawFastVLine(x,106-h,h,cLINE);}
 if(!s.has)txt("collecting noise samples...",(x0+x1)/2,(yt+yb)/2-4,1,cLINE,cPANEL,MC_DATUM);
 spr.fillCircle(32,92,1,cLINE);txt("avg",37,89,1,cLINE,cPANEL);spr.fillRect(64,90,6,3,cBAND);txt("min-max",73,89,1,cLINE,cPANEL);txt(String("time ")+winName(),244,89,1,cLINE,cPANEL,TR_DATUM);
 txt("BUSY",5,99,1,cLINE,cPANEL);}

void drawScan(){
 panel(2,18,246,90);const int x0=28,x1=242,yt=23,yb=78;char b[40];
 auto Y=[&](int v){return yb-(constrain(v,NOISE_MIN,-40)-NOISE_MIN)*(yb-yt)/(-40-NOISE_MIN);};
 for(int v=-120;v<=-50;v+=20){int y=Y(v);for(int x=x0;x<=x1;x+=4)spr.drawPixel(x,y,cLINE);snprintf(b,sizeof b,"%d",v);txt(b,25,y-3,1,cLINE,cPANEL,TR_DATUM);}
 auto X=[&](float f){return x0+int((f-SCAN_START)/(SCAN_STEP*(SCAN_N-1))*(x1-x0-3));};
 const float fm=pre::meshFreq(cfg.meshPreset);
 for(int y=yt;y<=yb;y+=2)spr.drawPixel(X(fm)+1,y,cLINE);   // kanal Meshtastic: linia przerywana
 for(unsigned i=0;i<SCAN_N;i++){int x=X(SCAN_START+i*SCAN_STEP);if(scanCur[i]!=nz::NONE){int y=Y(scanCur[i]);spr.fillRect(x,y,3,yb-y+1,cBAND);spr.fillRect(x,y,3,2,cLINE);}
  if(scanPeak[i]!=nz::NONE)spr.fillRect(x,Y(scanPeak[i])-1,3,1,cLINE);}
 for(int f=863;f<=870;f++){int x=X(float(f));spr.drawFastVLine(x+1,yb+1,3,cLINE);snprintf(b,sizeof b,"%d",f);txt(b,x+1,yb+5,1,cLINE,cPANEL,TC_DATUM);}
 int cur=(scanIdx+SCAN_N-1)%SCAN_N;snprintf(b,sizeof b,"%.1f MHz %d dBm",SCAN_START+cur*SCAN_STEP,scanCur[cur]==nz::NONE?0:scanCur[cur]);
 txt(b,6,93,1,cLINE,cPANEL);if(scanPaused)txt("PAUSED",244,93,1,cLINE,cPANEL,TR_DATUM);
 snprintf(b,sizeof b,"- - Meshtastic %.3f MHz",fm);txt(b,6,100,1,cLINE,cPANEL);}

// Skraca tekst tak, by miescil sie w podanej szerokosci (w pikselach) danym fontem.
void fitText(char* s,int maxW,int font){size_t n=strlen(s);while(n>1&&spr.textWidth(s,font)>maxW){s[--n]=0;}}
const char* nodeLabel(const mh::Node& n,char* b,size_t cap){if(n.name[0])snprintf(b,cap,"%s",n.name);else snprintf(b,cap,"!%08lx",(unsigned long)n.id);return b;}
// Panel pingu wybranego wezla (na dole ekranow MESH i NODE)
void drawPingPanel(uint32_t now){
 char b[64];panel(2,92,246,17);
 if(pingSt.target&&pingSt.target==meshSelId){
  snprintf(b,sizeof b,"PING !%08lx sent %u ack %u",(unsigned long)pingSt.target,pingSt.sent,pingSt.acks);txt(b,6,93,1,cLINE,cPANEL);if(autoPingOn())txt("AUTO 10s",244,93,1,cLINE,cPANEL,TR_DATUM);
  switch(pingSt.st){
   case PG_WAIT:snprintf(b,sizeof b,"waiting for ACK... %lu s",(unsigned long)((now-pingSt.sentAt)/1000));break;
   case PG_ACK:snprintf(b,sizeof b,"ACK %ddBm SNR %.1f %.1fs hops %d",pingSt.rssi,pingSt.snr,pingSt.rtt/1000.0,pingSt.hops);break;
   case PG_NAK:snprintf(b,sizeof b,"NAK (error %u) %d dBm",unsigned(pingSt.err),pingSt.rssi);break;
   case PG_TIMEOUT:snprintf(b,sizeof b,"no ACK (out of range or other key)");break;
   case PG_BLOCKED:snprintf(b,sizeof b,"not sent: duty limit or radio busy");break;
   default:snprintf(b,sizeof b,autoPingOn()?"auto ping in a moment":"hold KEY to ping");}
  txt(b,6,101,1,cLINE,cPANEL);}
 else txt(meshPicked?(cfg.autoPing?"pinging the selected node every 10 s":"hold KEY: ping the selected node"):(cfg.autoPing?"KEY: select a node (pinged every 10 s)":"KEY: select a node, hold KEY: ping"),6,97,1,cLINE,cPANEL);}

void drawMesh(){
 uint32_t now=millis();char b[64];const pre::Preset& pr=pre::get(cfg.meshPreset);
 panel(2,18,246,20);snprintf(b,sizeof b,"%s %.3f MHz SF%u BW%g",pr.name,(double)pre::meshFreq(cfg.meshPreset),pr.sf,(double)pr.bw);txt(b,6,20,1,cLINE,cPANEL);
 snprintf(b,sizeof b,"pkts %lu nodes %u air %.1f%% crc %lu",(unsigned long)mtab.packets,mtab.used,meshAir.pct(now),(unsigned long)crcErrors);txt(b,6,29,1,cLINE,cPANEL);
 unsigned idx[mh::Table::MAXN];unsigned n=mtab.order(now,idx);
 if(!n){panel(2,40,246,51);txt("listening for Meshtastic packets...",125,58,1,cLINE,cPANEL,MC_DATUM);txt("(headers only, content is not decoded)",125,72,1,cLINE,cPANEL,MC_DATUM);drawPingPanel(now);return;}
 unsigned sel=0;bool found=false;for(unsigned i=0;i<n;i++)if(mtab.n[idx[i]].id==meshSelId){sel=i;found=true;}if(!found)meshSelId=mtab.n[idx[0]].id;
 if(sel<meshOff)meshOff=sel;if(sel>=meshOff+3)meshOff=sel-2;if(meshOff+3>n)meshOff=n>3?n-3:0;
 for(unsigned r=0;r<3&&meshOff+r<n;r++){const mh::Node& x=mtab.n[idx[meshOff+r]];int y=39+int(r)*17;bool isSel=meshOff+r==sel;const uint16_t fg=isSel?cPANEL:cLINE,bg=isSel?cLINE:cPANEL;panel(2,y,246,18,bg,cLINE);
  char nm[28];nodeLabel(x,nm,sizeof nm);fitText(nm,112,2);txt(nm,6,y+2,2,fg,bg);
  snprintf(b,sizeof b,"%d",x.rssi);txt(b,152,y+2,2,fg,bg,TR_DATUM);
  snprintf(b,sizeof b,"%d",int(lroundf(x.snr)));txt(b,184,y+2,2,fg,bg,TR_DATUM);
  if(x.hops>=0)snprintf(b,sizeof b,"h%d",x.hops);else snprintf(b,sizeof b,"h?");txt(b,208,y+2,2,fg,bg,TR_DATUM);
  uint32_t age=(now-x.last)/1000;if(age<100)snprintf(b,sizeof b,"%lus",(unsigned long)age);else snprintf(b,sizeof b,"%lum",(unsigned long)(age/60));txt(b,245,y+2,2,fg,bg,TR_DATUM);}
 snprintf(b,sizeof b,"%u/%u",sel+1,n);txt(b,245,20,1,cLINE,cPANEL,TR_DATUM);
 drawPingPanel(now);}

// Ekran szczegolow wybranego wezla Meshtastic: dane z podsluchu + wynik pingu.
void drawNode(){
 uint32_t now=millis();char b[64];const mh::Node* x=nullptr;for(unsigned i=0;i<mtab.used;i++)if(mtab.n[i].id==meshSelId)x=&mtab.n[i];
 if(!x){panel(2,18,246,90);txt("no node selected",125,50,2,cLINE,cPANEL,MC_DATUM);txt("pick one on MESH (KEY: next node)",125,72,1,cLINE,cPANEL,MC_DATUM);return;}
 // lewa kolumna: nazwa, id, wiek, statystyki; prawa: wykres RSSI
 panel(2,18,120,72);char nm[28];nodeLabel(*x,nm,sizeof nm);fitText(nm,112,2);txt(nm,6,20,2,cLINE,cPANEL);
 snprintf(b,sizeof b,"!%08lx ch %02X%s",(unsigned long)x->id,unsigned(x->chan),x->mqtt?" MQTT":"");txt(b,6,38,1,cLINE,cPANEL);
 uint32_t age=(now-x->last)/1000;if(age<100)snprintf(b,sizeof b,"%lus ago",(unsigned long)age);else snprintf(b,sizeof b,"%lum ago",(unsigned long)(age/60));
 char c[24];snprintf(c,sizeof c,"%lu pkts  %s",(unsigned long)x->count,b);txt(c,6,47,1,cLINE,cPANEL);
 snprintf(b,sizeof b,"RSSI %d  SNR %.1f",x->rssi,x->snr);txt(b,6,57,1,cLINE,cPANEL);
 if(x->hops>=0)snprintf(b,sizeof b,"hops %d  avg %d",x->hops,x->ravg());else snprintf(b,sizeof b,"hops ?  avg %d",x->ravg());txt(b,6,66,1,cLINE,cPANEL);
 snprintf(b,sizeof b,"min %d  max %d",x->rmin,x->rmax);txt(b,6,75,1,cLINE,cPANEL);
 panel(124,18,124,72);const int cx0=146,cx1=244,cy0=24,cy1=84;txt("-30",144,cy0-3,1,cLINE,cPANEL,TR_DATUM);txt("-130",144,cy1-4,1,cLINE,cPANEL,TR_DATUM);
 for(int v=-50;v>=-110;v-=20){int y=cy1-(v+130)*(cy1-cy0)/100;for(int px=cx0;px<=cx1;px+=4)spr.drawPixel(px,y,cLINE);}
 auto Y=[&](int v){return cy1-(constrain(v,-130,-30)+130)*(cy1-cy0)/100;};int px0=0,py0=0;
 for(unsigned i=0;i<x->hn;i++){int px=cx0+int(uint32_t(i)*(cx1-cx0)/(mh::Node::HIST-1));int py=Y(x->at(i));if(i)spr.drawLine(px0,py0,px,py,cLINE);px0=px;py0=py;}
 for(unsigned i=0;i<x->hn;i++){int px=cx0+int(uint32_t(i)*(cx1-cx0)/(mh::Node::HIST-1));spr.fillRect(px-1,Y(x->at(i))-1,3,3,cLINE);}
 if(x->hn<2)txt("collecting...",(cx0+cx1)/2,(cy0+cy1)/2-4,1,cLINE,cPANEL,MC_DATUM);
 drawPingPanel(now);}

// ---- Ustawienia ----
constexpr unsigned SET_N=9;

void setLine(unsigned i,char* name,size_t nc,char* val,size_t vc){
 static const char* N[SET_N]={"TX power (ping)","Mesh preset","Dark mode","Flip screen","Screen refresh","Node beacon","Auto ping","Bluetooth","WiFi link (CYD)"};
 snprintf(name,nc,"%s",N[i]);const Prof p=profFor(M_MESH);
 switch(i){
  case 0:snprintf(val,vc,"%d dBm%s",int(p.pwr),pre::linkPower(cfg.power)>pre::powerLimit(p.mhz)?" (limit)":"");break;
  case 1:snprintf(val,vc,"%s",pre::get(cfg.meshPreset).name);break;
  case 2:snprintf(val,vc,"%s",cfg.invert?"ON":"OFF");break;
  case 3:snprintf(val,vc,"%s",cfg.flip?"180 deg":"normal");break;
  case 4:snprintf(val,vc,"every %u s",REFRESH_S[cfg.pwrHigh&3]);break;
  case 5:if(cfg.beacon)snprintf(val,vc,"every %u min",pre::beaconMin(cfg.beacon));else snprintf(val,vc,"OFF");break;
  case 6:snprintf(val,vc,"%s",cfg.autoPing?"ON  10 s":"OFF");break;
  case 7:if(cfg.ble)snprintf(val,vc,"PIN %06lu",(unsigned long)blePin);else snprintf(val,vc,"OFF");break;
  default:if(!cfg.wifi)snprintf(val,vc,"OFF");else if(apiLinked())snprintf(val,vc,"CYD %s",apiPeer);else snprintf(val,vc,"ON  waiting");}}
void drawSet(){
 unsigned first=setSel<2?0:setSel-2;if(first+5>SET_N)first=SET_N-5;
 for(unsigned r=0;r<5;r++){unsigned i=first+r;int y=18+int(r)*18;bool sel=i==setSel;char nm[24],v[32];setLine(i,nm,sizeof nm,v,sizeof v);
  const uint16_t fg=sel?cPANEL:cLINE,bg=sel?cLINE:cPANEL;panel(2,y,246,19,bg,cLINE);txt(nm,8,y+6,1,fg,bg);txt(v,243,y+2,2,fg,bg,TR_DATUM);}}

// ---------- ekran powitalny (statyczny: e-ink odswieza sie wolno, wiec bez animacji) ----------
void drawMeshArt(int cx,int cy){
 static const int8_t NX[6]={-84,-46,52,86,-18,32},NY[6]={-17,17,-18,15,-4,21};
 for(int i=0;i<6;i++)spr.drawLine(cx,cy,cx+NX[i],cy+NY[i],cLINE);
 spr.drawLine(cx+NX[1],cy+NY[1],cx+NX[4],cy+NY[4],cLINE);spr.drawLine(cx+NX[3],cy+NY[3],cx+NX[5],cy+NY[5],cLINE);spr.drawLine(cx+NX[0],cy+NY[0],cx+NX[4],cy+NY[4],cLINE);
 spr.drawCircle(cx,cy,10,cLINE);spr.drawCircle(cx,cy,17,cLINE);spr.drawCircle(cx,cy,24,cLINE);
 for(int i=0;i<6;i++){spr.fillCircle(cx+NX[i],cy+NY[i],4,cLINE);spr.fillCircle(cx+NX[i],cy+NY[i],i%2?1:2,cBG);}
 spr.fillCircle(cx,cy,6,cLINE);spr.fillCircle(cx,cy,2,cBG);}
void drawSplash(){
 spr.fillSprite(cBG);drawMeshArt(SCR_W/2,32);
 spr.drawRect(14,66,SCR_W-28,50,cLINE);spr.drawRect(16,68,SCR_W-32,46,cLINE);
 txt("MESH FIELD",SCR_W/2,74,4,cLINE,cBG,TC_DATUM);
 txt("Vision Master E213 - LoRa 868",SCR_W/2,101,1,cLINE,cBG,TC_DATUM);}
void splash(){drawSplash();epdShow(true);delay(4000);}

// ---------- krytycznie niska bateria: ekran koncowy (e-ink zachowuje obraz bez zasilania) ----------
void drawShutdown(){
 spr.fillSprite(cBG);spr.fillRect(0,0,SCR_W,22,cLINE);txt("BATTERY CRITICAL",SCR_W/2,3,2,cPANEL,cLINE,TC_DATUM);
 char b[24];snprintf(b,sizeof b,"%.2f V",(double)batV);txt(b,SCR_W/2,32,4,cLINE,cBG,TC_DATUM);
 txt("Shutting down",SCR_W/2,68,2,cLINE,cBG,TC_DATUM);
 txt("Connect power to restart",SCR_W/2,92,1,cLINE,cBG,TC_DATUM);
 spr.drawRect(0,0,SCR_W,SCR_H,cLINE);}

void render(){
 spr.fillSprite(cBG);
 switch(screen){case S_HOME:drawHome();break;case S_NOISE:drawNoise();break;case S_SCAN:drawScan();break;case S_MESH:drawMesh();break;case S_NODE:drawNode();break;default:drawSet();}
 drawHeader();
 switch(screen){
  case S_HOME:{char fb[40];snprintf(fb,sizeof fb,"hold KEY: beacon %u",beaconsSent);drawFooter("PRG: next screen",fb);break;}
  case S_NOISE:drawFooter("PRG: next screen","KEY: window  hold: clear");break;
  case S_SCAN:drawFooter("PRG: next screen","KEY: pause  hold: clear");break;
  case S_MESH:drawFooter("KEY: select  PRG: observe","hold KEY: ping");break;
  case S_NODE:drawFooter("KEY: next  PRG: screen","hold KEY: ping");break;
  default:drawFooter("PRG next/hold exit","KEY chg/hold prev");}
 epdMark();}

// ================= NVS =================
void cfgLoad(){prefs.begin("e213",true);size_t n=prefs.getBytesLength("cfg4");if(n>0&&n<=sizeof cfg)prefs.getBytes("cfg4",&cfg,n);prefs.end();if(cfg.ble>1)cfg.ble=0;
 if(cfg.meshPreset>=pre::N)cfg.meshPreset=0;if(cfg.power>=pre::NPWR)cfg.power=2;if(cfg.invert>1)cfg.invert=1;if(cfg.flip>1)cfg.flip=0;if(cfg.pwrHigh>3)cfg.pwrHigh=0;if(cfg.beacon>=pre::NBCN)cfg.beacon=2;if(cfg.autoPing>1)cfg.autoPing=1;if(cfg.wifi>1)cfg.wifi=1;}
void cfgSave(){prefs.begin("e213",false);prefs.putBytes("cfg4",&cfg,sizeof cfg);prefs.end();}

// ================= Ekran / zasilanie =================
// Wlasny identyfikator wezla (losowy, trwaly) uzywany jako nadawca pingow Meshtastic.
void nodeIdLoad(){prefs.begin("e213",true);uint32_t v=0;if(prefs.getBytesLength("nid")==4)prefs.getBytes("nid",&v,4);prefs.end();
 if(!v||v==0xFFFFFFFFu||(v&0xFF)==0||(v&0xFF)==0xFF){do{v=esp_random();}while(!v||v==0xFFFFFFFFu||(v&0xFF)==0||(v&0xFF)==0xFF);prefs.begin("e213",false);prefs.putBytes("nid",&v,4);prefs.end();}
 myId=v;Serial.printf("node id !%08lx\n",(unsigned long)myId);}
// Nazwa wezla: tylko drukowalny ASCII, bez spacji na brzegach, max 12 znakow.
void nameClean(char* o,const char* in,size_t n){size_t k=0;for(size_t i=0;i<n&&k<12;i++){unsigned char c=(unsigned char)in[i];if(c>=32&&c<127)o[k++]=char(c);}o[k]=0;
 while(k&&o[k-1]==' ')o[--k]=0;size_t a=0;while(o[a]==' ')a++;if(a)memmove(o,o+a,strlen(o+a)+1);}
void nameLoad(){char t[16]={0};prefs.begin("e213",true);size_t n=prefs.getBytesLength("nname");if(n>0&&n<sizeof t)prefs.getBytes("nname",t,n);prefs.end();nameClean(nodeName,t,n<sizeof t?n:0);}
void nameSave(){prefs.begin("e213",false);prefs.putBytes("nname",nodeName,strlen(nodeName));prefs.end();}
void posLoad(){uint8_t b[13]={0};prefs.begin("e213",true);size_t n=prefs.getBytesLength("pos");if(n==13)prefs.getBytes("pos",b,13);prefs.end();
 if(n==13&&b[12]==1){memcpy(&gpos.lat,b,4);memcpy(&gpos.lon,b+4,4);memcpy(&gpos.alt,b+8,4);gpos.has=gpos.lat>=-900000000&&gpos.lat<=900000000&&gpos.lon>=-1800000000&&gpos.lon<=1800000000;}if(!gpos.has)gpos=Pos();}
void posSave(){uint8_t b[13];memcpy(b,&gpos.lat,4);memcpy(b+4,&gpos.lon,4);memcpy(b+8,&gpos.alt,4);b[12]=gpos.has?1:0;prefs.begin("e213",false);prefs.putBytes("pos",b,13);prefs.end();}
void posText(char* o,size_t cap){if(gpos.has)snprintf(o,cap,"%.6f,%.6f,%ld",gpos.lat/1e7,gpos.lon/1e7,long(gpos.alt));else o[0]=0;}
// "szerokosc,dlugosc[,wysokosc]" w stopniach dziesietnych; pusty tekst kasuje pozycje. Zwraca false przy bledzie (stan bez zmian).
bool posParse(const char* in,size_t n){char t[48];if(n>=sizeof t)return false;memcpy(t,in,n);t[n]=0;char* p=t;while(*p==' ')++p;
 if(!*p){gpos=Pos();return true;}
 char* e;double la=strtod(p,&e);if(e==p)return false;while(*e==' ')++e;if(*e!=',')return false;p=e+1;double lo=strtod(p,&e);if(e==p)return false;while(*e==' ')++e;long al=0;
 if(*e==','){p=e+1;al=strtol(p,&e,10);while(*e==' ')++e;}if(*e)return false;
 if(!(la>=-90&&la<=90&&lo>=-180&&lo<=180&&al>=-500&&al<=9000))return false;
 gpos.lat=int32_t(lround(la*1e7));gpos.lon=int32_t(lround(lo*1e7));gpos.alt=int32_t(al);gpos.has=true;return true;}
void applyChange(unsigned item){
 cfgSave();
 if(item==0){radioApply(profFor(mode));}
 if(item==1){mtab.clear();pingSt=Ping();meshSelId=0;meshPicked=false;mtab.defHash=mh::chanHash(pre::get(cfg.meshPreset).name,mh::defaultKey(),16);radioApply(profFor(mode));if(mode==M_SCAN)scanReset();}
 if(item==2||item==3){epdChanged=true;epdUrgent=true;}   // wymus odswiezenie ekranu
 if(item==5)beaconSoon(millis());
 if(item==6)lastPingTry=millis()-AUTO_PING_MS+1500;
 if(item==7)bleSet(cfg.ble!=0);
 if(item==8)wifiSet(cfg.wifi!=0);
 bleCfgDirty=true;}
void changeSetting(unsigned i){
 switch(i){
  case 0:cfg.power=(cfg.power+1)%pre::NPWR;break;
  case 1:cfg.meshPreset=(cfg.meshPreset+1)%pre::N;break;
  case 2:cfg.invert^=1;break;
  case 3:cfg.flip^=1;break;
  case 4:cfg.pwrHigh=(cfg.pwrHigh+1)&3;break;
  case 5:cfg.beacon=(cfg.beacon+1)%pre::NBCN;break;
  case 6:cfg.autoPing^=1;break;
  case 7:cfg.ble^=1;break;
  default:cfg.wifi^=1;}
 applyChange(i);}

void gotoScreen(int s){screen=(s+S_COUNT)%S_COUNT;Mode m=modeFor(screen);if(m!=mode)switchMode(m);if(screen==S_SCAN)scanReset();dirty=true;}


// ================= Bluetooth LE: konfigurator =================
// Wlasna usluga GATT (nie Meshtastic): strona tools/configurator.html laczy sie przez Web Bluetooth, podaje PIN pokazany w SETUP
// i ustawia te same parametry co menu SETUP. Zapis konfiguracji bez poprawnego PIN-u jest ignorowany.
#define BLE_UUID_SERVICE "7a1d0001-5e4b-4c8e-9c5a-543139307031"
#define BLE_UUID_CONFIG  "7a1d0002-5e4b-4c8e-9c5a-543139307031"   // odczyt / zapis / powiadomienia: 7 bajtow ustawien
#define BLE_UUID_AUTH    "7a1d0003-5e4b-4c8e-9c5a-543139307031"   // zapis: PIN (ASCII), odczyt: 1 = autoryzowano
#define BLE_UUID_STATUS  "7a1d0004-5e4b-4c8e-9c5a-543139307031"   // odczyt: stan urzadzenia (JSON)
#define BLE_UUID_NAME    "7a1d0006-5e4b-4c8e-9c5a-543139307031"   // odczyt / zapis (po PIN): nazwa wezla, ASCII, max 12 znakow
#define BLE_UUID_POS     "7a1d0007-5e4b-4c8e-9c5a-543139307031"   // odczyt / zapis (po PIN): pozycja GPS jako tekst \"szerokosc,dlugosc,wysokosc\"; pusty = brak
#define BLE_UUID_TIME    "7a1d0008-5e4b-4c8e-9c5a-543139307031"   // zapis (po PIN): czas lokalny jako sekundy od 1970 (ASCII), np. \"1761412345\"
#define BLE_UUID_OPTIONS "7a1d0005-5e4b-4c8e-9c5a-543139307031"   // odczyt: dostepne opcje (JSON)
constexpr unsigned BLE_CFG_LEN=7;
BLEServer* bleSrv=nullptr;BLECharacteristic *bleChCfg=nullptr,*bleChAuth=nullptr,*bleChStat=nullptr,*bleChOpt=nullptr,*bleChName=nullptr,*bleChPos=nullptr,*bleChTime=nullptr;
bool bleInit=false,bleAdv=false;volatile bool bleConn=false,bleAuthed=false,bleKick=false,bleReAdv=false,bleCfgNew=false;volatile int bleBad=0;
char blePosBuf[48];volatile size_t blePosLen=0;volatile bool blePosNew=false;
char bleTimeBuf[16];volatile size_t bleTimeLen=0;volatile bool bleTimeNew=false;
char bleNameBuf[16];volatile size_t bleNameLen=0;volatile bool bleNameNew=false;
uint8_t bleCfgBuf[16];volatile size_t bleCfgLen=0;uint32_t bleStatAt=0;

uint8_t* cfgField(Cfg& c,unsigned i){switch(i){case 0:return &c.power;case 1:return &c.meshPreset;case 2:return &c.invert;case 3:return &c.flip;case 4:return &c.pwrHigh;case 5:return &c.beacon;default:return &c.autoPing;}}
bool cfgValid(const uint8_t* d){return d[0]<pre::NPWR&&d[1]<pre::N&&d[2]<2&&d[3]<2&&d[4]<4&&d[5]<pre::NBCN&&d[6]<2;}
// Zastosowanie zapisu z BLE: tylko zmienione pozycje, kazda przez to samo applyChange co w menu SETUP.
bool bleApplyCfg(const uint8_t* d,size_t n){if(n<BLE_CFG_LEN||!cfgValid(d))return false;for(unsigned i=0;i<BLE_CFG_LEN;i++){uint8_t* f=cfgField(cfg,i);if(*f!=d[i]){*f=d[i];applyChange(i);}}dirty=true;return true;}

struct BleSrvCb:BLEServerCallbacks{
 void onConnect(BLEServer*)override{bleConn=true;bleAuthed=false;bleBad=0;dirty=true;}
 void onDisconnect(BLEServer*)override{bleConn=false;bleAuthed=false;bleReAdv=true;dirty=true;}};
struct BleCfgCb:BLECharacteristicCallbacks{
 void onWrite(BLECharacteristic* c)override{if(!bleAuthed)return;auto v=c->getValue();size_t n=v.length();if(n>sizeof bleCfgBuf)n=sizeof bleCfgBuf;memcpy(bleCfgBuf,(const uint8_t*)v.c_str(),n);bleCfgLen=n;bleCfgNew=true;}};
struct BleNameCb:BLECharacteristicCallbacks{
 void onWrite(BLECharacteristic* c)override{if(!bleAuthed)return;auto v=c->getValue();size_t n=v.length();if(n>sizeof bleNameBuf)n=sizeof bleNameBuf;memcpy(bleNameBuf,v.c_str(),n);bleNameLen=n;bleNameNew=true;}};
struct BlePosCb:BLECharacteristicCallbacks{
 void onWrite(BLECharacteristic* c)override{if(!bleAuthed)return;auto v=c->getValue();size_t n=v.length();if(n>=sizeof blePosBuf)n=sizeof blePosBuf-1;memcpy(blePosBuf,v.c_str(),n);blePosLen=n;blePosNew=true;}};
struct BleTimeCb:BLECharacteristicCallbacks{
 void onWrite(BLECharacteristic* c)override{if(!bleAuthed)return;auto v=c->getValue();size_t n=v.length();if(n>=sizeof bleTimeBuf)n=sizeof bleTimeBuf-1;memcpy(bleTimeBuf,v.c_str(),n);bleTimeLen=n;bleTimeNew=true;}};
struct BleAuthCb:BLECharacteristicCallbacks{
 void onWrite(BLECharacteristic* c)override{auto v=c->getValue();char want[12];snprintf(want,sizeof want,"%06lu",(unsigned long)blePin);
  if(blePin&&v.length()==6&&memcmp(v.c_str(),want,6)==0){bleAuthed=true;bleBad=0;}else{bleAuthed=false;if(++bleBad>=3)bleKick=true;}
  uint8_t a=bleAuthed?1:0;c->setValue(&a,1);}};
BleSrvCb bleSrvCb;BleCfgCb bleCfgCb;BleNameCb bleNameCb;BlePosCb blePosCb;BleTimeCb bleTimeCb;BleAuthCb bleAuthCb;

void bleOptionsJson(char* o,size_t cap){size_t k=0;
 k+=snprintf(o+k,cap-k,"{\"presets\":[");for(unsigned i=0;i<pre::N&&k<cap;i++)k+=snprintf(o+k,cap-k,"%s[\"%s\",%u,%g,%.3f]",i?",":"",pre::get(i).name,pre::get(i).sf,(double)pre::get(i).bw,(double)pre::meshFreq(i));   // [nazwa, SF, BW kHz, MHz kanalu]
 k+=snprintf(o+k,cap-k,"],\"powers\":[");for(unsigned i=0;i<pre::NPWR&&k<cap;i++)k+=snprintf(o+k,cap-k,"%s%d",i?",":"",pre::linkPower(i));
 k+=snprintf(o+k,cap-k,"],\"beacons\":[");for(unsigned i=0;i<pre::NBCN&&k<cap;i++)k+=snprintf(o+k,cap-k,"%s%u",i?",":"",pre::beaconMin(i));
 snprintf(o+k,cap-k,"]}");}
void bleStatusJson(char* o,size_t cap,uint32_t now){nz::Stats s=nzMesh.stats(60);char pt[40];posText(pt,sizeof pt);const Prof p=profFor(mode);
 snprintf(o,cap,"{\"node\":\"%08lx\",\"name\":\"%s\",\"mode\":%d,\"mhz\":%.3f,\"sf\":%u,\"bw\":%g,\"pwr\":%d,\"pkts\":%lu,\"crc\":%lu,\"hasRx\":%s,\"rssi\":%d,\"snr\":%.1f,\"noise\":%d,\"floor\":%d,\"peak\":%d,\"busy\":%.0f,\"chg\":%s,\"bat\":%.2f,\"batPct\":%d,\"duty\":%.2f,\"nodes\":%u,\"radio\":%s,\"beacons\":%u,\"clk\":%lu,\"pos\":\"%s\",\"wifi\":%s,\"cyd\":%s}",
  (unsigned long)myId,nodeName,int(mode),(double)p.mhz,p.sf,(double)p.bw,int(p.pwr),(unsigned long)mtab.packets,(unsigned long)crcErrors,hasRx?"true":"false",lastRssi,(double)lastSnr,
  s.has?s.now:0,s.has?s.floor:0,s.has?s.peak:0,s.has?(double)s.busy:0.0,batChg?"true":"false",(double)batV,batPct,(double)duty.pct(now),mtab.used,radioOk?"true":"false",beaconsSent,(unsigned long)(time(nullptr)>=1700000000?time(nullptr):0),pt,wifiOn?"true":"false",apiLinked()?"true":"false");}
void bleStart(){
 if(!bleInit){char dn[24];snprintf(dn,sizeof dn,"E213-%s",nodeName[0]?nodeName:"FieldTest");BLEDevice::init(dn);bleSrv=BLEDevice::createServer();bleSrv->setCallbacks(&bleSrvCb);BLEService* sv=bleSrv->createService(BLE_UUID_SERVICE);
  bleChCfg=sv->createCharacteristic(BLE_UUID_CONFIG,BLECharacteristic::PROPERTY_READ|BLECharacteristic::PROPERTY_WRITE|BLECharacteristic::PROPERTY_NOTIFY);bleChCfg->addDescriptor(new BLE2902());bleChCfg->setCallbacks(&bleCfgCb);
  bleChAuth=sv->createCharacteristic(BLE_UUID_AUTH,BLECharacteristic::PROPERTY_READ|BLECharacteristic::PROPERTY_WRITE);bleChAuth->setCallbacks(&bleAuthCb);
  bleChStat=sv->createCharacteristic(BLE_UUID_STATUS,BLECharacteristic::PROPERTY_READ);
  bleChName=sv->createCharacteristic(BLE_UUID_NAME,BLECharacteristic::PROPERTY_READ|BLECharacteristic::PROPERTY_WRITE);bleChName->setCallbacks(&bleNameCb);bleChName->setValue((uint8_t*)nodeName,strlen(nodeName));
  {char pt[40];posText(pt,sizeof pt);bleChPos=sv->createCharacteristic(BLE_UUID_POS,BLECharacteristic::PROPERTY_READ|BLECharacteristic::PROPERTY_WRITE);bleChPos->setCallbacks(&blePosCb);bleChPos->setValue((uint8_t*)pt,strlen(pt));}
  bleChTime=sv->createCharacteristic(BLE_UUID_TIME,BLECharacteristic::PROPERTY_WRITE);bleChTime->setCallbacks(&bleTimeCb);
  bleChOpt=sv->createCharacteristic(BLE_UUID_OPTIONS,BLECharacteristic::PROPERTY_READ);
  static char opt[560];bleOptionsJson(opt,sizeof opt);bleChOpt->setValue((uint8_t*)opt,strlen(opt));uint8_t z=0;bleChAuth->setValue(&z,1);sv->start();bleInit=true;}
 blePin=123456;bleAuthed=false;bleBad=0;bleCfgDirty=true;
 BLEAdvertising* adv=BLEDevice::getAdvertising();adv->addServiceUUID(BLE_UUID_SERVICE);adv->setScanResponse(true);BLEDevice::startAdvertising();bleAdv=true;
 Serial.printf("BLE on, PIN %06lu\n",(unsigned long)blePin);}
void bleStop(){if(!bleInit)return;BLEDevice::getAdvertising()->stop();bleAdv=false;if(bleConn&&bleSrv)bleSrv->disconnect(bleSrv->getConnId());blePin=0;bleAuthed=false;Serial.println("BLE off");}
void bleSet(bool on){if(on)bleStart();else bleStop();}
void bleTick(uint32_t now){
 if(!bleInit)return;
 if(bleKick){bleKick=false;if(bleSrv)bleSrv->disconnect(bleSrv->getConnId());}
 static uint32_t reAdvAt=0;static bool reAdvPend=false;if(bleReAdv){bleReAdv=false;reAdvAt=now+300;reAdvPend=true;}
 if(reAdvPend&&int32_t(now-reAdvAt)>=0){reAdvPend=false;if(cfg.ble&&bleAdv)BLEDevice::startAdvertising();}
 if(bleCfgNew){bleCfgNew=false;uint8_t tmp[16];size_t n=bleCfgLen;if(n>sizeof tmp)n=sizeof tmp;memcpy(tmp,bleCfgBuf,n);if(!bleApplyCfg(tmp,n))bleCfgDirty=true;}   // niepoprawny zapis: odeslij biezace ustawienia
 if(bleNameNew){bleNameNew=false;char t[16];size_t n=bleNameLen;if(n>sizeof t)n=sizeof t;memcpy(t,bleNameBuf,n);nameClean(nodeName,t,n);nameSave();bleChName->setValue((uint8_t*)nodeName,strlen(nodeName));beaconSoon(now);dirty=true;Serial.printf("node name: %s\n",nodeName);}
 if(blePosNew){blePosNew=false;size_t n=blePosLen;if(n>=sizeof blePosBuf)n=sizeof blePosBuf-1;char t[48];memcpy(t,blePosBuf,n);if(posParse(t,n)){posSave();beaconSoon(now);Serial.println(gpos.has?"position set":"position cleared");}
  char pt[40];posText(pt,sizeof pt);bleChPos->setValue((uint8_t*)pt,strlen(pt));dirty=true;}
 if(bleTimeNew){bleTimeNew=false;size_t n=bleTimeLen;if(n>=sizeof bleTimeBuf)n=sizeof bleTimeBuf-1;char t[16];memcpy(t,bleTimeBuf,n);t[n]=0;char* e;unsigned long v=strtoul(t,&e,10);if(e!=t&&!*e&&v>=1700000000UL&&v<4000000000UL){clockSet((uint32_t)v);Serial.printf("clock set: %lu\n",v);}}
 if(bleCfgDirty){bleCfgDirty=false;uint8_t v[BLE_CFG_LEN];for(unsigned i=0;i<BLE_CFG_LEN;i++)v[i]=*cfgField(cfg,i);bleChCfg->setValue(v,BLE_CFG_LEN);if(bleConn)bleChCfg->notify();}
 if(now-bleStatAt>=1000){bleStatAt=now;static char js[640];bleStatusJson(js,sizeof js,now);bleChStat->setValue((uint8_t*)js,strlen(js));}}

// ================= Przyciski =================
struct Btn{int pin;bool down,lng;uint32_t t;Btn(int p):pin(p),down(false),lng(false),t(0){}};
Btn btnA(PIN_BTN_A),btnB(PIN_BTN_B);
int btnPoll(Btn& b,uint32_t now){   // 0 nic, 1 krotkie (po puszczeniu), 2 dlugie (po 700 ms trzymania)
 bool d=digitalRead(b.pin)==LOW;
 if(d&&!b.down){b.down=true;b.lng=false;b.t=now;return 0;}
 if(d&&b.down&&!b.lng&&now-b.t>=700){b.lng=true;return 2;}
 if(!d&&b.down){b.down=false;if(!b.lng&&now-b.t>=30)return 1;}
 return 0;}
void onButtons(uint32_t now){
 int a=btnPoll(btnA,now),k=btnPoll(btnB,now);if(a||k)epdUrgent=true;
 if(a){dirty=true;
  if(screen==S_SET){if(a==1)setSel=(setSel+1)%SET_N;else gotoScreen(S_HOME);}
  else gotoScreen(screen+(a==1?1:-1));}
 if(k){dirty=true;
  if(screen==S_SET){if(k==1)changeSetting(setSel);else setSel=(setSel+SET_N-1)%SET_N;}
  else if(k==1){switch(screen){
    case S_NOISE:noiseWin=(noiseWin+1)%3;break;
    case S_SCAN:scanPaused=!scanPaused;break;
    case S_MESH:case S_NODE:{unsigned idx[mh::Table::MAXN];unsigned n=mtab.order(now,idx);if(n){unsigned cur=0;for(unsigned i=0;i<n;i++)if(mtab.n[idx[i]].id==meshSelId)cur=i;if(!meshPicked)meshPicked=true;else cur=(cur+1)%n;meshSelId=mtab.n[idx[cur]].id;pingSt.st=PG_IDLE;lastPingTry=now-AUTO_PING_MS+1500;}}break;}}
  else{switch(screen){
    case S_NOISE:nzCur().reset();break;
    case S_SCAN:scanReset();break;
    case S_HOME:beaconNow(now);break;
    case S_MESH:case S_NODE:pingSend(now);break;}}}}

// ================= setup / loop =================
// ---------- krytycznie niska bateria: wylacz (deep sleep), wznow po podlaczeniu zasilania ----------
// Brak znanego pinu statusu ladowania, wiec co 30 s budzimy sie, mierzymy napiecie i startujemy dopiero, gdy wyraznie wzroslo (ladowarka) lub bateria jest naladowana.
// E-ink zachowuje obraz bez zasilania: komunikat zostaje na ekranie w czasie uspienia.
void vextOff(){digitalWrite(PIN_VEXT,LOW);gpio_hold_en((gpio_num_t)PIN_VEXT);gpio_deep_sleep_hold_en();}
void batShutdown(){
 Serial.println("battery critical: shutting down");
 drawShutdown();epdShow(true);epdhw::hibernate();
 radio.sleep();wifiStop();
 rtcLowOff=true;rtcMinV=batV;batLowCnt=0;vextOff();
 esp_sleep_enable_timer_wakeup(30ULL*1000000ULL);esp_deep_sleep_start();}
// Po obudzeniu timerem: true = kontynuuj start, false = wrocic do snu
bool batWakeCheck(){
 gpio_deep_sleep_hold_dis();gpio_hold_dis((gpio_num_t)PIN_VEXT);
 if(!rtcLowOff)return true;
 if(esp_sleep_get_wakeup_cause()!=ESP_SLEEP_WAKEUP_TIMER){rtcLowOff=false;return true;}   // reset / przycisk: normalny start
 pinMode(PIN_BAT_CTRL,OUTPUT);batRead();
 if(batV<rtcMinV)rtcMinV=batV;
 if(batV>=rtcMinV+0.06f||batV>=3.75f){rtcLowOff=false;return true;}
 pinMode(PIN_VEXT,OUTPUT);vextOff();
 esp_sleep_enable_timer_wakeup(30ULL*1000000ULL);esp_deep_sleep_start();return false;}

void setup(){
 if(!batWakeCheck())return;
 Serial.begin(115200);delay(200);
 pinMode(PIN_BTN_A,INPUT_PULLUP);pinMode(PIN_BTN_B,INPUT_PULLUP);pinMode(PIN_LORA_DIO1,INPUT);
 pinMode(PIN_VEXT,OUTPUT);digitalWrite(PIN_VEXT,HIGH);pinMode(PIN_BAT_CTRL,OUTPUT);digitalWrite(PIN_BAT_CTRL,LOW);delay(100);   // e-ink wymaga chwili na zasilanie
 cfgLoad();
 epdhw::begin();   // wykrywa wersje panelu i inicjalizuje e-ink (VEXT jest juz wlaczony)
 mtab.defHash=mh::chanHash(pre::get(cfg.meshPreset).name,mh::defaultKey(),16);
 nodeIdLoad();nameLoad();posLoad();nextBeaconAt=millis()+30000;if(cfg.ble)bleStart();radioInit();batRead();if(cfg.wifi)wifiStart();splash();
 Serial.println("t_ms,mode,noise_now,noise_floor,noise_peak,busy_pct,mesh_pkts,crc_err");
 render();}

void loop(){
 uint32_t now=millis();
 onButtons(now);bleTick(now);txPoll(now);rxPoll(now);apiTick(now);pingTick(now);beaconTick(now);noiseTick(now);
 static uint32_t lastScan=0;if(mode==M_SCAN&&now-lastScan>=40){lastScan=now;scanTick();}
 if(now-lastBatAt>=10000||!lastBatAt){lastBatAt=now?now:1;batRead();dirty=true;if(batLowCnt>=BAT_OFF_READS)batShutdown();}
 static uint32_t lastCsv=0;if(now-lastCsv>=5000){lastCsv=now;nz::Stats s=nzCur().stats(60);
  Serial.printf("%lu,%d,%d,%d,%d,%.0f,%lu,%lu\n",(unsigned long)now,int(mode),s.has?s.now:0,s.has?s.floor:0,s.has?s.peak:0,s.has?s.busy:0.f,(unsigned long)mtab.packets,(unsigned long)crcErrors);}
 if((dirty&&now-lastDraw>=80)||now-lastDraw>=500){lastDraw=now;dirty=false;render();}
 epdTick(now);
 delay(1);}
