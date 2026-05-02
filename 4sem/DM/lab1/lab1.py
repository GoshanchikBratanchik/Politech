def solve():
    with open('input_7.in', 'rb') as f:
        raw_lines = f.readlines()

    lines = []
    for line in raw_lines:
        try:
            decoded = line.decode('ascii').strip()
            if decoded and not decoded.startswith('*'):
                lines.append(decoded)
        except UnicodeDecodeError:
            pass

    first_line = lines[0].split()
    n = int(first_line[0])
    variant = int(first_line[1])

    matrix = []
    for i in range(1, n + 1):
        row = list(map(int, lines[i].split()))
        matrix.append(row)

    edges = []
    for i in range(n):
        for j in range(i + 1, n):
            w = matrix[i][j]
            if w != 0:
                edges.append((w, i, j))

    edges.sort()

    parent = list(range(n))
    rank = [0] * n

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    def union(x, y):
        rx, ry = find(x), find(y)
        if rx == ry:
            return False
        if rank[rx] < rank[ry]:
            rx, ry = ry, rx
        parent[ry] = rx
        if rank[rx] == rank[ry]:
            rank[rx] += 1
        return True

    mst_edges = []
    total_weight = 0

    for w, u, v in edges:
        if union(u, v):
            mst_edges.append((w, u, v))
            total_weight += w
            if len(mst_edges) == n - 1:
                break

    mst_matrix = [[0] * n for _ in range(n)]
    for w, u, v in mst_edges:
        mst_matrix[u][v] = w
        mst_matrix[v][u] = w

    with open('input_7.out', 'w') as f:
        f.write(f"{total_weight}\n")
        for row in mst_matrix:
            f.write(", ".join(map(str, row)) + "\n")
        edge_strs = []
        for w, u, v in mst_edges:
            a, b = u + 1, v + 1
            if a > b:
                a, b = b, a
            edge_strs.append(f"({a}, {b})")
        f.write(", ".join(edge_strs) + "\n")

    print(f"n={n}, variant={variant}")
    print(f"Total MST weight: {total_weight}")
    print(f"MST edges count: {len(mst_edges)}")
    print(f"First 5 edges: {edge_strs[:5]}")

solve()