import base64
import http.client
import json
import pathlib
import shutil
import struct
import sys
import time

SCRATCH_PREFABS = ("_mcp_smoke", "_mcp_smoke_copy")
SCRATCH_CLIP = "_mcp_smoke_clip"
SCRATCH_MACHINE = "_mcp_smoke_fsm"

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 17800
ENDPOINT = "/mcp"
TIMEOUT_SECONDS = 40


class Probe:
    def __init__(self, host, port):
        self.host = host
        self.port = port
        self.failures = []
        self.next_id = 1

    def send(self, method, path, body=None, headers=None):
        connection = http.client.HTTPConnection(self.host, self.port, timeout=TIMEOUT_SECONDS)
        try:
            connection.request(method, path, body=body, headers=headers or {})
            response = connection.getresponse()
            return response.status, dict(response.getheaders()), response.read()
        finally:
            connection.close()

    def post(self, payload, headers=None):
        merged = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream"}
        merged.update(headers or {})
        raw = payload if isinstance(payload, bytes) else json.dumps(payload).encode("utf-8")
        status, _, body = self.send("POST", ENDPOINT, raw, merged)
        return status, json.loads(body) if body else None

    def call(self, method, params=None):
        request = {"jsonrpc": "2.0", "id": self.next_id, "method": method}
        if params is not None:
            request["params"] = params
        self.next_id += 1
        status, reply = self.post(request)
        return status, reply, request["id"]

    def check(self, label, passed, detail=""):
        print(f"{'ok  ' if passed else 'FAIL'} {label}{' -- ' + detail if detail and not passed else ''}")
        if not passed:
            self.failures.append(label)


def run(probe):
    status, reply, sent_id = probe.call(
        "initialize",
        {
            "protocolVersion": "2025-06-18",
            "capabilities": {},
            "clientInfo": {"name": "mcp_smoke", "version": "1"},
        },
    )
    result = (reply or {}).get("result", {})
    probe.check("initialize answers 200", status == 200, str(status))
    probe.check("initialize echoes the id", (reply or {}).get("id") == sent_id, str(reply))
    probe.check("initialize keeps the protocol asked for", result.get("protocolVersion") == "2025-06-18", str(result))
    probe.check("initialize names the server", result.get("serverInfo", {}).get("name") == "sculptor", str(result))
    probe.check("initialize offers tools", "tools" in result.get("capabilities", {}), str(result))

    status, reply, _ = probe.call("initialize", {"protocolVersion": "1999-01-01"})
    offered = (reply or {}).get("result", {}).get("protocolVersion")
    probe.check("initialize offers a known protocol for an unknown one", offered == "2025-06-18", str(offered))

    status, reply = probe.post({"jsonrpc": "2.0", "method": "notifications/initialized"})
    probe.check("a notification gets 202 and no body", status == 202 and reply is None, f"{status} {reply}")

    status, reply, _ = probe.call("ping")
    probe.check("ping answers an empty result", status == 200 and (reply or {}).get("result") == {}, str(reply))

    status, reply, _ = probe.call("tools/list")
    tools = (reply or {}).get("result", {}).get("tools", [])
    names = [tool.get("name") for tool in tools]
    probe.check("tools/list has editor_state", "editor_state" in names, str(names))
    probe.check(
        "every tool has a description and an object schema",
        all(tool.get("description") and tool.get("inputSchema", {}).get("type") == "object" for tool in tools),
        str(tools),
    )

    status, reply, _ = probe.call("tools/call", {"name": "editor_state", "arguments": {}})
    result = (reply or {}).get("result", {})
    content = result.get("content", [])
    probe.check("editor_state succeeds", status == 200 and result.get("isError") is False, str(reply))
    probe.check("editor_state returns one text block", len(content) == 1 and content[0].get("type") == "text", str(content))
    state = json.loads(content[0]["text"]) if content else {}
    probe.check(
        "editor_state reports the editor",
        all(key in state for key in ("prefab", "context", "context_stack", "unsaved", "busy", "window_visible")),
        str(state),
    )
    print(f"     editor_state: {json.dumps(state, ensure_ascii=False)}")

    status, reply, _ = probe.call("tools/call", {"name": "editor_state"})
    probe.check("a tool call may omit arguments", (reply or {}).get("result", {}).get("isError") is False, str(reply))

    status, reply, _ = probe.call("tools/call", {"name": "no_such_tool", "arguments": {}})
    probe.check("an unknown tool is invalid params", (reply or {}).get("error", {}).get("code") == -32602, str(reply))

    status, reply, _ = probe.call("tools/call", {"arguments": {}})
    probe.check("a call without a name is invalid params", (reply or {}).get("error", {}).get("code") == -32602, str(reply))

    status, reply, _ = probe.call("no/such/method")
    probe.check("an unknown method is method not found", (reply or {}).get("error", {}).get("code") == -32601, str(reply))

    status, reply = probe.post(b'{"jsonrpc": "2.0", "id": 1, "method": ')
    probe.check(
        "broken JSON is a parse error",
        status == 400 and (reply or {}).get("error", {}).get("code") == -32700,
        f"{status} {reply}",
    )

    status, reply = probe.post([{"jsonrpc": "2.0", "id": 1, "method": "ping"}])
    probe.check(
        "a batch is an invalid request",
        status == 400 and (reply or {}).get("error", {}).get("code") == -32600,
        f"{status} {reply}",
    )

    status, reply = probe.post({"jsonrpc": "2.0", "id": "text-id", "method": "ping"})
    probe.check("a string id comes back as a string", (reply or {}).get("id") == "text-id", str(reply))

    status, headers, _ = probe.send("GET", ENDPOINT)
    probe.check("GET is refused with Allow: POST", status == 405 and headers.get("Allow") == "POST", f"{status} {headers}")

    status, _, _ = probe.send("POST", "/elsewhere", b"{}", {"Content-Type": "application/json"})
    probe.check("another path is 404", status == 404, str(status))

    ping = {"jsonrpc": "2.0", "id": 1, "method": "ping"}

    status, _ = probe.post(ping, {"Origin": "http://localhost:3000"})
    probe.check("a local origin is served", status == 200, str(status))

    status, _, _ = probe.send(
        "POST", ENDPOINT, json.dumps(ping).encode("utf-8"), {"Origin": "https://example.com"}
    )
    probe.check("a foreign origin is 403", status == 403, str(status))

    status, _, _ = probe.send(
        "POST", ENDPOINT, json.dumps(ping).encode("utf-8"), {"Host": "evil.example:17800"}
    )
    probe.check("a foreign host is 403", status == 403, str(status))

    padding = "x" * 200_000
    status, reply = probe.post({"jsonrpc": "2.0", "id": 7, "method": "ping", "params": {"padding": padding}})
    probe.check("a 200 KB request is read whole", status == 200 and (reply or {}).get("id") == 7, str(status))


def tool(probe, name, arguments=None):
    _, reply, _ = probe.call("tools/call", {"name": name, "arguments": arguments or {}})
    result = (reply or {}).get("result", {})
    content = result.get("content", [])
    text = content[0].get("text", "") if content else json.dumps(reply)
    if result.get("isError") is False:
        return True, json.loads(text)
    return False, text


def remove_scratch_assets(asset_root):
    for name in SCRATCH_PREFABS:
        (asset_root / "prefabs" / f"{name}.vox").unlink(missing_ok=True)
        shutil.rmtree(asset_root / "models" / name, ignore_errors=True)
    (asset_root / "animations" / f"{SCRATCH_CLIP}.voxa").unlink(missing_ok=True)
    (asset_root / "fsm" / f"{SCRATCH_MACHINE}.voxf").unlink(missing_ok=True)


def run_prefab_scenario(probe):
    ok, state = tool(probe, "editor_state")
    probe.check("the startup dialog does not make the editor busy", ok and state.get("busy") is None, str(state))
    asset_root = pathlib.Path(state["asset_root"])
    remove_scratch_assets(asset_root)

    ok, _ = tool(probe, "prefab_close", {"discard_unsaved": True})

    ok, assets = tool(probe, "assets_list")
    probe.check("assets_list has the humanoid prefab", ok and "p_humanoid.vox" in assets.get("prefabs", []), str(assets))
    probe.check(
        "assets_list has its volumes, clips and machines",
        ok
        and "models/p_humanoid/m_head.voxm" in assets.get("volumes", [])
        and "animations/a_humanoid_idle.voxa" in assets.get("clips", [])
        and "fsm/humanoid_locomotion.voxf" in assets.get("machines", []),
        str(assets),
    )

    ok, palette = tool(probe, "palette_list")
    names = {voxel["name"]: voxel for voxel in palette.get("voxels", [])} if ok else {}
    probe.check("palette_list has more than sixty voxels", len(names) > 60, str(len(names)))
    probe.check("palette_list gives white as #rrggbb", names.get("white", {}).get("color", "").startswith("#"), str(names.get("white")))
    probe.check("palette_list marks glowing voxels", "glow" in names.get("glow_blue", {}), str(names.get("glow_blue")))

    ok, text = tool(probe, "prefab_get")
    probe.check("prefab_get refuses when nothing is open", not ok and "no prefab is open" in text, str(text))

    ok, text = tool(probe, "prefab_open", {"name": "no_such_prefab"})
    probe.check("prefab_open names the prefabs it does have", not ok and "p_humanoid.vox" in text, str(text))

    ok, text = tool(probe, "prefab_open", {"name": 5})
    probe.check("prefab_open names the field of a wrong type", not ok and "arguments.name" in text, str(text))

    ok, summary = tool(probe, "prefab_open", {"name": "p_humanoid"})
    probe.check("prefab_open opens the humanoid", ok and summary.get("node_count") == 7, str(summary))

    ok, state = tool(probe, "editor_state")
    probe.check(
        "editor_state names the open prefab and is idle",
        ok and state.get("prefab") == "p_humanoid.vox" and state.get("busy") is None and state.get("can_undo") is False,
        str(state),
    )

    ok, prefab = tool(probe, "prefab_get")
    nodes = {node["name"]: node for node in prefab.get("nodes", [])} if ok else {}
    head = nodes.get("head", {})
    probe.check("prefab_get lists seven nodes under the root", len(nodes) == 7 and prefab.get("root_node") == "root", str(list(nodes)))
    probe.check("prefab_get gives the rig and two machines", prefab.get("rig") == "humanoid" and len(prefab.get("machines", [])) == 2, str(prefab.get("machines")))
    probe.check(
        "prefab_get places the head",
        head.get("parent") == "root" and head.get("position") == [0, 22, 0] and head.get("scale") == [1, 1, 1],
        str(head),
    )
    probe.check(
        "prefab_get describes the volume of the head",
        head.get("volume", {}).get("ref") == "models/p_humanoid/m_head.voxm" and len(head.get("volume", {}).get("size", [])) == 3,
        str(head.get("volume")),
    )
    probe.check(
        "prefab_get gives the other components typed",
        head.get("anim_target") == "head" and head.get("variant", {}).get("selected") == 0,
        str(head),
    )
    print(f"     head: {json.dumps(head, ensure_ascii=False)}")

    ok, text = tool(probe, "undo")
    probe.check("undo refuses with an empty history", not ok and "nothing to undo" in text, str(text))
    ok, text = tool(probe, "redo")
    probe.check("redo refuses with an empty history", not ok and "nothing to redo" in text, str(text))

    ok, text = tool(probe, "prefab_new", {"name": "sub/dir"})
    probe.check("prefab_new refuses a path", not ok and "cannot name a prefab" in text, str(text))

    ok, summary = tool(probe, "prefab_new", {"name": SCRATCH_PREFABS[0]})
    created = asset_root / "prefabs" / f"{SCRATCH_PREFABS[0]}.vox"
    probe.check("prefab_new creates and opens an empty prefab", ok and summary.get("node_count") == 0 and created.is_file(), str(summary))

    ok, text = tool(probe, "prefab_new", {"name": SCRATCH_PREFABS[0]})
    probe.check("prefab_new refuses an existing file", not ok and "overwrite: true" in text, str(text))

    ok, summary = tool(probe, "prefab_new", {"name": f"{SCRATCH_PREFABS[0]}.vox", "overwrite": True})
    probe.check("prefab_new overwrites when told to", ok and summary.get("prefab") == f"{SCRATCH_PREFABS[0]}.vox", str(summary))

    ok, text = tool(probe, "prefab_save")
    probe.check("prefab_save refuses a prefab without nodes", not ok and "root node" in text, str(text))

    ok, summary = tool(probe, "prefab_close")
    probe.check("prefab_close leaves the editor empty", ok and summary.get("prefab") is None, str(summary))

    ok, text = tool(probe, "prefab_close")
    probe.check("prefab_close refuses when nothing is open", not ok and "no prefab is open" in text, str(text))

    ok, _ = tool(probe, "prefab_open", {"name": "p_humanoid.vox"})
    ok, summary = tool(probe, "prefab_save_as", {"name": SCRATCH_PREFABS[1]})
    copied_volumes = sorted(path.name for path in (asset_root / "models" / SCRATCH_PREFABS[1]).glob("*.voxm"))
    probe.check("prefab_save_as switches to the copy", ok and summary.get("prefab") == f"{SCRATCH_PREFABS[1]}.vox", str(summary))
    probe.check("prefab_save_as carries six volumes into its own folder", len(copied_volumes) == 6, str(copied_volumes))

    ok, prefab = tool(probe, "prefab_get")
    refs = [node.get("volume", {}).get("ref", "") for node in prefab.get("nodes", []) if "volume" in node] if ok else []
    probe.check(
        "the copy points at its own volumes",
        len(refs) == 6 and all(ref.startswith(f"models/{SCRATCH_PREFABS[1]}/") for ref in refs),
        str(refs),
    )

    ok, text = tool(probe, "prefab_save_as", {"name": "p_humanoid"})
    probe.check("prefab_save_as refuses an existing file", not ok and "overwrite: true" in text, str(text))

    ok, summary = tool(probe, "prefab_save")
    probe.check("prefab_save writes the open prefab", ok and summary.get("unsaved") is False, str(summary))

    tool(probe, "prefab_close", {"discard_unsaved": True})
    remove_scratch_assets(asset_root)


def close_to(left, right, tolerance=1e-3):
    if isinstance(left, bool) or isinstance(right, bool):
        return left == right
    if isinstance(left, (int, float)) and isinstance(right, (int, float)):
        return abs(left - right) <= tolerance
    if isinstance(left, list) and isinstance(right, list):
        return len(left) == len(right) and all(close_to(a, b, tolerance) for a, b in zip(left, right))
    if isinstance(left, dict) and isinstance(right, dict):
        return left.keys() == right.keys() and all(close_to(left[key], right[key], tolerance) for key in left)
    return left == right


def nodes_of(probe):
    ok, prefab = tool(probe, "prefab_get")
    return {node["name"]: node for node in prefab.get("nodes", [])} if ok else {}


def run_node_scenario(probe):
    scratch = SCRATCH_PREFABS[0]
    ok, state = tool(probe, "editor_state")
    asset_root = pathlib.Path(state["asset_root"])
    remove_scratch_assets(asset_root)
    tool(probe, "prefab_close", {"discard_unsaved": True})

    ok, text = tool(probe, "node_create", {"name": "root"})
    probe.check("node_create refuses without a prefab", not ok and "no prefab is open" in text, str(text))

    tool(probe, "prefab_new", {"name": scratch})

    ok, text = tool(probe, "node_create", {"name": "body", "parent": "root"})
    probe.check("the first node takes no parent", not ok and "without a parent" in text, str(text))

    ok, root = tool(probe, "node_create", {"name": "root"})
    probe.check("node_create makes the root", ok and root.get("parent") is None and root.get("position") == [0, 0, 0], str(root))

    ok, text = tool(probe, "node_create", {"name": "root"})
    probe.check("node_create refuses a taken name", not ok and "already exists" in text, str(text))

    ok, text = tool(probe, "node_create", {"name": "body"})
    probe.check("node_create asks for the parent and names the root", not ok and "'root'" in text, str(text))

    ok, text = tool(probe, "node_create", {"name": "two words", "parent": "root"})
    probe.check("node_create refuses a name with a space", not ok and "cannot name a node" in text, str(text))

    ok, text = tool(probe, "node_create", {"nam": "body", "parent": "root"})
    probe.check("an unknown field is named", not ok and "unknown field 'nam'" in text, str(text))

    ok, text = tool(probe, "node_create", {"name": "body", "parent": "root", "position": [1, 2]})
    probe.check("a short vector is refused with its path", not ok and "arguments.position" in text, str(text))

    ok, text = tool(probe, "node_create", {"name": "body", "parent": "root", "volume": {"size": [0, 4, 4]}})
    probe.check("a volume side of zero is refused", not ok and "volume.size" in text, str(text))
    probe.check("a refused create leaves nothing behind", "body" not in nodes_of(probe), str(list(nodes_of(probe))))

    ok, body = tool(
        probe,
        "node_create",
        {"name": "body", "parent": "root", "position": [0, 2, 0], "volume": {"size": [4, 6, 3]}, "anim_target": True},
    )
    probe.check(
        "node_create places a node with an empty volume",
        ok and body.get("position") == [0, 2, 0] and body.get("volume", {}).get("size") == [4, 6, 3],
        str(body),
    )
    probe.check("a new volume has its pivot in the middle", ok and body.get("volume", {}).get("pivot") == [2, 3, 1.5], str(body))
    probe.check("anim_target true takes the node name", ok and body.get("anim_target") == "body", str(body))

    ok, head = tool(
        probe,
        "node_create",
        {
            "name": "head",
            "parent": "body",
            "position": [0, 5, 0],
            "rotation_degrees": [10, 20, 30],
            "scale": [0.99, 0.99, 0.99],
            "volume": {"ref": "models/p_humanoid/m_head.voxm"},
            "sockets": [{"name": "hat", "position": [0, 4, 0], "rotation_degrees": [0, 45, 0]}],
        },
    )
    probe.check("rotation in degrees comes back as given", ok and close_to(head.get("rotation_degrees"), [10, 20, 30]), str(head))
    probe.check("a float32 is written short", ok and head.get("scale") == [0.99, 0.99, 0.99], str(head))
    probe.check("node_create attaches a volume file", ok and head.get("volume", {}).get("size") == [11, 9, 10], str(head))
    probe.check(
        "node_create adds a socket",
        ok and len(head.get("sockets", [])) == 1 and close_to(head["sockets"][0].get("rotation_degrees"), [0, 45, 0]),
        str(head.get("sockets")),
    )

    ok, text = tool(probe, "node_set_transform", {"name": "hat", "position": [0, 0, 0]})
    probe.check("an unknown node is refused with the list of nodes", not ok and "body, head, root" in text, str(text))

    ok, text = tool(probe, "node_set_transform", {"name": "head"})
    probe.check("node_set_transform wants something to set", not ok and "at least one" in text, str(text))

    ok, moved = tool(probe, "node_set_transform", {"name": "head", "position": [0, 6, 0]})
    probe.check(
        "node_set_transform changes one field and keeps the rest",
        ok and moved.get("position") == [0, 6, 0] and close_to(moved.get("rotation_degrees"), [10, 20, 30]),
        str(moved),
    )

    ok, text = tool(probe, "node_set_components", {"name": "head"})
    probe.check("node_set_components wants a component", not ok and "at least one component" in text, str(text))

    ok, text = tool(probe, "node_set_components", {"name": "head", "anim_target": "body"})
    probe.check("a taken animation target is refused", not ok and "already taken by node 'body'" in text, str(text))

    ok, changed = tool(
        probe,
        "node_set_components",
        {
            "name": "head",
            "anim_target": "head",
            "sockets": [{"name": "hat", "position": [0, 5, 0]}, {"name": "mask"}],
            "structure": {"type": "house", "races": ["human", "elf"], "tier": 2, "size": "M"},
            "furniture": "chair",
            "connection": "door",
            "variant": {"candidates": ["models/p_humanoid/m_head.voxm"], "selected": 0},
        },
    )
    sockets = {socket["name"]: socket for socket in changed.get("sockets", [])} if ok else {}
    probe.check("sockets are replaced as a list", ok and set(sockets) == {"hat", "mask"} and sockets["hat"]["position"] == [0, 5, 0], str(sockets))
    probe.check(
        "structure, points and the target are set together",
        ok
        and changed.get("structure") == {"type": "house", "races": ["human", "elf"], "tier": 2, "size": "M"}
        and changed.get("furniture") == "chair"
        and changed.get("connection") == "door"
        and changed.get("anim_target") == "head",
        str(changed),
    )
    probe.check(
        "a variant slot takes its candidate",
        ok and changed.get("variant", {}).get("candidates") == ["models/p_humanoid/m_head.voxm"] and changed["variant"].get("selected") == 0,
        str(changed.get("variant")),
    )

    ok, text = tool(probe, "node_set_components", {"name": "head", "variant": {"candidates": ["models/none.voxm"]}})
    probe.check("a variant candidate that is not a file is refused", not ok and "there is no file" in text, str(text))

    ok, text = tool(probe, "node_set_components", {"name": "head", "structure": {"tier": 9}})
    probe.check("a structure tier out of range is refused", not ok and "structure.tier" in text, str(text))

    ok, _ = tool(probe, "undo")
    probe.check("one undo takes back the whole component call", ok and "structure" not in nodes_of(probe).get("head", {}), str(nodes_of(probe).get("head")))
    ok, _ = tool(probe, "redo")
    probe.check("redo brings it back", ok and nodes_of(probe).get("head", {}).get("furniture") == "chair", str(nodes_of(probe).get("head")))

    ok, cleared = tool(
        probe,
        "node_set_components",
        {"name": "head", "sockets": None, "structure": None, "furniture": None, "connection": None, "variant": None},
    )
    probe.check(
        "null removes components",
        ok and not any(key in cleared for key in ("sockets", "structure", "furniture", "connection", "variant")),
        str(cleared),
    )

    ok, text = tool(probe, "node_move", {"name": "root", "parent": "head"})
    probe.check("the root cannot be moved", not ok and "root node" in text, str(text))

    ok, text = tool(probe, "node_move", {"name": "body", "parent": "head"})
    probe.check("a node cannot go under its own descendant", not ok and "descendants" in text, str(text))

    ok, lifted = tool(probe, "node_move", {"name": "head", "parent": "root", "index": 0})
    probe.check(
        "node_move keeps the node where it was in the world",
        ok and lifted.get("parent") == "root" and close_to(lifted.get("position"), [0, 8, 0]),
        str(lifted),
    )

    ok, prefab = tool(probe, "prefab_get")
    order = [node["name"] for node in prefab.get("nodes", [])] if ok else []
    probe.check("node_move puts it first among the children", order == ["root", "head", "body"], str(order))

    tool(probe, "node_move", {"name": "head", "parent": "body"})
    tool(probe, "prefab_set_rig", {"rig": "smoke_rig"})
    tool(probe, "node_create", {"name": "hand", "parent": "head", "position": [3, 0, 0], "volume": {"size": [2, 2, 2]}})
    before = nodes_of(probe)

    ok, gone = tool(probe, "node_delete", {"name": "body"})
    probe.check("node_delete takes the subtree with it", ok and gone.get("node_count") == 1, str(gone))

    ok, _ = tool(probe, "undo")
    after = nodes_of(probe)
    probe.check("undo restores every node of the subtree", set(after) == set(before), str(list(after)))
    probe.check("undo restores them exactly", close_to(after, before, 1e-6), json.dumps(after))

    ok, text = tool(probe, "prefab_open", {"name": "p_humanoid"})
    probe.check("prefab_open refuses over unsaved changes and names them", not ok and "the prefab" in text and "discard_unsaved" in text, str(text))

    ok, saved = tool(probe, "prefab_save")
    volumes = sorted(path.name for path in (asset_root / "models" / scratch).glob("*.voxm"))
    probe.check("prefab_save writes the prefab", ok and saved.get("unsaved") is False, str(saved))
    probe.check("new volumes are written into the folder of the prefab", volumes == ["m_body.voxm", "m_hand.voxm"], str(volumes))

    in_memory = nodes_of(probe)
    ok, prefab = tool(probe, "prefab_get")
    probe.check("prefab_set_rig names the rig", ok and prefab.get("rig") == "smoke_rig", str(prefab.get("rig")))

    tool(probe, "prefab_close")
    ok, _ = tool(probe, "prefab_open", {"name": scratch})
    from_disk = nodes_of(probe)
    probe.check("the saved prefab reads back the same from disk", close_to(from_disk, in_memory), json.dumps(from_disk))

    ok, gone = tool(probe, "node_delete", {"name": "root"})
    probe.check("deleting the root empties the prefab", ok and gone.get("node_count") == 0 and gone.get("root_node") is None, str(gone))
    ok, _ = tool(probe, "undo")
    probe.check("and undo brings the prefab back", close_to(nodes_of(probe), in_memory), json.dumps(nodes_of(probe)))

    tool(probe, "prefab_close", {"discard_unsaved": True})
    remove_scratch_assets(asset_root)


def run_volume_scenario(probe):
    scratch = SCRATCH_PREFABS[0]
    ok, state = tool(probe, "editor_state")
    asset_root = pathlib.Path(state["asset_root"])
    remove_scratch_assets(asset_root)
    tool(probe, "prefab_close", {"discard_unsaved": True})

    tool(probe, "prefab_new", {"name": scratch})
    tool(probe, "node_create", {"name": "root"})
    tool(probe, "node_create", {"name": "box", "parent": "root", "volume": {"size": [4, 3, 2]}})

    ok, text = tool(probe, "volume_get", {"node": "root"})
    probe.check("volume_get refuses a node without a volume", not ok and "has no volume" in text, str(text))

    ok, empty = tool(probe, "volume_get", {"node": "box"})
    probe.check(
        "volume_get describes an empty volume",
        ok and empty.get("size") == [4, 3, 2] and empty.get("pivot") == [2, 1.5, 1] and empty.get("occupied") is None
        and empty.get("voxel_count") == 0 and empty.get("layers") is None,
        str(empty),
    )

    ok, floor = tool(probe, "volume_write", {"node": "box", "boxes": [{"min": [0, 0, 0], "max": [3, 0, 1], "voxel": "gray_10"}]})
    probe.check("volume_write fills a box", ok and floor.get("voxel_count") == 8 and floor.get("cells_written") == 8, str(floor))

    ok, state = tool(probe, "editor_state")
    probe.check(
        "volume_write opens the volume in the editor",
        ok and state.get("context") == "volume" and state.get("context_stack") == [{"kind": "volume", "node": "box"}],
        str(state.get("context_stack")),
    )

    ok, dotted = tool(probe, "volume_write", {"node": "box", "points": [{"voxel": "red_4", "at": [[1, 1, 0], [2, 1, 0]]}]})
    probe.check("volume_write sets points", ok and dotted.get("voxel_count") == 10, str(dotted))

    ok, read = tool(probe, "volume_get", {"node": "box"})
    layers = read.get("layers", {}) if ok else {}
    probe.check("volume_get bounds the occupied voxels", ok and read.get("occupied") == {"min": [0, 0, 0], "max": [3, 1, 1]}, str(read.get("occupied")))
    probe.check("the legend puts the most common voxel first", layers.get("legend") == {"a": "gray_10", "b": "red_4"}, str(layers.get("legend")))
    probe.check(
        "layers are slices of y, rows of z, characters of x",
        layers.get("origin") == [0, 0, 0] and layers.get("slices") == [["aaaa", "aaaa"], [".bb.", "...."]],
        str(layers.get("slices")),
    )

    ok, drawn = tool(
        probe,
        "volume_write",
        {"node": "box", "layers": {"origin": [0, 2, 0], "legend": {"x": "white"}, "slices": [["x..x", "...."]]}},
    )
    probe.check("volume_write draws layers", ok and drawn.get("voxel_count") == 12, str(drawn))

    ok, top = tool(probe, "volume_get", {"node": "box", "min": [0, 2, 0], "max": [3, 2, 1]})
    probe.check(
        "volume_get returns the region asked for",
        ok and top.get("layers", {}).get("origin") == [0, 2, 0] and top["layers"].get("slices") == [["a..a", "...."]]
        and top["layers"].get("legend") == {"a": "white"},
        str(top.get("layers")),
    )

    ok, back = tool(probe, "volume_write", {"node": "box", "layers": read["layers"]})
    ok, again = tool(probe, "volume_get", {"node": "box", "min": [0, 0, 0], "max": [3, 1, 1]})
    probe.check("what volume_get returns can be written back", ok and again.get("layers") == read["layers"], str(again.get("layers")))

    ok, text = tool(probe, "volume_write", {"node": "box", "layers": {"legend": {}, "slices": [["...."]], "air": "keep"}})
    probe.check("air kept writes nothing", not ok and "nothing to write" in text, str(text))

    ok, erased = tool(probe, "volume_write", {"node": "box", "layers": {"legend": {"g": "gray_10"}, "slices": [[".ggg"]]}})
    probe.check("air erases by default", ok and erased.get("voxel_count") == 11, str(erased))

    ok, text = tool(probe, "volume_write", {"node": "box", "points": [{"voxel": "white", "at": [[4, 0, 0]]}]})
    probe.check("a position outside the volume is refused", not ok and "outside the volume" in text and "volume_reshape" in text, str(text))

    ok, text = tool(probe, "volume_write", {"node": "box", "points": [{"voxel": "no_such", "at": [[0, 0, 0]]}]})
    probe.check("an unknown voxel is refused", not ok and "no voxel named 'no_such'" in text, str(text))

    ok, text = tool(probe, "volume_write", {"node": "box", "layers": {"legend": {"a": "white"}, "slices": [["ab"]]}})
    probe.check("a character missing from the legend is refused", not ok and "not in the legend" in text, str(text))

    ok, text = tool(probe, "volume_write", {"node": "box", "boxes": [{"min": [2, 0, 0], "max": [1, 0, 0], "voxel": "white"}]})
    probe.check("a box turned inside out is refused", not ok and "min must not exceed max" in text, str(text))

    ok, text = tool(probe, "volume_write", {"node": "box"})
    probe.check("volume_write wants something to write", not ok and "at least one" in text, str(text))

    ok, same = tool(probe, "volume_get", {"node": "box"})
    probe.check("a refused write changes nothing", ok and same.get("voxel_count") == 11, str(same.get("voxel_count")))

    tool(probe, "undo")
    ok, undone = tool(probe, "volume_get", {"node": "box"})
    probe.check("undo takes back one write", ok and undone.get("voxel_count") == 12, str(undone.get("voxel_count")))
    tool(probe, "redo")

    ok, grown = tool(probe, "volume_reshape", {"node": "box", "resize": {"min": [1, 0, 0], "max": [0, 1, 0]}})
    probe.check(
        "resize grows the volume and reports the shift",
        ok and grown.get("size") == [5, 4, 2] and grown.get("shift") == [1, 0, 0] and grown.get("pivot") == [3, 1.5, 1]
        and grown.get("voxel_count") == 11,
        str(grown),
    )
    probe.check("the voxels moved by the shift", grown.get("occupied") == {"min": [1, 0, 0], "max": [4, 2, 1]}, str(grown.get("occupied")))

    ok, text = tool(probe, "volume_reshape", {"node": "box", "resize": {"min": [-9, 0, 0]}})
    probe.check("resize refuses a side below one", not ok and "within 1.." in text, str(text))

    ok, trimmed = tool(probe, "volume_reshape", {"node": "box", "trim": True})
    probe.check(
        "trim cuts the volume to its voxels",
        ok and trimmed.get("size") == [4, 3, 2] and trimmed.get("shift") == [-1, 0, 0] and trimmed.get("voxel_count") == 11,
        str(trimmed),
    )

    ok, turned = tool(probe, "volume_reshape", {"node": "box", "rotate": {"axis": "y", "quarter_turns": 1}})
    probe.check("rotate swaps the sides", ok and turned.get("size") == [2, 3, 4] and turned.get("voxel_count") == 11 and "note" in turned, str(turned))

    ok, flipped = tool(probe, "volume_reshape", {"node": "box", "mirror": "x"})
    probe.check("mirror keeps the size and the voxels", ok and flipped.get("size") == [2, 3, 4] and flipped.get("voxel_count") == 11, str(flipped))

    ok, text = tool(probe, "volume_reshape", {"node": "box", "trim": True, "mirror": "x"})
    probe.check("volume_reshape takes one action", not ok and "exactly one" in text, str(text))

    ok, moved = tool(probe, "volume_set_pivot", {"node": "box", "pivot": [0, 0.5, 0]})
    probe.check("volume_set_pivot moves the pivot", ok and moved.get("pivot") == [0, 0.5, 0], str(moved))

    tool(probe, "node_set_transform", {"name": "box", "position": [0, 1, 0]})
    ok, state = tool(probe, "editor_state")
    probe.check("a node tool returns the editor to the prefab", ok and state.get("context") == "prefab", str(state.get("context_stack")))

    ok, text = tool(probe, "volume_rename", {"node": "box", "name": "crate"})
    probe.check("a volume without a file cannot be renamed", not ok and "save the prefab first" in text, str(text))

    tool(probe, "prefab_save")
    ok, twin = tool(probe, "node_create", {"name": "twin", "parent": "root", "volume": {"ref": f"models/{scratch}/m_box.voxm"}})
    ok, shared = tool(probe, "volume_get", {"node": "box"})
    probe.check("volume_get names the nodes that share the volume", ok and shared.get("shared_with") == ["twin"], str(shared.get("shared_with")))

    tool(probe, "volume_write", {"node": "twin", "points": [{"voxel": "air", "at": [[0, 0, 0], [0, 0, 1], [1, 0, 0], [1, 0, 1]]}]})
    ok, of_box = tool(probe, "volume_get", {"node": "box"})
    ok, of_twin = tool(probe, "volume_get", {"node": "twin"})
    probe.check(
        "a write through one node shows on the other",
        of_box.get("voxel_count") == of_twin.get("voxel_count") and of_box.get("layers") == of_twin.get("layers") and of_box.get("voxel_count") < 11,
        f"{of_box.get('voxel_count')} {of_twin.get('voxel_count')}",
    )

    ok, text = tool(probe, "volume_rename", {"node": "box", "name": "a/b"})
    probe.check("volume_rename refuses a path", not ok and "cannot name a volume file" in text, str(text))

    ok, renamed = tool(probe, "volume_rename", {"node": "box", "name": "crate"})
    models = asset_root / "models" / scratch
    probe.check(
        "volume_rename moves the file and both nodes follow",
        ok and renamed.get("ref") == f"models/{scratch}/crate.voxm" and (models / "crate.voxm").is_file()
        and not (models / "m_box.voxm").exists() and renamed.get("shared_with") == ["twin"],
        str(renamed),
    )

    ok, in_memory = tool(probe, "volume_get", {"node": "twin"})
    tool(probe, "prefab_save")
    tool(probe, "prefab_close")
    tool(probe, "prefab_open", {"name": scratch})
    ok, from_disk = tool(probe, "volume_get", {"node": "twin"})
    probe.check("the saved volume reads back the same from disk", ok and close_to(from_disk, in_memory), json.dumps(from_disk))

    tool(probe, "node_create", {"name": "slab", "parent": "root", "volume": {"size": [64, 16, 64]}})
    tool(probe, "volume_write", {"node": "slab", "boxes": [{"min": [0, 0, 0], "max": [63, 15, 63], "voxel": 40}]})
    ok, big = tool(probe, "volume_get", {"node": "slab"})
    probe.check(
        "a region too large for one answer is cut with a hint",
        ok and big.get("voxel_count") == 64 * 16 * 64 and "layers" not in big and "min and max" in big.get("truncated", ""),
        str({key: value for key, value in big.items() if key != "layers"}),
    )
    ok, part = tool(probe, "volume_get", {"node": "slab", "min": [0, 0, 0], "max": [7, 0, 0]})
    probe.check("and a part of it is returned", ok and part.get("layers", {}).get("slices") == [["aaaaaaaa"]], str(part.get("layers")))

    tool(probe, "prefab_close", {"discard_unsaved": True})
    remove_scratch_assets(asset_root)


def picture(probe, arguments=None):
    _, reply, _ = probe.call("tools/call", {"name": "view_screenshot", "arguments": arguments or {}})
    result = (reply or {}).get("result", {})
    images = [block for block in result.get("content", []) if block.get("type") == "image"]
    texts = [block.get("text", "") for block in result.get("content", []) if block.get("type") == "text"]
    if result.get("isError") is not False or not images:
        return None, texts[0] if texts else json.dumps(reply)
    return base64.b64decode(images[0]["data"]), json.loads(texts[0])


def png_size(data):
    return struct.unpack(">II", data[16:24])


def run_view_scenario(probe):
    scratch = SCRATCH_PREFABS[0]
    ok, state = tool(probe, "editor_state")
    asset_root = pathlib.Path(state["asset_root"])
    window_visible = state.get("window_visible")
    remove_scratch_assets(asset_root)
    tool(probe, "prefab_close", {"discard_unsaved": True})

    ok, text = tool(probe, "view_set", {})
    probe.check("view_set refuses with nothing open", not ok and "nothing to look at" in text, str(text))

    tool(probe, "prefab_new", {"name": scratch})
    tool(probe, "node_create", {"name": "root"})
    tool(probe, "node_create", {"name": "cube", "parent": "root", "position": [10, 0, 0], "volume": {"size": [4, 4, 4]}})
    tool(probe, "volume_write", {"node": "cube", "boxes": [{"min": [0, 0, 0], "max": [3, 3, 3], "voxel": "red_6"}]})

    ok, view = tool(probe, "view_set", {"node": "cube", "from": "+x"})
    probe.check(
        "view_set aims at the node from the side asked for",
        ok and close_to(view.get("target"), [10, 0, 0]) and view.get("yaw_degrees") == -90 and view.get("pitch_degrees") == 0
        and view.get("position", [0])[0] > 10 and close_to(view.get("position", [0, 1, 1])[1:], [0, 0], 1e-2),
        str(view),
    )

    ok, view = tool(probe, "view_set", {"node": "cube", "yaw_degrees": 0, "pitch_degrees": -45, "distance": 20})
    probe.check(
        "view_set takes yaw, pitch and distance",
        ok and view.get("distance") == 20 and close_to(view.get("position"), [10, 14.142, -14.142], 1e-2),
        str(view),
    )

    ok, text = tool(probe, "view_set", {"node": "ghost"})
    probe.check("view_set names the nodes for an unknown one", not ok and "cube, root" in text, str(text))

    ok, text = tool(probe, "view_set", {"from": "north"})
    probe.check("view_set refuses an unknown side", not ok and "arguments.from" in text, str(text))

    ok, flat = tool(probe, "view_set", {"node": "cube", "from": "-z", "projection": "orthographic"})
    probe.check(
        "an orthographic view fits the node by its height in voxels",
        ok and flat.get("projection") == "orthographic" and close_to(flat.get("voxels_high"), 4.2, 1e-2)
        and flat.get("voxels_wide", 0) > flat.get("voxels_high", 0) and "distance" not in flat
        and close_to(flat.get("position", [0, 0])[:2], flat.get("target", [1, 1])[:2], 1e-3),
        str(flat),
    )

    ok, kept = tool(probe, "view_set", {"node": "cube", "from": "+y"})
    probe.check(
        "the projection stays until it is set again, and the top view looks straight down",
        ok and kept.get("projection") == "orthographic" and kept.get("pitch_degrees") == -90
        and close_to(kept.get("voxels_high"), 4.2, 1e-2),
        str(kept),
    )

    ok, tall = tool(probe, "view_set", {"node": "cube", "from": "-z", "height": 16})
    probe.check("an orthographic view takes its height", ok and close_to(tall.get("voxels_high"), 16), str(tall))

    ok, text = tool(probe, "view_set", {"node": "cube", "distance": 20})
    probe.check("an orthographic view refuses a distance", not ok and "has no distance" in text, str(text))
    ok, text = tool(probe, "view_set", {"node": "cube", "projection": "perspective", "height": 16})
    probe.check("a perspective view refuses a height", not ok and "framed by 'distance'" in text, str(text))
    ok, text = tool(probe, "view_set", {"node": "cube", "projection": "flat"})
    probe.check("view_set refuses an unknown projection", not ok and "arguments.projection" in text, str(text))

    if window_visible:
        tool(probe, "view_set", {"node": "cube", "from": "-z", "projection": "orthographic", "height": 8})
        flat_picture, info = picture(probe, {"max_size": 256})
        probe.check(
            "an orthographic picture says how many pixels a voxel takes",
            flat_picture is not None and close_to(info.get("pixels_per_voxel"), info.get("height", 0) / 8, 1e-2),
            str(info),
        )
        tool(probe, "view_set", {"node": "cube", "from": "-z", "projection": "perspective"})
        deep_picture, info = picture(probe, {"max_size": 256})
        probe.check(
            "a perspective picture differs and names no pixel size",
            deep_picture is not None and deep_picture != flat_picture and "pixels_per_voxel" not in info,
            str(info),
        )

    ok, back = tool(probe, "view_set", {"node": "cube", "from": "iso", "projection": "perspective"})
    probe.check(
        "view_set goes back to perspective",
        ok and back.get("projection") == "perspective" and back.get("distance", 0) > 0 and "voxels_high" not in back,
        str(back),
    )

    if not window_visible:
        data, text = picture(probe)
        probe.check("view_screenshot says a minimised editor draws nothing", data is None and "minimised" in text, str(text))
        tool(probe, "prefab_close", {"discard_unsaved": True})
        remove_scratch_assets(asset_root)
        return

    data, info = picture(probe, {"max_size": 256})
    probe.check("view_screenshot returns a png", data is not None and data[:8] == b"\x89PNG\r\n\x1a\n", str(info))
    if data is None:
        return
    width, height = png_size(data)
    probe.check(
        "the picture fits max_size and says its size",
        max(width, height) == 256 and info.get("width") == width and info.get("height") == height,
        f"{width}x{height} {info}",
    )

    tool(probe, "volume_write", {"node": "cube", "boxes": [{"min": [0, 0, 0], "max": [3, 3, 3], "voxel": "green_6"}]})
    repainted, _ = picture(probe, {"max_size": 256})
    probe.check("a picture taken right after a write shows the write", repainted is not None and repainted != data, "the two pictures are equal")

    again, _ = picture(probe, {"max_size": 256})
    probe.check("an unchanged scene gives the same picture", again == repainted, "the two pictures differ")

    with_overlays, _ = picture(probe, {"max_size": 256, "overlays": True})
    probe.check("overlays change the picture", with_overlays is not None and with_overlays != repainted, "the two pictures are equal")

    full, info = picture(probe, {"max_size": 2048})
    probe.check("a large max_size does not enlarge the window", full is not None and max(png_size(full)) <= 2048, str(info))

    data, text = picture(probe, {"max_size": 10})
    probe.check("view_screenshot refuses a size out of range", data is None and "max_size" in text, str(text))

    tool(probe, "prefab_close", {"discard_unsaved": True})
    remove_scratch_assets(asset_root)


def head_position(probe):
    return nodes_of(probe).get("head", {}).get("position")


def keys_of(clip, target):
    return next((track["keys"] for track in clip.get("tracks", []) if track["target"] == target), None)


def run_clip_scenario(probe):
    ok, state = tool(probe, "editor_state")
    asset_root = pathlib.Path(state["asset_root"])
    remove_scratch_assets(asset_root)
    tool(probe, "prefab_close", {"discard_unsaved": True})

    ok, text = tool(probe, "clip_create", {"name": SCRATCH_CLIP})
    probe.check("clip_create refuses without a prefab", not ok and "no prefab is open" in text, str(text))

    tool(probe, "prefab_open", {"name": "p_humanoid"})

    ok, text = tool(probe, "clip_get")
    probe.check("clip_get says when no clip is open", not ok and "no clip is open" in text, str(text))

    ok, text = tool(probe, "clip_open", {"name": "no_such_clip"})
    probe.check("clip_open names the clip files for an unknown one", not ok and "a_humanoid_idle" in text, str(text))

    ok, idle = tool(probe, "clip_open", {"name": "a_humanoid_idle"})
    probe.check(
        "clip_open opens and selects a clip",
        ok and idle.get("rig") == "humanoid" and idle.get("selected") is True and idle.get("unsaved") is False,
        str(idle),
    )

    ok, state = tool(probe, "editor_state")
    probe.check(
        "clip_open brings the editor into the clip",
        ok and state.get("context") == "clip" and state.get("clip") == "a_humanoid_idle" and state.get("open_clips") == ["a_humanoid_idle"],
        str(state),
    )

    ok, idle = tool(probe, "clip_get")
    head_keys = keys_of(idle, "head") if ok else None
    probe.check(
        "clip_get lists the keys of a target in the order of time",
        head_keys is not None and head_keys[0].get("time") == 0 and head_keys[0].get("position") == [0, 22, 0]
        and [key["time"] for key in head_keys] == sorted(key["time"] for key in head_keys) and close_to(idle.get("duration"), 4.8),
        str(head_keys),
    )

    ok, fresh = tool(probe, "clip_create", {"name": SCRATCH_CLIP})
    probe.check(
        "clip_create makes an empty clip with the rig of the prefab",
        ok and fresh.get("rig") == "humanoid" and fresh.get("key_count") == 0 and fresh.get("selected") is True,
        str(fresh),
    )

    ok, text = tool(probe, "clip_create", {"name": SCRATCH_CLIP})
    probe.check("clip_create refuses a clip that is open", not ok and "already open" in text, str(text))

    ok, text = tool(probe, "clip_create", {"name": "a_humanoid_run_f"})
    probe.check("clip_create refuses an existing file", not ok and "overwrite: true" in text, str(text))

    ok, keyed = tool(
        probe,
        "clip_set_keys",
        {
            "keys": [
                {"target": "head", "time": 1, "position": [0, 12, 0], "rotation_degrees": [0, 45, 0], "interp": "ease_in_out"},
                {"target": "head", "time": 0, "position": [0, 9.5, 0]},
                {"target": "body", "time": 0.5, "scale": [1, 1.2, 1]},
            ]
        },
    )
    probe.check(
        "clip_set_keys puts keys and makes tracks",
        ok and keyed.get("key_count") == 4 and keyed.get("track_count") == 2 and keyed.get("duration") == 1 and keyed.get("unsaved") is True,
        str(keyed),
    )

    ok, clip = tool(probe, "clip_get", {"clip": SCRATCH_CLIP})
    head_keys = keys_of(clip, "head") if ok else None
    probe.check(
        "the keys read back as they were put",
        head_keys is not None and len(head_keys) == 2 and head_keys[0] == {"time": 0, "position": [0, 9.5, 0]}
        and head_keys[1].get("position") == [0, 12, 0] and close_to(head_keys[1].get("rotation_degrees"), [0, 45, 0])
        and head_keys[1].get("interp") == "ease_in_out",
        str(head_keys),
    )

    ok, replaced = tool(probe, "clip_set_keys", {"keys": [{"target": "head", "time": 1.0003, "position": [0, 13, 0]}]})
    probe.check("a key at a taken instant replaces the one there", ok and replaced.get("key_count") == 4, str(replaced))

    ok, text = tool(probe, "clip_set_keys", {"keys": [{"target": "tail", "time": 0, "position": [0, 0, 0]}]})
    probe.check("a target the prefab lacks is refused with the targets", not ok and "not an animation target" in text and "hand_left" in text, str(text))

    ok, text = tool(probe, "clip_set_keys", {"keys": [{"target": "head", "time": 0}]})
    probe.check("a key without a value is refused", not ok and "at least one of position" in text, str(text))

    ok, text = tool(probe, "clip_set_keys", {"keys": [{"target": "head", "time": -1, "position": [0, 0, 0]}]})
    probe.check("a negative time is refused", not ok and "must not be negative" in text, str(text))

    ok, text = tool(probe, "clip_set_keys", {"keys": [{"target": "head", "time": 0, "position": [0, 0, 0], "interp": "bouncy"}]})
    probe.check("an unknown interpolation is refused", not ok and "cubic_bezier" in text, str(text))

    ok, after_refusals = tool(probe, "clip_get")
    probe.check("refused keys change nothing", ok and after_refusals.get("key_count") == 4, str(after_refusals.get("key_count")))

    ok, posed = tool(probe, "clip_pose_at", {"time": 1})
    probe.check("clip_pose_at poses the prefab at a time", ok and close_to(head_position(probe), [0, 13, 0], 0.01), str(head_position(probe)))
    tool(probe, "clip_pose_at", {"time": 0.5})
    probe.check("the pose between two keys lies between them", close_to(head_position(probe), [0, 11.25, 0], 0.05), str(head_position(probe)))

    tool(probe, "undo")
    tool(probe, "clip_pose_at", {"time": 1})
    probe.check("undo takes back the last keys", close_to(head_position(probe), [0, 12, 0]), str(head_position(probe)))
    tool(probe, "redo")

    ok, removed = tool(probe, "clip_remove_keys", {"target": "head", "property": "rotation"})
    probe.check("clip_remove_keys removes the keys of one property", ok and removed.get("keys_removed") == 1 and removed.get("key_count") == 3, str(removed))

    ok, text = tool(probe, "clip_remove_keys", {"target": "head", "from": 5, "to": 6})
    probe.check("removing where there are no keys is refused", not ok and "no keys in that span" in text, str(text))

    ok, text = tool(probe, "clip_remove_keys", {"target": "foot_left"})
    probe.check("removing from a track that is not there names the tracks", not ok and "body, head" in text.replace("head, body", "body, head"), str(text))

    ok, removed = tool(probe, "clip_remove_keys", {"target": "body"})
    probe.check("removing every key removes the track", ok and removed.get("track_count") == 1 and removed.get("key_count") == 2, str(removed))

    ok, evented = tool(
        probe,
        "clip_set_events",
        {"events": [{"time": 0.5, "name": "hit.end"}, {"time": 0, "name": "footstep", "payload": "left"}]},
    )
    probe.check(
        "clip_set_events puts events in the order of time",
        ok and evented.get("event_count") == 2
        and evented.get("events") == [{"time": 0, "name": "footstep", "payload": "left"}, {"time": 0.5, "name": "hit.end"}],
        str(evented),
    )

    ok, text = tool(probe, "clip_set_events", {"events": [{"time": 5, "name": "late"}]})
    probe.check("an event past the end of the clip is refused", not ok and "past the end" in text, str(text))

    ok, text = tool(probe, "clip_set_events", {"events": [{"time": 0, "name": "two words"}]})
    probe.check("an event name with a space is refused", not ok and "whitespace" in text, str(text))

    tool(probe, "undo")
    ok, undone = tool(probe, "clip_get")
    probe.check("undo takes back the events", ok and undone.get("event_count") == 0, str(undone.get("events")))
    tool(probe, "redo")

    ok, saved = tool(probe, "clip_save")
    clip_file = asset_root / "animations" / f"{SCRATCH_CLIP}.voxa"
    probe.check(
        "clip_save writes the clip with its rig",
        ok and saved.get("unsaved") is False and clip_file.is_file() and "rig humanoid" in clip_file.read_text(encoding="utf-8"),
        str(saved),
    )

    ok, in_memory = tool(probe, "clip_get")
    ok, closed = tool(probe, "clip_close")
    probe.check("clip_close closes the clip", ok and closed.get("open_clips") == ["a_humanoid_idle"], str(closed))

    ok, state = tool(probe, "editor_state")
    probe.check(
        "closing the selected clip returns to the prefab and its rest pose",
        ok and state.get("context") == "prefab" and close_to(head_position(probe), [0, 22, 0]),
        f"{state.get('context')} {head_position(probe)}",
    )

    tool(probe, "clip_open", {"name": SCRATCH_CLIP})
    ok, from_disk = tool(probe, "clip_get")
    probe.check(
        "the saved clip reads back the same from disk",
        ok and close_to(from_disk.get("tracks"), in_memory.get("tracks")) and from_disk.get("rig") == "humanoid"
        and from_disk.get("events") == in_memory.get("events") and from_disk.get("event_count") == 2,
        json.dumps(from_disk.get("tracks")),
    )

    tool(probe, "clip_set_keys", {"keys": [{"target": "head", "time": 2, "position": [0, 9.5, 0]}]})
    ok, text = tool(probe, "clip_close")
    probe.check("clip_close refuses over unsaved keys", not ok and "discard_unsaved" in text, str(text))
    ok, text = tool(probe, "prefab_close")
    probe.check("prefab_close names the unsaved clip", not ok and f"the clip {SCRATCH_CLIP}" in text, str(text))
    ok, closed = tool(probe, "clip_close", {"discard_unsaved": True})
    probe.check("clip_close drops them when told to", ok, str(closed))

    tool(probe, "clip_open", {"name": "a_humanoid_idle"})
    tool(probe, "clip_pose_at", {"time": 1.2})
    posed_head = head_position(probe)
    ok, _ = tool(probe, "node_set_transform", {"name": "body", "scale": [1, 1, 1]})
    ok, state = tool(probe, "editor_state")
    probe.check(
        "a node tool leaves the clip and restores the rest pose first",
        ok and state.get("context") == "prefab" and not close_to(posed_head, [0, 22, 0]) and close_to(head_position(probe), [0, 22, 0]),
        f"{state.get('context')} posed {posed_head} now {head_position(probe)}",
    )

    tool(probe, "clip_close", {"clip": "a_humanoid_idle", "discard_unsaved": True})
    run_playback_checks(probe)
    tool(probe, "prefab_close", {"discard_unsaved": True})
    remove_scratch_assets(asset_root)


def run_playback_checks(probe):
    ok, opened = tool(probe, "clip_open", {"name": "a_humanoid_run_f"})
    probe.check(
        "an opened clip is not playing",
        ok and opened.get("playback", {}).get("state") == "stopped",
        str(opened),
    )

    ok, played = tool(probe, "clip_play", {"loop": "loop", "speed": 0.5})
    playback = played.get("playback", {}) if ok else {}
    probe.check(
        "clip_play starts the clip with the loop and the speed given",
        ok and playback.get("state") == "playing" and playback.get("loop") == "loop" and close_to(playback.get("speed"), 0.5),
        str(played),
    )

    time.sleep(0.3)
    _, first = tool(probe, "clip_get")
    time.sleep(0.3)
    ok, second = tool(probe, "clip_get")
    probe.check(
        "a playing clip moves on between two reads",
        ok and second["playback"]["state"] == "playing" and second["playback"]["time"] != first["playback"]["time"],
        f"{first.get('playback')} {second.get('playback')}",
    )

    ok, state = tool(probe, "editor_state")
    probe.check("playing keeps the editor in the clip", ok and state.get("context") == "clip", str(state.get("context")))

    ok, played = tool(probe, "clip_play", {"from": 0.2, "loop": "once", "speed": 1})
    probe.check(
        "clip_play starts from the time given",
        ok and played["playback"]["state"] == "playing" and close_to(played["playback"]["time"], 0.2),
        str(played),
    )

    ok, kept = tool(probe, "clip_play")
    probe.check(
        "the loop and the speed stay with the clip",
        ok and kept["playback"]["loop"] == "once" and close_to(kept["playback"]["speed"], 1),
        str(kept),
    )

    ok, stopped = tool(probe, "clip_stop")
    probe.check(
        "clip_stop leaves the clip at its start and not playing",
        ok and stopped["playback"]["state"] != "playing" and close_to(stopped["playback"]["time"], 0),
        str(stopped),
    )

    ok, text = tool(probe, "clip_play", {"speed": 0})
    probe.check("clip_play refuses a speed that is not positive", not ok and "positive" in text, str(text))
    ok, text = tool(probe, "clip_play", {"from": 99})
    probe.check("clip_play refuses a start past the end", not ok and "past the end" in text, str(text))
    ok, text = tool(probe, "clip_play", {"loop": "forever"})
    probe.check("clip_play refuses an unknown loop mode", not ok and "once, loop, ping_pong" in text, str(text))
    ok, text = tool(probe, "clip_play", {"clip": "nope"})
    probe.check("clip_play refuses a clip that is not open", not ok and "is not open" in text, str(text))

    tool(probe, "clip_close", {"clip": "a_humanoid_run_f", "discard_unsaved": True})


def machine_payload(machine):
    return {key: machine[key] for key in ("machine", "rig", "entry", "params", "states", "any")}


def run_fsm_scenario(probe):
    scratch = SCRATCH_PREFABS[0]
    scratch_ref = f"fsm/{SCRATCH_MACHINE}.voxf"
    ok, state = tool(probe, "editor_state")
    asset_root = pathlib.Path(state["asset_root"])
    remove_scratch_assets(asset_root)
    tool(probe, "prefab_close", {"discard_unsaved": True})

    ok, text = tool(probe, "fsm_get")
    probe.check("fsm_get says when no machine is open", not ok and "no state machine is open" in text, str(text))

    ok, text = tool(probe, "fsm_get", {"machine": "no_such_machine"})
    probe.check("fsm_get names the machine files for an unknown one", not ok and "fsm/humanoid_action.voxf" in text, str(text))

    ok, action = tool(probe, "fsm_get", {"machine": "humanoid_action"})
    states = {state["name"]: state for state in action.get("states", [])} if ok else {}
    probe.check(
        "fsm_get reads a machine from its file",
        ok and action.get("machine") == "fsm/humanoid_action.voxf" and action.get("entry") == "none" and action.get("layer") is None
        and action.get("params")
        == [
            {"name": "attack", "type": "trigger"},
            {"name": "dodge", "type": "trigger"},
            {"name": "attack_chain", "type": "int", "value": 0},
            {"name": "block", "type": "bool", "value": False},
            {"name": "block_impact", "type": "trigger"},
        ],
        str(action),
    )
    probe.check(
        "a state carries its clip, rate and fades",
        states.get("attack_1", {}).get("clip") == "animations/a_humanoid_attack_1.voxa" and states["attack_1"].get("playback") == "once"
        and states["attack_1"].get("rate") == 1 and states["attack_1"].get("fade_in") == 0.06 and states["none"].get("clip") is None,
        str(states.get("attack_1")),
    )
    probe.check(
        "transitions carry triggers, conditions, blends and waits",
        states.get("none", {}).get("transitions", [{}])[0]
        == {"to": "attack_1", "on": "attack", "when": [{"param": "attack_chain", "op": "==", "value": 1}], "blend": 0.08}
        and states.get("attack_1", {}).get("transitions", [{}])[-1] == {"to": "none", "wait_end": True, "wait_blend": True},
        str(states.get("none")),
    )

    ok, text = tool(probe, "fsm_create", {"name": SCRATCH_MACHINE})
    probe.check("fsm_create refuses without a prefab", not ok and "no prefab is open" in text, str(text))

    tool(probe, "prefab_open", {"name": "p_humanoid"})

    ok, locomotion = tool(probe, "fsm_get", {"machine": "fsm/humanoid_locomotion.voxf"})
    idle = next((state for state in locomotion.get("states", []) if state["name"] == "idle"), {}) if ok else {}
    probe.check(
        "fsm_get types parameter values",
        ok and locomotion.get("layer") == 0
        and {"name": "speed", "type": "float", "value": 0.0} in locomotion.get("params", [])
        and {"name": "air_state", "type": "int", "value": 0} in locomotion.get("params", []),
        str(locomotion.get("params")),
    )
    probe.check(
        "conditions read as parameter, operator and value",
        {"to": "run", "when": [{"param": "speed", "op": ">", "value": 0}], "blend": 0.15} in idle.get("transitions", [])
        and {"to": "rise", "when": [{"param": "air_state", "op": "==", "value": 1}], "blend": 0.02} in idle["transitions"],
        str(idle.get("transitions")),
    )

    ok, same = tool(probe, "fsm_set", machine_payload(locomotion))
    probe.check(
        "what fsm_get returns can be sent back unchanged",
        ok and same.get("unsaved") is False and machine_payload(same) == machine_payload(locomotion),
        str(same)[:400],
    )
    ok, state = tool(probe, "editor_state")
    probe.check(
        "fsm_set opens the machine in the editor",
        ok and state.get("context") == "machine" and state.get("machine") == "fsm/humanoid_locomotion.voxf",
        str(state.get("context_stack")),
    )

    tool(probe, "prefab_save_as", {"name": scratch})

    ok, made = tool(probe, "fsm_create", {"name": SCRATCH_MACHINE})
    machine_file = asset_root / "fsm" / f"{SCRATCH_MACHINE}.voxf"
    probe.check(
        "fsm_create makes a machine and attaches it as the next layer",
        ok and made.get("machine") == scratch_ref and made.get("layer") == 2 and made.get("rig") == "humanoid"
        and made.get("entry") == "idle" and machine_file.is_file(),
        str(made),
    )

    ok, text = tool(probe, "fsm_create", {"name": SCRATCH_MACHINE})
    probe.check("fsm_create refuses an existing file", not ok and "already exists" in text, str(text))

    wanted = {
        "machine": SCRATCH_MACHINE,
        "entry": "idle",
        "params": [
            {"name": "speed", "type": "float", "value": 0.5},
            {"name": "go", "type": "trigger"},
            {"name": "armed", "type": "bool", "value": True},
        ],
        "states": [
            {
                "name": "idle",
                "clip": "animations/a_humanoid_idle.voxa",
                "transitions": [
                    {"to": "walk", "when": [{"param": "speed", "op": ">", "value": 0.5}], "blend": 0.2},
                    {"to": "walk", "on": "go"},
                ],
            },
            {
                "name": "walk",
                "clip": "animations/a_humanoid_run_f.voxa",
                "rate": 1.5,
                "fade_in": {"duration": 0.3, "interp": "ease_in"},
                "transitions": [{"to": "idle", "when": [{"param": "armed", "op": "==", "value": False}], "wait_end": True}],
            },
        ],
        "any": [{"to": "idle", "on": "go", "blend": {"duration": 0.1, "interp": "cubic_bezier", "tangent_in": 0.2, "tangent_out": 0.8}}],
    }
    ok, built = tool(probe, "fsm_set", wanted)
    walk = next((state for state in built.get("states", []) if state["name"] == "walk"), {}) if ok else {}
    probe.check(
        "fsm_set replaces the machine",
        ok and built.get("unsaved") is True and built.get("rig") == "humanoid" and built.get("params") == wanted["params"]
        and len(built.get("states", [])) == 2 and built.get("any") == wanted["any"],
        str(built)[:600],
    )
    probe.check(
        "defaults are filled in and shaped fades are kept",
        walk.get("playback") == "loop" and walk.get("rate") == 1.5 and walk.get("fade_in") == {"duration": 0.3, "interp": "ease_in"}
        and walk.get("transitions") == wanted["states"][1]["transitions"],
        str(walk),
    )

    def refused(change, needle, label):
        broken = json.loads(json.dumps(wanted))
        change(broken)
        refused_ok, refused_text = tool(probe, "fsm_set", broken)
        probe.check(label, not refused_ok and needle in refused_text, str(refused_text))

    refused(lambda m: m.update(entry="nope"), "the entry 'nope' is not a state", "an entry that is not a state is refused")
    refused(lambda m: m["states"][0]["transitions"][0].update(to="run"), "the target 'run' is not a state", "a transition to a missing state is refused")
    refused(lambda m: m["states"][0]["transitions"][1].update(on="jump"), "'jump' is not a trigger parameter", "an undeclared trigger is refused")
    refused(
        lambda m: m["states"][0]["transitions"][0]["when"][0].update(param="stamina"),
        "which is not a value parameter",
        "a condition on an undeclared parameter is refused",
    )
    refused(lambda m: m["states"][1].update(clip="animations/no_such.voxa"), "there is no clip file", "a clip that is not a file is refused")
    refused(lambda m: m["states"][0]["transitions"][0]["when"][0].update(op="~="), "expected one of ==", "an unknown operator is refused")
    refused(lambda m: m["states"].append({"name": "idle"}), "the state 'idle' is declared 2 times", "a state declared twice is refused")
    refused(lambda m: m["states"][0].update(speed=2), "unknown field 'speed'", "an unknown field of a state is refused")
    refused(lambda m: m.update(rig="quadruped"), "the prefab has the rig 'humanoid'", "a rig that differs from the prefab's is refused")
    refused(lambda m: m.update(machine="humanoid_x"), "is not a state machine of this prefab", "a machine the prefab does not run is refused")

    ok, after_refusals = tool(probe, "fsm_get")
    probe.check("refused machines change nothing", ok and machine_payload(after_refusals) == machine_payload(built), str(after_refusals)[:300])

    tool(probe, "undo")
    ok, undone = tool(probe, "fsm_get")
    probe.check("undo takes back the whole replacement", ok and [state["name"] for state in undone.get("states", [])] == ["idle"], str(undone)[:300])
    tool(probe, "redo")

    ok, text = tool(probe, "fsm_set", machine_payload(locomotion))
    probe.check("another machine cannot be edited over unsaved changes", not ok and "fsm_save" in text and scratch_ref in text, str(text))

    ok, text = tool(probe, "prefab_set_machines", {"machines": ["humanoid_locomotion", "humanoid_action"]})
    probe.check("a machine with unsaved changes cannot be detached", not ok and "would be detached" in text, str(text))

    ok, text = tool(probe, "prefab_close")
    probe.check("prefab_close names the unsaved machine", not ok and f"the state machine {scratch_ref}" in text, str(text))

    ok, saved = tool(probe, "fsm_save")
    probe.check(
        "fsm_save writes the machine",
        ok and saved.get("saved") == scratch_ref and saved.get("unsaved") is False and "state walk" in machine_file.read_text(encoding="utf-8"),
        str(saved),
    )

    ok, text = tool(probe, "prefab_set_machines", {"machines": ["humanoid_locomotion", "no_such"]})
    probe.check("prefab_set_machines refuses a file that is not there", not ok and "machines[1]" in text, str(text))

    ok, text = tool(probe, "prefab_set_machines", {"machines": ["humanoid_action", "humanoid_action"]})
    probe.check("prefab_set_machines refuses a machine given twice", not ok and "given twice" in text, str(text))

    ok, reordered = tool(probe, "prefab_set_machines", {"machines": [SCRATCH_MACHINE, "fsm/humanoid_locomotion.voxf"]})
    probe.check(
        "prefab_set_machines sets the layers in order",
        ok and reordered.get("machines") == [scratch_ref, "fsm/humanoid_locomotion.voxf"],
        str(reordered),
    )
    ok, prefab = tool(probe, "prefab_get")
    probe.check("prefab_get shows the machines of the prefab", ok and prefab.get("machines") == reordered.get("machines"), str(prefab.get("machines")))

    ok, in_memory = tool(probe, "fsm_get", {"machine": SCRATCH_MACHINE})
    tool(probe, "prefab_close", {"discard_unsaved": True})
    ok, from_disk = tool(probe, "fsm_get", {"machine": SCRATCH_MACHINE})
    probe.check(
        "the saved machine reads back the same from disk",
        ok and close_to(machine_payload(from_disk), machine_payload(in_memory)),
        json.dumps(from_disk)[:500],
    )

    remove_scratch_assets(asset_root)


def run_rename_scenario(probe):
    tool(probe, "prefab_close", {"discard_unsaved": True})
    ok, opened = tool(probe, "prefab_open", {"name": "p_humanoid"})
    probe.check("the humanoid opens for the rename checks", ok, str(opened))

    before = nodes_of(probe)
    head = before.get("head", {})

    ok, renamed = tool(probe, "node_rename", {"name": "head", "new_name": "skull"})
    after = nodes_of(probe)
    skull = after.get("skull", {})
    probe.check(
        "node_rename gives the node its new name and nothing else",
        ok and renamed.get("name") == "skull" and "head" not in after and len(after) == len(before)
        and skull.get("parent") == head.get("parent") and close_to(skull.get("position"), head.get("position")),
        str(renamed),
    )
    probe.check(
        "the animation target keeps its name through a node rename",
        skull.get("anim_target") == "head",
        str(skull.get("anim_target")),
    )

    ok, state = tool(probe, "editor_state")
    probe.check("a rename leaves the prefab unsaved", ok and state.get("unsaved", {}).get("prefab") is True, str(state.get("unsaved")))

    ok, text = tool(probe, "node_rename", {"name": "skull", "new_name": "body"})
    probe.check("node_rename refuses a name another node has", not ok and "already exists" in text, str(text))
    ok, text = tool(probe, "node_rename", {"name": "skull", "new_name": "top head"})
    probe.check("node_rename refuses a name with a space", not ok and "cannot name a node" in text, str(text))
    ok, text = tool(probe, "node_rename", {"name": "nope", "new_name": "x"})
    probe.check("node_rename refuses a node that is not there", not ok and "there is no node 'nope'" in text, str(text))
    probe.check("refused renames change nothing", set(nodes_of(probe)) == set(after), str(sorted(nodes_of(probe))))

    tool(probe, "undo")
    probe.check("undo takes the rename back", "head" in nodes_of(probe) and "skull" not in nodes_of(probe), str(sorted(nodes_of(probe))))
    tool(probe, "redo")
    probe.check("redo renames again", "skull" in nodes_of(probe) and "head" not in nodes_of(probe), str(sorted(nodes_of(probe))))
    tool(probe, "undo")

    tool(probe, "node_set_transform", {"name": "head", "position": [0, 11, 0]})
    tool(probe, "node_rename", {"name": "head", "new_name": "skull"})
    tool(probe, "node_set_transform", {"name": "skull", "position": [0, 12, 0]})
    tool(probe, "undo")
    tool(probe, "undo")
    tool(probe, "undo")
    probe.check(
        "steps made before and after a rename undo in order",
        close_to(head_position(probe), head.get("position")),
        str(head_position(probe)),
    )

    ok, prefab = tool(probe, "prefab_get")
    root_name = prefab.get("root_node") if ok else None
    ok, renamed = tool(probe, "node_rename", {"name": root_name, "new_name": "origin"})
    ok_get, prefab = tool(probe, "prefab_get")
    probe.check(
        "renaming the root renames the root of the prefab",
        ok and ok_get and prefab.get("root_node") == "origin",
        str(prefab.get("root_node") if ok_get else prefab),
    )
    tool(probe, "undo")

    tool(probe, "clip_open", {"name": "a_humanoid_idle"})
    ok, clip = tool(probe, "clip_get")
    head_keys = keys_of(clip, "head") if ok else None

    ok, changed = tool(probe, "node_set_components", {"name": "head", "anim_target": "skull"})
    probe.check(
        "a target is renamed on its own, the node keeps its name",
        ok and changed.get("name") == "head" and changed.get("anim_target") == "skull",
        str(changed),
    )

    ok, text = tool(probe, "clip_retarget", {"clip": "a_humanoid_idle", "from": "neck", "to": "skull"})
    probe.check("clip_retarget refuses a track the clip does not have", not ok and "has no track for 'neck'" in text, str(text))
    ok, text = tool(probe, "clip_retarget", {"clip": "a_humanoid_idle", "from": "head", "to": "crown"})
    probe.check("clip_retarget refuses a target the prefab does not have", not ok and "is not an animation target" in text, str(text))
    ok, text = tool(probe, "clip_retarget", {"clip": "a_humanoid_idle", "from": "head", "to": "body"})
    probe.check("clip_retarget refuses a target that already has a track", not ok and "already has a track" in text, str(text))

    ok, moved = tool(probe, "clip_retarget", {"clip": "a_humanoid_idle", "from": "head", "to": "skull"})
    probe.check(
        "clip_retarget moves the keys to the new target",
        ok and keys_of(moved, "head") is None and close_to(keys_of(moved, "skull"), head_keys)
        and moved.get("track_count") == clip.get("track_count") and moved.get("unsaved") is True,
        json.dumps(moved)[:400],
    )

    tool(probe, "undo")
    ok, back = tool(probe, "clip_get", {"clip": "a_humanoid_idle"})
    probe.check(
        "undo puts the keys back under the old target",
        ok and keys_of(back, "skull") is None and close_to(keys_of(back, "head"), head_keys),
        json.dumps(back)[:400],
    )

    tool(probe, "clip_close", {"clip": "a_humanoid_idle", "discard_unsaved": True})
    tool(probe, "prefab_close", {"discard_unsaved": True})


def solid_from(probe, node):
    ok, volume = tool(probe, "volume_get", {"node": node})
    return (volume.get("layers") or {}).get("origin") if ok else None


def run_copy_scenario(probe):
    scratch = SCRATCH_PREFABS[0]
    ok, state = tool(probe, "editor_state")
    asset_root = pathlib.Path(state["asset_root"])
    remove_scratch_assets(asset_root)
    tool(probe, "prefab_close", {"discard_unsaved": True})

    tool(probe, "prefab_new", {"name": scratch})
    tool(probe, "node_create", {"name": "root"})
    tool(
        probe,
        "node_create",
        {
            "name": "arm_r", "parent": "root", "position": [5, 2, 0], "rotation_degrees": [0, 0, 30],
            "volume": {"size": [2, 4, 2]}, "anim_target": True,
            "sockets": [{"name": "grip", "position": [1, -2, 0]}],
        },
    )
    tool(probe, "node_create", {"name": "hand_r", "parent": "arm_r", "position": [1, -3, 1], "volume": {"size": [2, 2, 2]}})
    tool(probe, "volume_write", {"node": "arm_r", "boxes": [{"min": [0, 0, 0], "max": [0, 0, 0], "voxel": "red_6"}]})
    tool(probe, "node_create", {"name": "spare", "parent": "root", "anim_target": "ghost"})
    before = nodes_of(probe)

    ok, text = tool(probe, "node_duplicate", {"name": "arm_r", "names": {"arm_r": "arm_l"}})
    probe.check("node_duplicate asks for a name for every copied node", not ok and "missing for: hand_r" in text, str(text))
    ok, text = tool(probe, "node_duplicate", {"name": "arm_r", "names": {"arm_r": "arm_l", "hand_r": "spare"}})
    probe.check("node_duplicate refuses a name that is taken", not ok and "already exists" in text, str(text))
    ok, text = tool(probe, "node_duplicate", {"name": "arm_r", "names": {"arm_r": "ghost", "hand_r": "hand_l"}})
    probe.check("node_duplicate refuses a name that is another node's animation target", not ok and "animation target of node 'spare'" in text, str(text))
    ok, text = tool(probe, "node_duplicate", {"name": "arm_r", "names": {"arm_r": "arm_l", "hand_r": "hand_l", "spare": "s2"}})
    probe.check("node_duplicate refuses a name for a node outside the copy", not ok and "'spare' is not 'arm_r' or a node under it" in text, str(text))
    ok, text = tool(probe, "node_duplicate", {"name": "root", "names": {"root": "root2"}})
    probe.check("node_duplicate refuses the root", not ok and "root node" in text, str(text))
    ok, text = tool(probe, "node_duplicate", {"name": "arm_r", "names": {"arm_r": "arm_l", "hand_r": "hand_l"}, "mirror": "w"})
    probe.check("node_duplicate refuses an unknown axis", not ok and "arguments.mirror" in text, str(text))
    probe.check("refused copies change nothing", nodes_of(probe) == before, str(sorted(nodes_of(probe))))

    ok, copied = tool(probe, "node_duplicate", {"name": "arm_r", "names": {"arm_r": "arm_l", "hand_r": "hand_l"}, "mirror": "x"})
    after = nodes_of(probe)
    arm = after.get("arm_l", {})
    hand = after.get("hand_l", {})
    probe.check(
        "a mirrored copy stands across the parent's plane",
        ok and len(after) == len(before) + 2 and arm.get("parent") == "root" and close_to(arm.get("position"), [-5, 2, 0])
        and close_to(arm.get("rotation_degrees"), [0, 0, -30], 1e-2),
        str(arm),
    )
    probe.check(
        "the nodes under it are copied and mirrored with it",
        hand.get("parent") == "arm_l" and close_to(hand.get("position"), [-1, -3, 1]),
        str(hand),
    )
    probe.check(
        "a copied target takes the name of its node and the socket is mirrored",
        arm.get("anim_target") == "arm_l" and "anim_target" not in hand
        and close_to(arm.get("sockets", [{}])[0].get("position"), [-1, -2, 0]) and arm["sockets"][0].get("name") == "grip",
        str(arm),
    )
    probe.check(
        "the copy has a volume of its own, mirrored voxel for voxel",
        arm.get("volume", {}).get("ref") is None and solid_from(probe, "arm_r") == [0, 0, 0] and solid_from(probe, "arm_l") == [1, 0, 0],
        f"{arm.get('volume')} {solid_from(probe, 'arm_r')} {solid_from(probe, 'arm_l')}",
    )
    ok, own = tool(probe, "volume_get", {"node": "arm_l"})
    probe.check("and shares it with nobody", ok and "shared_with" not in own, str({k: v for k, v in own.items() if k != "layers"}))

    tool(probe, "prefab_get")
    tool(probe, "undo")
    probe.check("one undo takes the whole copy back", nodes_of(probe) == before, str(sorted(nodes_of(probe))))
    tool(probe, "redo")
    probe.check(
        "redo copies again",
        close_to(nodes_of(probe).get("hand_l", {}).get("position"), [-1, -3, 1]) and solid_from(probe, "arm_l") == [1, 0, 0],
        str(sorted(nodes_of(probe))),
    )

    ok, plain = tool(probe, "node_duplicate", {"name": "hand_r", "names": {"hand_r": "hand_spare"}, "parent": "root"})
    spare_hand = nodes_of(probe).get("hand_spare", {})
    probe.check(
        "a copy without mirror keeps its place and may go under another parent",
        ok and spare_hand.get("parent") == "root" and close_to(spare_hand.get("position"), [1, -3, 1]),
        str(spare_hand),
    )

    ok, saved = tool(probe, "prefab_save")
    models = asset_root / "models" / scratch
    probe.check(
        "saving names the volume files of the copies after their nodes",
        ok and (models / "m_arm_l.voxm").is_file() and (models / "m_hand_l.voxm").is_file() and (models / "m_hand_spare.voxm").is_file(),
        str(sorted(path.name for path in models.glob("*"))),
    )
    tool(probe, "prefab_close")
    tool(probe, "prefab_open", {"name": scratch})
    reopened = nodes_of(probe)
    probe.check(
        "the copy reads back from disk",
        close_to(reopened.get("arm_l", {}).get("position"), [-5, 2, 0]) and reopened.get("arm_l", {}).get("anim_target") == "arm_l"
        and solid_from(probe, "arm_l") == [1, 0, 0],
        str(reopened.get("arm_l")),
    )

    arm_ref = reopened["arm_r"]["volume"]["ref"]
    tool(probe, "node_create", {"name": "twin", "parent": "root", "volume": {"ref": arm_ref}})
    ok, shared = tool(probe, "volume_get", {"node": "twin"})
    probe.check("a node made from a volume file shares the volume", ok and shared.get("shared_with") == ["arm_r"], str(shared.get("shared_with")))

    ok, text = tool(probe, "volume_fork", {"node": "arm_l", "name": "arm_l_own"})
    probe.check("volume_fork refuses a volume nobody shares", not ok and "held by this node alone" in text, str(text))
    ok, text = tool(probe, "volume_fork", {"node": "twin", "name": "m_arm_r"})
    probe.check("volume_fork refuses a file name in use", not ok and "already uses a volume named 'm_arm_r'" in text, str(text))
    ok, text = tool(probe, "volume_fork", {"node": "twin", "name": "a/b"})
    probe.check("volume_fork refuses a name with a separator", not ok and "cannot name a volume file" in text, str(text))

    ok, forked = tool(probe, "volume_fork", {"node": "twin", "name": "twin_own"})
    probe.check(
        "volume_fork gives the node a copy under a file of its own",
        ok and forked.get("ref", "").endswith(f"{scratch}/twin_own.voxm") and "shared_with" not in forked
        and forked.get("voxel_count") == shared.get("voxel_count"),
        str({k: v for k, v in forked.items() if k != "layers"}),
    )
    tool(probe, "volume_write", {"node": "twin", "boxes": [{"min": [1, 3, 1], "max": [1, 3, 1], "voxel": "green_6"}]})
    ok, original = tool(probe, "volume_get", {"node": "arm_r"})
    probe.check("a write to the fork leaves the original alone", ok and original.get("voxel_count") == 1 and "shared_with" not in original, str(original.get("voxel_count")))

    tool(probe, "undo")
    tool(probe, "undo")
    ok, again = tool(probe, "volume_get", {"node": "twin"})
    probe.check("undo makes the volume shared again", ok and again.get("shared_with") == ["arm_r"] and again.get("ref") == arm_ref, str({k: v for k, v in again.items() if k != "layers"}))
    tool(probe, "redo")
    ok, saved = tool(probe, "prefab_save")
    probe.check("saving writes the fork to its file", ok and (models / "twin_own.voxm").is_file(), str(saved))

    ok, hidden = tool(probe, "view_hide", {"nodes": ["arm_l"]})
    probe.check(
        "view_hide hides the nodes named",
        ok and hidden.get("hidden") == ["arm_l"] and nodes_of(probe).get("arm_l", {}).get("hidden") is True and "hidden" not in nodes_of(probe).get("arm_r", {}),
        str(hidden),
    )
    ok, state = tool(probe, "editor_state")
    probe.check("hiding does not change the prefab", ok and state.get("unsaved", {}).get("prefab") is False, str(state.get("unsaved")))
    ok, text = tool(probe, "view_hide", {"nodes": ["nope"]})
    probe.check("view_hide refuses an unknown node and keeps the list", not ok and "there is no node 'nope'" in text and nodes_of(probe).get("arm_l", {}).get("hidden") is True, str(text))
    ok, hidden = tool(probe, "view_hide", {"nodes": []})
    probe.check("an empty list shows everything", ok and hidden.get("hidden") == [] and "hidden" not in nodes_of(probe).get("arm_l", {}), str(hidden))

    count = len(nodes_of(probe))
    ok, shown = tool(probe, "socket_preview", {"node": "arm_r", "socket": "grip", "prefab": "p_sword"})
    probe.check(
        "socket_preview shows a prefab in the socket without adding nodes",
        ok and shown.get("sockets", [{}])[0].get("preview") == "p_sword.vox" and len(nodes_of(probe)) == count,
        str(shown),
    )
    ok, state = tool(probe, "editor_state")
    probe.check("a preview does not change the prefab", ok and state.get("unsaved", {}).get("prefab") is False, str(state.get("unsaved")))
    ok, text = tool(probe, "socket_preview", {"node": "arm_r", "socket": "palm", "prefab": "p_sword"})
    probe.check("socket_preview names the sockets for an unknown one", not ok and "its sockets are: grip" in text, str(text))
    ok, text = tool(probe, "socket_preview", {"node": "arm_r", "socket": "grip", "prefab": "no_such_prefab"})
    probe.check("socket_preview refuses a prefab that is not there", not ok and "there is no prefab" in text, str(text))
    ok, other = tool(probe, "socket_preview", {"node": "arm_r", "socket": "grip", "prefab": "p_shield.vox"})
    probe.check("another prefab replaces the preview", ok and other.get("sockets", [{}])[0].get("preview") == "p_shield.vox", str(other))

    tool(probe, "node_set_transform", {"name": "spare", "position": [0, 1, 0]})
    tool(probe, "prefab_save")
    tool(probe, "prefab_close")
    tool(probe, "prefab_open", {"name": scratch})
    probe.check("a preview is not saved with the prefab", len(nodes_of(probe)) == count and "preview" not in nodes_of(probe).get("arm_r", {}).get("sockets", [{}])[0], str(sorted(nodes_of(probe))))

    tool(probe, "socket_preview", {"node": "arm_r", "socket": "grip", "prefab": "p_sword"})
    ok, gone = tool(probe, "socket_preview", {"node": "arm_r", "socket": "grip", "prefab": None})
    probe.check("null takes the preview away", ok and "preview" not in gone.get("sockets", [{}])[0], str(gone))

    tool(probe, "prefab_close", {"discard_unsaved": True})
    remove_scratch_assets(asset_root)


def layers_of(probe, node):
    ok, volume = tool(probe, "volume_get", {"node": node})
    return (volume.get("layers") or {}) if ok else {}


def run_paint_scenario(probe):
    scratch = SCRATCH_PREFABS[0]
    ok, state = tool(probe, "editor_state")
    asset_root = pathlib.Path(state["asset_root"])
    window_visible = state.get("window_visible")
    remove_scratch_assets(asset_root)
    tool(probe, "prefab_close", {"discard_unsaved": True})

    tool(probe, "prefab_new", {"name": scratch})
    tool(probe, "node_create", {"name": "root"})
    tool(probe, "node_create", {"name": "plate", "parent": "root", "volume": {"size": [6, 2, 2]}})

    ok, drawn = tool(probe, "volume_write", {"node": "plate", "symmetry": "x", "boxes": [{"min": [0, 0, 0], "max": [1, 0, 0], "voxel": "red_6"}]})
    probe.check(
        "symmetry draws the mirror image across the pivot",
        ok and drawn.get("cells_written") == 4 and drawn.get("voxel_count") == 4
        and layers_of(probe, "plate").get("slices") == [["aa..aa"]],
        f"{drawn.get('cells_written')} {layers_of(probe, 'plate')}",
    )
    tool(probe, "undo")
    ok, empty = tool(probe, "volume_get", {"node": "plate"})
    probe.check("both halves are one undo step", ok and empty.get("voxel_count") == 0, str(empty.get("voxel_count")))
    tool(probe, "redo")

    ok, painted = tool(probe, "volume_write", {"node": "plate", "recolor": [{"from": "red_6", "to": "blue_6", "min": [0, 0, 0], "max": [2, 1, 1]}]})
    layers = layers_of(probe, "plate")
    probe.check(
        "recolor repaints one kind of voxel inside a region and keeps the shape",
        ok and painted.get("cells_written") == 2 and painted.get("voxel_count") == 4
        and sorted(layers.get("legend", {}).values()) == ["blue_6", "red_6"] and len(set(layers.get("slices", [[""]])[0][0])) == 3,
        f"{painted.get('cells_written')} {layers}",
    )
    ok, whole = tool(probe, "volume_write", {"node": "plate", "recolor": [{"from": "red_6", "to": "blue_6"}]})
    probe.check(
        "recolor without a region repaints the whole volume",
        ok and whole.get("cells_written") == 2 and list(layers_of(probe, "plate").get("legend", {}).values()) == ["blue_6"],
        str(layers_of(probe, "plate")),
    )
    ok, nothing = tool(probe, "volume_write", {"node": "plate", "recolor": [{"from": "green_6", "to": "red_6"}]})
    probe.check("recolor of a voxel that is not there says so", ok and nothing.get("cells_written") == 0 and "nothing matched" in nothing.get("note", ""), str(nothing.get("note")))
    ok, text = tool(probe, "volume_write", {"node": "plate", "recolor": [{"from": "air", "to": "red_6"}]})
    probe.check("recolor refuses air", not ok and "air cannot be repainted" in text, str(text))
    ok, text = tool(probe, "volume_write", {"node": "plate", "symmetry": "w", "boxes": [{"min": [0, 0, 0], "max": [0, 0, 0], "voxel": "red_6"}]})
    probe.check("symmetry refuses an unknown axis", not ok and "arguments.symmetry" in text, str(text))

    tool(probe, "volume_set_pivot", {"node": "plate", "pivot": [2.3, 1, 1]})
    ok, text = tool(probe, "volume_write", {"node": "plate", "symmetry": "x", "boxes": [{"min": [0, 0, 0], "max": [0, 0, 0], "voxel": "red_6"}]})
    probe.check("symmetry refuses a pivot between cell boundaries", not ok and "multiple of 0.5" in text, str(text))
    tool(probe, "volume_set_pivot", {"node": "plate", "pivot": [2, 1, 1]})
    ok, text = tool(probe, "volume_write", {"node": "plate", "symmetry": "x", "boxes": [{"min": [5, 0, 0], "max": [5, 0, 0], "voxel": "red_6"}]})
    probe.check("symmetry refuses a mirror image outside the volume", not ok and "outside the volume" in text, str(text))
    tool(probe, "volume_set_pivot", {"node": "plate", "pivot": [2.5, 1, 1]})
    ok, odd = tool(probe, "volume_write", {"node": "plate", "symmetry": "x", "boxes": [{"min": [0, 1, 0], "max": [0, 1, 0], "voxel": "red_6"}]})
    probe.check(
        "a pivot in the middle of a cell mirrors about that cell",
        ok and odd.get("cells_written") == 2 and layers_of(probe, "plate").get("slices", [[], [""]])[1][0].startswith("b...b"),
        str(layers_of(probe, "plate")),
    )

    tool(probe, "prefab_save")
    tool(probe, "node_rename", {"name": "plate", "new_name": "dish"})
    tool(probe, "node_create", {"name": "plate", "parent": "root", "volume": {"size": [2, 2, 2]}})
    tool(probe, "volume_write", {"node": "plate", "boxes": [{"min": [0, 0, 0], "max": [0, 0, 0], "voxel": "white"}]})
    ok, saved = tool(probe, "prefab_save")
    named = nodes_of(probe)
    models = asset_root / "models" / scratch
    probe.check(
        "a new volume does not take the file name another node already holds",
        ok and named.get("dish", {}).get("volume", {}).get("ref", "").endswith("/m_plate.voxm")
        and named.get("plate", {}).get("volume", {}).get("ref", "").endswith("/m_plate_2.voxm")
        and (models / "m_plate.voxm").is_file() and (models / "m_plate_2.voxm").is_file(),
        f"{named.get('dish', {}).get('volume')} {named.get('plate', {}).get('volume')}",
    )
    tool(probe, "prefab_close")
    tool(probe, "prefab_open", {"name": scratch})
    ok, dish = tool(probe, "volume_get", {"node": "dish"})
    ok_plate, plate = tool(probe, "volume_get", {"node": "plate"})
    probe.check(
        "and both volumes read back as they were",
        ok and ok_plate and dish.get("size") == [6, 2, 2] and dish.get("voxel_count") == 6 and plate.get("size") == [2, 2, 2] and plate.get("voxel_count") == 1,
        f"{dish.get('size')} {dish.get('voxel_count')} {plate.get('size')} {plate.get('voxel_count')}",
    )

    tool(probe, "prefab_close", {"discard_unsaved": True})
    remove_scratch_assets(asset_root)

    tool(probe, "prefab_open", {"name": "p_humanoid"})
    tool(probe, "clip_open", {"name": "a_humanoid_attack_1"})
    tool(probe, "view_set", {"from": "iso", "projection": "perspective"})

    ok, text = tool(probe, "clip_filmstrip", {"frames": 40})
    probe.check("clip_filmstrip refuses too many frames", not ok and "arguments.frames" in text, str(text))
    ok, text = tool(probe, "clip_filmstrip", {"max_size": 10})
    probe.check("clip_filmstrip refuses a frame size out of range", not ok and "arguments.max_size" in text, str(text))
    ok, text = tool(probe, "clip_filmstrip", {"clip": "nope"})
    probe.check("clip_filmstrip refuses a clip that is not open", not ok and "is not open" in text, str(text))

    _, reply, _ = probe.call("tools/call", {"name": "clip_filmstrip", "arguments": {"frames": 5, "max_size": 128}})
    result = (reply or {}).get("result", {})
    content = result.get("content", [])
    if not window_visible:
        probe.check(
            "clip_filmstrip says a minimised editor draws nothing",
            result.get("isError") is True and "minimised" in content[0].get("text", ""),
            str(content)[:300],
        )
    else:
        image = next((entry for entry in content if entry.get("type") == "image"), None)
        info = json.loads(next((entry["text"] for entry in content if entry.get("type") == "text"), "{}"))
        data = base64.b64decode(image["data"]) if image else b""
        width, height = png_size(data) if data else (0, 0)
        probe.check(
            "clip_filmstrip returns one picture with a cell per moment",
            result.get("isError") is False and len(info.get("times", [])) == 5 and info.get("columns") == 4
            and width == info.get("frame_width", 0) * 4 and height == info.get("frame_height", 0) * 2
            and max(info.get("frame_width", 0), info.get("frame_height", 0)) == 128,
            f"{width}x{height} {info}",
        )
        ok, clip = tool(probe, "clip_get")
        probe.check(
            "and leaves the clip posed at the last moment",
            ok and close_to(clip["playback"]["time"], info["times"][-1]) and clip["playback"]["state"] != "playing",
            str(clip.get("playback")),
        )

    tool(probe, "clip_close", {"clip": "a_humanoid_attack_1", "discard_unsaved": True})
    tool(probe, "prefab_close", {"discard_unsaved": True})


def run_machine_scenario(probe):
    tool(probe, "prefab_close", {"discard_unsaved": True})

    ok, text = tool(probe, "fsm_run")
    probe.check("fsm_run refuses with no prefab open", not ok and "no prefab is open" in text, str(text))
    ok, idle = tool(probe, "fsm_status")
    probe.check("fsm_status says nothing is running", ok and idle == {"running": False, "layers": [], "parameters": {}, "triggers": []}, str(idle))

    tool(probe, "prefab_open", {"name": "p_humanoid"})
    rest = nodes_of(probe)

    ok, text = tool(probe, "fsm_drive", {"set": {"speed": 1}})
    probe.check("fsm_drive refuses when nothing runs", not ok and "start them with fsm_run" in text, str(text))

    ok, started = tool(probe, "fsm_run")
    layers = started.get("layers", []) if ok else []
    probe.check(
        "fsm_run starts every machine of the prefab at its entry state",
        ok and started.get("running") is True and [layer.get("machine") for layer in layers] == ["humanoid_locomotion", "humanoid_action"]
        and [layer.get("state") for layer in layers] == ["idle", "none"]
        and started.get("parameters")
        == {"speed": 0.0, "air_state": 0, "dir_x": 0.0, "dir_z": 0.0, "stance": False, "attack_chain": 0, "block": False},
        str(started),
    )
    ok, state = tool(probe, "editor_state")
    probe.check("running puts the editor into the machine", ok and state.get("context") == "machine", str(state.get("context")))

    time.sleep(0.3)
    ok, playing = tool(probe, "fsm_status")
    probe.check(
        "the entry state plays its clip",
        ok and playing["layers"][0].get("clip") == "a_humanoid_idle" and playing["layers"][0].get("playback") == "playing" and playing["layers"][0].get("time", 0) > 0,
        str(playing.get("layers")),
    )

    ok, walking = tool(probe, "fsm_drive", {"set": {"speed": 1.0}})
    probe.check(
        "a parameter takes the machine to another state",
        ok and walking["layers"][0].get("state") == "run" and walking["layers"][0].get("clip") == "a_humanoid_run_f" and walking["parameters"].get("speed") == 1.0,
        str(walking),
    )
    ok, struck = tool(probe, "fsm_drive", {"set": {"attack_chain": 1}, "fire": ["attack"]})
    probe.check(
        "a trigger moves the machine of another layer and leaves the first alone",
        ok and struck["layers"][1].get("state") == "attack_1" and struck["layers"][0].get("state") == "run",
        str(struck.get("layers")),
    )
    probe.check("a running machine moves the nodes", nodes_of(probe) != rest, "the nodes are where they rest")

    time.sleep(1.5)
    ok, done = tool(probe, "fsm_status")
    probe.check("a state that waits for its clip leaves when the clip ends", ok and done["layers"][1].get("state") == "none", str(done.get("layers")))
    ok, jumped = tool(probe, "fsm_drive", {"set": {"air_state": 1}})
    probe.check(
        "an int parameter is set by its kind",
        ok and jumped["parameters"].get("air_state") == 1 and jumped["layers"][0].get("state") == "rise",
        str(jumped),
    )

    tool(probe, "fsm_drive", {"set": {"air_state": 0, "speed": 1.0}})
    time.sleep(0.6)
    ok, locomotion = tool(probe, "fsm_get", {"machine": "humanoid_locomotion"})
    edited = json.loads(json.dumps(machine_payload(locomotion)))
    next(state for state in edited["states"] if state["name"] == "run")["rate"] = 0.5
    ok, changed = tool(probe, "fsm_set", edited)
    time.sleep(0.2)
    ok_status, live = tool(probe, "fsm_status")
    probe.check(
        "fsm_set reaches machines that are running and keeps their state and parameters",
        ok and changed.get("unsaved") is True and ok_status and live.get("running") is True
        and live["layers"][0].get("state") == "run" and live["parameters"].get("speed") == 1.0,
        str(live),
    )
    tool(probe, "undo")
    time.sleep(0.2)
    ok_status, live = tool(probe, "fsm_status")
    ok_machine, back = tool(probe, "fsm_get", {"machine": "humanoid_locomotion"})
    probe.check(
        "undo of the edit reaches them too",
        ok_status and live.get("running") is True and live["layers"][0].get("state") == "run"
        and ok_machine and machine_payload(back) == machine_payload(locomotion),
        str(live),
    )

    ok, text = tool(probe, "fsm_drive", {"set": {"stamina": 1}})
    probe.check("fsm_drive names the parameters for an unknown one", not ok and "speed, air_state, dir_x, dir_z, stance, attack_chain, block" in text, str(text))
    ok, text = tool(probe, "fsm_drive", {"fire": ["speed"]})
    probe.check("fsm_drive refuses to fire a value", not ok and "they are: land" in text and "attack" in text, str(text))
    ok, text = tool(probe, "fsm_drive", {"set": {"air_state": 0.5}})
    probe.check("fsm_drive refuses a fraction for an int", not ok and "is an int" in text, str(text))
    ok, text = tool(probe, "fsm_drive", {})
    probe.check("fsm_drive asks for something to do", not ok and "at least one of set and fire" in text, str(text))

    ok, stopped = tool(probe, "fsm_stop")
    ok_state, state = tool(probe, "editor_state")
    probe.check(
        "fsm_stop puts the rest pose back and changes nothing in the prefab",
        ok and stopped.get("running") is False and close_to(nodes_of(probe), rest) and ok_state and state.get("unsaved", {}).get("prefab") is False,
        str(state.get("unsaved")),
    )

    tool(probe, "fsm_run")
    time.sleep(0.3)
    ok, moved = tool(probe, "node_set_transform", {"name": "head", "scale": [1, 1, 1]})
    ok_status, status = tool(probe, "fsm_status")
    probe.check(
        "an edit stops the machines and is made from the rest pose",
        ok and ok_status and status.get("running") is False and close_to(moved.get("position"), rest["head"]["position"]),
        f"{moved.get('position')} {status.get('running')}",
    )
    tool(probe, "undo")

    tool(probe, "clip_open", {"name": "a_humanoid_idle"})
    tool(probe, "clip_pose_at", {"time": 1.2})
    ok, moved = tool(probe, "node_set_transform", {"name": "head", "scale": [1, 1, 1]})
    probe.check(
        "an edit made over a clip pose is made from the rest pose too",
        ok and close_to(moved.get("position"), rest["head"]["position"]),
        str(moved.get("position")),
    )
    tool(probe, "undo")

    tool(probe, "fsm_run")
    ok, opened = tool(probe, "clip_open", {"name": "a_humanoid_run_f"})
    ok_status, status = tool(probe, "fsm_status")
    probe.check("opening a clip stops the machines", ok and ok_status and status.get("running") is False, str(status))

    tool(probe, "clip_close", {"clip": "a_humanoid_run_f", "discard_unsaved": True})
    tool(probe, "clip_close", {"clip": "a_humanoid_idle", "discard_unsaved": True})
    tool(probe, "prefab_close", {"discard_unsaved": True})


def run_delete_scenario(probe):
    scratch, copy = SCRATCH_PREFABS
    ok, state = tool(probe, "editor_state")
    asset_root = pathlib.Path(state["asset_root"])
    remove_scratch_assets(asset_root)
    tool(probe, "prefab_close", {"discard_unsaved": True})

    tool(probe, "prefab_new", {"name": scratch})
    tool(probe, "node_create", {"name": "root", "anim_target": True})
    tool(probe, "node_create", {"name": "box", "parent": "root", "volume": {"size": [2, 2, 2]}})
    tool(probe, "prefab_set_rig", {"rig": "smoke"})
    tool(probe, "prefab_save")
    tool(probe, "clip_create", {"name": SCRATCH_CLIP})
    tool(probe, "clip_set_keys", {"keys": [{"target": "root", "time": 0, "position": [0, 0, 0]}, {"target": "root", "time": 1, "position": [0, 1, 0]}]})
    tool(probe, "clip_save")
    tool(probe, "fsm_create", {"name": SCRATCH_MACHINE})
    ok, machine = tool(probe, "fsm_get", {"machine": SCRATCH_MACHINE})
    probe.check("the machine to delete reads", ok, str(machine))
    if not ok:
        return
    machine = machine_payload(machine)
    machine["states"][0]["clip"] = f"animations/{SCRATCH_CLIP}.voxa"
    tool(probe, "fsm_set", machine)
    tool(probe, "fsm_save")
    tool(probe, "prefab_save")

    prefab_file = asset_root / "prefabs" / f"{scratch}.vox"
    volume_dir = asset_root / "models" / scratch
    clip_file = asset_root / "animations" / f"{SCRATCH_CLIP}.voxa"
    machine_file = asset_root / "fsm" / f"{SCRATCH_MACHINE}.voxf"
    probe.check("the assets to delete are on disk", prefab_file.is_file() and (volume_dir / "m_box.voxm").is_file() and clip_file.is_file() and machine_file.is_file(), str(sorted(path.name for path in volume_dir.glob("*"))))

    ok, text = tool(probe, "asset_delete", {"kind": "prefab", "name": scratch})
    probe.check("asset_delete refuses the open prefab", not ok and "is open; close it first" in text and prefab_file.is_file(), str(text))
    ok, text = tool(probe, "asset_delete", {"kind": "clip", "name": SCRATCH_CLIP})
    probe.check("asset_delete refuses an open clip", not ok and "is open" in text and clip_file.is_file(), str(text))
    ok, text = tool(probe, "asset_delete", {"kind": "machine", "name": SCRATCH_MACHINE})
    probe.check("asset_delete refuses a machine the open prefab runs", not ok and "the open prefab runs" in text and machine_file.is_file(), str(text))
    ok, text = tool(probe, "asset_delete", {"kind": "prefab", "name": "no_such_prefab"})
    probe.check("asset_delete refuses a file that is not there", not ok and "there is no prefab" in text, str(text))
    ok, text = tool(probe, "asset_delete", {"kind": "folder", "name": "x"})
    probe.check("asset_delete refuses an unknown kind", not ok and "arguments.kind" in text, str(text))
    ok, text = tool(probe, "asset_delete", {"kind": "clip", "name": "../prefabs/p_humanoid"})
    probe.check("asset_delete refuses a name that leaves its folder", not ok and "cannot name a clip file" in text, str(text))

    tool(probe, "clip_close", {"discard_unsaved": True})
    tool(probe, "prefab_close", {"discard_unsaved": True})

    ok, text = tool(probe, "asset_delete", {"kind": "clip", "name": SCRATCH_CLIP})
    probe.check("asset_delete refuses a clip a machine plays", not ok and f"{SCRATCH_MACHINE}.voxf" in text and clip_file.is_file(), str(text))
    ok, text = tool(probe, "asset_delete", {"kind": "machine", "name": SCRATCH_MACHINE})
    probe.check("asset_delete refuses a machine a prefab runs", not ok and f"{scratch}.vox" in text and machine_file.is_file(), str(text))

    ok, gone = tool(probe, "asset_delete", {"kind": "prefab", "name": f"{scratch}.vox"})
    probe.check(
        "asset_delete removes a prefab with its volumes",
        ok and not prefab_file.exists() and not volume_dir.exists()
        and gone.get("deleted") == [f"models/{scratch}/m_box.voxm", f"prefabs/{scratch}.vox"],
        str(gone),
    )
    ok, gone = tool(probe, "asset_delete", {"kind": "machine", "name": SCRATCH_MACHINE})
    probe.check("then the machine nothing runs any more", ok and not machine_file.exists(), str(gone))
    ok, gone = tool(probe, "asset_delete", {"kind": "clip", "name": SCRATCH_CLIP})
    probe.check("then the clip nothing plays any more", ok and not clip_file.exists(), str(gone))

    remove_scratch_assets(asset_root)


def main():
    port = DEFAULT_PORT
    with_scenario = False
    for argument in sys.argv[1:]:
        if argument.startswith("--port="):
            port = int(argument.split("=", 1)[1])
        elif argument == "--scenario":
            with_scenario = True
        else:
            print("usage: python tools/mcp_smoke.py [--port=N] [--scenario]")
            print("  --scenario  also drive the prefab tools; closes whatever the editor has open")
            return 2

    probe = Probe(DEFAULT_HOST, port)
    try:
        run(probe)
        if with_scenario:
            run_prefab_scenario(probe)
            run_node_scenario(probe)
            run_volume_scenario(probe)
            run_view_scenario(probe)
            run_clip_scenario(probe)
            run_fsm_scenario(probe)
            run_rename_scenario(probe)
            run_copy_scenario(probe)
            run_paint_scenario(probe)
            run_machine_scenario(probe)
            run_delete_scenario(probe)
    except OSError as error:
        print(f"FAIL cannot reach Sculptor on {DEFAULT_HOST}:{port} -- {error}")
        print("     start it with: sculptor --mcp")
        return 1

    if probe.failures:
        print(f"{len(probe.failures)} check(s) failed")
        return 1
    print("all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
