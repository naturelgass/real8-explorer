pico-8 cartridge // http://www.pico-8.com
version 43
__lua__
-- constants
r=24
r2=r*r
segs=4
yh=6
cols=8
sv=0.5
cv=0.866

-- top screen ball state
frame=0
d=1
frame2=0
ball_y=0
ball_vy=0
grav=0.02

-- horizontal step accumulator (halves horizontal distance per jump)
hacc=0
hstep=0.5

-- real8 settings
mode=0
platform_target=2
st_mode=0
st_depth=1
st_conv=0
swap=false
sensors=true

-- bottom screen state
bottom_on=false
bottom_mode=0
bottom_r=r\2
bottom_x=64
bottom_y=64
bottom_vx=0
bottom_vy=0
bottom_last=false
bottom_w=320
bottom_h=240
bottom_vis_h=240

-- bottom sensor config (matches sensor_ball_3ds)
friction=0.99
accel_gain=0.01
gyro_gain=0.0
max_speed=3
sensor_speed=2
filter_k=0.2
deadzone=0.03
x_sign=1
y_sign=1
use_az_for_y=false

-- sensor state
sensor_flags=0
sensor_ready=false
sensor_present=false
calibrated=false
platform=""
ax0=0
ay0=0
az0=0
gx0=0
gy0=0
ax_f=0
ay_f=0
az_f=0
gx_f=0
gy_f=0
ax_last=0
ay_last=0
az_last=0
gx_last=0
gy_last=0

-- framebuffer size (updates when top mode changes)
fb_w=128
fb_h=128

-- register helpers
function apply_regs()
 poke(0x5f85,platform_target)
 poke(0x5fe1,mode) -- request video mode
 poke(0x5fe0,sensors and 1 or 0) -- motion sensor enable flag
 local flags=0
 if st_mode==1 then flags=flags|0x01 end
 if swap then flags=flags|0x02 end
 poke(0x5f80,flags) -- stereo flags (enable/swap)
 poke(0x5f81,st_mode) -- stereo mode select
 poke(0x5f82,st_depth) -- stereo depth level
 poke(0x5f83,st_conv) -- stereo convergence level
end

function apply_bottom_mode()
 poke(0x5fe3,bottom_mode) -- request bottom mode
 refresh_bottom_fb()
end

-- bottom framebuffer size from host stats
function refresh_bottom_fb()
 local w=stat(152)
 local h=stat(153)
 if (w==0 or h==0) then
  w=bottom_w
  h=bottom_h
 end
bottom_w=w
bottom_h=h
bottom_vis_h=h
end

-- drawing helpers
function set_depth(b)
 -- write signed 4-bit value (two's complement) into low nibble
 poke(0x5ff0, b & 0x0f) -- per-sprite depth bucket
end

-- top framebuffer size from host stats
function refresh_fb()
 local w=stat(150)
 local h=stat(151)

 -- official pico-8: unknown stat() ids return 0
 if (w==0 or h==0) then
   w=128 h=128
 end

 if w!=fb_w or h!=fb_h then
  fb_w=w
  fb_h=h
  -- if the actual framebuffer size updates after a mode request,
  -- re-center the ball on the new dimensions
  reset_ball()
 end
end

function reset_ball()
 -- center the sprite in the active framebuffer
 local max_x=fb_w-2*r
 local floor=fb_h-2*r
 if max_x<0 then max_x=0 end
 if floor<0 then floor=0 end
 frame=max_x\2
 ball_y=floor\2
 ball_vy=0
 d=1
 frame2=0
 hacc=0
end

function init_bottom_ball()
 local spd=1.6
 bottom_x=bottom_w\2
 bottom_y=bottom_vis_h\2-20
 bottom_vx=spd
 bottom_vy=spd
end

function reset_bottom_ball()
 bottom_x=bottom_w\2
 bottom_y=bottom_vis_h\2-20
 bottom_vx=0
 bottom_vy=0
end

function calibrate_sensors()
 ax0=stat(142)
 ay0=stat(143)
 az0=stat(144)
 gx0=stat(145)
 gy0=stat(146)
 ax_f=0
 ay_f=0
 az_f=0
 gx_f=0
 gy_f=0
 ax_last=0
 ay_last=0
 az_last=0
 gx_last=0
 gy_last=0
 bottom_vx=0
 bottom_vy=0
end

function read_sensors()
 local ax=stat(142)-ax0
 local ay=stat(143)-ay0
 local az=stat(144)-az0
 local gx=stat(145)-gx0
 local gy=stat(146)-gy0
 ax_f=ax_f+(ax-ax_f)*filter_k
 ay_f=ay_f+(ay-ay_f)*filter_k
 az_f=az_f+(az-az_f)*filter_k
 gx_f=gx_f+(gx-gx_f)*filter_k
 gy_f=gy_f+(gy-gy_f)*filter_k
 if abs(ax_f)<deadzone then ax_f=0 end
 if abs(ay_f)<deadzone then ay_f=0 end
 if abs(az_f)<deadzone then az_f=0 end
 if abs(gx_f)<deadzone then gx_f=0 end
 if abs(gy_f)<deadzone then gy_f=0 end
 ax_last=ax_f
 ay_last=ay_f
 az_last=az_f
 gx_last=gx_f
 gy_last=gy_f
end

function update_sensor_state()
 local plat=stat(154)
 if plat!=platform then
  platform=plat
  if plat=="3DS" then
   x_sign=-1
   y_sign=-1
   use_az_for_y=true
  else
   x_sign=1
   y_sign=1
   use_az_for_y=false
  end
 end
 sensor_flags=stat(148)
 sensor_present=(sensor_flags & 0x03)!=0
 sensor_ready=(sensor_flags & 0x04)!=0
 if not sensor_ready then
  calibrated=false
 elseif not calibrated then
  calibrate_sensors()
  calibrated=true
 end
end

function update_bottom_ball_sensors()
 local speed=sensor_speed
 local ax=ax_f*accel_gain*x_sign*speed
 local ay=(use_az_for_y and az_f or ay_f)*accel_gain*y_sign*speed
 local gx=gx_f*gyro_gain
 local gy=gy_f*gyro_gain

 bottom_vx+=ax
 bottom_vy+=ay

 bottom_vx*=friction
 bottom_vy*=friction

 local maxv=max_speed*speed
 if bottom_vx>maxv then bottom_vx=maxv end
 if bottom_vx<-maxv then bottom_vx=-maxv end
 if bottom_vy>maxv then bottom_vy=maxv end
 if bottom_vy<-maxv then bottom_vy=-maxv end

 bottom_x+=bottom_vx
 bottom_y+=bottom_vy

 local minx=bottom_r
 local maxx=bottom_w-bottom_r
 local miny=bottom_r
 local maxy=bottom_vis_h-bottom_r
 if maxx<minx then maxx=minx end
 if maxy<miny then maxy=miny end

 if bottom_x<minx then bottom_x=minx bottom_vx=-bottom_vx*0.6 end
 if bottom_x>maxx then bottom_x=maxx bottom_vx=-bottom_vx*0.6 end
 if bottom_y<miny then bottom_y=miny bottom_vy=-bottom_vy*0.6 end
 if bottom_y>maxy then bottom_y=maxy bottom_vy=-bottom_vy*0.6 end
end

function update_bottom_ball_bounce()
 local bx=bottom_x
 local by=bottom_y
 local bvx=bottom_vx
 local bvy=bottom_vy
 local r=bottom_r
 local fb_w=bottom_w
 local fb_h=bottom_vis_h
 bx+=bvx
 by+=bvy
 local minx=r
 local maxx=fb_w-r
 local miny=r
 local maxy=fb_h-r
 local hit=false
 if (bx<minx) then
  bx=minx
  bvx=-bvx
  hit=true
 end
 if (bx>maxx) then
  bx=maxx
  bvx=-bvx
  hit=true
 end
 if (by<miny) then
  by=miny
  bvy=-bvy
  hit=true
 end
 if (by>maxy) then
  by=maxy
  bvy=-bvy
  hit=true
 end
 bottom_x=bx
 bottom_y=by
 bottom_vx=bvx
 bottom_vy=bvy
end

-- init
function _init()
 apply_regs()
 apply_bottom_mode()
 -- clear sprite area (prevents stray pixels)
 for y=0,2*r-1 do
  for x=0,2*r-1 do
   sset(x,y,0)
  end
 end

 -- build ball sprite
 for x=-r,r do
  for y=-r,r do
   if (x*x+y*y)<r2 then
    rx=cv*x+sv*y
    ry=cv*y-sv*x
    wid=sqrt(r2-ry*ry)*2/segs
    c=rx%wid*cols/wid
    if (((ry\yh)%2)==0) c+=cols/2
    c%=cols
    sset(x+r,y+r,c+1)
   end
  end
 end

 refresh_fb()
 reset_ball()
 init_bottom_ball()
 update_sensor_state()
end

-- draw
function _draw()
 local w=stat(150)
 local h=stat(151)

 -- official pico-8: unknown stat() ids return 0
 if (w==0 or h==0) then
   w=128 h=128
 end

 clip(0,0,w,h)
 cls(1)

 -- STATISTICS --
 local plat=stat(154)
 local msg

 if plat==0 then
   plat="pico-8"
   msg="real-8 functions not supported"
 else
   msg="press > to change screen mode"
 end

 set_depth(0)
 print(msg,2,1,12)
 print("press B for stereo mode: "..st_mode, 2, 9, 14)
 print("platform: "..plat.." target:"..platform_target, 2, 17, 11)
 print("resolution: "..w.."x"..h.." mode:"..mode,2,26,10)

 -- print("sensors: "..stat(148).." dt:"..stat(149),2,34,15)
 -- print("ax: "..stat(142),2,42,10)
 -- print("ay: "..stat(143),2,50,10)
 -- print("az: "..stat(144),2,58,10)
 -- print("gx: "..stat(145),2,66,9)
 -- print("gy: "..stat(146),2,74,9)
 -- print("gz: "..stat(147),2,82,9)

 -- ---- --

 set_depth(1)
 sspr(0,0,2*r,2*r,frame,ball_y)

 -- sound is triggered only on boundary contact (see _update())
 for x=1,cols do
  c=7
  fx=x+frame%cols
  fx%=cols
  if (fx>=4) c=8
  pal(x,c)
 end

 -- bottom screen demo (3ds host)
 if bottom_on then
  poke(0x5f84,0x01) -- enable bottom screen
  poke(0x5f84,0x03) -- draw to bottom
  cls(1)
  print("press < to change screen mode",2,1,12)
  print("press A to re-calibrate",2,9,14)
  print("ax:"..stat(142).." ay:"..stat(143).." az:"..stat(144),2,17,11)
  print("resolution: "..bottom_w.."x"..bottom_h.." mode:"..bottom_mode,2,25,10)

  sspr(0,0,2*r,2*r,bottom_x-bottom_r,bottom_y-bottom_r,bottom_r*2,bottom_r*2)
  poke(0x5f84,0x01) -- restore top draw target
 else
  poke(0x5f84,0)
 end

end

-- update
function _update()

 update_sensor_state()

 -- input: change screen modes
 local old_mode=mode
 local old_bottom_mode=bottom_mode
 if btnp(1) then mode=(mode+1)%4 end
 if btnp(0) then bottom_mode=(bottom_mode+1)%4 end
 if mode!=old_mode then
  apply_regs()
 end
 if bottom_mode!=old_bottom_mode then
  apply_bottom_mode()
 end

 -- input: update stereo depth/sides
 local stereo_changed=false
 if btnp(4) then st_mode=(st_mode==1) and 0 or 1 stereo_changed=true end
 if btnp(5) then
  if sensor_present then
   if sensor_ready then calibrate_sensors() end
   reset_bottom_ball()
  else
   init_bottom_ball()
  end
 end
 if stereo_changed then apply_regs() end

 -- bottom screen demo (3ds host or windows only)
 bottom_on = (platform == "3DS") or (platform == "Windows")
 if bottom_on!=bottom_last then
  bottom_last=bottom_on
  if bottom_on then
   refresh_bottom_fb()
   if sensor_present then
    reset_bottom_ball()
   else
    init_bottom_ball()
   end
  end
 end
 if bottom_on then
  refresh_bottom_fb()
 end

 -- update framebuffer size (may change after a mode request)
 refresh_fb()

 -- reset only the changing screen
 if mode!=old_mode then
  reset_ball()
 end
 if bottom_mode!=old_bottom_mode and bottom_on then
  if sensor_present then
   reset_bottom_ball()
  else
   init_bottom_ball()
  end
 end

 -- bottom ball update
 if bottom_on then
  if sensor_present then
   if sensor_ready then
    read_sensors()
    update_bottom_ball_sensors()
   end
  else
   update_bottom_ball_bounce()
  end
 end

 -- top ball update
 local max_frame=fb_w-2*r
 if max_frame<0 then max_frame=0 end

 -- vertical: floor boundary depends on framebuffer height
 local floor=fb_h-2*r
 if floor<0 then floor=0 end
 -- move + physics in 2 sub-steps per frame (2x faster)
 local steps=2
 for i=1,steps do
  -- move horizontally at half the previous rate
  -- (distance between floor bounces becomes ~half)
  hacc+=hstep
  if hacc>=1 then
   frame+=d
   hacc-=1
  end

  -- clamp (handles mode changes or any overshoot)
  if frame<0 then frame=0 end
  if frame>max_frame then frame=max_frame end

  -- keep the exact same horizontal bounce behavior, just with a dynamic right boundary
  if (frame==0 and d<0) d=abs(d),sfx(0)
  if (frame==max_frame and d>0) d=-abs(d), sfx(0)

  -- vertical: bounce up only when hitting the floor boundary
  ball_vy+=grav
  ball_y+=ball_vy
  if ball_y>floor then
   ball_y=floor
   ball_vy=-abs(ball_vy)
   -- play bounce sound only when touching the floor boundary
   sfx(0)
  end
 end

 frame2+=1
end

__sfx__
000100000e7700c760087500675003750017500075000700007000070000700007000070000700007000070000700007000070000700007000070000700007000070000700007000070000700007000070000700
000100000c07009070050700307000070000700000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
