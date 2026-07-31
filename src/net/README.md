# Networking subsystem

`src/net` is the small OS-owned boundary for inbound networking:

- `network_stack.*` owns TCP/UDP sockets, listening ports, stale-handle
  protection, and close-vs-I/O synchronization.
- `network_interface.*` reports the DHCP-assigned LAN address and owns the
  WPA2 hotspot/tethering interface.
- `http/http_parser.*` is the bounded, one-request HTTP/1 parser and canonical
  path mapper.
- `http/http_server.*` provides exact routes, JSON/text responses, static
  mounts, Basic authentication, CORS preflight, deferred responses, and
  round-robin streaming across up to four clients by default.

The framework is intentionally HTTP/1.1-with-close. It does not accept
transfer-coded request bodies or pipelining. WebSockets can later take over a
validated connection at the server boundary, and application handlers can add
SQLite access without putting either concern in the parser or transport.

## Built-in app example

```cpp
net::http::Router router;

net::http::DispatchResult status(
    const net::http::RequestContext &, net::http::Response &response, void *) {
  response.json("{\"ready\":true}");
  return net::http::DispatchResult::Responded;
}

router.get("/api/status", status);
router.staticFiles("/assets", "/www/my-app", openMyFile);
router.basicAuth("device", "a-long-local-password");

net::http::Server server(router);
server.start("my-app", 8080);

// From the owning service's regular update:
server.update(millis());
```

`StaticOpen` receives only an absolute path below the configured mount. URL
paths with dot components, repeated separators, backslashes, encoded
separators, controls, or malformed percent escapes never reach it.

Basic authentication is authentication, not encryption. Credentials and
request bodies remain visible to anyone able to observe the Wi-Fi link; use it
only on a trusted LAN or the device's WPA2 hotspot until a TLS server adapter
is added.

## External apps

External servers are session-owned and require an `ALLOW LAN SERVER?` prompt.
Stopping the app closes its listener, clients, streams, queued bodies, routes,
and permission records. Dynamic requests are copied into bounded PSRAM and
delivered from the normal Pink/PocketInkJS update context, never from the
socket callback.

PocketInkJS exposes `http.server.get/post/put/delete`,
`http.server.listen`, `http.server.response`, and `network.address`.
Plain returned objects are serialized as JSON. A `.pink` ABI 17 app uses
`httpRoute`, `httpStart`, `httpNextRequest`, and `httpRespond`.

The hotspot button in the Wi-Fi app creates a per-boot WPA2 password, shows it
with the hotspot address, and can coexist with a station connection. It is a
local device hotspot; it does not promise WAN/NAT forwarding.
