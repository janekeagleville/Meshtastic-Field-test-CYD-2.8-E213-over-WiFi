#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <string>
#include <math.h>
#include <algorithm>
#define constrain(a,lo,hi) ((a)<(lo)?(lo):((a)>(hi)?(hi):(a)))
using std::min;
using std::max;
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define ADC_2_5db 1
class String{public:std::string s;String(){}String(const char*c):s(c){}String(int v):s(std::to_string(v)){}String(unsigned v):s(std::to_string(v)){}String(long v):s(std::to_string(v)){}String(unsigned long v):s(std::to_string(v)){}
 String operator+(const String&o)const{String r;r.s=s+o.s;return r;}String operator+(const char*c)const{String r;r.s=s+c;return r;}friend String operator+(const char*c,const String&o){String r;r.s=std::string(c)+o.s;return r;}const char*c_str()const{return s.c_str();}unsigned length()const{return s.size();}};
unsigned long millis();void delay(unsigned long);void delayMicroseconds(unsigned);uint32_t esp_random();
void pinMode(int,int);void digitalWrite(int,int);int digitalRead(int);size_t strlcpy(char*,const char*,size_t);
void analogSetPinAttenuation(int,int);uint32_t analogReadMilliVolts(int);
struct SerialC{void begin(unsigned);int printf(const char*,...);void println(const char*);};extern SerialC Serial;
#ifndef PROGMEM
#define PROGMEM
#define pgm_read_byte(a) (*(const uint8_t*)(a))
#define pgm_read_word(a) (*(const uint16_t*)(a))
#define pgm_read_dword(a) (*(const uint32_t*)(a))
#define pgm_read_pointer(a) (*(void* const*)(a))
class __FlashStringHelper;
#define F(s) (reinterpret_cast<const __FlashStringHelper*>(s))
inline void yield(){}
#endif
#ifndef radians
#define radians(d) ((d)*0.017453292519943295)
#endif
