# syncsweep.py <host> <rise list a,b,c | start:stop:step> [noise samples] [outfile]: for each sync phase, #SYNC 1 <rise>
# then #NOISE; prints mean/sd of the six sense channels per phase, plus a sync-off reference first and last.
import socket, sys, time, re
host = sys.argv[1]
spec = sys.argv[2]
if ":" in spec:
    a, b, c = (int(x) for x in spec.split(":")); phases = list(range(a, b, c))
else:
    phases = [int(x) for x in spec.split(",")]
n = int(sys.argv[3]) if len(sys.argv) > 3 else 2000
out = sys.argv[4] if len(sys.argv) > 4 else None
s = socket.create_connection((host, 23), timeout=5); s.settimeout(0.1)
buf = []
def drain(t):
    end = time.time() + t
    while time.time() < end:
        try:
            d = s.recv(65536)
            if not d: return
            buf.append(d.decode("utf-8", "replace"))
        except socket.timeout:
            pass
def send(c, t):
    buf.append("\n>> %s\n" % c); s.sendall((c + "\n").encode()); drain(t)
NO = re.compile(r"NOISE M(\d) .*?a mean ([-+\d.]+) sd ([\d.]+) .*?\| b mean ([-+\d.]+) sd ([\d.]+) .*?\| c mean ([-+\d.]+) sd ([\d.]+)")
ER = re.compile(r"SYNC: (\w+).*?phase error (-?\d+) ticks \(spread (\d+)\)")
def measure(cmd):
    mark = len("".join(buf))
    if cmd: send(cmd, 1.0)
    send("$Spindle/G474=SEND #NOISE %d" % n, 0.8 + n / 20000.0 + 0.7)
    seg = "".join(buf)[mark:]
    m = ER.search(seg)
    st = {}
    for mm in NO.finditer(seg):
        k = int(mm.group(1)) - 1
        v = [float(x) for x in mm.groups()[1:]]
        st[3 * k + 0], st[3 * k + 1], st[3 * k + 2] = (v[0], v[1]), (v[2], v[3]), (v[4], v[5])
    return (m.group(1), int(m.group(2)), int(m.group(3))) if m else None, st
drain(0.5)
rows = []
names = ["M1a", "M1b", "M1c", "M2a", "M2b", "M2c"]
def show(label, e, st):
    if len(st) < 6:
        print("%-8s %s  (NOISE lines missing)" % (label, e)); return
    sds = [st[c][1] for c in range(6)]; mus = [st[c][0] for c in range(6)]
    print("%-8s err %-5s " % (label, e[1] if e else "-") + " ".join("%+7.1f/%5.1f" % (mus[c], sds[c]) for c in range(6)) +
          "   max sd %5.1f  rms sd %5.1f  max|mean| %5.1f" % (max(sds), (sum(x * x for x in sds) / 6) ** 0.5, max(abs(x) for x in mus)))
print("%-8s %-9s " % ("rise", "") + " ".join("%13s" % ("%s mean/sd" % c) for c in names))
send("$Spindle/G474=SEND #SYNC 0", 1.0)
e, st = measure(None); show("off", e, st)
for r in phases:
    e, st = measure("$Spindle/G474=SEND #SYNC 1 %d" % r); show(str(r), e, st)
send("$Spindle/G474=SEND #SYNC 0", 1.0)
e, st = measure(None); show("off", e, st)
s.shutdown(socket.SHUT_RDWR); s.close()
if out: open(out, "w").write("".join(buf))
