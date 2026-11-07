const {test}=require('node:test');
const assert=require('node:assert/strict');
const {distanceKm,bounds,selectFlights}=require('./nearby.cjs');
test('球面距离及跨日期变更线',()=>{
 assert.equal(distanceKm(39,116,39,116),0);
 assert.ok(Math.abs(distanceKm(0,0,0,1)-111.195)<0.01);
 assert.ok(distanceKm(0,179.9,0,-179.9)<23);
 assert.deepEqual(bounds(0,179.9,30).filter((_,i)=>i===1||i===3),[-180,180]);
});
test('30km边界、过期位置、缺失坐标与矩形角落',()=>{
 const at=d=>({Lat:d/6371.0088*180/Math.PI,Lon:0,Time:1000});
 const cache=new Map([['inside',at(29.99)],['outside',at(30.01)],['old',{...at(2),Time:900}],['missing',{Time:1000}],['corner',{Lat:0.25,Lon:0.25,Time:1000}]]);
 assert.deepEqual(selectFlights(cache,0,0,30,1000,60).map(p=>p.distance.toFixed(2)),['29.99']);
});
