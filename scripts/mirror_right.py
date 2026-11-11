import sys

M = {"1": "5", "5": "1", "2": "6", "6": "2", "3": "4", "4": "3"}
for line in sys.stdin:
    t = line.split()
    if len(t) == 4 and t[0] in "L+-":
        t[1] = M[t[1].lstrip("Rr")]
        line = " ".join(t) + "\n"
    sys.stdout.write(line)
