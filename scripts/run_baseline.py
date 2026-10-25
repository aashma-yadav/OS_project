import os, sys, pexpect

repo = os.path.expanduser("~/osproject/xv6-riscv")
cpus = sys.argv[1] if len(sys.argv) > 1 else "1"
cmd  = sys.argv[2] if len(sys.argv) > 2 else "pipeline 4 100 54 3"
mode = sys.argv[3] if len(sys.argv) > 3 else "EDF without donation"
runs = 5

c = pexpect.spawn(f"make CPUS={cpus} qemu", cwd=repo, encoding="utf-8", timeout=600)
c.expect("init: starting sh")
c.expect(r"\$ ")

lines = [f"config: {cmd}, CPUS={cpus}, {mode}"]
for i in range(runs):
    c.sendline(cmd)
    c.expect(r"done at \+(\d+) ticks, deadline (\d+): (MET|MISSED)")
    t, dl, res = c.match.groups()
    c.expect(r"\$ ")
    lines.append(f"run{i+1}: +{t} ticks, deadline {dl}: {res}")
    print(lines[-1])

c.sendcontrol("a"); c.send("x")
c.close(force=True)

out = os.path.join(repo, f"results_baseline_cpus{cpus}.txt")
open(out, "w").write("\n".join(lines) + "\n")
print("wrote", out)
