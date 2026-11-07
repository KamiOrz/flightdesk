#pragma once
#include <ArduinoJson.h>
#include <cstring>
#include "geo.h"

struct Observation {
  double lat=0, lon=0, distance=0, bearing=0;
  double altitude=NAN, speed=NAN, track=NAN;
};

// Treat missing values as unknown, never as a plane at coordinate (0, 0).
// ADS-B point-query radii are nautical miles; the final filter is in kilometres.
inline bool parseObservation(JsonObjectConst a, double homeLat, double homeLon,
                             double radiusKm, double responseAge,
                             Observation &out) {
  if (!a["lat"].is<double>() || !a["lon"].is<double>() ||
      !a["seen_pos"].is<double>()) return false;
  // The LAN feed can omit an ICAO address; a registration or flight still
  // identifies a real observation. Do not invent a hex address for it.
  bool identified=false;
  for (const char *key : {"hex", "r", "flight"})
    if (a[key].is<const char*>() && std::strlen(a[key].as<const char*>())) identified=true;
  if (!identified) return false;
  Observation p;
  p.lat=a["lat"].as<double>();
  p.lon=a["lon"].as<double>();
  if (!std::isfinite(p.lat) || !std::isfinite(p.lon) ||
      std::fabs(p.lat)>90 || std::fabs(p.lon)>180) return false;
  p.distance=geo::distanceKm(homeLat,homeLon,p.lat,p.lon);
  const char *altText=a["alt_baro"].is<const char*>() ? a["alt_baro"].as<const char*>() : "";
  const char *groundText=a["onground"].is<const char*>() ? a["onground"].as<const char*>() : "";
  bool ground=std::strcmp(altText,"ground")==0 || std::strcmp(groundText,"-1")==0;
  double seen=a["seen_pos"].as<double>()+std::fmax(0.,responseAge);
  if (!geo::eligible(p.distance,radiusKm,seen,ground)) return false;
  p.bearing=geo::bearing(homeLat,homeLon,p.lat,p.lon);
  if (a["alt_baro"].is<double>()) p.altitude=a["alt_baro"].as<double>()*.3048;
  if (a["gs"].is<double>()) p.speed=a["gs"].as<double>()*1.852;
  if (a["track"].is<double>()) p.track=a["track"].as<double>();
  out=p;
  return true;
}
