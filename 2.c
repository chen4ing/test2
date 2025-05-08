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
static int __dm_thread_cmp(struct thread *a, struct thread *b)
{
    if (!a->is_real_time || !b->is_real_time) {
        return a->is_real_time ? -1 : (b->is_real_time ? 1 : 0);
    }
    if (a->deadline != b->deadline) {
        return (a->deadline < b->deadline) ? -1 : 1;
    }
    return (a->ID < b->ID) ? -1 : 1;
}


struct threads_sched_result schedule_dm(struct threads_sched_args args)
{
    struct threads_sched_result r;
    struct thread *th;
    struct thread *selected = NULL;
    struct thread *missed = NULL;

    // 步驟 1：先找有沒有人已經 miss deadline
    list_for_each_entry(th, args.run_queue, thread_list) {
        if (th->is_real_time && th->remaining_time > 0 &&
            args.current_time > th->current_deadline) {

            if (missed == NULL || th->ID < missed->ID) {
                missed = th;
            }
        }
    }

    // 若有人 miss deadline，立刻排他（allocated_time = 0）
    if (missed) {
        r.scheduled_thread_list_member = &missed->thread_list;
        r.allocated_time = 0;
        return r;
    }

    // 步驟 2：從 run_queue 中挑選 deadline 最小者（DM）
    list_for_each_entry(th, args.run_queue, thread_list) {
        if (th->is_real_time && th->remaining_time > 0) {
            if (!selected || __dm_thread_cmp(th, selected) < 0) {
                selected = th;
            }
        }
    }

    // 步驟 3：正常排程執行
    if (selected) {
        r.scheduled_thread_list_member = &selected->thread_list;
        r.allocated_time = selected->remaining_time;  // 一次跑完，不分片
    } else {
        // fallback：無 real-time thread 可跑
        r.scheduled_thread_list_member = NULL;
        r.allocated_time = 1;
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
