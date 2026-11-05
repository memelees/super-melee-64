#!/usr/bin/env python3
"""Run asset-free sanitizer checks against actual NPC placement helpers.

Only the helper functions are extracted from sm64_npcs.inc. Geometry queries
and source positions are deterministic fixtures; no engine, assets, game,
network, or prepared checkout is built or modified.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shlex
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]


def function(source, name):
    """Extract the complete production brace block without rewriting its C."""
    matches = list(re.finditer(r"^static int " + re.escape(name) + r"\([^;]*?\)\s*\{", source, re.M))
    if len(matches) != 1:
        raise ValueError(f"Expected one production helper {name}; found {len(matches)}")
    match = matches[0]
    depth = 1
    tokens = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', re.S)
    for token in tokens.finditer(source, match.end()):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if not depth:
                return source[match.start():token.end()], source[:match.start()].count("\n") + 1
    raise ValueError(f"Unclosed production helper {name}")


FIXTURES = r'''
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef float Vec3f[3];
struct Surface {int type,room;struct {float x,y,z;} normal;};
struct WallCollisionData {float x,y,z,offsetY,radius;};
static struct Surface ground,ceiling,obstacle,other_room;
static struct {
    float water,source_water,floor_y,ceiling_y,wall_height,last_ray_y;
    bool has_floor,has_ceiling,wall,obstruction,water_gap,shallow_middle,dry_edge,wrong_room_edge;
    unsigned blocked_ray;
    unsigned water_queries,floor_queries,ceiling_queries,wall_queries,ray_queries;
} geometry;
static const float candidate_x=500,candidate_z=300,source_y=-1000;
static const char* test_name;
static unsigned checks;
#define CHECK(condition) do {checks++;if(!(condition)) { \
    fprintf(stderr,"FAIL %s:%d: %s\n",test_name,__LINE__,#condition);exit(1); \
}} while(0)
static float find_water_level(float x,float z) {
    geometry.water_queries++;
    float t=(x*candidate_x+z*candidate_z)/(candidate_x*candidate_x+candidate_z*candidate_z);
    if(geometry.water_gap&&t>.2f&&t<.8f)return -11000;
    if(geometry.shallow_middle&&t>.2f&&t<.8f)return source_y+100;
    if(geometry.dry_edge&&x>candidate_x+30)return -11000;
    return fabsf(x)<1&&fabsf(z)<1?geometry.source_water:geometry.water;
}
static float find_floor(float x,float y,float z,struct Surface** surface) {
    (void)z;geometry.floor_queries++;
    *surface=geometry.has_floor&&(!isfinite(geometry.floor_y)||y>=geometry.floor_y)?&ground:NULL;
    if(*surface&&geometry.wrong_room_edge&&x>candidate_x+30)*surface=&other_room;
    return *surface?geometry.floor_y:-11000;
}
static float find_ceil(float x,float y,float z,struct Surface** surface) {
    (void)x;(void)z;geometry.ceiling_queries++;
    *surface=geometry.has_ceiling&&y<=geometry.ceiling_y?&ceiling:NULL;
    return *surface?geometry.ceiling_y:20000;
}
static void find_wall_collisions(struct WallCollisionData* walls) {
    geometry.wall_queries++;
    if(geometry.wall&&(!geometry.wall_height||fabsf(walls->offsetY-geometry.wall_height)<1))walls->x+=10;
}
static void find_surface_on_ray(Vec3f origin,Vec3f direction,struct Surface** surface,Vec3f hit) {
    geometry.ray_queries++;geometry.last_ray_y=origin[1];
    *surface=geometry.obstruction&&(!geometry.blocked_ray||geometry.ray_queries==geometry.blocked_ray)?&obstacle:NULL;
    for(unsigned i=0;i<3;i++)hit[i]=origin[i]+direction[i]*.5f;
}
static void reset_geometry(void) {
    memset(&geometry,0,sizeof geometry);geometry.has_floor=true;
    geometry.water=geometry.source_water=0;geometry.floor_y=-4000;geometry.ceiling_y=20000;
    ground=(struct Surface){.type=SURFACE_DEFAULT,.room=2,.normal={0,1,0}};
    ceiling=obstacle=other_room=ground;other_room.room=3;
}
static int close_float(float a,float b) {return fabsf(a-b)<.001f;}
'''


TESTS = r'''
static void test_deep_water(void) {
    test_name="continuous deep water keeps source swimming depth";reset_geometry();
    float point[3];int room=0;
    CHECK(smb_npc_safe_water(candidate_x,candidate_z,source_y,2,point,&room));
    CHECK(close_float(point[0],50)&&close_float(point[1],-100)&&close_float(point[2],30)&&room==2);
    CHECK(fabsf(point[1]*10-geometry.floor_y)>300);
    CHECK(geometry.water_queries==5&&geometry.floor_queries==5&&geometry.ceiling_queries==5&&geometry.wall_queries==3);
    CHECK(!smb_npc_safe_floor(candidate_x,candidate_z,source_y,2,300,point,&room));
    CHECK(smb_npc_safe_spawn(candidate_x,candidate_z,source_y,2,300,point,&room));
    CHECK(close_float(point[1],(source_y+30)*.1f)&&room==2);
    reset_geometry();geometry.water=source_y+320;geometry.floor_y=source_y-20;
    geometry.has_ceiling=true;geometry.ceiling_y=source_y+320;
    CHECK(smb_npc_safe_water(candidate_x,candidate_z,source_y,2,point,&room));
    CHECK(close_float(point[1],source_y*.1f));
    test_name="water spawn lifts feet above the real seabed";reset_geometry();
    geometry.floor_y=source_y-2;
    CHECK(smb_npc_safe_spawn(candidate_x,candidate_z,source_y,2,300,point,&room));
    CHECK(close_float(point[1],(source_y+30)*.1f)&&close_float(point[1]*10-geometry.floor_y,32));
    const float origin[3]={0,-1000,0};CHECK(smb_npc_spawn_path(origin,point));
    test_name="exact deep-water threshold keeps the full body submerged";reset_geometry();
    CHECK(smb_npc_safe_spawn(candidate_x,candidate_z,-350,2,300,point,&room));
    CHECK(close_float(point[1],-32));
    CHECK(!smb_npc_safe_spawn(candidate_x,candidate_z,-349,2,300,point,&room));
}
static void test_unsafe_water(void) {
    static const char* names[]={"dry candidate","shallow candidate","death floor","wrong room",
        "wall at feet","insufficient headroom","missing underwater floor","floor too close",
        "dry footprint edge","wrong-room footprint edge","wall at middle","wall at head"};
    for(unsigned hazard=0;hazard<sizeof names/sizeof *names;hazard++) {
        test_name=names[hazard];reset_geometry();float point[3];int room=0;
        if(hazard==0)geometry.water=-11000;
        if(hazard==1)geometry.water=source_y+20;
        if(hazard==2)ground.type=SURFACE_DEATH_PLANE;
        if(hazard==3)ground.room=3;
        if(hazard==4){geometry.wall=true;geometry.wall_height=20;}
        if(hazard==5){geometry.has_ceiling=true;geometry.ceiling_y=source_y+50;}
        if(hazard==6)geometry.has_floor=false;
        if(hazard==7)geometry.floor_y=source_y+20;
        if(hazard==8)geometry.dry_edge=true;
        if(hazard==9)geometry.wrong_room_edge=true;
        if(hazard==10){geometry.wall=true;geometry.wall_height=160;}
        if(hazard==11){geometry.wall=true;geometry.wall_height=300;}
        CHECK(!smb_npc_safe_water(candidate_x,candidate_z,source_y,2,point,&room));
        /* A dry candidate deliberately falls back to a real nearby floor;
         * all other submerged hazards must remain rejected by the router. */
        if(hazard!=0)CHECK(!smb_npc_safe_spawn(candidate_x,candidate_z,source_y,2,300,point,&room));
        if(hazard==4||hazard==10||hazard==11)CHECK(geometry.wall_queries);
        if(hazard==5)CHECK(geometry.ceiling_queries);
    }
    test_name="invalid water coordinates and outputs";reset_geometry();float point[3];int room;
    CHECK(!smb_npc_safe_water(NAN,candidate_z,source_y,2,point,&room));
    CHECK(!smb_npc_safe_water(candidate_x,INFINITY,source_y,2,point,&room));
    CHECK(!smb_npc_safe_water(candidate_x,candidate_z,NAN,2,point,&room));
    CHECK(!smb_npc_safe_water(candidate_x,candidate_z,source_y,2,NULL,&room));
    CHECK(!smb_npc_safe_water(candidate_x,candidate_z,source_y,2,point,NULL));
    CHECK(!geometry.water_queries&&!geometry.floor_queries);
    geometry.water=NAN;CHECK(!smb_npc_safe_water(candidate_x,candidate_z,source_y,2,point,&room));
    geometry.water=INFINITY;CHECK(!smb_npc_safe_water(candidate_x,candidate_z,source_y,2,point,&room));
    geometry.water=0;geometry.floor_y=NAN;
    CHECK(!smb_npc_safe_water(candidate_x,candidate_z,source_y,2,point,&room));
    geometry.floor_y=INFINITY;CHECK(!smb_npc_safe_water(candidate_x,candidate_z,source_y,2,point,&room));
}
static void test_floor_fallback(void) {
    test_name="dry standing spawn retains floor placement";reset_geometry();
    geometry.water=geometry.source_water=-11000;geometry.floor_y=100;
    float point[3];int room=0;
    CHECK(!smb_npc_safe_water(candidate_x,candidate_z,100,2,point,&room));
    CHECK(smb_npc_safe_spawn(candidate_x,candidate_z,100,2,300,point,&room));
    CHECK(close_float(point[0],50)&&close_float(point[1],10.5f)&&close_float(point[2],30)&&room==2);
    test_name="near-surface standing spawn keeps the existing floor path";reset_geometry();
    geometry.floor_y=100;geometry.water=geometry.source_water=400;
    CHECK(smb_npc_safe_spawn(candidate_x,candidate_z,100,2,300,point,&room));
    CHECK(close_float(point[1],10.5f)&&geometry.wall_queries==1);
    static const char* names[]={"missing floor","steep floor","death floor on land","wrong floor room",
        "floor outside tolerance","floor headroom","floor wall penetration"};
    for(unsigned hazard=0;hazard<sizeof names/sizeof *names;hazard++) {
        test_name=names[hazard];reset_geometry();
        geometry.water=geometry.source_water=-11000;geometry.floor_y=100;
        if(hazard==0)geometry.has_floor=false;
        if(hazard==1)ground.normal.y=.6f;
        if(hazard==2)ground.type=SURFACE_DEATH_PLANE;
        if(hazard==3)ground.room=3;
        if(hazard==4)geometry.floor_y=-500;
        if(hazard==5){geometry.has_ceiling=true;geometry.ceiling_y=200;}
        if(hazard==6)geometry.wall=true;
        CHECK(!smb_npc_safe_spawn(candidate_x,candidate_z,100,2,300,point,&room));
    }
}
static void test_spawn_path(void) {
    test_name="continuous submerged path checks five body volumes and nine rays";reset_geometry();
    const float origin[3]={0,-1000,0},point[3]={50,-97,30};
    CHECK(smb_npc_spawn_path(origin,point));
    CHECK(geometry.water_queries==26&&geometry.floor_queries==25&&geometry.ceiling_queries==25);
    CHECK(geometry.wall_queries==15&&geometry.ray_queries==9);
    CHECK(close_float(geometry.last_ray_y,source_y+300));
    test_name="dry break in the water passage";reset_geometry();geometry.water_gap=true;
    CHECK(!smb_npc_spawn_path(origin,point));CHECK(!geometry.ray_queries);
    test_name="shallow body volume in the middle of the passage";reset_geometry();geometry.shallow_middle=true;
    CHECK(!smb_npc_spawn_path(origin,point));CHECK(!geometry.ray_queries);
    test_name="each height and lateral path ray can block placement";
    for(unsigned ray=1;ray<=9;ray++) {
        reset_geometry();geometry.obstruction=true;geometry.blocked_ray=ray;
        CHECK(!smb_npc_spawn_path(origin,point));CHECK(geometry.ray_queries==ray);
    }
    test_name="dry placement retains the single 120-unit path ray";reset_geometry();
    geometry.water=geometry.source_water=-11000;
    const float dry_origin[3]={0,100,0},dry_point[3]={50,10.5f,30};
    CHECK(smb_npc_spawn_path(dry_origin,dry_point));
    CHECK(geometry.water_queries==1&&!geometry.floor_queries&&!geometry.ceiling_queries&&!geometry.wall_queries);
    CHECK(geometry.ray_queries==1&&close_float(geometry.last_ray_y,220));
}
int main(void) {
    test_deep_water();test_unsafe_water();test_floor_fallback();test_spawn_path();
    printf("NPC placement PASS: %u checks across four groups (ASan + UBSan).\n",checks);return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=HERE / "sm64_npcs.inc")
    parser.add_argument("--surface-header", type=Path,
                        default=ROOT / "work/sm64coopdx-melee/include/surface_terrains.h")
    parser.add_argument("--compiler", default=os.environ.get("CC", "clang"))
    args = parser.parse_args()
    source = args.source.resolve()
    text = source.read_text()
    surface_text = args.surface_header.read_text()
    constants = re.findall(r"^#define SURFACE_[A-Z0-9_]+\s+[^\n]+", surface_text, re.M)
    helpers = []
    for name in ("smb_npc_safe_floor", "smb_npc_safe_water", "smb_npc_safe_spawn", "smb_npc_spawn_path"):
        body, line = function(text, name)
        helpers.append(f'#line {line} "{source}"\n' + body)
    harness = "\n".join(["\n".join(constants), FIXTURES, *helpers,
                          '#line 1 "npc_placement_checks.c"\n' + TESTS])
    flags = ["-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
             "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
    with tempfile.TemporaryDirectory(prefix="sm64-npc-placement-") as directory:
        directory = Path(directory)
        test, binary = directory / "npc_placement.c", directory / "npc_placement"
        test.write_text(harness)
        subprocess.run([*shlex.split(args.compiler), *flags, str(test), "-lm", "-o", str(binary)], check=True)
        environment = os.environ.copy()
        environment["ASAN_OPTIONS"] = "halt_on_error=1:abort_on_error=1:detect_leaks=0"
        environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        result = subprocess.run([str(binary)], capture_output=True, text=True, env=environment)
        if result.returncode:
            raise SystemExit(result.stdout + result.stderr + f"\nHarness failed with status {result.returncode}")
    print(json.dumps({"passed": True, "output": result.stdout.strip(), "source": str(source),
                      "source_sha256": hashlib.sha256(text.encode()).hexdigest(),
                      "harness_sha256": hashlib.sha256(harness.encode()).hexdigest(),
                      "sanitizers": ["address", "undefined"],
                      "groups": ["deep_water", "unsafe_water", "floor_fallback", "spawn_path"],
                      "scope": "Verbatim placement helpers with synthetic geometry and source origins; no asset loading, engine build, checkout preparation, game launch, or network calls."}, indent=2))


if __name__ == "__main__":
    main()
