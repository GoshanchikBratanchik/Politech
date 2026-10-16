// msort.cpp — многопоточная сортировка слиянием.
// ОС: Linux. Синхронизация: только семафоры (sem_t).
// Сборка: g++ -O2 msort.cpp -D_REENTRANT -lpthread -o msort
//
// Схема работы — дерево подзадач:
//  - Узел дерева описывает отрезок [lo, hi). Большой узел делится пополам на два
//    дочерних. Правый ребенок кладется в очередь (его может забрать другой
//    поток), левый поток обрабатывает сам. Узел не длиннее g_grain сортируется
//    последовательно (рекурсивной сортировкой слиянием).
//  - Когда узел отсортирован, поток увеличивает у родителя счетчик готовых
//    детей. Тот поток, который увидел "2 из 2", сам сливает две половины
//    родителя и поднимается по дереву дальше. Ожидания результата детей нет —
//    слияние выполняет тот, кто завершил последнего ребенка.
//  - Когда отсортирован корень, в очередь кладется по одному "стоп"-элементу
//    (NULL) на каждый поток пула.
//
// Семафоры:
//  - g_semQueue (двоичный, 1): взаимное исключение при работе с очередью;
//  - g_semTasks (счетный, 0): количество элементов в очереди; на нем спят
//    потоки, которым нечего делать;
//  - g_semTree  (двоичный, 1): защита счетчиков готовых детей в узлах.
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vector>

struct Node
{
    long lo, hi;          // отрезок [lo, hi)
    long mid;             // граница между детьми (для слияния)
    Node* parent;
    Node* left;
    Node* right;
    volatile int doneChildren;   // защищено g_semTree
};

// ----- Данные сортировки -----
static int* g_arr = NULL;
static int* g_tmp = NULL;     // вспомогательный буфер для слияния
static long g_n = 0;
static int g_threads = 0;
static long g_grain = 1000;

// ----- Очередь подзадач -----
static std::vector<Node*> g_queue;
static sem_t g_semQueue;
static sem_t g_semTasks;
static sem_t g_semTree;

// ================= Последовательная часть =================

// Слияние отсортированных a[lo, mid) и a[mid, hi) через буфер tmp.
static void Merge(int* a, int* tmp, long lo, long mid, long hi)
{
    long i = lo, j = mid, k = lo;
    if (a[mid - 1] <= a[mid])
        return;                       // половины уже идут по порядку
    while (i < mid && j < hi)
        tmp[k++] = (a[j] < a[i]) ? a[j++] : a[i++];
    while (i < mid)
        tmp[k++] = a[i++];
    // Хвост правой половины уже стоит на своем месте в a.
    memcpy(a + lo, tmp + lo, sizeof(int) * (k - lo));
}

static void InsertionSort(int* a, long lo, long hi)
{
    long i, j;
    for (i = lo + 1; i < hi; i++)
    {
        int v = a[i];
        for (j = i - 1; j >= lo && a[j] > v; j--)
            a[j + 1] = a[j];
        a[j + 1] = v;
    }
}

static void SeqMergeSort(int* a, int* tmp, long lo, long hi)
{
    long mid;
    if (hi - lo <= 16)
    {
        InsertionSort(a, lo, hi);
        return;
    }
    mid = lo + (hi - lo) / 2;
    SeqMergeSort(a, tmp, lo, mid);
    SeqMergeSort(a, tmp, mid, hi);
    Merge(a, tmp, lo, mid, hi);
}

// ================= Очередь (только семафоры) =================

static void PushTask(Node* node)
{
    sem_wait(&g_semQueue);
    g_queue.push_back(node);
    sem_post(&g_semQueue);
    sem_post(&g_semTasks);       // один элемент стал доступен
}

// Блокирует поток, пока в очереди нет элементов.
static Node* PopTask()
{
    Node* node;
    sem_wait(&g_semTasks);
    sem_wait(&g_semQueue);
    node = g_queue.back();
    g_queue.pop_back();
    sem_post(&g_semQueue);
    return node;
}

// ================= Дерево подзадач =================

static Node* NewNode(long lo, long hi, Node* parent)
{
    Node* n = new Node;
    n->lo = lo;
    n->hi = hi;
    n->mid = lo;
    n->parent = parent;
    n->left = NULL;
    n->right = NULL;
    n->doneChildren = 0;
    return n;
}

// Отрезок node отсортирован. Поднимаемся вверх, пока этот поток —
// последний из двух детей: тогда он сам выполняет слияние у родителя.
static void NodeSorted(Node* node)
{
    while (node->parent)
    {
        Node* p = node->parent;
        int ready;
        sem_wait(&g_semTree);
        p->doneChildren++;
        ready = (p->doneChildren == 2);
        sem_post(&g_semTree);
        if (!ready)
            return;                  // второй ребенок еще сортируется другим потоком
        Merge(g_arr, g_tmp, p->lo, p->mid, p->hi);
        delete p->left;
        delete p->right;
        p->left = p->right = NULL;
        node = p;
    }
    // Отсортирован корень — останавливаем все потоки пула.
    for (int i = 0; i < g_threads; i++)
        PushTask(NULL);
}

// Обработка узла, взятого из очереди: деление до g_grain, затем сортировка.
static void ProcessNode(Node* node)
{
    while (node->hi - node->lo > g_grain)
    {
        node->mid = node->lo + (node->hi - node->lo) / 2;
        node->left = NewNode(node->lo, node->mid, node);
        node->right = NewNode(node->mid, node->hi, node);
        PushTask(node->right);
        node = node->left;
    }
    SeqMergeSort(g_arr, g_tmp, node->lo, node->hi);
    NodeSorted(node);
}

static void* WorkerThread(void* param)
{
    Node* node;
    (void)param;
    while ((node = PopTask()) != NULL)
        ProcessNode(node);
    return 0;
}

// ================= Ввод / вывод =================

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
    while (*s && (*s == ' ' || *s == '\n' || *s == '\r' || *s == '\t')) s++;
    return strtol(s, p, 10);
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
    Node* root;
    long started, finished;
    int i;

    if (!ReadInput("input.txt"))
    {
        printf("Error reading input.txt\n");
        return 1;
    }
    g_grain = g_n / ((long)g_threads * 16);
    if (g_grain < 1000) g_grain = 1000;
    g_tmp = (int*)malloc(sizeof(int) * (g_n > 0 ? g_n : 1));

    sem_init(&g_semQueue, 0, 1);
    sem_init(&g_semTasks, 0, 0);
    sem_init(&g_semTree, 0, 1);

    tids = new pthread_t[g_threads];
    for (i = 0; i < g_threads; i++)
        pthread_create(&tids[i], 0, WorkerThread, 0);

    root = NewNode(0, g_n, NULL);
    started = NowMs();
    PushTask(root);
    for (i = 0; i < g_threads; i++)
        pthread_join(tids[i], 0);
    finished = NowMs();

    delete root;
    delete[] tids;
    sem_destroy(&g_semQueue);
    sem_destroy(&g_semTasks);
    sem_destroy(&g_semTree);

    WriteOutput(finished - started);
    free(g_arr);
    free(g_tmp);
    return 0;
}
