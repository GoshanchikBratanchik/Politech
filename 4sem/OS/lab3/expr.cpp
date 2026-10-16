// expr.cpp — количество расстановок знаков + и - в выражении "A1 A2 ... AN = S".
// ОС: Windows. Синхронизация: Semaphore + Critical Section.
//
// Идея: между A1..AN есть N-1 промежутков, всего 2^(N-1) вариантов знаков.
// Первые P знаков (P выбирается так, чтобы задач было заметно больше, чем потоков)
// задают номер подзадачи. Подзадача перебирает все оставшиеся знаки рекурсивно.
//
// Синхронизация:
//  - g_semTasks (Semaphore): счетчик доступных "разрешений". Изначально 0, поэтому
//    созданные потоки пула сразу засыпают на нем. Main после старта замера времени
//    выпускает (кол-во задач + кол-во потоков) разрешений: одно на каждую задачу и
//    по одному "стоп-сигналу" на каждый поток.
//  - g_cs (Critical Section): защищает счетчик следующей задачи и общий результат.
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

typedef long long ll;
typedef unsigned long long ull;

// ----- Входные данные (только чтение из потоков после запуска) -----
static int g_threads = 0;
static int g_n = 0;
static ll* g_a = NULL;
static ll g_s = 0;

// ----- Разбиение на подзадачи -----
static int g_prefixLen = 0;   // сколько первых знаков фиксирует номер задачи
static ll g_taskCount = 0;    // 2^g_prefixLen

// ----- Общие данные, защищенные g_cs -----
static CRITICAL_SECTION g_cs;
static volatile ll g_nextTask = 0;
static volatile ull g_total = 0;

static HANDLE g_semTasks = NULL;

// Рекурсивный перебор знаков для элементов a[idx..n-1]; sum — значение уже
// сформированной части выражения.
static ull CountFrom(int idx, ll sum)
{
    if (idx == g_n - 1)
    {
        // Последний элемент: два варианта знака проверяются сразу.
        ull c = 0;
        if (sum + g_a[idx] == g_s) c++;
        if (sum - g_a[idx] == g_s) c++;
        return c;
    }
    return CountFrom(idx + 1, sum + g_a[idx]) + CountFrom(idx + 1, sum - g_a[idx]);
}

// Решение одной подзадачи: биты номера задачи задают знаки перед A2..A(P+1).
static ull SolveTask(ll task)
{
    ll sum = g_a[0];
    int i;
    for (i = 0; i < g_prefixLen; i++)
    {
        if ((task >> i) & 1)
            sum -= g_a[i + 1];
        else
            sum += g_a[i + 1];
    }
    if (g_prefixLen + 1 == g_n)          // все знаки уже определены
        return (sum == g_s) ? 1 : 0;
    return CountFrom(g_prefixLen + 1, sum);
}

// Взять номер следующей задачи. Возвращает -1, если задачи закончились
// (это значит, что полученное разрешение семафора — "стоп-сигнал").
static ll TakeTask()
{
    ll task = -1;
    EnterCriticalSection(&g_cs);
    if (g_nextTask < g_taskCount)
    {
        task = g_nextTask;
        g_nextTask++;
    }
    LeaveCriticalSection(&g_cs);
    return task;
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
        // Блокирующее ожидание: поток спит, пока нет разрешений.
        WaitForSingleObject(g_semTasks, INFINITE);
        ll task = TakeTask();
        if (task < 0)
            break;                       // стоп-сигнал
        local += SolveTask(task);
    }
    AddResult(local);
    return 0;
}

static int ReadInput(const char* fileName)
{
    FILE* f = fopen(fileName, "r");
    int i;
    if (!f)
        return 0;
    if (fscanf(f, "%d %d", &g_threads, &g_n) != 2 || g_threads < 1 || g_n < 2)
    {
        fclose(f);
        return 0;
    }
    g_a = new ll[g_n];
    // N чисел A и затем S; все они 32-разрядные, long их вмещает.
    for (i = 0; i <= g_n; i++)
    {
        long v;
        if (fscanf(f, "%ld", &v) != 1) { fclose(f); return 0; }
        if (i < g_n) g_a[i] = v; else g_s = v;
    }
    fclose(f);
    return 1;
}

// Подбор длины префикса: примерно 64 задачи на поток (не больше 2^20 задач).
static void ChooseSplit()
{
    g_prefixLen = 0;
    g_taskCount = 1;
    while (g_prefixLen < g_n - 1 && g_prefixLen < 20 && g_taskCount < (ll)g_threads * 64)
    {
        g_prefixLen++;
        g_taskCount <<= 1;
    }
}

static double NowMs(LARGE_INTEGER freq)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart * 1000.0 / (double)freq.QuadPart;
}

static void WriteOutput(double ms)
{
    FILE* f = fopen("output.txt", "w");
    if (f)
    {
        // Вывод 64-битного числа без %llu/%I64u — для совместимости со старыми CRT.
        char buf[32];
        int pos = 31;
        ull v = g_total;
        buf[pos] = 0;
        do { buf[--pos] = (char)('0' + (int)(v % 10)); v /= 10; } while (v);
        fprintf(f, "%d\n%d\n%s\n", g_threads, g_n, buf + pos);
        fclose(f);
    }
    f = fopen("time.txt", "w");
    if (f)
    {
        fprintf(f, "%.0f\n", ms);
        fclose(f);
    }
}

int main()
{
    HANDLE* threads;
    LARGE_INTEGER freq;
    double started, finished;
    int i;

    if (!ReadInput("input.txt"))
    {
        printf("Error reading input.txt\n");
        delete[] g_a;
        return 1;
    }
    ChooseSplit();

    InitializeCriticalSection(&g_cs);
    g_semTasks = CreateSemaphore(NULL, 0, 0x7fffffff, NULL);

    // Пул потоков создается сразу, все потоки засыпают на семафоре.
    threads = new HANDLE[g_threads];
    for (i = 0; i < g_threads; i++)
        threads[i] = CreateThread(NULL, 0, WorkerThread, NULL, 0, NULL);

    QueryPerformanceFrequency(&freq);
    started = NowMs(freq);

    // Выдаем разрешения: задачи + по стоп-сигналу на каждый поток.
    ReleaseSemaphore(g_semTasks, (LONG)(g_taskCount + g_threads), NULL);

    // WaitForMultipleObjects ограничен 64 описателями, поэтому ждем по одному.
    for (i = 0; i < g_threads; i++)
        WaitForSingleObject(threads[i], INFINITE);

    finished = NowMs(freq);

    for (i = 0; i < g_threads; i++)
        CloseHandle(threads[i]);
    delete[] threads;
    CloseHandle(g_semTasks);
    DeleteCriticalSection(&g_cs);

    WriteOutput(finished - started);
    delete[] g_a;
    return 0;
}
