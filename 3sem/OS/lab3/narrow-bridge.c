/* File for 'narrow_bridge' task implementation.
   SPbSTU, IBKS, 2017 */
/* File for 'narrow_bridge' task implementation.
   SPbSTU, IBKS, 2017 */
#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "narrow-bridge.h"

#define NO_DIRECTION -1
#define WAVE_LIMIT 2

static struct lock bridge_lock;

static struct semaphore sema_norm_left;
static struct semaphore sema_norm_right;
static struct semaphore sema_emer_left;
static struct semaphore sema_emer_right;

static int on_bridge;
static int bridge_dir;
static int wave_count;

static int waiting_norm_left;
static int waiting_norm_right;
static int waiting_emer_left;
static int waiting_emer_right;

static int
any_waiting(enum car_direction dir)
{
    if (dir == dir_left)
        return waiting_emer_left + waiting_norm_left;
    else
        return waiting_emer_right + waiting_norm_right;
}

static int
emer_waiting(enum car_direction dir)
{
    return (dir == dir_left) ? waiting_emer_left : waiting_emer_right;
}

static enum car_direction
opposite(enum car_direction dir)
{
    return (dir == dir_left) ? dir_right : dir_left;
}

static int
wake_one(enum car_direction dir)
{
    if (dir == dir_left) {
        if (waiting_emer_left > 0) {
            waiting_emer_left--;
            on_bridge++;
            wave_count++;
            sema_up(&sema_emer_left);
            return 1;
        }
        if (waiting_norm_left > 0) {
            waiting_norm_left--;
            on_bridge++;
            wave_count++;
            sema_up(&sema_norm_left);
            return 1;
        }
    } else {
        if (waiting_emer_right > 0) {
            waiting_emer_right--;
            on_bridge++;
            wave_count++;
            sema_up(&sema_emer_right);
            return 1;
        }
        if (waiting_norm_right > 0) {
            waiting_norm_right--;
            on_bridge++;
            wave_count++;
            sema_up(&sema_norm_right);
            return 1;
        }
    }
    return 0;
}

static void
start_next_wave(void)
{
    int new_dir = NO_DIRECTION;

    bool has_prev_dir = (bridge_dir != NO_DIRECTION);
    enum car_direction prev_dir = (enum car_direction) bridge_dir;
    enum car_direction opp_dir  = has_prev_dir ? opposite(prev_dir) : dir_left;

    if (has_prev_dir && emer_waiting(opp_dir) > 0)
        new_dir = (int) opp_dir;

    if (new_dir == NO_DIRECTION && has_prev_dir && emer_waiting(prev_dir) > 0)
        new_dir = (int) prev_dir;

    if (new_dir == NO_DIRECTION) {
        if      (waiting_emer_left  > 0) new_dir = (int) dir_left;
        else if (waiting_emer_right > 0) new_dir = (int) dir_right;
    }

    if (new_dir == NO_DIRECTION && has_prev_dir && any_waiting(opp_dir) > 0)
        new_dir = (int) opp_dir;

    if (new_dir == NO_DIRECTION && has_prev_dir && any_waiting(prev_dir) > 0)
        new_dir = (int) prev_dir;

    if (new_dir == NO_DIRECTION) {
        if      (any_waiting(dir_left)  > 0) new_dir = (int) dir_left;
        else if (any_waiting(dir_right) > 0) new_dir = (int) dir_right;
    }

    if (new_dir == NO_DIRECTION) {
        bridge_dir = NO_DIRECTION;
        wave_count = 0;
        return;
    }

    bridge_dir = new_dir;
    wave_count = 0;

    enum car_direction chosen = (enum car_direction) new_dir;
    while (on_bridge < 2 && wave_count < WAVE_LIMIT && any_waiting(chosen) > 0)
        wake_one(chosen);
}

static void
try_fill_slot(void)
{
    if (on_bridge >= 2)             return;
    if (bridge_dir == NO_DIRECTION) return;
    if (wave_count >= WAVE_LIMIT)   return;

    enum car_direction cur = (enum car_direction) bridge_dir;
    enum car_direction opp = opposite(cur);

    if (emer_waiting(opp) > 0)      return;

    if (any_waiting(cur) > 0)
        wake_one(cur);
}

void narrow_bridge_init(void)
{
    lock_init(&bridge_lock);

    sema_init(&sema_norm_left,  0);
    sema_init(&sema_norm_right, 0);
    sema_init(&sema_emer_left,  0);
    sema_init(&sema_emer_right, 0);

    on_bridge  = 0;
    bridge_dir = NO_DIRECTION;
    wave_count = 0;

    waiting_norm_left  = 0;
    waiting_norm_right = 0;
    waiting_emer_left  = 0;
    waiting_emer_right = 0;
}

void arrive_bridge(enum car_priority prio, enum car_direction dir)
{
    struct semaphore *sema;

    if (prio == car_emergency)
        sema = (dir == dir_left) ? &sema_emer_left : &sema_emer_right;
    else
        sema = (dir == dir_left) ? &sema_norm_left : &sema_norm_right;

    lock_acquire(&bridge_lock);

    enum car_direction opp = opposite(dir);

    bool slot_ok      = (on_bridge < 2);
    bool dir_ok       = (bridge_dir == NO_DIRECTION || bridge_dir == (int) dir);
    bool wave_ok      = (bridge_dir == NO_DIRECTION || wave_count < WAVE_LIMIT);
    bool no_emer_opp  = (emer_waiting(opp) == 0);
    bool no_emer_same = (emer_waiting(dir) == 0);

    bool can_go;
    if (prio == car_emergency)
        can_go = slot_ok && dir_ok && wave_ok && no_emer_opp;
    else
        can_go = slot_ok && dir_ok && wave_ok && no_emer_opp && no_emer_same;

    if (can_go) {
        bridge_dir = (int) dir;
        on_bridge++;
        wave_count++;
        lock_release(&bridge_lock);
    } else {
        if (prio == car_emergency) {
            if (dir == dir_left) waiting_emer_left++;
            else                 waiting_emer_right++;
        } else {
            if (dir == dir_left) waiting_norm_left++;
            else                 waiting_norm_right++;
        }
        lock_release(&bridge_lock);
        sema_down(sema);
    }
}

void exit_bridge(enum car_priority prio UNUSED, enum car_direction dir UNUSED)
{
    lock_acquire(&bridge_lock);

    on_bridge--;

    if (on_bridge == 0)
        start_next_wave();
    else
        try_fill_slot();

    lock_release(&bridge_lock);
}
