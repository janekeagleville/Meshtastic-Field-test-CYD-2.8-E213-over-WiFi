#pragma once
#include <Arduino.h>
struct Preferences{void begin(const char*,bool);void end();size_t getBytesLength(const char*);size_t getBytes(const char*,void*,size_t);size_t putBytes(const char*,const void*,size_t);};
