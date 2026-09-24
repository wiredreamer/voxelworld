import math
import random
import sys
from collections import Counter
from dataclasses import dataclass

MVP_TIERS = (1, 2, 3)
DEPTH_TIERS = (1, 2, 3, 4, 5)
TIER_GROWTH = 1.6
TRIALS = 20000
SEED = 7

BODY_HEALTH = 20
HEALTH_PER_VITALITY = 10
DAMAGE_PER_MAIN_ATTRIBUTE = 1
ARMOR_VITALITY_PER_PIECE = 2
ARMOR_CONSTANT = 60
BASE_SPREAD = 0.10

ATTRIBUTE_GROWTH = 1.35
LADDER_LAST_TIER = 5
TYPICAL_RARITY_POINTS = {1: 0, 2: 4, 3: 8, 4: 12, 5: 16}
RARITIES = (("Зелёный", 1, 1), ("Синий", 2, 2), ("Фиолетовый", 3, 3), ("Оранжевый", 4, 4))

BALANCED = 0.5
ALL_IN_DAMAGE = 0.85
ALL_IN_VITALITY = 0.15
FAIRNESS_TOLERANCE = 0.10

DAMAGE_SPREAD = 0.05
BASE_CRIT_CHANCE = 0.05
CRIT_MULTIPLIER = 2.0
CRIT_CHANCES_SHOWN = (0.05, 0.15, 0.25)

SWORD_DAMAGE = 10
BOW_DAMAGE = 10
SWORD_CHAIN = (1.0, 1.0, 1.5)
BOW_QUICK_SHOT = 0.6
BOW_FULL_DRAW = 2.2
HEAVY_HIT = 1.7
HEAVY_GUARD_COST = 2.0

ARMOR_BY_WEIGHT = {
    "heavy": {"chest": 15, "helmet": 5, "gloves": 4, "boots": 4},
    "light": {"chest": 7, "helmet": 4, "gloves": 3, "boots": 3},
}

SHIELD_BY_TIER = {1: (0.50, 60), 2: (0.62, 110), 3: (0.75, 210)}

SWORD_SWING_SECONDS = 0.45
BOW_DRAW_SECONDS = 1.0
WARRIOR_ATTACK_UPTIME = 0.35
ARCHER_ATTACK_UPTIME = 0.45
COUNTER_MULTIPLIER, COUNTER_EVERY_SECONDS = 2.5, 8.0
WHIRL_MULTIPLIER, WHIRL_EVERY_SECONDS = 1.2, 6.0
VOLLEY_MULTIPLIER, VOLLEY_ARROWS, VOLLEY_EVERY_SECONDS = 0.6, 5, 6.0
PIN_MULTIPLIER, PIN_EVERY_SECONDS = 1.5, 8.0

BOSS_HEALTH = 2100
BOSS_TIER = 2


@dataclass(frozen=True)
class Enemy:
    name: str
    health: float
    hit: float


@dataclass(frozen=True)
class Build:
    name: str
    armor_weight: str
    weapon_damage: float


SKELETON = Enemy("Скелет", 40, 26)
ZOMBIE = Enemy("Зомби", 75, 30)

WARRIOR = Build("Воин", "heavy", SWORD_DAMAGE)
ARCHER = Build("Лучник", "light", BOW_DAMAGE)


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


def grown(value: float, tier: int) -> float:
    return value * TIER_GROWTH ** (tier - 1)


def attribute_growth(tier: int) -> float:
    ladder = min(tier, LADDER_LAST_TIER) - 1
    beyond = max(0, tier - LADDER_LAST_TIER)
    return ATTRIBUTE_GROWTH ** ladder * TIER_GROWTH ** beyond


def item_attributes(points: int, tier: int) -> int:
    return round(points * attribute_growth(tier))


def typical_attributes(tier: int) -> float:
    return TYPICAL_RARITY_POINTS[tier] * attribute_growth(tier)


def main_attribute(tier: int, main_share: float) -> float:
    return typical_attributes(tier) * main_share


def armor_vitality(tier: int) -> int:
    return len(ARMOR_BY_WEIGHT["heavy"]) * round(grown(ARMOR_VITALITY_PER_PIECE, tier))


def vitality(tier: int, main_share: float) -> float:
    return armor_vitality(tier) + typical_attributes(tier) * (1 - main_share)


def player_health(tier: int, main_share: float = BALANCED) -> float:
    return BODY_HEALTH + HEALTH_PER_VITALITY * vitality(tier, main_share)


def weapon_damage(build: Build, tier: int, main_share: float = BALANCED) -> float:
    return grown(build.weapon_damage, tier) + DAMAGE_PER_MAIN_ATTRIBUTE * main_attribute(tier, main_share)


def armor(build: Build, tier: int) -> float:
    return grown(sum(ARMOR_BY_WEIGHT[build.armor_weight].values()), tier)


def armor_reduction(armor_value: float, attacker_tier: int) -> float:
    return armor_value / (armor_value + grown(ARMOR_CONSTANT, attacker_tier))


def base_range(base: float, tier: int) -> tuple[float, float]:
    middle = grown(base, tier)
    return middle * (1 - BASE_SPREAD), middle * (1 + BASE_SPREAD)


def rolled(damage: float, rng: random.Random, crit_chance: float) -> int:
    spread = rng.uniform(1 - DAMAGE_SPREAD, 1 + DAMAGE_SPREAD)
    crit = CRIT_MULTIPLIER if rng.random() < crit_chance else 1.0
    return max(1, round(damage * spread * crit))


def hits_distribution(health: float, damage: float, multipliers: tuple[float, ...], crit_chance: float) -> Counter:
    rng = random.Random(SEED)
    outcomes = Counter()
    for _ in range(TRIALS):
        hits = 0
        dealt = 0
        while dealt < health:
            dealt += rolled(damage * multipliers[hits % len(multipliers)], rng, crit_chance)
            hits += 1
        outcomes[hits] += 1
    return outcomes


def typical(outcomes: Counter) -> int:
    return outcomes.most_common(1)[0][0]


def share_below(outcomes: Counter, hits: int) -> float:
    return sum(n for h, n in outcomes.items() if h < hits) / TRIALS


def described(outcomes: Counter, equal_tier: bool) -> str:
    main = typical(outcomes)
    text = f"**{main}**" if equal_tier else str(main)
    others = sorted((h, n / TRIALS) for h, n in outcomes.items() if h != main and n / TRIALS >= 0.05)
    if others:
        text += " (" + ", ".join(f"{h} — {share:.0%}" for h, share in others) + ")"
    return text


def sword_outcomes(tier: int, enemy: Enemy, enemy_tier: int, crit_chance: float = BASE_CRIT_CHANCE, main_share: float = BALANCED) -> Counter:
    return hits_distribution(grown(enemy.health, enemy_tier), weapon_damage(WARRIOR, tier, main_share), SWORD_CHAIN, crit_chance)


def survival_outcomes(build: Build, tier: int, enemy: Enemy, enemy_tier: int, main_share: float = BALANCED) -> Counter:
    taken = grown(enemy.hit, enemy_tier) * (1 - armor_reduction(armor(build, tier), enemy_tier))
    return hits_distribution(player_health(tier, main_share), taken, (1.0,), 0.0)


def equal_tier_survival(build: Build, tier: int) -> int:
    return typical(survival_outcomes(build, tier, SKELETON, tier))


def full_draws(tier: int, enemy_tier: int) -> int:
    damage = weapon_damage(ARCHER, tier) * BOW_FULL_DRAW
    return typical(hits_distribution(grown(SKELETON.health, enemy_tier), damage, (1.0,), BASE_CRIT_CHANCE))


def fighting_strength(build: Build, tier: int, main_share: float) -> float:
    return player_health(tier, main_share) * weapon_damage(build, tier, main_share)


def fairness(build: Build, tier: int, main_share: float) -> float:
    return fighting_strength(build, tier, main_share) / fighting_strength(build, tier, BALANCED)


def expected_crit_factor() -> float:
    return 1 + BASE_CRIT_CHANCE * (CRIT_MULTIPLIER - 1)


def warrior_boss_seconds() -> float:
    damage = weapon_damage(WARRIOR, BOSS_TIER) * expected_crit_factor()
    chain_average = damage * sum(SWORD_CHAIN) / len(SWORD_CHAIN)
    per_second = (
        chain_average / SWORD_SWING_SECONDS * WARRIOR_ATTACK_UPTIME
        + COUNTER_MULTIPLIER * damage / COUNTER_EVERY_SECONDS
        + WHIRL_MULTIPLIER * damage / WHIRL_EVERY_SECONDS
    )
    return BOSS_HEALTH / per_second


def archer_boss_seconds() -> float:
    damage = weapon_damage(ARCHER, BOSS_TIER) * expected_crit_factor()
    per_second = (
        BOW_FULL_DRAW * damage / BOW_DRAW_SECONDS * ARCHER_ATTACK_UPTIME
        + VOLLEY_ARROWS * VOLLEY_MULTIPLIER * damage / VOLLEY_EVERY_SECONDS
        + PIN_MULTIPLIER * damage / PIN_EVERY_SECONDS
    )
    return BOSS_HEALTH / per_second


def markdown_table(header: list[str], rows: list[list[str]]) -> str:
    lines = ["| " + " | ".join(header) + " |", "|" + "---|" * len(header)]
    lines += ["| " + " | ".join(row) + " |" for row in rows]
    return "\n".join(lines)


def attribute_table() -> str:
    header = ["Редкость"] + [f"Тир {t}" for t in DEPTH_TIERS]
    rows = []
    for name, points, first_tier in RARITIES:
        rows.append([name] + [str(item_attributes(points, t)) if t >= first_tier else "—" for t in DEPTH_TIERS])
    return markdown_table(header, rows)


def typical_table() -> str:
    header = ["Тир", "Очки редкости", "Основной атрибут", "Живучесть: броня + редкость", "Здоровье", "Урон оружия"]
    rows = []
    for tier in DEPTH_TIERS:
        rows.append([
            f"Тир {tier}",
            str(TYPICAL_RARITY_POINTS[tier]),
            f"{main_attribute(tier, BALANCED):.1f}",
            f"{armor_vitality(tier)} + {typical_attributes(tier) * (1 - BALANCED):.1f}",
            str(round(player_health(tier))),
            f"{weapon_damage(WARRIOR, tier):.1f}",
        ])
    return markdown_table(header, rows)


def base_spread_table() -> str:
    header = ["Тир", "Урон оружия, белое", "Броня тяжёлого нагрудника", "Живучесть части брони"]
    rows = []
    for tier in MVP_TIERS:
        weapon_low, weapon_high = base_range(SWORD_DAMAGE, tier)
        chest_low, chest_high = base_range(ARMOR_BY_WEIGHT["heavy"]["chest"], tier)
        rows.append([
            f"Тир {tier}",
            f"{weapon_low:.0f}–{weapon_high:.0f}",
            f"{chest_low:.0f}–{chest_high:.0f}",
            str(round(grown(ARMOR_VITALITY_PER_PIECE, tier))),
        ])
    return markdown_table(header, rows)


def white_sword_table() -> str:
    low, high = base_range(SWORD_DAMAGE, 1)
    health = grown(SKELETON.health, 1)
    worst = hits_distribution(health, low, SWORD_CHAIN, BASE_CRIT_CHANCE)
    best = hits_distribution(health, high, SWORD_CHAIN, BASE_CRIT_CHANCE)
    return markdown_table(
        ["Белый меч тира 1", "Ударов по скелету тира 1"],
        [[f"Худший, {low:.0f}", described(worst, False)], [f"Лучший, {high:.0f}", described(best, False)]],
    )


def sword_table() -> str:
    header = ["Снаряжение"] + [f"Скелет т{t}" for t in MVP_TIERS] + ["Зомби т1"]
    rows = []
    for tier in MVP_TIERS:
        row = [f"Тир {tier}"]
        row += [described(sword_outcomes(tier, SKELETON, et), tier == et) for et in MVP_TIERS]
        row.append(described(sword_outcomes(tier, ZOMBIE, 1), tier == 1))
        rows.append(row)
    return markdown_table(header, rows)


def survival_table() -> str:
    header = ["Игрок", "Здоровье"] + [f"Скелет т{t}" for t in MVP_TIERS]
    rows = []
    for build in (WARRIOR, ARCHER):
        for tier in MVP_TIERS:
            row = [f"{build.name}, тир {tier}", str(round(player_health(tier)))]
            row += [described(survival_outcomes(build, tier, SKELETON, et), tier == et) for et in MVP_TIERS]
            rows.append(row)
    return markdown_table(header, rows)


def depth_table() -> str:
    header = ["Равный тир"] + [f"Тир {t}" for t in DEPTH_TIERS]
    rows = [
        ["Здоровье"] + [str(round(player_health(t))) for t in DEPTH_TIERS],
        ["Воин умирает с"] + [str(equal_tier_survival(WARRIOR, t)) for t in DEPTH_TIERS],
        ["Лучник умирает с"] + [str(equal_tier_survival(ARCHER, t)) for t in DEPTH_TIERS],
        ["Меч убивает скелета с"] + [str(typical(sword_outcomes(t, SKELETON, t))) for t in DEPTH_TIERS],
        ["Лук, полных натяжений"] + [str(full_draws(t, t)) for t in DEPTH_TIERS],
    ]
    return markdown_table(header, rows)


def bow_table() -> str:
    header = ["Лук"] + [f"Скелет т{t}: быстрых / натяжений" for t in MVP_TIERS]
    rows = []
    for tier in MVP_TIERS:
        damage = weapon_damage(ARCHER, tier)
        row = [f"Тир {tier}"]
        for et in MVP_TIERS:
            health = grown(SKELETON.health, et)
            quick = typical(hits_distribution(health, damage * BOW_QUICK_SHOT, (1.0,), BASE_CRIT_CHANCE))
            row.append(f"{quick} / {full_draws(tier, et)}")
        rows.append(row)
    return markdown_table(header, rows)


def fairness_table() -> str:
    tiers = DEPTH_TIERS[1:]
    header = ["Воин"] + [f"Тир {t}" for t in tiers]
    rows = []
    for label, share in (("Баланс", BALANCED), ("Всё в урон", ALL_IN_DAMAGE), ("Всё в Живучесть", ALL_IN_VITALITY)):
        row = [label]
        for tier in tiers:
            dies = typical(survival_outcomes(WARRIOR, tier, SKELETON, tier, share))
            kills = typical(sword_outcomes(tier, SKELETON, tier, main_share=share))
            row.append(f"×{fairness(WARRIOR, tier, share):.2f} · умирает с {dies}, убивает с {kills}")
        rows.append(row)
    return markdown_table(header, rows)


def crit_table() -> str:
    header = ["Шанс крита", "Скелет т1 мечом т1 быстрее 4 ударов", "Зомби т1 мечом т1 быстрее 7 ударов"]
    rows = []
    for chance in CRIT_CHANCES_SHOWN:
        skeleton = share_below(sword_outcomes(1, SKELETON, 1, chance), 4)
        zombie = share_below(sword_outcomes(1, ZOMBIE, 1, chance), 7)
        rows.append([f"{chance:.0%}", f"{skeleton:.0%}", f"{zombie:.0%}"])
    return markdown_table(header, rows)


def block_table() -> str:
    header = ["Равный тир", "Проламывает обычный удар №", "Проламывает тяжёлый удар №", "Сквозь блок / без блока"]
    rows = []
    for tier in MVP_TIERS:
        share, guard = SHIELD_BY_TIER[tier]
        hit = grown(SKELETON.hit, tier)
        reduction = armor_reduction(armor(WARRIOR, tier), tier)
        heavy_guard_loss = hit * HEAVY_HIT * share * HEAVY_GUARD_COST
        rows.append([
            f"Тир {tier}",
            str(math.ceil(guard / (hit * share))),
            str(math.ceil(guard / heavy_guard_loss)),
            f"{round(hit * (1 - share) * (1 - reduction))} / {round(hit * (1 - reduction))}",
        ])
    return markdown_table(header, rows)


def targets() -> list[Target]:
    extremes = tuple(
        round(fairness(build, t, share), 2)
        for build in (WARRIOR, ARCHER)
        for t in DEPTH_TIERS[1:]
        for share in (ALL_IN_DAMAGE, ALL_IN_VITALITY)
    )
    tier_gaps = tuple(round(base_range(SWORD_DAMAGE, t + 1)[0] / base_range(SWORD_DAMAGE, t)[1], 2) for t in DEPTH_TIERS[:-1])
    depth_gains = tuple(
        equal_tier_survival(build, t) - equal_tier_survival(build, 1)
        for build in (WARRIOR, ARCHER)
        for t in DEPTH_TIERS[MVP_TIERS[-1]:]
    )
    return [
        Target("Скелет, меч тира 1: 4–5 ударов", 4, 5, (typical(sword_outcomes(1, SKELETON, 1)),)),
        Target("Зомби, меч тира 1: 7–8", 7, 8, (typical(sword_outcomes(1, ZOMBIE, 1)),)),
        Target("Воин тира 1 от скелета: 5–6", 5, 6, (typical(survival_outcomes(WARRIOR, 1, SKELETON, 1)),)),
        Target("Лучник тира 1 от скелета: 5–6", 5, 6, (typical(survival_outcomes(ARCHER, 1, SKELETON, 1)),)),
        Target("Тот же скелет, меч тира 3: 2–3", 2, 3, (typical(sword_outcomes(3, SKELETON, 1)),)),
        Target("Воин при равном тире, тиры 1–3: 6–7", 6, 7, tuple(equal_tier_survival(WARRIOR, t) for t in MVP_TIERS)),
        Target("Лучник при равном тире, тиры 1–3: 5–6", 5, 6, tuple(equal_tier_survival(ARCHER, t) for t in MVP_TIERS)),
        Target("Тиры 4–5: ударов до смерти больше, чем на тире 1, на 0–2", 0, 2, depth_gains),
        Target("Крайние сборки в пределах ±10% от баланса, тиры 2–5", 1 - FAIRNESS_TOLERANCE, 1 + FAIRNESS_TOLERANCE, extremes),
        Target("Худший меч следующего тира / лучший текущего: больше 1", 1.01, math.inf, tier_gaps),
        Target("Босс, воин: 60–90 с", 60, 90, (round(warrior_boss_seconds()),)),
        Target("Босс, лучник: 60–90 с", 60, 90, (round(archer_boss_seconds()),)),
    ]


def targets_table(checked: list[Target]) -> str:
    rows = [[t.label, t.shown(), "да" if t.met() else "**НЕТ**"] for t in checked]
    return markdown_table(["Цель", "Результат", "Попадает"], rows)


def main() -> int:
    checked = targets()
    sections = [
        ("Атрибутов на предмете", attribute_table()),
        ("Типичный набор", typical_table()),
        ("Разброс базы", base_spread_table()),
        ("Белый меч тира 1", white_sword_table()),
        ("Ударов мечом до смерти врага", sword_table()),
        ("Ударов скелета до смерти игрока", survival_table()),
        ("Равный тир на глубине", depth_table()),
        ("Лук против скелета, типично", bow_table()),
        ("Крайние сборки", fairness_table()),
        ("Что даёт шанс крита", crit_table()),
        ("Блок тяжёлым щитом", block_table()),
        ("Сверка с целями", targets_table(checked)),
    ]
    for title, table in sections:
        print(f"**{title}:**\n\n{table}\n")
    return 0 if all(t.met() for t in checked) else 1


if __name__ == "__main__":
    sys.exit(main())
