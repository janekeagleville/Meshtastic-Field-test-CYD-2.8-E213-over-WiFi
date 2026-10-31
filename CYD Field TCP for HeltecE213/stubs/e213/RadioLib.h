#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <deque>
#include <vector>
#define RADIOLIB_ERR_NONE 0
#define RADIOLIB_ERR_CRC_MISMATCH (-7)
typedef uint64_t RadioLibTime_t;
struct Module{Module(uint32_t,uint32_t,uint32_t,uint32_t,SPIClass&){}};
// Sterowany z testu symulator SX1262: kolejka odbieranych pakietow, zapis nadawanych, zadany poziom szumu.
struct SimPkt{std::vector<uint8_t> d;float rssi,snr;int err;};
struct SimRadio{
 std::deque<SimPkt> rx;std::vector<std::vector<uint8_t>> tx;std::vector<uint32_t> txAt;
 bool irq=false,rxMode=false,txMode=false,beginOk=true;SimPkt cur;float noise=-110;
 float freq=0,bw=0;uint8_t sf=0,cr=0,sync=0;uint16_t pre=0;int8_t pwr=0;int calls=0;std::vector<float> freqLog;
 void arm(){irq=!rx.empty();}
};
extern SimRadio gSim;
struct SX1262{
 SX1262(Module*){}
 int16_t begin(float f,float b,uint8_t s,uint8_t c,uint8_t sw,int8_t p,uint16_t pl,float tcxo,bool ldo){(void)tcxo;(void)ldo;if(!gSim.beginOk)return -2;gSim.freq=f;gSim.bw=b;gSim.sf=s;gSim.cr=c;gSim.sync=sw;gSim.pwr=p;gSim.pre=pl;return 0;}
 int16_t sleep(bool=true){gSim.rxMode=gSim.txMode=false;return 0;}
 int16_t standby(){gSim.rxMode=gSim.txMode=false;gSim.irq=false;return 0;}
 int16_t setBandwidth(float b){gSim.bw=b;return 0;}
 int16_t setSpreadingFactor(uint8_t s){gSim.sf=s;return 0;}
 int16_t setCodingRate(uint8_t c){gSim.cr=c;return 0;}
 int16_t setSyncWord(uint8_t s,uint8_t=0x44){gSim.sync=s;return 0;}
 int16_t setPreambleLength(size_t p){gSim.pre=uint16_t(p);return 0;}
 int16_t setFrequency(float f){gSim.freq=f;gSim.freqLog.push_back(f);return 0;}
 int16_t setOutputPower(int8_t p){gSim.pwr=p;return 0;}
 int16_t setDio2AsRfSwitch(bool=true){return 0;}
 int16_t setCurrentLimit(float){return 0;}
 int16_t setRxBoostedGainMode(bool,bool=true){return 0;}
 int16_t startReceive(){gSim.rxMode=true;gSim.txMode=false;gSim.arm();return 0;}
 int16_t startTransmit(const uint8_t* d,size_t n,uint8_t=0){gSim.tx.emplace_back(d,d+n);gSim.txAt.push_back(uint32_t(millis()));gSim.txMode=true;gSim.rxMode=false;gSim.irq=true;return 0;}
 int16_t finishTransmit(){gSim.txMode=false;gSim.irq=false;return 0;}
 size_t getPacketLength(bool=true){gSim.cur=gSim.rx.empty()?SimPkt{{},0,0,0}:gSim.rx.front();return gSim.cur.d.size();}
 int16_t readData(uint8_t* b,size_t n){if(gSim.rx.empty())return -1;SimPkt p=gSim.rx.front();gSim.rx.pop_front();gSim.cur=p;gSim.irq=false;size_t k=std::min(n,p.d.size());memcpy(b,p.d.data(),k);return p.err;}
 float getRSSI(bool packet){return packet?gSim.cur.rssi:gSim.noise;}
 float getRSSI(){return gSim.cur.rssi;}
 float getSNR(){return gSim.cur.snr;}
 RadioLibTime_t getTimeOnAir(size_t len){float ts=(1<<gSim.sf)/(gSim.bw*1000.f);int de=(gSim.sf>=11&&gSim.bw<=125)?1:0;float tp=(gSim.pre+4.25f)*ts;
  float n=8+std::max(ceilf((8.f*len-4.f*gSim.sf+28+16)/(4.f*(gSim.sf-2*de)))*gSim.cr,0.f);return RadioLibTime_t((tp+n*ts)*1e6f);}
};
