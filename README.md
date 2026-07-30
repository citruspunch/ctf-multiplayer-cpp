# CTF Multiplayer C++

Multiplayer Capture The Flag implemented in C++17 — authoritative server + playable client.

One executable runs in two modes:

- **`--server`**: authoritative server. Accepts up to 100 TCP clients, runs all game logic, and renders an observer view with Raylib (redesigned lobby view with START button, IP display, player roster table, and ambient particles).
- **`--client`**: playable client. Discovers servers via UDP, connects over TCP, and is controlled with keyboard (WASD/arrows + E). Features a polished UI with a discovery menu, join screen, lobby, countdown, game field, and game-over screen.

This project implements the **CTF CC8 2026 Protocol Standard v1.2.0** (protocol `v: 1`) and is designed to interoperate with other class projects implementing the same spec. See [docs/SPEC.md](docs/SPEC.md) for the full protocol specification.

> **Project Documentation:** [`Documentación de Implementación - Captura la Bandera C++.pdf`](./docs/Documentación%20de%20Implementación%20-%20Captura%20la%20Bandera%20C++.pdf)
> **Daily Development Log:** [`docs/IMPLEMENTATION.md`](./docs/IMPLEMENTATION.md) — chronologically tracks every implemented task with Git commit references

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
- `glfw3` — GLFW backend for Raylib (macOS)
- `Catch2` — unit and integration tests

No external connection libraries are used — only native OS sockets (POSIX on macOS/Linux, Winsock on Windows), abstracted in `src/common/net/`.

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

### macOS (requires app bundle)

On macOS, command-line binaries cannot display GUI windows directly. The build
automatically creates a `.app` bundle at `build/ctf.app`. Use the provided
launch scripts:

```bash
# Server (default TCP port 8889, UDP discovery port 8888)
./run-server.sh

# Server on a specific port
./run-server.sh 9000

# Client (discovery menu + game)
./run-client.sh
```

### Linux / Windows

```bash
# Server (default TCP port 8889, UDP discovery port 8888)
./build/ctf --server

# Server on a specific port
./build/ctf --server 9000

# Client (discovery menu + game)
./build/ctf --client
```

### Headless mode (for tests)

```bash
./build/ctf --server --headless
```

The headless mode runs the game logic without opening an observer window.
Useful for integration tests or running a server without a display.

## Client screens

| Screen | Description |
| ------ | ----------- |
| **Discovery** | UDP broadcast discovery with a server table (IP, name, state, players). Supports manual unicast (`IP`) and direct TCP (`IP:port`) connection. |
| **Join** | Enter a display name (1–20 characters). Validated server-side; errors shown inline. |
| **Lobby** | Centered player roster table with status indicators (green dot + "ready"), compact server config panel, ambient floating particles, bottom status bar. Press H for the How-to-Play overlay. |
| **Countdown** | Large pulsing number with color transition (green → yellow → red), black shadow, expanding decorative rings, player roster. |
| **Playing** | Game field with background grid, enhanced center circle ("BASE" label), redesigned flag (gold/orange when free, red pulsating when carried, bobbing animation on carrier), player circles with outer rings, name backgrounds, direction indicator for self, pulsating glow on flag carrier. HUD: FPS counter, elapsed match time (M:SS), player count, flag status, bottom control hints. |
| **Game Over** | Winner banner (`*** X WINS! ***`), match duration, player roster with winner marked, pulsing "Returning to lobby..." message. |

## Server observer

The server opens an 800×800 observer window:

- **Lobby view** (no game in progress): centered title, server IP with status dot, dynamic status text, player table (#, ID, Name), START GAME button (clickable when ≥2 players), ambient particles.
- **Game view** (countdown/playing/post-game): background grid, center circle with "BASE" label, flag rendering, player circles with name backgrounds, phase overlay (yellow text + FPS + player count).

The server can also auto-start in headless mode when the minimum player count is reached.

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
| Framing | `tests/test_framing.cpp` | Line buffer, split/combined messages, `\r\n` tolerance, 64 KB limit |
| Messages | `tests/test_messages.cpp` | Parsing and validation of all message types and error codes |
| Domain | `tests/test_domain.cpp` | Spawn, movement, capture/steal, victory — pure game logic |
| Discovery | `tests/test_discovery.cpp` | UDP discovery server responses and invalid-message rejection |
| Integration | `tests/test_integration.cpp` | Full round cycles with headless bots (countdown abort, carrier disconnect, two rounds) |

## Project structure

```
ctf-multiplayer-cpp/
├── CMakeLists.txt              # Top-level build
├── CMakePresets.json           # Presets for Clang/GCC/MSVC
├── vcpkg.json                  # vcpkg manifest (nlohmann/json, raylib, glfw, Catch2)
├── vcpkg-configuration.json
├── .gitignore
├── README.md
├── run-server.sh               # macOS launch script for server
├── run-client.sh               # macOS launch script for client
├── docs/
│   ├── SPEC.md                 # Protocol spec v1.2.0 — single source of truth
│   ├── project-guidelines.md   # Original project guidelines (enunciado)
│   └── Documentación de Implementación - Captura la Bandera C++.pdf  # Project documentation
├── src/
│   ├── main.cpp                # Entry point: --server or --client
│   ├── Info.plist              # macOS bundle metadata
│   ├── common/                 # Shared layer
│   │   ├── CMakeLists.txt
│   │   ├── constants.hpp       # Protocol constants
│   │   ├── json.hpp/.cpp       # Wrapper around nlohmann/json
│   │   ├── framing.hpp/.cpp    # LineBuffer and TCP encoder
│   │   ├── messages.hpp/.cpp   # Message catalog with validation
│   │   ├── gui_helpers.hpp/.cpp# Reusable UI elements (button, particles, colours)
│   │   ├── udp_discovery.hpp/.cpp # UDP discovery (server + client)
│   │   ├── app_activation.hpp/.mm  # macOS process-to-foreground + menu bar
│   │   └── net/                # Cross-platform socket abstraction
│   │       ├── platform.hpp/.cpp    # WSA init/cleanup, get_local_ipv4()
│   │       ├── socket.hpp/.cpp      # Base socket wrapper
│   │       ├── tcp_socket.hpp/.cpp  # TCP listen/connect/send/recv
│   │       ├── udp_socket.hpp/.cpp  # UDP bind/sendto/recvfrom
│   │       └── poller.hpp/.cpp      # poll() (POSIX) / select() (Windows)
│   ├── server/                 # Server mode
│   │   ├── CMakeLists.txt
│   │   ├── server.hpp/.cpp     # Server: poll loop, sessions, phases, dispatch
│   │   ├── game.hpp/.cpp       # Domain logic: spawn, movement, capture, victory
│   │   └── server_view.hpp/.cpp# Observer view with Raylib (LobbyView + GameView)
│   └── client/                 # Client mode
│       ├── CMakeLists.txt
│       ├── client.hpp/.cpp     # Client: state machine, UI screens, networking
│       ├── input.hpp/.cpp      # InputSampler: WASD/arrows + E
│       └── game_view.hpp/.cpp  # Game field rendering with Raylib
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

### Hybrid transport

- **TCP** for all game communication: `join`, `input`, `interact`, `welcome`, `lobby`, `countdown`, `start`, `state`, `game_over`, `error`. One persistent connection per client with `\n`-delimited JSON framing (max 64 KB per message).
- **UDP** exclusively for server discovery on port 8888 — dual broadcast (`255.255.255.255` + subnet) with manual `IP:8888` and direct `IP:port` fallbacks.

### Coalescence

`state` messages are coalescible (stale states are discarded for slow clients). `lobby`, `countdown`, `start`, `game_over`, and `error` are never discarded — they flush any pending coalesced state first.

### Phases

```
LOBBY → COUNTDOWN → PLAYING → POST_GAME → LOBBY
```

Repeatable without reconnecting. The countdown aborts if players drop below the minimum of 2.

### Cross-platform

POSIX sockets on macOS/Linux, Winsock on Windows, abstracted behind a neutral API in `src/common/net/`. No external connection libraries — only native OS headers.

### Game field rendering

The game field features:
- Subtle background grid (50-unit spacing) with map border
- Centre circle with radial gradient, translucent fill, bold outline, and "BASE" label
- Players rendered as coloured circles with outer rings, semi-transparent name backgrounds, direction indicator for self, and pulsating glow for flag carrier
- Flag with pole, waving fabric animation, gold/orange colour when free (high contrast against the grey circle), red with bobbing animation when carried
- HUD overlay: FPS counter (rolling average), elapsed match time, player count, flag status, and keyboard control hints

### Observer UI

The server observer splits into two visual modes:
- **Lobby View** (during LOBBY phase): no game field, centered information panel with server IP, player roster, status, and a styled START GAME button
- **Game View** (during COUNTDOWN / PLAYING / POST_GAME): game field with phase overlay