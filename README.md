# Multi-Process Chat Server

A terminal-based chat server built in C from scratch — no frameworks, no shortcuts. Raw system calls, forked processes, encrypted sockets, and a proper terminal UI.

> Built as an OS portfolio project covering process management, IPC, networking, signals, and terminal control.

---

## Demo

```
╔═══════════════════════════════════════════════════╗
║        🖧  MULTI-PROCESS CHAT SERVER               ║
╚═══════════════════════════════════════════════════╝
  ONLINE CLIENTS: 3

  [id:1]  Leo                  ● online
  [id:2]  Saraya               ● online
  [id:3]  Sam                  ● online

───────────────────────────────────────────────────
  ACTIVE ROOMS
───────────────────────────────────────────────────
  #room1          (3 members) → Leo, Saraya, Sam

───────────────────────────────────────────────────
  ACTIVITY LOG
───────────────────────────────────────────────────
  [18:49:23] LOGIN: Sam (id 3)
  [18:49:32] Sam joined [room1]
  [18:52:37] MSG [room1] Leo: Sam, Saraya was asking abt you yesterday!
  [18:53:21] MSG [room1] Sam: Hello Saraya! How are you?? I miss you
───────────────────────────────────────────────────
```

---

## Features

- **TLS encryption** — all traffic encrypted via OpenSSL, nothing goes over plaintext
- **Auth system** — register and login with credentials stored in SQLite3
- **Chat rooms** — create and join rooms on the fly, be in multiple rooms at once
- **Private messaging** — send a message to one client only, invisible to everyone else
- **ncurses TUI** — colored chat window, live sidebar with rooms, clean input bar
- **Server dashboard** — live view of connected clients, active rooms, and activity log with timestamps
- **Multiprocess architecture** — each client gets its own forked process with `socketpair` IPC

---

## How it works

When a client connects, the server forks a child process dedicated to that client. The parent and child communicate through a `socketpair`. The parent's only job is routing — it reads messages from all children and decides where they go (broadcast to a room, or directly to one client). The child's job is talking to the actual client over TLS.

Auth happens before anything else. The client sends `LOGIN user pass` or `REGISTER user pass`, the server checks SQLite3, and only then does the chat session begin.

```
CLIENT                          SERVER (parent)
connect()                       accept()
SSL_connect()    ←────────────  SSL_accept()
                                    │
                                  fork()
                                    │
                              child process
LOGIN alice pass  ────────────►  do_auth()
                                    │
                              socketpair IPC
/join general    ─────────────►  route() → broadcast_room()
hello everyone   ─────────────►  route() → all members
/msg 2 hey       ─────────────►  route() → send_private()
```

---

## System Calls Used

| Category | Syscalls |
|---|---|
| Process | `fork`, `exit`, `waitpid`, `signal` |
| Networking | `socket`, `bind`, `listen`, `accept`, `connect`, `setsockopt`, `gethostbyname` |
| IPC | `socketpair`, `read`, `write` |
| Multiplexing | `select` |
| File Descriptor | `close` |
| Terminal (ncurses) | `ioctl`, `tcgetattr`, `tcsetattr` |
| TLS (OpenSSL) | `fcntl` |

**Total: 19+ distinct system calls**

---

## Project Structure

```
├── src/
│   ├── server/
│   │   ├── server.c          — main loop, routing, live dashboard
│   │   ├── client_handler.c  — per-client process, auth gate, message parsing
│   │   ├── auth.c            — SQLite3 register/login
│   │   └── tls.c             — OpenSSL context setup
│   └── client/
│       └── client.c          — ncurses TUI, TLS connection, command parsing
├── include/
│   ├── common.h              — shared structs, enums, constants
│   ├── auth.h
│   ├── tls.h
│   └── client_handler.h
├── certs/                    — generate your own (see setup)
├── Makefile
└── README.md
```

---

## Setup

**1. Install dependencies**
```bash
sudo apt install libssl-dev libsqlite3-dev libncurses-dev gcc make
```

**2. Generate self-signed certificates**
```bash
mkdir -p certs
openssl req -x509 -newkey rsa:4096 \
    -keyout certs/server.key \
    -out certs/server.crt \
    -days 365 -nodes \
    -subj "/CN=localhost"
```

**3. Build**
```bash
mkdir -p bin
make
```

---

## Run

```bash
# terminal 1 — start the server
./bin/server 8080

# terminal 2, 3, 4 — connect clients
./bin/client localhost 8080
```

---

## Usage

On connect you'll see the auth prompt:
```
AUTH: LOGIN <user> <pass> | REGISTER <user> <pass>
```

Register or login:
```
REGISTER alice pass123
LOGIN alice pass123
```

Then navigate with commands:
```
/join general          — join or create a room
/leave general         — leave a room
/msg 2 hey there       — private message to client id 2
Bye                    — disconnect cleanly
```

---

## Dependencies

| Library | Purpose |
|---|---|
| OpenSSL (`libssl`, `libcrypto`) | TLS encryption |
| SQLite3 (`libsqlite3`) | User credential storage |
| ncurses (`libncurses`) | Terminal UI |

---

## Roadmap

- [ ] Password hashing with `bcrypt`
- [ ] `/list` command — show online users by name
- [ ] Dead client cleanup from server's client list
- [ ] CA-signed certificates
- [ ] Username-based private messaging instead of client ID

---

## Author

Built by **Leo** as an Operating System project In 4th semester.
