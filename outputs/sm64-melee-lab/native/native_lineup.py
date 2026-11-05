"""Plaza roster selection shared by the launcher and native table generator.

All 26 retail forms (Zelda and Sheik separately) plus seven Akaneia additions.
A 32-slot game keeps every retail entry and rotates the omitted added fighter.
Ice Climbers occupy one player slot; Nana remains a real companion entity.
"""
# name, external CharacterKind, native FighterKind
RETAIL = [
    ('mario',8,0),('fox',2,1),('falcon',0,2),('dk',1,3),
    ('kirby',4,4),('bowser',5,5),('link',6,6),('sheik',19,7),
    ('ness',11,8),('peach',12,9),('iceclimbers',14,10),
    ('pikachu',13,12),('samus',16,13),('yoshi',17,14),
    ('jigglypuff',15,15),('mewtwo',10,16),('luigi',7,17),
    ('marth',9,18),('zelda',18,19),('younglink',21,20),
    ('drmario',22,21),('falco',20,22),('pichu',24,23),
    ('gamewatch',3,24),('ganondorf',25,25),('roy',23,26),
]
ADDED = [(name,34+i,33+i) for i,name in enumerate(
    ('wolf','diddy','charizard','lucas','sonic','dedede','tails'))]
PLAYERS = RETAIL + ADDED
BY_NAME = {row[0]:row for row in PLAYERS}


def lineup(fighters, character='sonic', seed=1, humans=1, roster='all'):
    if fighters not in (2,16,24,32) or humans not in (0,1):
        raise ValueError('Unsupported player count')
    if character not in BY_NAME:raise ValueError('Unknown player character')
    if roster not in ('all','akaneia'):raise ValueError('Unknown roster')
    if roster=='akaneia':
        result=[BY_NAME['fox'] if s<4 else ADDED[(s-4)%7] for s in range(fighters)]
        if humans:result[0]=BY_NAME[character]
        return result
    human=BY_NAME[character] if humans else None
    retail=[r for r in RETAIL if r!=human]
    added=[r for r in ADDED if r!=human]
    # Prioritise all retail forms at capacity. Rotate the additional selection
    # without consuming game RNG or ever replacing the chosen human.
    offset=(seed-1)%len(added)
    added=added[offset:]+added[:offset]
    available=fighters-humans
    if available>=len(retail):added=added[:available-len(retail)]
    result=[]
    while retail or added:
        result.extend(retail[:4]);del retail[:4]
        if added:result.append(added.pop(0))
    return ([human] if human else [])+result[:available]


def allowed_kinds(kind):
    if kind in (7,19):return {7,19}  # Native Zelda/Sheik transformation.
    if kind==10:return {10,11}      # Native Ice Climbers leader/companion.
    return {kind}


def generate(path):
    rows=['/* Generated playable roster; numeric IDs are distinct namespaces. */',
          'static const struct { const char* name; int character,kind; } m3d_playable[] = {']
    rows += ['    {"%s",%d,%d},'%r for r in PLAYERS]
    rows += ['};']
    path.write_text('\n'.join(rows)+'\n')


def room_lineup(fighters,characters,seed=1):
    """Humans keep their picks; fill CPUs without repeating an unused form."""
    if fighters not in (2,16,24,32) or not 1<=len(characters)<=fighters or any(c not in BY_NAME for c in characters):
        raise ValueError('Invalid room roster')
    chosen=[BY_NAME[c] for c in characters]
    preferred=lineup(32,characters[0],seed,1)
    available=[r for r in preferred+PLAYERS if r not in chosen]
    available=list(dict.fromkeys(available))
    while len(chosen)<fighters:
        least=min(sum(r==c for c in chosen) for r in PLAYERS)
        row=next(r for r in available+PLAYERS if sum(r==c for c in chosen)==least)
        chosen.append(row)
    return chosen
