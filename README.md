# CTF Multiplayer C++

Multiplayer Capture The Flag implemented in C++17 — authoritative server + playable client.

One executable runs in two modes:

- **`--server`**: authoritative server. Accepts up to 100 TCP clients, runs all game logic, and renders an observer view with Raylib.
- **`--client`**: playable client. Discovers servers via UDP, connects over TCP, and is controlled with keyboard (WASD/arrows + E).

This project implements the **CTF CC8 2026 Protocol Standard v1.2.0** (protocol `v: 1`) and is designed to interoperate with other class projects implementing the same spec. See [docs/SPEC.md](docs/SPEC.md) for the full protocol specification.

---

## Requirements

| Tool  | Version |
| ----- | ------- |
| C++   | C++17   |
| CMake | ≥ 3.20  |
| vcpkg | recent  |

Dependencies (managed via vcpkg manifest mode):
- `nlohmann/json` — JSON serialization
- `raylib` — graphics (client + server observer)
- `Catch2` — unit and integration tests

## Build

```bash
# 1. Configure using presets (detects Clang/GCC on macOS/Linux)
cmake --preset=default

# 2. Build (release)
cmake --build --preset=release

# The executable is produced at ./build/ctf
```

On Windows with MSVC:

```bash
cmake --preset=windows-msvc
cmake --build --preset=release
```

## Usage

```bash
# Server (default TCP port 8889, UDP discovery port 8888)
./build/ctf --server

# Server on a specific port
./build/ctf --server 9000

# Client (discovery menu + game)
./build/ctf --client
```

### Server

The server listens on the configured TCP port (8889 by default) and opens a UDP socket on port 8888 to answer discovery queries. It renders an 800×800 observer window showing the central circle, the flag, players, and the current phase. The server exits when the window is closed.

### Client

The client opens a Raylib graphical menu that supports:

1. Automatic discovery: UDP broadcast on the local network — found servers are shown in a table (name, state, players).
2. Manual connection: enter an IP for unicast discovery, or `IP:port` for direct TCP connection without discovery.
3. Join: enter a name (1–20 characters) and receive the server configuration.
4. Play: WASD/arrows to move, E to interact (capture/steal the flag).

## Tests

```bash
# Run all tests
ctest --test-dir build --output-on-failure

# Or run the test binary directly
./build/tests/ctf_tests
```

Test suites include:

| Suite | File | Description |
| ----- | ---- | ----------- |
| Framing | `tests/test_framing.cpp` | Line buffer, split/combined messages, `\\r\\n` tolerance, 64 KB limit |
| Messages | `tests/test_messages.cpp` | Parsing and validation of all message types and error codes |
| Domain | `tests/test_domain.cpp` | Spawn, movement, capture/steal, victory — pure game logic |
| Discovery | `tests/test_discovery.cpp` | UDP discovery server responses and invalid-message rejection |
| Integration | `tests/test_integration.cpp` | Full round cycles with headless bots (countdown abort, carrier disconnect) |

## Project structure

```
ctf-multiplayer-cpp/
├── CMakeLists.txt              # Top-level build
├── CMakePresets.json           # Presets for Clang/GCC/MSVC
├── vcpkg.json                  # vcpkg manifest
├── docs/
│   └── SPEC.md                 # Protocol spec — single source of truth
├── src/
│   ├── main.cpp                # Entry point: --server or --client
│   ├── common/                 # Shared layer
│   │   ├── constants.hpp       # Protocol constants
│   │   ├── json.hpp/.cpp       # Wrapper around nlohmann/json
│   │   ├── framing.hpp/.cpp    # LineBuffer and TCP encoder
│   │   ├── messages.hpp/.cpp   # Message catalog with validation
│   │   ├── udp_discovery.hpp/.cpp # UDP discovery (server + client)
│   │   └── net/                # Cross-platform socket abstraction
│   │       ├── platform.hpp/.cpp
│   │       ├── socket.hpp/.cpp
│   │       ├── tcp_socket.hpp/.cpp
│   │       ├── udp_socket.hpp/.cpp
│   │       └── poller.hpp/.cpp
│   ├── server/                 # Server mode
│   │   ├── server.hpp/.cpp     # Server: poll loop, sessions, phases, dispatch
│   │   ├── game.hpp/.cpp       # Domain logic: spawn, movement, capture, victory
│   │   └── server_view.hpp/.cpp# Observer view with Raylib
│   └── client/                 # Client mode
│       ├── client.hpp/.cpp     # Client: state machine, UI, networking
│       ├── input.hpp/.cpp      # InputSampler: WASD/arrows + E
│       └── game_view.hpp/.cpp  # Game rendering with Raylib
└── tests/
    ├── CMakeLists.txt
    ├── main_test.cpp
    ├── test_framing.cpp
    ├── test_messages.cpp
    ├── test_domain.cpp
    ├── test_discovery.cpp
    ├── test_integration.cpp
    └── server_view_stub.cpp    # Stub for headless tests (no Raylib)
```

## Architecture

The server is authoritative — all game logic runs server-side in a single thread using `poll()` (POSIX) / `select()` (Windows) across all sockets. Messages are processed one at a time per client in TCP arrival order. The client never sends positions or declares victory; it only sends directional `input` and `interact` commands.

Hybrid transport:
- TCP for all game communication: `join`, `input`, `interact`, `welcome`, `lobby`, `countdown`, `start`, `state`, `game_over`, `error`. One persistent connection per client with `\\n`-delimited JSON framing.
- UDP exclusively for server discovery on port 8888 — dual broadcast (`255.255.255.255` + subnet) with manual `IP:8888` and direct `IP:port` fallbacks.

Coalescence: `state` messages are coalescible (stale states are discarded for slow clients). `lobby`, `countdown`, `start`, `game_over`, and `error` are never discarded.

Phases: `LOBBY → COUNTDOWN → PLAYING → POST_GAME → LOBBY` (repeatable without reconnecting).

Cross-platform: POSIX sockets on macOS/Linux, Winsock on Windows, abstracted behind a neutral API in `src/common/net/`. No external connection libraries — only native OS headers.