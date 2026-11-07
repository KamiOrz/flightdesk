#pragma once
#include <cctype>
#include <cstring>

struct AirframeOverride {
 const char *hex, *registration, *type, *operatorName, *artKey;
};
// Static identity supplement, never a position or a fabricated flight.
// FR24 B-4080: B737-85N / B738, mode S 7A3854, China - Air Force.
// https://www.flightradar24.com/data/aircraft/B-4080
// The older mode S is retained only with the same verified registration:
// https://api.adsbdb.com/v0/aircraft/B-4080
static constexpr AirframeOverride AIRFRAME_OVERRIDES[]={
 {"7a3854","B-4080","B738","China Air Force","CN_AF"},
 {"78059d","B-4080","B738","China Air Force","CN_AF"},
};
inline bool equalAirframeText(const char *a,const char *b,bool registration=false){
 if(!a||!b)return false;
 for(;;){
  if(registration){while(*a=='-'||*a==' ')++a;while(*b=='-'||*b==' ')++b;}
  if(std::toupper((unsigned char)*a)!=std::toupper((unsigned char)*b))return false;
  if(!*a)return true;
  ++a;++b;
 }
}
inline const AirframeOverride *findAirframeOverride(const char *hex,const char *registration,const char *sourceType){
 for(const auto &r:AIRFRAME_OVERRIDES)
  if(equalAirframeText(hex,r.hex)&&equalAirframeText(registration,r.registration,true)&&
     (!sourceType||!*sourceType||equalAirframeText(sourceType,r.type)))return &r;
 return nullptr;
}
