# shr

A decentralized, peer-to-peer file transfer and messaging tool written in C++17. No central servers. No accounts. No cloud. Just two machines, a UUID, and an encrypted connection.

```
$ shr install
Installation complete. Your ID: a1b2c3d4-e5f6-7890-abcd-ef1234567890

$ shr send ~/documents/report.pdf b9f3a1c2-d4e5-6f78-90ab-cdef12345678
Sending report.pdf to b9f3a1c2-d4e5-6f78-90ab-cdef12345678...
[####################] 100%  2.41 MB / 2.41 MB    4.20 MB/s
Transfer complete: report.pdf (2.41 MB)
```

---

## Table of Contents

- [Overview](#overview)
- [Features](#features)
- [Architecture](#architecture)
- [Dependencies](#dependencies)
- [Building](#building)
  - [Linux / Debian / Ubuntu](#linux--debian--ubuntu)
  - [macOS](#macos)
  - [Termux (Android)](#termux-android)
  - [Windows](#windows)
- [Quick Start](#quick-start)
- [Command Reference](#command-reference)
  - [Identity](#identity)
  - [File Transfer](#file-transfer)
  - [Messaging](#messaging)
  - [Network & Peers](#network--peers)
  - [Utility](#utility)
- [Configuration](#configuration)
  - [Config File](#config-file)
  - [Environment Variables](#environment-variables)
- [Protocol](#protocol)
  - [Packet Format](#packet-format)
  - [Packet Types](#packet-types)
  - [Transfer Flow](#transfer-flow)
  - [Message Flow](#message-flow)
- [Security](#security)
- [Storage & File Layout](#storage--file-layout)
- [Source Layout](#source-layout)
- [Module Reference](#module-reference)
- [Error Codes](#error-codes)
- [Limitations & Known Issues](#limitations--known-issues)
- [Contributing](#contributing)

---

## Overview

`shr` is a single-binary CLI tool for transferring files and sending messages directly between peers. It generates a UUID v4 identity on first run, stores all state in a local SQLite database, and communicates over TCP using a custom framed binary protocol with per-packet HMAC authentication.

There is no signup, no relay server, and no third-party dependency at runtime. Two users exchange their UUIDs out-of-band, connect once with `shr connect`, and can then send files and messages freely.

---

## Features

- **Zero-server P2P** — direct TCP connections between peers, no relay required
- **UUID-based identity** — globally unique, human-shareable IDs; no usernames or passwords
- **Resumable file transfers** — interrupted transfers pick up from the last acknowledged chunk
- **1 MB chunked transfers** — SHA-256 integrity check per chunk plus whole-file verification on completion
- **Concurrent transfers** — up to 5 simultaneous sends or receives
- **Encrypted keypair** — Curve25519 keypair generated at install time and stored locally
- **HMAC-authenticated packets** — every packet carries a 32-byte HMAC-SHA256 tag
- **AES-256-GCM encryption** — session payloads encrypted with keys derived via X25519 key exchange
- **SQLite persistence** — all peers, messages, and transfer metadata survive restarts
- **WAL journal mode** — crash-safe writes; no data loss on unexpected exit
- **Local network discovery** — UDP broadcast finds other `shr` instances on the LAN automatically
- **Paginated inbox** — message history with unread tracking
- **Progress display** — live transfer progress bar with speed metric
- **Retry logic** — configurable retry count and delay on failed chunk sends
- **Path traversal prevention** — all received filenames are sanitized before writing to disk
- **Cross-platform** — Linux, macOS, Windows (MSVC / MinGW), Android (Termux)
- **Environment variable overrides** — port, log level, verbosity all overridable without editing config

---

## Architecture

```
┌──────────────────────────────────────────────────────────┐
│                        shr binary                        │
│                                                          │
│  main.cpp ──► command dispatch                           │
│                                                          │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌─────────┐ │
│  │ identity │  │ transfer │  │messaging │  │  peers  │ │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬────┘ │
│       │             │              │              │      │
│  ┌────▼─────────────▼──────────────▼──────────────▼────┐ │
│  │                   network layer                      │ │
│  │         TCP accept loop + UDP broadcast              │ │
│  └──────────────────────┬───────────────────────────────┘ │
│                         │                                │
│  ┌──────────────────────▼───────────────────────────────┐ │
│  │  crypto (X25519 · AES-256-GCM · HMAC-SHA256)         │ │
│  └──────────────────────────────────────────────────────┘ │
│                                                          │
│  ┌──────────────────────────────────────────────────────┐ │
│  │  database (SQLite3 WAL)                              │ │
│  │  tables: peers · messages · transfers                │ │
│  └──────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────┘
```

All singletons use the `static instance()` pattern. Initialization order: `Config → Logger → Identity → Database → Crypto → Network`.

---

## Dependencies

| Library | Version | Purpose |
|---------|---------|---------|
| OpenSSL | ≥ 1.1.1 | SHA-256, HMAC-SHA256, X25519 key exchange, AES-256-GCM |
| SQLite3 | ≥ 3.35 | Persistent storage for peers, messages, transfers |
| nlohmann/json | ≥ 3.10 | JSON config file parsing; CBOR packet payload serialization |
| C++17 stdlib | — | `std::filesystem`, `std::optional`, structured bindings |
| CMake | ≥ 3.16 | Build system |
| pthreads | — | Accept loop thread, beacon thread, per-send threads |

All runtime dependencies are dynamically linked by default. Pass `-DBUILD_SHARED_LIBS=OFF` and supply static archives if you want a fully static binary.

---

## Building

### Download Source

```bash
curl -o shr.tar.xz https://github.com/wonderingtrader/shr/raw/main/shr.tar.xz
tar -xf shr.tar.xz
cd shr
```

### Linux / Debian / Ubuntu

```bash
sudo apt install build-essential cmake libssl-dev libsqlite3-dev nlohmann-json3-dev

cd shr && mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install        # installs to /usr/local/bin/shr
```

### macOS

```bash
brew install cmake openssl sqlite nlohmann-json

cd shr && mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release \
         -DOPENSSL_ROOT_DIR=$(brew --prefix openssl)
make -j$(sysctl -n hw.ncpu)
```

### Termux (Android)

```bash
pkg update && pkg install clang cmake openssl sqlite nlohmann-json

cd shr && mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4
cp shr $PREFIX/bin/
```

> **Note for Termux users:** `~/.shr/` resolves to `/data/data/com.termux/files/home/.shr/` and `~/shr_received/` to the equivalent home subdirectory. Both are created automatically on `shr install`.

### Windows

Using MSYS2 / MinGW:

```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake \
          mingw-w64-x86_64-openssl mingw-w64-x86_64-sqlite3 \
          mingw-w64-x86_64-nlohmann-json

cd shr && mkdir build && cd build
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
mingw32-make -j4
```

Using MSVC (Visual Studio 2022):

```powershell
vcpkg install openssl sqlite3 nlohmann-json
cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build . --config Release
```

---

## Quick Start

**Machine A — sender**

```bash
# 1. Install (generates UUID + keypair)
shr install
# Installation complete. Your ID: a1b2c3d4-e5f6-7890-abcd-ef1234567890

# 2. Check your ID any time
shr whoami
# a1b2c3d4-e5f6-7890-abcd-ef1234567890

# 3. Register machine B as a peer (share IDs out-of-band)
shr connect b9f3a1c2-d4e5-6f78-90ab-cdef12345678 192.168.1.42
# Peer added: b9f3a1c2-d4e5-6f78-90ab-cdef12345678 at 192.168.1.42:60000
# Ping: reachable

# 4. Send a file
shr send ~/downloads/archive.tar.gz b9f3a1c2-d4e5-6f78-90ab-cdef12345678

# 5. Send a message
shr msg b9f3a1c2-d4e5-6f78-90ab-cdef12345678 "file is on its way"
```

**Machine B — receiver**

```bash
shr install
# Installation complete. Your ID: b9f3a1c2-d4e5-6f78-90ab-cdef12345678

# Register machine A
shr connect a1b2c3d4-e5f6-7890-abcd-ef1234567890 192.168.1.11

# Check for incoming transfers
shr receive

# Read messages
shr inbox
```

On same LAN, skip manual `connect` and use discovery instead:

```bash
shr discover       # broadcasts UDP, auto-registers responding peers
shr peers          # confirm peers were found
```

---

## Command Reference

### Identity

#### `shr install`

Generates a UUID v4 identity, a Curve25519 keypair, default configuration, and initializes the SQLite database. Safe to run if already installed — it will print the existing ID and exit.

```
$ shr install
Installation complete. Your ID: a1b2c3d4-e5f6-7890-abcd-ef1234567890
Config directory: /home/user/.shr
Received files:   /home/user/shr_received
```

Files created:

| Path | Contents |
|------|----------|
| `~/.shr/config.json` | JSON configuration |
| `~/.shr/shr.db` | SQLite database |
| `~/.shr/shr.log` | Log file |
| `~/.shr/identity.key` | Curve25519 private key (owner-read only) |
| `~/.shr/identity.pub` | Curve25519 public key |
| `~/shr_received/` | Directory for received files |

#### `shr whoami`

Prints the current user's UUID to stdout.

```
$ shr whoami
a1b2c3d4-e5f6-7890-abcd-ef1234567890
```

---

### File Transfer

#### `shr send <file_path> <recipient_id>`

Sends a file to a registered peer. Accepts relative or absolute paths. The peer must be registered via `shr connect` or discovered via `shr discover` with a known IP before sending.

```
$ shr send /var/log/syslog b9f3a1c2-d4e5-6f78-90ab-cdef12345678
Sending syslog to b9f3a1c2-d4e5-6f78-90ab-cdef12345678...
[####################] 100%  14.23 MB / 14.23 MB    8.71 MB/s
Transfer complete: syslog (14.23 MB)
```

Transfer behavior:

- Splits the file into 1 MB chunks
- Sends a SHA-256 checksum with each chunk; receiver verifies before acknowledging
- On connection loss, the transfer record is saved with state `paused`; re-running `shr send` on the same file to the same peer will resume from the last acknowledged chunk
- Up to 5 concurrent outbound transfers; additional sends are rejected with an error until a slot is free
- Each chunk is retried up to 5 times (2 s delay between attempts) before the transfer is marked failed

#### `shr receive`

Lists all pending inbound transfers and prompts interactively to download them.

```
$ shr receive
Pending inbound transfers:
----------------------------------------------------------------------
[1] archive.tar.gz from a1b2c3d4-e5f6-7890-abcd-ef1234567890
    Size: 320.45 MB  Received: 0 B  ID: c3d4e5f6-...
----------------------------------------------------------------------
Enter transfer number to download (or 0 to skip): 1
```

Received files are saved to `~/shr_received/` with sanitized filenames. On completion, the whole-file SHA-256 checksum is verified against the value sent by the sender; mismatches mark the transfer as failed.

---

### Messaging

#### `shr msg <recipient_id> <message>`

Sends a UTF-8 text message to a peer. The message is delivered synchronously; the command exits with code 0 on successful delivery and 1 on failure. Multi-word messages do not need quoting — all tokens after the recipient ID are joined with spaces.

```
$ shr msg b9f3a1c2-d4e5-6f78-90ab-cdef12345678 "meeting at 3pm?"
[2025-03-22 14:01:07] Message sent to b9f3a1c2-d4e5-6f78-90ab-cdef12345678

$ shr msg b9f3a1c2-d4e5-6f78-90ab-cdef12345678 no quotes needed here either
[2025-03-22 14:01:09] Message sent to b9f3a1c2-d4e5-6f78-90ab-cdef12345678
```

#### `shr inbox [page]`

Displays received messages in reverse chronological order, 20 per page. Unread messages are marked with `*`. Reading the inbox marks all displayed messages as read.

```
$ shr inbox
Inbox (page 1)  -  2 unread
========================================================================
* [2025-03-22 14:01:07] From: a1b2c3d4-e5f6-7890-abcd-ef1234567890
  meeting at 3pm?
------------------------------------------------------------------------
* [2025-03-22 13:58:44] From: a1b2c3d4-e5f6-7890-abcd-ef1234567890
  file is on its way
------------------------------------------------------------------------

$ shr inbox 2    # page 2
```

---

### Network & Peers

#### `shr peers`

Lists all known peers with their IP, port, online status, and last-seen timestamp.

```
$ shr peers
ID                                     IP:Port            Status  Last Seen
--------------------------------------------------------------------------------
a1b2c3d4-e5f6-7890-abcd-ef1234567890  192.168.1.11:60000 online  2025-03-22 14:00:53
ff001122-3344-5566-7788-99aabbccddee  10.0.0.5:60000     offline 2025-03-21 09:12:01
```

#### `shr discover`

Broadcasts a UDP discovery packet on all local network interfaces. Any other `shr` instances on the same subnet will respond and be automatically registered as peers (without an IP until they respond to a direct connection).

```
$ shr discover
Broadcasting discovery on local network...
Discovery broadcast sent.
```

#### `shr connect <peer_id> <ip_address> [port]`

Manually registers a peer by UUID and IP address. Port defaults to 60000 if omitted. Immediately pings the peer to confirm reachability and update online status.

```
$ shr connect b9f3a1c2-d4e5-6f78-90ab-cdef12345678 192.168.1.42
Peer added: b9f3a1c2-d4e5-6f78-90ab-cdef12345678 at 192.168.1.42:60000
Ping: reachable

$ shr connect b9f3a1c2-d4e5-6f78-90ab-cdef12345678 10.8.0.3 60001
Peer added: b9f3a1c2-d4e5-6f78-90ab-cdef12345678 at 10.8.0.3:60001
Ping: unreachable
```

---

### Utility

#### `shr status`

Shows a snapshot of the local node: identity, network, active transfers, unread messages, storage usage, and peer reachability.

```
$ shr status
shr v1.0.0
--------------------------------------------------
User ID:     a1b2c3d4-e5f6-7890-abcd-ef1234567890
Listen port: 60000
Local IP:    192.168.1.11

Active/Pending Transfers: 2
--------------------------------------------------
OUT  67% report.pdf                    [in_progress]
 IN   0% dataset.csv                   [pending]

Unread messages: 3
Storage used:    128.00 MB
Peers online:    1/2
```

#### `shr config`

Prints the active configuration, including paths, port, retry settings, and log level.

```
$ shr config
shr configuration
--------------------------------------------------
user_id:        a1b2c3d4-e5f6-7890-abcd-ef1234567890
config_dir:     /home/user/.shr
db_path:        /home/user/.shr/shr.db
log_path:       /home/user/.shr/shr.log
received_dir:   /home/user/shr_received
listen_port:    60000
max_concurrent: 5
retry_limit:    5
retry_delay_ms: 2000
log_level:      info
verbose:        false

Environment overrides: SHR_PORT, SHR_VERBOSE, SHR_LOG_LEVEL
```

#### `shr clean`

Removes completed, failed, and cancelled transfer records older than 7 days from the database, and deletes any temporary files under `~/.shr/tmp/`.

```
$ shr clean
Removed completed/failed transfers older than 7 days.
Cleared temporary files.
Clean complete.
```

---

## Configuration

### Config File

Located at `~/.shr/config.json`. Created by `shr install` with defaults. Edit manually or rely on environment variables; changes take effect on the next command invocation.

```json
{
  "user_id":        "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
  "listen_port":    60000,
  "max_concurrent": 5,
  "retry_limit":    5,
  "retry_delay_ms": 2000,
  "log_level":      "info",
  "verbose":        false
}
```

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `user_id` | string | (generated) | UUID v4 identity. Do not change. |
| `listen_port` | integer | 60000 | TCP port to listen on for inbound connections. |
| `max_concurrent` | integer | 5 | Maximum simultaneous outbound file transfers. |
| `retry_limit` | integer | 5 | Chunk send retry attempts before failing a transfer. |
| `retry_delay_ms` | integer | 2000 | Milliseconds between retry attempts. |
| `log_level` | string | `"info"` | One of: `debug`, `info`, `warn`, `error`. |
| `verbose` | boolean | false | Currently reserved; increases future diagnostic output. |

### Environment Variables

Environment variables take precedence over the config file and are applied after loading it.

| Variable | Equivalent key | Example |
|----------|---------------|---------|
| `SHR_PORT` | `listen_port` | `SHR_PORT=60100 shr status` |
| `SHR_LOG_LEVEL` | `log_level` | `SHR_LOG_LEVEL=debug shr send ...` |
| `SHR_VERBOSE` | `verbose` | `SHR_VERBOSE=1 shr send ...` |

---

## Protocol

### Packet Format

Every TCP message begins with a fixed 41-byte header followed by the payload.

```
 0               1               2               3
 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  magic[0]='S' |  magic[1]='H' |  magic[2]='R' |  magic[3]='1' |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|     type      |           payload_len (32-bit, host order)     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                   hmac[0..31] (32 bytes)                       |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                   payload (payload_len bytes)                  |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

Payloads are CBOR-encoded JSON objects (via `nlohmann::json::to_cbor` / `from_cbor`). The HMAC covers the payload bytes using the session key derived from the X25519 handshake.

### Packet Types

| Hex | Name | Direction | Description |
|-----|------|-----------|-------------|
| `0x01` | `Handshake` | → peer | Initiate key exchange, carry public key |
| `0x02` | `HandshakeAck` | ← peer | Acknowledge handshake, carry peer public key |
| `0x10` | `FileOffer` | → peer | Announce intent to send; carry filename, size, checksum |
| `0x11` | `FileAccept` | ← peer | Peer accepted the offer |
| `0x12` | `FileReject` | ← peer | Peer rejected the offer |
| `0x13` | `FileChunk` | → peer | One 1 MB chunk: index, offset, base64 data, checksum |
| `0x14` | `FileChunkAck` | ← peer | Acknowledge receipt of chunk; carry new byte offset |
| `0x15` | `FileComplete` | → peer | All chunks sent; carry final whole-file checksum |
| `0x16` | `FileResume` | ← peer | Resume from offset (response to `FileOffer` for a paused transfer) |
| `0x20` | `MsgSend` | → peer | Text message payload |
| `0x21` | `MsgAck` | ← peer | Message delivery confirmed |
| `0x30` | `PeerDiscover` | broadcast | UDP discovery beacon |
| `0x31` | `PeerAnnounce` | → peer | Response to discovery; carry ID and port |
| `0x32` | `PeerList` | ← peer | Reserved: share known peer list |
| `0x40` | `Ping` | → peer | Liveness probe |
| `0x41` | `Pong` | ← peer | Liveness response |
| `0xFF` | `Error` | both | Protocol error; carries error code and message string |

### Transfer Flow

```
Sender                              Receiver
  │                                    │
  │──── FileOffer ────────────────────►│  filename, size, sha256
  │                                    │  (check for existing partial)
  │◄─── FileAccept ────────────────────│  (or FileResume with offset)
  │                                    │
  │──── FileChunk[0] ─────────────────►│  base64 data, per-chunk sha256
  │◄─── FileChunkAck ──────────────────│  new byte offset
  │──── FileChunk[1] ─────────────────►│
  │◄─── FileChunkAck ──────────────────│
  │     ...                            │
  │──── FileComplete ─────────────────►│  whole-file sha256
  │                                    │  (receiver verifies, marks complete)
```

On disconnection mid-transfer, the sender saves the last acknowledged offset. On reconnect, it sends `FileOffer` again; if the receiver has a paused record for the same transfer ID it responds with `FileResume` carrying the resume offset, 