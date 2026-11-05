# Adapting the integration to another game

Super Melee 64 embeds a native Melee fighter runtime inside an SM64 host. The
host keeps its levels, world objects, progression, camera and save system. The
embedded runtime keeps fighter moves, animations, hitboxes, damage, knockback,
articles, character effects and character sounds. A host-side adapter translates
between them.

You can reuse and modify the project's original integration under the license
described in [LICENSING.md](../LICENSING.md). This is working integration source,
not a drop-in SDK for arbitrary games. A different host needs new hooks into its
simulation, collision, interactions and renderer. Replacing Melee with another
fighter engine also requires a new donor adapter. Neither the project license
nor this guide grants rights to another game's code, assets or trademarks.

## Choose what to replace

For **Melee fighters in a different host**, retain the embedded runtime and
replace the SM64-facing adapter. Start with a host whose native source or
supported mod API lets you read collision, control a player proxy and insert a
rendering pass. Keep that host's normal game logic running.

For **another fighter game inside SM64**, retain the host-side design but
implement an equivalent donor boundary. Melee's `Fighter`, HSD/GX rendering,
motion numbers, articles and native byte-order handling do not transfer to
another engine. Its runtime must expose its own real simulation and rendering;
substituting character meshes alone will not reproduce its moves or physics.

For **another game embedded in another host**, first make those two boundaries
explicit. Treat gameplay ownership, coordinates and time as contracts before
adding character selection, encounters or multiplayer.

## Source map

All paths below are relative to this repository. The main integration lives in
`outputs/sm64-melee-lab/native/`.

| Read this source | To understand |
| --- | --- |
| [bridge_protocol.h](../outputs/sm64-melee-lab/native/bridge_protocol.h) | Fixed-width input, state, collision, rendering and network records |
| [melee_embedded.c](../outputs/sm64-melee-lab/native/melee_embedded.c) | Cooperative runtime lifetime, ticks, exported calls and native drawing |
| [sm64_host_bridge.c](../outputs/sm64-melee-lab/native/sm64_host_bridge.c) | The concrete host adapter, state ownership, scale and camera conversion |
| [prepare_sm64_host.py](../outputs/sm64-melee-lab/native/prepare_sm64_host.py) | Where the single-player host is patched to call the adapter |
| [prepare_coop_host.py](../outputs/sm64-melee-lab/native/prepare_coop_host.py) | Equivalent hooks in the co-op host |
| [melee_world.inc](../outputs/sm64-melee-lab/native/melee_world.inc), [melee_terrain.inc](../outputs/sm64-melee-lab/native/melee_terrain.inc) | Live host triangles, collision slices, stairs and swept terrain constraint |
| [sm64_combat.inc](../outputs/sm64-melee-lab/native/sm64_combat.inc), [melee_items.inc](../outputs/sm64-melee-lab/native/melee_items.inc) | Fighter/article attacks against host objects and article contact feedback |
| [melee_environment.inc](../outputs/sm64-melee-lab/native/melee_environment.inc), [melee_traversal.inc](../outputs/sm64-melee-lab/native/melee_traversal.inc) | Launches, respawn, sliding, water, shell and flight adaptation |
| [melee_gpu.cpp](../outputs/sm64-melee-lab/native/melee_gpu.cpp), [sm64_gl_composite.inc](../outputs/sm64-melee-lab/native/sm64_gl_composite.inc) | Offscreen native rendering, depth and effect compositing |
| [build_adventure.py](../outputs/sm64-melee-lab/native/build_adventure.py), [refresh_melee.py](../outputs/sm64-melee-lab/native/refresh_melee.py) | Donor source hooks, compiler boundary and library exports |

## Start with one process and one fighter

Normal releases load `libmelee_embedded.dylib` on macOS or
`melee_embedded.dll` on Windows into the host process. Calls cross a C ABI;
Melee's main loop yields back to the host on the same main thread. The macOS
implementation uses cooperative contexts and a host-reserved stack; Windows
uses fibers. Native graphics or audio backends may still own worker threads.

The older [melee_worker_bridge.c](../outputs/sm64-melee-lab/native/melee_worker_bridge.c)
and socket fallback are historical experiments. They are not the release's
normal architecture or a required second game process. Do not build a new
integration around screenshots from a second visible game window.

The current boundary is private to matching native builds. `SMB_MAGIC` checks
compatibility; it is not a security boundary. Records contain native-layout
floats and fixed-width fields, and are not a portable on-disk or network format.
Compile both sides from the same header, retain its limits, and update the ABI
identity, exports and tests together when changing its layout. Do not pass
Melee pointers, game-disc structs or C++ objects into the host.

These are actual entry points, rather than a proposed generic API. Refer to the
definitions and the function-pointer declarations in `sm64_host_bridge.c` for
the exact signatures and return values.

| Entry point | Responsibility |
| --- | --- |
| `smb_embed_init(abi, disc, stack, size)` | Initialize the donor from locally supplied data and reach its ready yield point; macOS requires a reserved stack, while Windows creates a fiber |
| `smb_embed_world(surfaces, count, revision)` | Publish the current host collision triangles in donor units |
| `smb_embed_step(command, result)` | Advance one native fighter tick and return the matching sequence's `SmbOutput` |
| `smb_embed_render(request, color, depth, transmission)` | Draw native fighters, articles and effects using the supplied camera, or the separate HUD pass |
| `smb_embed_shutdown()` | Shut down donor resources before host SDL/audio teardown |
| `smb_embed_switch(character, costume)` | Validate and queue a local fighter/costume change |
| `smb_embed_costumes(character)` | Query the character's costume count; zero means unsupported |
| `smb_embed_health(change, away_x, away_z)` | Apply local healing or a host-world damage reaction; positive values damage, ordinary negative values heal, while `-1000` is a reserved reset path |
| `smb_embed_contact(id)`, `smb_embed_impact(source, index, position)` | Feed host contact/impact results back to native articles |
| `smb_embed_actor_state`, `smb_embed_actor_environment`, `smb_embed_actor_world_hit`, `smb_embed_actor_world_ko`, `smb_embed_actor_damage` | Per-actor state, water/hazard handling and actual-damage accounting, including partners and CPUs |
| `smb_embed_npc_add`, `smb_embed_encounter`, `smb_embed_npc_configure`, `smb_embed_npc_ko` | Create and manage the local encounter's native CPU fighters |

Additional exports handle presentation-only remote fighters, articles, sounds,
grab ownership and encounter snapshots. They are separate concerns; do not
make the first single-player prototype depend on them. `smb_embed_camera` can
expose the donor camera, but the release normally uses the host's SM64 camera.

## Establish units, axes and time before tuning movement

The current SM64 mapping uses Y-up and keeps X/Z as the ground plane. Positions
and collision vertices convert with `donor = host * 0.1`; returning positions
uses `host = donor * 10`. This scale also affects character height, hit radii,
camera translation, water levels and interaction distances. Calibrate a known
character against a host doorway and platform before changing movement speeds.

Melee's 3D adapter still borrows a fighter-local two-dimensional lane for parts
of native physics. Lane direction and facing are not the same as camera yaw.
Use the existing lane/world conversion functions instead of writing world X/Z
directly into a local velocity. Test forward, back, up and down attacks at
several camera headings; a correct idle pose does not prove attack direction.

The current host simulation is 30 Hz; the donor simulation is 60 Hz. The host
calls `smb_embed_step` **twice per host frame**, increments the sequence for each
tick, and processes hits after each returned tick. This prevents a hitbox that
exists for only one donor tick from disappearing between host frames. Rendering
and simulation frequency are separate: do not change the number of simulation
ticks when the monitor refresh rate changes.

Velocity needs both conversions. A host displacement per 30 Hz frame becomes
donor displacement per 60 Hz tick by multiplying by `0.05`; the reverse uses
`20`. Applying the position factor alone makes cannon shots and platform carry
travel at the wrong speed. A host with a different update rate needs a fixed
step accumulator and a deliberate velocity conversion, rather than copying the
SM64 two-step loop unchanged.

Illustrative host-loop sketch — this is not compilable replacement code, and
the `host_*` helpers below do not exist in this repository:

```c
host_update_world_objects();
publish_stable_collision_in_donor_units();
sample_host_environment_for_each_actor();

SmbInput command = map_input_and_host_transitions();
for (unsigned tick = 0; tick < 2; ++tick) { /* SM64's 30 Hz host only */
    command.magic = SMB_MAGIC;
    command.sequence = ++sequence;
    require_success(smb_embed_step(&command, &state));
    validate_magic_sequence_counts_and_finite_state(&state);
    apply_native_hit_events_to_host_objects(&state);
    clear_one_shot_transition_flags(&command);
}

update_host_player_proxy_from_donor(&state);
host_process_triggers_pickups_and_progression();
render_host_and_depth_matched_native_passes();
```

Keep input edges consistent across the donor tick. Camera look and gameplay
must not consume each other's button edges. The existing adapter separates
camera input, combat modifiers and `SMB_SWIM_UP`/`SMB_SWIM_DOWN`; copying an N64
button bitmask directly into `SmbInput.buttons` is incorrect.

## Collision and moving worlds

Publish **collision geometry**, not the visual map. `SmbSurface` carries three
vertices, a normal, type and flags. The current host uses flag `1` for ignored
surfaces and flag `2` for death planes. Those values are adapter conventions;
translate another host's solid, trigger, one-way and lethal surfaces explicitly.
For example, an SM64 painting must remain a warp sensor instead of becoming an
impenetrable wall. Visual transparency does not determine collision behavior.

Keep triangle identities stable while platforms move or doors open. The SM64
adapter reserves stable slots for static surfaces and dynamic-object triangles,
and uses a revision to identify a new area/world generation. Repacking triangles
arbitrarily can change the identity of a ledge a fighter is currently holding.
Update moving geometry before fighter ticks; remove collision when its object
is unloaded. Carry platform translation and rotation exactly once, using the
host's real support object rather than guessing from the nearest visual mesh.

Native movement plus a final position snap is insufficient for thin walls or
high-speed knockback. The terrain guard sweeps between the previous and target
positions. Preserve that path constraint for launches, specials and CPUs too.
Include slopes, ceilings, stair risers, seams, platform edges and high-speed
movement in the first collision fixture. Ledge and wall probes expose the
published world through `smb_embed_ledge`, `smb_embed_wall`,
`smb_embed_support` and `smb_embed_stair` for diagnostics and host support.

## Give every transition a single owner

The invisible host player is an interaction proxy, not a second independently
moving player. Normal locomotion follows the donor result; the host still uses
that position for doors, coins, stars, enemies, hazards and save progression.
Disable or bypass conflicting host locomotion while native control is active.

Some host states deliberately retain host movement. Here, shells and wing-cap
flight use host traversal with native character poses. Cannons and enemy throws
instead transfer a one-shot position/velocity into native flight. Water has a
specific donor traversal policy while the host supplies water depth and its air
meter rules. See the environment and traversal sources before adding new flag
meanings; several existing transition bits are intentionally consumed once.

For each new host interaction, define entry, update, interruption and exit:

- **Respawn and area changes:** choose a clear landing position and headroom,
  reset stale projectiles/grabs, update the world revision, and synchronize both
  simulations' alive/dead state. Do not let the host die while the fighter lives.
- **Pickups and power-ups:** keep the host's object/save side effects, then map
  their fighter effect. Metal and vanish have native equivalents here. Other
  hosts need an explicit policy for unsupported abilities.
- **Grabs, carried props and boss throws:** choose which engine owns the
  attachment and when it releases. Feed the release velocity once instead of
  pinning a fighter to a scripted host trajectory indefinitely.
- **Hazards and healing:** route host attacks through native shielding, damage,
  weight, voice, hitstun and knockback. Preserve separate host necessities such
  as underwater air. Apply environment sampling to every actor and partner.
- **Victory and menus:** keep progression and menu ownership in the host,
  optionally request native celebration, then release it when gameplay resumes.

For attacks in the other direction, `SmbOutput` contains native attack/article
records. Translate their capsules, damage and impact semantics into the host's
object interactions. Keep stable producer IDs and rehit rules, so an arrow is
not a fresh hit every frame. Notify the donor when a host collision should
destroy, bounce or otherwise change an article. Native rendering without this
feedback produces visible projectiles that pass through the host world.

## Match the actual rendering contract

The release renders native fighters and effects offscreen at the host's shared
render-target dimensions. These derive from the drawable size, including Retina
pixels, with a default 1080-pixel height cap controlled by `SMB_RENDER_MAX_HEIGHT`.
It does not render the donor map.
The host supplies its view and projection through `SmbRender`; the adapter
converts matrix layout, translation scale, aspect and clip-depth convention.
Use the active render camera, including cutscenes, rather than approximating
its position and field of view with a second camera.

The GPU boundary returns **color, depth and RGB transmission**, not just an
image with a black background removed. Its composite is conceptually:

```text
final color = native color contribution + host color * native transmission
```

This preserves translucent, additive, subtractive and darkening effects. A
black color key loses dark character pixels and leaves boxes or opaque effects;
ordinary alpha blending alone does not represent all of these native effects.
The donor depth is reversed relative to this OpenGL host, so the current
compositor writes `1 - native_depth`. Another renderer must derive its own
correct conversion, including near/far planes and depth range.

Place native fighters against the host depth buffer so walls and platforms
occlude them. Write opaque native depth as appropriate so later host geometry
also respects fighters; keep translucent effect handling separate. Draw the
native damage HUD in a final overlay pass after world scenery, or surfaces can
erase its digits. Restore the host's graphics state and current context after
each donor pass. Include light registration, model-part visibility and host
ground shadows in visual checks, not only the main body mesh.

The current renderer transfers GPU results through readback buffers into host
OpenGL textures. It is one process but not a zero-copy graphics path. Profile
GPU synchronization and transfer cost before promising another host's frame
rate; cross-API texture sharing would be a separate implementation project.

## Audio, co-op and release boundaries

Keep host music and world sounds while retaining native character voices and
effects. The donor build hooks mute its music without muting looping character
effects that also use stream transport. `smb_embed_audio_sink` provides a PCM
callback for recording; it is not a replacement for normal sound playback.
Avoid emitting both engines' character hurt/grab voices for one event. Remote
sound records need bounded queues, sequence handling and explicit loop stops.

Add networking after the single-player transition tests pass. This project uses
Coop Deluxe transport and versioned fighter state alongside its normal host
player state, rather than sending native pointers or replaying a second full
world blindly. Remote presentation, combat, articles, sounds, grab ownership,
NPC encounter authority and authority handoff each have separate adapters.
See [sm64_coop.inc](../outputs/sm64-melee-lab/native/sm64_coop.inc),
[sm64_coop_transport.inc](../outputs/sm64-melee-lab/native/sm64_coop_transport.inc)
and [SECURITY.md](../SECURITY.md).

A new host needs its own authority model and validated serialization. Do not
send raw ABI structs over the internet or expose native file/mod exchange to
untrusted peers. The current fixed-content, bounded protocol is not proof of
anti-cheat or a substitute for reviewing a different game's networking.

Keep reproducible source input separate from locally generated game files.
[build_bootstrap.py](../outputs/sm64-melee-lab/native/build_bootstrap.py) verifies
the bundled source adapter; `refresh_melee.py` imports a completed donor build
into an isolated runtime; `build_adventure.py` applies the integration hooks and
relinks it. `prepare_sm64_host.py`/`build_host.py` and
`prepare_coop_host.py`/`build_coop.py` prepare the two host variants. These scripts
assume the pinned sources and local setup artifacts; they are not independent
one-command builders for a new game. Follow the install helper first, then
adapt the source acquisition, hook boundaries and build plans deliberately.

If changing the frozen donor adapter, update its bundle identity and member
hashes and rebuild against matching ABI headers. Retain source notices and
upstream pins. Consult [Bootstrap provenance](BOOTSTRAP-PROVENANCE.md) before
redistributing adapter changes. Publish original source, permitted patches and
installation tools; require users to provide the necessary game inputs locally.

## A practical porting sequence

1. **Empty test room:** one fighter, flat host floor, known scale, one camera,
   fixed simulation ticks, working shutdown. Verify standing, walking, jumping
   and attacks in every camera direction.
2. **Traversal room:** slopes, stairs, thin walls, ceiling, ledges and a moving
   platform. Exercise recovery specials and high-speed knockback through seams.
3. **Interaction room:** one host enemy, pickup, door, projectile target, hazard
   and launch. Verify hit deduplication, grab release and synchronized death.
4. **Real progression:** enter and leave an actual level, collect its objective,
   resume gameplay, save and reload. Test water, flight, riding and cutscenes
   according to the new host's rules.
5. **Roster and presentation:** unusual sizes, transformations, alternate model
   parts, costumes, partner fighters, translucent effects, shadows, sound and HUD.
6. **Networking:** two independent characters, remote attacks/projectiles/grabs,
   room changes, disconnect and NPC authority handoff; only then increase peers.
7. **Distribution:** isolated installation from verified user inputs, asset-free
   source audit, platform tests, documented limits and corresponding source.

Use the existing tests as patterns rather than assuming they validate a new
host. [test_terrain.c](../outputs/sm64-melee-lab/native/test_terrain.c),
[verify_npc_placement.py](../outputs/sm64-melee-lab/native/verify_npc_placement.py),
[verify_rounds.py](../outputs/sm64-melee-lab/native/verify_rounds.py) and
[verify_network_safety.py](../outputs/sm64-melee-lab/native/verify_network_safety.py)
exercise bounded contracts. [verify_beta.py](../outputs/sm64-melee-lab/native/verify_beta.py)
coordinates real-engine stage, roster, feature and co-op probes using isolated
profiles. [CONTRIBUTING.md](../CONTRIBUTING.md) lists asset-free checks and the
evidence expected for changes. Run a new host's equivalents serially against
the exact built engines, with timeouts and separate saves. A successful launch
or short stage probe does not establish that an entire game is completable.
