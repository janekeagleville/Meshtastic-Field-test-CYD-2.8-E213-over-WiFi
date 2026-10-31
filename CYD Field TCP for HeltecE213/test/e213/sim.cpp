// Symulacja calego firmware na hoscie: wirtualne radio SX1262, przyciski, zegar, ekran (rejestr operacji rysowania).
#include <cassert>
#include <map>
#include <cstdlib>
#include <cstdio>
#include "../../stubs/e213/Arduino.h"
#include "../../stubs/e213/RadioLib.h"
#include "../../stubs/e213/Preferences.h"
#include "../../stubs/e213/BLEDevice.h"
#include "../../stubs/e213/esp_sleep.h"
#include "../../stubs/e213/WiFi.h"
#include "../../include/e213/config.h"
namespace epdhw{int shows=0,fulls=0,hibernates=0;uint8_t last[(SCR_W+7)/8*SCR_H];bool lastInv=false,lastFlip=false,lastPartial=false,present=true;}
SimRadio gSim;SimNet gNet;WiFiClass WiFi;
int gSleepCalls=0;esp_sleep_wakeup_cause_t gWakeCause=ESP_SLEEP_WAKEUP_UNDEFINED;
static unsigned long gNow=0;static uint32_t gRand=1;static bool gBtn[64];static int gPinOut[64];static std::map<std::string,std::vector<uint8_t>> gNvs;
unsigned long millis(){return gNow;}void delay(unsigned long){}void delayMicroseconds(unsigned){}uint32_t esp_random(){return gRand++*2654435761u;}
void pinMode(int,int){}void digitalWrite(int p,int v){gPinOut[p]=v;}
int digitalRead(int p){if(p==14)return gSim.irq?1:0;return gBtn[p]?0:1;}
size_t strlcpy(char*d,const char*s,size_t n){size_t l=strlen(s);if(n){size_t k=l<n-1?l:n-1;memcpy(d,s,k);d[k]=0;}return l;}
void analogSetPinAttenuation(int,int){}static uint32_t gAdcMv=780;uint32_t analogReadMilliVolts(int){return gAdcMv;}
void SerialC::begin(unsigned){}int SerialC::printf(const char*,...){return 0;}void SerialC::println(const char*){}SerialC Serial;
static std::string gNs;
void Preferences::begin(const char* n,bool){gNs=n;}void Preferences::end(){}
size_t Preferences::getBytesLength(const char* k){auto i=gNvs.find(gNs+"/"+k);return i==gNvs.end()?0:i->second.size();}
size_t Preferences::getBytes(const char* k,void* b,size_t n){auto i=gNvs.find(gNs+"/"+k);if(i==gNvs.end())return 0;size_t m=std::min(n,i->second.size());memcpy(b,i->second.data(),m);return m;}
size_t Preferences::putBytes(const char* k,const void* b,size_t n){gNvs[gNs+"/"+k].assign((const uint8_t*)b,(const uint8_t*)b+n);return n;}
#include "../../src/e213/main.cpp"

static void run(unsigned ms,void(*hook)()=nullptr){for(unsigned i=0;i<ms;i++){++gNow;if(hook)hook();loop();}}
static void press(int pin,bool lng){gBtn[pin]=true;run(lng?760:80);gBtn[pin]=false;run(60);}
static void pressA(bool l=false){press(0,l);}static void pressB(bool l=false){press(21,l);}
static size_t shown=0;
static void dump(const char* name){FILE* f=fopen((std::string("shots/")+name+".pbm").c_str(),"wb");
 fprintf(f,"P4\n%d %d\n",SCR_W,SCR_H);fwrite(spr.getBuffer(),1,EPD_BYTES,f);fclose(f);}
static int overlaps(const char* name){int n=0;auto& T=spr.texts;for(size_t i=0;i<T.size();i++)for(size_t j=i+1;j<T.size();j++){auto&a=T[i];auto&b=T[j];
  if(a.x<b.x+b.w&&b.x<a.x+a.w&&a.y<b.y+b.h&&b.y<a.y+a.h){++n;fprintf(stderr,"  [%s] OVERLAP '%s'(%d,%d %dx%d) x '%s'(%d,%d %dx%d)\n",name,a.s.c_str(),a.x,a.y,a.w,a.h,b.s.c_str(),b.x,b.y,b.w,b.h);}}return n;}
static int gOverlaps=0,gOob=0;
static void checkText(const char* name){gOverlaps+=overlaps(name);if(spr.textOob){fprintf(stderr,"  [%s] text outside screen: %d\n",name,spr.textOob);gOob+=spr.textOob;}spr.texts.clear();spr.textOob=0;}
static void shot(const char* name){spr.texts.clear();spr.textOob=0;render();dump(name);checkText(name);}

// Rozszyfrowanie broadcastu E213: zwraca port, wypelnia payload; sprawdza naglowek (broadcast, hop 3, bez ACK, kanal domyslny).
static unsigned bcDecode(const std::vector<uint8_t>& pk,std::vector<uint8_t>& payload){mh::Hdr h;assert(mh::parse(pk.data(),pk.size(),h));
 assert(h.to==0xFFFFFFFFu&&h.from==myId&&!h.wantAck&&h.hopLimit==3&&h.hopStart==3&&h.chan==mtab.defHash);
 std::vector<uint8_t> pl(pk.size()-16);mh::ctrCrypt(h.id,h.from,pk.data()+16,pl.data(),pl.size());mesh::Reader r(pl.data(),pl.size());mesh::Field f;unsigned port=0;payload.clear();
 while(r.next(f)){if(f.tag==1&&f.wire==0)port=unsigned(f.value);if(f.tag==2&&f.wire==2)payload.assign(f.bytes,f.bytes+f.size);}assert(r.ok());return port;}
int main(){
 system("mkdir -p shots");
 setup();
 // --- start: parametry radia
 assert(radioOk);assert(cfg.meshPreset==3&&fabsf(gSim.freq-869.525f)<1e-3&&gSim.bw==250&&gSim.sf==9&&gSim.cr==5&&gSim.sync==0x2B&&gSim.pre==16&&gSim.pwr==14);   // domyslnie MediumFast, nasluch Meshtastic od startu
 cfg.meshPreset=0;applyChange(1);   // reszta testow zaklada LongFast
 assert(gSim.sf==11);
 assert(mode==M_MESH&&screen==S_HOME);shot("home_start");
 // --- beacon: pierwszy NODEINFO po ok. 30 s od startu (bez wpisanej nazwy: "E213 xxxx")
 {assert(gSim.tx.empty());run(31000);assert(gSim.tx.size()==1&&beaconsSent==1);std::vector<uint8_t> pl;assert(bcDecode(gSim.tx[0],pl)==4);
  mesh::Reader u(pl.data(),pl.size());mesh::Field f;char id[16]={},lng[48]={},sh[8]={};unsigned hw=0,role=9;while(u.next(f)){if(f.tag==1)mesh::copyText(id,sizeof id,f);if(f.tag==2)mesh::copyText(lng,sizeof lng,f);if(f.tag==3)mesh::copyText(sh,sizeof sh,f);if(f.tag==5)hw=unsigned(f.value);if(f.tag==7)role=unsigned(f.value);}
  char want[16];snprintf(want,sizeof want,"!%08lx",(unsigned long)myId);char wl[16],ws[8];snprintf(ws,sizeof ws,"%04lx",(unsigned long)(myId&0xFFFF));snprintf(wl,sizeof wl,"E213 %s",ws);
  assert(!strcmp(id,want)&&!strcmp(lng,wl)&&!strcmp(sh,ws)&&hw==255&&role==1);fprintf(stderr,"beacon: %s / %s / %s\n",id,lng,sh);
  run(60000);assert(gSim.tx.size()==1);}   // nastepny dopiero po odstepie (30 min)
 // --- beacon z nazwa i pozycja
 {nodeName[0]=0;strcpy(nodeName,"DACHA-1");assert(posParse("52.2297,21.0122,110",19)&&gpos.has&&gpos.lat==522297000&&gpos.lon==210122000&&gpos.alt==110);
  size_t t0=gSim.tx.size();beaconNow(gNow);run(1500);assert(gSim.tx.size()==t0+1);run(5000);assert(gSim.tx.size()==t0+2&&beaconsSent==2);
  std::vector<uint8_t> pl;assert(bcDecode(gSim.tx[t0],pl)==4);mesh::Reader u(pl.data(),pl.size());mesh::Field f;char lng[48]={},sh[8]={};while(u.next(f)){if(f.tag==2)mesh::copyText(lng,sizeof lng,f);if(f.tag==3)mesh::copyText(sh,sizeof sh,f);}assert(!strcmp(lng,"DACHA-1")&&!strcmp(sh,"DACH"));
  assert(bcDecode(gSim.tx[t0+1],pl)==3);mesh::Reader q(pl.data(),pl.size());int32_t la=0,lo=0,al=0;unsigned src=0;while(q.next(f)){if(f.tag==1)la=int32_t(uint32_t(f.value));if(f.tag==2)lo=int32_t(uint32_t(f.value));if(f.tag==3)al=int32_t(f.value);if(f.tag==5)src=unsigned(f.value);}
  assert(la==522297000&&lo==210122000&&al==110&&src==1);
  // wysokosc ujemna, granice, bledne dane
  assert(posParse("-33.5,-70.25,-20",16)&&gpos.alt==-20&&gpos.lat==-335000000);{uint8_t b[32];size_t n=mh::buildPosition(b,sizeof b,gpos.lat,gpos.lon,gpos.alt);mesh::Reader z(b,n);int64_t av=0;while(z.next(f))if(f.tag==3)av=int64_t(f.value);assert(n==23&&int32_t(av)==-20&&av==int64_t(-20));}
  Pos keep=gpos;assert(!posParse("91,0",4)&&!posParse("10,181",6)&&!posParse("abc",3)&&!posParse("10,20,99999",11)&&!posParse("10;20",5)&&!posParse("10,20,5x",8));assert(memcmp(&keep,&gpos,sizeof gpos)==0);
  assert(posParse("  ",2)&&!gpos.has);assert(posParse("52.2297, 21.0122",16)&&gpos.alt==0&&gpos.has);gpos=Pos();nodeName[0]=0;
  // duty: beacon nie wychodzi, gdy limit wyczerpany, i probuje ponownie pozniej
  duty.add(gNow,3600000);size_t t1=gSim.tx.size();beaconNow(gNow);run(2000);assert(gSim.tx.size()==t1&&beaconsSent==2);duty=pre::Duty();run(31000);assert(gSim.tx.size()==t1+1&&beaconsSent==3);}
 cfg.beacon=0;{size_t t1=gSim.tx.size();nextBeaconAt=gNow;run(5000);assert(gSim.tx.size()==t1);beaconNow(gNow);run(2000);assert(gSim.tx.size()==t1+1);}cfg.beacon=2;nextBeaconAt=gNow+1800000;   // wylaczony beacon nie nadaje sam, reczny dziala
 gSim.tx.clear();beaconsSent=0;
 // --- ustawienia: wszystkie pozycje
 gotoScreen(S_SET);for(unsigned i=0;i<SET_N;i++){setSel=i;shot(("set"+std::to_string(i)).c_str());}
 // --- moc nadawania ograniczana limitem pasma (868,0-868,6 MHz: 14 dBm); LongMod/LiteFast leza gdzie indziej
 assert(pre::powerLimit(868.1f)==14&&pre::powerLimit(869.525f)==22);

 // --- NOISE: podloga -112, impuls -60 raz na 30 s
 gotoScreen(S_NOISE);nzMesh.reset();gSim.noise=-112;
 auto burst=[](){static unsigned c=0;++c;gSim.noise=(c%30000<400)?-60.f:-112.f+float((c*7)%5)-2;};
 run(700000,burst);{nz::Stats s=nzMesh.stats(600);fprintf(stderr,"noise10m: now=%d floor=%d peak=%d busy=%.1f\n",s.now,s.floor,s.peak,s.busy);assert(s.has&&s.floor<=-109&&s.floor>=-117&&s.peak==-60);}
 shot("noise_1m");pressB();shot("noise_10m");pressB();shot("noise_1h");pressB();pressB(true);shot("noise_empty");gSim.noise=-112;run(5000);shot("noise_few");
 // --- SCAN
 gotoScreen(S_SCAN);auto scanHook=[](){gSim.noise=-110;if(scanIdx==37||scanIdx==38)gSim.noise=-70;};run(4000,scanHook);shot("scan");
 pressB();assert(scanPaused);pressB();pressB(true);shot("scan_cleared");run(8000,scanHook);
 assert(fabsf(gSim.freq-(SCAN_START+((scanIdx+SCAN_N-1)%SCAN_N)*SCAN_STEP))<0.06f);
 int pk=0;for(unsigned i=0;i<SCAN_N;i++)if(scanPeak[i]>-100)++pk;fprintf(stderr,"scan peaks above -100: %d\n",pk);assert(pk>=1);
 // --- MESH: wstrzykniecie pakietow (NODEINFO z domyslnym kluczem + zaszyfrowany inny kanal)
 gotoScreen(S_MESH);assert(mode==M_MESH&&fabsf(gSim.freq-869.525f)<1e-3&&gSim.sync==0x2B&&gSim.pre==16&&gSim.sf==11);shot("mesh_empty");
 auto mkpkt=[](uint32_t from,uint32_t id,uint8_t chan,const char* lname,const char* sname,bool nodeinfo){std::vector<uint8_t> p(16,0);uint32_t to=0xffffffff;memcpy(&p[0],&to,4);memcpy(&p[4],&from,4);memcpy(&p[8],&id,4);p[12]=uint8_t(2|(3<<5));p[13]=chan;
  std::vector<uint8_t> pl;if(nodeinfo){std::vector<uint8_t> u;u.push_back(0x12);u.push_back(uint8_t(strlen(lname)));u.insert(u.end(),lname,lname+strlen(lname));u.push_back(0x1a);u.push_back(uint8_t(strlen(sname)));u.insert(u.end(),sname,sname+strlen(sname));
   pl={0x08,0x04,0x12,uint8_t(u.size())};pl.insert(pl.end(),u.begin(),u.end());}else pl={0x08,0x01,0x12,0x05,'h','e','l','l','o'};
  uint8_t nonce[16]={};for(int i=0;i<4;i++)nonce[i]=uint8_t(id>>(8*i));for(int i=0;i<4;i++)nonce[8+i]=uint8_t(from>>(8*i));mh::Aes128 a;a.key(mh::defaultKey());std::vector<uint8_t> c(pl.size());a.ctr(nonce,pl.data(),c.data(),pl.size());p.insert(p.end(),c.begin(),c.end());return p;};
 for(int i=0;i<12;i++){char nm[40];snprintf(nm,sizeof nm,"Wezel testowy numer %d dlugi",i);auto p=mkpkt(0x1000+i,500+i,8,nm,"WT",true);gSim.rx.push_back(SimPkt{p,-80.f-i*3,6.f-i*0.7f,0});gSim.arm();run(200);}
 auto p2=mkpkt(0x2222,900,77,"x","y",false);gSim.rx.push_back(SimPkt{p2,-118,-12,0});gSim.arm();run(200);
 gSim.rx.push_back(SimPkt{std::vector<uint8_t>(40,0x55),-100,0,RADIOLIB_ERR_CRC_MISMATCH});gSim.arm();run(200);
 gSim.rx.push_back(SimPkt{std::vector<uint8_t>(5,0x55),-100,0,0});gSim.arm();run(200);   // za krotki
 fprintf(stderr,"mesh: pkts=%lu nodes=%u crc=%lu\n",(unsigned long)mtab.packets,mtab.used,(unsigned long)crcErrors);assert(mtab.used==13&&crcErrors==1&&mtab.packets==13);
 {unsigned idx[40];unsigned n=mtab.order(gNow,idx);int named=0;for(unsigned i=0;i<n;i++)if(mtab.n[idx[i]].name[0])++named;assert(named==12);}
 shot("mesh_list");pressB();pressB();pressB();shot("mesh_scrolled");for(int i=0;i<10;i++)pressB();shot("mesh_wrap");
 // --- ping wezla Meshtastic
 cfg.autoPing=0;
 {unsigned idx[40];unsigned n=mtab.order(gNow,idx);assert(n==13);uint32_t tg=meshSelId;assert(tg);size_t t0=gSim.tx.size();
  auto mkack=[&](uint32_t from,uint32_t pid,uint32_t req,int err){std::vector<uint8_t> d={0x08,0x05};if(err>=0){d.push_back(0x12);d.push_back(2);d.push_back(0x18);d.push_back(uint8_t(err));}d.push_back(0x35);for(int i=0;i<4;i++)d.push_back(uint8_t(req>>(8*i)));
   std::vector<uint8_t> p(16,0);for(int i=0;i<4;i++){p[i]=uint8_t(myId>>(8*i));p[4+i]=uint8_t(from>>(8*i));p[8+i]=uint8_t(pid>>(8*i));}p[12]=0x63;p[13]=8;std::vector<uint8_t> c(d.size());mh::ctrCrypt(pid,from,d.data(),c.data(),d.size());p.insert(p.end(),c.begin(),c.end());return p;};
  shot("mesh_before_ping");pressB(true);assert(gSim.tx.size()==t0+1&&pingSt.st==PG_WAIT&&pingSt.target==tg);
  {auto& pk=gSim.tx.back();mh::Hdr h;assert(pk.size()==23&&mh::parse(pk.data(),pk.size(),h));assert(h.to==tg&&h.from==myId&&h.wantAck&&h.hopLimit==0&&h.chan==8&&h.id==pingSt.id);assert(gSim.sync==0x2B&&fabsf(gSim.freq-869.525f)<1e-3);}
  assert(gSim.rxMode&&!gSim.txMode);shot("mesh_ping_wait");pressB(true);assert(gSim.tx.size()==t0+1);   // drugi ping w trakcie oczekiwania nie wychodzi
  // obca odpowiedz, zly request_id i odpowiedz od innego wezla sa ignorowane
  {auto a=mkack(tg,0x5001,pingSt.id+1,0);gSim.rx.push_back(SimPkt{a,-90,4,0});gSim.arm();run(100);assert(pingSt.st==PG_WAIT);
   unsigned idx2[40];mtab.order(gNow,idx2);uint32_t other=meshSelId==0x1000?0x1001:0x1000;auto b2=mkack(other,0x5002,pingSt.id,0);gSim.rx.push_back(SimPkt{b2,-90,4,0});gSim.arm();run(100);assert(pingSt.st==PG_WAIT);}
  run(1500);{auto a=mkack(tg,0x5003,pingSt.id,0);gSim.rx.push_back(SimPkt{a,-97,3.25f,0});gSim.arm();}run(100);
  assert(pingSt.st==PG_ACK&&pingSt.acks==1&&pingSt.sent==1&&pingSt.rssi==-97&&pingSt.hops==0&&pingSt.rtt>=1500&&pingSt.rtt<4000);fprintf(stderr,"ping: ack rssi=%d snr=%.2f rtt=%lu hops=%d\n",pingSt.rssi,pingSt.snr,(unsigned long)pingSt.rtt,pingSt.hops);shot("mesh_ping_ack");
  // drugi ping bez odpowiedzi -> timeout
  pressB(true);assert(pingSt.st==PG_WAIT&&pingSt.sent==2);run(13000);assert(pingSt.st==PG_TIMEOUT&&pingSt.acks==1);shot("mesh_ping_timeout");
  // NAK
  pressB(true);{auto a=mkack(tg,0x5004,pingSt.id,5);gSim.rx.push_back(SimPkt{a,-100,-3,0});gSim.arm();}run(100);assert(pingSt.st==PG_NAK&&pingSt.err==5);shot("mesh_ping_nak");
  // limit wypelnienia blokuje wyslanie
  duty.add(gNow,3600000);pressB(true);assert(pingSt.st==PG_BLOCKED);shot("mesh_ping_blocked");duty=pre::Duty();
  // zmiana wybranego wezla zeruje liczniki
  pressB();pressB(true);assert(pingSt.sent==1&&pingSt.acks==0);run(13000);
  // zmiana presetu Mesh czysci stan
  gotoScreen(S_SET);setSel=1;pressB();assert(pingSt.target==0&&mtab.used==0&&meshSelId==0);cfg.meshPreset=0;applyChange(1);gotoScreen(S_MESH);run(300);}
 shot("mesh_cleared");
 // ekran szczegolow wezla
 {gotoScreen(S_MESH);for(int i=0;i<3;i++){auto p=mkpkt(0x1000,700+i,8,"","",false);gSim.rx.push_back(SimPkt{p,-85.f-i*4,5.f-i,0});gSim.arm();run(200);}
  gotoScreen(S_NODE);assert(mode==M_MESH);shot("node_empty_sel");pressB();shot("node_next");}
 // --- ping automatyczny co 10 s po wybraniu wezla
 {cfg.autoPing=1;gotoScreen(S_MESH);run(300);meshPicked=false;pingSt=Ping();size_t t0=gSim.tx.size();run(25000);assert(gSim.tx.size()==t0);   // bez wyboru nic nie wychodzi
  pressB();assert(meshPicked&&meshSelId);uint32_t tg=meshSelId;shot("mesh_autoping_selected");run(1700);assert(gSim.tx.size()==t0+1&&pingSt.target==tg&&pingSt.sent==1);
  auto ack=[&](uint32_t from,uint32_t pid,uint32_t req){std::vector<uint8_t> d={0x08,0x05,0x35};for(int i=0;i<4;i++)d.push_back(uint8_t(req>>(8*i)));std::vector<uint8_t> p(16,0);for(int i=0;i<4;i++){p[i]=uint8_t(myId>>(8*i));p[4+i]=uint8_t(from>>(8*i));p[8+i]=uint8_t(pid>>(8*i));}p[12]=0x63;p[13]=8;std::vector<uint8_t> c(d.size());mh::ctrCrypt(pid,from,d.data(),c.data(),d.size());p.insert(p.end(),c.begin(),c.end());return p;};
  gSim.rx.push_back(SimPkt{ack(tg,0x7001,pingSt.id),-90,4,0});gSim.arm();run(200);assert(pingSt.st==PG_ACK&&pingSt.acks==1);
  run(8500);assert(gSim.tx.size()==t0+1);run(2000);assert(gSim.tx.size()==t0+2&&pingSt.sent==2);   // drugi ping ok. 10 s po pierwszym
  run(8500);assert(pingSt.st==PG_TIMEOUT&&pingSt.sent==2);shot("mesh_autoping_timeout");run(1500);assert(pingSt.sent==3);   // brak ACK: timeout przed kolejnym pingiem
  // kolejny wezel: licznik od nowa, nastepny ping po ok. 1,5 s
  {auto p=mkpkt(0x1001,800,8,"","",false);gSim.rx.push_back(SimPkt{p,-95,2,0});gSim.arm();run(200);}
  pressB();assert(meshSelId!=tg);run(1700);assert(pingSt.target==meshSelId&&pingSt.sent==1);
  // wylaczenie w SETUP zatrzymuje pingi
  cfg.autoPing=0;applyChange(6);size_t t1=gSim.tx.size();run(30000);assert(gSim.tx.size()==t1);cfg.autoPing=1;applyChange(6);run(3000);assert(gSim.tx.size()>t1);
  // zmiana presetu zdejmuje wybor
  cfg.meshPreset=1;applyChange(1);assert(!meshPicked&&!meshSelId);cfg.meshPreset=0;applyChange(1);run(300);}

 // --- ustawienia, NVS, wszystkie pozycje po kilka razy
 gotoScreen(S_SET);for(unsigned i=0;i<SET_N;i++)for(int k=0;k<10;k++){changeSetting(i);}
 cfg.power=3;cfg.meshPreset=7;cfg.beacon=4;cfg.autoPing=0;cfg.invert=0;cfg.flip=1;cfg.pwrHigh=1;cfgSave();Cfg saved=cfg;cfg=Cfg();cfgLoad();assert(memcmp(&saved,&cfg,sizeof cfg)==0);
 cfg=Cfg();applyChange(0);
 // --- Bluetooth: PIN, autoryzacja, zapis konfiguracji
 {cfg=Cfg();bleStop();assert(!cfg.ble);gotoScreen(S_SET);setSel=7;shot("set_ble_off");int adv0=BLEDevice::adv()->started;pressB();assert(cfg.ble==1&&bleInit&&bleAdv&&blePin>=100000&&blePin<=999999&&BLEDevice::adv()->started==adv0+1);run(1100);shot("set_ble_on");
  assert(BLEDevice::adv()->uuids.size()>=1&&BLEDevice::adv()->uuids[0]==BLE_UUID_SERVICE&&BLEDevice::adv()->scanResp);
  BLECharacteristic* cC=bleChars()[BLE_UUID_CONFIG];BLECharacteristic* cA=bleChars()[BLE_UUID_AUTH];BLECharacteristic* cS=bleChars()[BLE_UUID_STATUS];BLECharacteristic* cO=bleChars()[BLE_UUID_OPTIONS];assert(cC&&cA&&cS&&cO);
  assert(cC->val.size()==7&&uint8_t(cC->val[0])==cfg.power&&uint8_t(cC->val[1])==cfg.meshPreset);
  auto conn=[&](){BLEDevice::srv()->cb->onConnect(BLEDevice::srv());run(20);};
  auto wr=[&](BLECharacteristic* c,std::string v){c->phoneWrite(v);run(30);};
  std::string good=std::string(7,'\0');uint8_t want[7]={4,9,0,1,0,3,0};for(int i=0;i<7;i++)good[i]=char(want[i]);Cfg before=cfg;
  conn();assert(bleConn&&!bleAuthed);
  wr(cC,good);assert(memcmp(&before,&cfg,sizeof cfg)==0);                     // bez PIN-u zapis jest ignorowany
  char pin[8];snprintf(pin,sizeof pin,"%06lu",(unsigned long)(blePin==111111?222222:111111));
  wr(cA,pin);assert(!bleAuthed&&uint8_t(cA->val[0])==0);wr(cA,pin);wr(cA,"12345");int d0=BLEDevice::srv()->disconnects;wr(cA,pin);   // trzy bledy rozlaczaja
  assert(BLEDevice::srv()->disconnects==d0+1&&!bleConn);
  run(400);assert(BLEDevice::adv()->started>=adv0+2);                          // po rozlaczeniu znow rozgloszenie
  conn();snprintf(pin,sizeof pin,"%06lu",(unsigned long)blePin);wr(cA,pin);assert(bleAuthed&&uint8_t(cA->val[0])==1);
  int n0=cC->notifies;wr(cC,good);
  assert(cfg.power==4&&cfg.meshPreset==9&&cfg.invert==0&&cfg.flip==1&&cfg.pwrHigh==0&&cfg.beacon==3&&cfg.autoPing==0);
  assert(fabsf(gSim.freq-pre::meshFreq(9))<1e-3&&gSim.sf==7&&gSim.bw==62.5f&&gSim.pwr==22);   // NarrowFast, pelna moc na 869,525 MHz
  assert(cC->notifies>n0&&uint8_t(cC->val[1])==9);{Cfg saved=cfg;cfg=Cfg();cfgLoad();assert(memcmp(&saved,&cfg,sizeof cfg)==0);}
  std::string bad=good;bad[1]=char(99);Cfg keep=cfg;wr(cC,bad);assert(memcmp(&keep,&cfg,sizeof cfg)==0&&uint8_t(cC->val[1])==9);   // niepoprawny zapis: bez zmian, odeslane biezace ustawienia
  wr(cC,std::string("abc"));assert(memcmp(&keep,&cfg,sizeof cfg)==0);          // za krotki zapis
  {BLECharacteristic* cP=bleChars()[BLE_UUID_POS];assert(cP&&cP->val.empty());   // brak pozycji = pusty tekst
   bleAuthed=false;wr(cP,"52.1,21.0,100");assert(!gpos.has);bleAuthed=true;                                  // bez PIN-u ignorowane
   wr(cP,"52.2297,21.0122,110");assert(gpos.has&&cP->val=="52.229700,21.012200,110");{Pos sv=gpos;gpos=Pos();posLoad();assert(memcmp(&sv,&gpos,sizeof sv)==0);}   // trwale w NVS
   wr(cP,"999,1");assert(gpos.has&&gpos.lat==522297000&&cP->val=="52.229700,21.012200,110");               // bledne: bez zmian, odeslane biezace
   // zegar: czas lokalny z telefonu przez BLE (po PIN-ie), widoczny w naglowku i w stanie
   {BLECharacteristic* cT=bleChars()[BLE_UUID_TIME];assert(cT);
    bleAuthed=false;wr(cT,"1761404700");{struct tm t;assert(true);(void)t;}
    bleAuthed=true;wr(cT,"abc");wr(cT,"123");   // bledne wartosci ignorowane
    wr(cT,"1761404700");struct tm t;assert(clockGet(t)&&t.tm_hour==15&&t.tm_min==5);   // 2025-10-25 15:05 UTC (traktowane jako czas lokalny)
    char c[8];assert(clockText(c,sizeof c)&&!strcmp(c,"15:05"));
    char js[900];bleStatusJson(js,sizeof js,millis());assert(strstr(js,"\"clk\":17614047"));}
   wr(cP,"");assert(!gpos.has&&cP->val.empty());wr(cP,"52.2297,21.0122,110");assert(gpos.has);}
  {BLECharacteristic* cN=bleChars()[BLE_UUID_NAME];assert(cN);
   wr(cN,"  DACHA-1 ");assert(!strcmp(nodeName,"DACHA-1")&&cN->val=="DACHA-1");wr(cN,"Za\xc5\x82\xc4\x85\x63zniczek-dlugi-123");assert(strlen(nodeName)<=12);
   {char sv[13];strcpy(sv,nodeName);nodeName[0]=0;nameLoad();assert(!strcmp(sv,nodeName));}                 // trwale w NVS
   wr(cN,"DACHA-1");run(1100);assert(cS->val.find("\"name\":\"DACHA-1\"")!=std::string::npos&&cS->val.find("\"pos\":\"52.229700,21.012200,110\"")!=std::string::npos);shot("home_named");
   bleAuthed=false;wr(cN,"HACK");assert(!strcmp(nodeName,"DACHA-1"));bleAuthed=true;}
  {FILE* f=fopen("json_status.txt","w");fputs(cS->val.c_str(),f);fclose(f);f=fopen("json_options.txt","w");fputs(cO->val.c_str(),f);fclose(f);}
  assert(cS->val.size()>100&&cS->val.size()<512&&cO->val.size()>200&&cO->val.size()<560);
  shot("set_ble_connected");
  BLEDevice::srv()->cb->onDisconnect(BLEDevice::srv());run(400);
  // wylaczenie w SETUP: koniec rozgloszen, PIN przestaje dzialac
  int st0=BLEDevice::adv()->stopped;gotoScreen(S_SET);setSel=7;pressB();assert(cfg.ble==0&&BLEDevice::adv()->stopped==st0+1&&blePin==0);
  conn();wr(cA,pin);assert(!bleAuthed);BLEDevice::srv()->cb->onDisconnect(BLEDevice::srv());run(400);
  pressB();assert(cfg.ble==1&&blePin>0);                                          // ponowne wlaczenie: nowy PIN
  cfg=Cfg();bleStop();applyChange(0);cfg.ble=0;cfgSave();}
 cfg=Cfg();applyChange(0);
 // --- przyciski: caly obieg ekranow
 gotoScreen(S_HOME);for(int i=0;i<2*S_COUNT;i++){pressA();run(300);shot(("cycle"+std::to_string(i)).c_str());}
 pressA(true);pressA(true);
 // --- bledy radia

 // --- lacze WiFi / TCP z CYD: punkt dostepowy, API Meshtastic na porcie 4403
 {using Bytes=std::vector<uint8_t>;
  assert(cfg.wifi==1&&wifiOn&&gNet.apUp&&gNet.srvUp&&gNet.mode==WIFI_AP&&gNet.ssid==LINK_SSID&&gNet.pass==LINK_PASS&&gNet.channel==int(LINK_CHANNEL)&&strlen(LINK_PASS)>=8);
  cfg.meshPreset=0;cfg.power=2;applyChange(1);gotoScreen(S_MESH);cfg.autoPing=0;meshPicked=false;pingSt=Ping();duty=pre::Duty();gSim.tx.clear();
  // klient: ramki ToRadio i odczyt FromRadio
  auto var=[](Bytes& b,uint64_t v){do{b.push_back(uint8_t((v&127)|(v>127?128:0)));v>>=7;}while(v);};
  auto send=[&](const Bytes& pl){gNet.toServer.push_back(0x94);gNet.toServer.push_back(0xC3);gNet.toServer.push_back(uint8_t(pl.size()>>8));gNet.toServer.push_back(uint8_t(pl.size()));for(auto c:pl)gNet.toServer.push_back(c);run(30);};
  auto frames=[&](){std::vector<Bytes> out;api::Framer fr;for(auto c:gNet.fromServer)fr.feed(c,gNow,[&](const uint8_t* p,size_t n){out.push_back(Bytes(p,p+n));});gNet.fromServer.clear();return out;};
  // pole wiadomosci (pierwsze wystapienie)
  auto field=[](const Bytes& m,unsigned tag,api::Field& out){api::Reader r(m.data(),m.size());api::Field f;while(r.next(f))if(f.tag==tag){out=f;return true;}return false;};
  auto sub=[](const api::Field& f){return Bytes(f.bytes,f.bytes+f.size);};
  // pakiety LoRa w sieci: kilka wezlow z nazwami
  for(int i=0;i<3;i++){char nm[24];snprintf(nm,sizeof nm,"Mazowsze %d",i);auto p=mkpkt(0x3000+i,1500+i,mtab.defHash,nm,"MZ",true);gSim.rx.push_back(SimPkt{p,-90.f-i,4.f-i,0});gSim.arm();run(200);}
  assert(!apiLinked());gotoScreen(S_HOME);shot("home_wifi_ap");gotoScreen(S_MESH);
  // polaczenie klienta
  gNet.pending=true;run(50);assert(apiHas&&apiLinked()&&gNet.accepts==1&&!apiConfigured);gotoScreen(S_HOME);shot("home_wifi_cyd");gotoScreen(S_MESH);
  // przed want_config pakiety LoRa nie sa przekazywane
  {auto p=mkpkt(0x3000,1600,mtab.defHash,"","",false);gSim.rx.push_back(SimPkt{p,-91,3,0});gSim.arm();run(200);assert(frames().empty());}
  // want_config_id: my_info, NodeInfo (E213 + slyszane), Config.lora, kanal 0, config_complete_id
  {strcpy(nodeName,"POLE-1");assert(posParse("52.2297,21.0122,110",19));Bytes w={0x18};var(w,0xC0FFEEu);send(w);auto F=frames();fprintf(stderr,"config dump: %zu frames\n",F.size());
   assert(F.size()>=6);api::Field f;
   assert(field(F[0],3,f));{Bytes mi=sub(f);api::Field g;assert(field(mi,1,g)&&uint32_t(g.value)==myId);}
   assert(field(F[1],4,f));{Bytes ni=sub(f);api::Field g;assert(field(ni,1,g)&&uint32_t(g.value)==myId);assert(field(ni,2,g));Bytes u=sub(g);api::Field h;assert(field(u,2,h)&&std::string((const char*)h.bytes,h.size)=="POLE-1");
    assert(field(u,5,h)&&h.value==67);assert(field(ni,3,g));Bytes pos=sub(g);assert(field(pos,1,h)&&int32_t(uint32_t(h.value))==522297000);assert(field(ni,6,g));}
   unsigned nodes=0,named=0;size_t i=2;for(;i<F.size()&&field(F[i],4,f);i++){Bytes ni=sub(f);api::Field g;assert(field(ni,1,g));++nodes;if(field(ni,2,g))++named;}
   assert(nodes==mtab.used&&named>=3);
   assert(field(F[i],5,f));{Bytes c=sub(f);api::Field g;assert(field(c,6,g));Bytes l=sub(g);api::Field h;assert(field(l,1,h)&&h.value==1&&field(l,2,h)&&h.value==0&&field(l,7,h)&&h.value==3&&field(l,14,h));float mhz;uint32_t v=uint32_t(h.value);memcpy(&mhz,&v,4);assert(fabsf(mhz-869.525f)<1e-3);}
   assert(field(F[i+1],10,f));{Bytes c=sub(f);api::Field g;assert(field(c,3,g)&&g.value==1&&field(c,2,g));Bytes st=sub(g);api::Field h;assert(field(st,3,h)&&std::string((const char*)h.bytes,h.size)=="LongFast");}
   assert(field(F[i+2],7,f)&&uint32_t(f.value)==0xC0FFEEu&&apiConfigured&&i+3==F.size());
   uint32_t prev=0;for(auto& x:F){api::Field g;assert(field(x,1,g)&&uint32_t(g.value)>prev);prev=uint32_t(g.value);}}   // rosnace id ramek
  // pakiet LoRa na domyslnym kanale: odszyfrowany Data + RSSI/SNR/hopy
  {auto p=mkpkt(0x3001,1700,mtab.defHash,"Mazowsze 1","MZ",true);gSim.rx.push_back(SimPkt{p,-97,-2.25f,0});gSim.arm();run(200);auto F=frames();assert(F.size()==1);api::Field f;assert(field(F[0],2,f));Bytes m=sub(f);api::Field g;
   assert(field(m,1,g)&&uint32_t(g.value)==0x3001&&field(m,2,g)&&uint32_t(g.value)==0xFFFFFFFFu&&field(m,6,g)&&uint32_t(g.value)==1700);
   assert(field(m,12,g)&&int32_t(g.value)==-97&&field(m,8,g));{float sn;uint32_t v=uint32_t(g.value);memcpy(&sn,&v,4);assert(sn==-2.25f);}
   assert(field(m,9,g)&&g.value==2&&field(m,15,g)&&g.value==3&&field(m,21,g)&&g.value==1&&!field(m,3,g));
   assert(field(m,4,g));Bytes d=sub(g);api::Field h;assert(field(d,1,h)&&h.value==4&&field(d,2,h));}
  // inny kanal (nieznany klucz): zaszyfrowane bajty i skrot kanalu
  {auto p=mkpkt(0x4444,1800,77,"x","y",false);gSim.rx.push_back(SimPkt{p,-115,-11,0});gSim.arm();run(200);auto F=frames();assert(F.size()==1);api::Field f;assert(field(F[0],2,f));Bytes m=sub(f);api::Field g;
   assert(field(m,5,g)&&g.size==p.size()-16&&!field(m,4,g)&&field(m,3,g)&&g.value==77&&field(m,12,g)&&int32_t(g.value)==-115);}
  // pakiet od klienta (test lacza CYD: port PRIVATE_APP, want_ack) -> nadanie LoRa
  auto probe=[&](uint32_t to,uint32_t id,unsigned hop){Bytes d={0x08,0x80,0x02,0x12,0x04,'M','Z',0,7};Bytes m;m.push_back(0x15);for(int i=0;i<4;i++)m.push_back(uint8_t(to>>(8*i)));
   m.push_back(0x22);m.push_back(uint8_t(d.size()));m.insert(m.end(),d.begin(),d.end());m.push_back(0x35);for(int i=0;i<4;i++)m.push_back(uint8_t(id>>(8*i)));if(hop){m.push_back(0x48);m.push_back(uint8_t(hop));}m.push_back(0x50);m.push_back(1);
   Bytes t={0x0a};var(t,m.size());t.insert(t.end(),m.begin(),m.end());send(t);return d;};
  {size_t t0=gSim.tx.size();Bytes d=probe(0x3002,0xABCD1234u,0);run(20);assert(gSim.tx.size()==t0+1&&apiTxOk==1);auto& pk=gSim.tx.back();mh::Hdr h;assert(mh::parse(pk.data(),pk.size(),h));
   assert(h.to==0x3002&&h.from==myId&&h.id==0xABCD1234u&&h.wantAck&&h.hopLimit==3&&h.hopStart==3&&h.chan==mtab.defHash&&pk.size()==16+d.size());
   Bytes pl(pk.size()-16);mh::ctrCrypt(h.id,h.from,pk.data()+16,pl.data(),pl.size());assert(pl==d);
   run(1500);{std::vector<uint8_t> a={0x08,0x05,0x12,0x02,0x18,0x00,0x35};for(int i=0;i<4;i++)a.push_back(uint8_t(0xABCD1234u>>(8*i)));Bytes p(16,0);
    for(int i=0;i<4;i++){p[i]=uint8_t(myId>>(8*i));p[4+i]=uint8_t(0x3002>>(8*i));p[8+i]=uint8_t(0x9999>>(8*i));}p[12]=0x63;p[13]=mtab.defHash;Bytes c(a.size());mh::ctrCrypt(0x9999,0x3002,a.data(),c.data(),a.size());p.insert(p.end(),c.begin(),c.end());
    frames();gSim.rx.push_back(SimPkt{p,-99,1.5f,0});gSim.arm();run(200);auto F=frames();assert(F.size()==1);api::Field f;assert(field(F[0],2,f));Bytes m=sub(f);api::Field g;assert(field(m,4,g));Bytes dd=sub(g);api::Field h2;
    assert(field(dd,1,h2)&&h2.value==5&&field(dd,6,h2)&&uint32_t(h2.value)==0xABCD1234u);}}   // ACK celu trafia do klienta
  // hop_limit z pakietu klienta
  {probe(0x3002,0x1111,1);run(20);mh::Hdr h;assert(mh::parse(gSim.tx.back().data(),gSim.tx.back().size(),h)&&h.hopLimit==1&&h.id==0x1111);}
  // limit wypelnienia: blad routingu DUTY_CYCLE_LIMIT od E213 z request_id
  {run(2000);frames();duty.add(gNow,3600000);size_t t0=gSim.tx.size();probe(0x3002,0x2222,0);run(50);assert(gSim.tx.size()==t0);auto F=frames();assert(F.size()==1);api::Field f;assert(field(F[0],2,f));Bytes m=sub(f);api::Field g;
   assert(field(m,1,g)&&uint32_t(g.value)==myId&&!field(m,12,g)&&field(m,4,g));Bytes d=sub(g);api::Field h;assert(field(d,1,h)&&h.value==5&&field(d,6,h)&&uint32_t(h.value)==0x2222&&field(d,2,h));Bytes rt=sub(h);api::Field e;assert(field(rt,3,e)&&e.value==9);duty=pre::Duty();}
  // komenda do E213 (np. admin do wlasnego wezla): nie nadawana, blad NO_RESPONSE
  {size_t t0=gSim.tx.size();probe(myId,0x3333,0);run(50);assert(gSim.tx.size()==t0);auto F=frames();assert(F.size()==1);api::Field f;field(F[0],2,f);Bytes m=sub(f);api::Field g;field(m,4,g);Bytes d=sub(g);api::Field h;field(d,2,h);Bytes rt=sub(h);api::Field e;assert(field(rt,3,e)&&e.value==8);}
  // ekran SCAN: radio przemiata pasmo, pakiet klienta dostaje NO_INTERFACE
  {gotoScreen(S_SCAN);frames();probe(0x3002,0x4444,0);run(50);auto F=frames();assert(F.size()==1);api::Field f;field(F[0],2,f);Bytes m=sub(f);api::Field g;field(m,4,g);Bytes d=sub(g);api::Field h;field(d,2,h);Bytes rt=sub(h);api::Field e;assert(field(rt,3,e)&&e.value==4);gotoScreen(S_MESH);}
  // telemetria E213 co 60 s (bateria, zajetosc eteru)
  {frames();run(61000);auto F=frames();int tel=0;for(auto& x:F){api::Field f;if(!field(x,2,f))continue;Bytes m=sub(f);api::Field g;if(field(m,1,g)&&uint32_t(g.value)==myId&&field(m,4,g)){Bytes d=sub(g);api::Field h;if(field(d,1,h)&&h.value==67)++tel;}}assert(tel==1);}
  // heartbeat i smieci w strumieniu nie psuja polaczenia
  {send(Bytes{0x3a,0x00});for(int i=0;i<50;i++)gNet.toServer.push_back(uint8_t(i*37));run(2000);assert(apiLinked());}
  gotoScreen(S_SET);setSel=8;shot("set_wifi_cyd");
  // rozlaczenie klienta, ponowne polaczenie (stan od nowa)
  gNet.connected=false;run(50);assert(!apiHas&&!apiConfigured&&!apiLinked());
  {size_t n0=gNet.fromServer.size();auto p=mkpkt(0x3000,1900,mtab.defHash,"","",false);gSim.rx.push_back(SimPkt{p,-91,3,0});gSim.arm();run(200);assert(gNet.fromServer.size()==n0);}
  gNet.pending=true;run(50);assert(apiLinked()&&gNet.accepts==2&&!apiConfigured);{Bytes w={0x18};var(w,7);send(w);auto F=frames();api::Field f;assert(field(F.back(),7,f)&&f.value==7);}
  // klient bez ruchu przez 10 min jest rozlaczany
  run(601000);assert(!apiHas);
  // WiFi OFF w SETUP: punkt dostepowy wylaczony, ustawienie zapisane
  shot("set_wifi_waiting");pressB();assert(!cfg.wifi&&!wifiOn&&!gNet.apUp&&!gNet.srvUp);shot("set_wifi_off");{Cfg sv=cfg;cfg=Cfg();cfgLoad();assert(!cfg.wifi);cfg=sv;}
  gNet.pending=true;run(50);assert(!apiHas);
  pressB();assert(cfg.wifi&&wifiOn&&gNet.apUp);gotoScreen(S_HOME);nodeName[0]=0;gpos=Pos();
  fprintf(stderr,"wifi: frames out=%lu in=%lu tx ok=%lu fail=%lu sessions=%lu\n",(unsigned long)apiFramesOut,(unsigned long)apiFramesIn,(unsigned long)apiTxOk,(unsigned long)apiTxFail,(unsigned long)apiSessions);}
 gSim.beginOk=false;radioOk=false;gotoScreen(S_HOME);shot("home_radio_err");gSim.beginOk=true;
 // --- bateria
 {auto V=[&](float v){gAdcMv=(uint32_t)(v/BAT_MULT*1000+.5f);batRead();};
  V(3.90f);assert(!batChg);V(3.93f);assert(batChg);for(int i=0;i<5;i++){V(3.93f);assert(batChg);}V(3.93f);assert(!batChg);V(3.96f);assert(batChg);
  V(4.19f);assert(batChg);gotoScreen(S_HOME);shot("home_charging");V(4.19f);assert(batChg);V(4.12f);assert(!batChg);
  V(3.50f);assert(!batChg&&batPct<=10);shot("home_battery_low");V(4.20f);assert(batChg);
  // wylaczenie ponizej 5%: 3 odczyty, nie podczas ladowania; wznowienie po wzroscie napiecia
  V(3.45f);V(3.45f);assert(batLowCnt==2&&gSleepCalls==0);V(3.45f);assert(batLowCnt>=BAT_OFF_READS);
  spr.texts.clear();drawShutdown();dump("shutdown");checkText("shutdown");batShutdown();assert(gSleepCalls==1&&rtcLowOff);
  gWakeCause=ESP_SLEEP_WAKEUP_TIMER;gAdcMv=(uint32_t)(3.44f/BAT_MULT*1000+.5f);assert(!batWakeCheck()&&gSleepCalls==2&&rtcLowOff);   // dalej rozladowana
  gAdcMv=(uint32_t)(3.55f/BAT_MULT*1000+.5f);assert(batWakeCheck()&&!rtcLowOff);                                                   // zasilanie podlaczone
  gWakeCause=ESP_SLEEP_WAKEUP_UNDEFINED;rtcLowOff=true;assert(batWakeCheck()&&!rtcLowOff);                                         // reset: normalny start
  V(3.90f);V(3.90f);assert(batLowCnt==0);
  V(4.20f);assert(batChg);char js[900];bleStatusJson(js,sizeof js,millis());assert(strstr(js,"\"chg\":true"));}
 spr.texts.clear();drawSplash();dump("splash");checkText("splash");
 batPct=-1;shot("home_nobat");

 // --- e-ink: odswiezanie czesciowe / pelne, ograniczenie czestosci, przycisk = szybsze odswiezenie
 {cfg.pwrHigh=1;gotoScreen(S_HOME);run(600);                       // odstep 5 s
  int s0=epdhw::shows,f0=epdhw::fulls;
  for(int i=0;i<30;i++){dirty=true;run(500);}                       // 15 s: co najwyzej jedno odswiezenie na 5 s
  assert(epdhw::shows-s0<=4);
  run(5200);s0=epdhw::shows;gotoScreen(S_NOISE);epdUrgent=true;run(1200);assert(epdhw::shows==s0+1&&epdhw::lastPartial);   // zmiana ekranu (po przycisku ~1 s) = odswiezenie czesciowe
  epdPartials=EPD_FULL_EVERY;s0=epdhw::shows;f0=epdhw::fulls;gotoScreen(S_HOME);epdUrgent=true;run(1300);assert(epdhw::shows==s0+1&&epdhw::fulls==f0+1&&!epdhw::lastPartial&&epdPartials==0);   // co 10 czesciowych: pelne
  cfg.invert=1;cfg.flip=1;epdChanged=true;epdUrgent=true;run(1300);assert(epdhw::lastInv&&epdhw::lastFlip);
  cfg.invert=0;cfg.flip=0;cfg.pwrHigh=0;}
 fprintf(stderr,"overlaps=%d textOob=%d\n",gOverlaps,gOob);
 puts("sim OK");return 0;}
