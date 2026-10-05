import argparse
import math
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_ASSETS = ROOT / "assets"
IDENTITY = (0.0, 0.0, 0.0, 1.0)


def euler_to_quat(x, y, z):
    cx, sx = math.cos(-x * 0.5), math.sin(-x * 0.5)
    cy, sy = math.cos(-y * 0.5), math.sin(-y * 0.5)
    cz, sz = math.cos(-z * 0.5), math.sin(-z * 0.5)
    return (
        sx * cy * cz - cx * sy * sz,
        cx * sy * cz + sx * cy * sz,
        cx * cy * sz - sx * sy * cz,
        cx * cy * cz + sx * sy * sz,
    )


def quat_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
        aw * bw - ax * bx - ay * by - az * bz,
    )


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def rotate(q, v):
    axis = q[:3]
    t = [2.0 * c for c in cross(axis, v)]
    u = cross(axis, t)
    return tuple(v[i] + q[3] * t[i] + u[i] for i in range(3))


def read_volume(path):
    size = pivot = None
    for line in path.read_text(encoding="utf-8").splitlines():
        words = line.split()
        if words[:1] == ["size"]:
            size = tuple(float(w) for w in words[1:4])
        elif words[:1] == ["pivot"]:
            pivot = tuple(float(w) for w in words[1:4])
        if size and pivot:
            break
    return size, pivot


def read_prefab(assets, name):
    path = assets / "prefabs" / f"{name}.vox"
    rig = None
    nodes = {}
    current = None
    for line in path.read_text(encoding="utf-8").splitlines():
        words = line.split()
        if not words:
            continue
        if words[0] == "rig":
            rig = words[1]
        elif words[0] == "entity":
            current = {"name": words[1], "parent": None, "pos": (0.0, 0.0, 0.0), "rot": IDENTITY,
                       "scale": (1.0, 1.0, 1.0), "model": None, "target": None}
            nodes[words[1]] = current
        elif current is None or line.startswith("\t\t"):
            continue
        elif words[0] == "parent":
            current["parent"] = words[1]
        elif words[0] == "transform":
            values = [float(w) for w in words[1:10]]
            current["pos"] = tuple(values[0:3])
            current["rot"] = euler_to_quat(*values[3:6])
            current["scale"] = tuple(values[6:9])
        elif words[0] == "model":
            current["model"] = words[1]
        elif words[0] == "anim_target":
            current["target"] = words[1]

    parts = {}
    for node in nodes.values():
        if not node["model"] or not node["target"]:
            continue
        size, pivot = read_volume(assets / node["model"])
        if size is None or pivot is None:
            continue
        parent = nodes.get(node["parent"])
        if parent is not None and parent["parent"] is not None:
            print(f"{name}: node {node['name']} is nested deeper than the root; this check assumes parts under the root")
        parts[node["target"]] = {"size": size, "pivot": pivot, "pos": node["pos"], "rot": node["rot"], "scale": node["scale"]}
    return rig, parts


def read_clip(path):
    rig = None
    tracks = {}
    target = channel = None
    for line in path.read_text(encoding="utf-8").splitlines():
        words = line.split()
        if not words:
            continue
        if words[0] == "rig":
            rig = words[1]
        elif words[0] == "track":
            target = words[1]
            tracks[target] = {}
        elif words[0] == "channel":
            channel = words[1]
            if target is not None:
                tracks[target][channel] = []
        elif words[0] == "k" and target is not None and channel in ("position", "rotation", "scale"):
            count = 4 if channel == "rotation" else 3
            values = tuple(float(w) for w in words[2:2 + count])
            tracks[target][channel].append((float(words[1]), values, words[2 + count]))
    return rig, tracks


def eased(t, interp):
    if interp == "step":
        return 0.0
    if interp == "ease_in":
        return t * t
    if interp == "ease_out":
        return 1.0 - (1.0 - t) * (1.0 - t)
    if interp == "ease_in_out":
        return t * t * (3.0 - 2.0 * t)
    return t


def slerp(a, b, t):
    dot = sum(x * y for x, y in zip(a, b))
    if dot < 0.0:
        b = tuple(-x for x in b)
        dot = -dot
    if dot > 0.9995:
        out = tuple(x + (y - x) * t for x, y in zip(a, b))
    else:
        theta = math.acos(dot)
        s = math.sin(theta)
        out = tuple((math.sin((1.0 - t) * theta) * x + math.sin(t * theta) * y) / s for x, y in zip(a, b))
    length = math.sqrt(sum(x * x for x in out))
    return tuple(x / length for x in out)


def sample(keys, time, rest, rotation):
    if not keys:
        return rest
    if time <= keys[0][0]:
        return keys[0][1]
    for (t0, v0, interp), (t1, v1, _) in zip(keys, keys[1:]):
        if t0 <= time <= t1:
            f = eased((time - t0) / (t1 - t0) if t1 > t0 else 0.0, interp)
            return slerp(v0, v1, f) if rotation else tuple(a + (b - a) * f for a, b in zip(v0, v1))
    return keys[-1][1]


def box(part, pos, rot, scale):
    low = [-part["pivot"][i] for i in range(3)]
    high = [part["size"][i] - part["pivot"][i] for i in range(3)]
    centre_local = [(low[i] + high[i]) * 0.5 * scale[i] for i in range(3)]
    half = [(high[i] - low[i]) * 0.5 * abs(scale[i]) for i in range(3)]
    centre = tuple(p + c for p, c in zip(pos, rotate(rot, centre_local)))
    axes = [rotate(rot, e) for e in ((1, 0, 0), (0, 1, 0), (0, 0, 1))]
    return centre, axes, half


def penetration(a, b):
    ca, aa, ha = a
    cb, ab, hb = b
    d = tuple(cb[i] - ca[i] for i in range(3))
    candidates = list(aa) + list(ab)
    for u in aa:
        for v in ab:
            c = cross(u, v)
            if sum(x * x for x in c) > 1e-6:
                candidates.append(c)
    smallest = float("inf")
    for axis in candidates:
        length = math.sqrt(sum(x * x for x in axis))
        axis = tuple(x / length for x in axis)
        ra = sum(ha[i] * abs(sum(aa[i][k] * axis[k] for k in range(3))) for i in range(3))
        rb = sum(hb[i] * abs(sum(ab[i][k] * axis[k] for k in range(3))) for i in range(3))
        overlap = ra + rb - abs(sum(d[k] * axis[k] for k in range(3)))
        if overlap <= 0.0:
            return 0.0
        smallest = min(smallest, overlap)
    return smallest


def check(parts, tracks, tolerance, step):
    duration = max((keys[-1][0] for track in tracks.values() for keys in track.values() if keys), default=0.0)
    worst = {}
    names = sorted(parts)
    steps = int(math.floor(duration / step + 1e-6)) + 1
    for index in range(steps + 1):
        time = min(index * step, duration)
        boxes = {}
        for name in names:
            part = parts[name]
            track = tracks.get(name, {})
            pos = sample(track.get("position", []), time, part["pos"], False)
            rot = sample(track.get("rotation", []), time, part["rot"], True)
            scale = sample(track.get("scale", []), time, part["scale"], False)
            boxes[name] = box(part, pos, rot, scale)
        for i, a in enumerate(names):
            for b in names[i + 1:]:
                depth = penetration(boxes[a], boxes[b])
                if depth > tolerance and ((a, b) not in worst or depth > worst[(a, b)][0]):
                    worst[(a, b)] = (depth, time)
    return duration, worst


def main():
    parser = argparse.ArgumentParser(
        description="Find animation clips in which parts of a jointless rig sink into each other. "
        "Each part is the box of its volume, posed by the clip; a touch within the tolerance is allowed."
    )
    parser.add_argument("clips", nargs="*", help="clip names in assets/animations; every clip of the prefab's rig when omitted")
    parser.add_argument("--prefab", default="p_humanoid", help="prefab that gives the parts, their volumes and rest pose")
    parser.add_argument("--tolerance", type=float, default=1.0, help="allowed depth in voxels, default 1")
    parser.add_argument("--step", type=float, default=0.005, help="sampling step in seconds, default 0.005")
    parser.add_argument("--assets", type=pathlib.Path, default=DEFAULT_ASSETS)
    arguments = parser.parse_args()

    rig, parts = read_prefab(arguments.assets, arguments.prefab)
    folder = arguments.assets / "animations"
    names = arguments.clips or sorted(path.stem for path in folder.glob("*.voxa"))

    failed = False
    for name in names:
        clip_rig, tracks = read_clip(folder / f"{name}.voxa")
        if not arguments.clips and clip_rig != rig:
            continue
        duration, worst = check(parts, tracks, arguments.tolerance, arguments.step)
        if not worst:
            print(f"ok   {name} ({duration:.2f} s)")
            continue
        failed = True
        print(f"FAIL {name} ({duration:.2f} s)")
        for (a, b), (depth, time) in sorted(worst.items(), key=lambda item: -item[1][0]):
            print(f"     {a} into {b}: {depth:.1f} voxels at {time:.3f} s")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
