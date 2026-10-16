// chess.cpp — количество расстановок фигур "полководец" (король + конь).
// ОС: Windows. Синхронизация: Semaphore + Critical Section.
//
// Алгоритм: перебор с возвратом по строкам доски. Строка хранится битовой маской.
// Полководец в строке r бьет:
//   - в той же строке: соседние клетки (|dx| = 1);
//   - в строках r±1:   |dx| <= 2 (король: |dx| <= 1, конь: |dx| = 2);
//   - в строках r±2:   |dx| = 1 (конь); клетка прямо через строку не бьется.
// Поэтому для новой строки достаточно знать маски двух предыдущих строк.
// Уже стоящие K фигур учитываются заранее: клетки, которые они занимают или
// бьют, запрещены (g_static). Фигуры одинаковые — решение это множество клеток,
// клетки в строке выбираются по возрастанию, поэтому каждое решение
// порождается ровно один раз.
//
// Распараллеливание: main (после запуска таймера) перебирает первые несколько
// строк и получает список "префиксов" — подзадач. Потоки пула берут подзадачи
// и досчитывают оставшиеся строки.
//  - g_semTasks (Semaphore): разрешения на взятие задачи; потоки спят на нем,
//    пока main не выпустит "задачи + по стоп-сигналу на поток";
//  - g_cs (Critical Section): индекс следующей задачи и общий счетчик решений.
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <vector>

typedef unsigned long long Mask;
typedef unsigned long long ull;

#define MAX_N 64

struct State
{
    int row;      // первая еще не заполненная строка
    Mask m2;      // фигуры в строке row-2
    Mask m1;      // фигуры в строке row-1
    int need;     // сколько фигур еще нужно поставить
};

// ----- Входные данные -----
static int g_threads = 0;
static int g_n = 0, g_l = 0, g_k = 0;
static Mask g_full = 0;
static Mask g_static[MAX_N];   // клетки, запрещенные уже стоящими фигурами

// ----- Подзадачи и результат -----
static std::vector<State> g_tasks;
static CRITICAL_SECTION g_cs;
static volatile long g_nextTask = 0;   // защищено g_cs
static volatile ull g_total = 0;       // защищено g_cs
static HANDLE g_semTasks = NULL;

// Верхняя оценка числа фигур в rows строках: в любом квадрате 2x2 не больше
// одной фигуры (король), значит на каждую пару строк — не больше ceil(N/2).
static int MaxPieces(int rows)
{
    return ((rows + 1) / 2) * ((g_n + 1) / 2);
}

// Клетки строки row, которые бьются фигурами двух предыдущих строк и заранее
// стоящими фигурами.
static Mask Forbidden(int row, Mask m2, Mask m1)
{
    Mask f = g_static[row];
    f |= m1 | (m1 << 1) | (m1 >> 1) | (m1 << 2) | (m1 >> 2);
    f |= (m2 << 1) | (m2 >> 1);
    return f & g_full;
}

static ull Search(int row, Mask m2, Mask m1, int need, int stopRow, std::vector<State>* out);

// Перебор подмножеств доступных клеток строки row (без соседних клеток).
// cur — уже выбранные клетки строки, avail — клетки правее последней выбранной.
static ull SearchRow(int row, Mask avail, Mask cur, int cnt, Mask m2, Mask m1, int need,
                     int stopRow, std::vector<State>* out)
{
    // Вариант "больше в этой строке ничего не ставим".
    ull total = Search(row + 1, m1, cur, need - cnt, stopRow, out);
    if (cnt == need)
        return total;
    while (avail)
    {
        Mask b = avail & (0 - avail);    // младший доступный бит
        avail ^= b;
        total += SearchRow(row, avail & ~(b << 1), cur | b, cnt + 1, m2, m1, need, stopRow, out);
    }
    return total;
}

// Количество решений, начиная со строки row. Если out != NULL, поиск
// останавливается на строке stopRow, а состояние сохраняется как подзадача.
static ull Search(int row, Mask m2, Mask m1, int need, int stopRow, std::vector<State>* out)
{
    if (need == 0)
        return 1;                         // остальные клетки пустые — одно решение
    if (row == g_n || need > MaxPieces(g_n - row))
        return 0;
    if (out && row == stopRow)
    {
        State s;
        s.row = row; s.m2 = m2; s.m1 = m1; s.need = need;
        out->push_back(s);
        return 0;
    }
    return SearchRow(row, ~Forbidden(row, m2, m1) & g_full, 0, 0, m2, m1, need, stopRow, out);
}

// Строит список подзадач (примерно 32 на поток). Возвращает число решений,
// найденных уже на этапе разбиения (все фигуры поместились в первые строки).
static ull BuildTasks()
{
    ull direct = 0;
    int stopRow;
    for (stopRow = 1; stopRow <= g_n; stopRow++)
    {
        g_tasks.clear();
        direct = Search(0, 0, 0, g_l, stopRow, &g_tasks);
        if (g_tasks.size() >= (size_t)g_threads * 32)
            break;
    }
    return direct;
}

static long TakeTask()
{
    long idx = -1;
    EnterCriticalSection(&g_cs);
    if (g_nextTask < (long)g_tasks.size())
    {
        idx = g_nextTask;
        g_nextTask++;
    }
    LeaveCriticalSection(&g_cs);
    return idx;
}

static void AddResult(ull value)
{
    EnterCriticalSection(&g_cs);
    g_total += value;
    LeaveCriticalSection(&g_cs);
}

static DWORD WINAPI WorkerThread(void* param)
{
    ull local = 0;
    (void)param;
    while (1)
    {
        WaitForSingleObject(g_semTasks, INFINITE);
        long idx = TakeTask();
        if (idx < 0)
            break;                        // стоп-сигнал
        const State& s = g_tasks[idx];    // список не меняется, пока потоки работают
        local += Search(s.row, s.m2, s.m1, s.need, -1, NULL);
    }
    AddResult(local);
    return 0;
}

// Отметить клетку (r, c) как запрещенную, если она на доске.
static void Forbid(int r, int c)
{
    if (r >= 0 && r < g_n && c >= 0 && c < g_n)
        g_static[r] |= (Mask)1 << c;
}

// Уже стоящая фигура: запрещена ее клетка и все клетки, которые она бьет.
static void PlaceFixed(int r, int c)
{
    static const int dr[] = { -1, -1, -1, 0, 0, 1, 1, 1, -2, -2, -1, -1, 1, 1, 2, 2 };
    static const int dc[] = { -1, 0, 1, -1, 1, -1, 0, 1, -1, 1, -2, 2, -2, 2, -1, 1 };
    int d;
    Forbid(r, c);
    for (d = 0; d < 16; d++)
        Forbid(r + dr[d], c + dc[d]);
}

static int ReadInput(const char* name)
{
    FILE* f = fopen(name, "r");
    int i;
    if (!f)
        return 0;
    if (fscanf(f, "%d %d %d %d", &g_threads, &g_n, &g_l, &g_k) != 4 ||
        g_threads < 1 || g_n < 1 || g_n > MAX_N || g_l < 0 || g_k < 0)
    {
        fclose(f);
        return 0;
    }
    g_full = (g_n == 64) ? ~(Mask)0 : (((Mask)1 << g_n) - 1);
    for (i = 0; i < g_k; i++)
    {
        int x, y;
        if (fscanf(f, "%d %d", &x, &y) != 2) { fclose(f); return 0; }
        PlaceFixed(y, x);    // y — строка, x — столбец
    }
    fclose(f);
    return 1;
}

static void WriteNumber(FILE* f, ull v)
{
    char buf[32];
    int pos = 31;
    buf[pos] = 0;
    do { buf[--pos] = (char)('0' + (int)(v % 10)); v /= 10; } while (v);
    fprintf(f, "%s\n", buf + pos);
}

static double NowMs(LARGE_INTEGER freq)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart * 1000.0 / (double)freq.QuadPart;
}

int main()
{
    HANDLE* threads;
    LARGE_INTEGER freq;
    double started, finished;
    ull direct;
    FILE* f;
    int i;

    if (!ReadInput("input.txt"))
    {
        printf("Error reading input.txt\n");
        return 1;
    }

    InitializeCriticalSection(&g_cs);
    g_semTasks = CreateSemaphore(NULL, 0, 0x7fffffff, NULL);
    threads = new HANDLE[g_threads];
    for (i = 0; i < g_threads; i++)
        threads[i] = CreateThread(NULL, 0, WorkerThread, NULL, 0, NULL);

    QueryPerformanceFrequency(&freq);
    started = NowMs(freq);

    direct = BuildTasks();
    ReleaseSemaphore(g_semTasks, (LONG)(g_tasks.size() + g_threads), NULL);
    for (i = 0; i < g_threads; i++)
        WaitForSingleObject(threads[i], INFINITE);

    finished = NowMs(freq);

    for (i = 0; i < g_threads; i++)
        CloseHandle(threads[i]);
    delete[] threads;
    CloseHandle(g_semTasks);
    DeleteCriticalSection(&g_cs);

    f = fopen("output.txt", "w");
    if (f) { WriteNumber(f, g_total + direct); fclose(f); }
    f = fopen("time.txt", "w");
    if (f) { fprintf(f, "%.0f\n", finished - started); fclose(f); }
    return 0;
}
