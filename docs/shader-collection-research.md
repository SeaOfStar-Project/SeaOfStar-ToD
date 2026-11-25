# Shader and pipeline collection beyond manual play: research report

Date: 2026-11-25. Scope: how to make the pre-baked shader cache complete (and the game stutter-free)
for SeaOfStar-ToD without relying on manual play. No code was changed, nothing was built, the game
was never started. Facts are marked **[fact]** (read from our code or data, or from a cited
source). Guesses are marked **[hypothesis]**.

## 1. How our pipeline works today and where permutations come from

### 1.1 What a recipe is [fact]

- A recipe is `GxRecipe { TevKey tev; VertexKey vertex; GxRenderState state; uint8 bounding_box; }`
  (`runtime/gx_backend.h:163-168`), 807 bytes, stored in `shaders.bin` (magic `WRSR`, v2,
  `gx_recipes.c:21-27`). The file is append-only; new recipes are written the first time a draw uses a
  (program, state) pair (`gx_recipes.c:156-161`, called from `gx_vulkan_pipeline.c:660-663`).
- `TevKey` (`gx.h:166-181`, 677 bytes): stage count, alpha test (4 bytes), constant alpha,
  wrap/projection flags, depth mode/format/copy, early depth, **fog type/orthographic/range/none**,
  logic help, 4 indirect sources, and up to **16 TevStage** (40 bytes each: color/alpha inputs, bias,
  scale, clamp, target, texture map and coordinate, raster, konst selects, 9 indirect fields, wrap).
  The runtime values (colours, konst, alpha reference values, fog colour, map sizes) are NOT in the
  key; they live in the `TevValues` uniform block (`gx.h:183-195`). So a shader is specialised on
  the TEV topology only. This is already a good key.
- `VertexKey` (`gx.h:215-221`, 116 bytes): which coordinates and channels are read/made, normals,
  emboss, light channels (`ChannelKey` x4), per-map `CoordinateKey` x8, lines/points expansion.
- `GxRenderState` (`gx_backend.h:38-43`, 13 bytes): blend on/subtract/factors (7 bytes), depth test
  on/func/write, colour write, alpha write, cull.
- Vulkan pipeline key = hash of the vertex stage text + hash of the fragment stage text +
  `GxRenderState` (`gx_vulkan_pipeline.c:636-641`). Vertex input layout is derived from the vertex
  stage's input mask, not keyed separately (`layout_made`, `:492-524`).

### 1.2 What is baked in and what is dynamic [fact]

- Baked into the pipeline (`build_pipeline`, `gx_vulkan_pipeline.c:551-600`): cull, depth test
  enable/func/write, blend enable/equation/factors, colour write mask, vertex layout, render pass
  (`:594`).
- Dynamic: **viewport and scissor only** (`:589-590`; same in the second builder, `:804-812`).
- Consequence: every distinct `GxRenderState` multiplies every program. Pipelines = (programs x states
  seen with them). A new blend or depth setting on a known shader is a miss exactly like a new TEV
  setup.

### 1.3 Compile path is synchronous on a miss [fact]

- `gx_shader_use` -> `program_for` -> `build_program` -> `fragment_shader()/vertex_shader()` ->
  `gx_backend->stage_compile` (shaderc on the game thread, `gx_shader.c:91-136, 194-218`), then
  `vk_pipeline_find_new` -> `build_pipeline` -> `vkCreateGraphicsPipelines`
  (`gx_vulkan_pipeline.c:644-648, 597`). The code itself says so: "A new pipeline holds up the draw as
  a new shader does" (`:649`).
- No ubershader, no fallback shader, no async path exists on the draw path. `08-progress.md` lists
  "Async shaders: deferred until a measurement shows shader compiles cause stalls (current evidence:
  they do not)".
- Mitigations already present: SPIR-V cache by text hash (`stages.bin`, `:118`), driver
  `VkPipelineCache` persisted by a saver thread (`:78, 208-231, 351`), a pipelines list
  (`pipelines.bin`), and **multi-threaded start-up prebuild** of all recipes
  (`build_many`, up to 8 workers, `:692-724`; parallel shaderc `:435-465`;
  `gx_shader_prepare`, `gx_shader.c:153-192`). Boot with a warm cache: "341 ready in 19 ms".
  Cold-cache boot time is not recorded in the docs. Cache files: `stages.bin`, `pipelines.bin`, `driver.bin`
  (`gx_vulkan_pipeline.c:112-128`).
- `gx_stats` already counts `new_shaders`, `new_pipelines`, `slowest_shader_ms`; `WII_RECOMP_ACTIVITY=1`
  prints them. Measured: scripted and launcher runs with the 341-recipe image show **0 new shaders and
  0 new pipelines** and the hitches there are GPU load at scale 2 (`08-progress.md`). So the problem
  to solve is first-time misses for content not yet visited, not the steady state. **The cost of one
  miss in play is unmeasured:** the 423 activity lines in the five logs that have them (playtests and
  launcher logs) contain no miss at all (max "slowest" 0.1 ms, a prepared-at-boot figure). The
  Vulkan target is **1.1** (`gx_vulkan_device.c:64`; dual-source blend is enabled per device,
  `:185`), so EDS1/2 core promotion (1.3) does not apply without a version or extension gate.

### 1.4 What the recipe data says (analysis of the live `shaders.bin`) [fact]

File: `work/wii-recomp/recomp/build/title-data/00010000-534f3345/shaders.bin`, 373 records (the
image holds 341; the live file has grown since). Parsed read-only with a throw-away script (offsets
derived from the struct definitions; 677 + 116 + 13 + 1 = 807 checks out).

| Measure | Value |
|---|---|
| Recipes | 373 |
| Distinct TevKeys / VertexKeys / render states | 129 / 35 / 50 |
| Distinct programs (tev + vertex + bounding box) | 205 |
| Recipes per program | 114 programs have 1 recipe; max 8; mean 1.82 |
| Stage count of TevKeys (recipes) | 1:54, 2:52, 3:114, 4:118, 5:35; **never above 5** |
| Distinct blend+write-mask combos / depth+cull combos | 11 / 13 |
| Distinct states ignoring cull | 30 |
| TevKeys by fog type / alpha test | fog type {0:73, 2:22, 4:34}; alpha test on in 95 of 129 |

Discovery order (file order, which is first-use order unless a merge reordered it; counted at
40-record steps):

- records 40 -> 373: TevKeys 25 -> 129 (still growing, +16 in the last 53), VertexKeys 14 -> 35
  (nearly flat), programs 28 -> 205, render states 20 -> 50 (flat at 49-50 over the last 80).
- Of 373 recipes: 129 introduced a new TevKey, 13 a new VertexKey on a known TevKey, 63 a new
  combination of a known TevKey and VertexKey, and **168 (45%) were only a new render state on an
  already-built program**.
- Collapsing fog (4 bytes) and alpha test (4 bytes) out of the TevKey: 129 TevKeys fall to **68
  templates**; 35 templates were seen with only one fog/alpha variant. The full cross product of the
  observed fog tuples (3) and alpha tuples (2) would be 408 TevKeys.

Reading of this data:

- **[fact]** The render-state dimension has essentially saturated (50 states) while the TevKey
  dimension has not. 45% of all discovered recipes cost a pipeline build only because state is baked
  into the pipeline.
- **[hypothesis]** Fog differs per map and alpha test differs per material, so entering an unvisited
  map will produce fog/alpha siblings of templates we already have. The fan-out in section 3 (M7)
  targets exactly that (with the caveat given there).
- **[hypothesis]** Manual play found 183 recipes in 3.5 minutes, but a scripted 12-slot tour found 1
  (`08-progress.md`, "Recipe collection"). Coverage follows content visited (maps, seasons, enemies,
  spells, cutscenes), not time played.

### 1.5 What the engine looks like [fact]

- Game data is one 720 MB archive `RFF2.dat` (magic `HXTB0001`) with a 28 KB index `RFF2.bin`
  (`NLCM`). No J3D/BRRES content (the `.arc` files are Nintendo's HomeButton menus, the 34 `.cpk`
  are CRI audio). `main.dol` has a resource tag table (`HXAA, HXMB, HXTB, HXM3, HXVB, ...`) and
  **Scaleform GFx** strings, so the UI is a Flash runtime. Strings "Debug HUD/Memory/Tracker" and
  `MapMng.cpp`, `MakeMapAttr`, `MapPlaceData` exist; no warp or shader strings (string scan only).
- No public documentation of the HX* format was found; whether it holds TEV setups is unknown.

## 2. Catalogue of methods

Effort: S = days, M = 1-3 weeks, L = more. "Coverage" is the share of misses the method removes, not
of recipes it finds.

### M1. Ubershader (exclusive or hybrid), Dolphin style

- **How it works:** one big shader per stage that interprets the TEV configuration from a uniform
  block, so a new TEV setup needs no compile. Dolphin: "Disabled", "Hybrid" (draw with the
  ubershader while the specialised shader compiles in the background, then swap) and "Exclusive"
  (only ubershaders). Pipelines for ubershaders are queued at start-up (`PrecompileUberShaders`),
  specialised ones are compiled by `ShaderCompilerThreads` workers.
- **Who:** Dolphin (2017, PR #5702 "Ubershaders 2.0": vertex ubershaders, OpenGL/D3D/Vulkan,
  Hybrid/Exclusive, "per-pixel lighting disables ubershaders", D3D11 gave the best stutter
  reduction; performance cost significant). RT64's README: "Uses ubershaders to guarantee no
  stutters due to pipeline compilation" (the README does not describe how; an HN comment describes
  the fallback model, not verified in source). Dolphin's performance guide says ubershaders "come at the cost of being much slower than
  specialized shaders"; the 2017 blog says Hybrid mode still stutters in some setups (both from
  search summaries; the pages returned 403 to the fetch tool). RPCS3 has an asynchronous shader
  mode that skips draws until ready and an optional shader interpreter fallback (RPCS3 blog
  2018-08-08 and PR #4876, search summaries only; not researched further).
- **Applicability:** high in principle, because our key is already a data description of the TEV
  (up to 16 stages x ~40 bytes, max 5 seen) and the shader text generator (`gx_tev.c`) knows every
  feature. An ubershader means a second code generator that reads stage descriptors from a UBO and
  loops with `switch` on the inputs. Pipelines still bake render state (section 1.2), so an
  ubershader only removes the shader compile, not the state multiplication (cheap: 50 states x a
  handful of vertex layouts can be prebuilt at boot).
- **Effort:** L. **Expected coverage:** 100% of TEV misses once built (any key can be drawn), at
  lower GPU speed only on the miss frames in Hybrid mode. **Risks:** GPU cost (our test GPU is
  already marginal at scale 2: `08-progress.md`), bit-exactness drift between the two generators
  (a visible pop at swap), indirect/bump/fog paths are the hardest to interpret, the 64 KB text
  limit does not apply but driver compile time of the ubershader at boot does.

### M2. Async background compile with a safety fallback (no ubershader)

- **How:** on a miss, queue shaderc + `vkCreateGraphicsPipelines` on a worker, and on the game
  thread either skip the draw until ready ("skip" mode), or draw with the nearest ready pipeline of
  the same vertex layout. `build_many` and the compile workers already do the heavy part off-thread
  at boot.
- **Who:** Xenia Canary/Edge and Yuzu-era emulators list "asynchronous compilation" (reduces stutter
  "but can cause brief visual glitches": Emulation General Wiki, shader caches page, via search
  summary). Dolphin's async path (`GetPipelineForUidAsync`) returns "nothing" while pending and the
  caller falls back to the ubershader (ShaderCache.cpp, fetched).
- **Applicability:** high; the plumbing exists. Skip mode produces missing geometry for a few frames,
  which conflicts with "never a fake mode" only if hidden; it must be a logged, user-visible safety
  net (log line per skipped draw, counter in the summary card).
- **Effort:** M. **Coverage:** removes the hitch, not the glitch. **Risks:** the GX framebuffer copy
  chain (EFB copies read what was drawn): a skipped draw can corrupt an EFB copy that the game then
  uses as a texture for many frames; needs care around `copy_color`/`copy_depth` sequence.

### M3. Reduce permutations with dynamic state (and M4, pipeline libraries)

- **How:** M3a: cull and depth test/write/func dynamic (VK_EXT_extended_dynamic_state, core in 1.3).
  M3b: blend enable/equation/factors and colour/alpha write mask dynamic (VK_EXT_extended_dynamic_state3,
  not core). Then the 13-byte state leaves the key.
- **M4 (VK_EXT_graphics_pipeline_library):** build vertex-pre-raster, fragment and fragment-output
  libraries separately and link at draw time. Khronos blog (fetched): shaders compile "at material
  load time, and only a final link happens at draw time"; link unoptimised, optimise in the
  background; `graphicsPipelineLibraryFastLinking` is true on desktop NVIDIA/AMD/Intel; it also
  needs EDS1/2 and dynamic rendering. Emulator PRs report 0.008-0.3 ms links (self-reported, Lichii
  #1177/#411). Beats M3b only for blend state EDS3 cannot cover; effort L.
- **Who:** Khronos shader-object blog: dynamic state exists to "avoid the explosive pipeline
  permutation combinatorics" (fetched). The EDS3 reference page returned 403, so "not core" is spec
  knowledge, not re-verified.
- **Measured on our recipes [fact]:** 373 pipelines today. M3a alone: **247** (distinct program x
  blend x colour/alpha mask; 11 blend+mask combinations, 13 depth+cull combinations). M3b on top:
  **205** (= programs). So M3a removes 34%, M3b 45%, of recipes from what must be built.
- **Applicability:** high. Dual-source factors (`blend_factor()`, `gx_vulkan_pipeline.c:537-549`)
  must be checked against EDS3 per driver. The fork also targets Mac (MoltenVK) and Pi 5, so the baked
  path must remain as a fallback.
- **Effort:** M3a: M (needs an extension gate or a 1.3 requirement, since the device is created as
  1.1); M3b: M-L. **Coverage:** removes the state-miss class where supported. **Risks:** driver
  quirks, per-draw CPU cost; support on the development GPU was **not checked** (`vulkaninfo`).

### M5/M6. VK_EXT_shader_object and specialisation constants

- **Shader objects:** one `VkShaderEXT` per stage, fully dynamic state, no pipelines or render
  pass. Khronos blog (fetched): binaries can be retrieved and reused; draw CPU cost may not exceed
  150% of static pipelines; a layer emulates it. It would replace the whole pipeline/render-pass layer
  (`gx_vulkan*.c`, ~4,000 lines) and the Mac/Pi paths. Effort L. Low priority; revisit only if M3 is
  not enough.
- **Specialisation constants:** TEV fields as spec constants in one SPIR-V per stage. shaderc is
  already cached by text hash (`stages.bin`) and the driver still compiles each specialisation, so a
  miss still hitches. Effort M. Not recommended.

### M7. Hole-filling fan-out at prebake time (speculative recipes)

- **How:** in the launcher rebuild step, add likely siblings to every recipe: (a) state fan-out: with
  M3a only, every program x the 11 blend+mask combos = **2,255** pipelines (205 x 11, hypothesis
  that all combos are valid on all programs); with M3b there is no state left to fan out; without
  M3 a program x 30-50 states is too many (6,000-10,000). (b) TevKey fan-out over fog x alpha-test
  (408 vs 129 TevKeys, section 1.4), built through the existing `build_many`.
- **Who:** no direct precedent found; same idea as Dolphin's `PrecompileUberShaders` (build a closed
  space at start-up), restricted to an observed space.
- **Caveat on (b) [hypothesis]:** the caches use `memcmp` on the key, so a synthesised TevKey helps only
  if it matches byte-for-byte what `gx_tev_read` would produce for the real draw (fog and alpha
  fields may also imply other bytes, such as `wrap_output`, `constant_alpha`, `fog_none`). The 33
  sibling groups are supporting evidence only; read the fog and alpha parts of `gx_tev.c` before
  building it, then verify by checking that synthesised keys later appear in real recordings.
- **Effort:** S-M. **Coverage:** state siblings 100%, fog/alpha siblings only for observed
  templates, nothing for brand-new templates. **Risks:** cold-start time and driver-cache size, so
  keep it an optional tool step, not part of `gx_recipes.c`.

### M8. Scripted coverage: warp/sweep bot over native guest code

- **How:** the game is native, and the control pipe (`control_pipe.c:43-55`) has `stick`, `press`, `shot`,
  `record`, `peek` (read-only memory print), but **no memory write or guest-call verb**. Add `poke`
  and a guest-function call, find the map-change/time/season/weather functions with REA (`MapMng.cpp`,
  `MakeMapAttr`, `MapPlaceData` are leads), then sweep every map x season x time x weather, plus
  battle, menu and cutscene triggers.
- **Who:** memory poking is standard in emulators (Action Replay/Gecko codes, Dolphin TAS input).
  No shipped recomp documenting it for shader coverage was found.
- **Applicability:** high for map/season/weather/fog axes, medium for rare effects (spells, bosses,
  cutscenes) that need game state. Poking a map id without loading assets may crash.
- **Effort:** M-L (the RE of the map loader is the long pole; the pipe verbs are small).
  **Coverage:** [hypothesis] most world content, not rare events. **Risks:** dev tool only, run by the
  main session through `playtest.ps1`, one at a time; boots rewrite saves.

### M9. Replay corpora: scene recordings and Dolphin FIFO logs

- **Ours [fact]:** `record <frames> <file>` saves a scene and `tools/replay` plays it back with no game
  running, linking the real `gx_shader`, `gx_tev`, `gx_vulkan_pipeline` (`tools/replay/Makefile:7`).
  **But `tools/replay/stubs.c:24` stubs `gx_recipe_note` to a no-op**, so replay does not write
  recipes today. [hypothesis] Replacing that stub would let a corpus of recordings re-derive recipes
  after a key redesign (`RECIPE_VERSION`, `gx_recipes.c:21-24`) without replaying the game; cost S.
- **Dolphin:** FifoPlayer `.dff` logs (command stream plus memory updates) play back in batch
  (Dolphin wiki "FIFO Player Overview", via search summary). An importer for our decoder is M-L and
  the logs still need play in Dolphin. Not recommended.
- Recordings hold game data and stay outside the repo.

### M10. Recipes from a Dolphin run (shader UID cache mapping)

- Dolphin writes a hardware-independent per-game `<gameid>.uidcache` (pipeline UIDs: vertex format,
  VS/PS UIDs, raster/depth/blend state; ShaderCache.cpp, fetched). But a UID is Dolphin's own packed
  bitfield of GX registers, so mapping it to our keys means reversing our register-to-key derivation
  and tying to a Dolphin version; a patched Dolphin that dumps raw BP/XF registers per unique draw
  is more robust. Both still need someone to play in Dolphin. Effort L. Not recommended.

### M11. Static enumeration of GX state from the DOL or the data files

- **How:** find `GXSetTev*`, `GXSetBlendMode`... call sites (the SDK is linked into `main.dol`:
  "<< RVL_SDK - GX" is present) and read constant arguments; or parse material records from the disc.
- **Applicability:** low for the DOL. A draw's key is the composition of dozens of calls over
  different frames and objects, many with data-driven arguments (materials loaded from `RFF2.dat`),
  so call-site constants give a partial or wrong superset. Hand-written paths (Scaleform UI, 2D
  overlays) are the best case and are small. Parsing the disc: blocked by the unknown HX* format
  (section 1.5); J3D/BMD parsers do not apply. **Effort:** L-XL (format RE plus a material
  interpreter). Unleashed Recompiled does this for a known engine: pipelines are "compiled as part
  of the asset loading process" by walking the game's render structures, with a boot-time list for
  post-processing and 2D that cannot be detected ahead (UnleashedRecomp README, fetched). That works
  because Xbox 360 shaders ship as files with materials; our GPU state has no such shipped artefact.
- **[hypothesis]** If the HX* model/material records turn out to carry TEV configs in plain form, a
  parser plus the existing `gx_tev_read` logic could enumerate them. Worth a bounded REA
  investigation (S) before dismissing.

### M12. Crowd-sourced recipe merge and shipping a list

- Already supported: `drive_shaders` merges the image's recipes with the local file
  (`gx_recipes.c:143-148`). Cemu shares "transferable" caches that record Wii U GPU state at
  pipeline creation and replay it on load, "independent of hardware and drivers" (Cemu 1.25.0
  changelog, fetched); Yuzu/Ryujinx shared transferable pipeline caches; Dolphin's UID cache is
  portable, its pipeline blobs are not. Communities often ban requests for caches because the legal
  status is questionable (Emulation General Wiki, via search summary).
- Our recipes hold GX state flags only (portable, no code, text or assets), which fits ADR-008, but
  `08-progress.md` still lists "Recipe list in the repo: to check against ADR-008". Not legal advice.
  User upload is telemetry: consent, privacy review, a server. Not now. Effort: S (ship our own
  list), L (collection service).

### M13. Coverage measurement (Good-Turing style saturation estimate)

- **How:** record per recipe its first-use frame and number of draws; estimate the probability that the next
  distinct draw is unseen as (recipes seen once) / (total draws), as in standard species-richness
  estimation. A route is "done" when this estimate and the new-recipe rate over the last N minutes
  are both near zero.
- **Applicability:** answers "is it complete" with evidence instead of a feeling. **Effort:** S.
  **[hypothesis]** It will show that rare effects dominate the remaining mass.

### M14. Cold-cache UX

Driver caches are invalid after a driver update (Cemu wiki, search summary); our recipes stay the
portable source and the launcher should show progress for a cold prebuild. Effort S.

## 3. Ranked recommendation for this project

Ordering logic: measure first (nothing shows a miss costs a visible hitch yet), shrink the permutation
space (cheap, measured), automate the coverage, keep a logged safety net, and only then pay for an
ubershader.

### Short term (days)

1. **Measure the cost of one miss and the coverage state (S).** Main session, one run with
   `WII_RECOMP_NO_SHADER_CACHE=1` (all recipes become misses; read `slowest_shader_ms`, worst
   frame, per-frame cost with `WII_RECOMP_ACTIVITY=1`). Add M13: per-recipe first-use frame and draw
   counts, "new recipes in the last 10 minutes" and the Good-Turing estimate in the summary card.
   **M1 and M2 stay gated on this number:** if one miss costs under a frame, the safety net can
   be skipped.
2. **M3a (cull + depth dynamic) with an extension gate (M)**, and check EDS3 (`vulkaninfo`) for M3b.
   Evidence: 373 -> 247 -> 205 pipelines; the state axis is closed (50 states) and 45% of
   discovered recipes were state-only. Keep the baked path as fallback for Mac/Pi.
3. **M7 after M3a (S-M):** 205 programs x 11 blend+mask = 2,255 pipelines, optional launcher setting
   with the count shown. TevKey fog/alpha fan-out only after reading `gx_tev.c` (hypothesis).
4. **M12 limited to shipping our own `shaders.bin` in the packed image** (supported), after the
   ADR-008 check. No user upload.

### Medium term (weeks)

5. **M8 sweep bot (M-L).** Highest coverage gain without a human. First a bounded REA task: find the
   map-change function and season/time/weather state; add `poke`/call pipe verbs; sweep. Success:
   M13's new-recipe rate near zero across the sweep and a fresh manual session adds almost none.
6. **M2 async build with logged skip fallback (M),** only if item 1 shows a miss is visible. Off by
   default, counted in the summary, tested against EFB-copy corruption. Dolphin Hybrid is the
   quality reference, skip mode the cheap version.
7. **M9 replay-to-recipes (S):** swap the `gx_recipe_note` stub in `tools/replay` and keep a few
   recordings per area (outside the repo) to rebuild recipes after a key change.

### Long term (only if misses remain)

8. **M1 hybrid ubershader (L):** the only method that guarantees no TEV compile on the draw path.
   Second generator in `gx_tev.c` reading the `TevKey` as data; used only for misses because of the
   GPU budget. Max 5 stages seen suggests a bounded loop with M2 above it.
9. **M4 pipeline libraries** only if EDS3 is unavailable on target GPUs. **M11** only if a bounded RE
   spike shows plain TEV setups in the HX* data.

### Not recommended now

M5/M6 (shader objects, spec constants), M10 (Dolphin UID mapping), `.dff` import, user upload.

## 4. Open questions that would change the ranking

- Cold-cache prebuild time for 373 and for ~2,255 pipelines on this GPU (unmeasured).
- Does the development GPU driver expose EDS3 (blend enable/equation/write mask, dual-source)?
  (`vulkaninfo`, not run.)
- What share of in-play misses are fog/alpha siblings? (Needs M13 plus a sweep.)
- Whether the HX* archive holds plain material descriptions (RE spike).
- Whether `shaders.bin` file order is strictly first-use order after image merges (the discovery
  curve in 1.4 assumes it).

## 5. Sources

Fetched and read: Dolphin ShaderCache.cpp (github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/ShaderCache.cpp);
Dolphin PR #5702 (github.com/dolphin-emu/dolphin/pull/5702); RT64 README (github.com/rt64/rt64);
UnleashedRecomp README (github.com/hedge-dev/UnleashedRecomp); Khronos GPL blog
(khronos.org/blog/reducing-draw-time-hitching-with-vk-ext-graphics-pipeline-library); Khronos shader
object blog (khronos.org/blog/you-can-use-vulkan-without-pipelines-today); Cemu 1.25.0 changelog
(cemu.info/changelog/cemu_1_25_0.txt); Zelda64Recomp README (no shader details).

Search-result summaries only (page not read, treat as secondary): Dolphin blog
(dolphin-emu.org/blog/2017/07/30/ubershaders/, 403); Dolphin performance guide
(dolphin-emu.org/docs/guides/performance-guide/, 403); Dolphin FIFO Player wiki
(github.com/dolphin-emu/dolphin/wiki/FIFO-Player-Overview); Emulation General Wiki shader caches
(emulation.gametechwiki.com/index.php/Shader_caches); Cemu wiki setup guide (wiki.cemu.info, driver-update
rebuild remark); RPCS3 async blog (blog.rpcs3.net/2018/08/08/eliminating-stutter-with-asynchronous-shader-implementation/)
and PR #4876; Xenia issue #2028; HN thread on RT64 (news.ycombinator.com/item?id=43115899); Lichii PRs
#1177 and #411 (github.com/zortos293/Lichii); EDS3 reference (docs.vulkan.org, 403).

Local evidence: `docs/08-progress.md`, `docs/04-tooling.md`, the runtime files cited, `shaders.bin`
(373 records), `extracted/SO3EE9/files`, a string scan of `extracted/SO3EE9/sys/main.dol`, and the
activity lines in `work/playtests/*/*.log` and `logs`.

## 6. Measured result (2026-11-25)

Playtests `misscold-20261125-184058` and `misswarm-20261125-184328` (load a late save slot, about 40 s
of walking, same timeline) measured the cost of a miss. Cold (no shader cache, prebake skipped): 71
misses; new-program n=42, median 8.0 ms, p90 18.5, max 127.5, sum 539 ms; state-only n=29, median
0.0 ms, p90 6.0, max 10.8, sum 43 ms. Warm (prebaked, with this PC's local shader-cache files warm): 0 misses on the route. Fresh empty cache with recipes on: also 0 misses; the prebake built all 373 recipes in 212 ms at boot (one-off). The route stays inside the starting house/workshop, so outdoor coverage is untested. Frames over 50 ms were
167 cold, 154 warm, 180 fresh, so shader misses do not explain them (counts include boot/loading; the cause is open, the already-recorded scripted-route gap).

Effect on the recommendations: recommendation 2 (dynamic state) is deprioritised; for stutter on this PC it is not needed (recommendation, the user decides; state-only cost measured on one NVIDIA GPU only). State-only
pipelines cost about 0 ms on this PC, so it would cut pipeline counts without removing visible
stutter. Caveat: the NVIDIA driver disk cache could not be disabled per process, so cold pipeline
times are lower bounds and the state-only figure may reflect a warm driver cache. Next priorities: a map-sweep bot for
coverage of new programs outdoors (a new-program miss costs about 8 ms median, up to about 128 ms) and the unexplained scripted-route hitch gap. Details in `docs/08-progress.md`.

Map-sweep result (same day, M8 done): a cold warp sweep of 93 of the 94 base maps (`tools/map-sweep.py`; ID 186 faults, 4xx/5xx do not warp) saw 510 distinct recipes, rediscovered 312 of the 373 manual recipes (84%) and found 198 new ones, growing `shaders.bin` to 550. With a fresh shader-cache folder on maps 0-33 the prebake built 550 recipes in 251 ms and the route had 6 misses (2 new-program, 4 state-only). Hypothesis: the 61 unrediscovered recipes come from battles, effects, menus and events. Saturation is not yet shown (needs a full sweep under new season/time/weather/progress conditions that adds 0).
