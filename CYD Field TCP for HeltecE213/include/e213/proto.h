#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
namespace mesh {
struct Field { unsigned tag=0, wire=0; uint64_t value=0; const uint8_t* bytes=nullptr; size_t size=0; };
class Reader {
 const uint8_t* p; size_t n; bool good=true;
 bool var(uint64_t &v) { v=0; for(unsigned i=0;i<10;i++){if(!n) return good=false; uint8_t b=*p++; --n; if(i==9 && b>1) return good=false; v|=uint64_t(b&127)<<(7*i); if(!(b&128))return true;}return good=false; }
 public:
 Reader(const uint8_t* b,size_t len):p(b),n(len){}
 bool ok()const{return good;}
 bool next(Field& f){ if(!good||!n)return false; f=Field{};uint64_t key;if(!var(key)||!(key>>3)||key>>3>0x1fffffff)return good=false;f.tag=key>>3;f.wire=key&7;
 if(f.wire==0)return var(f.value);
 if(f.wire==1||f.wire==5){size_t k=f.wire==1?8:4;if(n<k)return good=false;for(size_t i=0;i<k;i++)f.value|=uint64_t(p[i])<<(8*i);p+=k;n-=k;return true;}
 if(f.wire==2){uint64_t len;if(!var(len)||len>n)return good=false;f.bytes=p;f.size=size_t(len);p+=f.size;n-=f.size;return true;}return good=false; }
};
inline float asFloat(uint64_t v){uint32_t bits=uint32_t(v);float f;memcpy(&f,&bits,4);return f;}
inline void copyText(char* dst,size_t cap,const Field& f){if(!cap)return;size_t k=f.size<cap-1?f.size:cap-1;memcpy(dst,f.bytes,k);dst[k]=0;}
// Display-safe name: the TFT bitmap fonts are ASCII-only, so UTF-8 is transliterated (Polish and common
// Latin letters), emoji/symbols/unknown scripts are dropped, controls become spaces, runs of spaces collapse,
// and a multi-byte sequence cut off by the buffer end is discarded. Returns the output length (0 = nothing printable).
inline const char* translit(uint32_t c){
 switch(c){case 0x105:return "a";case 0x107:return "c";case 0x119:return "e";case 0x142:return "l";case 0x144:return "n";case 0xF3:return "o";case 0x15B:return "s";case 0x17A:case 0x17C:return "z";
  case 0x104:return "A";case 0x106:return "C";case 0x118:return "E";case 0x141:return "L";case 0x143:return "N";case 0xD3:return "O";case 0x15A:return "S";case 0x179:case 0x17B:return "Z";
  case 0xE4:case 0xE0:case 0xE1:case 0xE2:case 0xE3:case 0xE5:return "a";case 0xE8:case 0xE9:case 0xEA:case 0xEB:return "e";case 0xEC:case 0xED:case 0xEE:case 0xEF:return "i";
  case 0xF2:case 0xF4:case 0xF5:case 0xF6:return "o";case 0xF9:case 0xFA:case 0xFB:case 0xFC:return "u";case 0xE7:return "c";case 0xF1:return "n";case 0xDF:return "ss";
  case 0xC4:case 0xC0:case 0xC1:case 0xC2:case 0xC3:case 0xC5:return "A";case 0xC8:case 0xC9:case 0xCA:case 0xCB:return "E";case 0xCC:case 0xCD:case 0xCE:case 0xCF:return "I";
  case 0xD2:case 0xD4:case 0xD5:case 0xD6:return "O";case 0xD9:case 0xDA:case 0xDB:case 0xDC:return "U";case 0xC7:return "C";case 0xD1:return "N";
  case 0xA0:return " ";case 0x2013:case 0x2014:return "-";case 0x2018:case 0x2019:return "'";case 0x201C:case 0x201D:return "\"";}
 return nullptr;}
inline size_t displayName(const char* in,char* out,size_t cap){
 if(!cap)return 0;size_t o=0;bool space=true;
 auto put=[&](const char* t){for(;*t&&o+1<cap;t++){if(*t==' '){if(space)continue;space=true;}else space=false;out[o++]=*t;}};
 for(size_t i=0;in&&in[i];){uint8_t b=uint8_t(in[i]);
  if(b<0x80){char t[2]={b>=0x20&&b!=0x7F?char(b):' ',0};put(t);i++;continue;}
  unsigned n=(b>=0xF0&&b<=0xF4)?3:(b>=0xE0)?2:(b>=0xC2&&b<0xE0)?1:0;
  if(!n){i++;continue;}
  uint32_t c=n==1?(b&0x1F):n==2?(b&0x0F):(b&0x07);unsigned k=0;
  while(k<n&&in[i+1+k]&&(uint8_t(in[i+1+k])&0xC0)==0x80){c=(c<<6)|(uint8_t(in[i+1+k])&0x3F);k++;}
  i+=1+k;if(k<n)continue; // truncated or malformed sequence
  const char* t=translit(c);if(t)put(t);}
 while(o&&out[o-1]==' ')o--;out[o]=0;return o;}
}
