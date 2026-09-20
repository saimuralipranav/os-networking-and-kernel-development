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

<!-- how you split the code into files, how you read the response body, and how you detect an invalid location -->

#### Changed defaults

<!-- default: request timeout 10 s; write "none" if unchanged -->

#### Assumptions

<!-- anything you took for granted that the spec did not state -->

#### Known bugs

<!-- what does not work, and when; write "none known" if there are none -->

### mastermind

```bash
mastermind [--cost-cutting] [--log]
```

#### Design

<!-- how you split the code into files, and what each file is responsible for -->

#### Discovery

<!-- the 4-byte magic and the broadcast payload layout; the sender's ip comes from the packet source, not the payload -->

#### Message verbs

<!-- every verb, its payload, and when it is sent; must match your .h -->

#### Reliable transfer over UDP (`--cost-cutting`)

<!-- the chunk struct, sequence numbering, how the total chunk count is sent, and the ack/retransmit scheme -->

#### Disconnection handling

<!-- how you detect that the other player is gone -->

- TCP:
- UDP:

#### Changed defaults

<!-- defaults: broadcast every 2 s, entries expire after 5 s, retransmit after 0.1 s; write "none" if unchanged -->

#### Assumptions

<!-- anything you took for granted that the spec did not state -->

#### Known bugs

<!-- what does not work, and when; write "none known" if there are none -->

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

<!-- where the reference counts live and how they are locked, which pte bit marks a page cow, and how a write fault duplicates a page -->

### Alarms

<!-- the fields you added to struct proc, what state sigreturn restores, and how you stop a handler being re-entered -->

### Assumptions

<!-- anything you took for granted that the spec did not state -->

### Known bugs

<!-- what does not work, and which test shows it; write "none known" if there are none -->
