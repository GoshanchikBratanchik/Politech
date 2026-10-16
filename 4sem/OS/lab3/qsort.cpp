// qsort.cpp — многопоточная быстрая сортировка.
// ОС: Linux. Синхронизация: Mutex + Condition variable.
// Сборка: g++ -O2 qsort.cpp -D_REENTRANT -lpthread -o qsort
//
// Схема работы:
//  - main читает данные, создает пул потоков; потоки засыпают на условной
//    переменной g_cond, т.к. очередь подзадач пуста.
//  - main засекает время и кладет в очередь одну задачу — весь массив.
//  - Поток берет из очереди отрезок [lo, hi). Если он большой — делит его
//    (разбиение Хоара), правую часть кладет в очередь (будит один спящий поток),
//    а левую продолжает обрабатывать сам. Маленький отрезок (<= g_grain)
//    сортируется последовательно рекурсивно.
//  - g_pending — число отрезков, которые еще не отсортированы до конца
//    (лежат в очереди или обрабатываются). Когда оно становится 0, сортировка
//    закончена: выставляется g_done и всем потокам посылается broadcast.
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vector>

struct Range
{
    long lo;   // включительно
    long hi;   // не включительно
};

// ----- Данные сортировки -----
static int* g_arr = NULL;
static long g_n = 0;
static int g_threads = 0;
static long g_grain = 1000;   // отрезки не длиннее — сортируются без разбиения

// ----- Очередь подзадач и состояние, защищенные g_mutex -----
static pthread_mutex_t g_mutex;
static pthread_cond_t g_cond;          // "в очереди появилась задача или работа закончена"
static std::vector<Range> g_queue;
static volatile long g_pending = 0;
static volatile int g_done = 0;

// ================= Последовательная часть =================

static void Swap(int* a, int* b)
{
    int t = *a; *a = *b; *b = t;
}

static void InsertionSort(int* a, long n)
{
    long i, j;
    for (i = 1; i < n; i++)
    {
        int v = a[i];
        for (j = i - 1; j >= 0 && a[j] > v; j--)
            a[j + 1] = a[j];
        a[j + 1] = v;
    }
}

// Разбиение Хоара с медианой из трех. Возвращает границу p:
// все элементы [lo, p) <= всех элементов [p, hi), обе части непусты.
static long Partition(int* a, long lo, long hi)
{
    long mid = lo + (hi - lo) / 2;
    long i, j;
    int pivot;
    if (a[mid] < a[lo]) Swap(&a[mid], &a[lo]);
    if (a[hi - 1] < a[lo]) Swap(&a[hi - 1], &a[lo]);
    if (a[hi - 1] < a[mid]) Swap(&a[hi - 1], &a[mid]);
    pivot = a[mid];
    i = lo - 1;
    j = hi;
    while (1)
    {
        do { i++; } while (a[i] < pivot);
        do { j--; } while (a[j] > pivot);
        if (i >= j)
            return j + 1;
        Swap(&a[i], &a[j]);
    }
}

// Последовательная рекурсивная быстрая сортировка (рекурсия по меньшей части).
static void SeqQuickSort(int* a, long lo, long hi)
{
    while (hi - lo > 16)
    {
        long p = Partition(a, lo, hi);
        if (p - lo < hi - p)
        {
            SeqQuickSort(a, lo, p);
            lo = p;
        }
        else
        {
            SeqQuickSort(a, p, hi);
            hi = p;
        }
    }
    InsertionSort(a + lo, hi - lo);
}

// ================= Очередь подзадач =================

// Положить отрезок в очередь и разбудить один ожидающий поток.
static void PushTask(long lo, long hi)
{
    Range r;
    r.lo = lo;
    r.hi = hi;
    pthread_mutex_lock(&g_mutex);
    g_queue.push_back(r);
    g_pending++;
    pthread_cond_signal(&g_cond);
    pthread_mutex_unlock(&g_mutex);
}

// Взять отрезок из очереди. Если очередь пуста — поток блокируется на
// условной переменной. Возвращает 0, когда вся работа завершена.
static int PopTask(Range* out)
{
    int ok = 0;
    pthread_mutex_lock(&g_mutex);
    while (g_queue.empty() && !g_done)
        pthread_cond_wait(&g_cond, &g_mutex);
    if (!g_queue.empty())
    {
        *out = g_queue.back();
        g_queue.pop_back();
        ok = 1;
    }
    pthread_mutex_unlock(&g_mutex);
    return ok;
}

// Отметить, что один отрезок полностью отсортирован.
static void TaskFinished()
{
    pthread_mutex_lock(&g_mutex);
    g_pending--;
    if (g_pending == 0)
    {
        g_done = 1;
        pthread_cond_broadcast(&g_cond);   // будим всех — пора завершаться
    }
    pthread_mutex_unlock(&g_mutex);
}

// ================= Поток пула =================

static void* WorkerThread(void* param)
{
    Range r;
    (void)param;
    while (PopTask(&r))
    {
        // Делим отрезок, пока он большой: правую часть отдаем в очередь.
        while (r.hi - r.lo > g_grain)
        {
            long p = Partition(g_arr, r.lo, r.hi);
            PushTask(p, r.hi);
            r.hi = p;
        }
        SeqQuickSort(g_arr, r.lo, r.hi);
        TaskFinished();
    }
    return 0;
}

// ================= Ввод / вывод =================

// Чтение всего файла в память (быстрее fscanf на миллионах чисел).
static char* ReadFile(const char* name)
{
    FILE* f = fopen(name, "rb");
    char* buf;
    long size;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (char*)malloc(size + 1);
    if (buf)
    {
        size = (long)fread(buf, 1, size, f);
        buf[size] = 0;
    }
    fclose(f);
    return buf;
}

static long ParseLong(char** p)
{
    char* s = *p;
    long v;
    while (*s && (*s == ' ' || *s == '\n' || *s == '\r' || *s == '\t')) s++;
    v = strtol(s, p, 10);
    return v;
}

static int ReadInput(const char* name)
{
    char* buf = ReadFile(name);
    char* p = buf;
    long i;
    if (!buf) return 0;
    g_threads = (int)ParseLong(&p);
    g_n = ParseLong(&p);
    if (g_threads < 1 || g_n < 0)
    {
        free(buf);
        return 0;
    }
    g_arr = (int*)malloc(sizeof(int) * (g_n > 0 ? g_n : 1));
    for (i = 0; i < g_n; i++)
        g_arr[i] = (int)ParseLong(&p);
    free(buf);
    return 1;
}

static void WriteOutput(long ms)
{
    FILE* f = fopen("output.txt", "w");
    long i;
    if (f)
    {
        char* buf = (char*)malloc(1 << 20);
        setvbuf(f, buf, _IOFBF, 1 << 20);
        fprintf(f, "%d\n%ld\n", g_threads, g_n);
        for (i = 0; i < g_n; i++)
            fprintf(f, i + 1 < g_n ? "%d " : "%d", g_arr[i]);
        fprintf(f, "\n");
        fclose(f);
        free(buf);
    }
    f = fopen("time.txt", "w");
    if (f)
    {
        fprintf(f, "%ld\n", ms);
        fclose(f);
    }
}

static long NowMs()
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

int main()
{
    pthread_t* tids;
    long started, finished;
    int i;

    if (!ReadInput("input.txt"))
    {
        printf("Error reading input.txt\n");
        return 1;
    }
    // Размер "неделимого" куска: не меньше 1000 и такой, чтобы на поток
    // приходилось порядка десятка кусков.
    g_grain = g_n / ((long)g_threads * 16);
    if (g_grain < 1000) g_grain = 1000;

    pthread_mutex_init(&g_mutex, 0);
    pthread_cond_init(&g_cond, 0);

    tids = new pthread_t[g_threads];
    for (i = 0; i < g_threads; i++)
        pthread_create(&tids[i], 0, WorkerThread, 0);

    started = NowMs();
    PushTask(0, g_n);           // первая задача — весь массив
    for (i = 0; i < g_threads; i++)
        pthread_join(tids[i], 0);
    finished = NowMs();

    delete[] tids;
    pthread_mutex_destroy(&g_mutex);
    pthread_cond_destroy(&g_cond);

    WriteOutput(finished - started);
    free(g_arr);
    return 0;
}
