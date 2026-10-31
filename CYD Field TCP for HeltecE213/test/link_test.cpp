// Test zgodnosci lacza E213 <-> CYD: ramki zbudowane przez serwer E213 (include/e213/api.h) sa czytane prawdziwym parserem CYD
// (include/cyd/protocol.h), a ramki zbudowane przez CYD (buildProbe, want_config, heartbeat) prawdziwym parserem E213.
// Oba naglowki w jednym programie: api:: i mesh:: nie koliduja.
#include <cassert>
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include "../include/cyd/protocol.h"
#include "../include/e213/api.h"
typedef std::vector<uint8_t> B;
// Ramki z bufora strumienia przez Framer CYD
static std::vector<B> viaCydFramer(const B& stream){std::vector<B> out;mesh::Framer fr;uint32_t t=0;for(auto c:stream)fr.feed(c,t++,[&](const uint8_t* p,size_t n){out.push_back(B(p,p+n));});assert(fr.errors==0);return out;}
static void put(B& s,const uint8_t* p,size_t n){assert(n);uint8_t f[600];size_t k=api::frame(p,n,f,sizeof f);assert(k==n+4);s.insert(s.end(),f,f+k);}
// pole FromRadio
static bool field(const B& m,unsigned tag,mesh::Field& out){mesh::Reader r(m.data(),m.size());mesh::Field f;while(r.next(f))if(f.tag==tag){out=f;return true;}return false;}
int main(){
 const uint32_t ME=0x9e3779b1;
 // ---- zrzut konfiguracji E213 -> parser CYD ----
 {B s;uint8_t o[400];size_t n;uint32_t fid=1;
  n=api::myInfo(o,sizeof o,fid++,ME);put(s,o,n);
  api::NodeDesc me;me.num=ME;me.longName="POLE-1";me.shortName="POLE";me.hw=67;me.hasPos=true;me.lat=522297000;me.lon=210122000;me.alt=110;me.hasMetrics=true;me.battery=78;me.voltage=3.94f;me.chUtil=12.5f;me.airTx=0.75f;me.uptime=3600;me.lastHeard=1761404700;
  n=api::nodeInfo(o,sizeof o,fid++,me);put(s,o,n);
  api::NodeDesc nb;nb.num=0x3001;nb.longName="Mazowsze 1";nb.shortName="MZ1";nb.snr=-3.5f;nb.hops=2;n=api::nodeInfo(o,sizeof o,fid++,nb);put(s,o,n);
  api::NodeDesc anon;anon.num=0x4444;anon.hasUser=false;n=api::nodeInfo(o,sizeof o,fid++,anon);put(s,o,n);
  n=api::loraConfig(o,sizeof o,fid++,0,11,250,5,869.525f,3,14);put(s,o,n);
  n=api::channel(o,sizeof o,fid++,0,"LongFast",true);put(s,o,n);
  n=api::configComplete(o,sizeof o,fid++,0xC0FFEE);put(s,o,n);
  auto F=viaCydFramer(s);assert(F.size()==7);
  mesh::Field f;assert(field(F[0],3,f));{mesh::Reader r(f.bytes,f.size);mesh::Field a;uint32_t id=0;while(r.next(a))if(a.tag==1)id=uint32_t(a.value);assert(id==ME);}
  // NodeInfo jak w CYD nodeInfo(): user, position, device_metrics
  assert(field(F[1],4,f));{mesh::Reader r(f.bytes,f.size);mesh::Field a,u,p,dm;uint32_t id=0;while(r.next(a)){if(a.tag==1)id=uint32_t(a.value);if(a.tag==2)u=a;if(a.tag==3)p=a;if(a.tag==6)dm=a;}assert(r.ok()&&id==ME&&u.bytes&&p.bytes&&dm.bytes);
   mesh::Reader ur(u.bytes,u.size);char nm[40]={},sh[12]={};while(ur.next(a)){if(a.tag==2)mesh::copyText(nm,sizeof nm,a);if(a.tag==3)mesh::copyText(sh,sizeof sh,a);}assert(!strcmp(nm,"POLE-1")&&!strcmp(sh,"POLE"));
   mesh::Position pos;assert(mesh::position(p.bytes,p.size,pos)&&pos.valid()&&pos.lat==522297000&&pos.lon==210122000&&pos.source==1);
   mesh::DeviceMetrics d;assert(mesh::deviceMetrics(dm.bytes,dm.size,d)&&d.battery==78&&fabsf(d.voltage-3.94f)<1e-4&&fabsf(d.chUtil-12.5f)<1e-4&&fabsf(d.airTx-0.75f)<1e-4&&d.uptime==3600);}
  assert(field(F[3],4,f));{mesh::Reader r(f.bytes,f.size);mesh::Field a;bool user=false;while(r.next(a))if(a.tag==2)user=true;assert(r.ok()&&!user);}
  assert(field(F[4],5,f));{mesh::LoraCfg c;assert(mesh::loraConfig(f.bytes,f.size,c)&&c.has&&c.usePreset&&c.preset==0&&c.region==3);mesh::Modem m;assert(mesh::modem(c,"",m)&&m.sf==11&&m.bwKhz==250&&fabsf(m.mhz-869.525f)<1e-3);
   assert(!strcmp(mesh::presetName(c.preset),"LongFast")&&!strcmp(mesh::regionName(c.region),"EU868"));}
  assert(field(F[5],10,f));{mesh::ChannelInfo c;assert(mesh::channel(f.bytes,f.size,c)&&c.index==0&&c.role==1&&!strcmp(c.name,"LongFast"));}
  assert(field(F[6],7,f)&&f.value==0xC0FFEE);}
 // ---- wszystkie presety E213: CYD zna parametry modemu i czestotliwosc ----
 {struct P{const char* n;unsigned i,sf;float bw;unsigned cr,region;float mhz;};
  const P ps[]={{"LongFast",0,11,250,5,0,869.525f},{"LongSlow",1,12,125,8,0,869.4625f},{"MediumSlow",2,10,250,5,0,869.525f},{"MediumFast",3,9,250,5,0,869.525f},{"ShortSlow",4,8,250,5,0,869.525f},
   {"ShortFast",5,7,250,5,0,869.525f},{"LongMod",6,11,125,8,0,869.5875f},{"LiteFast",7,9,125,5,1,866.2f},{"LiteSlow",8,10,125,5,1,866.9f},{"NarrowFast",9,7,62.5f,6,2,869.44f},{"NarrowSlow",10,8,62.5f,6,2,869.5f}};
  for(auto& p:ps){uint8_t o[200];size_t n=api::loraConfig(o,sizeof o,1,api::presetEnum(p.i),p.sf,p.bw,p.cr,p.mhz,api::regionCode(p.region),14);mesh::Field f;B m(o,o+n);assert(field(m,5,f));
   mesh::LoraCfg c;assert(mesh::loraConfig(f.bytes,f.size,c));mesh::Modem md;assert(mesh::modem(c,p.n,md));assert(md.sf==p.sf&&md.bwKhz==p.bw&&fabsf(md.mhz-p.mhz)<1e-3);
   if(api::presetEnum(p.i)<9)assert(c.usePreset&&!strcmp(mesh::presetName(c.preset),p.n));else assert(!c.usePreset);
   fprintf(stderr,"preset %-10s -> enum %2d region %u: CYD sees SF%u BW%g %.4f MHz\n",p.n,api::presetEnum(p.i),api::regionCode(p.region),md.sf,(double)md.bwKhz,(double)md.mhz);}}
 // ---- odebrany pakiet (odszyfrowany) i obcy kanal (zaszyfrowany) ----
 {const B data={0x08,0x04,0x12,0x03,0x12,0x01,'X'};api::PktDesc d;d.from=0x3001;d.to=0xFFFFFFFF;d.id=1700;d.hopLimit=2;d.hopStart=3;d.relay=0x01;d.snr=-2.25f;d.rssi=-97;d.rxTime=1761404700;d.decoded=data.data();d.decodedLen=data.size();
  uint8_t o[300];size_t n=api::packet(o,sizeof o,9,d);B m(o,o+n);mesh::Field f;assert(field(m,2,f));mesh::Packet p;assert(mesh::packet(f.bytes,f.size,p));
  assert(p.from==0x3001&&p.to==0xFFFFFFFF&&p.id==1700&&p.hasRssi&&p.rssi==-97&&p.hasSnr&&p.snr==-2.25f&&p.hopLimit==2&&p.hasHopStart&&p.hopStart==3&&p.relay==1&&p.transport==1&&!p.mqtt&&p.epoch==1761404700&&p.port==4&&p.payloadSize==3&&p.channel==0);
  const B enc(23,0x5A);api::PktDesc e;e.from=0x4444;e.to=0xFFFFFFFF;e.id=1800;e.channel=77;e.hopLimit=3;e.hopStart=3;e.snr=-11;e.rssi=-115;e.encrypted=enc.data();e.encLen=enc.size();
  n=api::packet(o,sizeof o,10,e);B m2(o,o+n);assert(field(m2,2,f));mesh::Packet q;assert(mesh::packet(f.bytes,f.size,q)&&q.from==0x4444&&q.port==0&&q.hasRssi&&q.rssi==-115&&q.channel==77&&!q.payload);}
 // ---- blad routingu od E213 (np. limit wypelnienia) -> CYD probeLost ----
 {uint8_t dd[24];size_t dn=api::dataRouting(dd,sizeof dd,0xABCD,9);api::PktDesc d;d.from=ME;d.to=ME;d.id=5;d.hasRx=false;d.decoded=dd;d.decodedLen=dn;uint8_t o[200];size_t n=api::packet(o,sizeof o,11,d);
  B m(o,o+n);mesh::Field f;assert(field(m,2,f));mesh::Packet p;assert(mesh::packet(f.bytes,f.size,p)&&p.from==ME&&p.port==5&&p.requestId==0xABCD&&!p.hasRssi);unsigned err;bool ack;assert(mesh::routing(p.payload,p.payloadSize,err,ack)&&ack&&err==9);}
 // ---- telemetria E213 -> kafelek DEVICE ----
 {uint8_t dd[64];size_t dn=api::dataTelemetry(dd,sizeof dd,1761404700,101,4.18f,3.5f,0.2f,7200);api::PktDesc d;d.from=ME;d.to=0xFFFFFFFF;d.id=6;d.hasRx=false;d.decoded=dd;d.decodedLen=dn;uint8_t o[200];size_t n=api::packet(o,sizeof o,12,d);
  B m(o,o+n);mesh::Field f;assert(field(m,2,f));mesh::Packet p;assert(mesh::packet(f.bytes,f.size,p)&&p.port==67);mesh::DeviceMetrics dm;assert(mesh::telemetry(p.payload,p.payloadSize,dm)&&dm.battery==101&&dm.uptime==7200&&fabsf(dm.voltage-4.18f)<1e-4);}
 // ---- CYD -> E213: test lacza (buildProbe), want_config, heartbeat ----
 {uint8_t b[96];size_t n=mesh::buildProbe(b,sizeof b,0x3002,0xABCD1234,7,256,0,0);api::ToRadio t;assert(api::parseToRadio(b,n,t));
  assert(t.kind==api::TR_PACKET&&t.to==0x3002&&t.id==0xABCD1234&&t.wantAck&&t.hopLimit==0&&t.port==256&&t.hasDecoded&&t.dataLen>0);
  {mesh::Reader r(t.data,t.dataLen);mesh::Field f;B pl;while(r.next(f))if(f.tag==2)pl.assign(f.bytes,f.bytes+f.size);assert(pl==(B{'M','Z',0,7}));}
  n=mesh::buildProbe(b,sizeof b,0x3002,0x55,1,256,2,5);assert(api::parseToRadio(b,n,t)&&t.channel==2&&t.hopLimit==5);
  const uint8_t wc[]={0x18,0xEE,0xFF,0x83,0x06};assert(api::parseToRadio(wc,sizeof wc,t)&&t.kind==api::TR_WANT_CONFIG&&t.configId==0xC0FFEE);
  const uint8_t hb[]={0x3a,0x00};assert(api::parseToRadio(hb,sizeof hb,t)&&t.kind==api::TR_HEARTBEAT);
  const uint8_t bad[]={0x0a,0x10,0x15};assert(!api::parseToRadio(bad,sizeof bad,t));
  // CYD: zmiana GPS przez admin do wlasnego wezla (E213 odrzuci: to==myId)
  mesh::PosCfg pc;pc.has=true;pc.len=0;n=mesh::buildGpsMode(b,sizeof b,ME,0x77,pc,1);assert(n==0||(api::parseToRadio(b,n,t)&&t.to==ME&&t.port==6));}
 // ---- Framer E213 na strumieniu z CYD: ramki sklejone, smieci, ramka przerwana ----
 {B s={0x00,0x94,0x94,0xC3,0x00,0x02,0x3a,0x00,0x11,0x94,0xC3,0x00,0x05,0x18,0xEE,0xFF,0x83,0x06};std::vector<B> got;api::Framer fr;uint32_t t=0;for(auto c:s)fr.feed(c,t++,[&](const uint8_t* p,size_t n){got.push_back(B(p,p+n));});
  assert(got.size()==2&&got[0]==(B{0x3a,0x00})&&got[1].size()==5);
  fr.feed(0x94,100,[](const uint8_t*,size_t){assert(0);});fr.feed(0xC3,101,[](const uint8_t*,size_t){assert(0);});fr.feed(0x00,102,[](const uint8_t*,size_t){assert(0);});
  int n=0;for(uint8_t c:{0x94,0xC3,0x00,0x02,0x3a,0x00})fr.feed(c,5000,[&](const uint8_t*,size_t){++n;});assert(n==1&&fr.errors==1);}   // przerwana ramka porzucona po 1,5 s
 // ---- czas: zegar lokalny E213 -> UTC dla CYD (CYD przelicza z powrotem na czas Polski) ----
 {const uint32_t T[]={1791650820,1768478400,1774745999,1774746000,1792889999+3601,1835479800,1761404700};
  for(uint32_t utc:T){uint32_t local=utc+uint32_t(mesh::warsawOffset(utc));assert(api::localToUtc(local)==utc);
   mesh::Civil c=mesh::civilLocal(utc);int y;unsigned mo,d;api::civilFromDays(local/86400,y,mo,d);assert(c.y==y&&c.mo==mo&&c.d==d&&c.h==(local%86400)/3600);}
  assert(api::localToUtc(0)==0&&api::localToUtc(1600000000)==0);}
 puts("link OK");return 0;}
