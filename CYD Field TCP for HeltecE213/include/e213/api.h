#pragma once
// API klienta Meshtastic po TCP (port 4403), podzbior wystarczajacy dla CYD Field Tester:
// ramki strumienia 0x94 0xC3 + dlugosc (big-endian) + FromRadio / ToRadio (protobuf), dokladnie jak w firmware Meshtastic.
// E213 udaje wezel Meshtastic: wysyla my_info, NodeInfo (siebie i slyszanych wezlow), Config.lora, kanal 0 i config_complete_id,
// a potem kazdy odebrany pakiet LoRa jako FromRadio.packet z RSSI/SNR. Pakiety od klienta (ToRadio.packet) szyfruje domyslnym kluczem i nadaje.
// Numery pol wg meshtastic/protobufs (mesh.proto, config.proto, channel.proto, telemetry.proto).
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
namespace api {
// Wlasny czytnik protobuf (kopia Reader), zeby api.h nie zalezal od proto.h i dal sie testowac razem z parserem CYD.
struct Field{unsigned tag=0,wire=0;uint64_t value=0;const uint8_t* bytes=nullptr;size_t size=0;};
class Reader{const uint8_t* p;size_t n;bool good=true;
 bool var(uint64_t& v){v=0;for(unsigned i=0;i<10;i++){if(!n)return good=false;uint8_t b=*p++;--n;if(i==9&&b>1)return good=false;v|=uint64_t(b&127)<<(7*i);if(!(b&128))return true;}return good=false;}
public:Reader(const uint8_t* b,size_t len):p(b),n(len){}
 bool ok()const{return good;}
 bool next(Field& f){if(!good||!n)return false;f=Field{};uint64_t key;if(!var(key)||!(key>>3)||key>>3>0x1fffffff)return good=false;f.tag=unsigned(key>>3);f.wire=unsigned(key&7);
  if(f.wire==0)return var(f.value);
  if(f.wire==1||f.wire==5){size_t k=f.wire==1?8:4;if(n<k)return good=false;for(size_t i=0;i<k;i++)f.value|=uint64_t(p[i])<<(8*i);p+=k;n-=k;return true;}
  if(f.wire==2){uint64_t len;if(!var(len)||len>n)return good=false;f.bytes=p;f.size=size_t(len);p+=f.size;n-=f.size;return true;}return good=false;}
};

// ---- Koder protobuf do bufora o stalej pojemnosci (ok=false po przepelnieniu) ----
struct Buf{uint8_t* p;size_t cap,n=0;bool ok=true;
 Buf(uint8_t* b,size_t c):p(b),cap(c){}
 void byte(uint8_t v){if(n<cap)p[n++]=v;else ok=false;}
 void raw(const void* d,size_t k){if(n+k<=cap){memcpy(p+n,d,k);n+=k;}else ok=false;}
 void var(uint64_t v){do{byte(uint8_t((v&127)|(v>127?128:0)));v>>=7;}while(v);}
 void key(unsigned t,unsigned w){var((uint64_t(t)<<3)|w);}
 void u(unsigned t,uint64_t v){key(t,0);var(v);}
 void i32(unsigned t,int32_t v){key(t,0);var(uint64_t(int64_t(v)));}           // int32: liczby ujemne jako 10-bajtowy varint
 void fx(unsigned t,uint32_t v){key(t,5);for(unsigned i=0;i<4;i++)byte(uint8_t(v>>(8*i)));}
 void flt(unsigned t,float f){uint32_t v;memcpy(&v,&f,4);fx(t,v);}
 void bytes(unsigned t,const void* d,size_t k){key(t,2);var(k);raw(d,k);}
 void str(unsigned t,const char* s){bytes(t,s,strlen(s));}
 void msg(unsigned t,const Buf& b){if(!b.ok)ok=false;bytes(t,b.p,b.n);}
};
// Ramka strumienia: 94 C3 len_hi len_lo + ladunek. Zwraca dlugosc ramki lub 0.
inline size_t frame(const uint8_t* pl,size_t n,uint8_t* out,size_t cap){if(n>512||cap<n+4)return 0;out[0]=0x94;out[1]=0xC3;out[2]=uint8_t(n>>8);out[3]=uint8_t(n);memcpy(out+4,pl,n);return n+4;}

// ---- Dekoder ramek strumienia (bajt po bajcie), z limitem czasu na niedokonczona ramke ----
class Framer{unsigned state=0;size_t length=0,used=0;uint8_t buf[512];uint32_t last=0;
public:unsigned errors=0;
 void reset(){state=0;length=used=0;}
 template<class F>void feed(uint8_t b,uint32_t now,F cb){if(state&&uint32_t(now-last)>1500){++errors;reset();}last=now;
  switch(state){case 0:if(b==0x94)state=1;break;case 1:if(b==0xC3)state=2;else state=b==0x94?1:0;break;case 2:length=size_t(b)<<8;state=3;break;
   case 3:length|=b;if(!length||length>sizeof buf){++errors;reset();}else{used=0;state=4;}break;case 4:buf[used++]=b;if(used==length){cb(buf,length);reset();}break;}}
};

// ---- Opisy wiadomosci FromRadio ----
struct NodeDesc{uint32_t num=0;const char* longName="";const char* shortName="";unsigned hw=255;float snr=0;uint32_t lastHeard=0;int hops=-1;
 bool hasPos=false;int32_t lat=0,lon=0,alt=0;bool hasMetrics=false;int battery=-1;float voltage=0,chUtil=0,airTx=0;uint32_t uptime=0;bool hasUser=true;};
struct PktDesc{uint32_t from=0,to=0,id=0,rxTime=0;unsigned channel=0,hopLimit=0,hopStart=0,relay=0;bool wantAck=false,viaMqtt=false,hasRx=true;float snr=0;int rssi=0;
 const uint8_t* decoded=nullptr;size_t decodedLen=0;const uint8_t* encrypted=nullptr;size_t encLen=0;};

inline void userMsg(Buf& u,uint32_t num,const char* lng,const char* sh,unsigned hw){char id[12];snprintf(id,sizeof id,"!%08lx",(unsigned long)num);u.str(1,id);u.str(2,lng);u.str(3,sh);u.u(5,hw);}
inline void metricsMsg(Buf& m,int battery,float voltage,float chUtil,float airTx,uint32_t uptime){if(battery>=0)m.u(1,unsigned(battery));if(voltage>0)m.flt(2,voltage);m.flt(3,chUtil);m.flt(4,airTx);m.u(5,uptime);}

inline size_t myInfo(uint8_t* o,size_t cap,uint32_t fid,uint32_t myId){Buf f(o,cap);f.u(1,fid);uint8_t t[16];Buf m(t,sizeof t);m.u(1,myId);f.msg(3,m);return f.ok?f.n:0;}
inline size_t nodeInfo(uint8_t* o,size_t cap,uint32_t fid,const NodeDesc& d){
 uint8_t nb[240];Buf n(nb,sizeof nb);n.u(1,d.num);
 if(d.hasUser){uint8_t ub[120];Buf u(ub,sizeof ub);userMsg(u,d.num,d.longName,d.shortName,d.hw);n.msg(2,u);}
 if(d.hasPos){uint8_t pb[40];Buf p(pb,sizeof pb);p.fx(1,uint32_t(d.lat));p.fx(2,uint32_t(d.lon));p.i32(3,d.alt);p.u(5,1);n.msg(3,p);}   // location_source = LOC_MANUAL
 n.flt(4,d.snr);if(d.lastHeard)n.fx(5,d.lastHeard);
 if(d.hasMetrics){uint8_t mb[40];Buf m(mb,sizeof mb);metricsMsg(m,d.battery,d.voltage,d.chUtil,d.airTx,d.uptime);n.msg(6,m);}
 if(d.hops>=0)n.u(9,unsigned(d.hops));
 Buf f(o,cap);f.u(1,fid);f.msg(4,n);return f.ok&&n.ok?f.n:0;}
// Config.lora: preset Meshtastic (enum) albo parametry jawne; zawsze z override_frequency, zeby klient znal dokladna czestotliwosc.
// bwKhz 62.5 kodowane jako 62 (konwencja Meshtastic). region: kod RegionCode (3 = EU_868, 29 = EU_866, 32 = EU_N_868).
inline size_t loraConfig(uint8_t* o,size_t cap,uint32_t fid,int presetEnum,unsigned sf,float bwKhz,unsigned cr,float mhz,unsigned region,int txPower){
 uint8_t lb[64];Buf l(lb,sizeof lb);bool usePreset=presetEnum>=0&&presetEnum<9;
 l.u(1,usePreset?1:0);if(usePreset)l.u(2,unsigned(presetEnum));
 l.u(3,unsigned(bwKhz==62.5f?62:bwKhz));l.u(4,sf);l.u(5,cr);l.u(7,region);l.u(8,3);l.u(9,1);l.i32(10,txPower);l.flt(14,mhz);
 uint8_t cb[80];Buf c(cb,sizeof cb);c.msg(6,l);Buf f(o,cap);f.u(1,fid);f.msg(5,c);return f.ok&&c.ok?f.n:0;}
// Kanal: index, settings{psk = 0x01 (domyslny klucz AQ==), name}, role (1 = PRIMARY)
inline size_t channel(uint8_t* o,size_t cap,uint32_t fid,unsigned index,const char* name,bool primary){
 uint8_t sb[48];Buf s(sb,sizeof sb);const uint8_t psk=1;s.bytes(2,&psk,1);s.str(3,name);
 uint8_t cb[64];Buf c(cb,sizeof cb);c.u(1,index);c.msg(2,s);c.u(3,primary?1:0);Buf f(o,cap);f.u(1,fid);f.msg(10,c);return f.ok&&c.ok?f.n:0;}
inline size_t configComplete(uint8_t* o,size_t cap,uint32_t fid,uint32_t id){Buf f(o,cap);f.u(1,fid);f.u(7,id);return f.ok?f.n:0;}
// MeshPacket w FromRadio.packet; decoded = gotowe bajty Data (np. odszyfrowany ladunek), encrypted = surowe bajty dla obcych kanalow.
inline size_t packet(uint8_t* o,size_t cap,uint32_t fid,const PktDesc& d){
 uint8_t mb[300];Buf m(mb,sizeof mb);m.fx(1,d.from);m.fx(2,d.to);if(d.channel)m.u(3,d.channel);
 if(d.decoded)m.bytes(4,d.decoded,d.decodedLen);else if(d.encrypted)m.bytes(5,d.encrypted,d.encLen);
 m.fx(6,d.id);if(d.rxTime)m.fx(7,d.rxTime);if(d.hasRx)m.flt(8,d.snr);m.u(9,d.hopLimit);if(d.wantAck)m.u(10,1);if(d.hasRx)m.i32(12,d.rssi);if(d.viaMqtt)m.u(14,1);m.u(15,d.hopStart);
 if(d.relay)m.u(19,d.relay);if(d.hasRx)m.u(21,1);   // transport_mechanism = TRANSPORT_LORA
 Buf f(o,cap);f.u(1,fid);f.msg(2,m);return f.ok&&m.ok?f.n:0;}
// Data dla ROUTING_APP z Routing{error_reason} i request_id (odpowiedz lokalnego wezla na pakiet klienta).
inline size_t dataRouting(uint8_t* o,size_t cap,uint32_t requestId,unsigned err){uint8_t rb[8];Buf r(rb,sizeof rb);r.u(3,err);Buf d(o,cap);d.u(1,5);d.msg(2,r);d.fx(6,requestId);return d.ok?d.n:0;}
// Data dla TELEMETRY_APP: Telemetry{time, device_metrics}
inline size_t dataTelemetry(uint8_t* o,size_t cap,uint32_t time,int battery,float voltage,float chUtil,float airTx,uint32_t uptime){
 uint8_t mb[40];Buf m(mb,sizeof mb);metricsMsg(m,battery,voltage,chUtil,airTx,uptime);uint8_t tb[56];Buf t(tb,sizeof tb);if(time)t.fx(1,time);t.msg(2,m);Buf d(o,cap);d.u(1,67);d.msg(2,t);return d.ok&&t.ok?d.n:0;}

// ---- Parser ToRadio ----
enum Kind{TR_NONE,TR_PACKET,TR_WANT_CONFIG,TR_HEARTBEAT,TR_DISCONNECT,TR_OTHER};
struct ToRadio{Kind kind=TR_NONE;uint32_t configId=0;
 uint32_t to=0,id=0;unsigned channel=0,hopLimit=0,port=0;bool wantAck=false,hasDecoded=false;const uint8_t* data=nullptr;size_t dataLen=0;};
inline bool parseToRadio(const uint8_t* b,size_t n,ToRadio& out){ToRadio t;Reader r(b,n);Field f;
 while(r.next(f)){
  if(f.tag==1&&f.wire==2){t.kind=TR_PACKET;Reader m(f.bytes,f.size);Field g;
   while(m.next(g)){if(g.tag==2&&g.wire==5)t.to=uint32_t(g.value);if(g.tag==3&&g.wire==0)t.channel=unsigned(g.value);if(g.tag==6&&g.wire==5)t.id=uint32_t(g.value);
    if(g.tag==9&&g.wire==0)t.hopLimit=unsigned(g.value);if(g.tag==10&&g.wire==0)t.wantAck=g.value!=0;
    if(g.tag==4&&g.wire==2){t.hasDecoded=true;t.data=g.bytes;t.dataLen=g.size;Reader d(g.bytes,g.size);Field h;while(d.next(h))if(h.tag==1&&h.wire==0)t.port=unsigned(h.value);if(!d.ok())return false;}}
   if(!m.ok())return false;}
  else if(f.tag==3&&f.wire==0){t.kind=TR_WANT_CONFIG;t.configId=uint32_t(f.value);}
  else if(f.tag==7&&f.wire==2)t.kind=TR_HEARTBEAT;
  else if(f.tag==4&&f.wire==0)t.kind=f.value?TR_DISCONNECT:TR_OTHER;
  else if(t.kind==TR_NONE)t.kind=TR_OTHER;}
 if(!r.ok())return false;out=t;return true;}

// ---- Mapowanie presetow E213 na enum ModemPreset i RegionCode Meshtastic ----
// Kolejnosc presetow E213: LongFast, LongSlow, MediumSlow, MediumFast, ShortSlow, ShortFast, LongMod, LiteFast, LiteSlow, NarrowFast, NarrowSlow
inline int presetEnum(unsigned i){static const int e[11]={0,1,3,4,5,6,7,10,11,12,13};return i<11?e[i]:-1;}
inline unsigned regionCode(unsigned presetRegion){return presetRegion==1?29:presetRegion==2?32:3;}

// ---- Czas: zegar E213 jest ustawiany czasem lokalnym (Polska); API Meshtastic uzywa UTC ----
inline int64_t daysFromCivil(int y,unsigned m,unsigned d){y-=m<=2;int era=(y>=0?y:y-399)/400;unsigned yoe=unsigned(y-era*400);unsigned doy=(153*(m>2?m-3:m+9)+2)/5+d-1;unsigned doe=yoe*365+yoe/4-yoe/100+doy;return int64_t(era)*146097+int64_t(doe)-719468;}
inline void civilFromDays(int64_t z,int& y,unsigned& m,unsigned& d){z+=719468;int64_t era=(z>=0?z:z-146096)/146097;unsigned doe=unsigned(z-era*146097);unsigned yoe=(doe-doe/1460+doe/36524-doe/146096)/365;y=int(yoe)+int(era)*400;unsigned doy=doe-(365*yoe+yoe/4-yoe/100);unsigned mp=(5*doy+2)/153;d=doy-(153*mp+2)/5+1;m=mp<10?mp+3:mp-9;y+=m<=2;}
inline int64_t lastSunday(int y,unsigned month){int64_t last=daysFromCivil(y,month,31);return last-((last+4)%7);}
// Przesuniecie czasu Polski (CET/CEST) dla chwili UTC
inline int32_t warsawOffset(uint32_t utc){int y;unsigned m,d;civilFromDays(utc/86400,y,m,d);int64_t s=lastSunday(y,3)*86400+3600,e=lastSunday(y,10)*86400+3600;return (int64_t(utc)>=s&&int64_t(utc)<e)?7200:3600;}
// Czas lokalny -> UTC (0 = zegar nieustawiony)
inline uint32_t localToUtc(uint32_t local){if(local<1700000000UL)return 0;uint32_t u=local-uint32_t(warsawOffset(local-3600));return local-uint32_t(warsawOffset(u));}
}
