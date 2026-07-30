# Network Setup: smarties06 <-> smarties10

Ping between smarties06 (this machine) and smarties10 over the direct link on
interface `enp202s0f0np0` (192.168.168.1/24 on this machine).

```
ping 192.168.168.2 -I enp202s0f0np0
```

## RDMA bandwidth tests

RDMA device on this machine's link is `mlx5_0` (mapped to `enp202s0f0np0`).

Start the server on smarties10 first, then run the client command here on smarties06.

### RDMA Write

Server (smarties10):
```
ib_write_bw -d mlx5_0
```

Client (smarties06):
```
ib_write_bw -d mlx5_0 192.168.168.2
```

Result: BW peak 10785.78 MiB/sec, BW average 10783.90 MiB/sec (65536 bytes, 5000 iterations)

### RDMA Read

Server (smarties10):
```
ib_read_bw -d mlx5_0
```

Client (smarties06):
```
ib_read_bw -d mlx5_0 192.168.168.2
```

Result: BW peak 9730.07 MiB/sec, BW average 9729.76 MiB/sec (65536 bytes, 1000 iterations)

## Network gotcha: duplicate IP on smarties10

Both of smarties10's RDMA ports (`enp202s0f0np0` and `enp202s0f1np1`) had the
same IP `192.168.168.2/24` assigned. Plain ping and perftest (`-I`/`-d` pin
the interface/device explicitly) worked fine despite this, but it broke
`rdma_cm`-based connections (used by Eden's `rcntrl`/`memserver`/runtime):
outbound connections from smarties06 failed instantly and locally (0 packets
ever left the NIC, confirmed via `tcpdump`), while smarties10-initiated
connections nondeterministically went out the wrong port.

Fix (on smarties10, only `enp202s0f0np0` should keep the IP):
```
sudo ip addr del 192.168.168.2/24 dev enp202s0f1np1
```

## Eden minimal remote-memory setup

Goal: smarties10 as the memory/swap server, smarties06 as the client using
its local memory + far memory backed by smarties10 over RDMA.

### 1. One-time machine setup (smarties06)

`iokerneld` (Shenango's dispatcher) needs hugepages and a few sysctls, done
once per boot:
```
sudo bash scripts/setup_machine.sh
```

### 2. Build Eden with remote memory enabled

The checked-in build lacked the `REMOTE_MEMORY` compile flag, which gates
all of Eden's userfaultfd-based fault handling (`rmem/uffd.c`) — without it,
remote memory is a stub that always fails. Rebuild from repo root:
```
make clean && make REMOTE_MEMORY=1 -j$(nproc)
```
This also rebuilds `rcntrl`/`memserver`/`iokerneld` (harmlessly, they don't
depend on the flag).

### 3. Minimal test app

Added `tests/test_rmem_touch.c` — allocates memory via Eden's direct
`rmalloc()` API (`inc/rmem/api.h`), writes it (forcing local faults +
eviction to remote once past the local memory threshold), reads it back
(forcing remote fetches), and verifies correctness. Picked up automatically
by the top-level Makefile (`tests/*.c` glob) as `tests/test_rmem_touch`.

### 4. Start the memory server + rack controller (smarties10)

```
sudo ./rcntrl -s 192.168.168.2
sudo ./memserver -s 192.168.168.2 -c 192.168.168.2 -n 512
```
(`-n 512` slabs x 128KB = 64MB exported; size to comfortably exceed whatever
the client will allocate.)

### 5. Client config (smarties06)

`client_rmem.config`:
```
host_addr 192.168.168.1
host_netmask 255.255.255.0
host_gateway 192.168.168.1
runtime_kthreads 4
runtime_spinning_kthreads 4
runtime_guaranteed_kthreads 4
remote_memory 1
rmem_backend rdma
rmem_local_memory 16777216
```
Note: `runtime_guaranteed_kthreads` must equal `runtime_kthreads` — remote
memory doesn't support burstable/on-demand cores.

### 6. Start iokerneld (smarties06)

```
sudo ./iokerneld
```

### 7. Run the test

```
sudo RDMA_RACK_CNTRL_IP=192.168.168.2 RDMA_RACK_CNTRL_PORT=9202 ./tests/test_rmem_touch client_rmem.config
```

Result: connected to `rcntrl`/`memserver` over RDMA, allocated a 32MB buffer
(exceeding the 16MB local threshold), evicted the excess to smarties10 on
write, fetched it back correctly on read:
```
PASSED: all pages read back correctly
init: shutting down -> SUCCESS
```
