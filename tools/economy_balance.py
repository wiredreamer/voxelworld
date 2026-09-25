import random
import sys
from dataclasses import dataclass

MVP_TIERS = (1, 2, 3)
TIER_GROWTH = 1.6
TRIALS = 2000
SEED = 11
RUN_CAP = 40

WHITE = "Белый"
GREEN = "Зелёный"
BLUE = "Синий"
RARITIES = (WHITE, GREEN, BLUE)
RARITY_VALUE = {WHITE: 1.0, GREEN: 2.2, BLUE: 5.0}
WHITE_VALUE = 40
SALE_SHARE = 0.25
NO_WHITE_FROM_TIER = 3

IRON = "Железо"
CLOTH = "Ткань"
WOOD = "Дерево"
MATERIALS = (IRON, CLOTH, WOOD)
MATERIAL_VALUE = 2

ARMOR_SLOTS = ("Голова", "Тело", "Руки", "Ноги")
WARRIOR_SLOTS = ARMOR_SLOTS + ("Оружие", "Вторая рука")
ARCHER_SLOTS = ARMOR_SLOTS + ("Лук",)
SLOT_WEIGHT = {"Лук": 2}

FLOORS_BY_TIER = {1: 2, 2: 4, 3: 6}
MINUTES_PER_FLOOR = 6
BOSS_MINUTES = 3
TRAVEL_MINUTES = 4

ENEMIES_PER_FLOOR = 12
ENEMY_ITEM_CHANCE = 0.10
ENEMY_MATERIAL_CHANCE = 0.35
ENEMY_MATERIALS = (1, 3)
IRON_VEINS_PER_FLOOR = 2
WOOD_VEINS_PER_FLOOR = 1
VEIN_MATERIALS = (2, 4)
CHESTS_PER_FLOOR = 1
CHEST_ITEMS = (1, 2)
CHEST_MATERIALS = (5, 10)
CHEST_SPARK_CHANCE = 0.25
ELITE_SPARK_CHANCE = 0.40
BOSS_ITEMS = 2
BOSS_CLOTS = 1
CLASS_SHARE = 0.75

DROP_RARITY = {
    "враг": {WHITE: 0.79, GREEN: 0.20, BLUE: 0.01},
    "элита": {WHITE: 0.32, GREEN: 0.62, BLUE: 0.06},
    "босс": {WHITE: 0.0, GREEN: 0.80, BLUE: 0.20},
    "сундук": {WHITE: 0.57, GREEN: 0.40, BLUE: 0.03},
}

SERVICE_GROWTH = 2.5

POTIONS_PER_FLOOR = 0.8
POTION_PRICE = 0.25
ARROWS_PER_FLOOR = 60
ARROW_PRICE = 0.15
ENEMY_ARROW_CHANCE = 0.25
ENEMY_ARROWS = 5

REFORGE_GOLD = 0.4
REFORGE_MATERIALS = 3
TO_GREEN_GOLD = 1.2
TO_GREEN_MATERIALS = 5
TO_BLUE_GOLD = 2.6
TO_BLUE_MATERIALS = 8
OFFERING_VALUE = 1.6
CULT_FLAW = 0.10

CONSUMABLE_SHARE = (0.08, 0.30)
REFORGES_PER_RUN = (6.0, 16.0)
GOLD_HEADROOM = (1.0, 3.0)
SPARKS_PER_RUN = (0.8, 4.0)
CLOTS_PER_RUN = (1.0, 1.0)
MATERIAL_HEADROOM = (1.5, 12.0)
RUNS_TO_BASE = (1.0, 3.0)
RUNS_TO_BLUE = (3.0, 7.0)
HOURS_PER_TIER = (1.5, 3.5)
HOURLY_GROWTH = (1.5, 2.8)
BLUE_PRICE_SHARE = (0.3, 1.0)
OFFERING_SHARE = (0.2, 0.7)
ARROW_SHARE = 0.12


@dataclass(frozen=True)
class Item:
    tier: int
    rarity: str


@dataclass(frozen=True)
class Klass:
    name: str
    slots: tuple[str, ...]
    line: dict[str, str]
    arrows: bool


@dataclass(frozen=True)
class Target:
    label: str
    low: float
    high: float
    values: tuple[float, ...]

    def met(self) -> bool:
        return all(self.low <= v <= self.high for v in self.values)

    def shown(self) -> str:
        low, high = min(self.values), max(self.values)
        return f"{low:g}" if low == high else f"{low:g}–{high:g}"


WARRIOR = Klass("Воин", WARRIOR_SLOTS, {s: IRON for s in WARRIOR_SLOTS}, False)
ARCHER = Klass("Лучник", ARCHER_SLOTS, {**{s: CLOTH for s in ARMOR_SLOTS}, "Лук": WOOD}, True)
CLASSES = (WARRIOR, ARCHER)


def unit(tier: int) -> float:
    return WHITE_VALUE * TIER_GROWTH ** (tier - 1)


def service_unit(tier: int) -> float:
    return WHITE_VALUE * SERVICE_GROWTH ** (tier - 1)


def weight(slot: str) -> int:
    return SLOT_WEIGHT.get(slot, 1)


def item_value(item: Item, slot: str) -> float:
    return unit(item.tier) * RARITY_VALUE[item.rarity] * weight(slot)


def rank(item: Item, slot: str) -> float:
    return item.tier * 100 + RARITIES.index(item.rarity)


def run_minutes(tier: int) -> float:
    return FLOORS_BY_TIER[tier] * MINUTES_PER_FLOOR + BOSS_MINUTES + TRAVEL_MINUTES


def potion_price(tier: int) -> float:
    return POTION_PRICE * unit(tier)


def arrow_cost(tier: int) -> float:
    floors = FLOORS_BY_TIER[tier]
    found = ENEMIES_PER_FLOOR * floors * ENEMY_ARROW_CHANCE * ENEMY_ARROWS
    bought = max(0.0, ARROWS_PER_FLOOR * floors - found)
    return bought * ARROW_PRICE


def consumable_cost(klass: Klass, tier: int) -> float:
    cost = POTIONS_PER_FLOOR * FLOORS_BY_TIER[tier] * potion_price(tier)
    if klass.arrows:
        cost += arrow_cost(tier)
    return cost


def rarity_roll(kind: str, tier: int, rng: random.Random) -> str:
    table = dict(DROP_RARITY[kind])
    if tier >= NO_WHITE_FROM_TIER:
        table[GREEN] += table.pop(WHITE, 0.0)
    names = list(table)
    return rng.choices(names, weights=[table[n] for n in names])[0]


def price_to_green(tier: int, slot: str) -> tuple[float, int]:
    return TO_GREEN_GOLD * service_unit(tier) * weight(slot), TO_GREEN_MATERIALS * weight(slot)


def price_to_blue(tier: int, slot: str) -> tuple[float, int]:
    return TO_BLUE_GOLD * service_unit(tier) * weight(slot), TO_BLUE_MATERIALS * weight(slot)


def simulate_run(tier: int, rng: random.Random) -> tuple[list[Item], dict[str, int], int, int]:
    floors = FLOORS_BY_TIER[tier]
    items: list[Item] = []
    materials = {m: 0 for m in MATERIALS}
    sparks = clots = 0
    for floor in range(floors):
        for _ in range(ENEMIES_PER_FLOOR):
            if rng.random() < ENEMY_ITEM_CHANCE:
                items.append(Item(tier, rarity_roll("враг", tier, rng)))
            if rng.random() < ENEMY_MATERIAL_CHANCE:
                materials[CLOTH] += rng.randint(*ENEMY_MATERIALS)
        for _ in range(IRON_VEINS_PER_FLOOR):
            materials[IRON] += rng.randint(*VEIN_MATERIALS)
        for _ in range(WOOD_VEINS_PER_FLOOR):
            materials[WOOD] += rng.randint(*VEIN_MATERIALS)
        for _ in range(CHESTS_PER_FLOOR):
            for _ in range(rng.randint(*CHEST_ITEMS)):
                items.append(Item(tier, rarity_roll("сундук", tier, rng)))
            materials[rng.choice(MATERIALS)] += rng.randint(*CHEST_MATERIALS)
            if rng.random() < CHEST_SPARK_CHANCE:
                sparks += 1
        if floor == floors - 1:
            for _ in range(BOSS_ITEMS):
                items.append(Item(tier, rarity_roll("босс", tier, rng)))
            clots += BOSS_CLOTS
        else:
            items.append(Item(tier, rarity_roll("элита", tier, rng)))
            if rng.random() < ELITE_SPARK_CHANCE:
                sparks += 1
    return items, materials, sparks, clots


def sort_drops(klass: Klass, items: list[Item], gear: dict[str, Item], keep: set[str], rng: random.Random) -> float:
    sold = 0.0
    for item in items:
        if rng.random() >= CLASS_SHARE:
            sold += unit(item.tier) * RARITY_VALUE[item.rarity] * SALE_SHARE
            continue
        slot = rng.choice(klass.slots)
        if rank(item, slot) > rank(gear[slot], slot):
            if slot not in keep:
                sold += item_value(gear[slot], slot) * SALE_SHARE
            keep.add(slot)
            gear[slot] = item
        else:
            sold += item_value(item, slot) * SALE_SHARE
    return sold


def raise_rarity(klass: Klass, tier: int, gear: dict[str, Item], purse: dict) -> None:
    while True:
        done = True
        for slot in sorted(klass.slots, key=lambda s: RARITIES.index(gear[s].rarity)):
            item = gear[slot]
            if item.tier != tier or item.rarity == BLUE:
                continue
            step = RARITIES.index(item.rarity)
            gold, need = price_to_green(tier, slot) if step == 0 else price_to_blue(tier, slot)
            token = "sparks" if step == 0 else "clots"
            line = klass.line[slot]
            if purse[token] >= weight(slot) and purse["gold"] >= gold and purse[line] >= need:
                purse[token] -= weight(slot)
                purse["gold"] -= gold
                purse[line] -= need
                gear[slot] = Item(tier, RARITIES[step + 1])
                done = False
        if done:
            return


def simulate_campaign(klass: Klass, rng: random.Random) -> dict[int, dict]:
    gear = {s: Item(1, WHITE) for s in klass.slots}
    starting = set(klass.slots)
    purse = {"gold": 0.0, "sparks": 0, "clots": 0, IRON: 0, CLOTH: 0, WOOD: 0}
    phases: dict[int, dict] = {}
    for tier in MVP_TIERS:
        runs = 0
        income = spend = 0.0
        sparks = clots = 0
        mined = {m: 0 for m in MATERIALS}
        to_base = None
        while runs < RUN_CAP:
            runs += 1
            items, materials, got_sparks, got_clots = simulate_run(tier, rng)
            sold = sort_drops(klass, items, gear, starting, rng)
            cost = consumable_cost(klass, tier)
            purse["gold"] += sold - cost
            income += sold
            spend += cost
            sparks += got_sparks
            clots += got_clots
            purse["sparks"] += got_sparks
            purse["clots"] += got_clots
            for m in MATERIALS:
                purse[m] += materials[m]
                mined[m] += materials[m]
            raise_rarity(klass, tier, gear, purse)
            if to_base is None and all(gear[s].tier == tier for s in klass.slots):
                to_base = runs
            if all(gear[s].tier == tier and gear[s].rarity == BLUE for s in klass.slots):
                break
        phases[tier] = {
            "runs": runs,
            "to_base": to_base or runs,
            "income": income / runs,
            "spend": spend / runs,
            "sparks": sparks / runs,
            "clots": clots / runs,
            "materials": {m: mined[m] / runs for m in MATERIALS},
            "left": purse["gold"],
        }
        starting = set()
    return phases


def averaged() -> dict[str, dict[int, dict]]:
    out: dict[str, dict[int, dict]] = {}
    for klass in CLASSES:
        rng = random.Random(SEED)
        totals: dict[int, dict] = {}
        for _ in range(TRIALS):
            phases = simulate_campaign(klass, rng)
            for tier, phase in phases.items():
                acc = totals.setdefault(tier, {})
                for key, value in phase.items():
                    if key == "materials":
                        pot = acc.setdefault(key, {m: 0.0 for m in MATERIALS})
                        for m in MATERIALS:
                            pot[m] += value[m]
                    else:
                        acc[key] = acc.get(key, 0.0) + value
        for tier, acc in totals.items():
            for key in list(acc):
                if key == "materials":
                    acc[key] = {m: acc[key][m] / TRIALS for m in MATERIALS}
                else:
                    acc[key] = acc[key] / TRIALS
        out[klass.name] = totals
    return out


def markdown_table(header: list[str], rows: list[list[str]]) -> str:
    lines = ["| " + " | ".join(header) + " |", "|" + "---|" * len(header)]
    lines += ["| " + " | ".join(row) + " |" for row in rows]
    return "\n".join(lines)


def price_table() -> str:
    header = ["Услуга", "Тир 1", "Тир 2", "Тир 3", "Материалы"]
    rows = [
        ["Перековать", *[f"{REFORGE_GOLD * service_unit(t):.0f} з" for t in MVP_TIERS], f"{REFORGE_MATERIALS} базовых"],
        ["Белый → зелёный", *[f"{TO_GREEN_GOLD * service_unit(t):.0f} з" for t in MVP_TIERS], f"{TO_GREEN_MATERIALS} базовых + искра"],
        ["Зелёный → синий", *[f"{TO_BLUE_GOLD * service_unit(t):.0f} з" for t in MVP_TIERS], f"{TO_BLUE_MATERIALS} базовых + сгусток"],
        ["Подношение", *[f"{OFFERING_VALUE * service_unit(t):.0f} з" for t in MVP_TIERS], "добычей"],
        ["Зелье", *[f"{potion_price(t):.0f} з" for t in MVP_TIERS], "—"],
        ["Стрела", *[f"{ARROW_PRICE:.2f} з" for _ in MVP_TIERS], "—"],
    ]
    return markdown_table(header, rows)


def value_table() -> str:
    header = ["Что", "Тир 1", "Тир 2", "Тир 3"]
    rows = []
    for rarity in RARITIES:
        rows.append([f"{rarity} предмет", *[f"{unit(t) * RARITY_VALUE[rarity]:.0f} з" for t in MVP_TIERS]])
    for rarity in RARITIES:
        rows.append([f"{rarity}: скупщик даёт", *[f"{unit(t) * RARITY_VALUE[rarity] * SALE_SHARE:.0f} з" for t in MVP_TIERS]])
    rows.append(["Базовый материал", *[f"{MATERIAL_VALUE} з" for _ in MVP_TIERS]])
    return markdown_table(header, rows)


def income_table(data: dict) -> str:
    header = ["Класс · тир", "Доход за спуск", "Расходники", "Чистыми", "Минут", "Чистыми в час", "Перековок"]
    rows = []
    for klass in CLASSES:
        for tier in MVP_TIERS:
            phase = data[klass.name][tier]
            net = phase["income"] - phase["spend"]
            minutes = run_minutes(tier)
            rows.append([
                f"{klass.name} · т{tier}",
                f"{phase['income']:.0f} з",
                f"{phase['spend']:.0f} з",
                f"{net:.0f} з",
                f"{minutes:.0f}",
                f"{net * 60 / minutes:.0f} з",
                f"{net / (REFORGE_GOLD * service_unit(tier)):.1f}",
            ])
    return markdown_table(header, rows)


def material_table(data: dict) -> str:
    header = ["Класс · тир", "Железо", "Ткань", "Дерево", "Искра", "Сгусток", "Хватает редкостей"]
    rows = []
    for klass in CLASSES:
        for tier in MVP_TIERS:
            phase = data[klass.name][tier]
            line = klass.line[ARMOR_SLOTS[0]]
            rows.append([
                f"{klass.name} · т{tier}",
                f"{phase['materials'][IRON]:.0f}",
                f"{phase['materials'][CLOTH]:.0f}",
                f"{phase['materials'][WOOD]:.0f}",
                f"{phase['sparks']:.1f}",
                f"{phase['clots']:.1f}",
                f"{phase['materials'][line] / TO_BLUE_MATERIALS:.1f}",
            ])
    return markdown_table(header, rows)


def bottleneck_table(data: dict) -> str:
    header = ["Класс · тир", "Синих на золото", "Синих на сгустки", "Золото свободнее в"]
    rows = []
    for klass in CLASSES:
        for tier in MVP_TIERS:
            phase = data[klass.name][tier]
            net = phase["income"] - phase["spend"]
            by_gold = net / (TO_BLUE_GOLD * service_unit(tier))
            by_clots = phase["clots"]
            rows.append([
                f"{klass.name} · т{tier}",
                f"{by_gold:.1f}",
                f"{by_clots:.1f}",
                f"×{by_gold / by_clots:.1f}",
            ])
    return markdown_table(header, rows)


def set_value(klass: Klass, tier: int, rarity: str) -> float:
    return sum(unit(tier) * RARITY_VALUE[rarity] * weight(s) for s in klass.slots)


def set_table(data: dict) -> str:
    header = ["Комплект", "Тир 1", "Тир 2", "Тир 3"]
    rows = []
    for rarity in RARITIES:
        rows.append([f"{rarity}, цена", *[f"{set_value(WARRIOR, t, rarity):.0f} з" for t in MVP_TIERS]])
    rows.append(["Синий, скупщик даёт", *[f"{set_value(WARRIOR, t, BLUE) * SALE_SHARE:.0f} з" for t in MVP_TIERS]])
    rows.append([
        "Синий, в спусках дохода",
        *[f"{set_value(WARRIOR, t, BLUE) * SALE_SHARE / (data[WARRIOR.name][t]['income'] - data[WARRIOR.name][t]['spend']):.1f}" for t in MVP_TIERS],
    ])
    return markdown_table(header, rows)


def hours_per_tier(data: dict, klass: Klass, tier: int) -> float:
    return data[klass.name][tier]["runs"] * run_minutes(tier) / 60


def progress_table(data: dict) -> str:
    header = ["Класс · тир", "Спусков до базы тира", "Спусков до полного синего", "Часов на тир"]
    rows = []
    for klass in CLASSES:
        for tier in MVP_TIERS:
            phase = data[klass.name][tier]
            rows.append([
                f"{klass.name} · т{tier}",
                f"{phase['to_base']:.1f}",
                f"{phase['runs']:.1f}",
                f"{phase['runs'] * run_minutes(tier) / 60:.1f}",
            ])
    return markdown_table(header, rows)


def hourly(data: dict, klass: Klass, tier: int) -> float:
    phase = data[klass.name][tier]
    return (phase["income"] - phase["spend"]) * 60 / run_minutes(tier)


def targets(data: dict) -> list[Target]:
    consumable = tuple(
        round(data[k.name][t]["spend"] / data[k.name][t]["income"], 2) for k in CLASSES for t in MVP_TIERS
    )
    reforges = tuple(
        round((data[k.name][t]["income"] - data[k.name][t]["spend"]) / (REFORGE_GOLD * service_unit(t)), 1)
        for k in CLASSES for t in MVP_TIERS
    )
    headroom = tuple(
        round((data[k.name][t]["income"] - data[k.name][t]["spend"]) / (TO_BLUE_GOLD * service_unit(t)) / data[k.name][t]["clots"], 1)
        for k in CLASSES for t in MVP_TIERS
    )
    sparks = tuple(round(data[k.name][t]["sparks"], 1) for k in CLASSES for t in MVP_TIERS)
    clots = tuple(round(data[k.name][t]["clots"], 1) for k in CLASSES for t in MVP_TIERS)
    material = tuple(
        round(data[k.name][t]["materials"][k.line[ARMOR_SLOTS[0]]] / TO_BLUE_MATERIALS, 1)
        for k in CLASSES for t in MVP_TIERS
    )
    to_base = tuple(round(data[k.name][t]["to_base"], 1) for k in CLASSES for t in MVP_TIERS)
    to_blue = tuple(round(data[k.name][t]["runs"], 1) for k in CLASSES for t in MVP_TIERS)
    growth = tuple(
        round(hourly(data, k, t + 1) / hourly(data, k, t), 2) for k in CLASSES for t in MVP_TIERS[:-1]
    )
    blue_share = tuple(
        round(TO_BLUE_GOLD * service_unit(t) / (data[k.name][t]["income"] - data[k.name][t]["spend"]), 2)
        for k in CLASSES for t in MVP_TIERS
    )
    offering = tuple(
        round(OFFERING_VALUE * service_unit(t) / (data[k.name][t]["income"] - data[k.name][t]["spend"]), 2)
        for k in CLASSES for t in MVP_TIERS
    )
    arrows = tuple(round(arrow_cost(t) / data[ARCHER.name][t]["income"], 2) for t in MVP_TIERS)
    hours = tuple(round(hours_per_tier(data, k, t), 1) for k in CLASSES for t in MVP_TIERS)
    deeper = tuple(
        round(hours_per_tier(data, k, t + 1) - hours_per_tier(data, k, t), 1) for k in CLASSES for t in MVP_TIERS[:-1]
    )
    return [
        Target("Расходники: 8–30% дохода", *CONSUMABLE_SHARE, consumable),
        Target("Перековок на чистый доход за спуск: 6–16", *REFORGES_PER_RUN, reforges),
        Target("Золото свободнее сгустков в ×1–3", *GOLD_HEADROOM, headroom),
        Target("Искр за спуск: 0.8–4", *SPARKS_PER_RUN, sparks),
        Target("Сгустков за спуск: ровно 1", *CLOTS_PER_RUN, clots),
        Target("Базовых хватает на 1.5–12 поднятий", *MATERIAL_HEADROOM, material),
        Target("Спусков до базы своего тира: 1–3", *RUNS_TO_BASE, to_base),
        Target("Спусков до полного синего: 3–7", *RUNS_TO_BLUE, to_blue),
        Target("Чистый доход в час растёт за тир ×1.5–2.8", *HOURLY_GROWTH, growth),
        Target("Цена синего к доходу за спуск: 0.3–1", *BLUE_PRICE_SHARE, blue_share),
        Target("Подношение: 0.2–0.7 дохода за спуск", *OFFERING_SHARE, offering),
        Target("Стрелы лучника: не больше 12% дохода", 0.0, ARROW_SHARE, arrows),
        Target("Часов на полный синий тира: 1.5–3.5", *HOURS_PER_TIER, hours),
        Target("Глубже тир — дольше тир", 0.1, float("inf"), deeper),
    ]


def targets_table(checked: list[Target]) -> str:
    rows = [[t.label, t.shown(), "да" if t.met() else "**НЕТ**"] for t in checked]
    return markdown_table(["Цель", "Результат", "Попадает"], rows)


def main() -> int:
    data = averaged()
    checked = targets(data)
    sections = [
        ("Цены услуг", price_table()),
        ("Стоимость предметов", value_table()),
        ("Доход со спуска", income_table(data)),
        ("Материалы за спуск", material_table(data)),
        ("Что ограничивает редкость", bottleneck_table(data)),
        ("Сколько спусков на тир", progress_table(data)),
        ("Цена комплекта", set_table(data)),
        ("Сверка с целями", targets_table(checked)),
    ]
    for title, table in sections:
        print(f"**{title}:**\n\n{table}\n")
    return 0 if all(t.met() for t in checked) else 1


if __name__ == "__main__":
    sys.exit(main())
