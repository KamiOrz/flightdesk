const {test}=require('node:test');const assert=require('node:assert/strict');
const {parseQuery,aircraft,createService}=require('./server.cjs');
test('ADSB.lol 海里和公里参数及非法输入',()=>{
 assert.equal(parseQuery(new URL('http://x/v2/point/39/116/10')).radiusKm,18.52);
 assert.equal(parseQuery(new URL('http://x/v2/point/39/116/30?unit=km')).radiusKm,30);
 for(const u of ['/api/nearby?lat=39&lon=116','/v2/point/91/116/30','/v2/point/39/116/NaN','/v2/point/39/116/0','/v2/point/39/116/30?unit=bad'])assert.throws(()=>parseQuery(new URL('http://x'+u)));
});
test('输出单位换算和真实ICAO标识',()=>{
 const p={Alt:3048,Spd:185.2,Vspd:5.08,Time:100,Lat:39,Lon:116,distance:18.52,icaoId:'ABC123',onground:'0'};
 const a=aircraft(p,102);assert.equal(a.alt_baro,10000);assert.ok(Math.abs(a.gs-100)<1e-9);assert.equal(a.baro_rate,1000);assert.equal(a.dst,10);assert.equal(a.seen_pos,2);assert.equal(a.hex,'abc123');assert.equal(aircraft({...p,icaoId:'unknown',onground:'-1'},102).alt_baro,'ground');assert.ok(!('hex' in aircraft({...p,icaoId:''},102)));
});
test('HTTP精确筛选、并发复用、空结果和上游错误',async()=>{
 let calls=0;let region;
 const service=createService({provider:async()=>{calls++;await new Promise(r=>setTimeout(r,10));const now=Date.now()/1000;return region={cache:new Map([['in',{Lat:0,Lon:0.1,Time:now,onground:'0'}],['outside',{Lat:0.25,Lon:0.25,Time:now}],['old',{Lat:0,Lon:0,Time:now-100}]]),received:Date.now(),close(){}};}});
 await new Promise(r=>service.server.listen(0,'127.0.0.1',r));const base='http://127.0.0.1:'+service.server.address().port;
 try{
 const results=await Promise.all([1,2].map(()=>fetch(base+'/api/nearby?lat=0&lon=0&radius_km=30').then(r=>r.json())));assert.equal(calls,1);assert.equal(results[0].total,1);assert.ok(results[0].ac[0].distance_km<30);
 let r=await fetch(base+'/v2/point/NaN/0/10');assert.equal(r.status,400);
 r=await fetch(base+'/api/nearby?lat=50&lon=100&radius_km=30');assert.equal((await r.json()).total,0);
 r=await fetch(base+'/health');assert.equal(r.status,200);
 }finally{await service.close();}
 const failed=createService({provider:async()=>{throw Error('test unavailable')}});await new Promise(r=>failed.server.listen(0,'127.0.0.1',r));try{const r=await fetch('http://127.0.0.1:'+failed.server.address().port+'/v2/point/0/0/10');assert.equal(r.status,502);assert.ok(!('ac' in await r.json()));}finally{await failed.close();}
});
