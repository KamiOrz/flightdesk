#pragma once
#include <ArduinoJson.h>
#include <cmath>

// seen_pos is measured at response time, so upstream frame age must not be
// added to it again. Independently reject an abandoned upstream subscription.
inline bool localFeedFreshness(JsonObjectConst j, double localNow,
                               double &responseAge, double &frameAge) {
  if (!j["now"].is<double>() || !j["upstream_age"].is<double>()) return false;
  double stamp=j["now"].as<double>();
  if (stamp>1e12) stamp/=1000.;
  responseAge=localNow-stamp;
  frameAge=j["upstream_age"].as<double>();
  return std::isfinite(responseAge) && std::isfinite(frameAge) &&
    responseAge<=60 && responseAge>=-120 && frameAge>=0 && frameAge<=30;
}
