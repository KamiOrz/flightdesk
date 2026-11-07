const http=require('node:http');
const crypto=require('crypto-js');
const io=require('socket.io-client');
const protobuf=require('protobufjs');
const {bounds,selectFlights}=require('./nearby.cjs');
const BASE='https://flightadsb.variflight.com';
const headers={Origin:BASE,Referer:BASE+'/', 'User-Agent':'FlightDesk/local-api'};
class ApiError extends Error {constructor(status,message){super(message);this.status=status;}}
function parseQuery(url){
 const parts=url.pathname.split('/').filter(Boolean);
 let lat,lon,radius,unit;
 if(parts.length===5 && parts[0]==='v2' && parts[1]==='point'){
  [lat,lon,radius]=parts.slice(2);unit=url.searchParams.get('unit')||'nm';
 }else if(url.pathname==='/api/nearby'){
  lat=url.searchParams.get('lat');lon=url.searchParams.get('lon');radius=url.searchParams.get('radius_km');unit='km';
 }else throw new ApiError(404,'Use /v2/point/{lat}/{lon}/{radius_nm} or /api/nearby?lat=...&lon=...&radius_km=30');
 if(!['km','nm'].includes(unit))throw new ApiError(400,'unit must be km or nm');
 const values=[lat,lon,radius];
 if(values.some(v=>v===null||!v.trim()||!Number.isFinite(Number(v))))throw new ApiError(400,'lat, lon and radius must be finite numbers');
 [lat,lon,radius]=values.map(Number);const radiusKm=radius*(unit==='nm'?1.852:1);
 if(Math.abs(lat)>90||Math.abs(lon)>180||radiusKm<=0||radiusKm>1000)throw new ApiError(400,'lat [-90,90], lon [-180,180], radius (0,1000] km');
 const airborne=url.searchParams.get('airborne')||'0';
 if(!['0','1'].includes(airborne))throw new ApiError(400,'airborne must be 0 or 1');
 return {lat,lon,radiusKm,airborne:airborne==='1'};
}
function aircraft(p,now){
 const a={flight:p.Fnum||'',r:p.Anum||'',t:p.icaoatype||'',lat:p.Lat,lon:p.Lon,seen_pos:Math.max(0,now-p.Time),seen:Math.max(0,now-p.Time),dst:p.distance/1.852,distance_km:p.distance,origin:p.Org||'',destination:p.Dst||'',source:'variflight'};
 const hex=String(p.icaoId||'').toLowerCase();if(/^[0-9a-f]{6}$/.test(hex))a.hex=hex;
 if(Number.isFinite(p.Alt))a.alt_baro=p.onground==='-1'?'ground':Math.round(p.Alt/0.3048);
 if(Number.isFinite(p.Spd))a.gs=p.Spd/1.852;
 if(Number.isFinite(p.Ang))a.track=p.Ang;
 if(Number.isFinite(p.Vspd))a.baro_rate=Math.round(p.Vspd*60/0.3048);
 a.onground=p.onground;return a;
}
let schemaPromise;
async function getJson(url){const r=await fetch(url,{headers,signal:AbortSignal.timeout(12000)});if(!r.ok)throw Error('Upstream HTTP '+r.status);return r.json();}
async function openRegion(q){
 const pcr=await getJson('https://adsbapi.variflight.com/api/v2/business/pcr/adsb?lang=zh_CN');
 if(pcr.code!==200)throw Error('Upstream initialization rejected');
 const auth=JSON.parse(crypto.AES.decrypt(pcr.data,crypto.enc.Utf8.parse('flightadsb123456'),{mode:crypto.mode.ECB,padding:crypto.pad.Pkcs7}).toString(crypto.enc.Utf8));
 if(!schemaPromise)schemaPromise=getJson(BASE+'/flightloc.json').then(s=>protobuf.Root.fromJSON(s).lookupType('Loc.SubscribeFlightLoc')).catch(e=>{schemaPromise=null;throw e});
 const loc=await schemaPromise;
 const socket=io('https://radar.variflight.com?type=flightadsb',{path:'/adsb/socket.io',transports:['websocket'],reconnection:false,extraHeaders:headers,timeout:12000});
 const region={cache:new Map(),received:0,error:null,close:()=>socket.close()};
 return new Promise((resolve,reject)=>{
  let settled=false;
  const timer=setTimeout(()=>fail(Error('No upstream position frame within 15 seconds')),15000);
  function fail(e){region.error=e;if(!settled){settled=true;clearTimeout(timer);reject(e);}socket.close();}
  socket.on('connect',()=>socket.emit('validate',{appid:auth.appid,token:auth.token,timestamp:auth.timestamp},()=>{}));
  socket.on('validateSuccess',()=>socket.emit('sub',{clientBounds:bounds(q.lat,q.lon,q.radiusKm),limit:-1,increment:1,updateTime:Date.now()},()=>{}));
  socket.on('validateFailed',()=>fail(Error('Upstream authentication rejected')));
  socket.on('connect_error',e=>fail(Error('Upstream connection failed: '+e.message)));
  socket.on('disconnect',()=>{if(!region.error)fail(Error('Upstream disconnected'));});
  socket.on('heartbeat',()=>socket.emit('heartbeat',{},()=>{}));
  socket.on('~',b=>{try{
   const data=loc.toObject(loc.decode(new Uint8Array(b)),{longs:Number});
   const now=Date.now()/1000;
   for(const [id,p]of region.cache)if(now-p.Time>60)region.cache.delete(id);
   for(const p of data.flightLoc||[]){const id=p.Anum||p.icaoId||p.Fnum;if(!id)continue;const prev=region.cache.get(id);if(prev&&p.Time!==undefined&&p.Time<prev.Time)continue;region.cache.set(id,{...prev,...p});}
   region.received=Date.now();if(!settled){settled=true;clearTimeout(timer);resolve(region);}
  }catch(e){fail(Error('Upstream protobuf decode failed'));}});
 });
}
function createService({provider=openRegion,maxRegions=8,idleMs=60000,staleMs=20000}={}){
 const regions=new Map();
 async function dispose(key){const e=regions.get(key);regions.delete(key);if(e){try{(await e.promise).close();}catch{}}}
 async function regionFor(q){
  const key=JSON.stringify([q.lat,q.lon,q.radiusKm]);let e=regions.get(key);
  if(e){const r=await e.promise;if(r.error||Date.now()-r.received>staleMs){await dispose(key);e=null;}}
  if(!e){if(regions.size>=maxRegions)throw new ApiError(503,'Too many active regions; retry after idle subscriptions expire');
   e={lastUsed:Date.now()};e.promise=provider(q);regions.set(key,e);
   e.promise.catch(()=>{if(regions.get(key)===e)regions.delete(key);});
  }
  e.lastUsed=Date.now();return e.promise;
 }
 const cleanup=setInterval(()=>{for(const [k,e]of regions)if(Date.now()-e.lastUsed>idleMs)void dispose(k);},5000);cleanup.unref();
 const server=http.createServer(async(req,res)=>{
  const start=performance.now();
  function send(status,body){res.writeHead(status,{'Content-Type':'application/json; charset=utf-8','Cache-Control':'no-store'});res.end(JSON.stringify(body));}
  if(req.method!=='GET'){res.setHeader('Allow','GET');return send(405,{error:'GET required'});}
  try{
   const url=new URL(req.url,'http://localhost');
   if(url.pathname==='/health')return send(200,{status:'ok',source:'variflight',activeRegions:regions.size});
   const q=parseQuery(url),region=await regionFor(q),now=Date.now()/1000;
   if(region.error||Date.now()-region.received>staleMs)throw new ApiError(503,'Upstream data unavailable');
   let flights=selectFlights(region.cache,q.lat,q.lon,q.radiusKm,now,60);
   if(q.airborne)flights=flights.filter(p=>p.onground==='0');
   const ac=flights.map(p=>aircraft(p,now));
   send(200,{ac,msg:'No error',now,total:ac.length,ctime:region.received/1000,ptime:performance.now()-start,source:'variflight',query:{lat:q.lat,lon:q.lon,radius_km:q.radiusKm},upstream_age:now-region.received/1000});
  }catch(e){send(e.status||502,{error:e.status?e.message:'Upstream failed: '+e.message,source:'variflight'});}
 });
 return {server,async close(){clearInterval(cleanup);await Promise.all([...regions.keys()].map(dispose));await new Promise(resolve=>server.close(resolve));}};
}
if(require.main===module){const port=Number(process.env.PORT||8787),host=process.env.HOST||'127.0.0.1';const service=createService();service.server.on('error',e=>{console.error(e.message);process.exitCode=1;});service.server.listen(port,host,()=>console.log(`Flight API: http://${host}:${port}\nGET /api/nearby?lat=31.230&lon=121.470&radius_km=30\nGET /v2/point/31.230/121.470/16.198704`));for(const sig of ['SIGINT','SIGTERM'])process.once(sig,()=>service.close());}
module.exports={parseQuery,aircraft,createService};
