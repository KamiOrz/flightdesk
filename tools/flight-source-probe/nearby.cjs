#!/usr/bin/env node
const crypto = require('crypto-js');
const io = require('socket.io-client');
const protobuf = require('protobufjs');
const fs = require('node:fs');
const EARTH_KM = 6371.0088;
const rad = x => x * Math.PI / 180;
function distanceKm(lat1, lon1, lat2, lon2) {
  const a = Math.sin(rad(lat2-lat1)/2)**2 + Math.cos(rad(lat1))*Math.cos(rad(lat2))*Math.sin(rad(lon2-lon1)/2)**2;
  return 2 * EARTH_KM * Math.asin(Math.sqrt(Math.min(1, a)));
}
function bounds(lat, lon, radius) {
  const delta = radius/EARTH_KM;
  const south = Math.max(-90,lat-delta*180/Math.PI), north = Math.min(90,lat+delta*180/Math.PI);
  const dl = south<=-90 || north>=90 ? 180 : Math.asin(Math.min(1,Math.sin(delta)/Math.cos(rad(lat))))*180/Math.PI;
  // Keep the rectangle conservative at the antimeridian.
  const crosses = lon-dl < -180 || lon+dl > 180;
  return [north, crosses ? -180 : lon-dl, south, crosses ? 180 : lon+dl];
}
function selectFlights(cache, lat, lon, radius, now, maxAge) {
  return [...cache.values()].filter(p => Number.isFinite(p.Lat) && Number.isFinite(p.Lon) && Math.abs(p.Lat)<=90 && Math.abs(p.Lon)<=180 && Number.isFinite(p.Time) && now-p.Time<=maxAge && p.Time-now<=30)
    .map(p=>({...p, distance:distanceKm(lat,lon,p.Lat,p.Lon)})).filter(p=>p.distance<=radius).sort((a,b)=>a.distance-b.distance);
}
async function main() {
  const args=process.argv.slice(2), options={lat:31.230,lon:121.470,radius:30,seconds:0,'max-age':60};
  if(args.includes('--help')) { console.log('node nearby.cjs [--lat 31.230 --lon 121.470 --radius 30 --seconds 30 --max-age 60 --json output.json]'); return; }
  for(let i=0;i<args.length;i+=2) {
    const key=args[i].replace(/^--/,'');
    if(!(key in options) && key!=='json' || args[i+1]===undefined) throw Error('未知参数或缺少值: '+args[i]);
    options[key]=key==='json'?args[i+1]:Number(args[i+1]);
  }
  const {lat,lon,radius,seconds}=options, maxAge=options['max-age'];
  if(!Number.isFinite(lat)||Math.abs(lat)>90||!Number.isFinite(lon)||Math.abs(lon)>180||!Number.isFinite(radius)||radius<=0||radius>1000||!Number.isFinite(seconds)||seconds<0||!Number.isFinite(maxAge)||maxAge<=0) throw Error('坐标/半径/时间参数无效，半径应为 0–1000 km');
  const base='https://flightadsb.variflight.com', headers={Origin:base,Referer:base+'/','User-Agent':'FlightDesk/diagnostic'};
  async function getJson(url) { const r=await fetch(url,{headers,signal:AbortSignal.timeout(12000)}); if(!r.ok)throw Error('HTTP '+r.status); return r.json(); }
  console.log(`中心 ${lat}, ${lon}；半径 ${radius} km；位置有效期 ${maxAge} 秒`);
  const pcr=await getJson('https://adsbapi.variflight.com/api/v2/business/pcr/adsb?lang=zh_CN');
  if(pcr.code!==200)throw Error('游客初始化被拒绝: '+pcr.code);
  const auth=JSON.parse(crypto.AES.decrypt(pcr.data,crypto.enc.Utf8.parse('flightadsb123456'),{mode:crypto.mode.ECB,padding:crypto.pad.Pkcs7}).toString(crypto.enc.Utf8));
  const loc=protobuf.Root.fromJSON(await getJson(base+'/flightloc.json')).lookupType('Loc.SubscribeFlightLoc');
  const cache=new Map(); let frames=0, finished=false;
  const socket=io('https://radar.variflight.com?type=flightadsb',{path:'/adsb/socket.io',transports:['websocket'],reconnection:false,extraHeaders:headers,timeout:12000});
  let durationTimer;
  function render() {
    const now=Date.now()/1000;
    for(const [id,p] of cache)if(now-p.Time>maxAge)cache.delete(id);
    const flights=selectFlights(cache,lat,lon,radius,now,maxAge);
    const snapshot={receivedAt:new Date().toISOString(),center:{lat,lon},radiusKm:radius,frames,flights};
    if(options.json){const tmp=options.json+'.tmp';fs.writeFileSync(tmp,JSON.stringify(snapshot,null,2));fs.renameSync(tmp,options.json);}
    if(process.stdout.isTTY) console.clear();
    console.log(`${new Date().toLocaleString('zh-CN',{timeZone:'Asia/Shanghai',hour12:false})} 北京时间 | ${flights.length} 架在范围内 | 已收 ${frames} 帧`);
    if(flights.length)console.table(flights.map(p=>({航班:p.Fnum||'—',注册号:p.Anum||'—',出发:p.Org||'—',到达:p.Dst||'—','距离km':p.distance.toFixed(2),纬度:p.Lat.toFixed(5),经度:p.Lon.toFixed(5),'高度m':p.Alt??'—','速度kmh':p.Spd??'—','位置年龄s':Math.max(0,Math.round(now-p.Time)),地面:p.onground??'—'})));
    else console.log('暂无有效位置落在指定半径内（不代表该区域没有航班）。');
  }
  const refresh=setInterval(()=>{if(frames)render()},5000);
  const startup=setTimeout(()=>stop('30 秒内未收到位置数据',2),30000);
  function stop(message,code=0){if(finished)return;finished=true;clearInterval(refresh);clearTimeout(startup);clearTimeout(durationTimer);socket.close();console.log(message);process.exitCode=code;}
  socket.on('connect',()=>socket.emit('validate',{appid:auth.appid,token:auth.token,timestamp:auth.timestamp},()=>{}));
  socket.on('validateSuccess',()=>{socket.emit('sub',{clientBounds:bounds(lat,lon,radius),limit:-1,increment:1,updateTime:Date.now()},()=>{});if(seconds)durationTimer=setTimeout(()=>stop('测试结束',frames?0:2),seconds*1000);});
  socket.on('validateFailed',()=>stop('游客认证失败',3));
  socket.on('connect_error',e=>stop('连接失败: '+e.message,1));
  socket.on('disconnect',reason=>{if(!finished)stop('连接断开: '+reason,1)});
  socket.on('heartbeat',()=>socket.emit('heartbeat',{},()=>{}));
  socket.on('~',b=>{try{const data=loc.toObject(loc.decode(new Uint8Array(b)),{longs:Number});frames++;clearTimeout(startup);for(const p of data.flightLoc||[]){const id=p.Anum||p.icaoId||p.Fnum;if(!id)continue;const previous=cache.get(id);if(previous && p.Time!==undefined && p.Time<previous.Time)continue;cache.set(id,{...previous,...p});}render();}catch(e){stop('数据解码失败: '+e.message,1)}});
  process.once('SIGINT',()=>stop('已停止'));
  process.once('SIGTERM',()=>stop('已停止'));
}
module.exports={distanceKm,bounds,selectFlights};
if(require.main===module)main().catch(e=>{console.error(e.message);process.exitCode=1});
