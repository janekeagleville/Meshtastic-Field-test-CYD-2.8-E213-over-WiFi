#pragma once
#include <Arduino.h>
#include <map>
#include <vector>
// Atrapa biblioteki BLE do testow na hoscie: rejestruje charakterystyki i pozwala wywolac callbacki jak z telefonu.
struct BLEDescriptor{virtual ~BLEDescriptor(){}};struct BLE2902:BLEDescriptor{};
struct BLECharacteristic;struct BLEServer;
struct BLECharacteristicCallbacks{virtual ~BLECharacteristicCallbacks(){}virtual void onWrite(BLECharacteristic*){}};
struct BLEServerCallbacks{virtual ~BLEServerCallbacks(){}virtual void onConnect(BLEServer*){}virtual void onDisconnect(BLEServer*){}};
struct BLECharacteristic{static const uint32_t PROPERTY_READ=1,PROPERTY_WRITE=2,PROPERTY_NOTIFY=4;
 std::string uuid,val;uint32_t props=0;BLECharacteristicCallbacks* cb=nullptr;int notifies=0;
 void setValue(uint8_t* d,size_t n){val.assign((const char*)d,n);}std::string getValue(){return val;}
 void setCallbacks(BLECharacteristicCallbacks* c){cb=c;}void addDescriptor(BLEDescriptor*){}void notify(){++notifies;}
 void phoneWrite(const std::string& v){val=v;if(cb)cb->onWrite(this);}};   // zapis z telefonu
inline std::map<std::string,BLECharacteristic*>& bleChars(){static std::map<std::string,BLECharacteristic*> m;return m;}
struct BLEService{BLECharacteristic* createCharacteristic(const char* u,uint32_t p){auto* c=new BLECharacteristic();c->uuid=u;c->props=p;bleChars()[u]=c;return c;}void start(){}};
struct BLEServer{BLEServerCallbacks* cb=nullptr;int disconnects=0;void setCallbacks(BLEServerCallbacks* c){cb=c;}BLEService* createService(const char*){return new BLEService();}
 void disconnect(uint16_t){++disconnects;if(cb)cb->onDisconnect(this);}uint16_t getConnId(){return 1;}};
struct BLEAdvertising{int started=0,stopped=0;bool scanResp=false;std::vector<std::string> uuids;void addServiceUUID(const char* u){uuids.push_back(u);}void setScanResponse(bool b){scanResp=b;}void stop(){++stopped;}};
struct BLEDevice{static BLEAdvertising*& adv(){static BLEAdvertising* a=new BLEAdvertising();return a;}static BLEServer*& srv(){static BLEServer* s=nullptr;return s;}
 static int& inits(){static int n=0;return n;}
 static void init(const char*){++inits();}static BLEServer* createServer(){srv()=new BLEServer();return srv();}static BLEAdvertising* getAdvertising(){return adv();}static void startAdvertising(){++adv()->started;}};
