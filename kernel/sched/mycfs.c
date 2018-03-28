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
init_mycfs_rq(struct mycfs_rq *Mycfs_rq, struct rq *parent)
{	
	Mycfs_rq->tasks_timeline = RB_ROOT;
	Mycfs_rq->tasks_timeline = RB_ROOT;
	Mycfs_rq->min_vruntime = (u64)(-(1LL << 20));
	Mycfs_rq->rq = parent;
#ifndef CONFIG_64BIT
	Mycfs_rq->min_vruntime_copy = Mycfs_rq->min_vruntime;
#endif

}

/*
 *
 */
int
mycfs_scheduler_tick(struct rq *rq)
{
	struct mycfs_rq *Mycfs_rq = &rq->mycfs;
	int rc = 0;
	u64 now = Mycfs_rq->rq->clock_task;
	if(now - Mycfs_rq->curr_period_start > CPU_PERIOD)
	{
		++Mycfs_rq->curr_period;
		Mycfs_rq->curr_period_start = now;
		if(Mycfs_rq->skipped_task)
			rc = 1;
	}
	return rc;
}

/*
 *
 */
static inline struct task_struct *
task_of(struct sched_mycfs_entity *myse)
{
	return container_of(myse, struct task_struct, myse);
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
 * Check if a limit has been put on the task
 */
static inline int
is_se_limited(struct sched_mycfs_entity *se, struct mycfs_rq *Mycfs_rq)
{
	struct task_struct *p = task_of(se);
	if(p->mycfs_limit == 0)
		return 0;
	if(se->curr_period_id != Mycfs_rq->curr_period)
		return 0;
	if(se->curr_period_sum_runtime * 100 <= CPU_PERIOD * p->mycfs_limit)
		return 0;
	return 1;
}

/*
 * Helper function to get the next task in the rbtree
 */
static struct sched_mycfs_entity *
mycfs_pick_first_entity(struct mycfs_rq *Mycfs_rq)
{
	struct rb_node *left = Mycfs_rq->rb_leftmost;
	{
		u64 now = Mycfs_rq->rq->clock_task;
		if(now - Mycfs_rq->curr_period_start > CPU_PERIOD) {
			++Mycfs_rq->curr_period;
			Mycfs_rq->curr_period_start = now;
		}
	}
	Mycfs_rq->skipped_task = 0;

	while (left) {
		struct sched_mycfs_entity *se = rb_entry(left, struct sched_mycfs_entity, run_node);
		int is_limited = is_se_limited(se, Mycfs_rq);
		if (!is_limited)
			return se;
		Mycfs_rq->skipped_task = 1;
		left = rb_next(left);
	}

	return NULL;
}

/*
 * Helper to get the default 10ms scheduling latency
 */
static u64
__sched_period(unsigned long nr_running)
{
	u64 period = SCHED_LATENCY;
	return period;
}

/*
 * Helper
 */
static u64
sched_slice(struct mycfs_rq *cfs_rq, struct sched_mycfs_entity *se)
{
	return __sched_period(cfs_rq->nr_running + !se->on_rq);
}

/*
 * Helper to get max_vruntime
 */
static inline u64
max_vruntime(u64 min_vruntime, u64 vruntime)
{
	s64 delta = (s64)(vruntime - min_vruntime);
	if (delta > 0)
		min_vruntime = vruntime;
	
	return min_vruntime;
}

/*
 * Helper to get min_vruntime
 */
static inline u64
min_vruntime(u64 min_vruntime, u64 vruntime)
{
	s64 delta = (s64)(vruntime - min_vruntime);
	if (delta < 0)
		min_vruntime = vruntime;
	
	return min_vruntime;
}

/*
 * Helper to get vruntime difference of two SEs
 */
static inline int
entity_before(struct sched_mycfs_entity *sea, struct sched_mycfs_entity *seb)
{
	return (s64)(sea->vruntime - seb->vruntime) < 0;
}

/*
 * Perform the heavy lifting of enqueue
 */
static void 
__enqueue_entity(struct mycfs_rq *Mycfs_rq, struct sched_mycfs_entity *se)
{
	struct rb_node **link = &Mycfs_rq->tasks_timeline.rb_node;
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
		Mycfs_rq->rb_leftmost = &se->run_node;

	rb_link_node(&se->run_node, parent, link);
	rb_insert_color(&se->run_node, &Mycfs_rq->tasks_timeline);
}

/*
 * Perform the heavy lifting of dequeue
 */
static void
__dequeue_entity(struct mycfs_rq *Mycfs_rq, struct sched_mycfs_entity *se)
{
	if (Mycfs_rq->rb_leftmost == &se->run_node){
		struct rb_node *next_node;
		next_node = rb_next(&se->run_node);
		Mycfs_rq->rb_leftmost = next_node;
	}

	rb_erase(&se->run_node, &Mycfs_rq->tasks_timeline);
}

/*
 * Update the min vruntime for the mycfs rq
 */
static void
mycfs_update_min_vruntime(struct mycfs_rq *Mycfs_rq)
{
	u64 vruntime = Mycfs_rq->min_vruntime;
	
	// Is this the rq we are working on?
	if (Mycfs_rq->curr)
		vruntime = Mycfs_rq->curr->vruntime;

	if (Mycfs_rq->rb_leftmost){
		struct rb_node *node = Mycfs_rq->rb_leftmost;
		while (node){
			struct sched_mycfs_entity *se = rb_entry(node, struct sched_mycfs_entity, run_node);

			if (!Mycfs_rq->curr)
				vruntime = se->vruntime;
			else
				vruntime = min_vruntime(vruntime, se->vruntime);
			break;
		}
	}

	Mycfs_rq->min_vruntime = max_vruntime(Mycfs_rq->min_vruntime, vruntime);
	{
		struct rb_node *node = Mycfs_rq->rb_leftmost;
		while(node) {
			struct sched_mycfs_entity *se = rb_entry(node, struct sched_mycfs_entity, run_node);

			if (is_se_limited(se, Mycfs_rq)) 
				se->vruntime = max_vruntime(se->vruntime, Mycfs_rq->min_vruntime);
			node = rb_next(node);
		}
	}

#ifndef CONFIG_64BIT
	smp_wmb();
	Mycfs_rq->min_vruntime_copy = Mycfs_rq->min_vruntime;
#endif
}

/*
 * Performs heacy lifting of updating periods
 */
static inline int
__update_curr_mycfs(struct mycfs_rq *Mycfs_rq, struct sched_mycfs_entity *curr, unsigned long delta_exec, u64 now)
{
	curr->vruntime += delta_exec;
	mycfs_update_min_vruntime(Mycfs_rq);

	if (now - Mycfs_rq->curr_period_start > CPU_PERIOD){
		++Mycfs_rq->curr_period;
		Mycfs_rq->curr_period_start = now - delta_exec;
	}

	if (curr->curr_period_id != Mycfs_rq->curr_period){
		curr->curr_period_id = Mycfs_rq->curr_period;
		curr->curr_period_sum_runtime = delta_exec;
	}
	else {
		curr->curr_period_sum_runtime += delta_exec;
	}

	return is_se_limited(curr, Mycfs_rq);
}

/*
 * Update curr for the mycfs scheduler (calls __update_curr_mycfs)
 */
static int
mycfs_update_curr(struct mycfs_rq *Mycfs_rq)
{
	struct sched_mycfs_entity *curr = Mycfs_rq->curr;
	u64 now = Mycfs_rq->rq->clock_task;
	unsigned long delta_exec;
	int rc;

	if (!curr)
		return 0;
	
	// The change in exec time is just the current time - starting time
	delta_exec = (unsigned long)(now - curr->exec_start);
	if (!delta_exec)
		return 0;

	rc = __update_curr_mycfs(Mycfs_rq, curr, delta_exec, now);
	curr->exec_start = now;

	{
		struct task_struct *curtask = task_of(curr);
		cpuacct_charge(curtask, delta_exec);
		account_group_exec_runtime(curtask, delta_exec);
	}
	return rc;
}


/*
 * Call when a task enters a runnable state
 * Puts task in runqueue and incrememnts nr_running
 */
static void
enqueue_task_mycfs(struct rq *rq, struct task_struct *p, int flags)
{
	struct sched_mycfs_entity *se = &p->myse;
	struct mycfs_rq *Mycfs_rq = mycfs_rq_of(se);

	if (se->on_rq)
		return;
	
	if (!(flags & ENQUEUE_WAKEUP) || (flags & ENQUEUE_WAKING))
		se->vruntime += Mycfs_rq->min_vruntime;

	mycfs_update_curr(Mycfs_rq);

	if (flags & ENQUEUE_WAKEUP) 
	{
		u64 vruntime = Mycfs_rq->min_vruntime;
		vruntime = max_vruntime(se->vruntime, vruntime);
		se->vruntime = vruntime;
	}

	if (se != Mycfs_rq->curr)
		__enqueue_entity(Mycfs_rq, se);
	
	se->on_rq = 1;
	inc_nr_running(rq);
	++Mycfs_rq->nr_running;
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
	struct mycfs_rq *Mycfs_rq = mycfs_rq_of(se);

	mycfs_update_curr(Mycfs_rq);

	if (se != Mycfs_rq->curr)
		__dequeue_entity(Mycfs_rq, se);
	
	se->on_rq = 0;
	--Mycfs_rq->nr_running;
	dec_nr_running(rq);

	if (!(flags & DEQUEUE_SLEEP))
		se->vruntime -= Mycfs_rq->min_vruntime;

	mycfs_update_min_vruntime(Mycfs_rq);
}

/*
 * Called to voluntarily give up CPU, but no go out of runnable
 * state (no nr_running change)
 */
static void
yield_task_mycfs(struct rq *rq)
{
	struct mycfs_rq *Mycfs_rq = &rq->mycfs;

	// Nothing to yield to
	if (rq->nr_running == 1)
		return;
	
	update_rq_clock(rq);
	mycfs_update_curr(Mycfs_rq);
	rq->skip_clock_update = 1;
}

static int
wakeup_preempt_entity(struct mycfs_rq *Mycfs_rq, struct sched_mycfs_entity *curr, struct sched_mycfs_entity *se)
{
	s64 gran, vdiff = curr->vruntime - se->vruntime;
	if (is_se_limited(curr, Mycfs_rq) && !is_se_limited(se, Mycfs_rq))
		return 1;

	if (vdiff <= 0)
		return -1;
	
	gran = sysctl_sched_wakeup_granularity;
	if (vdiff > gran)
		return 0;
	
	return 0;
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
 * Helper function to perform a dequeue and set curr as se
 */
static inline void
mycfs_set_next_entity(struct mycfs_rq *Mycfs_rq, struct sched_mycfs_entity *se)
{
	if (se->on_rq)
		__dequeue_entity(Mycfs_rq, se);
	
	Mycfs_rq->curr = se;
	se->exec_start = Mycfs_rq->rq->clock_task;
}

/*
 * Choose the most appropriate task eligible to run next
 */
static struct task_struct *
pick_next_task_mycfs(struct rq *rq)
{
	struct mycfs_rq *Mycfs_rq = &rq->mycfs;
	struct sched_mycfs_entity *se;

	// You can't pick a task is there are none to choose from
	if (!Mycfs_rq->nr_running)
		return NULL;
	
	// Pick the first entry
	se = mycfs_pick_first_entity(Mycfs_rq);
	if(!se)
		return NULL;
	
	mycfs_set_next_entity(Mycfs_rq, se);

	return task_of(se);
}

/*
 * Update curr and enqueue 
 */
static void
put_prev_task_mycfs(struct rq *rq, struct task_struct *prev)
{
	struct sched_mycfs_entity *se = &prev->myse;
	struct mycfs_rq *Mycfs_rq = mycfs_rq_of(se);

	if (se->on_rq){
		mycfs_update_curr(Mycfs_rq);
		__enqueue_entity(Mycfs_rq, se);
	}

	Mycfs_rq->curr = NULL;
}

#ifdef CONFIG_SMP
/*
 *
 */
static int
select_task_rq_mycfs(struct task_struct *p, int sd_flag, int wake_flags)
{
	return task_cpu(p);
}

/*
 *
 */
static void
rq_online_mycfs(struct rq *rq)
{
	// IGNORE
}

/*
 *
 */
static void
rq_offline_mycfs(struct rq *rq)
{
	// IGNORE
}

/*
 * Wakeup task in mycfs
 */
static void
task_waking_mycfs(struct task_struct *task)
{
	struct sched_mycfs_entity *se = &task->myse;
	struct mycfs_rq *Mycfs_rq = mycfs_rq_of(se);
	u64 min_vruntime;

#ifndef CONFIG_64BIT
	u64 min_vruntime_copy;

	do {
		min_vruntime_copy = Mycfs_rq->min_vruntime_copy;
		smp_rmb();
		min_vruntime = Mycfs_rq->min_vruntime;
	} while (min_vruntime != min_vruntime_copy);
#else
	min_vruntime = Mycfs_rq->min_vruntime;
#endif
	se->vruntime -= min_vruntime;
}
#endif

/*
 * 
 */
static void
set_curr_task_mycfs(struct rq *rq)
{
	struct sched_mycfs_entity *se = &rq->curr->myse;
	struct mycfs_rq *Mycfs_rq = mycfs_rq_of(se);

	mycfs_set_next_entity(Mycfs_rq, se);
}

/*
 * Check the time tick for preemption
 */
static void
mycfs_check_preempt_tick(struct mycfs_rq *Mycfs_rq, struct sched_mycfs_entity *curr)
{
	unsigned long ideal_runtime, delta_exec;
	struct sched_mycfs_entity *se;
	u64 now = Mycfs_rq->rq->clock_task;
	s64 delta;

	ideal_runtime = sched_slice(Mycfs_rq, curr);

	delta_exec = (unsigned long)(now - curr->exec_start);
	if (delta_exec > ideal_runtime) {
		resched_task(Mycfs_rq->rq->curr);
		return;
	}

	if (delta_exec < SCHED_LATENCY)
		return;

	se = mycfs_pick_first_entity(Mycfs_rq);
	delta = curr->vruntime - se->vruntime;

	if (delta < 0)
		return;

	if (delta > ideal_runtime)
		resched_task(Mycfs_rq->rq->curr);
}

/*
 * Called by time tick functions. May lead to process switch
 * This drives the running preemption
 */
static void
task_tick_mycfs(struct rq *rq, struct task_struct *curr, int queued)
{
	struct sched_mycfs_entity *se = &curr->myse;
	struct mycfs_rq *Mycfs_rq = mycfs_rq_of(se);
	int need_resched;
	
	need_resched = mycfs_update_curr(Mycfs_rq);
	
	if (need_resched) {
		resched_task(curr);
		return;
	}

	if (Mycfs_rq->nr_running > 1){
		mycfs_check_preempt_tick(Mycfs_rq, se);
	}
}

/*
 * Notify the scheduler if a new task has spawned
 * Do tasks necessary to forking said task
 */
static void
task_fork_mycfs(struct task_struct *p)
{
	struct mycfs_rq *Mycfs_rq;
	struct sched_mycfs_entity *se = &p->myse, *curr;
	int this_cpu = smp_processor_id();
	struct rq *rq = this_rq();
	unsigned long flags;
	int need_resched;

	raw_spin_lock_irqsave(&rq->lock, flags);

	update_rq_clock(rq);

	Mycfs_rq = &rq->mycfs;
	curr = Mycfs_rq->curr;

	if (unlikely(task_cpu(p) != this_cpu)){
		rcu_read_lock();
		__set_task_cpu(p, this_cpu);
		rcu_read_unlock();
	}

	need_resched = mycfs_update_curr(Mycfs_rq);

	if (curr)
		se->vruntime = curr->vruntime;
	
	{
		u64 vruntime = Mycfs_rq->min_vruntime;
		vruntime = max_vruntime(se->vruntime, vruntime);
		se->vruntime = vruntime;
	}

	if (need_resched && curr && entity_before(curr, se)){
		swap(curr->vruntime, se->vruntime);
		resched_task(rq->curr);
	}

	se->vruntime -= Mycfs_rq->min_vruntime;
	raw_spin_unlock_irqrestore(&rq->lock, flags);
}

/*
 *
 */
static void
prio_changed_mycfs(struct rq *rq, struct task_struct *p, int oldprio)
{
	//IGNORE
}

/*
 *
 */
static void
switched_from_mycfs(struct rq *rq, struct task_struct *p)
{
	struct sched_mycfs_entity *se = &p->myse;
	struct mycfs_rq *Mycfs_rq = mycfs_rq_of(se);

	if (!se->on_rq && p->state != TASK_RUNNING) {
		{
			u64 vruntime = Mycfs_rq->min_vruntime;
			vruntime = max_vruntime(se->vruntime, vruntime);
			se->vruntime = vruntime;
		}
		se->vruntime -= Mycfs_rq->min_vruntime;
	}
}

/*
 *
 */
static void
switched_to_mycfs(struct rq *rq, struct task_struct *p)
{
	p->mycfs_limit = 0;
	if (rq->curr == p)
		resched_task(rq->curr);
}

/*
 *
 */
static unsigned int
get_rr_interval_mycfs(struct rq *rq, struct task_struct *task)
{
	return NS_TO_JIFFIES(SCHED_LATENCY);
}

const struct sched_class mycfs_sched_class = {
	.next				= &idle_sched_class,
	.enqueue_task		= enqueue_task_mycfs,
	.dequeue_task		= dequeue_task_mycfs,
	.yield_task			= yield_task_mycfs,
	
	.check_preempt_curr	= check_preempt_wakeup,
	
	.pick_next_task		= pick_next_task_mycfs,
	.put_prev_task		= put_prev_task_mycfs,

#ifdef CONFIG_SMP
	.select_task_rq		= select_task_rq_mycfs,
	
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
};

/*
 * Init function
 */
__init void
init_sched_mycfs_class(void)
{

}
