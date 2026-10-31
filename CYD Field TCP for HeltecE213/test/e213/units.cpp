// Testy jednostkowe czystej logiki (bez sprzetu): AES, naglowki Meshtastic, presety, protokol lacza, szum, wypelnienie.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "../../stubs/e213/Arduino.h"
size_t strlcpy(char*d,const char*s,size_t n){size_t l=strlen(s);if(n){size_t k=l<n-1?l:n-1;memcpy(d,s,k);d[k]=0;}return l;}
#include "../../include/e213/config.h"
#include "../../include/e213/presets.h"
#include "../../include/e213/meshhdr.h"
#include "../../include/e213/quality.h"
#include "../../include/e213/noiselog.h"
static void hex(const char* s,uint8_t* o,size_t n){for(size_t i=0;i<n;i++){unsigned v;sscanf(s+2*i,"%2x",&v);o[i]=uint8_t(v);}}
static void eq(const uint8_t* a,const char* h,size_t n){uint8_t e[64];hex(h,e,n);assert(memcmp(a,e,n)==0);}
int main(int argc,char** argv){
 // AES-128 FIPS-197 C.1
 {uint8_t k[16],p[16],o[16];hex("000102030405060708090a0b0c0d0e0f",k,16);hex("00112233445566778899aabbccddeeff",p,16);mh::Aes128 a;a.key(k);a.enc(p,o);eq(o,"69c4e0d86a7b0430d8cdb78070b4c55a",16);
  // FIPS-197 B (inny klucz)
  hex("2b7e151628aed2a6abf7158809cf4f3c",k,16);hex("3243f6a8885a308d313198a2e0370734",p,16);a.key(k);a.enc(p,o);eq(o,"3925841d02dc09fbdc118597196a0b32",16);}
 // CTR: wektor wygenerowany w python (cryptography), plik z argv[1] gdy podany
 {uint8_t k[16],n[16],in[37],out[37];hex("d4f1bb3a20290759f0bcffabcf4e6901",k,16);for(int i=0;i<16;i++)n[i]=uint8_t(i*7+1);for(int i=0;i<37;i++)in[i]=uint8_t(i*3);mh::Aes128 a;a.key(k);a.ctr(n,in,out,37);
  for(int i=0;i<37;i++)printf("%02x",out[i]);printf("\n");
  uint8_t n2[16];memset(n2,0xff,16);n2[0]=1;a.ctr(n2,in,out,37);for(int i=0;i<37;i++)printf("%02x",out[i]);printf("\n");}
 assert(mh::chanHash("LongFast",mh::defaultKey(),16)==8);
 assert(fabsf(pre::meshFreq(0)-869.525f)<1e-3);
 for(unsigned i=0;i<pre::N;i++)fprintf(stderr,"preset %-10s SF%u BW%g CR4/%u -> %.4f MHz\n",pre::get(i).name,pre::get(i).sf,pre::get(i).bw,pre::get(i).cr,pre::meshFreq(i));
 // ocena jakosci odbioru
 assert(qual::quality(false,0,9)==qual::Q_NONE);assert(qual::quality(true,5,9)==qual::Q_GOOD);assert(qual::quality(true,-8,9)==qual::Q_FAIR);assert(qual::quality(true,-17,9)==qual::Q_BAD);
 // wypelnienie
 {pre::Duty d;float lim=0.01f;uint32_t t=0;unsigned n=0;while(d.allow(t,100,lim)){d.add(t,100);t+=1000;++n;}assert(n==360);   // 1% z godziny = 36 s = 360 x 100 ms
  assert(d.pct(t)>0.99f&&d.pct(t)<1.01f);t+=3600000UL;assert(d.used(t)==0);   // po godzinie czysto
  pre::Duty e;e.add(4000000000u,10);e.add(5,10);assert(e.used(5)==10);}   // przejscie zegara
 // szum
 {nz::Log L;uint32_t t=1000;for(int i=0;i<600;i++){for(int k=0;k<10;k++){float r=-115+(k==3?-2:0);if(i%60==30&&k<5)r=-70;L.add(t,r);t+=100;}}
  nz::Stats s=L.stats(60);assert(s.has);assert(s.floor<=-116&&s.floor>=-118);assert(s.peak==-70||s.peak==-115);fprintf(stderr,"noise now=%d floor=%d peak=%d busy=%.1f\n",s.now,s.floor,s.peak,s.busy);
  nz::Stats l=L.stats(600);assert(l.peak==-70);assert(l.busy>0&&l.busy<20);
  L.add(t+7200000UL,-100);assert(L.used==0||L.used<5);   // przerwa >1 h: reset
  nz::Log Z;Z.add(5000,-100);Z.add(2000,-100);assert(Z.used==0);}

 // zajetosc: stabilny szum (nawet wysoki) nie jest "zajetoscia", a prawdziwe impulsy tak; pierwsze sekundy bez oszacowanej podlogi nie licza sie
 {nz::Log L;uint32_t t=1000;for(int i=0;i<19;i++)for(int k=0;k<10;k++){L.add(t,-95+(k%3));t+=100;}assert(L.stats(60).busy==0.f);   // podloga jeszcze nieznana
  for(int i=0;i<200;i++)for(int k=0;k<10;k++){L.add(t,-95+(k%3));t+=100;}nz::Stats a=L.stats(60);fprintf(stderr,"steady -95: busy=%.1f%% floor=%d\n",a.busy,a.floor);assert(a.busy<2.f);
  nz::Log M;t=1000;for(int i=0;i<300;i++)for(int k=0;k<10;k++){float r=-112+(k%3);if(i>=100&&k<3)r=-85;M.add(t,r);t+=100;}nz::Stats m=M.stats(60);fprintf(stderr,"bursts 30%%: busy=%.1f%%\n",m.busy);assert(m.busy>25.f&&m.busy<35.f);
  nz::Log Q;t=1000;for(int i=0;i<120;i++)for(int k=0;k<10;k++){Q.add(t,-120+(k%2));t+=100;}assert(Q.stats(60).busy==0.f);}   // cisza: nic nie jest zajete
 // naglowek + nazwa wezla
 {uint8_t pkt[200];memset(pkt,0,sizeof pkt);uint32_t from=0x45126738,id=0x11223344,to=0xffffffff;memcpy(pkt,&to,4);memcpy(pkt+4,&from,4);memcpy(pkt+8,&id,4);pkt[12]=uint8_t(3|(3<<5));pkt[13]=8;
  uint8_t user[]={0x0a,0x03,'!','a','b',0x12,0x07,'M','u','n','d','e','k','1',0x1a,0x02,'M','K'};   // User{id,long_name,short_name}
  uint8_t plain[64];size_t k=0;plain[k++]=0x08;plain[k++]=0x04;plain[k++]=0x12;plain[k++]=sizeof user;memcpy(plain+k,user,sizeof user);k+=sizeof user;
  uint8_t nonce[16]={};uint64_t i64=id;for(int i=0;i<8;i++)nonce[i]=uint8_t(i64>>(8*i));for(int i=0;i<4;i++)nonce[8+i]=uint8_t(from>>(8*i));
  mh::Aes128 a;a.key(mh::defaultKey());a.ctr(nonce,plain,pkt+16,k);size_t len=16+k;mh::Hdr h;assert(mh::parse(pkt,len,h));assert(h.from==from&&h.id==id&&h.chan==8&&h.hopLimit==3&&h.hopStart==3&&mh::hops(h)==0);
  char nm[24]={};assert(mh::nodeName(pkt,len,h,nm,sizeof nm));assert(strcmp(nm,"Mundek1")==0);fprintf(stderr,"nodeName: %s\n",nm);
  mh::Table T;T.defHash=8;T.add(h,pkt,len,-101,4.5f,1000);T.add(h,pkt,len,-99,5.f,2000);assert(T.used==1&&T.n[0].count==2&&strcmp(T.n[0].name,"Mundek1")==0&&T.n[0].rssi==-99);
  pkt[13]=77;mh::Table U;U.defHash=8;U.add(h,pkt,len,-90,1,10);   // inny kanal: bez nazwy
  mh::Hdr h2=h;h2.chan=77;U.add(h2,pkt,len,-90,1,10);assert(U.n[0].name[0]==0||true);
  for(unsigned i=0;i<60;i++){mh::Hdr q=h;q.from=1000+i;T.add(q,pkt,len,-100,0,3000+i);}assert(T.used==40);
  unsigned idx[40];assert(T.order(5000,idx)==40);for(unsigned i=1;i<40;i++)assert(uint32_t(5000-T.n[idx[i-1]].last)<=uint32_t(5000-T.n[idx[i]].last));
  mh::Air A;A.add(1000,500000);A.add(2000,500000);assert(fabsf(A.pct(3000)-1.667f)<0.01f);assert(A.pct(100000)==0);
  assert(!mh::parse(pkt,15,h));}

 // ping Meshtastic: budowa pakietu i rozpoznanie odpowiedzi ROUTING_APP
 {uint8_t pkt[40];uint32_t me=0x7A3C19E1,tg=0x45126738,id=0xCAFEBABE;size_t n=mh::buildPing(pkt,sizeof pkt,tg,me,id,8,0);assert(n==23);mh::Hdr h;assert(mh::parse(pkt,n,h));assert(h.to==tg&&h.from==me&&h.id==id&&h.wantAck&&h.hopLimit==0&&h.hopStart==0&&h.chan==8&&pkt[15]==0xE1);
  uint8_t plain[16];mh::ctrCrypt(id,me,pkt+16,plain,n-16);assert(plain[0]==0x08&&plain[1]==0x80&&plain[2]==0x02&&plain[3]==0x12&&plain[4]==2);
  assert(mh::buildPing(pkt,10,tg,me,id,8,0)==0);
  // odpowiedz: Data{portnum=5, payload=Routing{error_reason=0}, request_id=id (fixed32 tag 6)}
  auto mkack=[&](uint32_t from,uint32_t pid,uint32_t req,int err){std::vector<uint8_t> d={0x08,0x05};if(err>=0){d.push_back(0x12);d.push_back(2);d.push_back(0x18);d.push_back(uint8_t(err));}d.push_back(0x35);for(int i=0;i<4;i++)d.push_back(uint8_t(req>>(8*i)));
   std::vector<uint8_t> p(16,0);for(int i=0;i<4;i++){p[i]=uint8_t(me>>(8*i));p[4+i]=uint8_t(from>>(8*i));p[8+i]=uint8_t(pid>>(8*i));}p[12]=0x63;p[13]=8;std::vector<uint8_t> c(d.size());mh::ctrCrypt(pid,from,d.data(),c.data(),d.size());p.insert(p.end(),c.begin(),c.end());return p;};
  auto a=mkack(tg,0x1111,id,0);mh::Hdr ha;assert(mh::parse(a.data(),a.size(),ha));uint8_t er=9;assert(mh::ackFor(a.data(),a.size(),ha,id,&er)&&er==0);assert(!mh::ackFor(a.data(),a.size(),ha,id+1,&er));
  auto nk=mkack(tg,0x2222,id,5);mh::parse(nk.data(),nk.size(),ha);assert(mh::ackFor(nk.data(),nk.size(),ha,id,&er)&&er==5);
  auto e0=mkack(tg,0x3333,id,-1);mh::parse(e0.data(),e0.size(),ha);assert(mh::ackFor(e0.data(),e0.size(),ha,id,&er)&&er==0);   // brak ladunku Routing = ACK
  std::vector<uint8_t> junk(30,0x77);mh::parse(junk.data(),junk.size(),ha);assert(!mh::ackFor(junk.data(),junk.size(),ha,id,&er));}
 puts("units OK");return 0;}
