# Linux_deku: Linux remote swap (Fastswap / Hermit) baseline

Notes as of 2026-10-23. Goal: run our workloads (mcf, cactuBSSN, Redis, omnetpp)
end to end over RDMA through **stock Linux swapping** with remote memory, as a
baseline next to eden (no prefetch, Leap, XGBoost, PySTreeD). Status: kernel and
module built and committed, **not yet installed or booted**.

## Why this baseline

- FarSight (arXiv 2506.00384) compares only against FastSwap and Hermit, which
  both use the **Linux default swap read-ahead** (Hermit = FastSwap + async /
  proactive swap-out). See memory note `leap-readahead-and-farsight-notes`.
- Hermit (NSDI'23, github.com/uclasystem/hermit) = its own Linux 5.14-rc5 kernel
  + `rswap-client.ko` (frontswap module, from Fastswap) + user-space
  `rswap-server`. Its prefetching is stock `swapin_nr_pages()` read-ahead; its
  async prefetch thread is off by default.
- Running Hermit's own kernel isn't practical: eden needs our 5.16 kernel with the
  userfaultfd PC patch, and Hermit's kernel would need a reboot per switch,
  cgroup v1, and OFED 5.4 (we have DOCA-OFED 26.04).

## Findings that shaped the design

1. **Stock frontswap reads are synchronous, including read-ahead.**
   `swap_readpage()` (mm/page_io.c) marks the page up to date right after
   `frontswap_load()` returns, so the module must finish the read first, and
   `swap_cluster_readahead()` reads each read-ahead page that way before the
   faulting page. A window of 8 = 7 back-to-back RDMA reads before the page the
   app needs. Fastswap's kernel patch adds `load_async` + `poll_load` to fix it.
2. **kswapd doesn't help under a cgroup limit.** kswapd balances node-wide
   watermarks; hitting `memory.max` reclaims in the allocating task
   (`try_charge_memcg` -> `try_to_free_mem_cgroup_pages`), and `memory.high`
   reclaims on return to user space, in the task's context. There is no per-cgroup
   kswapd in mainline. Fastswap offloads `memory.high` reclaim to a worker pinned
   to one CPU.
3. **Hermit's module completes I/O itself.** Even its "sync" `load` only posts
   the RDMA read; the completion handler sets PageUptodate + unlocks. `store`
   posts and waits, and the completion unlocks. So the kernel must not
   unlock/mark up to date after frontswap calls, and must `poll_load(cpu)` on the
   same CPU to wait for a needed page. It therefore does **not** work with stock
   frontswap (would read garbage / double-unlock). Load-async queues are
   interrupt driven (IB_POLL_SOFTIRQ); sync loads and stores are polled
   (IB_POLL_DIRECT).
4. The module's `invalidate_page` / `invalidate_area` are no-ops, so stock
   frontswap's invalidation calls were left in (Fastswap/Hermit comment them out).

## Kernel: `/home/shaurya/linux`, branch `fastswap`

- Branched from `deku_validation` @ `d29428a1d1f7` (the running `5.16.0+`;
  verified committed and pushed to origin, built 2026-07-30 16:20).
- Commit **`0ee814d95526`** "Port Fastswap's kernel support to 5.16 for
  Hermit's rswap-client" (local only, not pushed). Port of
  github.com/clusterfarmem/fastswap `kernel/kernel.patch` (Linux 4.11):
  - `include/linux/adc_macros.h` (new): `RSWAP_KERNEL_SUPPORT 1`.
  - `frontswap.h/.c`: `->load_async`, `->poll_load`, `__frontswap_load_async()`,
    `__frontswap_poll_load()`; export `frontswap_ops`, `frontswap_enabled_key`.
  - `page_io.c`: no unlock after a successful store; sync reads poll this CPU's
    load queue (get_cpu/put_cpu), async reads use `load_async`.
  - `swap_state.c`: cluster and VMA read-ahead read the faulting page first and
    wait, then issue the rest asynchronously.
  - `memcontrol.c`: `memory.high` reclaim on a worker pinned to
    `fastswap_reclaim_cpu=` (boot param, default 7), <= 2048 pages per pass;
    `memory.high` writes shrink asynchronously.
  - `swap.h`: `SWAP_CLUSTER_MAX` 32 -> 64 (compile-time, always on).
  - `vmscan.c`: no `cond_resched()` in `shrink_page_list()`.
  - Everything except `SWAP_CLUSTER_MAX` is gated on `frontswap_enabled()`, so
    without the module loaded it behaves like `deku_validation`.
- Built with the running kernel's `.config` (identical to
  `/boot/config-5.16.0+`): `make -j64 LOCALVERSION=-fastswap` -> release
  **`5.16.0-fastswap`**, no errors (log: `build_fastswap.log`). Exports confirmed
  in `Module.symvers`. **Never booted.**
- Note: the in-tree build artifacts (`vmlinux`, `bzImage`) are now the fastswap
  build; rebuilding `deku_validation` later just needs a normal `make`.

## Module + server: `/home/shaurya/hermit-remoteswap` (own git repo)

- `cc441e6` import of Hermit `remoteswap/` (uclasystem/hermit@10dfa5c91).
- `2f69cc5` drop Hermit-only `linux/swap_stats.h` (unused at level 1) and
  `linux/lightnvm.h` (removed from Linux in 5.15).
- `bf62cd1` include `linux/hermit.h` only at level >= 3; add OFED's
  `include/uapi` to the include path (its `rdma_user_cm.h` needs its own
  `ib_user_sa.h`); add `EDEN_SETUP.md` with build/run notes.
- Builds against `KDIR=/home/shaurya/linux` with
  `OFA_DIR=/usr/src/ofa_kernel/x86_64/<kernel>` (the `default` OFED headers are
  for 5.15 and break the build): `BACKEND=DRAM` and `BACKEND=RDMA` both 0
  errors; vermagic `5.16.0-fastswap`; depends `ib_core,rdma_cm` (OFED).
- `rswap-server` builds unchanged (`server/`, `-lrdmacm -libverbs`).
- Constants: 8 GB remote regions, up to 128 GB. Server must be told the host's
  online cores (**64** on smarties06) or the client crashes.

## Machine facts (smarties06)

- Ubuntu 22.04.5, gcc 11.4, 64 cores, ~1 TB RAM, root disk ~28 GB free.
- NIC: BlueField-2 / ConnectX-6 Dx, RoCE (`mlx5_0`, `mlx5_1`).
- RDMA stack is DOCA-OFED 26.04 via DKMS (`/lib/modules/5.16.0+/updates/dkms/`);
  `/etc/kernel/postinst.d/dkms` will rebuild it for a newly installed kernel.
- cgroup v2 (`cgroup2fs`); use `memory.high` (what the offloaded reclaim
  acts on) instead of Hermit's cgroup-v1 instructions.
- Memory server for eden is smarties10 (`192.168.168.2`, rcntrl/memserver);
  `rswap-server` can run there on another port if RAM allows both pools.

## Next steps (need root / reboot - user)

1. `cd /home/shaurya/linux && sudo make modules_install install LOCALVERSION=-fastswap INSTALL_MOD_STRIP=1`
2. Keep `5.16.0+` as GRUB default; boot once:
   `sudo grub-reboot "Advanced options for Ubuntu>Ubuntu, with Linux 5.16.0-fastswap" && sudo reboot`
3. Check `uname -r` = `5.16.0-fastswap`, `dkms status` shows mlnx-ofed-kernel for it.

Then (Claude):

4. Confirm eden still works on the new kernel (quick `benchmarks/ll` run; PC patch unchanged).
5. Rebuild `rswap-client.ko` with `OFA_DIR=/usr/src/ofa_kernel/x86_64/5.16.0-fastswap`.
6. Test with `BACKEND=DRAM` first (user insmods + swapon), a small program under a
   `memory.high` cgroup, check outputs; then RDMA against smarties10.
7. Run the four workloads with the same local-memory sizes as the eden runs.

## Risks / open questions

- New code in core mm (swap-in, memcg reclaim): a bug could crash or hang the
  machine once the module is loaded and swapping. Hence DRAM test first and the
  old kernel kept as default. smarties06 is shared (e.g. user `tom`).
- This is a **Fastswap-style** baseline, not Hermit: Hermit's level 2-3 kernel
  features are absent. Report it as such.
- Choose `fastswap_reclaim_cpu` off the benchmark's cores.
- Alternative if this stalls: a Linux-default read-ahead baseline inside eden
  (no reboot), noted in `leap-readahead-and-farsight-notes`.
