#pragma once
#include <stdint.h>
// Ocena odbioru wzgledem granicy demodulacji dla danego SF (SNR graniczny: SF7 -7,5 dB, kazdy kolejny SF o 2,5 dB mniej).
namespace qual {
enum Q{Q_NONE,Q_GOOD,Q_FAIR,Q_BAD};
inline float snrLimit(unsigned sf){return -7.5f-2.5f*float(int(sf)-7);}
inline float margin(float snr,unsigned sf){return snr-snrLimit(sf);}
inline Q quality(bool has,float snr,unsigned sf){if(!has)return Q_NONE;float m=margin(snr,sf);return m>=10?Q_GOOD:m>=3?Q_FAIR:Q_BAD;}
inline const char* qText(Q q){return q==Q_GOOD?"GOOD":q==Q_FAIR?"MARGINAL":q==Q_BAD?"WEAK":"NO DATA";}
}
