#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <vector>
#include <algorithm>
#include <time.h>
#include "geo.h"
#include "observation.h"
#include "feed_freshness.h"
#include "trust_roots.h"
#include "web_page.h"
#include "artwork.h"
#include "clock_font.h"
#include "display_wake.h"
#include "airframe_overrides.h"
#include "project_config.h"

// Standard T-Display-S3: ST7789 170x320, GPIO0 / GPIO14 buttons.
struct Settings {
  String ssid, password, name="FlightDesk";
  // Approximate neighbourhood centre, WGS84; editable in the local WebUI.
  double lat=0, lon=0;
  double radius=20;
  int interval=10, rotation=1;
  uint32_t revision=0;
};
struct Plane : Observation {
  String hex, id, callsign, registration, type;
  String liveryAirline, airframeSource;
  String flight, airline, origin, destination, originName, destinationName, routeSource;
};
struct Snapshot {
  std::vector<Plane> planes;
  String status="Waiting for Wi-Fi", source="VariFlight LAN";
  uint32_t updated=0, revision=0;
  bool valid=false, error=false;
};
struct Route { String callsign,flight,airline,origin,destination,originName,destinationName,source; uint32_t checked=0; bool ok=false; };
Settings settings;
Snapshot traffic;
std::vector<Route> routes;
SemaphoreHandle_t dataMutex;
Preferences prefs;
WebServer server(80);
DNSServer dns;
TFT_eSPI tft;
TFT_eSprite canvas(&tft);
bool apActive=false,mdnsStarted=false,reconnectRequested=false,redraw=true,radar=false;
bool backlightOn=false;
DisplayWake displayWake;
uint32_t apStarted=0, pageAt=0, lastRender=0, reconnectAt=0;
String selectedId;
int selected=0;
uint16_t BG=0x0863,PANEL=0x10E5,WHITE=0xEF9E,MUTED=0x9494,ACCENT=0x977A,AMBER=0xFE69;
uint32_t frameInterval=1000;
String serialLine;
volatile bool serialImage=false;

Settings getSettings(){xSemaphoreTake(dataMutex,portMAX_DELAY);Settings s=settings;xSemaphoreGive(dataMutex);return s;}
Snapshot getTraffic(){xSemaphoreTake(dataMutex,portMAX_DELAY);Snapshot s=traffic;xSemaphoreGive(dataMutex);return s;}
void saveSettings(){Settings s=getSettings();prefs.putString("ssid",s.ssid);prefs.putString("pass",s.password);prefs.putString("name",s.name);prefs.putDouble("lat",s.lat);prefs.putDouble("lon",s.lon);prefs.putDouble("radius_km",s.radius);prefs.putInt("interval",s.interval);prefs.putInt("rotation",s.rotation);}
void loadSettings(){prefs.begin("flightdesk",false);settings.ssid=prefs.getString("ssid","");settings.password=prefs.getString("pass","");settings.name=prefs.getString("name",settings.name);settings.lat=prefs.getDouble("lat",settings.lat);settings.lon=prefs.getDouble("lon",settings.lon);settings.radius=prefs.isKey("radius_km")?prefs.getDouble("radius_km",7.5):(prefs.isKey("radius")?(double)prefs.getInt("radius",30):7.5);if(!isfinite(settings.radius)||settings.radius<1||settings.radius>100)settings.radius=7.5;settings.interval=constrain(prefs.getInt("interval",10),5,300);settings.rotation=prefs.getInt("rotation",1)==3?3:1;}
String ascii(String s){String out;for(size_t i=0;i<s.length();i++)if((uint8_t)s[i]>=32&&(uint8_t)s[i]<127)out+=s[i];return out;}
String shortText(String s,int max=25){s=ascii(s);return s.length()>(unsigned)max?s.substring(0,max-1)+"~":s;}
String numeric(double v,int digits=0){return isfinite(v)?String(v,digits):"--";}
const char* direction(double b){const char* dirs[]={"N","NE","E","SE","S","SW","W","NW"};return dirs[((int)round(b/45))%8];}

void startAP(){if(apActive)return;WiFi.mode(WIFI_AP_STA);if(WiFi.softAP("FlightDesk-Setup","flightdesk")){dns.start(53,"*",WiFi.softAPIP());apActive=true;apStarted=millis();Serial.println("SETUP_AP 192.168.4.1");redraw=true;}}
void connectWifi(){Settings s=getSettings();if(s.ssid.length()){WiFi.begin(s.ssid.c_str(),s.password.c_str());reconnectAt=millis();}else startAP();}

bool applyConfig(JsonDocument &j,String &err){
 Settings s=getSettings();
 if(j["lat"].isNull()||!j["lat"].is<double>()||j["lon"].isNull()||!j["lon"].is<double>()){err="经纬度必须是数字";return false;}
 s.lat=j["lat"].as<double>();s.lon=j["lon"].as<double>();
 if(!isfinite(s.lat)||!isfinite(s.lon)||s.lat < -90||s.lat>90||s.lon < -180||s.lon>180){err="经纬度超出范围";return false;}
 if(!j["radius"].is<double>()||!j["interval"].is<int>()){err="半径必须是数字，间隔必须是整数";return false;}
 s.radius=j["radius"].as<double>();s.interval=j["interval"].as<int>();
 if(!isfinite(s.radius)||s.radius<1||s.radius>100||s.interval<5||s.interval>300){err="半径 1–100km，间隔 5–300 秒";return false;}
 if(j["name"].is<String>())s.name=j["name"].as<String>().substring(0,40);
 s.rotation=(j["rotation"]|1)==3?3:1;
 xSemaphoreTake(dataMutex,portMAX_DELAY);s.revision=settings.revision+1;settings=s;traffic=Snapshot();traffic.revision=s.revision;traffic.status="Settings changed; refreshing";xSemaphoreGive(dataMutex);
 saveSettings();selectedId="";redraw=true;return true;
}
bool applyWifi(JsonDocument &j,String &err){
 if(!j["ssid"].is<String>()||!j["password"].is<String>()){err="需要 SSID 和密码字段";return false;}
 String ssid=j["ssid"].as<String>(),pass=j["password"].as<String>();
 if(ssid.length()<1||ssid.length()>32||pass.length()>63||(pass.length()>0&&pass.length()<8)){err="SSID 1–32 字节，密码为空或 8–63 字节";return false;}
 xSemaphoreTake(dataMutex,portMAX_DELAY);settings.ssid=ssid;settings.password=pass;xSemaphoreGive(dataMutex);saveSettings();reconnectRequested=true;return true;
}
void jsonResponse(JsonDocument &j){server.client().setNoDelay(true);String body;serializeJson(j,body);server.sendHeader("Cache-Control","no-store");server.send(200,"application/json",body);}
void sendConfig(){Settings s=getSettings();JsonDocument j;j["name"]=s.name;j["lat"]=s.lat;j["lon"]=s.lon;j["radius"]=s.radius;j["interval"]=s.interval;j["rotation"]=s.rotation;j["ssid"]=s.ssid;jsonResponse(j);}
void sendState(){
 Snapshot s=getTraffic();JsonDocument j;j["wifi"]=WiFi.status()==WL_CONNECTED;j["ip"]=WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():WiFi.softAPIP().toString();j["status"]=s.status;j["source"]=s.source;j["version"]="0.5.5";j["heap"]=ESP.getFreeHeap();j["screen_on"]=backlightOn;j["backlight_gpio"]=digitalRead(38);
 uint32_t age=s.valid?(millis()-s.updated)/1000:0;if(s.valid)j["age"]=age;else j["age"]=nullptr;
 bool fresh=s.valid&&age<=60;j["stale"]=s.valid&&!fresh;j["count"]=fresh?s.planes.size():0;
 JsonArray a=j["planes"].to<JsonArray>();if(fresh)for(const Plane &p:s.planes){JsonObject o=a.add<JsonObject>();o["hex"]=p.hex;o["id"]=p.id;o["flight"]=p.flight.length()?p.flight:p.callsign;o["type"]=p.type;o["registration"]=p.registration;o["aircraft_source"]=p.airframeSource;o["distance"]=p.distance;if(isfinite(p.altitude))o["altitude"]=p.altitude;else o["altitude"]=nullptr;o["airline"]=p.airline;o["origin"]=p.origin;o["destination"]=p.destination;o["origin_name"]=p.originName;o["destination_name"]=p.destinationName;o["route_source"]=p.routeSource;o["lat"]=p.lat;o["lon"]=p.lon;}
 jsonResponse(j);
}
void screenBmp(){
 // Sprite stores byte-swapped RGB565. Convert a row at a time to standard BGR24 BMP.
 uint32_t rowBytes=(320*3+3)&~3,size=54+rowBytes*170;uint8_t header[54]={0x42,0x4D};
 auto put=[&](int p,uint32_t v){for(int b=0;b<4;b++)header[p+b]=(v>>(b*8))&255;};
 put(2,size);put(10,54);put(14,40);put(18,320);put(22,170);header[26]=1;header[28]=24;put(34,rowBytes*170);
 server.setContentLength(size);server.send(200,"image/bmp","");WiFiClient c=server.client();c.setNoDelay(true);
 auto writeAll=[&](const uint8_t*data,size_t len){size_t done=0;uint32_t started=millis();while(done<len&&c.connected()&&millis()-started<10000){size_t n=c.write(data+done,len-done);if(!n)delay(1);done+=n;}return done==len;};
 if(!writeAll(header,54))return;
 // Batch rows to avoid a TCP packet and delayed ACK for each individual row.
 uint8_t *block=(uint8_t*)malloc(960*8);if(!block)return;
 size_t used=0;
 for(int y=169;y>=0;y--){for(int x=0;x<320;x++){uint16_t col=canvas.readPixel(x,y);block[used++]=(col&31)*255/31;block[used++]=((col>>5)&63)*255/63;block[used++]=((col>>11)&31)*255/31;}
  if(used==960*8||y==0){if(!writeAll(block,used))break;used=0;}}
 free(block);
}
void setupWeb(){
 server.on("/",HTTP_GET,[]{server.send_P(200,"text/html; charset=utf-8",WEB_PAGE);});server.on("/api/config",HTTP_GET,sendConfig);server.on("/api/state",HTTP_GET,sendState);server.on("/screen.bmp",HTTP_GET,screenBmp);
 for(String path:{String("/api/config"),String("/api/wifi")})server.on(path.c_str(),HTTP_POST,[path]{
  if(server.hasHeader("Origin")){String origin=server.header("Origin");String expected="http://"+server.hostHeader();if(origin!=expected){server.send(403,"text/plain","Origin rejected");return;}}
  if(server.arg("plain").length()>1024){server.send(413,"text/plain","Request too large");return;}
  JsonDocument j;if(deserializeJson(j,server.arg("plain"))){server.send(400,"text/plain; charset=utf-8","JSON 无效");return;}String err;bool ok=path=="/api/wifi"?applyWifi(j,err):applyConfig(j,err);server.send(ok?200:400,"text/plain; charset=utf-8",ok?"已保存":err);
 });
 const char *headers[]={"Origin"};server.collectHeaders(headers,1);
 server.onNotFound([]{server.sendHeader("Location","/",true);server.send(302,"text/plain","");});server.begin();
}

// Position and route workers have independent clients; cache access is locked.
int getJson(const String &url,JsonDocument &doc,JsonDocument *filter=nullptr){
 WiFiClient plain;WiFiClientSecure tls;tls.setCACert(TRUST_ROOTS);tls.setHandshakeTimeout(8);
 bool local=String(FLIGHT_API_BASE_URL).startsWith("http://") && url.startsWith(String(FLIGHT_API_BASE_URL)+"/");
 HTTPClient http;http.setConnectTimeout(5000);http.setTimeout(local?45000:7000);http.useHTTP10(false);http.setReuse(false);
 if(!(local?http.begin(plain,url):http.begin(tls,url)))return -100;http.addHeader("User-Agent","FlightDesk-TDisplayS3/0.4 (open source)");int code=http.GET();
 if(code==200){
  // HTTPClient decodes HTTP/1.1 chunked responses before JSON parsing.
  if(http.getSize()>131072){http.end();return -102;}
  String body=http.getString();
  if(body.length()>131072){http.end();return -102;}
  DeserializationError e=filter?deserializeJson(doc,body,DeserializationOption::Filter(*filter)):deserializeJson(doc,body);
  if(e){if(!serialImage)Serial.printf("JSON_ERROR %s\n",e.c_str());code=-101;}
 }
 http.end();return code;
}
void setError(const Settings &cfg,const String &message){xSemaphoreTake(dataMutex,portMAX_DELAY);if(settings.revision==cfg.revision){traffic.error=true;traffic.status=message;}xSemaphoreGive(dataMutex);if(!serialImage)Serial.println("DATA "+message);}
void addRouteUnlocked(Plane &p){
 for(const Route&r:routes)if(r.callsign==p.callsign&&r.ok){
  if(p.flight.isEmpty())p.flight=r.flight;
  if(p.airline.isEmpty())p.airline=r.airline;
  // Live upstream airports take precedence over a callsign route database.
  if(p.origin.isEmpty()||p.destination.isEmpty()){
   p.origin=r.origin;p.destination=r.destination;p.originName=r.originName;p.destinationName=r.destinationName;p.routeSource=r.source;
  }
  break;
 }
}
void addRoute(Plane &p){xSemaphoreTake(dataMutex,portMAX_DELAY);addRouteUnlocked(p);xSemaphoreGive(dataMutex);}
bool routeCached(const String&callsign){bool cached=false;xSemaphoreTake(dataMutex,portMAX_DELAY);for(const Route&r:routes)if(r.callsign==callsign&&millis()-r.checked<(r.ok?21600000UL:300000UL))cached=true;xSemaphoreGive(dataMutex);return cached;}
void lookupRoute(const String &callsign){
 if(callsign.isEmpty())return;
 if(routeCached(callsign))return;
 // Callsign is provider input: accept only simple flight identifiers in URL path.
 for(size_t i=0;i<callsign.length();i++)if(!isalnum((unsigned char)callsign[i]))return;
 Route r;r.callsign=callsign;r.checked=millis();JsonDocument j;int code=getJson("https://api.adsbdb.com/v0/callsign/"+callsign,j);
 if(code==200){JsonObject f=j["response"]["flightroute"];if(!f.isNull()){r.ok=true;r.flight=f["callsign_iata"]|"";r.airline=f["airline"]["name"]|"";r.origin=f["origin"]["iata_code"]|"";r.destination=f["destination"]["iata_code"]|"";r.originName=f["origin"]["municipality"]|"";r.destinationName=f["destination"]["municipality"]|"";r.source="ADSBdb";r.ok=r.origin.length()&&r.destination.length();}}
 if(!r.ok){
  JsonDocument alt;int status=getJson("https://api.adsb.lol/api/0/route/"+callsign,alt);
  if(status==200&&alt["_airports"].is<JsonArray>()&&alt["_airports"].size()>=2){
   JsonArray aps=alt["_airports"].as<JsonArray>();JsonObject first=aps[0],last=aps[aps.size()-1];
   r.origin=first["iata"]|"";r.destination=last["iata"]|"";
   if(r.origin.isEmpty())r.origin=first["icao"]|"";if(r.destination.isEmpty())r.destination=last["icao"]|"";
   r.originName=first["location"]|"";r.destinationName=last["location"]|"";
   r.source="ADSB.lol";r.ok=r.origin.length()&&r.destination.length();
  }
 }
 xSemaphoreTake(dataMutex,portMAX_DELAY);
 routes.erase(std::remove_if(routes.begin(),routes.end(),[&](const Route &v){return v.callsign==callsign;}),routes.end());if(routes.size()>=64)routes.erase(routes.begin());routes.push_back(r);
 for(Plane&p:traffic.planes)addRouteUnlocked(p);
 xSemaphoreGive(dataMutex);
 if(r.ok&&!serialImage)Serial.println("ROUTE "+callsign+" "+r.origin+">"+r.destination+" "+r.source);
}
void metadataTask(void*){
 for(;;){
  Snapshot s=getTraffic();
  if(WiFi.status()==WL_CONNECTED&&s.valid&&millis()-s.updated<60000&&!s.planes.empty()){
   int first=constrain(selected,0,(int)s.planes.size()-1);
   for(size_t n=0;n<s.planes.size();n++){
    const Plane&p=s.planes[(first+n)%s.planes.size()];
    if((p.origin.isEmpty()||p.destination.isEmpty())&&p.callsign.length()&&!routeCached(p.callsign)){lookupRoute(p.callsign);break;}
   }
  }
  vTaskDelay(pdMS_TO_TICKS(1000));
 }
}

String airlineCode(const Plane&p);

void networkTask(void*){
 uint32_t due=0;int failures=0;uint32_t lastRevision=UINT32_MAX;
 for(;;){
  Settings s=getSettings();if(s.revision!=lastRevision){due=0;lastRevision=s.revision;}
  if(WiFi.status()!=WL_CONNECTED){vTaskDelay(pdMS_TO_TICKS(250));continue;}
  if(time(nullptr)<1700000000){setError(s,"Syncing network time");vTaskDelay(pdMS_TO_TICKS(3000));continue;}
  if(due&&int32_t(millis()-due)<0){vTaskDelay(pdMS_TO_TICKS(100));continue;}
  String url=String(FLIGHT_API_BASE_URL)+"/api/nearby?lat="+String(s.lat,6)+"&lon="+String(s.lon,6)+"&radius_km="+String(s.radius)+"&airborne=1";
  JsonDocument filter;filter["now"]=true;filter["upstream_age"]=true;filter["ac"][0]["hex"]=true;for(const char*k:{"flight","r","t","lat","lon","seen_pos","alt_baro","gs","track","origin","destination","onground"})filter["ac"][0][k]=true;
  JsonDocument j;int code=getJson(url,j,&filter);
  due=millis()+s.interval*1000UL;
  if(code!=200){failures=std::min(failures+1,4);due=millis()+std::max((uint32_t)s.interval*1000UL,15000UL*(1UL<<failures));setError(s,"Flight API error "+String(code)+"; retrying");continue;}
  if(!j["ac"].is<JsonArray>()){setError(s,"Local flight feed invalid");continue;}
  double age=0,frameAge=0;
  if(!localFeedFreshness(j.as<JsonObjectConst>(),time(nullptr),age,frameAge)){setError(s,"Local flight feed stale or invalid");continue;}
  failures=0;Snapshot out;out.valid=true;out.updated=millis()-(uint32_t)std::max(std::max(0.0,age),frameAge)*1000UL;out.revision=s.revision;
  for(JsonObject a:j["ac"].as<JsonArray>()){
   Plane p;
   if(!parseObservation(a,s.lat,s.lon,s.radius,age,p))continue;
   p.hex=a["hex"]|"";p.callsign=a["flight"]|"";p.callsign.trim();p.registration=a["r"]|"";p.type=a["t"]|"";
   p.registration.trim();p.type.trim();p.type.toUpperCase();
   if(const auto *known=findAirframeOverride(p.hex.c_str(),p.registration.c_str(),p.type.c_str())){
    if(p.type.isEmpty())p.type=known->type;
    p.airline=known->operatorName;p.liveryAirline=known->artKey;p.airframeSource="Verified registration";
   }
   p.id=p.hex.length()?p.hex:(p.registration.length()?"reg:"+p.registration:"flight:"+p.callsign);
   p.origin=a["origin"]|"";p.destination=a["destination"]|"";
   if(p.origin.length()||p.destination.length())p.routeSource="VariFlight";
   String operatorCode=airlineCode(p);for(const auto&a:AIRLINE_LABELS)if(operatorCode==a.code){p.airline=a.name;break;}
   addRoute(p);out.planes.push_back(p);
  }
  std::sort(out.planes.begin(),out.planes.end(),[](const Plane&a,const Plane&b){return a.distance<b.distance;});if(out.planes.size()>40)out.planes.resize(40);
  out.status=out.planes.empty()?"No aircraft in selected radius":"Live aircraft nearby";
  xSemaphoreTake(dataMutex,portMAX_DELAY);if(settings.revision==s.revision)traffic=out;xSemaphoreGive(dataMutex);
  if(!serialImage)Serial.printf("FLIGHTS count=%u radius=%.1fkm\n",out.planes.size(),s.radius);

 }
}

void text(String s,int x,int y,int font=2,uint16_t color=0){canvas.setTextColor(color?color:WHITE);canvas.drawString(s,x,y,font);}
void planeIcon(int x,int y,int size,uint16_t color){canvas.fillTriangle(x,y-size,x-3,y+size,x+3,y+size,color);canvas.fillTriangle(x,y-2,x-size,y+6,x+size,y+6,color);canvas.drawFastHLine(x-size/2,y+size-2,size,color);}
void drawRadar(const Snapshot&s,const Settings&cfg){
 int cx=82,cy=96,r=58;canvas.drawCircle(cx,cy,r,MUTED);canvas.drawCircle(cx,cy,r/2,PANEL);canvas.drawFastHLine(cx-r,cy,r*2,PANEL);canvas.drawFastVLine(cx,cy-r,r*2,PANEL);text("N",cx-4,20,1,MUTED);canvas.fillCircle(cx,cy,3,ACCENT);
 for(size_t i=0;i<s.planes.size();i++){const Plane&p=s.planes[i];float a=p.bearing*geo::kPi/180;int x=cx+sin(a)*r*p.distance/cfg.radius,y=cy-cos(a)*r*p.distance/cfg.radius;canvas.fillCircle(x,y,i==(size_t)selected?4:2,i==(size_t)selected?AMBER:ACCENT);}
 if(!s.planes.empty()){const Plane&p=s.planes[selected];text(shortText(p.callsign,13),157,42,4);text(shortText(p.type+" / "+p.registration,18),157,76,2,MUTED);text(String(p.distance,1)+" km "+direction(p.bearing),157,103,2,ACCENT);text(numeric(p.altitude)+" m",157,127,2);}
}
uint16_t themeColor(int y){float t=y/169.0f;return tft.color565(lroundf(2-t),lroundf(29+59*t),lroundf(39+52*t));}
String airlineCode(const Plane&p){
 if(p.liveryAirline.length())return p.liveryAirline;
 String flight=p.callsign;flight.toUpperCase();
 if(flight.length()>2&&isdigit((unsigned char)flight[2]))
  for(const auto&a:AIRLINE_LABELS)if(String(a.iata).length()==2&&flight.startsWith(a.iata))return a.code;
 String code=flight.substring(0,3);
 for(size_t i=0;i<code.length();i++)if(!isalpha((unsigned char)code[i]))return "";
 return code.length()==3?code:String("");
}
String airlineLabel(const Plane&p){if(p.airline.length())return p.airline;String code=airlineCode(p);for(const auto&a:AIRLINE_LABELS)if(code==a.code)return a.name;return code.length()?code:String("Operator unknown");}
String modelLabel(const Plane&p){for(const auto&t:TYPE_LABELS)if(p.type==t.code)return t.name;return p.type.length()?p.type:"Aircraft";}
bool drawLivery(const Plane&p){
 const uint16_t*data=nullptr;String code=airlineCode(p);
 for(const auto&a:LIVERIES)if(p.type==a.type&&code==a.airline){data=a.data;break;}
 if(!data)for(const auto&a:LIVERIES)if(p.type==a.type&&String(a.airline)=="*"){data=a.data;break;}
 if(!data)return false;
 uint16_t row[ART_W];int x=0,y=0;size_t pos=0;
 while(y<ART_H){uint16_t token=pgm_read_word(data+pos++);int count=token&0x7fff;uint16_t color=0;if(token&0x8000)color=pgm_read_word(data+pos++);
  while(count--&&y<ART_H){row[x++]=(token&0x8000)?color:pgm_read_word(data+pos++);if(x==ART_W){for(int i=0;i<ART_W;i++)canvas.drawPixel(36+i,48+y,row[i]);x=0;y++;}}
 }
 return true;
}
void drawPremiumCard(const Plane&p,const Snapshot&s,const Settings&cfg){
 text("NEARBY AIRCRAFT",11,5,1,0x8DF4);canvas.setTextColor(0x8DF4);canvas.drawRightString(String(cfg.radius,1)+" KM  /  "+String(selected+1)+" OF "+String(s.planes.size()),308,5,1);
 String title=p.flight.length()?p.flight:(p.callsign.length()?p.callsign:p.hex);
 text(shortText(title,12),10,20,4,WHITE);
 canvas.setTextColor(0xFF12);canvas.drawRightString(shortText(airlineLabel(p),21),308,23,2);
 // Font 1 is 8px high: keep its last row above the artwork starting at y=48.
 canvas.setTextColor(0xC73B);canvas.drawRightString(shortText(modelLabel(p),25),308,40,1);
 if(!drawLivery(p)){planeIcon(160,79,25,WHITE);text("No matching livery",104,103,1,MUTED);}
 text("DISTANCE",16,114,1,0x9EB7);text("ALTITUDE",132,114,1,0x9EB7);text("GROUND SPEED",231,114,1,0x9EB7);
 text(String(p.distance,1)+" km",16,124,2,WHITE);text(numeric(p.altitude)+" m",132,124,2,WHITE);text(numeric(p.speed)+" km/h",231,124,2,WHITE);
 if(p.origin.length()&&p.destination.length()){
  text(p.origin,39,144,4,WHITE);canvas.setTextColor(WHITE);canvas.drawRightString(p.destination,280,144,4);
  canvas.drawFastHLine(143,156,31,0x8DF4);canvas.drawLine(168,151,174,156,0x8DF4);canvas.drawLine(168,161,174,156,0x8DF4);
 }else{
  text("Finding departure & arrival...",74,150,2,0x8DF4);
 }
}
// Quiet standby composition: a large clock and a real-time scanning instrument.
uint16_t mix565(uint16_t bg,uint16_t fg,int alpha,int maxAlpha=255){
 int r=(((bg>>11)&31)*(maxAlpha-alpha)+((fg>>11)&31)*alpha)/maxAlpha;
 int g=(((bg>>5)&63)*(maxAlpha-alpha)+((fg>>5)&63)*alpha)/maxAlpha;
 int b=((bg&31)*(maxAlpha-alpha)+(fg&31)*alpha)/maxAlpha;
 return (r<<11)|(g<<5)|b;
}
void clockGlyph(int digit,int x,int y){
 if(digit<0||digit>10)return;
 const uint8_t*data=CLOCK_GLYPHS[digit];
 for(int row=0;row<CLOCK_GLYPH_H;row++){
  uint16_t bg=themeColor(y+row);
  for(int col=0;col<CLOCK_GLYPH_W;col++){
   int pos=row*CLOCK_GLYPH_W+col;uint8_t pair=pgm_read_byte(data+pos/2);int alpha=pos%2?pair&15:pair>>4;
   if(alpha)canvas.drawPixel(x+col,y+row,alpha==15?WHITE:mix565(bg,WHITE,alpha,15));
  }
 }
}
void standbyRadar(uint32_t now){
 constexpr int cx=259,cy=84,r=47;
 const uint16_t grid=0x2B6D,glow=0x8FF9;
 canvas.fillCircle(cx,cy,r,0x01C9);
 float angle=(now%6000)*(2*geo::kPi/6000.0f)-geo::kPi/2;
 // A fading 68-degree fan; it indicates scanning, not synthetic aircraft.
 for(int step=0;step<46;step++){
  float a=angle-(68-step*1.5f)*geo::kPi/180;
  float b=a+1.7f*geo::kPi/180;
  uint16_t c=mix565(0x01C9,0x46D5,8+step*2);
  canvas.fillTriangle(cx,cy,cx+cosf(a)*(r-2),cy+sinf(a)*(r-2),cx+cosf(b)*(r-2),cy+sinf(b)*(r-2),c);
 }
 canvas.drawCircle(cx,cy,r,grid);canvas.drawCircle(cx,cy,r*2/3,grid);canvas.drawCircle(cx,cy,r/3,grid);
 canvas.drawFastHLine(cx-r+2,cy,r*2-3,grid);canvas.drawFastVLine(cx,cy-r+2,r*2-3,grid);
 for(int i=0;i<36;i++){
  float a=i*geo::kPi/18;int inner=r+(i%3==0?3:4),outer=r+(i%3==0?6:5);
  canvas.drawLine(cx+cosf(a)*inner,cy+sinf(a)*inner,cx+cosf(a)*outer,cy+sinf(a)*outer,i%3==0?0x65B4:grid);
 }
 canvas.drawLine(cx,cy,cx+cosf(angle)*(r-2),cy+sinf(angle)*(r-2),glow);
 canvas.fillCircle(cx,cy,3,0x01C9);canvas.fillCircle(cx,cy,1,WHITE);
 text("N",cx-3,22,1,0xAEB7);
 canvas.setTextColor(0x8DF4);canvas.drawCentreString("LOCAL RADAR",cx,142,1);
}
void drawStandby(const Snapshot&s,const Settings&cfg,bool fresh){
 uint32_t nowMs=millis();
 text("FLIGHT DESK",12,13,1,0xAEF8);text("LOCAL TIME / CST",12,33,1,0x7DD3);
 struct tm local;time_t now=time(nullptr);bool valid=now>1700000000&&localtime_r(&now,&local);
 if(valid){
  clockGlyph(local.tm_hour/10,10,48);clockGlyph(local.tm_hour%10,51,48);
  clockGlyph(10,78,48);clockGlyph(local.tm_min/10,103,48);clockGlyph(local.tm_min%10,144,48);
  char date[24];strftime(date,sizeof(date),"%a, %d %b",&local);String label=date;label.toUpperCase();text(label,12,132,2,0xD75B);
  char seconds[4];snprintf(seconds,sizeof(seconds),"%02d",local.tm_sec);text(seconds,167,135,1,0x7DD3);
 }else{
  text("--:--",12,61,7,WHITE);text("Synchronizing clock",12,133,2,0x7DD3);
 }
 standbyRadar(nowMs);
 canvas.drawFastHLine(12,153,296,0x2B6D);
 bool error=s.error||(s.valid&&!fresh);String status;
 if(error)status="DATA DELAYED";else if(!fresh)status="CONNECTING";else status="SCANNING "+String(cfg.radius,1)+" KM";
 uint16_t dot=error?AMBER:0x8FF9;canvas.fillCircle(15,162,2,dot);text(status,23,159,1,error?AMBER:0xAEF8);
 canvas.setTextColor(0x7DD3);canvas.drawRightString(WiFi.localIP().toString(),308,159,1);
}
void setBacklight(bool on){
 if(backlightOn==on)return;
 digitalWrite(38,on?HIGH:LOW);backlightOn=on;
 if(!serialImage)Serial.println(on?"SCREEN ON":"SCREEN OFF");
}
void render(){
 Snapshot s=getTraffic();Settings cfg=getSettings();for(int y=0;y<170;y++)canvas.drawFastHLine(0,y,320,themeColor(y));
 bool wifi=WiFi.status()==WL_CONNECTED;uint32_t age=s.valid?(millis()-s.updated)/1000:0;bool fresh=s.valid&&age<=60;
 bool light=displayWake.shouldLight(millis(),wifi&&fresh&&!s.planes.empty(),apActive||cfg.ssid.isEmpty());
 if(!light)setBacklight(false);
 frameInterval=light&&wifi&&(!fresh||s.planes.empty())?66:1000;
 if(!wifi){planeIcon(275,80,18,ACCENT);text("Connect to Wi-Fi",12,39,4);text(apActive?"AP: FlightDesk-Setup":"Connecting to saved network...",12,78);if(apActive){text("Password: flightdesk",12,101,2,MUTED);text("Open 192.168.4.1",12,125,2,ACCENT);}else text("Hold right key for setup",12,112,2,MUTED);}
 else if(!fresh||s.planes.empty()){
  drawStandby(s,cfg,fresh);
 }else{
  int found=-1;for(size_t i=0;i<s.planes.size();i++)if(s.planes[i].id==selectedId)found=i;
  if(found>=0)selected=found;else selected=0;
  if(millis()-pageAt>10000&&s.planes.size()>1){selected=(selected+1)%s.planes.size();pageAt=millis();}
  selected=constrain(selected,0,(int)s.planes.size()-1);selectedId=s.planes[selected].id;
  if(radar)drawRadar(s,cfg);else drawPremiumCard(s.planes[selected],s,cfg);
  if(s.error){canvas.fillRect(0,0,320,15,0x5200);text("Data delayed / "+String(age)+" seconds old",10,3,1,AMBER);}
 }
 // Paint the new frame before enabling the light; avoid flashing the old flight.
 if(light){canvas.pushSprite(0,0);setBacklight(true);}
}
void serialScreenshot(){
 serialImage=true;Serial.setTxTimeoutMs(1000);
 Serial.println("SCREEN_RGB565 320 170");
 uint8_t row[640];
 for(int y=0;y<170;y++){for(int x=0;x<320;x++){uint16_t col=canvas.readPixel(x,y);row[2*x]=col&255;row[2*x+1]=col>>8;}Serial.write(row,sizeof(row));}
 Serial.println("\nSCREEN_END");Serial.flush();Serial.setTxTimeoutMs(0);serialImage=false;
}
void readSerial(){
 while(Serial.available()>0){char c=Serial.read();if(c=='\r')continue;if(c=='\n'){
  JsonDocument j;String err;if(!deserializeJson(j,serialLine)){
   String cmd=j["cmd"]|"";
   if(cmd=="wifi")Serial.println(applyWifi(j,err)?"WIFI_SAVED":"CONFIG_ERROR "+err);
   else if(cmd=="config")Serial.println(applyConfig(j,err)?"CONFIG_SAVED":"CONFIG_ERROR "+err);
   else if(cmd=="screen")serialScreenshot();
   else if(cmd=="wake"){displayWake.peek(millis());redraw=true;Serial.println("SCREEN_WAKE 15s");}
   else if(cmd=="status"){Snapshot s=getTraffic();JsonDocument out;out["wifi"]=WiFi.status()==WL_CONNECTED;out["ip"]=WiFi.localIP().toString();out["count"]=s.planes.size();out["status"]=s.status;out["heap"]=ESP.getFreeHeap();out["screen_on"]=backlightOn;out["backlight_gpio"]=digitalRead(38);Settings cfg=getSettings();out["radius"]=cfg.radius;out["lat"]=cfg.lat;out["lon"]=cfg.lon;out["age"]=s.valid?(millis()-s.updated)/1000:0;out["error"]=s.error;if(!s.planes.empty()){const Plane&p=s.planes[0];out["nearest"]["callsign"]=p.callsign;out["nearest"]["type"]=p.type;out["nearest"]["distance"]=p.distance;out["nearest"]["flight"]=p.flight;out["nearest"]["origin"]=p.origin;out["nearest"]["destination"]=p.destination;}serializeJson(out,Serial);Serial.println();}
  }else Serial.println("INVALID_JSON");serialLine="";
 }else if(serialLine.length()<1024)serialLine+=c;else serialLine="";}
}
void buttons(){
 static bool prev[2]={true,true},wakeOnly[2]={false,false};static uint32_t down[2]={0,0};static bool held[2]={false,false};int pins[2]={0,14};
 for(int i=0;i<2;i++){bool state=digitalRead(pins[i]);if(!state&&prev[i]){down[i]=millis();held[i]=false;wakeOnly[i]=!backlightOn;displayWake.peek(down[i]);redraw=true;}
  if(!state&&!held[i]&&millis()-down[i]>2000){held[i]=true;if(i==1)startAP();else{radar=!radar;redraw=true;}}
  if(state&&!prev[i]&&!held[i]&&!wakeOnly[i]&&millis()-down[i]>40){if(i==0){Snapshot s=getTraffic();if(s.planes.size()){selected=(selected+1)%s.planes.size();selectedId=s.planes[selected].id;pageAt=millis();}}else radar=!radar;redraw=true;}
  prev[i]=state;
 }
}
void setup(){
 pinMode(15,OUTPUT);digitalWrite(15,HIGH);pinMode(38,OUTPUT);digitalWrite(38,LOW);pinMode(0,INPUT_PULLUP);pinMode(14,INPUT_PULLUP);
 Serial.setTxBufferSize(4096);Serial.begin(115200);dataMutex=xSemaphoreCreateMutex();loadSettings();tft.init();digitalWrite(38,LOW);tft.setRotation(settings.rotation);canvas.setColorDepth(16);if(!canvas.createSprite(320,170)){tft.fillScreen(TFT_RED);while(true)delay(1000);}canvas.setTextDatum(TL_DATUM);render();
 WiFi.mode(WIFI_STA);WiFi.setHostname("flightdesk");WiFi.setAutoReconnect(true);WiFi.setSleep(false);connectWifi();configTzTime("CST-8","ntp.aliyun.com","time.cloudflare.com","pool.ntp.org");setupWeb();
 xTaskCreatePinnedToCore(networkTask,"flight-network",16384,nullptr,1,nullptr,0);xTaskCreatePinnedToCore(metadataTask,"route-metadata",16384,nullptr,1,nullptr,0);Serial.println("FLIGHTDESK_READY v0.5.5");
}
void loop(){
 readSerial();buttons();server.handleClient();if(apActive)dns.processNextRequest();
 if(reconnectRequested){reconnectRequested=false;WiFi.disconnect(false);connectWifi();redraw=true;}
 if(WiFi.status()==WL_CONNECTED){if(!mdnsStarted&&MDNS.begin("flightdesk")){MDNS.addService("http","tcp",80);mdnsStarted=true;Serial.println("WIFI_CONNECTED "+WiFi.localIP().toString());}if(apActive&&millis()-apStarted>60000){dns.stop();WiFi.softAPdisconnect(true);WiFi.mode(WIFI_STA);apActive=false;redraw=true;}}
 else if(getSettings().ssid.length()&&millis()-reconnectAt>25000&&!apActive)startAP();
 int rotation=getSettings().rotation;if(tft.getRotation()!=rotation){tft.setRotation(rotation);redraw=true;}
 if(redraw||millis()-lastRender>frameInterval){redraw=false;lastRender=millis();render();}delay(5);
}
