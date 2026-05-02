/*
  File for 'shower' task implementation.
*/

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "devices/timer.h"


#define MAX_CAPACITY 3


static struct semaphore mutex;           
static struct semaphore men_queue;       
static struct semaphore women_queue;     

static int men_inside;                   
static int women_inside;                 
static int men_waiting;                  
static int women_waiting;                


static int men_bathed_in_row;            
static int women_bathed_in_row;          


static bool men_allowed;                 
static bool women_allowed;               

static void init(void)
{
    sema_init(&mutex, 1);
    sema_init(&men_queue, 0);
    sema_init(&women_queue, 0);
    
    men_inside = 0;
    women_inside = 0;
    men_waiting = 0;
    women_waiting = 0;
    
    men_bathed_in_row = 0;
    women_bathed_in_row = 0;
    
    men_allowed = true;
    women_allowed = false;
}

static void man(void* arg UNUSED)
{
    int id = (int) arg;
    msg("man %d created.", id);
    
    sema_down(&mutex);
    
    while (!men_allowed || 
           women_inside > 0 || 
           (men_inside > 0 && men_inside >= MAX_CAPACITY)) {
        men_waiting++;
        msg("man %d waiting (men inside: %d, women inside: %d, men bathed in row: %d, allowed: %d)", 
            id, men_inside, women_inside, men_bathed_in_row, men_allowed);
        sema_up(&mutex);
        sema_down(&men_queue);  
        sema_down(&mutex);
        men_waiting--;
    }
    

    men_inside++;
    men_bathed_in_row++;
    
    msg("man %d entering shower (men inside: %d, men bathed in row: %d)", 
        id, men_inside, men_bathed_in_row);
    
    if (men_inside < MAX_CAPACITY && 
        men_bathed_in_row < MAX_CAPACITY && 
        men_waiting > 0) {
        sema_up(&men_queue);
    } else if (men_bathed_in_row >= MAX_CAPACITY) {
        men_allowed = false;
    }
    
    sema_up(&mutex);
    
    timer_sleep(10);
    
    sema_down(&mutex);
    men_inside--;
    msg("man %d leaving shower (men inside: %d)", id, men_inside);
    
    if (men_inside == 0 && women_inside == 0) {
        if (women_waiting > 0) {
            men_bathed_in_row = 0;
            women_bathed_in_row = 0;
            men_allowed = false;
            women_allowed = true;
            msg("releasing women");
            sema_up(&women_queue);
        } else if (men_waiting > 0) {
            men_bathed_in_row = 0;
            men_allowed = true;
            msg("releasing men");
            sema_up(&men_queue);
        }
    }
    
    sema_up(&mutex);
    
    thread_exit();
}

static void woman(void* arg UNUSED)
{
    int id = (int) arg;
    msg("woman %d created.", id);
    
    sema_down(&mutex);
    
    while (!women_allowed || 
           men_inside > 0 || 
           (women_inside > 0 && women_inside >= MAX_CAPACITY)) {
        women_waiting++;
        msg("woman %d waiting (men inside: %d, women inside: %d, women bathed in row: %d, allowed: %d)", 
            id, men_inside, women_inside, women_bathed_in_row, women_allowed);
        sema_up(&mutex);
        sema_down(&women_queue);  
        sema_down(&mutex);
        women_waiting--;
    }
    
    women_inside++;
    women_bathed_in_row++;
    
    msg("woman %d entering shower (women inside: %d, women bathed in row: %d)", 
        id, women_inside, women_bathed_in_row);
    
    if (women_inside < MAX_CAPACITY && 
        women_bathed_in_row < MAX_CAPACITY && 
        women_waiting > 0) {
        sema_up(&women_queue);
    } else if (women_bathed_in_row >= MAX_CAPACITY) {
        women_allowed = false;
    }
    
    sema_up(&mutex);
    
    timer_sleep(10);
    
    sema_down(&mutex);
    women_inside--;
    msg("woman %d leaving shower (women inside: %d)", id, women_inside);
    
    if (men_inside == 0 && women_inside == 0) {
        if (men_waiting > 0) {
            men_bathed_in_row = 0;
            women_bathed_in_row = 0;
            women_allowed = false;
            men_allowed = true;
            msg("releasing men");
            sema_up(&men_queue);
        } else if (women_waiting > 0) {

            women_bathed_in_row = 0;
            women_allowed = true;
            msg("releasing women");
            sema_up(&women_queue);
        }
    }
    
    sema_up(&mutex);
    
    thread_exit();
}

void test_shower(unsigned int num_men, unsigned int num_women, unsigned int interval)
{
  int last_women = 1;
  unsigned int men = 0, women = 0;
  init();

  while ( (men < num_men) || (women < num_women) )
  {
    char name[32];
    timer_sleep(interval);

    if (men < num_men && (last_women || (num_women == women)) )
    {
      men++;
      snprintf(name, sizeof(name), "man_%d", men);      
      thread_create(name, PRI_DEFAULT, &man, (void*) men);
      last_women = 0;
    }
    else if (women < num_women)
    {
      women++;
      snprintf(name, sizeof(name), "woman_%d", women);      
      thread_create(name, PRI_DEFAULT, &woman, (void*) women);
      last_women = 1;
    }
  }

  timer_msleep(5000);
  pass();
}
