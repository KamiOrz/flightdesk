from pathlib import Path
import cadquery as cq, math,json
O=Path(__file__).parent; (O/'stl').mkdir(exist_ok=True)
axis=(0,math.cos(math.radians(32.5)),math.sin(math.radians(32.5)))
def rot(s):return s.rotate((0,0,0),axis,180)
def local(s):return rot(s.translate((0,7,-32)))
def world(s):return rot(s).translate((0,-7,38.4))
def rr(w,h,r,z,d,x=0,y=0):return cq.Workplane('XY').workplane(offset=z).center(x,y).rect(w,h).extrude(d).edges('|Z').fillet(r).val()
def box(x,y,z,w,h,d):return cq.Workplane('XY').box(w,h,d,centered=False).translate((x,y,z)).val()
def cyl(x,y,z,d,h):return cq.Workplane('XY').workplane(offset=z).center(x,y).circle(d/2).extrude(h).val()
old=cq.importers.importStep(str(O.parent/'flightdesk-assembly.step')).solids().vals()
shell=local(old[0]);cover=local(old[1])
outer=cq.Workplane(obj=rr(76,40,7,0,18.2)).faces('<Z').edges().fillet(1.8).val()
shell=shell.intersect(outer)
outerback=cq.Workplane(obj=rr(75.4,39.4,6.7,11,9.6)).faces('>Z').edges().fillet(1.0).val()
cover=cover.intersect(outerback)
foot=cq.Workplane(obj=rr(78,48,8,0,10.4,0,1)).faces('>Z').edges().fillet(2.4).val()
# Rounded rear support, with an exact central mating plane and no residual plinth lip.
a=math.radians(65);ytop=math.cos(a)*3+math.sin(a)*20.6-7;ztop=math.sin(a)*3-math.cos(a)*20.6+38.4
yfront=ytop-(ztop-10.2)/math.tan(a)
cradle=cq.Workplane('YZ',origin=(-26,0,0)).polyline([(yfront,10.2),(22.5,10.2),(22.5,ztop),(ytop,ztop)]).close().extrude(52).val()
# Large rear upright corners first, then top/rear exposed edges.
rear_vertical=[e for e in cradle.Edges() if abs(e.Center().x)>25.9 and e.Center().y>22.4 and e.Length()>15]
cradle=cq.Workplane(obj=cradle).newObject(rear_vertical).fillet(5).val()
rear_top=[e for e in cradle.Edges() if abs(e.Center().z-ztop)<.01 and e.Center().y>20]
cradle=cq.Workplane(obj=cradle).newObject(rear_top).fillet(3).val()
base=foot.fuse(cradle).clean()
root_edges=[e for e in base.Edges() if abs(e.startPoint().z-10.4)<.01 and abs(e.endPoint().z-10.4)<.01 and abs(e.Center().x)<30 and e.Center().y>0]
base=cq.Workplane(obj=base).newObject(root_edges).fillet(2).val().clean()
# Two rear cover screws retain original local datum and M3 pilot.
for x in [-16,16]:base=base.cut(world(cyl(x,-6,20.45,2.6,8.6)))
base=base.cut(box(-17.3,-9.3,-.1,34.6,20.6,8.35))
base=base.fuse(cyl(0,1,3.35,5.8,7.05)).cut(cyl(0,1,3.25,2.5,5.95))
for y in [-18,15]:base=base.cut(box(-28,y,-.1,56,6,1.1))
disc=cyl(0,1,2,12,1.2).cut(cyl(0,1,1.9,3.4,1.5)).cut(cyl(0,1,2.75,6.3,.6))
parts={'front-housing':world(shell),'rear-pin-carrier':world(cover),'weighted-base':base,'weight-retainer':disc}
for i,n in enumerate(['button-1','button-2','button-keeper-1','button-keeper-2'],3):parts[n]=old[i].translate((0,0,6.4))
metal=box(-17,-9,3.2,34,20,4.85).cut(cyl(0,1,3.1,6.2,5.1))
collisions=[]
for i,(a,s) in enumerate(parts.items()):
 assert s.isValid() and len(s.Solids())==1,(a,'invalid or disconnected')
 for b,t in list(parts.items())[i+1:]+[('metal-weight',metal)]:
  v=s.intersect(t).Volume()
  if v>.01:collisions.append([a,b,round(v,4)])
assert not collisions,collisions
assembly=cq.Assembly(name='FlightDesk_Soft_Box_V6')
colors={'front-housing':(.17,.20,.22),'rear-pin-carrier':(.13,.16,.18),'weighted-base':(.39,.43,.44),'weight-retainer':(.13,.16,.18)}
for name,s in parts.items():
 color=colors.get(name,(.07,.51,.52))
 assembly.add(s,name=name,color=cq.Color(*color))
 if name=='front-housing':p=shell
 elif name=='rear-pin-carrier':p=cover.rotate((0,0,0),(1,0,0),180)
 elif name.startswith('button'):p=local(s.translate((0,0,-6.4)))
 else:p=s
 b=p.BoundingBox();p=p.translate((-(b.xmin+b.xmax)/2,-(b.ymin+b.ymax)/2,-b.zmin))
 cq.exporters.export(p,str(O/'stl'/(name+'.stl')),tolerance=.025,angularTolerance=.08)
assembly.save(str(O/'flightdesk-soft-box-v6.step'))
# View-only black display at the actual active-area dimensions; excluded from manufacturing STEP.
parts['display-off']=world(box(3.1536-21.36,-11.3475,2.12,42.72,22.695,.05))
report={'style':'original rectangular instrument, softened front/rear perimeter and plinth','front_mm':[76,40,18.2],'front_edge_radius_mm':1.8,'rear_edge_radius_mm':1,'base_top_radius_mm':2.4,'rear_support_vertical_radius_mm':5,'rear_support_top_radius_mm':3,'support_to_foot_radius_mm':2,'weight_mm':[34,20,4.85],'hole_mm':6.2,'locator_mm':5.8,'screw':'M3x6 self tapping panhead, head height <=2 mm','pin_projection_mm':8.25,'collisions':collisions,'physical_fit':'not verified'}
(O/'checks.json').write_text(json.dumps(report,indent=2))
# Export tessellated assembly for a deterministic software preview.
mesh=[]
for n,s in parts.items():
 vs,fs=s.tessellate(.06,.12);mesh.append({'name':n,'color':colors.get(n,(.07,.51,.52)) if n!='display-off' else (.025,.035,.04),'vertices':[[v.x,v.y,v.z] for v in vs],'faces':fs})
(O/'preview-mesh.json').write_text(json.dumps(mesh))
print(json.dumps(report))
