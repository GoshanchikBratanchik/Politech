import numpy as np

np.random.seed(42)

a_true = 4.33
loc_true = -3.2681
n = 300
N_sim = 100

def generate_loglaplace(a, loc, size):
    # Метод обратного преобразования
    # F(x) = 0.5*(x-loc)^a при loc < x <= loc+1
    # F(x) = 1 - 0.5*(x-loc)^(-a) при x > loc+1
    u = np.random.uniform(0, 1, size)
    x = np.where(
        u <= 0.5,
        loc + (2 * u) ** (1 / a),
        loc + (2 * (1 - u)) ** (-1 / a)
    )
    return x

def mom_estimate(sample):
    loc = np.median(sample) - 1
    xbar = np.mean(sample) - loc
    if xbar <= 0:
        return None, None
    a = (1 + np.sqrt(1 + 4 * xbar**2)) / (2 * xbar)
    return a, loc

def log_likelihood(sample, a, loc):
    shifted = sample - loc
    if np.any(shifted <= 0) or a <= 0:
        return -1e10
    A = shifted[shifted <= 1]
    B = shifted[shifted > 1]
    if len(A) == 0 or len(B) == 0:
        return -1e10
    return (len(sample) * np.log(a / 2)
            + (a - 1) * np.sum(np.log(A))
            - (a + 1) * np.sum(np.log(B)))

def mle_estimate(sample):
    # Метод Нелдера-Мида
    a_init, loc_init = mom_estimate(sample)
    if a_init is None:
        return None, None

    p1 = [a_init,       loc_init + 0.1]
    p2 = [a_init,       loc_init      ]
    p3 = [a_init + 0.1, loc_init      ]

    def f(p):
        return -log_likelihood(sample, p[0], p[1])

    for _ in range(100000):
        vals = [f(p1), f(p2), f(p3)]
        order = np.argsort(vals)
        best, mid, worst = [p1, p2, p3][order[0]], [p1, p2, p3][order[1]], [p1, p2, p3][order[2]]

        d1 = np.sqrt((p1[0]-p2[0])**2 + (p1[1]-p2[1])**2)
        d2 = np.sqrt((p2[0]-p3[0])**2 + (p2[1]-p3[1])**2)
        d3 = np.sqrt((p1[0]-p3[0])**2 + (p1[1]-p3[1])**2)
        if d1 < 0.00001 and d2 < 0.00001 and d3 < 0.00001:
            break

        mid_point = [(best[0] + mid[0]) / 2, (best[1] + mid[1]) / 2]
        new_point = [2 * mid_point[0] - worst[0], 2 * mid_point[1] - worst[1]]

        if f(new_point) < f(worst):
            if order[2] == 0: p1 = new_point
            elif order[2] == 1: p2 = new_point
            else: p3 = new_point
        else:
            p1 = [(best[0] + p1[0]) / 2, (best[1] + p1[1]) / 2]
            p2 = [(best[0] + p2[0]) / 2, (best[1] + p2[1]) / 2]
            p3 = [(best[0] + p3[0]) / 2, (best[1] + p3[1]) / 2]

    vals = [f(p1), f(p2), f(p3)]
    best = [p1, p2, p3][np.argmin(vals)]
    return best[0], best[1]

# Сбор оценок
mom_a_vals = []
mle_a_vals = []

for _ in range(10):
    sample = generate_loglaplace(a_true, loc_true, n)

    a_mom, _ = mom_estimate(sample)
    a_mle, _ = mle_estimate(sample)

    if a_mom is not None:
        mom_a_vals.append(a_mom)
    if a_mle is not None:
        mle_a_vals.append(a_mle)
    
print("sdfs")
mom_a = np.array(mom_a_vals)
mle_a = np.array(mle_a_vals)

# Несмещённость
print("=== Несмещённость ===")
print(f"МОМ:  E[a] = {mom_a.mean():.4f},  смещение = {mom_a.mean() - a_true:+.4f}")
print(f"ММП:  E[a] = {mle_a.mean():.4f},  смещение = {mle_a.mean() - a_true:+.4f}")

# Состоятельность — разброс при разных n
print("\n=== Состоятельность (разброс при разных n) ===")
for size in [50, 100, 200, 300, 500]:
    tmp_mom, tmp_mle = [], []
    for _ in range(10):
        s = generate_loglaplace(a_true, loc_true, size)
        am, _ = mom_estimate(s)
        al, _ = mle_estimate(s)
        if am is not None: tmp_mom.append(am)
        if al is not None: tmp_mle.append(al)
    print(f"n={size}: МОМ разброс={np.std(tmp_mom):.4f}  ММП разброс={np.std(tmp_mle):.4f}")

# Эффективность
cr_bound = a_true**2 / n
d_mom = np.var(mom_a)
d_mle = np.var(mle_a)

print("\n=== Эффективность ===")
print(f"Граница Крамера-Рао: {cr_bound:.4f}")
print(f"МОМ:  D(a) = {d_mom:.4f},  R-эфф = {cr_bound / d_mom:.4f}")
print(f"ММП:  D(a) = {d_mle:.4f},  R-эфф = {cr_bound / d_mle:.4f}")