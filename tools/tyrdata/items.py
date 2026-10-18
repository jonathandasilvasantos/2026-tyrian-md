"""Item and enemy tables of tyrian.hdt (episode 4: last entry of
tyrian4.lvl) -- docs/data_formats.md §4, episodes.c:JE_loadItemDat."""
from . import data_path
from .binfile import Reader, offset_table

COUNTS = (780, 42, 6, 13, 30, 10, 850)   # WEAP PORT POWER SHIP OPTION SHIELD ENEMY
SPECIAL_RECORDS = 47
ITEM_BLOCK_SIZE = 137192


def _weapon(r):
    return dict(drain=r.u16(), shotrepeat=r.u8(), multi=r.u8(), weapani=r.u16(), max=r.u8(),
                tx=r.u8(), ty=r.u8(), aim=r.u8(), attack=r.array(r.u8, 8), del_=r.array(r.u8, 8),
                sx=r.array(r.s8, 8), sy=r.array(r.s8, 8), bx=r.array(r.s8, 8), by=r.array(r.s8, 8),
                sg=r.array(r.u16, 8), acceleration=r.s8(), accelerationx=r.s8(), circlesize=r.u8(),
                sound=r.u8(), trail=r.u8(), shipblastfilter=r.u8())


def _port(r):
    return dict(name=r.pstring(31), opnum=r.u8(), op=[r.array(r.u16, 11), r.array(r.u16, 11)],
                cost=r.u16(), itemgraphic=r.u16(), poweruse=r.u16())


def _special(r):
    return dict(name=r.pstring(31), itemgraphic=r.u16(), pwr=r.u8(), stype=r.u8(), wpn=r.u16())


def _power(r):
    return dict(name=r.pstring(31), itemgraphic=r.u16(), power=r.u8(), speed=r.s8(), cost=r.u16())


def _ship(r):
    return dict(name=r.pstring(31), shipgraphic=r.u16(), itemgraphic=r.u16(), ani=r.u8(),
                spd=r.s8(), dmg=r.u8(), cost=r.u16(), bigshipgraphic=r.u8())


def _option(r):
    return dict(name=r.pstring(31), pwr=r.u8(), itemgraphic=r.u16(), cost=r.u16(), tr=r.u8(),
                option=r.u8(), opspd=r.s8(), ani=r.u8(), gr=r.array(r.u16, 20), wport=r.u8(),
                wpnum=r.u16(), ammo=r.u8(), stop=bool(r.u8()), icongr=r.u8())


def _shield(r):
    return dict(name=r.pstring(31), tpwr=r.u8(), mpwr=r.u8(), itemgraphic=r.u16(), cost=r.u16())


def _enemy(r):
    return dict(ani=r.u8(), tur=r.array(r.u8, 3), freq=r.array(r.u8, 3), xmove=r.s8(),
                ymove=r.s8(), xaccel=r.s8(), yaccel=r.s8(), xcaccel=r.s8(), ycaccel=r.s8(),
                startx=r.s16(), starty=r.s16(), startxc=r.s8(), startyc=r.s8(), armor=r.u8(),
                esize=r.u8(), egraphic=r.array(r.u16, 20), explosiontype=r.u8(), animate=r.u8(),
                shapebank=r.u8(), xrev=r.s8(), yrev=r.s8(), dgr=r.u16(), dlevel=r.s8(),
                dani=r.s8(), elaunchfreq=r.u8(), elaunchtype=r.u16(), value=r.s16(),
                eenemydie=r.u16())


def read_items(data, start):
    r = Reader(data, start)
    counts = tuple(r.array(r.u16, 7))
    if counts != COUNTS:
        raise ValueError(f"item counts {counts}, expected {COUNTS}")
    w, p, pw, sh, op, sd, en = counts
    items = dict(
        weapons=[_weapon(r) for _ in range(w + 1)],
        ports=[_port(r) for _ in range(p + 1)],
        specials=[_special(r) for _ in range(SPECIAL_RECORDS)],
        powers=[_power(r) for _ in range(pw + 1)],
        ships=[_ship(r) for _ in range(sh + 1)],
        options=[_option(r) for _ in range(op + 1)],
        shields=[_shield(r) for _ in range(sd + 1)],
        enemies=[_enemy(r) for _ in range(en + 1)],
    )
    if r.pos - start != ITEM_BLOCK_SIZE:
        raise ValueError(f"item block is {r.pos - start} bytes, expected {ITEM_BLOCK_SIZE}")
    return items


def load_items(episode=1):
    """Episodes 1-3 use tyrian.hdt; episode 4 its own block in tyrian4.lvl."""
    if episode > 3:
        with open(data_path("tyrian4.lvl"), "rb") as f:
            data = f.read()
        return read_items(data, offset_table(data)[-1][0])
    with open(data_path("tyrian.hdt"), "rb") as f:
        data = f.read()
    return read_items(data, Reader(data).u32())
