import base64
import http.client
import json
import pathlib
import shutil
import struct
import sys

SCRATCH_PREFABS = ("_mcp_smoke", "_mcp_smoke_copy")
SCRATCH_CLIP = "_mcp_smoke_clip"

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
        and "animations/a_idle.voxa" in assets.get("clips", [])
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
        head.get("parent") == "root" and head.get("position") == [0, 9.5, 0] and head.get("scale") == [1, 1, 1],
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
    probe.check("new volumes are written into the folder of the prefab", volumes == ["body.voxm", "hand.voxm"], str(volumes))

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

    ok, floor = tool(probe, "volume_write", {"node": "box", "boxes": [{"min": [0, 0, 0], "max": [3, 0, 1], "voxel": "gray_5"}]})
    probe.check("volume_write fills a box", ok and floor.get("voxel_count") == 8 and floor.get("cells_written") == 8, str(floor))

    ok, state = tool(probe, "editor_state")
    probe.check(
        "volume_write opens the volume in the editor",
        ok and state.get("context") == "volume" and state.get("context_stack") == [{"kind": "volume", "node": "box"}],
        str(state.get("context_stack")),
    )

    ok, dotted = tool(probe, "volume_write", {"node": "box", "points": [{"voxel": "red_2", "at": [[1, 1, 0], [2, 1, 0]]}]})
    probe.check("volume_write sets points", ok and dotted.get("voxel_count") == 10, str(dotted))

    ok, read = tool(probe, "volume_get", {"node": "box"})
    layers = read.get("layers", {}) if ok else {}
    probe.check("volume_get bounds the occupied voxels", ok and read.get("occupied") == {"min": [0, 0, 0], "max": [3, 1, 1]}, str(read.get("occupied")))
    probe.check("the legend puts the most common voxel first", layers.get("legend") == {"a": "gray_5", "b": "red_2"}, str(layers.get("legend")))
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

    ok, erased = tool(probe, "volume_write", {"node": "box", "layers": {"legend": {"g": "gray_5"}, "slices": [[".ggg"]]}})
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
    ok, twin = tool(probe, "node_create", {"name": "twin", "parent": "root", "volume": {"ref": f"models/{scratch}/box.voxm"}})
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
        and not (models / "box.voxm").exists() and renamed.get("shared_with") == ["twin"],
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
    tool(probe, "volume_write", {"node": "cube", "boxes": [{"min": [0, 0, 0], "max": [3, 3, 3], "voxel": "red_3"}]})

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

    tool(probe, "view_set", {"node": "cube", "from": "iso"})

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

    tool(probe, "volume_write", {"node": "cube", "boxes": [{"min": [0, 0, 0], "max": [3, 3, 3], "voxel": "green_3"}]})
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
    probe.check("clip_open names the clip files for an unknown one", not ok and "a_idle" in text, str(text))

    ok, idle = tool(probe, "clip_open", {"name": "a_idle"})
    probe.check(
        "clip_open opens and selects a clip",
        ok and idle.get("rig") == "humanoid" and idle.get("selected") is True and idle.get("unsaved") is False,
        str(idle),
    )

    ok, state = tool(probe, "editor_state")
    probe.check(
        "clip_open brings the editor into the clip",
        ok and state.get("context") == "clip" and state.get("clip") == "a_idle" and state.get("open_clips") == ["a_idle"],
        str(state),
    )

    ok, idle = tool(probe, "clip_get")
    head_keys = keys_of(idle, "head") if ok else None
    probe.check(
        "clip_get lists the keys of a target in the order of time",
        head_keys is not None and head_keys[0].get("time") == 0 and head_keys[0].get("position") == [0, 9.5, 0]
        and [key["time"] for key in head_keys] == sorted(key["time"] for key in head_keys) and idle.get("duration") == 2,
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

    ok, text = tool(probe, "clip_create", {"name": "a_walk"})
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

    ok, saved = tool(probe, "clip_save")
    clip_file = asset_root / "animations" / f"{SCRATCH_CLIP}.voxa"
    probe.check(
        "clip_save writes the clip with its rig",
        ok and saved.get("unsaved") is False and clip_file.is_file() and "rig humanoid" in clip_file.read_text(encoding="utf-8"),
        str(saved),
    )

    ok, in_memory = tool(probe, "clip_get")
    ok, closed = tool(probe, "clip_close")
    probe.check("clip_close closes the clip", ok and closed.get("open_clips") == ["a_idle"], str(closed))

    ok, state = tool(probe, "editor_state")
    probe.check(
        "closing the selected clip returns to the prefab and its rest pose",
        ok and state.get("context") == "prefab" and close_to(head_position(probe), [0, 9.5, 0]),
        f"{state.get('context')} {head_position(probe)}",
    )

    tool(probe, "clip_open", {"name": SCRATCH_CLIP})
    ok, from_disk = tool(probe, "clip_get")
    probe.check(
        "the saved clip reads back the same from disk",
        ok and close_to(from_disk.get("tracks"), in_memory.get("tracks")) and from_disk.get("rig") == "humanoid",
        json.dumps(from_disk.get("tracks")),
    )

    tool(probe, "clip_set_keys", {"keys": [{"target": "head", "time": 2, "position": [0, 9.5, 0]}]})
    ok, text = tool(probe, "clip_close")
    probe.check("clip_close refuses over unsaved keys", not ok and "discard_unsaved" in text, str(text))
    ok, text = tool(probe, "prefab_close")
    probe.check("prefab_close names the unsaved clip", not ok and f"the clip {SCRATCH_CLIP}" in text, str(text))
    ok, closed = tool(probe, "clip_close", {"discard_unsaved": True})
    probe.check("clip_close drops them when told to", ok, str(closed))

    tool(probe, "clip_open", {"name": "a_idle"})
    tool(probe, "clip_pose_at", {"time": 1.2})
    posed_head = head_position(probe)
    ok, _ = tool(probe, "node_set_transform", {"name": "body", "scale": [1, 1, 1]})
    ok, state = tool(probe, "editor_state")
    probe.check(
        "a node tool leaves the clip and restores the rest pose first",
        ok and state.get("context") == "prefab" and not close_to(posed_head, [0, 9.5, 0]) and close_to(head_position(probe), [0, 9.5, 0]),
        f"{state.get('context')} posed {posed_head} now {head_position(probe)}",
    )

    tool(probe, "clip_close", {"clip": "a_idle", "discard_unsaved": True})
    tool(probe, "prefab_close", {"discard_unsaved": True})
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
