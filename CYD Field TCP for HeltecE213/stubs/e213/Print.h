#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
class Print{public:virtual ~Print(){}virtual size_t write(uint8_t)=0;virtual size_t write(const uint8_t*b,size_t n){size_t k=0;while(n--)k+=write(*b++);return k;}
 size_t write(const char*s){return s?write((const uint8_t*)s,strlen(s)):0;}size_t print(const char*s){return write(s);}};
