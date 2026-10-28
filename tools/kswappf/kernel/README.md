Kernel side of the eden_swappf swap-in prefetcher (Linux 6.6.160).

    0001  mm/swap: pluggable swap-in prefetcher hook
          include/linux/swap_prefetch.h, mm/swap_state.c:
          register_/unregister_swap_prefetcher(ops); ops->predict(vmf, req) is
          called from swapin_readahead() and fills two 64-page masks (fwd +1..+64,
          back -1..-64). Returning false falls back to the kernel's read-ahead.
    0002  swapin:swapin_fault tracepoint in do_swap_page() (address, user PC,
          tgid, in_swap_cache) used to collect training traces.

Apply to the 6.6.160 stable tarball:

    tar xf linux-6.6.160.tar.xz && cd linux-6.6.160
    git init && git add -A && git commit -qm "Linux 6.6.160"
    git am /path/to/eden/tools/kswappf/kernel/*.patch
    cp /path/to/eden/tools/kswappf/kernel/config-6.6.160-swappf .config
    make olddefconfig && make -j$(nproc)
    sudo make modules_install INSTALL_MOD_STRIP=1 && sudo make install

config-6.6.160-swappf is the .config of the running 6.6.160-swappf+ kernel
(MGLRU off; brd/zram/null_blk as modules). The module (../eden_swappf.c) is
built against this tree with ../Makefile.
