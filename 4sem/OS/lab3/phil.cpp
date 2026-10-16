// phil.cpp — обедающие философы.
// ОС: Windows. Синхронизация: Mutex + Event. Метод: состояние философов.
// Запуск: phil TOTAL PHIL
//
// Состояния философа: T (думает), H (голоден, ждет), E (ест).
// В выводе H не показывается — голодный философ для наблюдателя все еще "T".
//
// Общие данные (g_state, g_ticket, g_nextTicket) защищены одним мьютексом g_mutex.
// У каждого философа свое событие с автосбросом g_canEat[i]: голодный философ
// спит на нем, а разрешение есть выдает функция Test(), когда условие выполнено.
//
// Условие CanEat(i): философ i голоден, ни один сосед не ест и ни один сосед
// не голоден "дольше" (номерок голода соседа не меньше номерка i).
// Номерок выдается из возрастающего счетчика в момент перехода в H.
//
// Глобальный инвариант (выполняется всякий раз, когда мьютекс свободен):
//   1) никакие два соседа не находятся одновременно в состоянии E;
//   2) ни один философ в состоянии H не удовлетворяет CanEat — т.е. каждый
//      голодный ждет либо едящего соседа, либо голодного соседа со старшим номерком.
// Пункт 2 поддерживается тем, что Test(i) вызывается при каждом событии, после
// которого CanEat(i) может стать истинным: при переходе i в H и при переходе
// любого соседа i в T (сосед перестал есть или отказался ждать).
//
// Отсутствие взаимной блокировки и голодания: среди голодных философов тот,
// у кого минимальный номерок, ждет только едящих соседей; еда длится конечное
// время (PHIL мс), после чего сосед уходит в T и вызывает Test — значит
// "самый старший" голодный обязательно начнет есть. Новые номерки только
// больше, поэтому каждый голодный за конечное время становится самым старшим.
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _MSC_VER
#pragma comment(lib, "winmm.lib")   // timeBeginPeriod — точность Sleep 1 мс
#endif

#define PHIL_COUNT 5

enum PhilState { THINKING, HUNGRY, EATING };

// ----- Параметры -----
static long g_total = 0;   // TOTAL, мс
static long g_phil = 0;    // PHIL, мс

// ----- Время -----
static LARGE_INTEGER g_freq;
static LARGE_INTEGER g_start;

// ----- Общее состояние, защищено g_mutex -----
static HANDLE g_mutex = NULL;
static HANDLE g_canEat[PHIL_COUNT];
static volatile int g_state[PHIL_COUNT];
static volatile unsigned long g_ticket[PHIL_COUNT];
static unsigned long g_nextTicket = 0;

// Миллисекунды от начала работы программы.
static long NowMs()
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (long)((t.QuadPart - g_start.QuadPart) * 1000 / g_freq.QuadPart);
}

// Пассивное ожидание до момента t (Sleep — поток заблокирован).
// Цикл нужен только на случай, если Sleep проснулся на миллисекунду раньше.
static void SleepUntil(long t)
{
    long now;
    while ((now = NowMs()) < t)
        Sleep((DWORD)(t - now));
}

static int LeftOf(int i)  { return (i + PHIL_COUNT - 1) % PHIL_COUNT; }
static int RightOf(int i) { return (i + 1) % PHIL_COUNT; }

static void Lock()   { WaitForSingleObject(g_mutex, INFINITE); }
static void Unlock() { ReleaseMutex(g_mutex); }

// Сосед n не мешает философу i есть. Вызывать под мьютексом.
static int NeighbourAllows(int i, int n)
{
    if (g_state[n] == EATING)
        return 0;
    if (g_state[n] == HUNGRY && g_ticket[n] < g_ticket[i])
        return 0;
    return 1;
}

// Если философ i может есть — перевести его в E и разбудить. Под мьютексом.
static void Test(int i)
{
    if (g_state[i] == HUNGRY && NeighbourAllows(i, LeftOf(i)) && NeighbourAllows(i, RightOf(i)))
    {
        g_state[i] = EATING;
        SetEvent(g_canEat[i]);
    }
}

// Философ i переходит в T (закончил есть или перестал ждать) — соседи
// могли получить возможность есть. Под мьютексом.
static void PutDown(int i)
{
    g_state[i] = THINKING;
    Test(LeftOf(i));
    Test(RightOf(i));
}

// Печать смены состояния. Под мьютексом — строки выводятся по порядку времени.
static void PrintChange(int i, char from, char to, long time)
{
    printf("%ld:%d:%c->%c\n", time, i + 1, from, to);
    fflush(stdout);
}

// Попытка начать есть. Возвращает время начала еды или -1, если
// время работы программы истекло и философ должен завершиться.
static long TakeForks(int i)
{
    long now, eatStart = -1;
    DWORD waitRes;

    Lock();
    g_state[i] = HUNGRY;
    g_ticket[i] = g_nextTicket++;
    Test(i);
    Unlock();

    now = NowMs();
    waitRes = WaitForSingleObject(g_canEat[i], now < g_total ? (DWORD)(g_total - now) : 0);

    Lock();
    now = NowMs();
    if (waitRes == WAIT_OBJECT_0 && now < g_total)
    {
        PrintChange(i, 'T', 'E', now);
        eatStart = now;
    }
    else
    {
        // Время вышло: есть больше нельзя. Если разрешение успело прийти —
        // отказываемся от него, в любом случае отпускаем соседей.
        PutDown(i);
    }
    Unlock();
    return eatStart;
}

// Закончить еду. Возвращает время перехода в T.
static long ReleaseForks(int i)
{
    long now;
    Lock();
    now = NowMs();
    PrintChange(i, 'E', 'T', now);
    PutDown(i);
    Unlock();
    return now;
}

static DWORD WINAPI PhilosopherThread(void* param)
{
    int i = (int)((char*)param - (char*)0);
    long stateStart = 0;   // момент последней смены состояния (изначально T с 0 мс)

    while (1)
    {
        long eatStart;
        // Думает не меньше PHIL мс. Если к этому моменту TOTAL уже истечет —
        // есть он больше не сможет, завершаемся.
        if (stateStart + g_phil >= g_total)
            break;
        SleepUntil(stateStart + g_phil);

        eatStart = TakeForks(i);
        if (eatStart < 0)
            break;

        SleepUntil(eatStart + g_phil);   // ест не меньше PHIL мс
        stateStart = ReleaseForks(i);
    }
    return 0;
}

static int ParseArgs(int argc, char** argv)
{
    if (argc != 3)
        return 0;
    g_total = atol(argv[1]);
    g_phil = atol(argv[2]);
    return g_total > 0 && g_phil > 0;
}

int main(int argc, char** argv)
{
    HANDLE threads[PHIL_COUNT];
    int i;

    if (!ParseArgs(argc, argv))
    {
        printf("Usage: phil TOTAL PHIL\n");
        return 1;
    }

    timeBeginPeriod(1);
    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_start);

    g_mutex = CreateMutex(NULL, FALSE, NULL);
    for (i = 0; i < PHIL_COUNT; i++)
    {
        g_state[i] = THINKING;
        g_ticket[i] = 0;
        g_canEat[i] = CreateEvent(NULL, FALSE, FALSE, NULL);   // автосброс, не сигнальное
    }

    for (i = 0; i < PHIL_COUNT; i++)
        threads[i] = CreateThread(NULL, 0, PhilosopherThread, (char*)0 + i, 0, NULL);

    WaitForMultipleObjects(PHIL_COUNT, threads, TRUE, INFINITE);

    for (i = 0; i < PHIL_COUNT; i++)
    {
        CloseHandle(threads[i]);
        CloseHandle(g_canEat[i]);
    }
    CloseHandle(g_mutex);
    timeEndPeriod(1);
    return 0;
}
