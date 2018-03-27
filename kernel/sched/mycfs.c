/*
 * A simple version of the CFS scheduler
 */

#include "sched.h"
#include <linux/sched.h>
#include <linux/syscalls.h>

#define SCHED_LATENCY 10000000 /* 10ms in ns */
#define CPU_PERIOD 100000000LL /* 100ms in ns */

/*
 * Initialize mycfs rq in the same spirit as the cfs rq
 */
void 
init_mycfs_rq(struct mycfs_rq *mycfs_rq, struct rq *parent)
{
	mycfs_rq->tasks_timeline = RB_ROOT;
	mycfs_rq->min_vruntime = (u64)(-(1LL << 20));
	mycfs_rq->rq = parent;
#ifndef CONFIG_64BIT
	mycfs->min_vruntime_copy = mycfs_rq->min_vruntime;
#endif

}

/*
 * Perform the heavy lifting of enqueue
 */
static void 
__enqueue_entitiy(struct mycfs_rq * mycfs_rq, struct sched_mycfs_entity *se)
{
	struct rb_node **link = &mycfs_rq->tasks_timeline.rb_node;
	struct rb_node *parent = NULL;
	struct sched_mycfs_entity *entry;
	int leftmost = 1;

	/* Find the correct location in the rbtree */
	while (*link){
		parent = *link;
		// Each entry is a mycfs se)
		entry = rb_entry(parent, struct sched_mycfs_entity, run_node);
		if (entity_before(se, entry)){
			link = &parent->rb_left;
		}
		else {
			link = &parent->rb_right;
			leftmost = 0;
		}
	}

	// Cache of entries run the least ammount of time
	if (leftmost)
		cfs_rq->rb_leftmost = &se->run_node;

	rb_link_node(&se->run_node, parent, link);
	rb_insert_color(&se->run_node, &cfs_rq->tasks_timeline);
}

/*
 * Perform the heavy lifting of dequeue
 */
static void
__dequeue_entity(struct mycfs_rq *mycfs_rq, struct sched_mycfs_entity *se)
{
	if (mycfs_rq->rb_leftmost == &se->run_node){
		struct rb_node *next_node;
		next_node = rb_next(&se->run_node);
		mycfs_rq->rb_leftmost = next_node;
	}

	rb_erase(&se->run_node, &mycfs_rq->tasks_timeline)
}

/*
 * Helper to get rq of se
 */
static inline struct mycfs_rq *
mycfs_rq_of(struct sched_mycfs_entity *myse)
{
	struct task_struct *p = task_of(myse);
	struct rq *rq = task_rq(p);
	return &rq->mycfs;
}

/*
 * Call when a task enters a runnable state
 * Puts task in runqueue and incrememnts nr_running
 */
static void
enqueue_task_mycfs(struct rq *rq, struct task_struct *p, int flags)
{
	struct sched_mycfs_entity *se = &p->myse;
	struct mycfs_rq *mycfs_rq = mycfs_rq_of(se);

	if (se->on_rq)
		return;
	
	if (!(flags & ENQUEUE_WAKEUP) || (flags & ENQUEUE_WAKING))
		se->vruntime += mycfs_rq->min_vruntime;

	mycfs_update_curr(mycfs_rq);

	if (flags & ENQUEUE_WAKEUP) 
	{
		u64 vruntime = mycfs_rq->min_vruntime;
		vruntime = max_vruntime(se->vruntime, vruntime);
		se->vruntime = vruntime;
	}

	if (se != myfs_rq->curr)
		__enqueue_entity(mycfs_rq, se);
	
	se->on_rq = 1;
	inc_nr_running(rq);
	++mycfs_rq->nr_running;
}

}

/*
 * Call when task is no longer runnable
 * Called to keep corresponding scheduling entity out of runqueue
 * Decrements nr_running
 */
static void
dequeue_task_mycfs(struct rq *rq, struct task_struct *p, int flags)
{
	struct sched_mycfs_entity *se = &p->myse;
	struct mycfs_rq *cfs_rq = mycfs_rq_of(se);

	mycfs_update_curr(cfs_rq);

	if (se != cfs_rq->curr)
		__dequeue_entity(cfs_rq, se);
	
	se->on_rq = 0;
	--cfs_rq->nr_running;
	dec_nr_runnint(rq);

	if (!(flags & DEQUEUE_SLEEP))
		se->vruntime -= cfs_rq->min_vruntime;

	mycfs_update_min_vruntime(cfs_rq);
}

/*
 * Called to voluntarily give up CPU, but no go out of runnable
 * state (no nr_running change)
 */
static void
yield_task_mycfs(struct rq *rq)
{
	struct mycfs_rq *mycfs_rq = &rq->mycfs;

	// Nothing to yield to
	if (rq->nr_running == 1)
		return;
	
	update_rq_clock(rq);
	mycfs_update_curr(mycfs_rq);
	rq->skip_clock_update = 1;
}

/*
 * Check if a task that entered runnable state should preempt
 * the currently running task
 */
static void
check_preempt_wakeup(struct rq *rq, struct task_struct *p, int wake_flags)
{
	struct task_struct *curr = rq->curr;
	struct sched_mycfs_entity *se = &curr->myse, *pse = &p->myse;
	int need_resched;

	if (unlikely(se == pse))
		return;
	
	need_resched = mycfs_update_curr(mycfs_rq_of(se));
	
	if (!need_resched && wakeup_preempt_entity(&rq->mycfs, se, pse) != 1){
		return;
	}
	
	resched_task(curr);
}

/*
 * Choose the most appropriate task eligible to run next
 */
static struct
*pick_next_task_mycfs(struct rq *rq)
{

}

/*
 *
 */
static void
put_prev_task_mycfs(struct rq *rq, struct task_struct *prev)
{

}

#ifdef CONFIG_SMP
/*
 *
 */
static int
select_task_rq_fair(struct task_struct *p, int sd_flag, int wake_flags)
{

}

#ifdef CONFIG_MYCFS_GROUP_SCHED
/*
 *
 */
static void
migrate_task_rq_mycfs(struct task_struct *p, int next_cpu)
{

}
#endif

/*
 *
 */
static void
rq_online_mycfs(struct rq *rq)
{

}

/*
 *
 */
static void
rq_offline_mycfs(struct rq *rq)
{

}

/*
 *
 */
static void
task_waking_mycfs(struct task_struct *p)
{

}
#endif

/*
 *
 */
static void
set_curr_task_mycfs(struct rq *rq)
{

}

/*
 * Called by time tick functions. May lead to process switch
 * This drives the running preemption
 */
static void
task_tick_mycfs(struct rq *rq, struct task_struct *curr, int queued)
{

}

/*
 * Notify the scheduler if a new task has spawned
 */
static void
task_fork_mycfs(struct task_struct *p)
{

}

/*
 *
 */
static void
prio_changed_mycfs(struct rq *rq, struct task_struct *p, int oldprio)
{

}

/*
 *
 */
static void
switched_from_mycfs(struct rq *rq, struct task_struct *p)
{

}

/*
 *
 */
static void
switched_to_mycfs(struct rq *rq, struct task_struct *p)
{

}

/*
 *
 */
static unsigned int
get_rr_interval_mycfs(struct rq *rq, struct task_struct *task)
{

}

#ifdef CONFIG_MYCFS_GROUP_SCHED
/*
 *
 */
static void
task_move_group_mycfs(struct task_struct *p, int on_rq)
{

}
#endif

const struct sched_class mycfs_sched_class = {
	.next				= &idle_sched_class,
	.enqueue_task		= enqueue_task_mycfs,
	.dequeue_task		= dequeue_task_mycfs,
	.yield_task			= yield_task_mycfs,
	.yield_to_task		= yield_to_task_mycfs,
	
	.check_preempt_curr	= check_preempt_wakeup,
	
	.pick_next_task		= pick_next_task_mycfs,
	.put_prev_task		= put_prev_task_mycfs,

#ifdef CONFIG_SMP
	.select_task_rq		= select_task_rq_mycfs,
#ifdef CONFIG_MYCFS_GROUP_SCHED
	.migrate_task_rq	= migrate_task_rq_mycfs,
#endif
	.rq_online			= rq_online_mycfs,
	.rq_offline			= rq_offline_mycfs,
	
	.task_waking		= task_waking_mycfs,
#endif

	.set_curr_task		= set_curr_task_mycfs,
	.task_tick			= task_tick_mycfs,
	.task_fork			= task_fork_mycfs,

	.prio_changed		= prio_changed_mycfs,
	.switched_from		= switched_from_mycfs,
	.switched_to		= switched_to_mycfs,
	
	.get_rr_interval	= get_rr_interval_mycfs,
	
#ifdef CONFIG_MYCFS_GROUP_SCHED
	.task_move_group	= task_move_group_mycfs,
#endif

};

/*
 * Init function
 */
__init void
init_sched_mycfs_class(void)
{

}
