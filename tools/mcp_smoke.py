import http.client
import json
import sys

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


def main():
    port = DEFAULT_PORT
    for argument in sys.argv[1:]:
        if argument.startswith("--port="):
            port = int(argument.split("=", 1)[1])
        else:
            print("usage: python tools/mcp_smoke.py [--port=N]")
            return 2

    probe = Probe(DEFAULT_HOST, port)
    try:
        run(probe)
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
