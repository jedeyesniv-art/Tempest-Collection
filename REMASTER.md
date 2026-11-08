# Tempest 2000 remaster renderer - survey and design

Step 1 (survey) done 2026-11-08. Builds on Llamasoft on the ST's REMASTER.md (C:\LlamatronRecomp): the game runs
unchanged; hooks catch its draw primitives; we draw those natively at full resolution, interpolated between game
ticks, over the emulated picture (which keeps everything not caught). Run-ahead removes the interpolation delay.

## How a frame is drawn (yak.s; addresses for the owner's ROM)
- `Frame` (vertical-blank interrupt, 60 Hz): game logic (run_objects over `activeobjects`), copies the Object
  Processor list, and if `screen_ready` and `sync` swaps `dscreen` <-> `cscreen` (the displayed buffer). Logic
  never waits for drawing; a slow draw only drops displayed frames.
- `mainloop` waits for `sync`, sets `gpu_screen` = `dscreen`, runs `mainloop_routine` (normally `draw_objects`),
  then `screen_ready` = 1.
- `draw_objects` (all through the GPU via `gpurun` $81A4F0, module in a0, `gpu_mode` $F03FFC, parameters in
  `in_buf` $F03F60, destination `gpu_screen` $F03FF8):
  1. clearscreen: llama mode 0 (undraw) - caller $80FD0C. Every frame: no feedback trails in normal play.
  2. starfield: llama mode 3 (starfield1, in_buf: vp xyz, field1, warp_count/add) - $80F0EE.
  3. web: solid = donky mode 0 (web3d) $80F1BA; vector = llama mode 2 (vect3d) via drawweb.
  4. each object in `activeobjects` via its `draw_vex` routine (index at 34(a6)): `draw` -> vector objects with
     llama vect3d ($80FA12), or (header < 0) queued on `apriority` by z and drawn later by `drawpolyos`
     (`solids` table: horse mode 0 polyo2d, $803AB4); draw_pel pixels, draw_pring / prex pixel rings (xcamel 6 / 7,
     $80768C / $8076EA), dxshot, drawsphere (ox / horse pshere), draw_fw fireworks (xcamel 5 $80FCEE), etc.
  5. showscore (stoat text $80DC2E), drawpolyos, drawmsg, demo text, superzapper bolts (ox fline).
- 2D bitmaps (logos, big fonts, "TOP GUNS", the pause text) are blitter copies (centext etc.), not GPU calls:
  they stay in the emulated picture.

## GPU modules (yakgpu.s; header address in ROM -> name)
llama $81A526 (vect3d 3D vectors, starfield1, undraw), goat $81AFAE (vect3d for webs, view_otm), antelope
$81BBFA (ripplewarp / stretch / scale-and-rotate bitmap effects), camel $81CA4A (poly, rex), xcamel $81D0DA
(poly, pixel rings, snglpix), stoat $81DE12 (texter), ox $81E10E (fline lightning, starseg, psphere), horse
$81EB52 (polyo2d solid polygons, pixel rings, sphere), donky $81F6A2 (web3d solid web, starplane).

## Survey (headless --gpulog, attract -> play, 3600 frames)
- 4-256 title: antelope 2 (scale+rotate the logo) + camel 4.   - 257-1652: antelope 2 backgrounds (ripple menu).
- 407-601 yak screen: llama 0 + donky 1 (starplane).            - 753-801 options text: stoat / camel 2 / antelope 1.
- 953+: starfield (llama 3) everywhere; 1909+ solid web (donky 0); play: horse 0 (3-10 a frame), llama 2 (5 a
  frame), xcamel 5/6/7, stoat 0 (score). 2798-3043: camel 4 (warp / death streaks).
- Play frame: ~18 GPU calls. Trap at gpurun is invisible to the game (screenshots byte-identical).

## Tools built for this
- Core fork: line-A host traps (m68kHostTrap: host runs, then the displaced opcode executes normally),
  M68KHostGetReg, GPUHostRAM (GPU RAM without bus side effects).
- src/traps.cpp: traps::Install(addr, expected word, handler), Reg(), Read8/16/32 (RAM, cart window, GPU RAM).
- src/gpulog.cpp: `--gpulog=F0:F1:file` logs frame, caller, module, mode, gpu_screen, in_buf (39 longs).
- Could not build rmac here (running downloaded tools was refused), so no symbol file: callers are mapped to the
  source by hand (byte patterns + the call order in draw_objects).

## Design
- Capture: trap gpurun. For each call we understand, record a draw item (module, mode, in_buf copy, plus the RAM
  it reads: vertex / connection lists, colour tables, sine table $30000) under the `gpu_screen` it targets, and
  SKIP the GPU run for captured modes (so the emulated buffer holds only what we don't draw). Uncaptured modes
  run as before. Skipping GPU work also removes the original's draw slowdown (logic is 60 Hz regardless).
  Skipping must never cover modes whose results the 68K reads back (view_otm transforms etc.).
- Show: each host frame, the item list of `cscreen` (the buffer the OP displays) is drawn.
- Draw: port each captured GPU mode's maths to C++ (the .gas files are the spec): vect3d (sine-table rotation,
  translate, perspective with xcent / ycent / scaler, depth-cued intensity z_top / z_max, CRY colours), polyo2d,
  web3d, starfield1, pixel rings, spheres, fline. Output lines / triangles in 384x240 screen space as floats,
  rendered with OpenGL at the window's resolution (MSAA; later glow).
- Compose: our layer, then the emulated picture with the cleared colour (black) see-through on top (text / HUD /
  bitmaps over objects, as the game draws them later in the frame). Check at 1x in software against the
  original buffer (overlay alignment; lines won't be pixel-identical, positions must be).
- Interpolate: match items between consecutive captured frames (key = caller + object a6 + occurrence, as on the
  ST) and lerp the in_buf inputs (x, y, z, angles; the camera vp_x/y/z) before projecting, at display rate.
  Run-ahead (core serialize, 2 frames default) to hide the one-frame delay.
- 16:9: widen the projection (xcent / clip extents), starfield over the full width, emulated picture centred
  4:3; HUD placement later.

## Next (step 2 prototype)
1. Read llama vect3d fully (transform_3d tail: perspective, z clip, intensity, the line draw and colour), port it,
   and draw the web + claw + vector enemies natively on top of the untouched emulated picture (no skipping yet)
   to check alignment at 1x and 4x.
2. Then skip those calls, compose, and move on to horse polyo2d (solid enemies), donky web3d, starfield, pixels.
