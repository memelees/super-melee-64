#!/usr/bin/env python3
"""Exercise the actual authored-round/preflight code against a tiny C world.

The production functions are extracted verbatim. World queries and the native
fighter lifecycle are stubbed so failures can prove the old actors survive a
partially successful placement search. Full SM64/native playtests are separate.
"""
from pathlib import Path
import json
import hashlib
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent

PREFIX = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "bridge_protocol.h"
#define SMB_COOP_CAMPAIGN 1
enum {LEVEL_CASTLE_GROUNDS=16,LEVEL_CASTLE=6,COURSE_CAKE_END=25,SURFACE_DEATH_PLANE=10};
typedef float Vec3f[3];
struct Surface {struct {float x,y,z;} normal;int type,room;};
struct WallCollisionData {float x,y,z,offsetY,radius;};
struct MarioState {float pos[3];int16_t faceAngle[3];};
static struct MarioState player={{100,200,300},{0,0,0}},requester={{1200,200,300},{0,0,0}};
static struct MarioState* gMarioState=&player;
static void* gCurrentArea=&player;
static int gCurrLevelNum=9,gCurrCourseNum=1,current_act=1,active=1;
static int encounter_course=-1,encounter_level=-1,encounter_act=-1;
static int manual_course=-1,manual_level=-1,manual_act=-1,npc_room;
static unsigned frames=200,encounter_count,encounter_defeated,encounter_next_wave,encounter_wave,encounter_waves,encounter_key,encounter_round_serial,manual_npcs;
static uint32_t encounter_sizes[3],encounter_roster[3][3],encounter_costumes[3][3],encounter_levels[3][3];
static unsigned npc_ready[3],npc_pending[3],npc_launched[3],npc_deferred[3],npc_sampled[3],npc_transition_ko[3],npc_due[3],npc_anchor_ready[3],npc_last_reset[3],npc_ko_serial[3],npc_ko_star_mask;
static float npc_ko_position[3][3];
static unsigned native_creates,native_places,air_resets[3],commits,requests,authority=1,ledger_serial,ledger_wave;
static unsigned active_items=99,live_roster[3],live_costumes[3],live_levels[3];
static float live_positions[3][3],air_hurt[3];
static int no_floor,obstructed,one_point_only;
static char difficulty[16]="none";
static struct Surface flat={.normal={0,1,0},.room=1};
static float sins(int16_t a){return sinf(a*(6.28318530718f/65536.f));}
static float coss(int16_t a){return cosf(a*(6.28318530718f/65536.f));}
static int smb_npc_act(void){return current_act;}
static int smb_impossible(void){return !strcmp(difficulty,"impossible");}
static int smb_course_enemies(void){return 1;}
static int smb_npc_authoritative(void){return authority;}
static int SDL_setenv(const char* k,const char* v,int o){(void)k;(void)v;(void)o;return 0;}
static void smb_npc_water_reset(unsigned seat){air_resets[seat-1]++;air_hurt[seat-1]=0;}
static void smb_host_npc_queue(unsigned why){for(unsigned i=0;i<encounter_count;i++)npc_pending[i]=why;}
static void smb_coop_campaign_round_base(unsigned* serial,unsigned* wave){if(ledger_serial>*serial){*serial=ledger_serial;*wave=ledger_wave;}}
static void smb_coop_campaign_spawned(void){commits++;}
static int smb_coop_campaign_request_round(void){requests++;return 0;}
static int native_encounter(unsigned count,const SmbEncounter* plan){
    native_creates++;active_items=0;
    memset(live_roster,0,sizeof live_roster);memset(live_costumes,0,sizeof live_costumes);memset(live_positions,0,sizeof live_positions);
    memcpy(live_roster,plan->character,count*sizeof *live_roster);memcpy(live_costumes,plan->costume,count*sizeof *live_costumes);return 0;
}
static int native_place(unsigned seat,const float* p,SmbNpc* out){
    assert(seat>=1&&seat<=encounter_count&&p);native_places++;memcpy(live_positions[seat-1],p,12);memset(out,0,sizeof *out);return 0;
}
static int native_configure(unsigned seat,unsigned level,float attack){(void)attack;live_levels[seat-1]=level;return 0;}
static int (*embedded_encounter)(unsigned,const SmbEncounter*)=native_encounter;
static int (*embedded_npc)(unsigned,const float*,SmbNpc*)=native_place;
static int (*embedded_npc_configure)(unsigned,unsigned,float)=native_configure;
static float water_surface=-11000;
static float find_water_level(float x,float z){(void)x;(void)z;return water_surface;}
static float find_floor(float x,float y,float z,struct Surface** out){(void)x;(void)y;(void)z;*out=no_floor?NULL:&flat;return 200;}
static float find_ceil(float x,float y,float z,struct Surface** out){(void)x;(void)y;(void)z;*out=NULL;return 10000;}
static void find_wall_collisions(struct WallCollisionData* w){if(one_point_only&&(fabsf(w->x-player.pos[0])>1||fabsf(w->z-(player.pos[2]+500))>1))w->x+=10;}
static void find_surface_on_ray(Vec3f origin,Vec3f dir,struct Surface** hit,Vec3f p){(void)origin;(void)dir;(void)p;*hit=obstructed?&flat:NULL;}
'''

TEST = r'''
int main(void) {
    /* None mode is deliberately overridden, using default authored identities
     * and levels. Four presses exercise two complete BOB cycles. */
    for(unsigned press=1;press<=4;press++) {
        unsigned wave=(press-1)%2;
        air_hurt[0]=air_hurt[1]=air_hurt[2]=99;
        npc_ko_star_mask=7;encounter_next_wave=123;
        assert(!smb_host_spawn_round());
        assert(encounter_round_serial==press&&encounter_wave==wave&&encounter_count==1);
        assert(manual_npcs&&manual_course==1&&manual_level==9&&manual_act==1);
        assert(encounter_level==9&&encounter_act==1&&encounter_course==1);
        assert(!encounter_next_wave&&!encounter_defeated&&!npc_ko_star_mask&&!active_items);
        assert(live_roster[0]==(wave?35u:1u)&&live_levels[0]==(wave?4u:5u));
        assert(npc_ready[0]&&!npc_pending[0]&&npc_last_reset[0]==frames);
        float distance=hypotf(live_positions[0][0]*10-player.pos[0],live_positions[0][2]*10-player.pos[2]);
        assert(distance>=349&&distance<=701&&live_positions[0][2]*10>player.pos[2]);
        for(unsigned i=0;i<3;i++)assert(npc_ko_serial[i]==press&&air_resets[i]==press&&!air_hurt[i]);
        assert(commits==press&&native_creates==press);
    }
    /* New life serials revise empty seats too. A stale local view follows the
     * newer stage ledger, then advances its authored wave rather than wave0. */
    ledger_serial=100;ledger_wave=1;
    assert(!smb_host_spawn_round()&&encounter_round_serial==101&&encounter_wave==0);
    assert(npc_ko_serial[0]==5&&npc_ko_serial[1]==5&&npc_ko_serial[2]==5);
    ledger_serial=0;
    /* Failed floor/LOS preflight leaves every existing native actor, item,
     * damage queue, serial and pending KO exactly as they were. */
    unsigned before=native_creates,serial=encounter_round_serial,lives=npc_ko_serial[0],old_air=air_resets[0];
    active_items=17;air_hurt[0]=73.5f;no_floor=1;
    assert(smb_host_spawn_round()==-2);
    assert(native_creates==before&&encounter_round_serial==serial&&npc_ko_serial[0]==lives);
    assert(active_items==17&&air_hurt[0]==73.5f&&air_resets[0]==old_air);
    no_floor=0;obstructed=1;assert(smb_host_spawn_round()==-2&&native_creates==before);obstructed=0;
    /* A three-actor round with room for only its first fighter must not
     * partially replace the old one. Then permit a real separated lineup. */
    strcpy(difficulty,"impossible");gCurrCourseNum=0;gCurrLevelNum=LEVEL_CASTLE_GROUNDS;
    one_point_only=1;assert(smb_host_spawn_round()==-2&&native_creates==before);one_point_only=0;
    assert(!smb_host_spawn_round());
    assert(encounter_round_serial==1&&encounter_wave==0&&encounter_count==3);
    assert(live_roster[0]==17&&live_roster[1]==12&&live_roster[2]==5);
    for(unsigned i=0;i<3;i++) {
        assert(npc_ready[i]&&live_levels[i]==9);
        for(unsigned j=0;j<i;j++)assert(hypotf(live_positions[i][0]-live_positions[j][0],live_positions[i][2]-live_positions[j][2])>=18);
    }
    /* A remote request must use that requester's position, not the host's. */
    assert(!smb_host_spawn_round_at(&requester));
    assert(live_positions[0][0]*10==requester.pos[0]);
    assert(hypotf(live_positions[0][0]*10-player.pos[0],live_positions[0][2]*10-player.pos[2])>700);
    /* A new act starts the authored cycle at its first round. Default mode
     * restores default CPU levels and the stage's authored costume variant. */
    strcpy(difficulty,"none");gCurrCourseNum=10;gCurrLevelNum=10;current_act=2;
    assert(!smb_host_spawn_round());assert(encounter_round_serial==1&&encounter_wave==0);
    assert(live_roster[0]==13&&live_costumes[0]==1&&live_levels[0]==7);
    /* Standing on the real seabed preserves the player's swimming depth,
     * with 30 world units of foot clearance instead of snapping to floor+5. */
    float dry_y=player.pos[1];player.pos[1]=202;water_surface=1000;
    assert(!smb_host_spawn_round());
    for(unsigned i=0;i<encounter_count;i++) {
        assert(fabsf(live_positions[i][1]-23.2f)<.0001f);
        assert(water_surface-live_positions[i][1]*10>300);
    }
    unsigned wet_creates=native_creates,wet_places=native_places,wet_serial=encounter_round_serial,wet_lives=npc_ko_serial[0];
    float wet_positions[3][3];memcpy(wet_positions,live_positions,sizeof wet_positions);
    obstructed=1;assert(smb_host_spawn_round()==-2);obstructed=0;
    assert(native_creates==wet_creates&&native_places==wet_places&&encounter_round_serial==wet_serial&&npc_ko_serial[0]==wet_lives);
    assert(!memcmp(wet_positions,live_positions,sizeof wet_positions));
    water_surface=-11000;player.pos[1]=dry_y;
    /* A nonowner queues one request and never mutates local fighters. */
    authority=0;before=native_creates;assert(!smb_host_spawn_round()&&requests==1&&native_creates==before);authority=1;
    encounter_round_serial=UINT32_MAX;assert(smb_host_spawn_round()==-4&&native_creates==before);
    encounter_round_serial=1;npc_ko_serial[2]=UINT32_MAX;assert(smb_host_spawn_round()==-4&&native_creates==before);
    puts("PASS: actual authored round code cycles on four presses, honors costumes/default/impossible levels, overrides none, resets air/items/all-seat lives, preflights floor/LOS/separation atomically, spawns near requester and above the submerged seabed, retains blocked underwater rounds, resets by act, routes nonowner requests and rejects exhausted serials");
    return 0;
}
'''

INPUT_PREFIX = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define SMB_COOP_CAMPAIGN 1
typedef int16_t s16;
enum {SDL_SCANCODE_F6=63,SDL_SCANCODE_F7,SDL_SCANCODE_F8,SDL_SCANCODE_F9,SDL_SCANCODE_F10,SDL_SCANCODE_F11};
static unsigned char host_keys[512],physical_keys[512];
static int embedded_step=1,active=1,character_menu_open,character_input_latch,round_input_pending,character_menu_requested;
static float controller_look[2];static int controller_vertical;static unsigned controller_buttons;
static s16 sCurrPlayMode,gMenuMode=-1;static int dialog=-1,panel;
static int get_dialog_id(void){return dialog;}
static int djui_panel_is_active(void){return panel;}
static const unsigned char* SDL_GetKeyboardState(int* count){if(count)*count=512;return physical_keys;}
static void smb_host_option(int option,int value){(void)option;(void)value;}
static void smb_host_change_menu_request(void){character_menu_requested=1;}
static void smb_record_toggle(void){}
struct Controller {unsigned buttonDown,buttonPressed;};
static struct Controller first={99,99},third={99,99},*gPlayer1Controller=&first,*gPlayer3Controller=&third;
'''

INPUT_TEST = r'''
int main(void) {
    /* The picker owns its SDL event loop. Its F8 keydown never reaches the
     * normal host callback; closing the picker must sample the physical key. */
    physical_keys[SDL_SCANCODE_F8]=1;host_keys[SDL_SCANCODE_F8]=0;
    character_menu_open=1;round_input_pending=1;
    smb_host_change_menu_closed();
    assert(!character_menu_open&&!round_input_pending&&character_input_latch&&host_keys[SDL_SCANCODE_F8]);
    assert(!first.buttonDown&&!first.buttonPressed&&!third.buttonDown&&!third.buttonPressed);
    /* A neutral gameplay frame clears the general character-input latch.
     * Later SDL auto-repeat still must not become a new round key edge. */
    character_input_latch=0;assert(smb_round_controls_ready());
    for(unsigned i=0;i<20;i++)smb_host_key(SDL_SCANCODE_F8,1);
    assert(!round_input_pending);
    physical_keys[SDL_SCANCODE_F8]=0;smb_host_key(SDL_SCANCODE_F8,0);
    physical_keys[SDL_SCANCODE_F8]=1;smb_host_key(SDL_SCANCODE_F8,1);
    assert(round_input_pending);round_input_pending=0;
    smb_host_key(SDL_SCANCODE_F8,1);assert(!round_input_pending);
    /* Closing with no held shortcut still permits the next genuine press. */
    physical_keys[SDL_SCANCODE_F8]=0;smb_host_change_menu_closed();character_input_latch=0;
    assert(!host_keys[SDL_SCANCODE_F8]);
    physical_keys[SDL_SCANCODE_F8]=1;smb_host_key(SDL_SCANCODE_F8,1);assert(round_input_pending);
    round_input_pending=0;smb_host_key(SDL_SCANCODE_F8,0);
    /* Pause, dialogs and the Coop menu reject an edge without buffering a
     * surprise round on the first valid gameplay frame after dismissal. */
    sCurrPlayMode=2;smb_host_key(SDL_SCANCODE_F8,1);assert(!round_input_pending);
    sCurrPlayMode=0;smb_host_key(SDL_SCANCODE_F8,1);assert(!round_input_pending);smb_host_key(SDL_SCANCODE_F8,0);
    dialog=0;smb_host_key(SDL_SCANCODE_F8,1);assert(!round_input_pending);dialog=-1;smb_host_key(SDL_SCANCODE_F8,0);
    panel=1;smb_host_key(SDL_SCANCODE_F8,1);assert(!round_input_pending);panel=0;smb_host_key(SDL_SCANCODE_F8,0);
    smb_host_key(SDL_SCANCODE_F8,1);assert(round_input_pending);
    puts("PASS: verbatim picker-close/input functions suppress held F8 auto-repeat, require release/repress, preserve fresh presses and reject pause/dialog/Coop-menu buffering");return 0;
}
'''


def main():
    source = (HERE / "sm64_npcs.inc").read_text()
    wave = source[source.index("static void smb_encounter_wave(void)"):source.index("/* Authored identities")]
    plan = source[source.index("typedef struct {unsigned waves"):source.index("static void smb_encounter_enter")]
    # Extract all four real floor/water/spawn/path helpers before the round.
    placement = source[source.index("static int smb_npc_safe_floor("):source.index("static void smb_host_npcs(")]
    rounds = source[source.index("/* A press replaces the encounter"):]
    bridge = (HERE / "sm64_host_bridge.c").read_text()
    ready = bridge[bridge.index("static int smb_round_controls_ready(void)"):bridge.index("unsigned smb_host_keyboard_buttons(void);")]
    key = bridge[bridge.index("void smb_host_key(int code,int down)"):bridge.index("void smb_host_controller_look(")]
    close = bridge[bridge.index("void smb_host_change_menu_closed(void)"):bridge.index("int smb_host_change_character(")]
    input_source = INPUT_PREFIX + ready + key + close + INPUT_TEST
    with tempfile.TemporaryDirectory(prefix="sm64-round-test-") as folder:
        folder = Path(folder)
        test, binary = folder / "rounds.c", folder / "rounds"
        test.write_text(PREFIX + wave + plan + placement + rounds + TEST)
        subprocess.run(["clang", "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(HERE), str(test), "-lm", "-o", str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True, check=True)
        input_file, input_binary = folder / "input.c", folder / "input"
        input_file.write_text(input_source)
        subprocess.run(["clang", "-std=c11", "-Wall", "-Wextra", "-Werror", str(input_file), "-o", str(input_binary)], check=True)
        input_result = subprocess.run([str(input_binary)], capture_output=True, text=True, check=True)
    receipt = HERE.parent / "verification/rounds-rules.json"
    receipt.parent.mkdir(parents=True, exist_ok=True)
    receipt.write_text(json.dumps({"passed": True, "output": result.stdout.strip(), "trace": result.stderr.strip(), "input_regression": input_result.stdout.strip(), "input_source_sha256": hashlib.sha256(input_source.encode()).hexdigest(), "scope": "Verbatim production round/preflight/authored-plan functions with synthetic world queries and native lifecycle stubs; verbatim input/picker-close functions with a stub physical keyboard. Actual game/native input and network are tested separately."}, indent=2) + "\n")
    print(result.stdout.strip())
    print(input_result.stdout.strip())
    print(receipt)


if __name__ == "__main__":
    main()
