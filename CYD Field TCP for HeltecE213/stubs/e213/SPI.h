#pragma once
#include <Arduino.h>
#define FSPI 0
#define HSPI 1
struct SPIClass{SPIClass(int=0){}void begin(int=-1,int=-1,int=-1,int=-1){}void end(){}};
