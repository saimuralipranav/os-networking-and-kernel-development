# Mini Project 2

Mini-project 2 from CS3.301 Operating Systems and Networks, Monsoon 2026

## networking

### Build

```bash
cd networking
make all      # builds tempest/tempest and mastermind/mastermind
make clean
```

### tempest

```bash
tempest <city>
tempest <city> --raw
```

#### Design

The code is organized into modular components:
- `main.c`: CLI argument parsing, flags validation (`--raw`), input validation (verifies city name is provided, errors if $>1$ non-flag argument is given), and exit code management.
- `url.h` / `url.c`: RFC 3986 percent-encoding utility that converts spaces to `%20` and special characters to `%XX` hexadecimal escapes. Constructs the HTTP request target as `/<encoded_city>?0T`.
- `http.h` / `http.c`: Network communication and HTTP handling. Resolves host `wttr.is:80` via `getaddrinfo`, connects a raw TCP stream socket, applies a 10-second send/receive timeout via `setsockopt(SO_RCVTIMEO / SO_SNDTIMEO)`, sends an HTTP/1.1 `GET` request (`Host: wttr.is`, `User-Agent: curl/8.5.0`, `Connection: close`), and reads the complete response using a robust dynamic buffer handling short reads.
- Response body parsing: Parses HTTP status line. If the HTTP status is $\ge 400$ or location cannot be found by wttr.is, reports `tempest: invalid location`. In normal mode, strips the HTTP header block (`\r\n\r\n`) and outputs only the weather report body. In `--raw` mode, prints each line of the request prefixed by `> ` and each line of the response prefixed by `< `.

#### Changed defaults

none

#### Assumptions

- wttr.is is reachable via raw HTTP on port 80. Standard terminal displays UTF-8 weather icons and ANSI text.
- Running without arguments (`tempest`) or passing `--raw` alone (`tempest --raw`) prints `tempest: invalid location` and exits with code 1 (per Q4).
- Passing `--raw` before the city (`tempest --raw Hyderabad`) prints `tempest: too many arguments` and exits with code 1 (per Q6).
- The 10-second request deadline begins before `connect()` per Q15 (DNS resolution via `getaddrinfo` is performed immediately prior).
- Both `./tempest <city>` and `tempest <city>` (when adding directory to `$PATH`) are supported run styles (per Q21).

#### Known bugs

none known

---

### mastermind

```bash
mastermind [--cost-cutting] [--log]
```

#### Design

The application is architected into modular components:
- `protocol.h`: Defines the packet formats, protocol magic constants, and message verb structures.
- `main.c`: Coordinates CLI options (`--cost-cutting`, `--log`), terminal setup, game lobby loop, and invokes transports.
- `discovery.h` / `discovery.c`: Non-blocking UDP broadcast announcement on port `34567`. Automatically prunes inactive peers after 5 seconds of inactivity.
- `game.h` / `game.c`: Implements the Mastermind game state machine, sequence validation (strictly 5 digits `0-9`), and exact Bulls & Cows peg calculation (`x`, `o`, `-`) using digit frequency counters to properly handle duplicates.
- `ui.h` / `ui.c`: Renders the 12-row board view, masks the secret code for Codebreaker (`*****`), displays ANSI colored feedback pegs (`x` green, `o` yellow, `-` red), and prompts for input.
- `logger.h` / `logger.c`: Append-mode file logger writing microsecond timestamps `[YYYY-MM-DD HH:MM:SS.uuuuuu] [LOG] ...` to `log.txt`.
- `tcp_transport.h` / `tcp_transport.c`: Persistent TCP connection for normal gameplay.
- `udp_transport.h` / `udp_transport.c`: Reliable UDP transport implementing segmentation, chunking, pipelined transmission, and retransmissions.

#### Discovery

- Uses a dedicated 4-byte magic identifier: `0x4D4D4E44` ("MMND").
- Discovery port: `34567` (broadcast on LAN and loopback).
- Broadcast payload layout:
  ```c
  struct discovery_pkt {
    uint32_t magic;         // 0x4D4D4E44 ("MMND")
    uint64_t instance_id;   // Globally unique random instance ID per running process
    uint16_t port;          // TCP or UDP listening port (network byte order)
    uint8_t cost_cutting;   // 1 if cost-cutting enabled, 0 if TCP
    char name[32];          // Null-terminated player username
  };
  ```
- The sender's IP address is extracted directly from `recvfrom()`'s `struct sockaddr_in` source address, never from the packet payload.
- Unique instance IDs eliminate duplicate entries when announcements arrive across multiple interfaces (e.g., Wi-Fi broadcast + loopback) while allowing distinct players who share the same name on the LAN to be accurately discovered. Non-matching broadcast packets are discarded.

#### Message verbs

1. `MSG_CHALLENGE`: Initiator requests a game with peer. Payload: player name.
2. `MSG_ACCEPT`: Peer accepts challenge. Initiator becomes Mastermind (Codemaker), accepter becomes Codebreaker.
3. `MSG_REJECT`: Peer declines challenge, returning both to lobby.
4. `MSG_ATTEMPT`: Codebreaker sends 5-digit guess to Mastermind.
5. `MSG_FEEDBACK`: Mastermind sends 5 feedback pegs (`x`, `o`, `-`) to Codebreaker.
6. `MSG_END`: Mastermind reveals secret code to Codebreaker upon game conclusion.
7. `MSG_DISCONNECT`: Sent when a player quits or cleanly disconnects.
8. `MSG_HEARTBEAT`: Periodic keepalive frame between connected peers.
9. `MSG_START`: Mastermind notifies Codebreaker that the secret code is set, initiating round 1.

#### Reliable transfer over UDP (`--cost-cutting`)

- Packet Structures:
  - Data Chunk (`0x4D4D434B` / "MMCK"):
    ```c
    struct chunk_pkt {
      uint32_t magic;       // 0x4D4D434B
      uint16_t seq;         // Chunk sequence number (0-indexed)
      uint16_t total;       // Total number of chunks in this message
      uint16_t data_len;    // Length of payload in this chunk
      char data[CHUNK_SIZE];// Chunk payload
    };
    ```
  - Chunk ACK (`0x4D4D414B` / "MMAK"):
    ```c
    struct ack_pkt {
      uint32_t magic;       // 0x4D4D414B
      uint16_t seq;         // Acknowledged chunk sequence number
    };
    ```
- Pipelined Transfer: The sender segments the application message into chunks and sends all chunks immediately without waiting for individual ACKs between transmissions. The total chunk count is transmitted inside every chunk header (`total`).
- Per-Chunk ACKs & Retransmission: The receiver returns an ACK packet for each received chunk. The sender maintains per-chunk timestamps and a 0.1-second retransmission timer. Unacknowledged chunks are resent every 0.1 seconds.
- Non-blocking I/O: Socket timeouts and retransmissions are multiplexed using `poll()` with low timeouts so retransmission checks never block user input or terminal rendering.

#### Disconnection handling

- TCP: Detected immediately when `recv()` returns 0 (peer closed socket) or -1 with `ECONNRESET`.
- UDP: Detected when an unacknowledged chunk reaches maximum retransmission attempts (5 timeouts = 0.5s without response) or upon receiving an explicit `MSG_DISCONNECT` packet. Both players are cleanly notified and returned to the lobby.

#### Changed defaults

none

#### Assumptions

Local network allows UDP broadcast packets (`SO_BROADCAST`) on port 34567. Standard ANSI-compatible terminal.

#### Known bugs

none known

---

## xv6

### Build

```bash
cd xv6
make qemu     # boot xv6; Ctrl-A X to quit
make clean
```

### Run

```bash
# in the xv6 shell
cowtest
alarmtest
usertests -q

# or from outside
./test-xv6.py cowtest
./test-xv6.py alarmtest
./test-xv6.py -q usertests
```

### Copy-on-write fork

- **Reference Counts & Locking**: An array `ref_counts.count[(pa - KERNBASE) / PGSIZE]` tracks the reference count of every physical page in `[KERNBASE, PHYSTOP)` (128 MB = 32,768 pages). It is protected by spinlock `ref_counts.lock`. `kalloc()` sets the page ref count to 1, and `kfree()` decrements the count under `ref_counts.lock`, releasing the page to `kmem.freelist` only when the count reaches 0.
- **PTE COW Bit**: Defined in `riscv.h` as `PTE_COW = (1L << 8)`, using a software-reserved bit in the RISC-V Sv39 page table entry format.
- **CoW Fork in `uvmcopy()`**: For every mapped writable page, clears `PTE_W`, sets `PTE_COW`, maps into the child page table with identical flags, and increments the physical page's reference count via `kref_inc()`.
- **Write Fault Handling in `cowfault()`**:
  - In `usertrap()`, store page faults (`r_scause() == 15`) are routed to `cowfault(p->pagetable, r_stval())`.
  - If the page has `PTE_COW`:
    - If `kref_get(pa) > 1`: Allocates a new physical page with `kalloc()`, copies 4096 bytes via `memmove()`, decrements the old page's ref count with `kref_dec()`, and maps the new page with `PTE_W` and clearing `PTE_COW`.
    - If `kref_get(pa) == 1`: The process is now the sole owner; re-enables `PTE_W` and clears `PTE_COW` in place without allocating a new page.
  - Flushes the TLB using `sfence_vma()`.
- **`copyout()` Support**: Modified `copyout()` to check if the target destination page is marked with `PTE_COW`. If so, it invokes `cowfault()` to resolve page duplication before kernel writes proceed.

### Alarms

- **Fields added to `struct proc` (`proc.h`)**:
  - `int alarm_interval`: Alarm period in ticks ($0$ = disabled).
  - `int alarm_ticks`: Ticks elapsed since last alarm trigger.
  - `uint64 alarm_handler`: User virtual address of the alarm handler function.
  - `int alarm_running`: Boolean flag indicating if an alarm handler is currently executing (prevents reentrant invocations).
  - `struct trapframe *alarm_trapframe`: Dedicated backup trapframe allocated via `kalloc()` in `allocproc()` and freed in `freeproc()`.
- **Timer Interrupt Handling (`trap.c`)**:
  - In `usertrap()` on timer interrupt (`which_dev == 2`): If `p->alarm_interval > 0` and `!p->alarm_running`, increments `p->alarm_ticks`. When `p->alarm_ticks >= p->alarm_interval`, marks `p->alarm_running = 1`, resets `p->alarm_ticks = 0`, saves the entire user execution state (`*p->alarm_trapframe = *p->trapframe`), and redirects execution by setting `p->trapframe->epc = p->alarm_handler`.
- **State Restored by `sys_sigreturn()` (`sysproc.c`)**:
  - Restores all user registers and program counter by copying `*p->trapframe = *p->alarm_trapframe`.
  - Resets `p->alarm_running = 0`.
  - Returns `p->trapframe->a0` so the kernel syscall dispatcher writes back the user's restored `a0` register undisturbed.
- **Reentrancy Prevention**:
  - While `p->alarm_running` is 1, timer interrupts will not trigger another invocation of the alarm handler or increment ticks until the active handler completes and executes `sigreturn()`.

### Assumptions

Timer interrupts in xv6 occur via device interrupt type 2 (`which_dev == 2`) in user traps. Address `0` is a valid user pointer in xv6.

### Known bugs

none known
