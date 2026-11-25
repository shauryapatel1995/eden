"""GAPBS -a output check against the native run: SSSP's reached-node count must match exactly;
PR's top-5 vertices must match in order with each score within 1e-4 relative (PR's tolerance;
the parallel Gauss-Seidel update order makes the last digits vary run to run).
A run with N trials prints N stat blocks; every block must match.
Usage: gap_check.py <run stdout> <native stdout>"""
import re, sys
def stats(p):
    t = open(p).read()
    return re.findall(r"^SSSP Tree reaches (\d+) nodes", t, re.M), re.findall(r"^(\d+):([0-9.e+-]+)$", t, re.M)
(rs, rp), (ns, np_) = stats(sys.argv[1]), stats(sys.argv[2])
# a run with N trials prints N blocks of stats; each must match the native run's (1 trial) block
k = max(1, len(np_))
ok = (rs or rp) and (rs == ns if rs or ns else len(rp) % k == 0 and len(rp) > 0)
ok = ok and all(a[0] == b[0] and abs(float(a[1]) - float(b[1])) <= 1e-4 * abs(float(b[1])) for a, b in zip(rp, np_ * (len(rp) // k)))
print("pass" if ok else "FAIL")
