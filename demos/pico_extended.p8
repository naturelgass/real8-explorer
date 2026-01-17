pico-8 cartridge // http://www.pico-8.com
version 43
__lua__
r=24
r2=r*r
segs=4
yh=6
cols=8
frame=0
sv=0.5
cv=0.866
d=1
frame2=0

-- horizontal step accumulator (lets us halve horizontal distance per jump
-- while keeping integer pixel positions + the original boundary bounce logic)
hacc=0
hstep=0.5

-- defaults (real8 extensions)
mode=0
st_mode=0
st_depth=1
st_conv=0
swap=false
sensors=true

-- framebuffer size (updates when mode changes)
fb_w=128
fb_h=128

-- vertical motion (bounce only on the floor)
ball_y=0
ball_vy=0
grav=0.02

function apply_regs()
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

function set_depth(b)
 -- write signed 4-bit value (two's complement) into low nibble
 poke(0x5ff0, b & 0x0f) -- per-sprite depth bucket
end

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

function _init()
 apply_regs()
 -- clear sprite area (prevents stray pixels)
 for y=0,2*r-1 do
  for x=0,2*r-1 do
   sset(x,y,0)
  end
 end

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
end

function q16(n)
 return n/65536
end

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
 local plat=stat(152)
 local msg

 if plat==0 then
   plat="pico-8"
   msg="real-8 functions not supported"
 else
   msg="< > to change screen mode"
 end

 set_depth(0)
 print(msg,2,1,12)
 print("resolution: "..w.."x"..h,2,9,13)
 print("platform: "..plat, 2, 17, 11)
 print("stereo mode: "..st_mode.." swap: "..tostr(swap), 2, 26, 14)
 print("sensors: "..stat(148).." dt:"..stat(149),2,34,15)
 print("ax: "..stat(142),2,42,10)
 print("ay: "..stat(143),2,50,10)
 print("az: "..stat(144),2,58,10)
 print("gx: "..stat(145),2,66,9)
 print("gy: "..stat(146),2,74,9)
 print("gz: "..stat(147),2,82,9)

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

end

function clamp(v,lo,hi)
 if v<lo then return lo end
 if v>hi then return hi end
 return v
end

function _update()

 -- change screen modes
 local old_mode=mode
 if btnp(0) then mode=(mode+3)%4 end
 if btnp(1) then mode=(mode+1)%4 end
 if mode!=old_mode then
  apply_regs()
 end

 -- update stereo depth/sides
 local stereo_changed=false
 if btnp(4) then st_mode=(st_mode==1) and 0 or 1 stereo_changed=true end
 if btnp(5) then swap=not swap stereo_changed=true end
 if stereo_changed then apply_regs() end

 -- update framebuffer size (may change after a mode request)
 refresh_fb()

 -- at every mode change the ball resets to the center
 if mode!=old_mode then
  reset_ball()
 end

 -- adapt horizontal stage boundary to current framebuffer width
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
