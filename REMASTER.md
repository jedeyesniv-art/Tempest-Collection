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

## Step 2 prototype (2026-11-08): llama vect3d native - DONE
- rmac builds here (owner allowed; tools/make_syms.py -> reference/t2k.sym, 2463 symbols: text +$802000, data +$4000,
  bss +$EFA8 from rmac's listing of yak.s; all hand-found addresses confirmed). Rebuild: reference/tools/rmac/rmac.exe
  -fb -isrc -lsymout/yak.lst src/yak.s -o symout/yak.cof (in reference/tempest2k), then python -I tools/make_syms.py.
- src/remaster.cpp captures every llama mode 2 (vect3d) call at gpurun (in_buf, scaler/z_max/z_top/_sysflags from GPU
  RAM, the vertex and connection lists), and while the remaster is on SKIPS it (the trap runs an rts) for the two play
  buffers (cscreen $7FAC / dscreen $7FB0). Lists are kept per gpu_screen, cleared at clearscreen (llama mode 0).
- Projection verified against the GPU's own vertex cache (RM_CHECK=1: vertex_buffer $F03B40, 24 bytes a vertex): ours
  are within 1-2 px (the GPU truncates zi = Z>>14 and the >>7; we keep the fractions).
- Which list to show: the picture the core returns for a frame was scanned out during it, i.e. it shows the buffer
  that was cscreen at the END of the PREVIOUS frame (a frame draws dscreen completely; the swap is at the next VBL).
  fit_origin.py hit rate 0.875 with that, 0.79 with the end-of-frame cscreen.
- Mapping GPU screen -> emulated picture: x - 23.75, y + 5.25, no scale (tools/fit_origin.py on 3 moving frames).
- Window: emulated picture, then our lines (SDL_RenderGeometry quads, 1-px feathered edges, width setting
  remaster_line_width = 1.5 game px). F8 toggles (setting remaster). Tests: --rmshot=F:bmp (+ .lines, .orig.bmp),
  --state=<file> --winshot=N:<bmp> (window mode: start from a state, save the rendered window after N frames).
- Not yet: the solid web fill (donky 0), solid claw / enemies (horse polyo2d via drawpolyos), starfield (llama 3),
  pixels / rings (xcamel 5-7), lightning (ox fline), text. Text and HUD drawn after objects now sit UNDER our lines.

## Step 3 (2026-11-08): solid web, starfield, solids - DONE
- donky web3d (solid web): per lane a quad near rim (z - $500000, >>14 +1) to far rim (z + $500000, >>14),
  vertices = data words * 8 - 64, rotated by in_buf+24, Gouraud bright (near) -> 1/4 (far); brightness =
  ((sine[(phase + angl) & 255] >> 1) + 64) * glint >> 8, where phase is the PREVIOUS lane's colour word (the GPU
  overwrites the loaded phase with it; carried over between calls). Owner: the original fill "often glitchy, snaps in
  and out" - ours is solid every frame.
- llama starfield1: 32-byte stars (x, y, z, colour word), z = (z - vp) & $1FFFFFF, intensity 255 - (z >> 17),
  zi = (z >> 14) + 1, centre 192 / 120; warp: warp_count extra points warp_add apart (drawn as streak lines).
  Drawn as soft dots (8-triangle fans). Warp points are separate dots, NOT joined into streaks: the original plots
  each point (owner 2026-11-08: our joined streaks looked wrong between levels / on first level select; repro state
  logs/owner_star.state). The lattice of short diagonal dashes seen then (and on a death) is not starfield1 (2300+
  lit pixels in a corner vs ~800 star points a frame): another display layer, still the emulated picture.
- horse polyo2d (solid claw / enemies / shots): long faces; 16-byte faces (colour word, 3 x (vertex, intensity
  word: $FF00 = full, byte = word >> 8), pad); x, y vertex words; rotate, <<8, + pos, / (z >> 14), >> 7, + xcent
  ($F03EFC) / ycent ($F03EF8). Kept in call order (the game sorts far to near) and drawn after the lines.
- Compose (window and --rmshot): stars, web fill, lines, solids, then the emulated picture with black see-through
  (text, HUD, anything not ours).
- Debug: RM_NOSKIP=1 (capture but let the GPU draw: same-run comparisons), RM_OURS=1 (rmshot without the emulated
  picture). Remaster on vs off runs drift apart (skipping GPU work changes main-loop timing), so compare in one run.

## Step 4 (2026-11-09): camel rex (scaled bitmap sprites) - DONE
- camel mode 4 rex: a source rect of a 320-wide CRY sprite sheet (pic / pic2 / pic5), each source pixel put as ONE
  destination pixel at start + i * (scalex, sheary) + j * (shearx, scaley); black see-through (DCOMPEN, pattern 0).
  dst / scale: sdiv by (z >> 14), << 9, + $C00000 / $780000 ($8C0000 PAL); start = dst - (w * sx + h * shx,
  h * sy + w * shy) / 2 (always centred, the mode word is unused); left-edge hack drops source columns while x < 0.
- Taken over only for the draw_vex routines (by caller range): draw_oneup $8072CA, draw_pixex $8074D4 (green shots
  = pic 10,182 7x7 at scale $4000, power-up pixel explosions), dmpix / draw_mpixex $807806..$80791C (Excellent,
  demo explosions). The title's rexfb ($807294), the yak screen ($807104) and xcamel's rex (level select) draw over
  backgrounds we don't capture and stay with the GPU.
- Drawn as squares of min(scale, 1) game px (at least 1 window px): below scale 1 a sharp small image (the shots
  show their sprite), above it the original's dot matrix. Verified same-run (RM_NOSKIP) on play.state shots and an
  attract-demo explosion (logs/demo6400.state, frames +104..+170). tools/side_by_side.py <rmshot> <png> [crop].

## Step 5 (2026-11-09): interpolation + run-ahead - DONE
- Scenes: the last two displayed frames' captured inputs (camera-relative). remaster::Interpolate(t) matches calls by
  (caller, a6, occurrence) and blends positions, angles (fractional sine table), the star viewpoint, rex position /
  scale; a move over 32 units + 1/4 of the distance in a tick is not blended (reused slots, camera cuts). Headless
  stays t = 1; RM_T=<t> renders --rmshot blended (tools/check_interp.py: t = 0 equals frame N-1, t = 0.5 halfway,
  0.001 px on a sideways-moving web). Window: t = how far real time is into the current frame's slot.
- Run-ahead (main.cpp RunAhead, setting run_ahead default 2, headless --runahead=N): after the real frame, save the
  core state + t2k::Mark (stick state, $DFFC00 port area: not in the core's state) + remaster::Mark (capture
  lists), emulate N frames with the same input (remaster::AfterFrame(publish) only on the last), rewind. Verified:
  picture and remaster at f == plain run at f + 2, audio byte-identical, claw trace identical. Cost ~4 ms a frame.
- Not covered: hidden frames can write the EEPROM save buffer early (the same write the real frame makes 2 frames
  later unless the input changes). The emulated HUD / text shows frame f + N while our layer blends f + N - 1 -> f + N.

## Step 6 (2026-11-09): any aspect ratio - DONE
- Hor+ / Vert+: the emulated picture stays a 4:3 box fitted to the window; our layer fills the window with the same
  projection (the game's world is there: web, enemies and the starfield cover 21:9 and portrait). Culls (stars,
  pixels, rex) use the window's visible GPU-space range (remaster::SetWindow) instead of the original screen; headless
  keeps the original culls unless --aspect=W:H (rmshots that shape). Window size for tests: --winsize=WxH.
- HUD: in the game (scene has vector objects or a solid web, not the demo) the picture's top band (rows 0-64) is
  split: x 0-160 (score, lives) to the window's top left, 260-326 (icon) to the top right, the middle stays
  (setting hud_corners, default on). A stopgap until the HUD text is ours.
- Fixed 2026-11-09 (owner's screenshots): the title / option / game select screens never clear with the GPU, so the
  last two yak-screen web captures stayed in the lists and were shown alternately (a juddering web in the side bars /
  black transitions) and turned the HUD split on over the menus. Now: no llama mode 0 clear for 8 frames -> empty
  scene (counter in the run-ahead mark), and the HUD split needs t2k::HudShown(): the overlay object beasties+64 ($DBBA) is
  the score strip (ObType 8, 7 head to head; screen3 = $50000 holds the HUD) AND mainloop_routine ($E374) ==
  draw_objects ($80F05E). Type 8 is also set in level select (draw_oo); after a game mainloop_routine stays
  draw_objects on the title / Top Guns screens (they set other ObTypes), so neither test works alone. The score is
  not a GPU call (blitted into screen3 when it changes). Top Guns' tumbling wire web is the game's own (vect3d). Debug: RM_TRACE=1 prints each published scene (frame, cscreen, mainloop_routine, calls).
- Still 4:3: everything not ours (title / menu screens, the dash lattice, superzapper bolts). Untested: 2-player /
  duel HUD (player 2's score may reach left of x 260).

## Step 7 (2026-11-09): ox fline (Superzapper bolts, AI droid zap) - DONE
- Callers: namsg $80F42E / $80F4B6 (two bolts, zapped enemy boltx/y/z -> the claw lane's two rim points; only with
  solids on, bolt_lock set by zappit), draw_adroid (droid zap), nohorra, pyrfb (unused). All in play: every ox mode 0
  into the play buffers is taken over.
- in_buf: x1 y1 z1 x2 y2 z2 (camera relative), colour (flashcol), segs (32), seed (frames & $FF | 1). Per segment:
  base += (end - start) / segs (sdiv); inner points get x += (r - 128) << 10, y += (r' - 128) << 10 from the 8-bit
  shift register (odd: >> 1 ^ $B8, even: >> 1; two steps a point; the last point unmoved); project z >> 14,
  / z >> 7 + xcent / ycent; vect3d depth intensity; a segment with an end off screen is dropped (ours: off the view).
- Verified same run (RM_NOSKIP, red / green overlay of the bolt pixels): identical paths (1 px rounding). Drawn as
  thin lines after the solids. Interpolation moves the ends only; each frame's jitter is kept whole (blending two
  random shapes made the bolt calm down between frames). Run-ahead 2 picture == plain run + 2 frames.

## Step 8 (2026-11-09): bolt glow, particle size, menu timing
- Bolts: an additive glow (two wide feathered passes) under a whitened core (settings remaster_bolt_width game px,
  default 2, 0 = the original thin line; remaster_bolt_glow). Particles (xcamel pixels / rings) are solid discs and,
  with the rex dot-matrix dots (explosions; not the small sharp images), x remaster_particle_size (default 1.5).
- Menu timing (owner, comparing BigPEmu: melts too quick then black for a second, mode select texture too fast).
  The melt (fade -> ffade / failfade, caller $806114 antelope 2 'scar') is 32 steps of -8 intensity inside a fixed
  150-frame transition (failcount); thang syncs each step to a VBL, and the emulated scar (one full-screen blit)
  finished inside a frame, so it was black by ~35 frames. The core's experimental timing options (blitter / GPU
  pipeline / DRAM) changed nothing measurable here (its blit model caps at 2 fields; checked with a debug print).
  Host pacing instead: gpuwait ($81A50E) -> jmp stub $DFFDC0 that spins while port $DFFE30 != 0; the gpurun trap
  sets it for scar (pace_melt, default 4 frames a step: the fade now lasts ~126 of the 150 frames) and optiondraw's
  ripple ($804980 antelope 1; pace_ripple, default 2: 30 fps). The Top Guns background shares scar's caller.
- Spawn markers (draw_pel, xcamel 5 snglpix, red $F0, camera z ~600 -> ~200): size x clamp(200 / z, 0.3, 1),
  2/3 of a particle at the far rim (owner: grow in, smaller than the other particles). Menu DISPLAY: particle size,
  lightning width, lightning glow (number items have a default now, not their minimum).
- Run-ahead only where latency matters (t2k::LatencyMatters: play HUD up or level select's draw_oo): on the menu
  screens N extra frames a shown frame dropped emulation below real time (~40 fps in the window).

## Step 9 (2026-11-09): the 'dash lattice' = drawmsg text (stoat texter) - DONE
- Not a starfield or a display layer: drawmsg ($80DC2E, second line $80DC98) prints the big centre messages with
  stoat texter (camel rex per character from afont: pic2 sheet, 15 x 18 glyphs). Zooming in, scalex = 1 and scaley
  ~20-26 with shear: each glyph row becomes a run of adjacent pixels (a dash), rows ~20 px apart -> the lattice.
  Its width is the string's, so it stays inside the 4:3 width; wide screens show it whole vertically.
- Ported (BuildText): xs = w * scalex, ys = h * scaley, axs = h * shearx, ays = w * sheary; start = dest << 16 -
  ((xs + axs) >> 1, (ys + ays) >> 1); pixel (i, j) at start + i * (scalex, sheary) + j * (shearx, scaley); advance
  int(w * scalex) + 2; left-edge column drop sticks for the rest of the string (the original's register reuse).
  Drop-shadow mode (in_buf+36 = 1) and other texter callers stay with the GPU. Pixels: squares of min(scale, 1),
  not grown by the particle setting. Interpolated: scale / shear. Verified same run (RM_NOSKIP): lattice frames and
  "SUPERZAPPER RECHARGE" match; run-ahead 2 == plain + 2.
- Fixed: yellow lane lines missing in the warp. vect3d ends a vertex's connection list when the vertex BYTE is 0
  (after applying its colour byte: $0200 = colour 2, end); we ended only on a 0 word, so the web's second, colourless
  section (after $0200 0000) was drawn too and repainted the yellow lane blue. Same-run checks: warp, play, demo.

## Step 10 (2026-11-09): sharp picture, HUD layer
- The HUD is not a GPU draw: score digits / lives (CopyBlock from pic2, ashowscore / setlives) and the warp pyramid
  (makepyr / ppyr) go into screen3 ($50000, 384 x 48 CRY; 32 rows head to head), an Object Processor overlay
  (beasties+64). screen3 (0, 0) = picture (-24, 14) (exact match on play.state).
- remaster::Hud() copies screen3 when a frame is published (run-ahead: the shown frame's). In the game on a non-4:3
  window (hud_corners): picture pixels under a non-zero screen3 pixel are made see-through, and screen3 is drawn as
  our own layer: x 0-184 to the window's top left, 184-284 in place, 284-384 to the top right. Replaces the band
  split of the picture (no more chopping the top 64 rows).
- Sharp bilinear for the picture and the HUD (setting picture_filter sharp | smooth): nearest to an integer multiple
  in a render target, then a linear fit. All remaining pixel art (menus, PRESS FIRE, DEMO, pause) is crisp.

- xBR option (picture_filter = xbr, menu "Pixel art"): src/xbr.cpp, 2xBR as FFmpeg vf_xbr (|dY|+|dU|+|dV| distance,
  eq < 155, 2x blend set) applied twice = 4x, on premultiplied ARGB (alpha blended like colour, + |dA| in the
  distance) so edges against see-through black fade out; drawn with SDL_BLENDMODE_BLEND_PREMULTIPLIED. Picture and HUD
  strip, recomputed only on a new frame (or a filter change); real time at 1440p.
- Original mode (setting native, menu DISPLAY): remaster off, run-ahead 0, no HUD layer, bilinear 4:3, no menu
  pacing. Controls (stick hook, remapping) stay.

## Step 11 (2026-11-09): CRT+ / CRT++ from Llamasoft on the ST
- src/crt.cpp = the ST's crt.cpp (per-game looks and docs mode left out; settings via settings::Num, read every
  frame), shaders copied (src/shaders/*.frag.hlsl + committed .dxil.h; tools/build_shaders.py rebuilds with the SDK's
  dxc). Renderer: SDL's GPU renderer with DXIL (D3D12) via crt::CreateRenderer; T2K_RENDERER=<name> for tests.
- With a look on (setting crt 1 / 2, F9 cycles, menu CRT section with the ST panel's sliders and the owner's ST
  defaults): the whole window (our layer, picture, HUD) is drawn into a window-sized target, then crt::DrawPicture
  draws it with src = the window in game pixels (ww * 326 / dst.w, wh * 240 / dst.h), so the scanlines / mask follow
  the Jaguar's lines on any window shape. Menu and toasts are drawn after, unfiltered. Off in Original mode.
  Verified at 1440p: off unchanged, CRT+ / CRT++ real time.

## Step 12 (2026-11-09): 2-player team and Duel
- How to get there: game select (cursor starts on Tempest 2000): Tempest Plus -> "1 player / AI droid / 2 player team";
  Tempest Duel -> play / practice -> one round / best of 3 / 5 -> a two-web level select. States: logs/team.state,
  logs/duelplay.state (logs/modesel.state = game select).
- Team: two claws on one web, one shared HUD: all ours, matches the original.
- Duel: two webs drawn by goat (mode 2, a vect3d with a viewer matrix, view_otm mode 4): NOT captured, so they stay in
  the emulated picture (sharp scaled). The HUD (ObType 7, 32 rows) is player 1 x 0-186 | player 2 186-384.
- The HUD layer no longer masks the picture: masking left holes where the HUD covered things in the picture (the Duel
  web tops). Core hook (op.c vjOPSkipLo / Hi): the OP skips fixed bitmaps reading screen3's HUD rows while our HUD
  layer is up (set before each emulated batch), so the picture is the play area alone.
- Open: port goat vect3d so the Duel webs are native too.

## Next
1. Remaining play primitives: xcamel 5 (draw_pel far enemies as pixels), xcamel 6/7 (pixel rings / explosions),
   camel 4 (death / warp), ox fline (superzapper bolts), horse / ox psphere, and the other draw_vex routines.
2. horse polyo2d (solid claw and enemies: drawsolidxy $803A90..) and donky web3d (solid web), then llama 3 starfield
   and the xcamel pixels, each verified the same way (GPU output vs ours) and skipped when captured.
2. Interpolation between frames (match by caller + a6 + occurrence; lerp in_buf positions / angles and the camera),
   presenting at display rate; run-ahead 2 by default.
3. Compose order: once the solids are ours the emulated layer can go on top with black see-through (text over objects).
1. Read llama vect3d fully (transform_3d tail: perspective, z clip, intensity, the line draw and colour), port it,
   and draw the web + claw + vector enemies natively on top of the untouched emulated picture (no skipping yet)
   to check alignment at 1x and 4x.
2. Then skip those calls, compose, and move on to horse polyo2d (solid enemies), donky web3d, starfield, pixels.
