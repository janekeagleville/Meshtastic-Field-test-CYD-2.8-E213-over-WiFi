#pragma once
#include <stdint.h>
#include <string.h>
#include <algorithm>
// Prawdziwy pomiar szumu kanalu: chwilowy RSSI radia SX1262 (10 odczytow na sekunde) agregowany do kubelkow 1 s.
// Kubelek: srednia, minimum, maksimum i odsetek odczytow "zajetych" (energia > podloga + BUSY_DB). Pamiec: 1 godzina.
namespace nz {
constexpr unsigned N=3600;
constexpr int NONE=-128;          // brak danych w kubelku
constexpr int BUSY_DB=8;          // odczyt jest "zajety", gdy przekracza szacowana podloge o tyle dB
constexpr int BUSY_MIN_DBM=-110;  // ...i jednoczesnie jest glosniejszy niz ta wartosc bezwzgledna
constexpr unsigned REF_MIN_SEC=20; // zajetosc liczona dopiero, gdy podloga ma co najmniej tyle sekund danych
struct Stats{bool has=false;int now=0,floor=0,peak=0;float busy=0;unsigned seconds=0;};
struct Log{int8_t avg[N],lo[N],hi[N];uint8_t busy[N];unsigned head=0,used=0;
 // akumulator biezacej sekundy
 uint32_t curSec=0;bool started=false;int cnt=0,sum=0,cLo=0,cHi=0,cBusy=0;int ref=-120;bool refOk=false;   // ref = biezaca podloga (dBm), refOk = oszacowana z danych
 Log(){reset();}
 void reset(){memset(avg,NONE,sizeof avg);memset(lo,NONE,sizeof lo);memset(hi,NONE,sizeof hi);memset(busy,0,sizeof busy);head=used=0;started=false;cnt=0;ref=-120;refOk=false;}
 void close(){ // zamkniecie biezacej sekundy
  if(cnt){avg[head]=int8_t(sum/cnt);lo[head]=int8_t(cLo);hi[head]=int8_t(cHi);busy[head]=uint8_t(cBusy*100/cnt);}else{avg[head]=lo[head]=hi[head]=NONE;busy[head]=0;}
  head=(head+1)%N;if(used<N)++used;cnt=sum=cBusy=0;updateRef();}
 // podloga = 25. percentyl srednich sekundowych z ostatnich 300 s (odporna na impulsy; potrzebuje REF_MIN_SEC sekund danych)
 void updateRef(){int v[300];unsigned k=0;for(unsigned a=1;a<=300&&a<=used;a++){int x=avg[(head+N-a)%N];if(x!=NONE)v[k++]=x;}if(k>=REF_MIN_SEC){unsigned p=(k-1)/4;std::nth_element(v,v+p,v+k);ref=v[p];refOk=true;}}
 void add(uint32_t nowMs,float rssi){uint32_t s=nowMs/1000;int r=int(rssi+(rssi>=0?0.5f:-0.5f));if(r<-127)r=-127;if(r>0)r=0;
  if(started&&(s<curSec||s-curSec>N))reset();            // cofniety zegar lub przerwa dluzsza niz godzina: zaczynamy od zera
  if(!started){started=true;curSec=s;cnt=sum=cBusy=0;}
  while(curSec<s){close();++curSec;}
  if(!cnt){cLo=cHi=r;}else{cLo=std::min(cLo,r);cHi=std::max(cHi,r);}++cnt;sum+=r;if(refOk&&r>ref+BUSY_DB&&r>BUSY_MIN_DBM)++cBusy;}
 // i-ty kubelek od najnowszego (0 = ostatnia zamknieta sekunda)
 int idx(unsigned ago)const{return int((head+N-1-ago)%N);}
 Stats stats(unsigned windowSec)const{Stats o;unsigned w=std::min(windowSec,used);static int v[N];unsigned k=0;long bs=0;unsigned bn=0;o.peak=NONE;
  for(unsigned a=0;a<w;a++){int i=idx(a);if(avg[i]==NONE)continue;if(!o.has){o.has=true;o.now=avg[i];}v[k++]=lo[i];if(hi[i]>o.peak)o.peak=hi[i];bs+=busy[i];++bn;}
  o.seconds=bn;if(k){unsigned p=(k-1)/10;std::nth_element(v,v+p,v+k);o.floor=v[p];o.busy=float(bs)/bn;}return o;}
};
}
