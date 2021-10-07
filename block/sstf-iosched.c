/*
 * elevator sstf
 */
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/slab.h>	
#include <linux/init.h>

struct sstf_data {
	struct list_head queue;
	sector_t last_sector;
};

static unsigned int local_abs(int val) {
	return val * ((val > 0) - (val < 0));
}

static void sstf_merged_requests(struct request_queue *q, struct request *rq,
				 struct request *next)
{
	list_del_init(&next->queuelist);
}

static int sstf_dispatch(struct request_queue *q, int force)
{
	struct sstf_data *sd = q->elevator->elevator_data;
	struct request *req, *req_next, *req_prev;

	if (list_empty(&sd->queue)) {
		return 0;
	}

	req_next = list_entry(sd->queue.next, struct request, queuelist);
	req_prev = list_entry(sd->queue.prev, struct request, queuelist);

	if (req_next == req_prev) {
		req = req_next;
	} else {
		int delta_prev = local_abs(blk_rq_pos(req_prev) - sd->last_sector);
		int delta_next = local_abs(blk_rq_pos(req_next) - sd->last_sector);

		if (delta_prev < delta_next) {
			req = req_prev;
		} else {
			req = req_next;
		}
	}

	list_del_init(&req->queuelist);
	sd->last_sector = blk_rq_pos(req)/* + blk_rq_sectors(req)*/;

	elv_dispatch_add_tail(q, req);

	printk("[SSTF][sstf_dispatch] Sector %llu has been traversed. Queue size %d", sd->last_sector, sd->queue);
	return 1;
}

static void sstf_add_request(struct request_queue *q, struct request *rq)
{
	struct sstf_data *sd = q->elevator->elevator_data;
	struct request *req_next, *req_prev;
	
	if (list_empty(&sd->queue)) {
		list_add(&rq->queuelist, &sd->queue);
		printk("[SSTF][sstf_add_request] EMPTY QUEUE : Request added for sector %llu", blk_rq_pos(rq));
	} else {
		sector_t next_pos;
		do {
			req_next = list_entry(sd->queue.next, struct request, queuelist);
			req_prev = list_entry(sd->queue.prev, struct request, queuelist);
			next_pos = blk_rq_pos(req_next);
		} while (blk_rq_pos(rq) > next_pos);

		__list_add(&rq->queuelist, &req_prev->queuelist, &req_next->queuelist);
		printk("[SSTF][sstf_add_request] Request added for sector %llu", blk_rq_pos(rq));
	}

	// printk("[SSTF][sstf_add_request] Request added for sector %llu", blk_rq_pos(rq));
}

static int sstf_init_queue(struct request_queue *q, struct elevator_type *e)
{
	struct sstf_data *sd;
	struct elevator_queue *eq;

	eq = elevator_alloc(q, e);
	if (!eq)
		return -ENOMEM;

	sd = kmalloc_node(sizeof(*sd), GFP_KERNEL, q->node);
	if (!sd) {
		kobject_put(&eq->kobj);
		return -ENOMEM;
	}
	sd->last_sector = 0;
	eq->elevator_data = sd;

	INIT_LIST_HEAD(&sd->queue);

	spin_lock_irq(q->queue_lock);
	q->elevator = eq;
	spin_unlock_irq(q->queue_lock);
	return 0;
}

static void sstf_exit_queue(struct elevator_queue *e)
{
	struct sstf_data *sd = e->elevator_data;

	BUG_ON(!list_empty(&sd->queue));
	kfree(sd);
}

static struct elevator_type elevator_sstf = {
	.ops.sq = {
		.elevator_merge_req_fn	= sstf_merged_requests,
		.elevator_dispatch_fn	= sstf_dispatch,
		.elevator_add_req_fn	= sstf_add_request,
		.elevator_init_fn		= sstf_init_queue,
		.elevator_exit_fn		= sstf_exit_queue,
	},
	.elevator_name = "sstf",
	.elevator_owner = THIS_MODULE,
};

static int __init sstf_init(void)
{
	return elv_register(&elevator_sstf);
}

static void __exit sstf_exit(void)
{
	elv_unregister(&elevator_sstf);
}

module_init(sstf_init);
module_exit(sstf_exit);


MODULE_AUTHOR("Rafael Almeida de Bem");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Shortest Seek Time First IO scheduler");
