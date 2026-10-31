#pragma once
// Warstwa sprzetowa e-ink Heltec Vision Master E213: GxEPD2 (fork Meshtastic) z wykrywaniem wersji panelu w locie.
// Wersja V1: LCMEN2R13EFC1 (Fitipower, BUSY aktywny LOW), V1.1: E0213A367 (SSD1682, BUSY aktywny HIGH).
// Detekcja jak w firmware Meshtastic (variants/esp32s3/heltec_vision_master_e213/einkDetect.h).
#include <stdint.h>
#include <stddef.h>
#include "config.h"
#ifdef HOST_SIM
namespace epdhw{
 extern int shows,fulls,hibernates;extern uint8_t last[];extern bool lastInv,lastFlip,lastPartial,present;
 inline bool begin(){return true;}
 inline void show(const uint8_t* buf,bool inv,bool flip,bool partial){++shows;if(!partial)++fulls;lastInv=inv;lastFlip=flip;lastPartial=partial;memcpy(last,buf,(SCR_W+7)/8*SCR_H);}
 inline void hibernate(){++hibernates;}
}
#else
#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <epd/GxEPD2_213_FC1.h>
#include <epd/GxEPD2_213_E0213A367.h>
namespace epdhw{
 typedef GxEPD2_213_FC1 DrvA;typedef GxEPD2_213_E0213A367 DrvB;
 typedef GxEPD2_BW<DrvA,DrvA::HEIGHT> DispA;typedef GxEPD2_BW<DrvB,DrvB::HEIGHT> DispB;
 inline SPIClass* spi=nullptr;inline DispA* a=nullptr;inline DispB* b=nullptr;
 inline bool begin(){
  // BUSY przy wymuszonym RESET: LOW = V1 (FC1), HIGH = V1.1 (E0213A367)
  pinMode(PIN_EINK_RST,OUTPUT);digitalWrite(PIN_EINK_RST,LOW);delay(10);pinMode(PIN_EINK_BUSY,INPUT);const bool busyHigh=digitalRead(PIN_EINK_BUSY);pinMode(PIN_EINK_RST,INPUT);
  spi=new SPIClass(HSPI);spi->begin(PIN_EINK_SCK,-1,PIN_EINK_MOSI,-1);
  if(!busyHigh){a=new DispA(DrvA(PIN_EINK_CS,PIN_EINK_DC,PIN_EINK_RST,PIN_EINK_BUSY,*spi));a->init(0,true,2,false);}
  else{b=new DispB(DrvB(PIN_EINK_CS,PIN_EINK_DC,PIN_EINK_RST,PIN_EINK_BUSY,*spi));b->init(0,true,2,false);}
  spi->begin(PIN_EINK_SCK,-1,PIN_EINK_MOSI,-1);   // GxEPD2 wola _spi.begin() bez argumentow: przywroc piny
  return true;}
 template<class D> inline void blit(D& d,const uint8_t* buf,bool inv,bool flip,bool partial){
  d.setRotation(flip?3:1);d.fillScreen(inv?GxEPD_BLACK:GxEPD_WHITE);d.drawBitmap(0,0,buf,SCR_W,SCR_H,inv?GxEPD_WHITE:GxEPD_BLACK);d.display(partial);}
 inline void show(const uint8_t* buf,bool inv,bool flip,bool partial){if(a)blit(*a,buf,inv,flip,partial);else if(b)blit(*b,buf,inv,flip,partial);}
 inline void hibernate(){if(a)a->hibernate();else if(b)b->hibernate();}
}
#endif
