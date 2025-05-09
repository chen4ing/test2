#include "kernel/types.h"
#include "user/user.h"
#include "user/list.h"
#include "user/threads.h"
#include "user/threads_sched.h"
#include <limits.h>
#define NULL 0

/* default scheduling algorithm */
#ifdef THREAD_SCHEDULER_DEFAULT
struct threads_sched_result schedule_default(struct threads_sched_args args)
{
    struct thread *thread_with_smallest_id = NULL;
    struct thread *th = NULL;
    list_for_each_entry(th, args.run_queue, thread_list) {
        if (thread_with_smallest_id == NULL || th->ID < thread_with_smallest_id->ID)
            thread_with_smallest_id = th;
    }

    struct threads_sched_result r;
    if (thread_with_smallest_id != NULL) {
        r.scheduled_thread_list_member = &thread_with_smallest_id->thread_list;
        r.allocated_time = thread_with_smallest_id->remaining_time;
    } else {
        r.scheduled_thread_list_member = args.run_queue;
        r.allocated_time = 1;
    }

    return r;
}
#endif

/* MP3 Part 1 - Non-Real-Time Scheduling */

// HRRN
#ifdef THREAD_SCHEDULER_HRRN
struct threads_sched_result schedule_hrrn(struct threads_sched_args args)
{
    struct threads_sched_result r;
    struct thread *selected = NULL;
    struct thread *th = NULL;

    int best_numerator = -1;
    int best_denominator = 1;

    list_for_each_entry(th, args.run_queue, thread_list) {
        int waiting_time = args.current_time - th->arrival_time;
        int burst = th->processing_time;

        int numerator = waiting_time + burst;
        int rr_left = numerator * best_denominator;
        int rr_right = best_numerator * burst;

        if (selected == NULL || rr_left > rr_right ||
            (rr_left == rr_right && th->ID < selected->ID)) {
            selected = th;
            best_numerator = numerator;
            best_denominator = burst;
        }
    }

    if (selected) {
        r.scheduled_thread_list_member = &selected->thread_list;
        r.allocated_time = selected->remaining_time;
    } else {
        r.scheduled_thread_list_member = args.run_queue;
        r.allocated_time = 1;
    }

    return r;
}
#endif

#ifdef THREAD_SCHEDULER_PRIORITY_RR
struct threads_sched_result schedule_priority_rr(struct threads_sched_args args)
{
    struct threads_sched_result r;
    struct thread *th;
    struct thread *selected = NULL;

    int highest_priority = 1 << 30;
    int count_in_group = 0;

    // 先找出最高優先權（priority 越小越高）
    list_for_each_entry(th, args.run_queue, thread_list) {
        if (th->priority < highest_priority) {
            highest_priority = th->priority;
        }
    }

    // 第二次遍歷：找出該 priority group 中第一個符合條件的 thread（RR）並計數
    list_for_each_entry(th, args.run_queue, thread_list) {
        if (th->priority == highest_priority) {
            if (selected == NULL) {
                selected = th;
            }
            count_in_group++;
        }
    }

    // 決定分配時間：如果只有一個 thread，直接分配全部 remaining_time
    if (selected) {
        r.scheduled_thread_list_member = &selected->thread_list;

        if (count_in_group == 1) {
            r.allocated_time = selected->remaining_time;
        } else {
            r.allocated_time = (selected->remaining_time <= args.time_quantum)
                               ? selected->remaining_time
                               : args.time_quantum;
        }
    } else {
        r.scheduled_thread_list_member = NULL;
        r.allocated_time = 1;
    }

    return r;
}
#endif


/* MP3 Part 2 - Real-Time Scheduling*/

#if defined(THREAD_SCHEDULER_EDF_CBS) || defined(THREAD_SCHEDULER_DM)
static struct thread *__check_deadline_miss(struct list_head *run_queue, int current_time)
{
    struct thread *th = NULL;
    struct thread *thread_missing_deadline = NULL;
    list_for_each_entry(th, run_queue, thread_list) {
        if (th->current_deadline <= current_time) {
            if (thread_missing_deadline == NULL)
                thread_missing_deadline = th;
            else if (th->ID < thread_missing_deadline->ID)
                thread_missing_deadline = th;
        }
    }
    return thread_missing_deadline;
}
#endif

#ifdef THREAD_SCHEDULER_DM
/* Deadline-Monotonic Scheduling */
static int __dm_thread_cmp(struct thread *a, struct thread *b) {
    // If one thread is real-time and the other is not, the real-time thread comes first
    if (a->is_real_time && !b->is_real_time) {
        return -1;
    }
    if (!a->is_real_time && b->is_real_time) {
        return 1;
    }
    // If both are real-time, compare their relative deadlines (smaller deadline = higher priority)
    if (a->is_real_time && b->is_real_time) {
        if (a->deadline != b->deadline) {
            return (a->deadline < b->deadline) ? -1 : 1;
        }
    }
    // For threads with equal deadlines or both non-real-time, break tie by ID (smaller ID = higher priority)
    if (a->ID != b->ID) {
        return (a->ID < b->ID) ? -1 : 1;
    }
    return 0;
}

/* Deadline-Monotonic Scheduling: Scheduler function */
struct threads_sched_result schedule_dm(struct threads_sched_args args) {
    struct threads_sched_result r;
    struct thread *th;
    struct thread *selected = NULL;

    // Step 1: Check if any real-time thread has missed its deadline
    struct thread *missed = __check_deadline_miss(args.run_queue, args.current_time);
    if (missed && missed->is_real_time && missed->remaining_time > 0) {
        // Missed deadline: dispatch this thread with 0 time to signal a deadline miss
        r.scheduled_thread_list_member = &missed->thread_list;
        r.allocated_time = 0;
        return r;
    }

    // Step 2: Find the real-time thread (with remaining execution time) that has the earliest deadline
    list_for_each_entry(th, args.run_queue, thread_list) {
        if (th->remaining_time > 0) {
            if (selected == NULL || __dm_thread_cmp(th, selected) < 0) {
                selected = th;
            }
        }
    }

    // Step 3: Determine scheduling result
    if (selected != NULL) {
        // A thread is selected to run
        r.scheduled_thread_list_member = &selected->thread_list;
        if (!list_empty(args.release_queue)) {
            // Calculate next release time to support preemption at task arrivals
            int next_release_time = INT_MAX;
            struct release_queue_entry *entry;
            list_for_each_entry(entry, args.release_queue, thread_list) {
                if (entry->release_time < next_release_time) {
                    next_release_time = entry->release_time;
                }
            }
            if (next_release_time <= args.current_time) {
                // If a release is due now or overdue, limit to 1 tick to trigger release
                r.allocated_time = 1;
            } else {
                int time_until_next_release = next_release_time - args.current_time;
                // Allocate only up to the next release time to allow preemption at that tick
                r.allocated_time = (selected->remaining_time > time_until_next_release) 
                                     ? time_until_next_release 
                                     : selected->remaining_time;
            }
        } else {
            // No upcoming releases; allocate all remaining execution time to finish the thread’s current cycle
            r.allocated_time = selected->remaining_time;
        }
    } else {
        // No thread available to run
        r.scheduled_thread_list_member = args.run_queue;  // Use run_queue sentinel to indicate idle
        if (!list_empty(args.release_queue)) {
            // Idle until the next thread release if one is scheduled
            int next_release_time = INT_MAX;
            struct release_queue_entry *entry;
            list_for_each_entry(entry, args.release_queue, thread_list) {
                if (entry->release_time < next_release_time) {
                    next_release_time = entry->release_time;
                }
            }
            int sleep_ticks = next_release_time - args.current_time;
            r.allocated_time = (sleep_ticks > 0) ? sleep_ticks : 1;
        } else {
            // No threads at all; idle for 1 tick
            r.allocated_time = 1;
        }
    }

    return r;
}


#endif


#ifdef THREAD_SCHEDULER_EDF_CBS
// EDF with CBS comparation
static int __edf_thread_cmp(struct thread *a, struct thread *b)
{
    // TO DO
}
//  EDF_CBS scheduler
struct threads_sched_result schedule_edf_cbs(struct threads_sched_args args)
{
    struct threads_sched_result r;

    // notify the throttle task
    // TO DO

    // first check if there is any thread has missed its current deadline
    // TO DO

    // handle the case where run queue is empty
    // TO DO

    return r;
}
#endif
