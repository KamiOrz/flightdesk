#include "geo.h"
#include <cassert>
#include <limits>
#include <iostream>
int main(){
 assert(geo::distanceKm(31.230,121.470,31.230,121.470)<1e-8);
 assert(std::abs(geo::distanceKm(0,0,0,1)-111.195)<.01);
 assert(geo::distanceKm(0,179.99,0,-179.99)<2.3);
 assert(geo::distanceKm(89.99,0,89.99,180)<2.3);
 assert(std::abs(geo::bearing(0,0,1,0))<1e-8);
 assert(std::abs(geo::bearing(0,0,0,1)-90)<1e-8);
 assert(geo::eligible(10,10,30,false));
 assert(geo::eligible(7.5,7.5,0,false));
 assert(!geo::eligible(7.5001,7.5,0,false));
 assert(!geo::eligible(10.01,10,0,false));
 assert(!geo::eligible(1,10,30.01,false));
 assert(!geo::eligible(1,10,0,true));
 assert(!geo::eligible(std::numeric_limits<double>::quiet_NaN(),10,0,false));
 assert(!geo::eligible(1,10,std::numeric_limits<double>::quiet_NaN(),false));
 std::cout<<"Distance, bearing, radius boundary, freshness, ground filtering: passed\n";
}
