# Implementation Log — CTF Multiplayer C++

> Daily development log for `ctf-multiplayer-cpp`.

## Connections

### TCP (game communication)

All game messages (`join`, `input`, `interact`, `welcome`, `lobby`, `countdown`, `start`, `state`, `game_over`, `error`) are sent over a single persistent TCP connection per client. The server listens on a configurable port (default **8889**) and accepts up to 100 simultaneous connections.

**Framing:** Each message is a compact JSON object serialised on a single line, terminated by `\n`. The receiver uses a `LineBuffer` that accumulates raw bytes and splits on `\n`, tolerating `\r\n` line endings. Messages exceeding 64 KB trigger `MESSAGE_TOO_LARGE` and close the connection.

**Coalescence:** `state` messages are coalescible — if a slow client has a pending `state` in its send queue when a new `state` arrives, the old one is replaced rather than enqueued. This prevents the server from overwhelming slow clients with stale frames. `lobby`, `countdown`, `start`, `game_over`, and `error` messages are never coalesced (they flush any pending `state` first).

**Decision vs SPEC:** The SPEC mandates a single TCP connection per client with `\n`-delimited JSON. The coalescence strategy is an implementation choice not mandated by the SPEC but consistent with §2.1 (framing) and §4.2.1 (state broadcast). No external connection libraries are used — raw POSIX (`sys/socket.h`, `poll`) on macOS/Linux, Winsock (`winsock2.h`, `select`) on Windows, abstracted in `src/common/net/`.

### UDP (server discovery)

UDP is used exclusively for server discovery on the fixed port **8888** — never for game state. The client sends a `discover{v:1}` query via dual broadcast (`255.255.255.255` + subnet broadcast calculated from `getifaddrs`), and each server responds directly with a `server_info` unicast containing its name, TCP port, state, and player count.

**Manual fallback:** The client also supports manual unicast discovery (`IP:8888`) and direct TCP connection (`IP:port`), bypassing UDP entirely.

**Decision vs SPEC:** The dual-broadcast strategy (both `255.255.255.255` and subnet) matches SPEC §1.3 exactly, maximising discovery across routers that may not forward the limited broadcast. `SO_REUSEADDR`/`SO_REUSEPORT` on the server UDP socket tolerates rapid restarts and local multi-process testing. Datagrams exceeding 64 KB or containing invalid JSON are silently discarded per SPEC §1.3.

## Architecture overview

- **TCP** for all game communication (join, input, state broadcast, game events). One connection per client. Framing: one JSON line per message, terminated by `\n`.
- **UDP** exclusively for server discovery on fixed port **8888** (broadcast + unicast).
- **Coalescence:** `state` messages are coalescible (stale states are discarded for slow clients). `lobby`, `countdown`, `start`, `game_over`, `error` are never discarded.
- **Authoritative server:** single-threaded `poll()` loop, one message at a time per client in TCP order. The server is observe-only; you play from a client connected to a server.
- **Phases:** `LOBBY → COUNTDOWN → PLAYING → POST_GAME → LOBBY` (repeatable without reconnecting).

---

## 2026-07-26 — TASK-001: Project scaffold

- **Commit:** `chore(build): scaffold C++17 project with vcpkg manifest and CMakePresets`
- **Summary:** Project base structure, CMake with vcpkg manifest mode, CMakePresets for MSVC/Clang/GCC, and initial directories.
- **Decisions/notes:** Uses vcpkg manifest mode (not FetchContent) with presets for all three compilers. nlohmann/json, raylib and catch2 as dependencies. Single executable `ctf` with `--server` / `--client` flags.
- **Files:** `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`, `vcpkg-configuration.json`, `src/main.cpp`, `src/common/CMakeLists.txt`, `src/server/CMakeLists.txt`, `src/client/CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/main_test.cpp`

## 2026-07-26 — TASK-002: Protocol constants

- **Commit:** `677b370` — `feat(common): add protocol constants from SPEC v1.2.0`
- **Summary:** All SPEC v1.2.0 constants defined as `inline constexpr` in `ctf::constants` namespace. Header-only, no .cpp needed.
- **Decisions/notes:** `victory_distance` is derived as `circle_radius + player_radius` (→ 315) per SPEC. Uses C++17 `inline constexpr` to avoid ODR issues across translation units.
- **Files:** `src/common/constants.hpp`

## 2026-07-26 — TASK-003: JSON parse/dump wrapper

- **Commit:** `39771a3` — `feat(common): add JSON parse/dump wrapper over nlohmann/json`
- **Summary:** Minimal wrapper isolating `nlohmann/json` compilation cost. `parse_line()` returns `std::optional<nlohmann::json>`, catches parse errors. `dump_compact()` serializes with no spaces/newlines and ignores invalid UTF-8.
- **Decisions/notes:** Implementation in a separate `.cpp` to avoid slow recompilation of `nlohmann/json` in every TU. The `common` library was promoted from `INTERFACE` to `STATIC` to accommodate the `.cpp` file.
- **Files:** `src/common/json.hpp`, `src/common/json.cpp`, `src/common/CMakeLists.txt`

## 2026-07-26 — TASK-004: TCP line framing

- **Commit:** `51d69f5` — `feat(common): implement TCP line framing (LineBuffer) and encoder`
- **Summary:** `LineBuffer` class accumulates raw bytes and extracts complete JSON lines by `\n`, tolerating `\r\n` endings. Throws `message_too_large_error` (error code `MESSAGE_TOO_LARGE`) when exceeding 64 KB. Free function `encode()` serializes JSON to compact string + `\n` and validates size.
- **Decisions/notes:** Default max_size uses `ctf::constants::message_max_size`. The `\r` stripping happens in `extract()` before returning the message, keeping the buffer clean.
- **Files:** `src/common/framing.hpp`, `src/common/framing.cpp`, `src/common/CMakeLists.txt`

## 2026-07-26 — TASK-005: Message catalog with validation

- **Commit:** `161375e` — `feat(common): implement message catalog with field validation`
- **Summary:** All 12 message types defined as plain structs in `namespace ctf`. `ctf::msg::from_json()` parses and validates JSON into a `std::variant<Message>`, dispatching by `type`. Per-field validation for `v`, `dir`, `name` (trim, length, control chars), required fields, and type correctness. Public `validate_*` functions for reusability. `to_json()` overloads for all server-sent messages. Error codes from SPEC §5.1 as `constexpr` string constants.
- **Decisions/notes:** Validation failures are returned as `ctf::Error{reason}` variant alternatives (no exceptions). Welcome config is validated field-by-field. `state.flag.owner` uses `std::optional<std::string>`, serializing as JSON `null` when empty. `interact` and `start` have no fields. Name trimming happens during `join` parsing before storage.
- **Files:** `src/common/messages.hpp`, `src/common/messages.cpp`, `src/common/CMakeLists.txt`

## 2026-07-26 — TASK-006: Cross-platform socket abstraction

- **Commit:** `2a2e8b7` — `feat(net): add cross-platform socket abstraction (POSIX/Winsock) and poller`
- **Summary:** Complete POSIX/Winsock abstraction layer in `src/common/net/`. `TcpSocket` (RAII, non-copyable, movable) with `listen()`/`accept()`/`recv()`/`send()` in non-blocking mode. `UdpSocket` with `bind()` (SO_REUSEADDR/SO_REUSEPORT) and `make_broadcast()` (SO_BROADCAST). `Poller` wrapping `poll()` on POSIX and `select()` on Windows. `platform.hpp`/`.cpp` for one-time `WSAStartup`/`WSACleanup`.
- **Decisions/notes:** On macOS `EWOULDBLOCK == EAGAIN` (same value 35), so the error string switch only uses `EAGAIN`. The poller uses `select()` on Windows because `WSAPoll` has known bugs. UDP bind enables both `SO_REUSEADDR` and `SO_REUSEPORT` for local testing with multiple processes. No external connection libraries used — only native OS headers.
- **Files:** `src/common/net/platform.hpp`, `src/common/net/platform.cpp`, `src/common/net/socket.hpp`, `src/common/net/socket.cpp`, `src/common/net/tcp_socket.hpp`, `src/common/net/tcp_socket.cpp`, `src/common/net/udp_socket.hpp`, `src/common/net/udp_socket.cpp`, `src/common/net/poller.hpp`, `src/common/net/poller.cpp`, `src/common/CMakeLists.txt`

## 2026-07-26 — TASK-007/008: UDP discovery (server + client)

- **Commit:** `3487e2a` — `feat(net): implement UDP discovery server and client`
- **Summary:** `DiscoveryServer` with `tick()` loop that responds to `discover` (v==1) queries with `server_info` via `ServerStateProvider` interface. `DiscoveryClient` with dual broadcast (`255.255.255.255` + subnet via `getifaddrs`), manual unicast mode, and `parse_ip_port()` utility for direct `IP:port` TCP fallback. All UDP datagrams exceeding 64 KB or containing invalid JSON are silently discarded.
- **Decisions/notes:** Both server and client live in the same module (`udp_discovery.hpp/.cpp`). The server holds a `const` reference to `ServerStateProvider` (no ownership). Client discovery uses non-blocking recv_from with `std::this_thread::sleep_for` between polls. Subnet broadcast is detected via `getifaddrs()` on POSIX, falling back to `255.255.255.255`. Server socket uses both `SO_REUSEADDR` and `SO_REUSEPORT`.
- **Files:** `src/common/udp_discovery.hpp`, `src/common/udp_discovery.cpp`, `src/common/CMakeLists.txt`

## 2026-07-26 — TASK-009: Local UDP discovery integration test

- **Commit:** `8ee3d61` — `test(net): add local UDP discovery integration test`
- **Summary:** Two Catch2 test cases in `tests/test_discovery.cpp`: (1) "Responds to valid discover" — starts a `DiscoveryServer` on a random port with a dummy provider, calls `discover_unicast("127.0.0.1", port)`, verifies name, TCP port, state, and player count. (2) "Silently discards invalid messages" — sends `discover` with `v=2` and garbage text, verifies no response, then sends a valid `v=1` discover and verifies a correct `server_info` response.
- **Decisions/notes:** Added `local_port()` to `UdpSocket` to retrieve the OS-assigned port after `bind(0)`. Fixed `UdpSocket::bind()` and `make_broadcast()` to set non-blocking mode (they were blocking, causing hangs in the polling loops). Extracted `set_non_blocking()` from `tcp_socket.cpp` into shared `socket.cpp` for reuse.
- **Files:** `tests/test_discovery.cpp`, `tests/CMakeLists.txt`, `src/common/net/socket.hpp`, `src/common/net/socket.cpp`, `src/common/net/tcp_socket.cpp`, `src/common/net/udp_socket.hpp`, `src/common/net/udp_socket.cpp`

## 2026-07-26 — TASK-010/011/012: TCP server, phase machine, countdown

- **Commit:** `9627ac2` — `feat(server): add TCP server with poll loop, phase machine, join/welcome/lobby and countdown`
- **Summary:** Core server implementation covering three sub-tasks:
  - **Part A (TCP server skeleton):** `Session` class per connection with `LineBuffer`, send queue, coalesced state, and `try_send()`. `Server` class with `TcpSocket` listener, `Poller`, session map, and 50ms poll loop. Accepts new connections, reads/dispatches messages, handles write readiness.
  - **Part B (Phase machine + join handling):** `enum class Phase { Lobby, Countdown, Playing, PostGame }`. LOBBY phase: validates `join` (version, name, duplicate, capacity), assigns unique `player_id`, sends `welcome` with fixed config, broadcasts `lobby`, triggers countdown at `min_players`. COUNTDOWN/PLAYING/PostGame guard join with `GAME_STARTED` + close. Non-join messages in lobby return `INVALID_PHASE`.
  - **Part C (Countdown timer):** When ≥2 players in lobby → transition to COUNTDOWN, broadcast `countdown{5,4,...,1}`, then `start` and transition to PLAYING. Aborts on player drop below `min_players` (broadcast `lobby`, return to Lobby). Uses `std::chrono::steady_clock` for real-time tracking.
- **Decisions/notes:** Session keeps a `joined` flag to prevent duplicate joins. Coalescence is detected by string-search for `"type":"state"`. The `send_error()` helper takes a `close_conn` flag mapping SPEC §5.1 error rules (e.g., `VERSION_MISMATCH` closes, `NAME_INVALID` does not). No timeout logic per SPEC v1 (disconnect only on TCP close or write error). `main.cpp` now accepts `--server [port]`.
- **Files:** `src/server/server.hpp`, `src/server/server.cpp`, `src/server/CMakeLists.txt`, `src/main.cpp`

## 2026-07-26 — TASK-013: Game domain

- **Commit:** `e29f018` — `feat(server): implement game domain (spawn, movement, capture/steal, victory)`
- **Summary:** Pure game domain module in `namespace ctf::game`. `GameState` with `PlayerState` (id, name, position, direction, `inside_on_pickup` flag) and `FlagState` (optional owner, position). Functions: `spawn_player()` (uniform θ ∈ [0,2π), uniform R ∈ [350,450]), `move_player()` (normalises diagonals ÷√2, applies speed × dt, clamps to [15,985]), `distance_to_centre()`, `is_inside_circle()` (≤ victory_distance), `process_interact()` (capture if free and ≤40, steal if owned by other and ≤40 to carrier, updates `inside_on_pickup`), `check_victory()` (`inside_on_pickup == true` AND distance > victory_distance), `reset_flag()`, `round_1dp()` (half-away-from-zero). RNG is injected via `std::mt19937&` for testability.
- **Decisions/notes:** Uses C++17 (`std::numbers::pi` is C++20, so a local `constexpr double pi` is used). All constants from `ctf::constants`. The `inside_on_pickup` is set at capture time only, not at spawn (spawn is always outside by construction). Tick order per SPEC: movement → victory check → interactions.
- **Files:** `src/server/game.hpp`, `src/server/game.cpp`

## 2026-07-26 — TASK-014: 20Hz tick, state broadcast and coalescence

- **Commit:** `b8ef817` — `feat(server): implement 20Hz tick, state broadcast and coalescence`
- **Summary:** Integrated the game domain into the server loop. `game_tick()` called during Playing phase: calculates real `dt`, moves all players, checks victory for carrier before interactions, processes pending `interact` queue (in TCP arrival order), updates flag position to carrier position, broadcasts `state` at 20 Hz. `broadcast_state()` builds `State` JSON with `flag.owner` (null if free), `flag.x/y` (carrier position or centre), `players[]` (only connected players, rounded to 1 dp). `broadcast_game_over()` sends `game_over` exactly once. `remove_player_from_game()` handles carrier disconnect → flag reset to (500,500). `process_post_game()` waits `post_game_seconds` then resets to LOBBY with `broadcast_lobby()`.
- **Decisions/notes:** `pending_interacts_` queue preserves TCP arrival order for fair desempate. `dt` is clamped to 1 second to prevent simulation explosion after debugger stops. The `tick_initialised_` flag ensures the first tick has a zero dt (no physics jump). Coalescence in `Session::queue_message()` was already present from TASK-010.
- **Files:** `src/server/server.hpp`, `src/server/server.cpp`, `src/server/CMakeLists.txt`

## 2026-07-26 — TASK-015/016: Disconnect handling and game lifecycle

- **Commit:** `b38dcf4` — `feat(server): handle disconnections and flag reset on carrier drop`
- **Summary:** Fixed critical iterator invalidation bug: `disconnect()` was erasing from `sessions_` while the `run()` loop iterated over it. Introduced `queue_disconnect()` — all code paths that need to disconnect (recv=0, write error, send_error with close_conn=true) now push to a `pending_disconnects_` vector instead. `cleanup_disconnected()` drains the queue after each event loop iteration. The `disconnect()` handler covers all SPEC §5.2 scenarios: carrier disconnect → flag to centre, countdown abort on drop below `min_players`, full reset when all sessions disconnect.
- **Decisions/notes:** Part B (game_over → POST_GAME → 5s → LOBBY cycle) was implemented in TASK-014 (`broadcast_game_over()`, `process_post_game()`). The disconnect-deferred approach prevents UB without complex iterator management. Duplicate fds in the pending queue are filtered out. No timeout logic — only TCP close/write error detection per SPEC v1.
- **Files:** `src/server/server.hpp`, `src/server/server.cpp`

## 2026-07-26 — TASK-017: Server observer view with Raylib

- **Commit:** `0e1326c` — `feat(server): add Raylib observer view for server mode`
- **Summary:** `ServerView` class wraps the Raylib observer window. Constructor takes a reference to `GameState`. On first `render()` call, initialises an 800×800 Raylib window titled "CTF Server — Observer". Each frame draws: dark background, centre circle (scaled, light gray), flag as a red triangle (or white when free), players as circles (blue for normal, red for carrier) with name labels, and a phase overlay text (LOBBY, COUNTDOWN N, PLAYING, GAME OVER — Winner: X). `should_close()` checks `WindowShouldClose()`. Integrated into `Server::run()` — created at loop start, `render()` called after cleanup with a computed phase text string, loop breaks when window is closed. `server_view.cpp` compiled as part of the `ctf` executable to avoid static library linker issues with Raylib/GLFW.
- **Decisions/notes:** The view is forward-declared in `server.hpp` to avoid pulling Raylib headers into the server library. `server_view.cpp` is compiled directly into the executable (not the static library) because Raylib on macOS needs GLFW symbols resolved at link time. Fixed the root `CMakeLists.txt` to `find_package(glfw3 CONFIG REQUIRED)` and link `glfw` — raylib's vcpkg CMake config doesn't propagate GLFW linkage automatically on macOS.
- **Files:** `src/server/server_view.hpp`, `src/server/server_view.cpp`, `src/server/server.hpp`, `src/server/server.cpp`, `CMakeLists.txt`

## 2026-07-26 — TASK-018: Client discovery menu and join flow

- **Commit:** `74b56b2` — `feat(client): add discovery menu, manual connect and join flow`
- **Summary:** Playable client application with a Raylib UI. `Client` class (`src/client/client.hpp/.cpp`) with an explicit screen state machine (`ClientState { Discovery, JoinName, Lobby, Countdown, Playing, GameOver, Disconnected }`) and separate `update_*()`/`draw_*()` methods per state. Discovery screen: broadcast discovery runs on a worker thread (`std::async`) so the "Discovering servers..." spinner stays animated; clickable server table (IP, name, state, players); "Search again", "Connect" (selected row), manual IP field + "Manual connect" (async `discover_unicast` → TCP connect using the announced `tcp_port`), and direct `IP:port` field + "Direct connect" (`parse_ip_port` → TCP, skipping discovery). Join screen: name field (max 20 chars), client-side trim/length pre-validation, sends `join{v:1, name}` on click or Enter; on `error{NAME_INVALID}` shows the reason and allows retry; on `welcome` stores `player_id` + config and transitions to Lobby. Added `TcpSocket::connect(host, port, timeout_ms)` — `getaddrinfo` resolution, non-blocking connect with `select()` timeout (avoids `WSAPoll` bugs on Windows), returns a non-blocking socket. Networking per frame: non-blocking `recv` into `LineBuffer`, line extraction → `msg::from_json` dispatch; outbound messages appended to a small send buffer drained each frame. Lobby/countdown/playing/game-over/disconnected states exist with basic placeholder rendering (full lobby/countdown views in TASK-019, game view in TASK-020+).
- **Decisions/notes:** UI is hand-drawn Raylib (`DrawRectangle`+`DrawText` buttons, custom text fields) — raygui is not a dependency. Discovery futures are checked with `wait_for(0)` each frame; the server table is only read after the broadcast future is ready, and `discover_unicast` uses only local state, so no data races. `client.cpp` is compiled directly into the `ctf` executable (same as `server_view.cpp`) so Raylib/GLFW symbols resolve at link time on macOS. `SetExitKey(0)` prevents ESC from closing the window while typing. A `lobby` message received during countdown/playing/game-over returns the client to the Lobby screen (covers countdown abort and the post-game cycle).
- **Files:** `src/client/client.hpp`, `src/client/client.cpp`, `src/common/net/tcp_socket.hpp`, `src/common/net/tcp_socket.cpp`, `src/main.cpp`, `CMakeLists.txt`

## 2026-07-26 — TASK-019: Lobby and countdown display

- **Commit:** `f6550bf` — `feat(client): implement lobby and countdown display`
- **Summary:** Full lobby screen: "In waiting room — ID: {player_id}" header, server config panel (all six values from the `welcome` config: map_size, circle_radius, player_radius, interact_radius, speed, tick_rate), and a player table (ID + name, up to 16 visible rows) refreshed on every `lobby` message, with the local player's row highlighted and tagged "(you)". Header shows "Players (N/100)". No start button — a hint explains the countdown auto-triggers at 2+ players. Full countdown screen: "Game starting" label, large centred number (~220 px) with a per-second pulse animation, and the round's player count. Transition to `Playing` on `start` was already wired in TASK-018's message dispatch; the client never sends `input`/`interact` before `start` (no input code runs outside the Playing state).
- **Decisions/notes:** The pulse animation uses `fmod(GetTime(), 1.0)` — purely cosmetic, no state. The countdown uses the server's `seconds` value verbatim (server is authoritative for timing). Lobby table is capped at 16 rows to fit the 800×800 window; the SPEC's 100-player lobby remains fully handled protocol-side.
- **Files:** `src/client/client.cpp`

## 2026-07-26 — TASK-020: Raylib game view and keyboard input

- **Commit:** `a3682e0` — `feat(client): implement Raylib game view and keyboard input`
- **Summary:** Two new client modules. `input.hpp/.cpp` — `InputSampler` reads WASD/arrows each frame into `dir_x/dir_y ∈ {-1,0,1}`, edge-detects direction changes (`take_dir_changed()`) so the client sends a single `input` message only when the value actually changes, and edge-detects E (`take_interact()`) for `interact`. `game_view.hpp/.cpp` — pure `draw_game_view(state, self_id, names)` rendering: centre circle (gray, scaled by `800/map_size`), flag as a triangle (red when owned, white when free), players as circles (green=self, red=carrier, blue=others) with name labels, and a top overlay (FPS, player count, flag owner/free). Wired into `Client`: `update_playing()` samples input and sends `input`/`interact`; `draw_playing()` delegates to `draw_game_view`. Added `player_names_` (id→name) populated from each `lobby` message so the game view can label players by name (the `state` message carries only ids). `input_` is reset on `start` so each round begins neutral.
- **Decisions/notes:** The client sends raw diagonal directions (e.g. `{-1,-1}`); the server normalises diagonals ÷√2 per SPEC, so no client-side normalisation is needed. `input` is only sent on change (not every frame) to avoid flooding the server with redundant identical messages. `game_view` is a free function (no state) for testability and to keep rendering decoupled from the Client state machine. `input.cpp`/`game_view.cpp` are compiled into the `ctf` executable alongside `client.cpp` (Raylib/GLFW link-time resolution on macOS).
- **Files:** `src/client/input.hpp`, `src/client/input.cpp`, `src/client/game_view.hpp`, `src/client/game_view.cpp`, `src/client/client.hpp`, `src/client/client.cpp`, `CMakeLists.txt`

## 2026-07-26 — TASK-021: Player leaves, game_over and lobby return

- **Commit:** `b3ba228` — `feat(client): infer player leaves, show game_over and return to lobby`
- **Summary:** Departure inference: the client keeps `known_players_` (id→`Player`) built from each `state` message. On a new state, IDs present in the previous state but missing from the new one are flagged as departed and shown as a transient "X left the game" notice (fades after ~3 s) at the bottom of the playing screen. Since the protocol has no explicit "player_left" message, departures are inferred purely from the state stream (the server removes disconnected players, so they simply stop appearing). Game Over screen now resolves the winner's display name from the lobby roster (`name_of(winner_id_)`) — "{name} won!" or "You won!" when `winner == player_id` — plus a "Returning to lobby..." hint. Lobby return: a `lobby` message received during Countdown/Playing/GameOver clears `latest_state_`, `known_players_`, `departure_notice_`, and `winner_id_`, then switches to the Lobby screen on the same TCP connection (no reconnect). `known_players_`/`departure_notice_` are also cleared on `start` (fresh round) and on `return_to_discovery()`.
- **Decisions/notes:** Departure detection only runs once `known_players_` is non-empty (i.e. after the first state of the round), avoiding a false "everyone left" burst on the first frame. The notice uses Raylib's `GetTime()` for fade timing (no extra chrono state). The winner name lookup falls back to the id when the roster no longer contains it (e.g. the winner disconnected before `game_over`).
- **Files:** `src/client/client.hpp`, `src/client/client.cpp`

## 2026-07-26 — TASK-022: Server disconnect detection and return to discovery

- **Commit:** `9f687a3` — `feat(client): detect server disconnect and return to discovery menu`
- **Summary:** Server disconnect is now detected on both sides of the TCP connection. The recv path (recv returns 0) was already handled in TASK-018 (`on_server_disconnected`). Added send-error detection: `TcpSocket::send()` now returns -1 for EAGAIN/EWOULDBLOCK (retry later) and -2 for a real error (broken connection), instead of collapsing both to -1. The client's `flush_send()` treats -2 as a fatal connection error and calls `on_server_disconnected("Connection lost")`, which closes the socket, clears buffers, and transitions to `ClientState::Disconnected`. The Disconnected screen shows "Server disconnected" + the reason + a "Back to menu" button that calls `return_to_discovery("")` (clears all session state, restarts broadcast discovery). This is purely local TCP close detection — not a protocol message.
- **Decisions/notes:** The -1/-2 split is safe because only the client uses `TcpSocket::send()`; the server's `Session::try_send()` calls the raw `::send()` syscall directly and already distinguishes EAGAIN from real errors via errno/WSAGetLastError. `on_server_disconnected()` is idempotent for the state transition (closing an already-closed socket is a no-op), so a recv=0 followed by a send=-2 in the same frame does not double-fire. `send_message()` early-returns when `socket_` is invalid, preventing further sends after a disconnect mid-frame.
- **Files:** `src/common/net/tcp_socket.hpp`, `src/common/net/tcp_socket.cpp`, `src/client/client.cpp`

## 2026-07-26 — TASK-023: Unit tests for framing, validation and domain

- **Commit:** `cb48150` — `test(common): add unit tests for framing, validation and domain`
- **Summary:** Three new Catch2 unit test files (31 test cases, 469 assertions total). `test_framing.cpp` (6 cases): two messages in one read, split message across reads, `\r\n` tolerance, `MESSAGE_TOO_LARGE` on >64 KB, nullopt on no-newline, and `encode()` compactness. `test_messages.cpp` (11 cases): `discover` v:1 ok / v:2 `VERSION_MISMATCH`, `input` dir.x:2 `INVALID_FIELD` / valid dir ok, `join` empty/21-char name `NAME_INVALID` / valid name ok / whitespace trim, unknown type `UNKNOWN_TYPE`, invalid JSON nullopt, missing `type`/`dir`/`name` `MISSING_FIELD`. `test_domain.cpp` (14 cases): 100 spawns all in ring [350,450] and outside circle (>315), cardinal movement (1,0)@200u/s→x+200, diagonal (1,1) normalised ÷√2 (total ≈200), clamp to [15,985], victory at dist 316 / no victory at 315 / no victory when `inside_on_pickup` false, capture at dist 40 / fail at 41, steal at dist 40 / fail at 41, `reset_flag` clears owner and centres. Also added `TcpSocket::local_port()` (needed for integration tests) and made `Server` headless-capable (`headless` constructor flag + `stop()` method + `port()` accessor) so the server can run in a background thread without a Raylib window.
- **Decisions/notes:** `Approx` is in `catch2/catch_approx.hpp` under `Catch::` namespace (added `using Catch::Approx`). `game.hpp` doesn't include `constants.hpp`, so the test includes it explicitly. The headless refactor is minimal: `run()` skips `ServerView` creation when `headless_`, checks `running_` atomic instead of `WindowShouldClose()`, and skips `view_->render()`. `stop()` sets `running_ = false` for clean shutdown from the test thread. The `local_port()` implementation mirrors `UdpSocket::local_port()` using `getsockname()`.
- **Files:** `tests/test_framing.cpp`, `tests/test_messages.cpp`, `tests/test_domain.cpp`, `tests/CMakeLists.txt`, `src/common/net/tcp_socket.hpp`, `src/common/net/tcp_socket.cpp`, `src/server/server.hpp`, `src/server/server.cpp`

## 2026-07-26 — TASK-024: Headless integration tests with BotClient

- **Commit:** `c9e9cbf` — `test(server): add integration test with 4 bot clients`
- **Summary:** Full headless integration test suite using a `BotClient` helper class that connects, joins, polls messages, and plays automatically (moves toward the flag, captures it, runs away for victory). Three test cases in `test_integration.cpp`:
  - **Full game lifecycle (2 bots):** Both bots join, wait for countdown → start, play until `game_over`, and verify the same winner is declared to both. Then waits for `lobby` broadcast (post-game pause) to confirm the cycle repeats.
  - **Countdown abort on player drop:** Two bots join, one disconnects before countdown finishes — verifies no `start` is broadcast and the remaining bot stays in lobby.
  - **Carrier disconnect resets flag:** Bot A captures the flag, then disconnects — Bot B observes the flag reset to centre (null owner, position 500,500) in the next `state`.
  - Also includes all server-side fixes needed for headless testing: `TcpSocket::release()` to preserve fd ownership across `accept`, deferred countdown trigger (from main loop instead of `handle_join`) to prevent race conditions with simultaneous joins, tick-rate guard (skip tick if `dt < 0.05` to enforce ~20 Hz), and `ServerViewDeleter` custom deleter with a test stub (`server_view_stub.cpp`) to avoid Raylib linkage in the test target.
- **Decisions/notes:** The BotClient uses a simple go-toward-centre → interact → go-away strategy without pathfinding (sufficient for deterministic tests). Server runs on port 0 (OS-assigned) to avoid port conflicts. All tests use `std::chrono` deadlines with 10 ms polling to keep them fast but reliable. The tick-rate guard (`dt < 0.05`) prevents the double-tick bug that occurred when `poll()` returned early due to I/O activity. The deferred countdown start ensures simultaneous joins from multiple bots all land in Lobby phase before the countdown begins. `release()` on the accepted `TcpSocket` prevents the temporary RAII object's destructor from closing the fd.
- **Files:** `tests/test_integration.cpp`, `tests/server_view_stub.cpp`, `tests/CMakeLists.txt`, `src/server/server.cpp`, `src/server/server.hpp`, `src/server/server_view.cpp`, `src/common/net/tcp_socket.hpp`, `src/common/net/tcp_socket.cpp`


## 2026-07-28 — TASK-025: Fix macOS Raylib window not appearing

- **Commit:** `0d421d6` — `fix(macOS): make GUI windows appear on macOS`
- **Summary:** The launch scripts (`./run-server.sh`, `./run-client.sh`) used `open -n build/ctf.app --args ...` and were returning immediately with no window. Diagnosis revealed **two independent bugs** that were masking each other:
  1. **The `.app` bundle was empty.** `ctf.app/Contents/MacOS/` had no executable, so `open` failed with `"The application cannot be opened because its executable is missing."` and exited with code 1. The reason: `glfw` had been (uncommitted) removed from `target_link_libraries(ctf ...)` — Raylib's vcpkg CMake config does not propagate GLFW linkage on macOS, so the link failed with undefined `_glfwWindowShouldClose`/`_InitPlatform` references. The link target is `ctf.app/Contents/MacOS/ctf` (because of `MACOSX_BUNDLE`), so a failed link left the bundle's `MacOS/` folder empty. Restoring `glfw` to the link line made the link succeed and the binary land in the bundle automatically — no `POST_BUILD` copy step needed.
  2. **Direct binary launch had no WindowServer access.** Even with a working bundle, running `./build/ctf --client` from a terminal showed `"Not Responding"` in Activity Monitor because macOS treats a terminal-launched binary as a background process. The existing `src/common/app_activation.hpp` already implemented `TransformProcessType(&psn, kProcessTransformToForegroundApplication)` for exactly this case, but it was never included anywhere. Wired it up in `Client::run()` and `ServerView::render()` BEFORE `InitWindow()`, and added `-framework ApplicationServices` to the `common` library (where `TransformProcessType` lives — it is NOT in the Cocoa umbrella pulled in by Raylib).
- **Decisions/notes:** With `MACOSX_BUNDLE` on and the link line correct, the bundle is populated automatically by every build — no manual `POST_BUILD` copy needed. The `ClearWindowState(FLAG_WINDOW_HIDDEN) + RestoreWindow() + SetWindowPosition()` blocks in `Client::run()`/`ServerView::render()` (already in the working tree) are kept as belt-and-suspenders. `open -n` allows multiple instances (server + client simultaneously). `app_activation.hpp` is a header-only `inline` function so no `.cpp` is needed; the framework link is `PUBLIC` on `common` so it propagates transitively to the `ctf` executable. All 31 unit + integration tests pass after the change.
- **Files:** `CMakeLists.txt`, `src/common/CMakeLists.txt`, `src/common/app_activation.hpp`, `src/client/client.cpp`, `src/server/server_view.cpp`, `src/Info.plist`, `run-server.sh`, `run-client.sh`, `README.md`

---

## Cross‑project interoperability tests (Parte A del enunciado)

These tests verify that `ctf-multiplayer-cpp` interoperates with other CC8 2026 projects that implement SPEC v1 (protocol `v: 1`). Each test must be executed in both directions:

> **My client → Their server** and **Their client → My server**

Results will be recorded when a classmate's project is available for testing. The plan below covers all 13 tests from the standard verification checklist.

| ID | Description | My→Theirs | Theirs→Mine | Notes |
| :-- | :---------- | :-------- | :---------- | :---- |
| **TEST-001** | **Discovery:** broadcast finds server; manual unicast `IP:8888` works; manual `IP:port` (TCP direct) works | ⬜ | ⬜ | Both discovery paths must be tested. |
| **TEST-002** | **Framing:** two messages in one TCP read, split across reads, `\r\n` tolerance — all decode correctly | ⬜ | ⬜ | Our `LineBuffer` handles these; verify theirs does too. |
| **TEST-003** | **No timeout:** 60 s idle — connection stays open, no unexpected close | ⬜ | ⬜ | SPEC v1 explicitly has no timeout (§5.2). |
| **TEST-004** | **Single capture:** two players interact with the flag in quick succession → only the first gets ownership | ⬜ | ⬜ | Test both TCP arrival order fairness. |
| **TEST-005** | **Steal outside circle:** carrier leaves the centre circle, opponent steals — no instant victory on steal | ⬜ | ⬜ | Steal does not reset `inside_on_pickup`. |
| **TEST-006** | **Join in countdown/playing:** late join receives `error{GAME_STARTED}` + connection close | ⬜ | ⬜ | Verify both countdown and playing phases. |
| **TEST-007** | **Carrier disconnect:** flag resets to centre (500,500) with `owner: null` in next `state` | ⬜ | ⬜ | Our implementation does this; verify theirs does too. |
| **TEST-008** | **Victory:** distance > 315 from centre while carrying → `game_over`; exactly 315 → no win | ⬜ | ⬜ | Boundary test for `victory_distance`. |
| **TEST-009** | **Invalid names:** empty, 21+ chars, control chars → `error{NAME_INVALID}` (no close) | ⬜ | ⬜ | Trimmed name must be 1–20 UTF-8. |
| **TEST-010** | **Slow client:** coalescence works — slow reader receives only latest `state` | ⬜ | ⬜ | Induce client-side delay, verify no queue buildup or disconnect. |
| **TEST-011** | **Malformed messages:** bad JSON, unknown type, missing fields → correct error codes (`INVALID_JSON`, `UNKNOWN_TYPE`, `MISSING_FIELD`) | ⬜ | ⬜ | No close unless SPEC says so. |
| **TEST-012** | **Two rounds:** play a full round (`game_over` → wait → `lobby`), then a second round without reconnecting | ⬜ | ⬜ | Both clients on same TCP connection. |
| **TEST-013** | **Countdown abort:** player drops below `min_players` during countdown → `lobby` broadcast, no `start` | ⬜ | ⬜ | Test with 2 players, disconnect 1. |