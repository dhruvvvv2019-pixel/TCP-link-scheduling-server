# Link Scheduling over TCP

## Overview

This project is my implementation of **Part A: Link Scheduling** for the Advanced Computer Networks coursework.

The main objective of this project was to build a **multithreaded TCP-based file server and client** and study how different scheduling policies affect request waiting time, throughput, fairness, and transfer behavior.

The server supports four scheduling policies:

- **FCFS — First Come First Serve**
- **SJF — Shortest Job First**
- **RR — Round Robin**
- **DRR — Deficit Round Robin**

The implementation is written in **C++17** using POSIX TCP sockets and C++ threads. I also implemented Python-based analysis scripts and shell scripts to make the experiments easier to reproduce.

The project mainly consists of three parts:

1. A multithreaded TCP server which receives and schedules requests.
2. A client which can perform individual GET/PUT operations as well as generate workloads.
3. Scripts and workload files used to run experiments and analyze the collected metrics.

---

## 1. Project Objectives

The main objectives of this project were:

- Implement a working TCP file server and client.
- Support multiple simultaneous clients.
- Implement different request scheduling policies.
- Compare FCFS, SJF, RR and DRR.
- Implement byte-based Round Robin scheduling.
- Handle requests without splitting individual lines of a file.
- Measure request waiting time and response time.
- Measure throughput.
- Measure forfeited bytes in RR.
- Implement DRR to handle the long-line problem more fairly.
- Generate reproducible workloads.
- Collect experiment data into CSV files.
- Analyze the results using Python scripts.
- Study how the number of server worker threads affects scheduling behavior.

---

## 2. Technologies Used

| Component | Technology |
|---|---|
| Server | C++17 |
| Client | C++17 |
| Networking | POSIX TCP sockets |
| Concurrency | `std::thread`, mutexes, condition variables |
| Build | `g++`, Make |
| Configuration | JSON |
| Data Analysis | Python 3 |
| Experiment Automation | Bash |
| Operating System | Linux / macOS with POSIX socket support |

The required C++ compilation standard is:

```bash
g++ -std=c++17 -pthread
```

No external networking or concurrency framework is required.

---

## 3. Repository Structure

```text
.
├── src/
│   ├── server.cpp
│   ├── client.cpp
│   ├── common.hpp
│   └── config.hpp
│
├── scripts/
│   ├── analyze_metrics.py
│   ├── generate_plots.py
│   ├── run_all.sh
│   ├── run_one.sh
│   └── README.txt
│
├── workload/
│   ├── small.txt
│   ├── medium.txt
│   ├── large.txt
│   └── large_long.txt
│
├── Makefile
├── config.json
└── README.md
```

---

## 4. Overall Architecture

The project consists of a TCP client-server system.

```text
                    ┌─────────────────┐
                    │     Clients     │
                    └────────┬────────┘
                             │
                             │ TCP
                             ▼
                    ┌─────────────────┐
                    │ Listening Socket│
                    └────────┬────────┘
                             │
                             ▼
                  ┌──────────────────────┐
                  │ Connection / Request │
                  │      Handling        │
                  └──────────┬───────────┘
                             │
                             ▼
                    ┌─────────────────┐
                    │  Request Queue  │
                    └────────┬────────┘
                             │
             ┌───────────────┼────────────────┐
             │               │                │
             ▼               ▼                ▼
           FCFS             SJF          RR / DRR
             │               │                │
             └───────────────┼────────────────┘
                             │
                             ▼
                    ┌─────────────────┐
                    │ Worker Threads  │
                    └────────┬────────┘
                             │
                             ▼
                       File Transfer
```

The server accepts TCP connections and parses requests from clients. After a request is parsed, it is admitted to the scheduler queue. Worker threads then pick requests from this queue according to the selected scheduling policy.

---

## 5. Server Implementation

The main server implementation is:

```text
src/server.cpp
```

The server is a multithreaded TCP server. The number of worker threads is controlled through `config.json`.

The server maintains a request queue protected using synchronization primitives such as mutexes and condition variables. Workers sleep when there are no requests available and are notified when a new request arrives.

---

## 6. Client Implementation

The client implementation is:

```text
src/client.cpp
```

The client supports:

- Individual PUT requests
- Individual GET requests
- Workload generation

The main commands are:

```bash
./client put <file>
./client get <filename>
./client load <workload-directory> --requests <N>
```

The load generator uses multiple client threads as specified by the configuration.

---

## 7. Request Types

### GET

A GET request asks the server to send a file to the client.

```bash
./client get small.txt
```

### PUT

A PUT request sends a local file to the server.

```bash
./client put ./workload/small.txt
```

The client sends the file size as part of the request so that the server knows exactly how many bytes are expected.

### LOAD

The `load` operation is used for experiments.

```bash
./client load ./workload --requests 1000
```

The load generator uses multiple client threads and generates both GET and PUT requests.

---

## 8. Scheduling Policies

The project implements four scheduling policies:

```text
FCFS
SJF
RR
DRR
```

### 8.1 FCFS — First Come First Serve

FCFS serves requests according to their arrival/admission order.

A large request can therefore make smaller requests wait behind it.

### 8.2 SJF — Shortest Job First

SJF selects the request with the smallest declared byte count.

For example:

```text
Request A → 150 KB
Request B → 1 KB
Request C → 30 KB
Request D → 1 KB
```

SJF prefers:

```text
B → D → C → A
```

This can reduce the waiting time of smaller requests, while larger requests may wait longer.

### 8.3 RR — Round Robin

Round Robin uses a configurable byte quantum.

For example:

```text
Q = 8192 bytes
```

A request receives service for one scheduler round, up to the configured quantum. If it is not complete, it is placed back into the queue.

### 8.4 Line-Aware Round Robin

GET responses are **line-aware**. A line is not split across two scheduler rounds.

If the next complete line is larger than the remaining quantum, part of the available quantum cannot be used. I record this unused portion as **forfeited bytes**.

### 8.5 A14 / Long-Line Behavior

For example:

```text
Q = 8192 bytes
Line = 9002 bytes
```

If only 8192 bytes remain in the current round, the line does not fit. The next round can then send the complete line.

This behavior is tracked using the A14 counter.

### 8.6 DRR — Deficit Round Robin

DRR addresses the long-line problem in RR.

Instead of discarding unused allowance, DRR carries the unused allowance forward using a deficit value:

```text
Current quantum
      +
Previous unused allowance
      =
Available deficit
```

This allows a request to accumulate enough credit to eventually send a long line.

---

## 9. Multithreading

The server uses multiple worker threads.

The number of server workers is configured using:

```text
server_threads
```

inside `config.json`.

The client uses:

```text
client_threads
```

for workload generation.

The reference experiment uses:

```text
Server threads = 4
Client threads = 8
```

An additional comparison uses:

```text
Server threads = 1
```

for FCFS and RR.

---

## 10. Request Admission

The server separates request admission from scheduler execution.

A client which connects but does not send anything should not block the complete system.

Therefore, request headers are parsed before the request enters the scheduler queue.

A lightweight health request is handled directly and is not inserted into the scheduler queue or experiment metrics.

---

## 11. PUT Reliability

Incomplete PUT requests are handled using temporary files.

The process is:

```text
PUT request
     │
     ▼
Temporary file
     │
     │ complete upload
     ▼
rename()
     │
     ▼
Final file
```

If the client disconnects or the expected number of bytes is not received, the temporary file is removed.

This prevents an incomplete upload from corrupting an existing valid file.

---

## 12. Configuration

The project uses:

```text
config.json
```

The main server configuration contains:

```json
{
    "server": {
        "ip": "...",
        "port": "...",
        "server_threads": "...",
        "client_threads": "..."
    }
}
```

Important fields:

- `ip` — server bind address
- `port` — TCP listening port
- `server_threads` — number of worker threads
- `client_threads` — number of client workload threads

The scheduler policy is selected through `--sched`.

---

## 13. Command-Line Interface

The server supports:

```text
--sched
--quantum
--file
--p
--config
--metrics-out
```

Basic syntax:

```bash
./server --sched <scheduler> [--quantum Q] --file <path>
```

### FCFS

```bash
./server --sched fcfs --file ./server_files
```

### SJF

```bash
./server --sched sjf --file ./server_files
```

### RR

```bash
./server --sched rr --quantum 8192 --file ./server_files
```

### DRR

```bash
./server --sched drr --quantum 8192 --file ./server_files
```

RR and DRR require a positive quantum. FCFS and SJF reject `--quantum`.

---

## 14. Metrics Collection

The server records:

```text
request_id
op
filename
bytes
rounds
forfeited_bytes
arrival_ns
start_ns
finish_ns
```

CSV header:

```text
request_id,op,filename,bytes,rounds,forfeited_bytes,arrival_ns,start_ns,finish_ns
```

Timestamps are measured in nanoseconds.

### Waiting Time

```text
waiting time = start - arrival
```

I mainly analyze p50 and p99 waiting time.

### Response Time

```text
response time = finish - arrival
```

This includes waiting and service time.

### Throughput

```text
throughput = completed requests / elapsed experiment time
```

### Slowdown

Slowdown provides a size-normalized view of performance. I analyze it separately for the different workload sizes.

### Forfeited Bytes

RR records unused quantum caused by the line boundary constraint. The value is stored in `forfeited_bytes`.

---

## 15. Workload Files

The workload directory contains:

```text
workload/
├── small.txt
├── medium.txt
├── large.txt
└── large_long.txt
```

The main size classes are approximately:

```text
small.txt      → 1 KB
medium.txt     → 30 KB
large.txt      → 150 KB
large_long.txt → 150 KB with long lines
```

`large_long.txt` is particularly important for the RR vs DRR comparison.

---

## 16. Experiment Methodology

The main experiment configuration is:

```text
Server threads = 4
Client threads = 8
Requests       = at least 1000
```

The four main policies are tested:

```text
FCFS
SJF
RR
DRR
```

FCFS and RR are also tested with one server worker.

This gives six main runs:

```text
1. FCFS — 4 server threads
2. SJF  — 4 server threads
3. RR   — 4 server threads
4. DRR  — 4 server threads
5. FCFS — 1 server thread
6. RR   — 1 server thread
```

---

## 17. Experiment Scripts

The `scripts/` directory contains:

### `run_one.sh`

Runs an experiment for one scheduling policy.

### `run_all.sh`

Automates the collection of the scheduling experiments.

### `analyze_metrics.py`

Processes generated CSV metrics.

Example:

```bash
python3 scripts/analyze_metrics.py results/fcfs_4t.csv
```

### `generate_plots.py`

Generates plots for:

- throughput
- waiting time
- RR vs DRR forfeiture
- slowdown for different workload sizes

---

## 18. Results Organization

Experiment results can be stored as:

```text
results/
├── fcfs_4t.csv
├── sjf_4t.csv
├── rr_4t.csv
├── drr_4t.csv
├── fcfs_1t.csv
└── rr_1t.csv
```

`4t` means four server threads and `1t` means one server thread.

---

## 19. Graceful Shutdown

The server supports graceful shutdown using:

```text
Ctrl+C
```

When the server receives the shutdown signal, it wakes waiting worker threads, joins the worker threads and writes the collected metrics.

---

## 20. Error Handling and Edge Cases

The implementation considers:

- Invalid scheduler names
- Missing command-line arguments
- Missing quantum for RR/DRR
- Zero quantum
- Passing quantum to FCFS/SJF
- Invalid PUT sizes
- Incomplete PUT uploads
- Client disconnection during PUT
- Large file transfers
- Long lines larger than the quantum
- Empty request queues
- Multiple worker threads
- Graceful shutdown
- Silent/slow client connections
- Requests requiring multiple RR/DRR rounds

---

## 21. Design Choices

### C++ threads

I used `std::thread` for worker threads.

### Condition variables

Workers wait on a condition variable when there are no requests instead of busy-waiting.

### TCP sockets

The client and server communicate using POSIX TCP sockets.

### Monotonic clock

Request timestamps use a monotonic clock so wall-clock changes do not affect latency measurements.

### Temporary files for PUT

PUT requests are first written to temporary files and renamed only after successful completion.

### Runtime scheduler selection

The scheduler is selected using:

```text
--sched
```

### Separate workload and server storage

The original workload files are kept separately from the server's storage directory.

---

## 22. What I Learned

The main thing I found interesting in this project is that implementing a scheduler for a real TCP server is quite different from implementing the same algorithm on paper.

Several things interact at the same time:

```text
TCP networking
      +
multiple clients
      +
multiple worker threads
      +
file I/O
      +
request scheduling
      +
line boundaries
      +
quantum size
```

Normal Round Robin would be relatively straightforward if data could be split at arbitrary byte boundaries. Once complete lines have to be preserved, long lines can prevent the current quantum from being fully utilized.

This is where the difference between RR and DRR becomes more interesting.

I also found that throughput alone is not enough to understand scheduler behavior. Waiting-time distributions, especially p99 waiting time, can reveal behavior which is not visible from throughput alone.

---

## 23. Limitations

### Hardware dependence

Exact latency values depend on the machine, CPU, OS and current system load.

### File-system effects

File I/O and OS caching can affect response times.

### Closed-loop workload

The workload generator is closed-loop, so clients wait for requests to complete before generating additional requests.

### Finite workload

The experiments use a fixed number of requests rather than running indefinitely.

---

# 24. Complete Setup From Scratch

## Step 1 — Clone the Repository

```bash
git clone <YOUR-GITHUB-REPOSITORY-URL>
```

For example:

```bash
git clone https://github.com/<username>/tcp-link-scheduling-server.git
```

Enter the project:

```bash
cd tcp-link-scheduling-server
```

---

## Step 2 — Check the Files

```bash
ls
```

Expected:

```text
Makefile
config.json
src
scripts
workload
README.md
```

Check source:

```bash
ls src
```

Expected:

```text
client.cpp
common.hpp
config.hpp
server.cpp
```

Check workload:

```bash
ls workload
```

Expected:

```text
small.txt
medium.txt
large.txt
large_long.txt
```

---

## Step 3 — Check Required Software

```bash
g++ --version
make --version
python3 --version
```

The project requires:

- C++17-compatible `g++`
- `make`
- Python 3

---

## Step 4 — Build the Project

```bash
make
```

After a successful build, verify:

```bash
ls
```

You should see:

```text
server
client
```

---

## Step 5 — Create Server Storage

```bash
mkdir -p server_files
cp workload/*.txt server_files/
```

Verify:

```bash
ls server_files
```

Expected:

```text
small.txt
medium.txt
large.txt
large_long.txt
```

---

## Step 6 — Start the Server

Use **Terminal 1**:

```bash
./server     --sched fcfs     --file ./server_files     --config config.json     --metrics-out metrics.csv
```

Keep the server running.

---

## Step 7 — Test PUT and GET

Use **Terminal 2**:

```bash
./client put ./workload/small.txt --config config.json
```

Then:

```bash
./client get small.txt --config config.json
```

---

## Step 8 — Run a Workload

From Terminal 2:

```bash
./client load ./workload --requests 1000 --config config.json
```

Wait for the client to finish.

---

## Step 9 — Stop the Server

Go to Terminal 1 and press:

```text
Ctrl+C
```

The server writes:

```text
metrics.csv
```

---

## Step 10 — Analyze the Metrics

```bash
python3 scripts/analyze_metrics.py metrics.csv
```

To save the result:

```bash
mkdir -p results
cp metrics.csv results/fcfs_4t.csv
```

---

# 25. Running the Four Scheduling Policies

## FCFS

```bash
./server     --sched fcfs     --file ./server_files     --config config.json     --metrics-out metrics.csv
```

Terminal 2:

```bash
./client load ./workload --requests 1000 --config config.json
```

Press `Ctrl+C` in Terminal 1 after completion.

---

## SJF

```bash
./server     --sched sjf     --file ./server_files     --config config.json     --metrics-out metrics.csv
```

Terminal 2:

```bash
./client load ./workload --requests 1000 --config config.json
```

Stop the server:

```text
Ctrl+C
```

Save:

```bash
cp metrics.csv results/sjf_4t.csv
```

---

## RR

```bash
./server     --sched rr     --quantum 8192     --file ./server_files     --config config.json     --metrics-out metrics.csv
```

Terminal 2:

```bash
./client load ./workload --requests 1000 --config config.json
```

Stop:

```text
Ctrl+C
```

Save:

```bash
cp metrics.csv results/rr_4t.csv
```

---

## DRR

```bash
./server     --sched drr     --quantum 8192     --file ./server_files     --config config.json     --metrics-out metrics.csv
```

Terminal 2:

```bash
./client load ./workload --requests 1000 --config config.json
```

Stop:

```text
Ctrl+C
```

Save:

```bash
cp metrics.csv results/drr_4t.csv
```

---

# 26. Running the One-Worker Experiments

Change:

```text
server_threads
```

inside `config.json` to:

```text
1
```

## FCFS — 1 Worker

```bash
./server     --sched fcfs     --file ./server_files     --config config.json     --metrics-out metrics.csv
```

Terminal 2:

```bash
./client load ./workload --requests 1000 --config config.json
```

Stop:

```text
Ctrl+C
```

Save:

```bash
cp metrics.csv results/fcfs_1t.csv
```

## RR — 1 Worker

```bash
./server     --sched rr     --quantum 8192     --file ./server_files     --config config.json     --metrics-out metrics.csv
```

Terminal 2:

```bash
./client load ./workload --requests 1000 --config config.json
```

Stop:

```text
Ctrl+C
```

Save:

```bash
cp metrics.csv results/rr_1t.csv
```

---

# 27. Running the Analysis

Analyze individual experiments:

```bash
python3 scripts/analyze_metrics.py results/fcfs_4t.csv
python3 scripts/analyze_metrics.py results/sjf_4t.csv
python3 scripts/analyze_metrics.py results/rr_4t.csv
python3 scripts/analyze_metrics.py results/drr_4t.csv
python3 scripts/analyze_metrics.py results/fcfs_1t.csv
python3 scripts/analyze_metrics.py results/rr_1t.csv
```

---

# 28. Generating Plots

```bash
python3 scripts/generate_plots.py
```

The generated plots can be used to compare:

- Throughput
- Waiting time
- RR vs DRR forfeiture
- Slowdown for different workload sizes

---

# 29. Complete Experiment Checklist

```text
[ ] Build project using make
[ ] Create server_files/
[ ] Copy workload files into server_files/
[ ] Verify config.json
[ ] Run FCFS with 4 server threads
[ ] Run SJF with 4 server threads
[ ] Run RR with 4 server threads
[ ] Run DRR with 4 server threads
[ ] Change server_threads to 1
[ ] Run FCFS with 1 server thread
[ ] Run RR with 1 server thread
[ ] Save all six metrics CSV files
[ ] Analyze the CSV files
[ ] Generate plots
[ ] Compare p50 waiting time
[ ] Compare p99 waiting time
[ ] Compare throughput
[ ] Compare slowdown
[ ] Compare RR and DRR forfeited bytes
[ ] Check A14 behavior
```

---

# 30. Quick Start

```bash
git clone <YOUR-GITHUB-REPOSITORY-URL>

cd tcp-link-scheduling-server

make

mkdir -p server_files

cp workload/*.txt server_files/
```

Terminal 1:

```bash
./server     --sched fcfs     --file ./server_files     --config config.json     --metrics-out metrics.csv
```

Terminal 2:

```bash
./client load ./workload --requests 1000 --config config.json
```

After the workload completes, press `Ctrl+C` in Terminal 1.

Then:

```bash
python3 scripts/analyze_metrics.py metrics.csv
```

---

# Final Summary

This project implements a multithreaded TCP file server with four different scheduling policies:

```text
FCFS
SJF
RR
DRR
```

The implementation combines:

```text
TCP networking
+
multithreading
+
request scheduling
+
file transfer
+
line-aware service
+
performance measurement
+
workload generation
+
Python-based analysis
```

The main focus of the project is not just implementing the four scheduling algorithms, but understanding how they behave when they are actually used inside a TCP server with multiple clients and worker threads.

In particular, the RR and DRR comparison shows why details such as line boundaries and quantum size matter. A long line can prevent normal RR from fully using its available quantum, while DRR can carry the unused allowance forward.

The project also demonstrates why looking at only throughput is not enough. Waiting-time percentiles, tail latency, size-dependent slowdown and forfeited bytes provide additional information about how each scheduling policy behaves.

---

# Author

**Dhruv Mishra**  
M.Tech — Cybersecurity  
IIT Delhi
