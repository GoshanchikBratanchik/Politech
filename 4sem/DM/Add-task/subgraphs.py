"""
Поиск всех неизоморфных подграфов заданного ориентированного графа
(перечисление подграфов с точностью до изоморфизма).

Формат входного файла:
    1-я строка      — порядок графа n;
    следующие n строк — матрица смежности n x n (числа через пробел).
Для невзвешенного графа элементы матрицы — 0 или 1,
для взвешенного — вес дуги (0 означает отсутствие дуги).
Петли (ненулевая диагональ) и кратные дуги не допускаются.

Требуются библиотеки: networkx, matplotlib
    pip install networkx matplotlib
"""

import math
import os
import re
import subprocess
import sys
from itertools import combinations, chain

try:
    import networkx as nx
    import matplotlib
    import matplotlib.pyplot as plt
except ImportError:
    print(
        "Не найдены библиотеки. Установите их командой:\n"
        "    pip install networkx matplotlib"
    )
    sys.exit(1)


def setup_backend():
    """
    Выбирает оконный бэкенд matplotlib. Если ни один не доступен
    (например, не установлен tkinter), возвращает False — тогда
    картинки сохраняются в PNG и открываются системным просмотрщиком.
    """
    if matplotlib.get_backend().lower() not in ("agg", "pdf", "svg", "ps", "cairo"):
        return True
    for backend in ("TkAgg", "QtAgg", "Qt5Agg", "GTK4Agg", "GTK3Agg", "WXAgg"):
        try:
            plt.switch_backend(backend)
            return True
        except Exception:
            continue
    plt.switch_backend("Agg")
    return False


INTERACTIVE = setup_backend()
IMAGE_DIR = "images"
_image_counter = 0


def show_figure(fig, name):
    """Показывает окно с рисунком или, если окна недоступны, сохраняет PNG."""
    global _image_counter
    if INTERACTIVE:
        plt.show()
        return
    os.makedirs(IMAGE_DIR, exist_ok=True)
    _image_counter += 1
    safe = re.sub(r"[^\w\-]+", "_", name).strip("_")[:60]
    path = os.path.join(IMAGE_DIR, f"{_image_counter:03d}_{safe}.png")
    fig.savefig(path, dpi=110)
    plt.close(fig)
    print(f"  Рисунок сохранён: {os.path.abspath(path)}")
    opener = {"linux": "xdg-open", "darwin": "open"}.get(sys.platform)
    try:
        if sys.platform.startswith("win"):
            os.startfile(os.path.abspath(path))
        elif opener:
            subprocess.Popen(
                [opener, path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
            )
    except Exception:
        pass


PER_PAGE_CLASSES = 12  # сколько подграфов-классов на одном окне
PER_PAGE_OCCURRENCES = 9  # сколько вхождений на одном окне
NODE_SIZE = 500


class GraphError(Exception):
    """Ошибка в содержимом файла с графом."""


# ----------------------------------------------------------------------
#  а) Загрузка и проверка графа
# ----------------------------------------------------------------------


def read_text(filename):
    for enc in ("utf-8-sig", "cp1251"):
        try:
            with open(filename, encoding=enc) as f:
                return f.read()
        except UnicodeDecodeError:
            continue
    raise GraphError("Не удалось прочитать файл: неизвестная кодировка.")


def load_graph(filename, weighted):
    """Читает файл и возвращает матрицу смежности. При ошибке — GraphError."""
    try:
        text = read_text(filename)
    except FileNotFoundError:
        raise GraphError(f'Файл "{filename}" не найден.')
    except OSError as e:
        raise GraphError(f'Не удалось открыть файл "{filename}": {e}')

    lines = [ln.strip() for ln in text.splitlines() if ln.strip()]
    if not lines:
        raise GraphError("Файл пуст.")

    try:
        n = int(lines[0])
    except ValueError:
        raise GraphError(
            f"1-я строка должна содержать целое число — порядок графа, "
            f'а содержит "{lines[0]}".'
        )
    if n < 1:
        raise GraphError(f"Порядок графа должен быть не меньше 1 (указано {n}).")

    rows = lines[1:]
    if len(rows) != n:
        raise GraphError(
            f"Порядок графа {n}, значит в матрице должно быть {n} строк, "
            f"а найдено {len(rows)}."
        )

    matrix = []
    for i, row in enumerate(rows, start=1):
        tokens = re.split(r"[\s;,]+", row)
        if len(tokens) != n:
            raise GraphError(
                f"Строка {i} матрицы: должно быть {n} чисел, "
                f"найдено {len(tokens)} (номера вершин выходят "
                f"за пределы диапазона 1..{n})."
            )
        values = []
        for j, tok in enumerate(tokens, start=1):
            try:
                x = float(tok)
            except ValueError:
                raise GraphError(f'Элемент [{i},{j}] = "{tok}" не является числом.')
            if not math.isfinite(x):
                raise GraphError(f'Элемент [{i},{j}] = "{tok}" недопустим.')
            if not weighted and x not in (0, 1):
                if x == int(x) and x > 1:
                    raise GraphError(
                        f"Элемент [{i},{j}] = {tok}: кратные дуги не допускаются."
                    )
                raise GraphError(
                    f"Элемент [{i},{j}] = {tok}: в невзвешенном графе "
                    f"допускаются только 0 и 1."
                )
            if i == j and x != 0:
                raise GraphError(
                    f"Элемент [{i},{i}] = {tok}: петля в вершине {i}, "
                    f"петли не допускаются."
                )
            values.append(int(x) if x == int(x) else x)
        matrix.append(values)
    return matrix


# ----------------------------------------------------------------------
#  Поиск неизоморфных подграфов
# ----------------------------------------------------------------------


def invariant(k, edges):
    """Быстрый инвариант: графы с разными инвариантами точно не изоморфны."""
    out_d = [0] * k
    in_d = [0] * k
    es = set(edges)
    for u, v in edges:
        out_d[u] += 1
        in_d[v] += 1
    mutual = sum(1 for u, v in edges if (v, u) in es) // 2
    return len(edges), mutual, tuple(sorted(zip(out_d, in_d)))


def count_candidates(matrix, k, induced):
    n = len(matrix)
    total = 0
    for combo in combinations(range(n), k):
        m = sum(1 for u in combo for v in combo if u != v and matrix[u][v])
        total += 1 if induced else 2**m
    return total


def find_classes(matrix, k, induced):
    """
    Возвращает список классов изоморфизма подграфов порядка k.
    Класс: {'graph': DiGraph на 0..k-1, 'occ': [(вершины, дуги), ...], 'key': ...}
    Вершины и дуги в вхождениях — в нумерации исходного графа (с 1).
    induced=True  — порождённые подграфы (берутся все дуги между выбранными вершинами);
    induced=False — все подграфы (любое подмножество дуг между выбранными вершинами).
    """
    n = len(matrix)
    classes = []
    buckets = {}
    for combo in combinations(range(n), k):
        local = {v: i for i, v in enumerate(combo)}
        avail = [(u, v) for u in combo for v in combo if u != v and matrix[u][v]]
        if induced:
            edge_sets = [avail]
        else:
            edge_sets = chain.from_iterable(
                combinations(avail, r) for r in range(len(avail) + 1)
            )
        for es in edge_sets:
            loc_edges = [(local[u], local[v]) for u, v in es]
            key = invariant(k, loc_edges)
            h = nx.DiGraph()
            h.add_nodes_from(range(k))
            h.add_edges_from(loc_edges)
            found = None
            for c in buckets.get(key, []):
                if nx.is_isomorphic(c["graph"], h):
                    found = c
                    break
            if found is None:
                found = {"graph": h, "occ": [], "key": key}
                buckets.setdefault(key, []).append(found)
                classes.append(found)
            found["occ"].append(
                (tuple(v + 1 for v in combo), [(u + 1, v + 1) for u, v in es])
            )
    # упорядочим: сначала подграфы с меньшим числом дуг
    classes.sort(key=lambda c: c["key"])
    return classes


# ----------------------------------------------------------------------
#  Рисование
# ----------------------------------------------------------------------


def draw_graph(
    ax, nodes, edges, pos, weights=None, hl_nodes=None, hl_edges=None, title=None
):
    """
    Рисует орграф на осях ax.
    weights  — словарь {(u, v): вес} для подписей (или None);
    hl_nodes, hl_edges — выделяемые вершины/дуги (остальное рисуется бледно).
    """
    g = nx.DiGraph()
    g.add_nodes_from(nodes)
    g.add_edges_from(edges)
    edge_set = set(edges)
    highlight = hl_nodes is not None

    if highlight:
        node_colors = ["#e74c3c" if v in hl_nodes else "#e0e0e0" for v in nodes]
        font_colors = "black"
    else:
        node_colors = ["#5dade2"] * len(nodes)
        font_colors = "black"
    nx.draw_networkx_nodes(
        g,
        pos,
        nodelist=nodes,
        node_color=node_colors,
        node_size=NODE_SIZE,
        edgecolors="black",
        ax=ax,
    )
    nx.draw_networkx_labels(
        g, pos, font_size=10, font_weight="bold", font_color=font_colors, ax=ax
    )

    for u, v in edges:
        rad = 0.18 if (v, u) in edge_set else 0.0  # встречные дуги — дугами
        if highlight:
            on = (u, v) in hl_edges
            color, width = ("#c0392b", 2.5) if on else ("#cccccc", 1.0)
        else:
            color, width = ("#333333", 1.5)
        nx.draw_networkx_edges(
            g,
            pos,
            edgelist=[(u, v)],
            edge_color=color,
            width=width,
            arrows=True,
            arrowstyle="-|>",
            arrowsize=16,
            node_size=NODE_SIZE,
            connectionstyle=f"arc3,rad={rad}",
            ax=ax,
        )
        if weights is not None:
            (x1, y1), (x2, y2) = pos[u], pos[v]
            dx, dy = x2 - x1, y2 - y1
            # точка на 40% пути от начала дуги (кривая Безье для arc3),
            # чтобы подписи пересекающихся дуг не накладывались
            t = 0.4
            cx, cy = (x1 + x2) / 2 + rad * dy, (y1 + y2) / 2 - rad * dx
            lx = (1 - t) ** 2 * x1 + 2 * t * (1 - t) * cx + t**2 * x2
            ly = (1 - t) ** 2 * y1 + 2 * t * (1 - t) * cy + t**2 * y2
            ax.text(
                lx,
                ly,
                str(weights[(u, v)]),
                fontsize=9,
                color="#1a5276",
                ha="center",
                va="center",
                bbox=dict(boxstyle="round,pad=0.15", fc="white", ec="none", alpha=0.85),
            )

    if title:
        ax.set_title(title, fontsize=11)
    # Явно задаём границы осей: при k = 1 или 2 все вершины лежат на одной
    # горизонтали, и автоматический масштаб сплющивает рисунок в линию.
    xs = [pos[v][0] for v in nodes]
    ys = [pos[v][1] for v in nodes]
    cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
    half = max(max(xs) - min(xs), max(ys) - min(ys), 1.0) / 2 + 0.35
    ax.set_xlim(cx - half, cx + half)
    ax.set_ylim(cy - half, cy + half)
    ax.set_aspect("equal")
    ax.axis("off")


def graph_layout(n):
    nodes = list(range(1, n + 1))
    if n == 1:
        return {1: (0.0, 0.0)}
    return nx.circular_layout(nodes)


def paged_show(items, per_page, draw_item, window_title):
    """Выводит элементы по страницам (одно окно — одна страница)."""
    pages = math.ceil(len(items) / per_page)
    for p in range(pages):
        chunk = items[p * per_page : (p + 1) * per_page]
        cols = min(4, len(chunk)) if per_page > 9 else min(3, len(chunk))
        rows = math.ceil(len(chunk) / cols)
        fig, axes = plt.subplots(
            rows, cols, figsize=(max(3.6 * cols, 7), 3.6 * rows), squeeze=False
        )
        for ax in axes.flat:
            ax.axis("off")
        for ax, item in zip(axes.flat, chunk):
            draw_item(ax, item)
        suffix = f"  (страница {p + 1} из {pages})" if pages > 1 else ""
        fig.suptitle(window_title + suffix, fontsize=13)
        fig.tight_layout()
        if pages > 1 and INTERACTIVE:
            print(
                f"  Показана страница {p + 1} из {pages}. "
                f"Закройте окно, чтобы перейти дальше."
            )
        show_figure(fig, f"{window_title}_{p + 1}")


# ----------------------------------------------------------------------
#  б) г) Функции меню
# ----------------------------------------------------------------------


class App:
    def __init__(self):
        self.matrix = None
        self.weighted = False
        self.filename = None
        self.classes = {}  # k -> список классов
        self.induced = {}  # k -> тип подграфов, для которого считали

    # --- вспомогательное ---
    @property
    def n(self):
        return len(self.matrix)

    def edges(self):
        return [
            (i + 1, j + 1)
            for i in range(self.n)
            for j in range(self.n)
            if self.matrix[i][j]
        ]

    def weights(self):
        if not self.weighted:
            return None
        return {
            (i + 1, j + 1): self.matrix[i][j]
            for i in range(self.n)
            for j in range(self.n)
            if self.matrix[i][j]
        }

    def need_graph(self):
        if self.matrix is None:
            print("Сначала загрузите граф (пункт 1).")
            return False
        return True

    # --- а) ---
    def load(self):
        filename = input("Введите имя файла с графом: ").strip().strip('"')
        ans = input("Граф взвешенный? (д/н) [н]: ").strip().lower()
        weighted = ans in ("д", "да", "y", "yes")
        try:
            matrix = load_graph(filename, weighted)
        except GraphError as e:
            print(f"ОШИБКА: {e}")
            print("Граф не загружен.")
            return
        self.matrix, self.weighted, self.filename = matrix, weighted, filename
        self.classes.clear()
        self.induced.clear()
        print(
            f"Граф загружен: {self.n} вершин, {len(self.edges())} дуг"
            f"{', взвешенный' if weighted else ''}."
        )

    # --- б) ---
    def show_graph(self):
        if not self.need_graph():
            return
        fig, ax = plt.subplots(figsize=(6, 6))
        draw_graph(
            ax,
            list(range(1, self.n + 1)),
            self.edges(),
            graph_layout(self.n),
            self.weights(),
            title=f"Исходный граф ({os.path.basename(self.filename)})",
        )
        fig.tight_layout()
        show_figure(fig, "Исходный граф")

    # --- в) ---
    def find_subgraphs(self):
        if not self.need_graph():
            return
        k = ask_int(f"Введите порядок подграфов k (1..{self.n}): ", 1, self.n)
        if k is None:
            return
        print(
            "Какие подграфы искать?\n"
            "  1 — порождённые (все дуги исходного графа между выбранными вершинами)\n"
            "  2 — все подграфы (любое подмножество этих дуг)"
        )
        mode = ask_int("Ваш выбор [1]: ", 1, 2, default=1)
        if mode is None:
            return
        induced = mode == 1

        total = count_candidates(self.matrix, k, induced)
        if total > 200_000:
            ans = (
                input(
                    f"Нужно проверить {total} подграфов, это может занять "
                    f"много времени. Продолжить? (д/н): "
                )
                .strip()
                .lower()
            )
            if ans not in ("д", "да", "y", "yes"):
                return
        print("Идёт поиск...")
        classes = find_classes(self.matrix, k, induced)
        self.classes[k] = classes
        self.induced[k] = induced

        kind = "порождённых" if induced else "всех"
        print(f"\nНайдено неизоморфных {kind} подграфов порядка {k}: {len(classes)}")
        for j, c in enumerate(classes, start=1):
            arcs = ", ".join(f"{u + 1}→{v + 1}" for u, v in sorted(c["graph"].edges()))
            print(
                f"  ({k},{j}): дуг {c['key'][0]}, вхождений {len(c['occ'])}; "
                f"дуги: {arcs if arcs else 'нет'}"
            )

        items = list(enumerate(classes, start=1))

        def draw_item(ax, item):
            j, c = item
            g = c["graph"]
            nodes = list(range(1, k + 1))
            edges = [(u + 1, v + 1) for u, v in g.edges()]
            draw_graph(
                ax,
                nodes,
                edges,
                graph_layout(k),
                title=f"({k},{j})   вхождений: {len(c['occ'])}",
            )

        title_kind = "порождённые" if induced else "все"
        paged_show(
            items,
            PER_PAGE_CLASSES,
            draw_item,
            f"Неизоморфные подграфы порядка {k} ({title_kind})",
        )

    # --- г) ---
    def show_occurrences(self):
        if not self.need_graph():
            return
        if not self.classes:
            print("Сначала найдите подграфы (пункт 3).")
            return
        s = input("Введите номер подграфа в виде k,j (например 3,2): ")
        nums = re.findall(r"\d+", s)
        if len(nums) != 2:
            print("Номер нужно ввести двумя числами, например 3,2.")
            return
        k, j = int(nums[0]), int(nums[1])
        if k not in self.classes:
            print(
                f"Подграфы порядка {k} ещё не искались. "
                f"Доступные порядки: {sorted(self.classes)}."
            )
            return
        if not 1 <= j <= len(self.classes[k]):
            print(
                f"Подграфа ({k},{j}) нет: для порядка {k} найдено "
                f"{len(self.classes[k])} классов."
            )
            return

        occ = self.classes[k][j - 1]["occ"]
        print(f"Подграф ({k},{j}) входит в исходный граф {len(occ)} раз(а):")
        for i, (vs, es) in enumerate(occ, start=1):
            arcs = ", ".join(f"{u}→{v}" for u, v in es) or "нет дуг"
            print(f"  {i}) вершины {{{', '.join(map(str, vs))}}}; дуги: {arcs}")

        if len(occ) > 45:
            ans = input(f"Вхождений много ({len(occ)}), показать все рисунки? (д/н): ")
            if ans.strip().lower() not in ("д", "да", "y", "yes"):
                return

        pos = graph_layout(self.n)
        nodes = list(range(1, self.n + 1))
        all_edges = self.edges()
        weights = self.weights()
        items = list(enumerate(occ, start=1))

        def draw_item(ax, item):
            i, (vs, es) = item
            draw_graph(
                ax,
                nodes,
                all_edges,
                pos,
                weights,
                hl_nodes=set(vs),
                hl_edges=set(es),
                title=f"Вхождение {i}: {{{', '.join(map(str, vs))}}}",
            )

        paged_show(
            items,
            PER_PAGE_OCCURRENCES,
            draw_item,
            f"Вхождения подграфа ({k},{j}) в исходный граф",
        )


def ask_int(prompt, lo, hi, default=None):
    s = input(prompt).strip()
    if s == "" and default is not None:
        return default
    try:
        x = int(s)
    except ValueError:
        print("Нужно ввести целое число.")
        return None
    if not lo <= x <= hi:
        print(f"Число должно быть в диапазоне от {lo} до {hi}.")
        return None
    return x


def main():
    if not INTERACTIVE:
        print(
            "Внимание: оконный режим matplotlib недоступен (не установлен tkinter\n"
            "или Qt). Рисунки будут сохраняться в папку images/ и открываться\n"
            "системным просмотрщиком. Чтобы получить окна, установите, например:\n"
            "    sudo apt install python3-tk      (Ubuntu/Debian)\n"
            "    sudo pacman -S tk                (Arch)\n"
            "    sudo dnf install python3-tkinter (Fedora)"
        )
    app = App()
    actions = {
        "1": app.load,
        "2": app.show_graph,
        "3": app.find_subgraphs,
        "4": app.show_occurrences,
    }
    while True:
        print("\n========== Неизоморфные подграфы орграфа ==========")
        if app.matrix is not None:
            print(f"Текущий граф: {app.filename} ({app.n} вершин)")
        print(
            "1 — загрузить граф из файла\n"
            "2 — показать исходный граф\n"
            "3 — найти неизоморфные подграфы порядка k\n"
            "4 — показать вхождения подграфа (k,j)\n"
            "0 — выход"
        )
        choice = input("Выберите пункт: ").strip()
        if choice == "0":
            break
        action = actions.get(choice)
        if action is None:
            print("Нет такого пункта меню.")
            continue
        action()


if __name__ == "__main__":
    main()
