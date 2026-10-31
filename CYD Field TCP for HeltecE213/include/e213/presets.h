#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
namespace pre {
// Presety modemu Meshtastic dla regionow EU z aktualnego firmware (develop): EU_868 (869,4-869,65 MHz), EU_866 (presety Lite, 865,6-867,6 MHz)
// i EU_N_868 (presety Narrow, 869,4-869,65 MHz). Presety VeryLongSlow i ShortTurbo nie sa dostepne w tych regionach.
struct Region{float start,end,spacing,padding;};   // MHz; szerokosc slotu = spacing + 2*padding + BW
struct Preset{const char* name;uint8_t sf;float bw;uint8_t cr;uint8_t region;};   // cr 5..8 = kodowanie 4/5..4/8; region: indeks w regions()
inline const Region& regions(unsigned i){static const Region r[3]={{869.4f,869.65f,0,0},{865.6f,867.6f,0.4f,0.0375f},{869.4f,869.65f,0,0.0104f}};return r[i<3?i:0];}
constexpr unsigned N=11;
inline const Preset& get(unsigned i){static const Preset p[N]={{"LongFast",11,250,5,0},{"LongSlow",12,125,8,0},{"MediumSlow",10,250,5,0},{"MediumFast",9,250,5,0},{"ShortSlow",8,250,5,0},{"ShortFast",7,250,5,0},
 {"LongMod",11,125,8,0},{"LiteFast",9,125,5,1},{"LiteSlow",10,125,5,1},{"NarrowFast",7,62.5f,6,2},{"NarrowSlow",8,62.5f,6,2}};return p[i<N?i:0];}
inline const char* regionName(unsigned i){return get(i).region==1?"EU 866":get(i).region==2?"EU N 868":"EU 868";}
// Czestotliwosc srodkowa kanalu Meshtastic jak w firmware: liczba slotow = round((koniec-poczatek+spacing)/szerokosc slotu),
// numer slotu = hash djb2(nazwa presetu) % liczba slotow, f = poczatek + BW/2 + padding + slot * szerokosc slotu.
inline float meshFreq(unsigned i){const Preset& p=get(i);const Region& r=regions(p.region);float w=r.spacing+2*r.padding+p.bw/1000.f;unsigned n=unsigned(roundf((r.end-r.start+r.spacing)/w));
 if(!n)return(r.start+r.end)/2;uint32_t h=5381;for(const char* c=p.name;*c;c++)h=((h<<5)+h)+uint8_t(*c);unsigned slot=h%n;return r.start+p.bw/2000.f+r.padding+slot*w;}
// Moc nadawania (uzywana przy pingu wezla).
constexpr unsigned NPWR=5,NBCN=5;
// Odstep miedzy beaconami (NODEINFO + pozycja) w minutach; 0 = wylaczone.
inline unsigned beaconMin(unsigned i){static const unsigned m[NBCN]={0,15,30,60,180};return m[i<NBCN?i:0];}
inline int linkPower(unsigned i){static const int p[NPWR]={2,10,14,17,22};return p[i<NPWR?i:0];}
// Limit wypelnienia (ETSI EN 300 220): 869,4-869,65 MHz 10 %, 865,6-867,6 MHz 2,5 % (jak dla urzadzen mobilnych w Meshtastic), 868,0-868,6 MHz 1 %, reszta 0,1-1 %. Moc: 868,0-868,6 MHz max 14 dBm.
inline float dutyLimit(float mhz){if(mhz>=869.4f&&mhz<=869.65f)return 0.10f;if(mhz>=865.6f&&mhz<=867.6f)return 0.025f;if(mhz>=868.0f&&mhz<=868.6f)return 0.01f;return 0.001f;}
inline int powerLimit(float mhz){return(mhz>=868.0f&&mhz<=868.6f)?14:22;}
// Czas nadawania w ms z ostatnich 60 minut (kubelki minutowe), sluzy do pilnowania limitu wypelnienia.
struct Duty{uint32_t ms[60]={};uint32_t minute=0;bool started=false;
 void roll(uint32_t nowMs){uint32_t m=nowMs/60000;if(!started||m<minute){started=true;minute=m;memset(ms,0,sizeof ms);return;}while(minute<m){++minute;ms[minute%60]=0;if(m-minute>60){minute=m;memset(ms,0,sizeof ms);}}}
 void add(uint32_t nowMs,uint32_t airMs){roll(nowMs);ms[minute%60]+=airMs;}
 uint32_t used(uint32_t nowMs){roll(nowMs);uint32_t s=0;for(unsigned i=0;i<60;i++)s+=ms[i];return s;}
 float pct(uint32_t nowMs){return used(nowMs)/36000.f;}                              // % z godziny (3 600 000 ms)
 bool allow(uint32_t nowMs,uint32_t airMs,float limit){return used(nowMs)+airMs<=uint32_t(limit*3600000.f);}};
}
