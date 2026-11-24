/*
 * queue.c - bounded FIFO queue between the dispatcher and the worker pool.
 * Owner: A (Justin).
 *
 * This is the textbook bounded-buffer producer/consumer problem (L13):
 *   - ONE producer: the dispatcher thread, calling queue_push()
 *   - N consumers:  the worker threads, calling queue_pop()
 *
 * Condition variables rather than semaphores. Either satisfies the memo,
 * but unnamed sem_init() is not implemented on macOS and we build on both.
 *
 * INVARIANT: every field of queue_t is read and written ONLY while
 * q->lock is held. That is why q->shutdown can be a plain int instead of
 * volatile sig_atomic_t - the mutex supplies the ordering guarantees that
 * volatile would otherwise have to.
 *
 * LOCK ORDER: this file takes only q->lock, which sits in the middle of
 * the global order (rules -> queue -> file). It must therefore never call
 * into rules.c or output.c. It doesn't, and it never should.
 */

#include "common.h"

#include <stdio.h>      /* snprintf */


/* ---------------------------------------------------------------------
 * queue_init - prepare an empty queue.
 *
 * Called once from main.c before any thread is created, so no locking is
 * needed here.
 * ------------------------------------------------------------------ */
void queue_init(queue_t *q)
{
    q->head = 0;
    q->tail = 0;
    //how many messages are queued
    q->count = 0;
    q->shutdown = 0;

    //when first initialized the lock is unlocked
    pthread_mutex_init(&q->lock, NULL);

    //count == 0 so queue is empty
    pthread_cond_init(&q->item_avail, NULL);

    //shutdown == 0 so its accepting work
    pthread_cond_init(&q->slot_avail, NULL);
}


/* ---------------------------------------------------------------------
 * queue_destroy - release the mutex and condition variables.
 *
 * Called from main.c AFTER every worker has been pthread_join()ed.
 * Destroying a mutex another thread is blocked on is undefined behaviour,
 * so the join has to happen first.
 * ------------------------------------------------------------------ */
void queue_destroy(queue_t *q)
{
    pthread_mutex_destroy(&q->lock);
    pthread_cond_destroy(&q->item_avail);
    pthread_cond_destroy(&q->slot_avail);
}


/* ---------------------------------------------------------------------
 * queue_push - append one line. Blocks while the queue is full.
 *
 * Returns 1 if the line was queued, 0 if the queue is shutting down.
 * The 0 case is what stops a dispatcher blocked on a full queue from
 * hanging forever when SIGTERM arrives.
 * ------------------------------------------------------------------ */
int queue_push(queue_t *q, const char *line)
{
    /*acquire lock*/
    pthread_mutex_lock(&q->lock);

    while(q->count == QUEUE_CAP && !q->shutdown){
        pthread_cond_wait(&q->slot_avail, &q->lock);
    }

    if(q->shutdown){
        pthread_mutex_unlock(&q->lock);
        return 0;
    }
    /*Critical Section*/
    //copy message into queue
    snprintf(q->buf[q->tail].line, MAX_LINE + 1, "%s", line);
    q->tail = (q->tail + 1) % QUEUE_CAP;
    q->count++;
    /*signals a thread to wake up*/
    pthread_cond_signal(&q->item_avail);

    /*release lock*/
    pthread_mutex_unlock(&q->lock);
    return 1;
}


/* ---------------------------------------------------------------------
 * queue_pop - take the oldest line. Blocks while the queue is empty.
 *
 * Returns 1 and fills *out on success.
 * Returns 0 ONLY when the queue is shutting down AND drained.
 *
 * That contract is deliberate: it makes "workers drain the queue before
 * exiting" fall out of the worker's `while (queue_pop(...))` loop for
 * free, with no separate drain phase.
 *
 * TODO:
 *   1. lock q->lock
 *   2. while (count == 0 && !shutdown)
 *          pthread_cond_wait(&q->item_avail, &q->lock);
 *
 *   3. if (count == 0) { unlock; return 0; }
 *
 *      Test count, NOT shutdown. If you write `if (shutdown)` here, a
 *      worker will bail out while messages are still queued and you will
 *      lose lines on every SIGTERM - which is exactly what the Week 2
 *      "no lost lines" check catches.
 *
 *   4. *out = q->buf[q->head];
 *      Struct assignment copies the whole msg_t. This is why msg_t wraps
 *      the char array in a struct - C cannot assign raw arrays.
 *   5. head = (head + 1) % QUEUE_CAP;  count--
 *   6. pthread_cond_signal(&q->slot_avail)
 *   7. unlock; return 1
 * ------------------------------------------------------------------ */
int queue_pop(queue_t *q, msg_t *out)
{
    pthread_mutex_lock(&q->lock);
    
    while(q->count == 0 && !q->shutdown){
        pthread_cond_wait(&q->item_avail, &q->lock);
    }

    if(&q->count == 0){
        pthread_mutex_unlock(&q->lock);
        return 0;
    }

    *out = q->buf[q->head];
    q->head = (q->head + 1) % QUEUE_CAP;
    q->count--;

    pthread_cond_signal(&q->slot_avail);
    pthread_mutex_unlock(&q->lock);
    return 1;

}


/* ---------------------------------------------------------------------
 * queue_shutdown - tell everyone to stop.
 *
 * Called from main.c once dispatcher_run() returns. Idempotent: calling
 * it twice must be harmless.
 *
 * TODO:
 *   1. lock q->lock
 *   2. q->shutdown = 1
 *   3. pthread_cond_broadcast(&q->item_avail)
 *      pthread_cond_broadcast(&q->slot_avail)
 *
 *      BROADCAST, not signal. Every blocked thread has to wake up and
 *      notice the flag. cond_signal wakes exactly one, so the other
 *      workers would sleep forever and pthread_join would never return.
 *
 *   4. unlock
 * ------------------------------------------------------------------ */
void queue_shutdown(queue_t *q)
{
    (void)q;    /* TODO: remove once implemented */
}


/* ---------------------------------------------------------------------
 * queue_depth - current number of queued messages, for `logctl status`.
 *
 * Exists so control.c can report depth without reaching into q->count
 * directly - an unlocked read of that field is a data race, and Helgrind
 * will flag it in Week 3.
 *
 * TODO:
 *   1. lock, read count into a local, unlock, return the local
 * ------------------------------------------------------------------ */
size_t queue_depth(queue_t *q)
{
    (void)q;    /* TODO: remove once implemented */
    return 0;
}
