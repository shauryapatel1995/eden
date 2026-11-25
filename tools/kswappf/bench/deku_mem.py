#!/usr/bin/env python3
"""Minimum memory for <=5% and <=10% performance loss vs native, readahead vs Deku trees.

For each workload: a fresh native run (native.sh: native metric + anon peak), then for each config
(default = kernel read-ahead, and the workload's best tree config) a bisection over memory.max in
STEP% steps of the native anon peak, one run per probe (mpc.sh, round tag "d").
A probe that is killed by the timeout or fails validation counts as a miss.
Usage: deku_mem.py [wl ...]   (default: redis_stack wiredtiger webservice gap_pr). Summary: logs/deku_mem_summary.csv
"""
import csv, math, os, subprocess, sys, time

B = '/data1/shaurya/linux66_bench'
TREES = dict(x.split(':') for x in 'redis_stack:st_ext4 redis_p99:st_ext4 wiredtiger:n32_c0 webservice:n4_c0 gap_pr:n32_c0'.split())
STEP = 2
THRESH = (1.05, 1.10)
SUMMARY = f'{B}/logs/deku_mem_summary.csv'


def log(*a):
    print(time.strftime('[%H:%M:%S]'), *a, flush=True)


def wl_vars(wl):
    out = subprocess.run(['bash', '-c', f'. {B}/wl/{wl}.sh; echo "$WL_DIR $WL_METRIC $WL_BETTER"'],
                         capture_output=True, text=True, check=True).stdout.split()
    return out[0] + '/linux66b', out[1], out[2]


def native(wl, L):
    t0 = time.time()
    r = subprocess.run([f'{B}/native.sh', wl], capture_output=True, text=True)
    whole = time.time() - t0
    log(wl, 'native:', (r.stdout.strip().splitlines() or ['?'])[-1], f'(whole {whole:.0f} s)')
    m = float(open(f'{L}/native/metric').read())
    v = open(f'{L}/native/valid').read().strip()
    peak = int(open(f'{L}/native/anon_peak').read())
    tot = int(open(f'{L}/native/memory_peak').read())
    return m, v, whole, peak, tot


def probe(wl, cfg, tree, pct, timeout, nat, better, cache):
    if pct in cache:
        return cache[pct]
    env = dict(os.environ, TREES=tree, TIMEOUT=str(int(timeout)))
    r = subprocess.run([f'{B}/mpc.sh', wl, cfg, str(pct), 'd'], capture_output=True, text=True, env=env)
    row = [l for l in r.stdout.splitlines() if l.startswith(wl + ',')]
    sd, valid, metric = math.inf, 'error', 'NA'
    if row:
        f = next(csv.reader([row[-1]]))
        metric, valid = f[7], f[20]
        if valid == 'pass':
            m = float(metric)
            sd = m / nat if better == 'lower' else nat / m
    else:
        log(wl, cfg, pct, 'NO ROW:', r.stdout[-300:], r.stderr[-300:])
    log(f'{wl} {cfg} {pct}%: metric={metric} valid={valid} slowdown={sd:.3f}')
    cache[pct] = sd
    return sd


def search(wl, cfg, tree, timeout, nat, better, hi_pct):
    """returns {thresh: min passing pct or None}, probes"""
    cache = {}
    res = {}
    for th in sorted(THRESH, reverse=True):          # 10% first, then 5% inside the narrowed range
        passing = [p for p, s in cache.items() if s <= th]
        hi = min(passing) if passing else None
        if hi is None:
            hi = 100
            if probe(wl, cfg, tree, hi, timeout, nat, better, cache) > th:
                hi = hi_pct
                if hi <= 100 or probe(wl, cfg, tree, hi, timeout, nat, better, cache) > th:
                    res[th] = None
                    continue
        lo = max([p for p, s in cache.items() if s > th and p < hi] or [0])
        while hi - lo > STEP:
            mid = int(round((lo + hi) / 2 / STEP)) * STEP
            if mid <= lo or mid >= hi:
                break
            if probe(wl, cfg, tree, mid, timeout, nat, better, cache) <= th:
                hi = mid
            else:
                lo = mid
        res[th] = hi
    return res, cache


def main():
    wls = sys.argv[1:] or list(TREES)
    new = not os.path.exists(SUMMARY)
    with open(SUMMARY, 'a') as fs:
        w = csv.writer(fs)
        if new:
            w.writerow(['workload', 'config', 'native_metric', 'metric_better', 'anon_peak_mb',
                        'min_pct_5', 'min_mb_5', 'min_pct_10', 'min_mb_10', 'probes', 'timeout_s'])
        for wl in wls:
            L, metric_name, better = wl_vars(wl)
            nat, nv, whole, peak, tot = native(wl, L)
            if nv != 'pass':
                log(wl, 'native run not valid, skipping'); continue
            # kill a probe once it is clearly beyond 10%: wall-time metrics at 1.3x the native
            # whole run, other metrics (trial time, throughput) at 3x
            timeout = (1.3 * whole + 15) if metric_name == 'wall_secs' else (3 * whole + 30)
            hi_pct = min(200, int(math.ceil(tot / peak * 100 / STEP)) * STEP)   # file-inclusive peak
            for cfg in ('default', TREES[wl]):
                res, cache = search(wl, cfg, TREES[wl], timeout, nat, better, hi_pct)
                mb = lambda p: '' if p is None else round(peak * p / 100 / 2**20)
                pr = ' '.join(f'{p}:{"x" if s == math.inf else round(s, 3)}' for p, s in sorted(cache.items()))
                w.writerow([wl, cfg, nat, better, round(peak / 2**20),
                            res[1.05] if res[1.05] is not None else f'>{hi_pct}', mb(res[1.05]),
                            res[1.10] if res[1.10] is not None else f'>{hi_pct}', mb(res[1.10]), pr, int(timeout)])
                fs.flush()
                log(f'== {wl} {cfg}: 5% -> {res[1.05]}%, 10% -> {res[1.10]}%  ({pr})')
    log('deku_mem done')


if __name__ == '__main__':
    main()
