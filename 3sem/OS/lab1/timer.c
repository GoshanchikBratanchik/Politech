#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "devices/pit.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/thread.h"
  
/* Аппаратные детали таймерного чипа 8254 см. в [8254]. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* Количество тиков таймера с момента загрузки ОС. */
static int64_t ticks;

/* Количество циклов на один тик таймера.
   Инициализируется timer_calibrate(). */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);
static void real_time_delay (int64_t num, int32_t denom);

static void add_thread(struct thread* t, int64_t good_morning);
static struct thread* slthreads[128];
static int sltk = 0;

/* Настраивает таймер на прерывание TIMER_FREQ раз в секунду
   и регистрирует соответствующее прерывание. */
void
timer_init (void) 
{
  pit_configure_channel (0, 2, TIMER_FREQ);
  intr_register_ext (0x20, timer_interrupt, "8254 Timer");
}

static void add_thread (struct thread *t, int64_t good_morning)
{
    int i = sltk;

    ASSERT(sltk < 128);

    while (i > 0 && slthreads[i - 1]->wakeup > good_morning) {
        slthreads[i] = slthreads[i - 1];
        i--;
    }
    slthreads[i] = t;
    sltk++;
}

/* Калибрует loops_per_tick, используемый для реализации коротких задержек. */
void
timer_calibrate (void) 
{
  unsigned high_bit, test_bit;

  ASSERT (intr_get_level () == INTR_ON);
  printf ("Calibrating timer...  ");

  /* Приближаем loops_per_tick как наибольшую степень двойки,
     все еще меньшую одного тика таймера. */
  loops_per_tick = 1u << 10;
  while (!too_many_loops (loops_per_tick << 1)) 
    {
      loops_per_tick <<= 1;
      ASSERT (loops_per_tick != 0);
    }

  /* Уточняем следующие 8 бит loops_per_tick. */
  high_bit = loops_per_tick;
  for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
    if (!too_many_loops (high_bit | test_bit))
      loops_per_tick |= test_bit;

  printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* Возвращает количество тиков таймера с момента загрузки ОС. */
int64_t
timer_ticks (void) 
{
  enum intr_level old_level = intr_disable ();
  int64_t t = ticks;
  intr_set_level (old_level);
  return t;
}

/* Возвращает количество тиков таймера, прошедших с момента THEN,
   которое должно быть значением, ранее возвращенным timer_ticks(). */
int64_t
timer_elapsed (int64_t then) 
{
  return timer_ticks () - then;
}

/* Спит приблизительно TICKS тиков таймера. Прерывания должны быть включены. */
void
timer_sleep (int64_t ticks) 
{
  if (ticks <= 0) return;
  struct thread *sl = thread_current();
  int64_t good_morning = ticks + timer_ticks();
  sl->wakeup = good_morning;
  enum intr_level old_level = intr_disable();
  add_thread(sl, good_morning);
  thread_block();
  intr_set_level(old_level);
}

/* Спит приблизительно MS миллисекунд. Прерывания должны быть включены. */
void
timer_msleep (int64_t ms) 
{
  real_time_sleep (ms, 1000);
}

/* Спит приблизительно US микросекунд. Прерывания должны быть включены. */
void
timer_usleep (int64_t us) 
{
  real_time_sleep (us, 1000 * 1000);
}

/* Спит приблизительно NS наносекунд. Прерывания должны быть включены. */
void
timer_nsleep (int64_t ns) 
{
  real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* Активное ожидание приблизительно MS миллисекунд. Прерывания могут быть отключены.

   Активное ожидание тратит циклы CPU, и активное ожидание с отключенными прерываниями
   на интервал между тиками таймера или дольше приведет к потере тиков таймера.
   Поэтому используйте timer_msleep(), если прерывания включены. */
void
timer_mdelay (int64_t ms) 
{
  real_time_delay (ms, 1000);
}

/* Спит приблизительно US микросекунд. Прерывания могут быть отключены.

   Активное ожидание тратит циклы CPU, и активное ожидание с отключенными прерываниями
   на интервал между тиками таймера или дольше приведет к потере тиков таймера.
   Поэтому используйте timer_usleep(), если прерывания включены. */
void
timer_udelay (int64_t us) 
{
  real_time_delay (us, 1000 * 1000);
}

/* Приостанавливает выполнение приблизительно на NS наносекунд. Прерывания могут быть отключены.

   Активное ожидание тратит циклы CPU, и активное ожидание с отключенными прерываниями
   на интервал между тиками таймера или дольше приведет к потере тиков таймера.
   Поэтому используйте timer_nsleep(), если прерывания включены.*/
void
timer_ndelay (int64_t ns) 
{
  real_time_delay (ns, 1000 * 1000 * 1000);
}

/* Выводит статистику таймера. */
void
timer_print_stats (void) 
{
  printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/* Обработчик прерывания таймера. */
static void
timer_interrupt (struct intr_frame *args UNUSED)
{
  ticks++;
  while (sltk > 0 && slthreads[0]->wakeup <= ticks) {
    struct thread* wake_thread = slthreads[0];
    for (int i = 0; i < sltk - 1; i++) {
      slthreads[i] = slthreads[i + 1];
    } 
    sltk--;
    slthreads[sltk] = NULL;
    thread_unblock(wake_thread);
  }
  thread_tick ();
}

/* Возвращает true, если LOOPS итераций ожидают больше одного тика таймера,
   иначе false. */
static bool
too_many_loops (unsigned loops) 
{
  /* Ждем тик таймера. */
  int64_t start = ticks;
  while (ticks == start)
    barrier ();

  /* Выполняем LOOPS циклов. */
  start = ticks;
  busy_wait (loops);

  /* Если счетчик тиков изменился, мы итерировали слишком долго. */
  barrier ();
  return start != ticks;
}

/* Итерирует простой цикл LOOPS раз для реализации коротких задержек.

   Помечено NO_INLINE, потому что выравнивание кода может значительно влиять на время,
   так что если эта функция была встроена по-разному в разных местах,
   результаты было бы сложно предсказать. */
static void NO_INLINE
busy_wait (int64_t loops) 
{
  while (loops-- > 0)
    barrier ();
}

/* Спит приблизительно NUM/DENOM секунд. */
static void
real_time_sleep (int64_t num, int32_t denom) 
{
  /* Преобразуем NUM/DENOM секунд в тики таймера, округляя вниз.
          
        (NUM / DENOM) с          
     ---------------------- = NUM * TIMER_FREQ / DENOM тиков. 
      1 с / TIMER_FREQ тиков
  */
  int64_t ticks = num * TIMER_FREQ / denom;

  ASSERT (intr_get_level () == INTR_ON);
  if (ticks > 0)
    {
      /* Мы ждем как минимум один полный тик таймера. Используем
         timer_sleep(), потому что он уступит CPU другим процессам. */                
      timer_sleep (ticks); 
    }
  else 
    {
      /* В противном случае используем цикл активного ожидания для более точного
         суб-тикового тайминга. */
      real_time_delay (num, denom); 
    }
}

/* Активное ожидание приблизительно NUM/DENOM секунд. */
static void
real_time_delay (int64_t num, int32_t denom)
{
  /* Уменьшаем числитель и знаменатель в 1000 раз, чтобы избежать
     возможности переполнения. */
  ASSERT (denom % 1000 == 0);
  busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000)); 
}
