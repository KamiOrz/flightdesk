#pragma once
#include <cmath>
namespace geo {
constexpr double kPi = 3.14159265358979323846;
inline double distanceKm(double lat1, double lon1, double lat2, double lon2) {
  double a = lat1 * kPi / 180, b = lat2 * kPi / 180;
  double dl = (lat2-lat1)*kPi/180, dn = (lon2-lon1)*kPi/180;
  double h = std::sin(dl/2)*std::sin(dl/2)+std::cos(a)*std::cos(b)*std::sin(dn/2)*std::sin(dn/2);
  return 6371.0088 * 2 * std::asin(std::sqrt(fmin(1.0, fmax(0.0, h))));
}
inline double bearing(double lat1,double lon1,double lat2,double lon2) {
  double a=lat1*kPi/180,b=lat2*kPi/180,d=(lon2-lon1)*kPi/180;
  return std::fmod(std::atan2(std::sin(d)*std::cos(b),std::cos(a)*std::sin(b)-std::sin(a)*std::cos(b)*std::cos(d))*180/kPi+360,360);
}
inline bool eligible(double distance,double radius,double seen,bool ground) {
  return !ground && std::isfinite(distance) && std::isfinite(seen) && seen>=0 && seen<=30 && distance<=radius;
}
}
