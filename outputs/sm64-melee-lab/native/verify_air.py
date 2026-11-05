#!/usr/bin/env python3
"""Compare underwater air timing with the unmodified stock SM64 C routine."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]


def stock_health_function(text):
    # Ignore braces inside comments and strings while retaining exact source
    # offsets. The returned function itself is copied without translation.
    mask = re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"',
                  lambda match: ' ' * len(match[0]), text, flags=re.S)
    declaration = re.search(r'void\s+update_mario_health\s*\([^)]*\)\s*\{', mask)
    if not declaration:
        raise ValueError('Stock update_mario_health was not found')
    depth, end = 1, declaration.end()
    while depth:
        if mask[end] == '{':
            depth += 1
        elif mask[end] == '}':
            depth -= 1
        end += 1
    return text[declaration.start():end], text[:declaration.start()].count('\n') + 1


C_PREFIX = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
typedef uint32_t u32;
typedef int32_t s32;
typedef struct {int terrainType;} Area;
struct MarioState {int health;unsigned healCounter,hurtCounter,input,action,flags;float pos[3];int waterLevel;Area* area;};
#define INPUT_IN_POISON_GAS (1u<<15)
#define ACT_FLAG_INTANGIBLE (1u<<12)
#define ACT_FLAG_SWIMMING (1u<<13)
#define MARIO_METAL_CAP 4u
#define ACT_GROUP_MASK 0x1c0u
#define ACT_GROUP_SUBMERGED 0xc0u
#define TERRAIN_MASK 7
#define TERRAIN_SNOW 2
#define SOUND_MOVING_ALMOST_DROWNING 1
static int gDebugLevelSelect,gRumblePakTimer;
static float gDefaultSoundArgs[3];
static void play_sound(int sound,const void* source){(void)sound;(void)source;}
static int is_rumble_finished_and_queue_empty(void){return 0;}
static void queue_rumble_data(int duration,int strength){(void)duration;(void)strength;}
'''

C_TEST = r'''
#include "sm64_air.inc"
static unsigned reference_ticks,dry_ticks,fraction_ticks;
static uint32_t random_state=0x5a17c0de;
static uint32_t next_random(void){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
static int step(int air,float* heal,float* hurt,int wet,int metal,int cold,int surface,int intangible,int debug){return smb_air_step(air,heal,hurt,wet,metal,cold,surface,intangible,debug);}
static void require(int pass,const char* name){if(!pass){fprintf(stderr,"AIR FAIL %s\n",name);assert(pass);}}
static void compare_stock(void) {
    for(unsigned conditions=0;conditions<64;conditions++)for(unsigned trial=0;trial<128;trial++) {
        int wet=conditions&1,metal=(conditions>>1)&1,cold=(conditions>>2)&1;
        int surface=(conditions>>3)&1,intangible=(conditions>>4)&1,debug=(conditions>>5)&1;
        Area area={cold?TERRAIN_SNOW:0};
        struct MarioState m={0};m.health=0x100+(next_random()%(0x880-0x100+1));m.area=&area;m.waterLevel=1000;
        m.pos[1]=surface?860:500;m.flags=metal?MARIO_METAL_CAP:0;
        /* Stock metal-water actions belong to the submerged group but omit
         * ACT_FLAG_SWIMMING; the metal cap alone is not checked by its water
         * health branch. Ordinary swimming can also be intangible. */
        m.action=ACT_GROUP_SUBMERGED|(metal?0:ACT_FLAG_SWIMMING)|(intangible?ACT_FLAG_INTANGIBLE:0);
        m.healCounter=(trial%4==1||trial%4==3)?next_random()%41:0;
        m.hurtCounter=(trial%4==2||trial%4==3)?next_random()%41:0;
        int air=m.health;float heal=m.healCounter*64.f,hurt=m.hurtCounter*64.f;
        gDebugLevelSelect=debug;
        for(unsigned frame=0;frame<64&&air>=0x100;frame++) {
            if(!wet) {
                /* Intentional mod adaptation: outside water breath refills
                 * and underwater damage cannot cause a hidden land death.
                 * Stock SM64 instead shares combat HP with its air meter. */
                int expected=air+26;if(expected>0x880)expected=0x880;
                air=step(air,&heal,&hurt,0,metal,cold,surface,intangible,debug);
                require(air==expected&&heal==0&&hurt==0,"dry fresh air and cleared queues");dry_ticks++;continue;
            }
            update_mario_health(&m);
            air=step(air,&heal,&hurt,1,metal,cold,surface,intangible,debug);
            if(air!=m.health||heal!=m.healCounter*64.f||hurt!=m.hurtCounter*64.f) {
                fprintf(stderr,"AIR REFERENCE MISMATCH flags=%u trial=%u frame=%u air=%d stock=%d heal=%.3f stockheal=%u hurt=%.3f stockhurt=%u\n",conditions,trial,frame,air,m.health,heal,m.healCounter,hurt,m.hurtCounter);
                require(0,"exact stock frame and queues");
            }
            reference_ticks++;
        }
    }
}
static void explicit_rules(void) {
    const unsigned values[3]={1,2,5};
    for(unsigned coin=0;coin<3;coin++) {
        float heal=256.f*values[coin],hurt=0;int air=512;
        for(unsigned frame=0;frame<4*values[coin];frame++) {
            air=step(air,&heal,&hurt,1,0,0,0,0,0);
            require(air==512+(int)(frame+1)*64,"coin heals one64-unit tick per frame");
        }
        require(air==512+(int)values[coin]*256&&heal==0,"yellow red blue capacity");
        air=step(air,&heal,&hurt,1,0,0,0,0,0);
        require(air==511+(int)values[coin]*256,"coin finishes then passive drain resumes");
    }
    float heal=256,hurt=256;int air=1000;
    for(unsigned n=0;n<4;n++){air=step(air,&heal,&hurt,1,0,0,0,0,0);require(air==1000,"simultaneous healing and damage pause passive drain");}
    require(step(air,&heal,&hurt,1,0,0,0,0,0)==999,"passive drain resumes after both queues");
    heal=hurt=0;air=600;
    for(unsigned n=0;n<90;n++)air=step(air,&heal,&hurt,1,1,0,1,0,0);
    require(air==600,"metal freezes rather than surface refills");
    heal=128;air=step(air,&heal,&hurt,1,1,0,1,0,0);air=step(air,&heal,&hurt,1,1,0,1,0,0);
    require(air==728&&heal==0,"metal still accepts queued coin healing");
    heal=hurt=0;require(step(600,&heal,&hurt,1,0,1,1,0,0)==597,"cold surface still drains3");
    require(step(600,&heal,&hurt,1,0,0,1,0,0)==626,"warm surface refills26");
    require(step(600,&heal,&hurt,1,0,0,0,1,0)==600,"intangible pauses passive drain");
    require(step(600,&heal,&hurt,1,0,0,0,0,1)==600,"debug pauses deep drain");
    require(step(0x100,&heal,&hurt,1,0,0,0,0,0)==0xff,"fatal threshold is strictly below256");
    require(step(0x100,&heal,&hurt,1,1,0,0,0,0)==0x100,"exact256 remains alive without drain");
    heal=128;hurt=1e7f;air=step(0x100,&heal,&hurt,0,0,1,0,0,0);
    require(air==0x11a&&heal==0&&hurt==0,"dry queued hit clears without drowning");
    heal=0;hurt=1600;air=2176;
    for(unsigned n=0;n<25;n++){air=step(air,&heal,&hurt,1,0,0,0,0,0);require(air==2176-(int)(n+1)*64,"large Melee hit uses64-unit ticks");}
    require(air==576&&hurt==0,"50percent maps to1600air units");
}
static void fractional_damage(void) {
    float heal=0,hurt=0;int air=1000;
    for(unsigned n=0;n<4;n++){hurt+=.25f;air=step(air,&heal,&hurt,1,1,0,0,0,0);fraction_ticks++;}
    require(air==999&&hurt==0,"four fractional quarters preserve one damage unit");
    for(unsigned trial=0;trial<256;trial++) {
        air=2000;heal=hurt=0;float total=0;
        for(unsigned n=0;n<50;n++) {
            float percent=(next_random()%26)/100.f;
            float units=percent*32.f;total+=units;hurt+=units;
            air=step(air,&heal,&hurt,1,1,0,0,0,0);fraction_ticks++;
            require(fabsf((2000-air)+hurt-total)<.003f,"fractional native damage is conserved");
            require(hurt>=0&&hurt<1,"sub-unit remainder survives each tick");
        }
    }
    heal=0;hurt=.75f;
    require(step(1000,&heal,&hurt,1,0,0,0,0,0)==999&&hurt==.75f,"sub-unit remainder does not pause passive drain");
    hurt=1.25f;require(step(1000,&heal,&hurt,1,0,0,0,0,0)==999&&hurt==.25f,"whole queued unit pauses passive drain and retains fraction");
}
int main(void) {
    compare_stock();explicit_rules();fractional_damage();
    printf("AIR RULES PASS stock_ticks=%u dry_policy_ticks=%u fractional_ticks=%u condition_combinations=64 coin_values=1,2,5\n",reference_ticks,dry_ticks,fraction_ticks);
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stock-source', type=Path, default=ROOT / 'work/sm64ex-source')
    args = parser.parse_args()
    source = args.stock_source.resolve()
    reference_file = 'src/game/mario.c'
    commit = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    stock = subprocess.check_output(['git', '-C', str(source), 'show', f'HEAD:{reference_file}'], text=True)
    reference, line = stock_health_function(stock)
    if 'smb_host_' in reference:
        raise ValueError('The selected reference contains mod hooks instead of an unmodified stock routine')
    with tempfile.TemporaryDirectory(prefix='sm64-air-reference-') as folder:
        folder = Path(folder)
        test, binary = folder / 'air.c', folder / 'air'
        test.write_text(C_PREFIX + '\n' + reference + '\n' + C_TEST)
        subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(HERE), str(test), '-lm', '-o', str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True, check=True)
    receipt = dict(passed=True, output=result.stdout.strip(),
                   reference=dict(checkout=str(source), commit=commit, file=reference_file, line=line,
                                  function='update_mario_health', sha256=hashlib.sha256(reference.encode()).hexdigest(),
                                  extraction='Exact unmodified function copied from git HEAD; minimal C state and inert audio/rumble stubs.'),
                   helper_sha256=hashlib.sha256((HERE / 'sm64_air.inc').read_bytes()).hexdigest(),
                   scope='Pure C frame-by-frame stock comparison with real shared helper; no host game or native GPU run.',
                   intentional_dry_policy='Outside water refill26 and clear pending underwater damage/healing; stock combat HP does not have this separate-air adaptation.',
                   coverage=['all64 wet/metal/cold/surface/intangible/debug combinations',
                             'randomized integer healing/damage queues until death or64frames',
                             'yellow/red/blue coin value1/2/5 exact timing', 'simultaneous healing and damage',
                             'metal freeze and queued healing', 'strict fatal threshold', 'dry queued-damage clearance',
                             '32air units per Melee percentage point', 'fractional damage conservation'])
    target = HERE.parent / 'verification/air-rules.json'
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(receipt, indent=2) + '\n')
    print(result.stdout.strip())
    print(target)


if __name__ == '__main__':
    main()
