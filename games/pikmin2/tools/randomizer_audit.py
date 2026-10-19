#!/usr/bin/env python3
"""Auditoría de tesoros de Pikmin 2 para el Randomizer (docs/PLAN_RANDOMIZER.md §5.1).

Lee, fuera del juego y con las mismas reglas que el decomp:
  - user/Abe/stages.txt (cursos, LimitGenInfo, CaveOtakara, tesoros de superficie);
  - los generadores de texto de superficie de cada curso (defaultgen, initgen,
    plantsgen, nonloop/, loop/), con los días en que se carga cada archivo;
  - los caveinfo de las cuevas de historia (TekiInfo, ItemInfo, CapInfo);
  - otakara_config.txt e item_config.txt (índice = orden en el archivo);
  - los nombres de gEnemyInfo (enemyInfo.cpp) para separar "Enemigo_tesoro".

Comprueba que los totales cuadran con stages.txt (175 en cuevas + 26 en
superficie = 201) y puede generar los datos del catálogo C++.

Uso:
  tools/randomizer_audit.py                 informe en texto
  tools/randomizer_audit.py --json out.json volcado completo
  tools/randomizer_audit.py --emit-catalog  regenera pc_port/randomizer/pc_randomizer_catalog_data.inc
Sale con código 1 si algo no cuadra.
"""

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "assets"
USER = ASSETS / "user"
ENEMY_INFO_CPP = ROOT / "pikmin2-decomp" / "src" / "plugProjectYamashitaU" / "enemyInfo.cpp"
ENEMY_INFO_H = ROOT / "pikmin2-decomp" / "include" / "Game" / "enemyInfo.h"
CATALOG_INC = ROOT / "pc_port" / "randomizer" / "pc_randomizer_catalog_data.inc"

KIND_OTAKARA = 3  # PelletList::cKind (OTAKARA)
KIND_ITEM = 4     # PelletList::cKind (ITEM)

# Orden de los gestores de pellets al resolver un nombre por prefijo
# (PelletMgr::getCaveID recorre los gestores y usa strncmp con strlen(name)).
# Solo importan los dos que contienen tesoros.
CAVE_ID_KIND_ORDER = (KIND_OTAKARA, KIND_ITEM)

FIXED_ALWAYS = {"loozy"}
FIXED_DEFAULT = {"map01", "map02"}

# Tesoros del Titan Dweevil fijados en BigTreasure.cpp (no salen de caveinfo).
BIG_TREASURE_NAMES = ("elec", "fire", "gas", "water")
BIG_TREASURE_LOUIE = "loozy"
BIG_TREASURE_CAVE = "l_03"


# ─── Lectura de texto ────────────────────────────────────────────────────────

def read_text(path):
    data = path.read_bytes()
    for enc in ("shift_jis", "cp932"):
        try:
            return data.decode(enc)
        except UnicodeDecodeError:
            pass
    return data.decode("latin-1")


def tokens(path):
    """Tokens separados por espacios, sin comentarios '#' (como Stream de texto)."""
    out = []
    for line in read_text(path).splitlines():
        cut = line.find("#")
        if cut >= 0:
            line = line[:cut]
        out.extend(line.split())
    return out


class Cursor:
    def __init__(self, toks, where):
        self.t = toks
        self.i = 0
        self.where = where

    def peek(self):
        return self.t[self.i] if self.i < len(self.t) else None

    def next(self):
        if self.i >= len(self.t):
            raise ValueError(f"{self.where}: fin de archivo inesperado")
        v = self.t[self.i]
        self.i += 1
        return v

    def int(self):
        return int(self.next())

    def float(self):
        return float(self.next())

    def expect(self, tok):
        v = self.next()
        if v != tok:
            raise ValueError(f"{self.where}: se esperaba {tok!r} y hay {v!r} (token {self.i - 1})")

    def skip_block(self):
        """Salta desde un '{' ya consumido hasta su '}'."""
        depth = 1
        while depth:
            v = self.next()
            if v == "{":
                depth += 1
            elif v == "}":
                depth -= 1


# ─── Configuración de tesoros y enemigos ─────────────────────────────────────

def read_pellet_config(path, kind):
    """Lista de configs en orden; el índice es la posición (PelletConfig::mIndex)."""
    configs = []
    cur = None
    for line in read_text(path).splitlines():
        cut = line.find("#")
        if cut >= 0:
            line = line[:cut]
        parts = line.split()
        if not parts:
            continue
        if parts[0] == "{":
            cur = {}
        elif parts[0] == "}":
            if cur is not None:
                cur["kind"] = kind
                cur["index"] = len(configs)
                configs.append(cur)
            cur = None
        elif cur is not None and len(parts) >= 2:
            cur[parts[0]] = parts[1]
    out = []
    for c in configs:
        out.append({
            "name": c["name"],
            "kind": kind,
            "index": c["index"],
            "code": (kind << 8) | c["index"],
            "min": int(c.get("min", 0)),
            "max": int(c.get("max", 0)),
            "money": int(c.get("money", 0)),
            "unique": c.get("unique", "no") == "yes",
            "special": int(c.get("code", 0)),
            "dictionary": int(c.get("dictionary", -1)),
        })
    return out


class Treasures:
    def __init__(self):
        base = USER / "Abe" / "Pellet" / "us"
        self.otakara = read_pellet_config(base / "otakara_config.txt", KIND_OTAKARA)
        self.items = read_pellet_config(base / "item_config.txt", KIND_ITEM)
        self.by_kind = {KIND_OTAKARA: self.otakara, KIND_ITEM: self.items}
        self.by_name = {}
        for t in self.otakara + self.items:
            if t["name"] in self.by_name:
                raise ValueError(f"tesoro repetido en las listas: {t['name']}")
            self.by_name[t["name"]] = t

    def all(self):
        return self.otakara + self.items

    def by_code(self, code):
        kind, idx = code >> 8, code & 0xFF
        lst = self.by_kind.get(kind)
        if lst is None or idx >= len(lst):
            return None
        return lst[idx]

    def exact(self, name):
        """makeOtakaraItemCode: getConfigAndKind (nombre exacto)."""
        return self.by_name.get(name)

    def cave_id(self, name):
        """ItemInfo: PelletMgr::getCaveID, primer config cuyo nombre empieza por name."""
        for kind in CAVE_ID_KIND_ORDER:
            for t in self.by_kind[kind]:
                if t["name"].startswith(name):
                    return t
        return None


def read_enemy_names():
    """gEnemyInfo: nombre, EnemyTypeID y si tiene EFlag_CanBeSpawned."""
    text = ENEMY_INFO_CPP.read_text(encoding="utf-8", errors="replace")
    rows = re.findall(r'\{\s*"(\w+)",\s*EnemyTypeID::EnemyID_(\w+),[^,]*,[^,]*,\s*\(([^)]*)\)', text)
    if not rows:
        raise ValueError("no se pudieron leer los nombres de gEnemyInfo")
    # El orden de gEnemyInfo no es el de EnemyTypeID: el número sale del enum.
    enum = {m.group(1): int(m.group(2)) for m in re.finditer(
        r"EnemyID_(\w+)\s*=\s*(-?\d+)", ENEMY_INFO_H.read_text(encoding="utf-8", errors="replace"))}
    return [{"name": n, "id": i, "type_id": enum.get(i), "spawnable": "EFlag_CanBeSpawned" in f}
            for n, i, f in rows]


# ─── stages.txt ──────────────────────────────────────────────────────────────

def read_stages():
    c = Cursor(tokens(USER / "Abe" / "stages.txt"), "stages.txt")
    n = c.int()
    courses = []
    for _ in range(n):
        c.expect("{")
        course = {}
        while c.peek() != "end":
            key = c.next()
            if key in ("start",):
                course[key] = [c.float(), c.float(), c.float()]
            else:
                course[key] = c.next()
        c.expect("end")
        nonloop = []
        for _ in range(c.int()):
            nonloop.append({"file": c.next(), "start": c.int(), "end": c.int(), "dur": c.int()})
        loop = []
        for _ in range(c.int()):
            loop.append({"file": c.next(), "start": c.int(), "end": c.int(), "dur": c.int()})
        caves = []
        for _ in range(c.int()):
            caves.append({"id": c.next().strip("{}"), "treasures": c.int(), "file": c.next()})
        ground = c.int()
        c.expect("}")
        course.update(nonloop=nonloop, loop=loop, caves=caves, ground=ground)
        courses.append(course)
    return courses


# ─── Generadores de superficie ───────────────────────────────────────────────

def parse_generators(path, treasures):
    """Lista de generadores de un archivo (formato de Generator::read)."""
    c = Cursor(tokens(path), str(path.relative_to(ROOT)))
    c.next()            # {v0.1}
    c.float(); c.float(); c.float()  # startPos
    c.float()           # startDir
    count = c.int()
    gens = []
    for gi in range(count):
        c.expect("{")
        version = c.next()
        reserved = c.int()
        revive_days = c.int()
        name_bytes = bytes(c.int() & 0xFF for _ in range(32)).split(b"\0")[0]
        try:
            name = name_bytes.decode("shift_jis")
        except UnicodeDecodeError:
            name = name_bytes.decode("latin-1")
        pos = [c.float(), c.float(), c.float()]
        c.float(); c.float(); c.float()  # offset
        # El resto del generador hasta su '}' (las llaves sueltas anidan; las
        # etiquetas como {teki} o {????} son un solo token).
        body, depth = [], 1
        while True:
            v = c.next()
            if v == "{":
                depth += 1
            elif v == "}":
                depth -= 1
                if depth == 0:
                    break
            body.append(v)
        gtype = body[0] if body else ""
        g = {"index": gi, "name": name, "version": version, "reserved": reserved,
             "revive_days": revive_days, "pos": pos, "type": gtype.strip("{}")}
        if gtype == "{teki}":
            # GenObjectEnemy::doRead / doReadOldVersion. 0005: {versión}, id,
            # birth_type, num, dirección, punto/círculo, radio, tamaño, código de
            # tesoro, ... 0003 y 0004: igual sin birth_type. Antes: sin tesoro.
            ver = body[1]
            g["type_version"] = ver
            g["enemy_id"] = int(body[2])
            if ver == "{0005}":
                g["birth_type"] = int(body[3])
                g["num"] = int(body[4])
                g["otakara_code"] = int(body[9])
            elif ver in ("{0003}", "{0004}"):
                g["num"] = int(body[3])
                g["otakara_code"] = int(body[8])
            else:
                g["otakara_code"] = 0
        elif gtype == "{item}" and len(body) > 3 and body[2] == "{":
            # GenItem: {versión} { {tipo} rotación(3) {versión} parámetros... }
            g["item_type"] = body[3].strip("{}")
            end = body.index("}", 4) if "}" in body[4:] else len(body)
            g["item_params"] = body[7:end]
        elif gtype == "{pelt}":
            # GenPellet::doRead: {versión} { mgr id, rotación, versión local, índice }
            g["type_version"] = body[1]
            if body[2] != "{":
                raise ValueError(f"{c.where} #{gi}: pellet sin bloque")
            g["mgr_id"] = int(body[3])
            g["pellet_index"] = int(body[8])
        gens.append(g)
    return gens


def surface_files(course):
    """(archivo relativo, primer día, último día) de cada archivo de generadores.
    None = todos los días. Días según LimitGenInfo de stages.txt."""
    folder = USER / "Abe" / "map" / course["name"]
    files = [("defaultgen.txt", None, None), ("initgen.txt", None, None), ("plantsgen.txt", None, None)]
    for e in course["nonloop"]:
        files.append(("nonloop/" + e["file"], e["start"], e["end"]))
    for e in course["loop"]:
        files.append(("loop/" + e["file"], e["start"], e["end"]))
    return folder, files


# ─── caveinfo ────────────────────────────────────────────────────────────────

def parse_param_block(c):
    """{fXXX} tipo valor ... {_eof}; devuelve dict id -> valor (texto)."""
    c.expect("{")
    out = {}
    while True:
        key = c.next()
        if key == "{_eof}":
            break
        c.next()  # tipo/tamaño
        out[key.strip("{}")] = c.next()
    c.expect("}")
    return out


def split_teki_name(raw, enemy_names):
    """TekiInfo::read: '$'/'$N' de caída y separación 'Enemigo_tesoro' por el
    primer '_' que sigue a un nombre de enemigo conocido."""
    drop = 0
    s = raw
    if s.startswith("$"):
        if len(s) > 1 and "1" <= s[1] <= "9":
            drop = int(s[1])
            s = s[2:]
        else:
            drop = -1  # DROP_PikminOrLeader
            s = s[1:]
    enemy, treasure, in_enemy = "", "", True
    for ch in s:
        if ch == "_" and in_enemy and enemy in enemy_names:
            in_enemy = False
            continue
        if in_enemy:
            enemy += ch
        else:
            treasure += ch
    return drop, enemy, treasure


def parse_caveinfo(path, enemy_names):
    c = Cursor(tokens(path), str(path.relative_to(ROOT)))
    head = parse_param_block(c)
    nfloors = c.int()
    floors = []
    for _ in range(nfloors):
        parms = parse_param_block(c)
        f = {"from": int(parms["f000"]), "to": int(parms["f001"]),
             "version": int(parms.get("f015", "0")),
             "blackman_timer": float(parms.get("f016", "0")),
             "teki": [], "item": [], "gate": [], "cap": []}
        c.expect("{")
        for i in range(c.int()):
            drop, enemy, tre = split_teki_name(c.next(), enemy_names)
            f["teki"].append({"index": i, "drop": drop, "enemy": enemy, "treasure": tre,
                              "weight": c.int(), "type": c.int()})
        c.expect("}")
        c.expect("{")
        for i in range(c.int()):
            f["item"].append({"index": i, "name": c.next(), "weight": c.int()})
        c.expect("}")
        c.expect("{")
        for i in range(c.int()):
            f["gate"].append({"index": i, "name": c.next(), "life": c.float(), "weight": c.int()})
        c.expect("}")
        if f["version"] >= 1:
            c.expect("{")
            for i in range(c.int()):
                empty = c.int()
                cap = {"index": i, "empty": empty}
                if empty == 0:
                    drop, enemy, tre = split_teki_name(c.next(), enemy_names)
                    cap.update(drop=drop, enemy=enemy, treasure=tre, weight=c.int(), type=c.int())
                f["cap"].append(cap)
            c.expect("}")
        floors.append(f)
    return int(head.get("c000", nfloors)), floors


# ─── Auditoría ───────────────────────────────────────────────────────────────

def audit():
    problems = []
    treasures = Treasures()
    enemies = read_enemy_names()
    enemy_names = {e["name"] for e in enemies}
    enemy_by_id = {e["type_id"]: e for e in enemies}
    courses = [c for c in read_stages() if c["name"] != "test_map"]

    locations = []
    surface_extra = []  # tesoros en archivos de días limitados (no cuentan)
    obstacles = []      # entradas de cueva, compuertas, muros, puentes... (requisitos §8.2)

    for ci, course in enumerate(courses):
        folder, files = surface_files(course)
        ground_found = 0
        for rel, d0, d1 in files:
            path = folder / rel
            if not path.exists():
                problems.append(f"{course['name']}: falta {rel}")
                continue
            for g in parse_generators(path, treasures):
                if g["type"] == "item" and g.get("item_type") != "barl":
                    obstacles.append({"course": course["name"], "file": rel, "days": [d0, d1],
                                      "gen_index": g["index"], "name": g["name"], "item": g.get("item_type"),
                                      "params": g.get("item_params", []), "pos": g["pos"]})
                code, source, carrier = None, None, None
                if g["type"] == "pelt" and g["mgr_id"] in (KIND_OTAKARA, KIND_ITEM):
                    code = (g["mgr_id"] << 8) | g["pellet_index"]
                    source = "ground"
                elif g["type"] == "teki" and (g["otakara_code"] >> 8) in (KIND_OTAKARA, KIND_ITEM):
                    code = g["otakara_code"]
                    source = "enemy"
                    e = enemy_by_id.get(g["enemy_id"])
                    carrier = e["name"] if e else f"#{g['enemy_id']}"
                if code is None:
                    continue
                t = treasures.by_code(code)
                if t is None:
                    problems.append(f"{course['name']}/{rel} #{g['index']}: código {code:#x} sin tesoro")
                    continue
                loc = {
                    "kind": "SurfaceGround" if source == "ground" else "SurfaceEnemy",
                    "course": course["name"], "course_idx": ci, "cave": None, "floor": 0,
                    "file": rel, "gen_index": g["index"], "days": [d0, d1],
                    "carrier": carrier, "gen_type": None, "list_index": g["index"],
                    "treasure": t["name"], "code": code, "pos": g["pos"],
                    "revive_days": g["revive_days"], "entries": 1, "weight_min": 1,
                }
                if d0 is None:
                    locations.append(loc)
                    ground_found += 1
                else:
                    surface_extra.append(loc)
        if ground_found != course["ground"]:
            problems.append(f"{course['name']}: {ground_found} tesoros de superficie, stages.txt dice {course['ground']}")

        for cave in course["caves"]:
            if cave["id"] == "test":
                continue
            path = USER / "Mukki" / "mapunits" / "caveinfo" / cave["file"]
            nfloors, floors = parse_caveinfo(path, enemy_names)
            found = 0
            for f in floors:
                for floor in range(f["from"] + 1, f["to"] + 2):
                    def add(kind, carrier, gen_type, list_index, tname, weight, resolve):
                        nonlocal found
                        t = resolve(tname)
                        if t is None:
                            problems.append(f"{cave['id']} {floor}: tesoro desconocido {tname!r}")
                            return
                        if resolve is treasures.cave_id and t["name"] != tname:
                            problems.append(f"{cave['id']} {floor}: ItemInfo {tname!r} se resuelve como {t['name']!r} (prefijo)")
                        locations.append({
                            "kind": kind, "course": course["name"], "course_idx": ci,
                            "cave": cave["id"], "floor": floor, "file": cave["file"],
                            "gen_index": None, "days": [None, None], "carrier": carrier,
                            "gen_type": gen_type, "list_index": list_index,
                            "treasure": t["name"], "code": t["code"], "pos": None,
                            "revive_days": None, "entries": 1,
                            "weight_min": weight // 10, "weight_rand": weight % 10,
                        })
                        found += 1

                    for e in f["teki"]:
                        if e["treasure"]:
                            add("CaveTeki", e["enemy"], e["type"], e["index"], e["treasure"], e["weight"], treasures.exact)
                    for e in f["item"]:
                        add("CaveItem", None, 2, e["index"], e["name"], e["weight"], treasures.cave_id)
                    for e in f["cap"]:
                        if not e["empty"] and e["treasure"]:
                            add("CaveCap", e["enemy"], e["type"], e["index"], e["treasure"], e["weight"], treasures.exact)
            if cave["id"] == BIG_TREASURE_CAVE:
                for i, n in enumerate(BIG_TREASURE_NAMES + (BIG_TREASURE_LOUIE,)):
                    t = treasures.exact(n)
                    locations.append({
                        "kind": "BossHardcoded", "course": course["name"], "course_idx": ci,
                        "cave": cave["id"], "floor": nfloors, "file": "BigTreasure.cpp",
                        "gen_index": None, "days": [None, None], "carrier": "BigTreasure",
                        "gen_type": 8, "list_index": i, "treasure": n, "code": t["code"],
                        "pos": None, "revive_days": None, "entries": 1, "weight_min": 1,
                    })
                    found += 1
            cave["found"] = found
            cave["floors"] = nfloors

    # Una identidad repetida (Waterwraith en 5 subniveles) es una sola
    # ubicación lógica con varias entradas.
    merged, seen = [], {}
    for loc in locations:
        key = (loc["cave"], loc["treasure"]) if loc["cave"] else None
        if key and key in seen:
            first = seen[key]
            first["kind"] = "MultiEntry"
            first["entries"] += 1
            first.setdefault("floors", [first["floor"]]).append(loc["floor"])
            continue
        if key:
            seen[key] = loc
        merged.append(loc)
    locations = merged

    for course in courses:
        for cave in course["caves"]:
            if cave["id"] == "test":
                continue
            n = sum(1 for l in locations if l["cave"] == cave["id"])
            if n != cave["treasures"]:
                problems.append(f"{cave['id']}: {n} ubicaciones, stages.txt dice {cave['treasures']}")

    names = [l["treasure"] for l in locations]
    for n in sorted(set(names)):
        if names.count(n) > 1:
            problems.append(f"tesoro en varias ubicaciones: {n}")
    missing = sorted(t["name"] for t in treasures.all() if t["name"] not in names)
    expected_total = sum(c["ground"] for c in courses) + sum(
        cv["treasures"] for c in courses for cv in c["caves"] if cv["id"] != "test")
    if len(locations) != expected_total:
        problems.append(f"{len(locations)} ubicaciones, stages.txt suma {expected_total}")

    return {
        "courses": courses, "locations": locations, "surface_limited": surface_extra,
        "treasures": treasures.all(), "missing": missing, "problems": problems, "obstacles": obstacles,
        "enemies": enemies,
    }


# ─── Salida ──────────────────────────────────────────────────────────────────

def print_report(r):
    locs = r["locations"]
    print(f"Ubicaciones: {len(locs)} (superficie {sum(1 for l in locs if l['cave'] is None)}, "
          f"cuevas {sum(1 for l in locs if l['cave'])})")
    print(f"Tesoros en listas: {len(r['treasures'])}; sin ubicación: {len(r['missing'])}")
    for m in r["missing"]:
        print(f"  sin ubicación: {m}")
    for c in r["courses"]:
        print(f"\n[{c['name']}] superficie {c['ground']}")
        for l in locs:
            if l["course"] == c["name"] and l["cave"] is None:
                src = l["carrier"] or "suelo"
                print(f"  {l['file']} #{l['gen_index']}: {l['treasure']} ({src})")
        for cv in c["caves"]:
            if cv["id"] == "test":
                continue
            n = sum(1 for l in locs if l["cave"] == cv["id"])
            print(f"  {cv['id']} {cv['file']}: {n} (stages.txt {cv['treasures']}), {cv['floors']} subniveles")
            for l in locs:
                if l["cave"] == cv["id"]:
                    extra = f" x{l['entries']} {l.get('floors')}" if l["entries"] > 1 else ""
                    src = l["carrier"] or "item"
                    gt = "" if l["gen_type"] is None else f", tipo {l['gen_type']}"
                    print(f"    {l['floor']}: {l['treasure']} ({l['kind']}, {src}{gt}, mín {l['weight_min']}){extra}")
    if r["surface_limited"]:
        print("\nTesoros en archivos de días limitados (no cuentan):")
        for l in r["surface_limited"]:
            print(f"  {l['course']}/{l['file']} días {l['days'][0]}-{l['days'][1]} #{l['gen_index']}: {l['treasure']}")
    if r["obstacles"]:
        print("\nEntradas de cueva y obstáculos de superficie (base de los requisitos, §8.2):")
        for o in r["obstacles"]:
            days = "" if o["days"][0] is None else f" días {o['days'][0]}-{o['days'][1]}"
            params = o["params"][:3] if o["item"] == "cave" else o["params"]
            print(f"  {o['course']}/{o['file']}{days} #{o['gen_index']} {o['item']} {' '.join(params)}: {o['name']}")
    print()
    if r["problems"]:
        print("PROBLEMAS:")
        for p in r["problems"]:
            print(f"  - {p}")
    else:
        print("OK: todos los totales cuadran con stages.txt.")


KIND_ENUM = {
    "SurfaceGround": "PC_P2LOC_SurfaceGround", "SurfaceEnemy": "PC_P2LOC_SurfaceEnemy",
    "CaveItem": "PC_P2LOC_CaveItem", "CaveTeki": "PC_P2LOC_CaveTeki", "CaveCap": "PC_P2LOC_CaveCap",
    "BossHardcoded": "PC_P2LOC_BossHardcoded", "MultiEntry": "PC_P2LOC_MultiEntry",
}


def emit_catalog(r, path):
    caves = [cv["id"] for c in r["courses"] for cv in c["caves"] if cv["id"] != "test"]
    lines = [
        "// Generado por tools/randomizer_audit.py --emit-catalog. No editar a mano.",
        "// clang-format off",
        "",
        "// Tesoros: nombre, tipo (3 tesoro / 4 Kit), índice, min, max, money.",
        "PC_P2_TREASURE_TABLE_BEGIN",
    ]
    for t in r["treasures"]:
        lines.append(f'\t{{ "{t["name"]}", {t["kind"]}, {t["index"]}, {t["min"]}, {t["max"]}, {t["money"]} }},')
    lines += ["PC_P2_TREASURE_TABLE_END", "",
              "// Cuevas de historia: id, archivo, curso.",
              "PC_P2_CAVE_TABLE_BEGIN"]
    for c in r["courses"]:
        for cv in c["caves"]:
            if cv["id"] != "test":
                lines.append(f'\t{{ "{cv["id"]}", "{cv["file"]}", {r["courses"].index(c)}, {cv["floors"]}, {cv["treasures"]} }},')
    lines += ["PC_P2_CAVE_TABLE_END", "",
              "// Ubicaciones: clase, curso, cueva (-1 = superficie), subnivel, archivo,",
              "// generador o índice en su lista, portador, tipo de punto, código original,",
              "// mínimo garantizado, entradas, posición (superficie).",
              "PC_P2_LOCATION_TABLE_BEGIN"]
    for l in r["locations"]:
        cave = caves.index(l["cave"]) if l["cave"] else -1
        carrier = f'"{l["carrier"]}"' if l["carrier"] else "nullptr"
        gt = -1 if l["gen_type"] is None else l["gen_type"]
        idx = l["gen_index"] if l["gen_index"] is not None else l["list_index"]
        pos = l["pos"] or [0.0, 0.0, 0.0]
        lines.append(
            f'\t{{ {KIND_ENUM[l["kind"]]}, {l["course_idx"]}, {cave}, {l["floor"]}, "{l["file"]}", {idx}, '
            f'{carrier}, {gt}, 0x{l["code"]:03X}, {l["weight_min"]}, {l["entries"]}, '
            f'{{ {pos[0]:.1f}f, {pos[1]:.1f}f, {pos[2]:.1f}f }} }}, // {l["treasure"]}')
    lines += ["PC_P2_LOCATION_TABLE_END", ""]
    path.write_text("\n".join(lines), encoding="utf-8")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--json", type=Path)
    ap.add_argument("--emit-catalog", action="store_true")
    args = ap.parse_args()
    r = audit()
    print_report(r)
    if args.json:
        args.json.write_text(json.dumps(r, ensure_ascii=False, indent=1), encoding="utf-8")
    if args.emit_catalog:
        if r["problems"]:
            print("No se genera el catálogo: hay problemas.", file=sys.stderr)
            return 1
        emit_catalog(r, CATALOG_INC)
        print(f"Catálogo escrito en {CATALOG_INC.relative_to(ROOT)}")
    return 1 if r["problems"] else 0


if __name__ == "__main__":
    sys.exit(main())
