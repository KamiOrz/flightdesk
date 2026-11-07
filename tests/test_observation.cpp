#include "observation.h"
#include "feed_freshness.h"
#include <cassert>
#include <iostream>
int main(){
 JsonDocument j;
 assert(!deserializeJson(j,R"({"hex":"7813a1","lat":31.236,"lon":121.466,"seen_pos":1,"alt_baro":10000,"gs":200,"track":90})"));
 Observation p;
 assert(parseObservation(j.as<JsonObjectConst>(),31.230,121.470,10,0,p));
 assert(p.distance<1 && std::abs(p.altitude-3048)<.01 && std::abs(p.speed-370.4)<.01);
 assert(!parseObservation(j.as<JsonObjectConst>(),31.230,121.470,.1,0,p));
 assert(!parseObservation(j.as<JsonObjectConst>(),31.230,121.470,10,30,p));
 j["alt_baro"]="ground";
 assert(!parseObservation(j.as<JsonObjectConst>(),31.230,121.470,10,0,p));
 j["alt_baro"]=nullptr;j["gs"]=nullptr;
 assert(parseObservation(j.as<JsonObjectConst>(),31.230,121.470,10,0,p));
 assert(std::isnan(p.altitude)&&std::isnan(p.speed));
 j["lat"]=nullptr;
 assert(!parseObservation(j.as<JsonObjectConst>(),31.230,121.470,100,0,p));
 j["lat"]="31.236";
 assert(!parseObservation(j.as<JsonObjectConst>(),31.230,121.470,100,0,p));
 j["lat"]=91;
 assert(!parseObservation(j.as<JsonObjectConst>(),31.230,121.470,100,0,p));
 j["lat"]=31.236;j["seen_pos"]=nullptr;
 assert(!parseObservation(j.as<JsonObjectConst>(),31.230,121.470,100,0,p));
 // Numeric integers must parse, while a missing aircraft identity must not.
 j["lat"]=0;j["lon"]=0;j["seen_pos"]=0;
 assert(parseObservation(j.as<JsonObjectConst>(),0,0,10,0,p));
 j["hex"]=nullptr;
 assert(!parseObservation(j.as<JsonObjectConst>(),0,0,10,0,p));
 j["r"]="B1680";
 assert(parseObservation(j.as<JsonObjectConst>(),0,0,10,0,p));
 j["onground"]="-1";j["alt_baro"]=100;
 assert(!parseObservation(j.as<JsonObjectConst>(),0,0,10,0,p));
 j["onground"]="0";j["r"]=nullptr;j["flight"]="MU6685";
 assert(parseObservation(j.as<JsonObjectConst>(),0,0,10,0,p));
 JsonDocument feed;feed["now"]=100000.5;feed["upstream_age"]=2;
 double responseAge=0,frameAge=0;
 assert(localFeedFreshness(feed.as<JsonObjectConst>(),100010,responseAge,frameAge));
 assert(std::abs(responseAge-9.5)<.01 && frameAge==2);
 // Current HTTP time must not conceal a frozen upstream subscription.
 feed["upstream_age"]=31;
 assert(!localFeedFreshness(feed.as<JsonObjectConst>(),100010,responseAge,frameAge));
 feed["upstream_age"]=nullptr;
 assert(!localFeedFreshness(feed.as<JsonObjectConst>(),100010,responseAge,frameAge));
 feed["upstream_age"]=0;feed["now"]=1793869918097.0;
 assert(localFeedFreshness(feed.as<JsonObjectConst>(),1793869919,responseAge,frameAge));
 assert(!localFeedFreshness(feed.as<JsonObjectConst>(),1793870019,responseAge,frameAge));
 std::cout<<"Observation parsing, units, freshness, missing fields and ground traffic: passed\n";
}
