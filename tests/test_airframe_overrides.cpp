#include "airframe_overrides.h"
#include <cassert>
#include <iostream>
int main(){
 auto r=findAirframeOverride("7A3854","B4080","");
 assert(r&&std::strcmp(r->type,"B738")==0&&std::strcmp(r->operatorName,"China Air Force")==0);
 assert(findAirframeOverride("7a3854","B-4080","B738"));
 assert(findAirframeOverride("78059D","B-4080","B738"));
 // A conflicting provider model must not be overwritten.
 assert(!findAirframeOverride("7a3854","B4080","C919"));
 // A callsign alone must not match: require both aircraft identity fields.
 assert(!findAirframeOverride("7a3854","OM4080",""));
 assert(!findAirframeOverride("781234","B4080",""));
 assert(!findAirframeOverride("7a3854","B4081",""));
 assert(!findAirframeOverride("7a3854",nullptr,""));
 assert(!findAirframeOverride("","B4080",""));
 std::cout<<"Verified airframe identity, legacy hex, missing model and conflicting inputs: passed\n";
}
