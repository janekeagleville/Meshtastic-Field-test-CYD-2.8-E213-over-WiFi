#pragma once
// Plotno 1-bitowe 250x122 (landscape) z API zblizonym do TFT_eSPI, zbudowane na Adafruit_GFX (GFXcanvas1).
// Kolory: PAPER (0) = bialy, INK (1) = czarny, GREY (2) = szachownica (pol-ton). Wszystko inne traktowane jako INK.
#include <Adafruit_GFX.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <string>
#ifdef HOST_SIM
#include <vector>
struct TextRec{int x,y,w,h;std::string s;};
#endif
#ifndef TL_DATUM
#define TL_DATUM 0
#define TC_DATUM 1
#define TR_DATUM 2
#define ML_DATUM 3
#define MC_DATUM 4
#define MR_DATUM 5
#endif
namespace epd{
constexpr uint16_t PAPER=0,INK=1,GREY=2;
// numer czcionki "jak w TFT_eSPI": 1 = wbudowana 6x8, 2 = FreeSans 9 pt, 4 = FreeSansBold 12 pt
inline const GFXfont* fontOf(int f){return f==4?&FreeSansBold12pt7b:f==2?&FreeSansBold9pt7b:nullptr;}
}
class Canvas:public GFXcanvas1{
public:
 uint8_t datum=TL_DATUM;uint16_t fg=epd::INK,bgc=epd::PAPER;bool inverted=false;
 Canvas(uint16_t w,uint16_t h):GFXcanvas1(w,h){}
#ifdef HOST_SIM
 std::vector<TextRec> texts;int textOob=0;   // tylko testy na hoscie: zapis prostokatow tekstu (kontrola nakladania i wyjscia poza ekran)
#endif
 // --- kolory (GREY = szachownica) ---
 void drawPixel(int16_t x,int16_t y,uint16_t c)override{if(c==epd::GREY)c=((x+y)&1)?epd::INK:epd::PAPER;GFXcanvas1::drawPixel(x,y,c?1:0);}
 void drawFastHLine(int16_t x,int16_t y,int16_t w,uint16_t c)override{if(c==epd::GREY){for(int i=0;i<w;i++)drawPixel(x+i,y,c);}else GFXcanvas1::drawFastHLine(x,y,w,c?1:0);}
 void drawFastVLine(int16_t x,int16_t y,int16_t h,uint16_t c)override{if(c==epd::GREY){for(int i=0;i<h;i++)drawPixel(x,y+i,c);}else GFXcanvas1::drawFastVLine(x,y,h,c?1:0);}
 void fillSprite(uint16_t c){if(c==epd::GREY){for(int y=0;y<height();y++)for(int x=0;x<width();x++)drawPixel(x,y,c);}else GFXcanvas1::fillScreen(c?1:0);}
 // --- API w stylu TFT_eSPI ---
 void* createSprite(int,int){return this;}
 void setTextDatum(uint8_t d){datum=d;}
 void setTextColor(uint16_t f,uint16_t b){fg=f;bgc=b;}
 void fillRoundRect(int x,int y,int w,int h,int r,uint16_t c){Adafruit_GFX::fillRoundRect(x,y,w,h,r,c);}
 void drawRoundRect(int x,int y,int w,int h,int r,uint16_t c){Adafruit_GFX::drawRoundRect(x,y,w,h,r,c);}
 // metryka tekstu: top = odleglosc gornej krawedzi komorki od linii bazowej, h = wysokosc komorki
 void metrics(int font,int& top,int& h){const GFXfont* gf=epd::fontOf(font);if(!gf){top=0;h=8;return;}setFont(gf);int16_t x1,y1;uint16_t w,hh;getTextBounds("H0(",0,0,&x1,&y1,&w,&hh);int t=-y1;getTextBounds("g",0,0,&x1,&y1,&w,&hh);top=t;h=t+(y1+int(hh));}
 int textWidth(const char* s,int font){const GFXfont* gf=epd::fontOf(font);setFont(gf);setTextSize(1);int16_t x1,y1;uint16_t w,h;getTextBounds(s,0,0,&x1,&y1,&w,&h);return int(w);}
 int fontHeight(int font){int t,h;metrics(font,t,h);return h;}
 void drawString(const std::string& s,int x,int y,int font){drawStr(s.c_str(),x,y,font);}
 void drawStr(const char* s,int x,int y,int font){
  const GFXfont* gf=epd::fontOf(font);int top,h;metrics(font,top,h);setFont(gf);setTextSize(1);
  int w=textWidth(s,font);int ax=x,ay=y;
  switch(datum%3){case 1:ax=x-w/2;break;case 2:ax=x-w;break;}
  switch(datum/3){case 1:ay=y-h/2;break;case 2:ay=y-h;break;}
#ifdef HOST_SIM
  {int16_t bx,by;uint16_t bw,bh;getTextBounds(s,ax,gf?ay+top:ay,&bx,&by,&bw,&bh);if(!gf&&bh>0)--bh;texts.push_back({bx,by,int(bw),int(bh),s});if(bx<0||by<0||bx+int(bw)>width()||by+int(bh)>height())++textOob;}
#endif
  if(bgc!=fg)fillRect(ax,ay,w,h,bgc);
  setTextColor_(fg);setCursor(ax,gf?ay+top:ay);print_(s);}
private:
 void setTextColor_(uint16_t c){Adafruit_GFX::setTextColor(c?1:0);}
 void print_(const char* s){for(;*s;++s)write((uint8_t)*s);}
};
