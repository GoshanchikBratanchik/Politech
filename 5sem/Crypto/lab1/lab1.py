import sys
import math
import timeit

PAIRS = {
    "1": (3857119886342675369, 6966345877103822939),
    "2": (
        409299785680620857521697722741778371157,
        1783651784122767224648788401388776081841,
    ),
    "3": (
        41959147502895284447724879593990666827676949333978488488345015006907964641669287,
        31289316386889381024252200231126511927338714430170474577074515137323290690536121,
    ),
}


def ext_euclid(a, b, log=None):
    r_prev, r_cur = a, b  # r_{i-1}, r_i
    x_prev, x_cur = 1, 0  # x_{i-1}, x_i
    y_prev, y_cur = 0, 1  # y_{i-1}, y_i
    i = 1
    if log is not None:
        log.append([0, r_prev, x_prev, y_prev, None])
    while True:
        q, r_next = divmod(r_prev, r_cur)
        if log is not None:
            log.append([i, r_cur, x_cur, y_cur, q])
        if r_next == 0:
            return r_cur, x_cur, y_cur
        x_next = x_prev - q * x_cur
        y_next = y_prev - q * y_cur
        r_prev, r_cur = r_cur, r_next
        x_prev, x_cur = x_cur, x_next
        y_prev, y_cur = y_cur, y_next
        i += 1


def ext_binary(a, b, log=None):
    # шаг 1
    g = 1
    # шаг 2
    while a % 2 == 0 and b % 2 == 0:
        a //= 2
        b //= 2
        g *= 2
    # шаг 3
    u, v, A, B, C, D = a, b, 1, 0, 0, 1
    if log is not None:
        log.append([0, u, v, A, B, C, D])
    k = 0
    # шаг 4
    while u != 0:
        # 4.1
        while u % 2 == 0:
            u //= 2  # 4.1.1
            if A % 2 == 0 and B % 2 == 0:  # 4.1.2
                A, B = A // 2, B // 2
            else:
                A, B = (A + b) // 2, (B - a) // 2
        # 4.2
        while v % 2 == 0:
            v //= 2  # 4.2.1
            if C % 2 == 0 and D % 2 == 0:  # 4.2.2
                C, D = C // 2, D // 2
            else:
                C, D = (C + b) // 2, (D - a) // 2
        # 4.3
        if u >= v:
            u, A, B = u - v, A - C, B - D
        else:
            v, C, D = v - u, C - A, D - B
        k += 1
        if log is not None:
            log.append([k, u, v, A, B, C, D])
    # шаг 5
    return g * v, C, D, g


def ext_truncated(a, b, log=None):
    # шаг 1
    r_prev, r_cur = a, b
    x_prev, x_cur = 1, 0
    y_prev, y_cur = 0, 1
    i = 1
    trunc_cur = False  # был ли r_i получен «усечением»
    if log is not None:
        log.append([0, r_prev, x_prev, y_prev, None, False])
    while True:
        # шаг 2: деление с неотрицательным остатком 0 <= r_next < |r_i|
        m = r_cur if r_cur > 0 else -r_cur  # |r_i|
        q, r_next = divmod(r_prev, m)  # r_{i-1} = q |r_i| + r_next
        trunc_next = 2 * r_next > m
        if trunc_next:  # «усечение» остатка
            r_next -= m
            q += 1
        if r_cur < 0:  # переход к r_{i-1} = q_i r_i + r_{i+1}
            q = -q
        if log is not None:
            log.append([i, r_cur, x_cur, y_cur, q, trunc_cur])
        # шаг 3
        if r_next == 0:
            s = 1 if r_cur > 0 else -1
            return s * r_cur, s * x_cur, s * y_cur  # шаг 4
        x_next = x_prev - q * x_cur
        y_next = y_prev - q * y_cur
        r_prev, r_cur = r_cur, r_next
        x_prev, x_cur = x_cur, x_next
        y_prev, y_cur = y_cur, y_next
        trunc_cur = trunc_next
        i += 1


class Tee:
    def __init__(self, path):
        self.f = open(path, "w", encoding="utf-8")

    def __call__(self, *args):
        s = " ".join(str(a) for a in args)
        print(s)
        self.f.write(s + "\n")


def fmt(v):
    return "" if v is None else str(v)


def print_rows(out, header, rows):
    """Пронумерованные итерации; если строк > 20 — первые 5 и последние 5."""
    out(header)
    n = len(rows)
    idx = list(range(n)) if n <= 20 else list(range(5)) + [None] + list(range(n - 5, n))
    for i in idx:
        if i is None:
            out("   ...")
            continue
        r = rows[i]
        out(("%4d | " % r[0]) + " | ".join(fmt(v) for v in r[1:]))
    out("")


def run_pair(out, name, a, b):
    out("=" * 100)
    out("Пара %s" % name)
    out("a =", a)
    out("b =", b)
    # По условию алгоритмов пособия 0 < b <= a; если a < b — меняем местами,
    # а коэффициенты возвращаем в порядке исходных a, b.
    swap = a < b
    A, B = (b, a) if swap else (a, b)
    if swap:
        out(
            "(a < b, поэтому алгоритмы запускаются для пары (b, a); "
            "в ответе коэффициенты приведены к исходному порядку a, b)"
        )
    out("")

    results = {}

    # --- Алгоритм 1.3
    log = []
    d, x, y = ext_euclid(A, B, log)
    if swap:
        x, y = y, x
    results["Евклид"] = (d, x, y)
    out("--- Алгоритм 1.3. Расширенный алгоритм Евклида ---")
    out("Число итераций (делений с остатком): %d" % (len(log) - 1))
    print_rows(out, "   i | r_i | x_i | y_i | q_i", log)
    out("НОД =", d)
    out("x =", x)
    out("y =", y)
    out("")

    # --- Алгоритм 1.4
    log = []
    d, x, y, g = ext_binary(A, B, log)
    if swap:
        x, y = y, x
    results["Бинарный"] = (d, x, y)
    out("--- Алгоритм 1.4. Расширенный бинарный алгоритм Евклида ---")
    out("g =", g)
    out("Число итераций шага 4: %d" % (len(log) - 1))
    print_rows(out, "   k | u | v | A | B | C | D", log)
    out("НОД =", d)
    out("x =", x)
    out("y =", y)
    out("")

    # --- «Усечённые» остатки
    log = []
    d, x, y = ext_truncated(A, B, log)
    if swap:
        x, y = y, x
    results["Усечённые остатки"] = (d, x, y)
    out("--- Расширенный алгоритм Евклида с «усечёнными» остатками ---")
    out("Число итераций (делений с остатком): %d" % (len(log) - 1))
    print_rows(
        out,
        "   i | r_i | x_i | y_i | q_i | * — r_i усечён",
        [r[:5] + ["*" if r[5] else ""] for r in log],
    )
    out("НОД =", d)
    out("x =", x)
    out("y =", y)
    out("")

    # --- Проверка корректности
    out("--- Проверка ---")
    d_true = math.gcd(a, b)
    out("math.gcd(a, b) =", d_true)
    for nm, (d, x, y) in results.items():
        ok = (d == d_true) and (a * x + b * y == d)
        out(
            "%-18s a*x + b*y = %s  -> %s"
            % (nm, a * x + b * y, "верно" if ok else "ОШИБКА")
        )
    out("")

    out("--- Быстродействие (среднее время одного запуска, без записи итераций) ---")
    for nm, f in [
        ("Евклид", ext_euclid),
        ("Бинарный", ext_binary),
        ("Усечённые остатки", ext_truncated),
    ]:
        t = timeit.Timer(lambda: f(A, B))
        number, _ = t.autorange()
        best = min(t.repeat(repeat=5, number=number)) / number
        out("%-18s %10.3f мс" % (nm, best * 1e6))
    out("")
    return results


def main():
    out = Tee("lab1_output.txt")
    if len(sys.argv) == 3:
        run_pair(out, "(пользовательская)", int(sys.argv[1]), int(sys.argv[2]))
    else:
        for name, (a, b) in PAIRS.items():
            run_pair(out, name, a, b)
    out.f.close()


if __name__ == "__main__":
    main()
