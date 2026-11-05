#!/usr/bin/env python3
"""Convert a Linux swapin_fault trace into eden's fault-trace format.

The 6.6 swappf kernel records every fault on a swapped-out page with the
swapin:swapin_fault tracepoint (include/trace/events/swapin.h):

    redis-server-1234  [005] .....  123.456789: swapin_fault: tgid=1234 addr=7f.. ip=55.. cached=0

Collect it with, e.g.:

    echo 'tgid == <pid>' > /sys/kernel/tracing/events/swapin/swapin_fault/filter
    echo 1 > /sys/kernel/tracing/events/swapin/swapin_fault/enable
    cat /sys/kernel/tracing/trace_pipe > swapin.trace

This writes one line per fault in the format eden's handler (rmem/handler.c)
prints, so process_linux_trace.py and the PySTreeD training work unchanged:

    "N PF addr, faulting addr, and ip", <page> <addr> <ip> <prev_ip> <prev_delta> <delta>

prev_ip is the previous fault's PC, delta the page delta (bytes) from the
previous fault to this one, and prev_delta the delta between the two faults
before it - all over this process's swap-in faults, in trace (time) order.
There are no pointer candidates ("Loc:" lines): the kernel doesn't read
page contents, so only spatial training data comes out of these traces.

Usage: swapin_trace_to_eden.py <trace> <out> [--tgid N] [--drop-cached]
  --drop-cached: skip faults that hit the swap cache (no device read)
"""
import argparse
import re

EVENT = re.compile(r"\s(\d+\.\d+): swapin_fault: tgid=(\d+) addr=([0-9a-f]+) ip=([0-9a-f]+) cached=(\d)")
PAGE_MASK = ~0xfff


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("trace")
    ap.add_argument("out")
    ap.add_argument("--tgid", type=int, default=0, help="only this process (default: the most common tgid)")
    ap.add_argument("--drop-cached", action="store_true", help="skip swap-cache hits")
    args = ap.parse_args()

    events = []
    with open(args.trace, errors="replace") as f:
        for line in f:
            m = EVENT.search(line)
            if m:
                events.append((float(m.group(1)), int(m.group(2)), int(m.group(3), 16),
                               int(m.group(4), 16), m.group(5) == "1"))
    if not events:
        raise SystemExit("no swapin_fault events found")
    tgid = args.tgid
    if not tgid:
        counts = {}
        for e in events:
            counts[e[1]] = counts.get(e[1], 0) + 1
        tgid = max(counts, key=counts.get)
    events = [e for e in events if e[1] == tgid]
    # trace_pipe merges the per-CPU buffers in time order already; sort anyway
    # (stable) in case the input was concatenated from per-CPU files
    events.sort(key=lambda e: e[0])

    n = cached = 0
    have_last = False
    last_page = last_ip = last_delta = 0
    with open(args.out, "w") as out:
        for _ts, _tgid, addr, ip, in_cache in events:
            if in_cache:
                cached += 1
                if args.drop_cached:
                    continue
            page = addr & PAGE_MASK
            prev_ip, prev_delta = last_ip, last_delta
            delta = page - last_page if have_last else 0
            n += 1
            out.write(f'"{n} PF addr, faulting addr, and ip", {page:x} {addr:x} {ip:x} '
                      f'{prev_ip:x} {prev_delta} {delta}\n')
            last_page, last_ip, last_delta, have_last = page, ip, delta, True
    print(f"tgid {tgid}: wrote {n} faults ({cached} were swap-cache hits"
          f"{', dropped' if args.drop_cached else ''})")


if __name__ == "__main__":
    main()
