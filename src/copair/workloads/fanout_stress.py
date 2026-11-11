import random
import sys


def generate(out, seed, ops=6000, nx=24, na=4, enum_every=40):
    rng = random.Random(seed)
    present = set()
    dom = {1: (na, na), 5: (na, na), 2: (na, nx), 3: (nx, na), 4: (nx, na), 6: (na, nx)}
    target = rng.choice([0.35, 0.5, 0.7])
    for i in range(ops):
        rel = rng.choice([1, 5, 2, 3, 4, 6, 2, 3, 4, 6])
        da, db = dom[rel]
        t = (rel, rng.randrange(da), rng.randrange(db))
        if i % 500 == 499:
            target = rng.choice([0.2, 0.5, 0.9])
        if t in present:
            if rng.random() > target:
                present.discard(t)
                out.write("- %d %d %d\n" % t)
        elif rng.random() < target:
            present.add(t)
            out.write("+ %d %d %d\n" % t)
        if i % enum_every == 0:
            out.write("E\n")
    out.write("E\n")


if __name__ == "__main__":
    generate(sys.stdout, int(sys.argv[1]), *(int(v) for v in sys.argv[2:]))
