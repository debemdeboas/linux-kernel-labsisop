/*
 * elevator sstf
 */
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/slab.h>	
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/kernel.h>

#define FORWARDS 1
#define BACKWARDS 0

struct sstf_data {
	struct list_head queue;
	sector_t last_sector;
	unsigned char direction;
};

static void sstf_merged_requests(struct request_queue *q, struct request *rq,
				 struct request *next)
{
	list_del_init(&next->queuelist);
}

static int sstf_dispatch(struct request_queue *q, int force)
{
	struct request *req, *req_next, *req_prev;
	struct sstf_data *sd = q->elevator->elevator_data;

	sector_t delta_prev, delta_next;

	if (!list_empty(&sd->queue)) {
		req_next = list_entry(sd->queue.next, struct request, queuelist);
		req_prev = list_entry(sd->queue.prev, struct request, queuelist);

		if (req_next == req_prev) {
			req = req_next;
		} else {
			delta_prev = abs(blk_rq_pos(req_prev) - sd->last_sector);
			delta_next = abs(blk_rq_pos(req_next) - sd->last_sector);

			if (delta_prev > delta_next) {
				sd->direction = FORWARDS;
				req = req_next;
			} else if (delta_prev == delta_next) {
				if (sd->direction == FORWARDS) {
					req = req_next;
				} else {
					req = req_next;
				}
			} else {
				sd->direction = BACKWARDS;
				req = req_prev;
			}
		}

		list_del_init(&req->queuelist);
		sd->last_sector = blk_rq_pos(req);

		elv_dispatch_add_tail(q, req);

		printk("[SSTF][sstf_dispatch] %llu", sd->last_sector);
		return 1;
	}
	return 0;
}

static void sstf_add_request(struct request_queue *q, struct request *rq)
{
	sector_t rq_sector;
	struct list_head *ptr;
	struct request *req_aux;
	struct sstf_data *sd = q->elevator->elevator_data;

	if (list_empty(&sd->queue)){
		list_add(&rq->queuelist, &sd->queue);
	} else {
		rq_sector = blk_rq_pos(rq);
		list_for_each(ptr, &sd->queue) {
			req_aux = list_entry(ptr, struct request, queuelist);
			if (blk_rq_pos(req_aux) < rq_sector) {
				list_add_tail(&rq->queuelist, ptr);
				return;
			}
		}
		list_add_tail(&rq->queuelist, &sd->queue);
		// printk("[SSTF][sstf_add_request] Added queued request for sector %llu", blk_rq_pos(rq));
	}
	// printk("[SSTF][sstf_add_request] %llu", blk_rq_pos(rq));
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
	sd->direction = FORWARDS;
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

static struct request *sstf_former_request(struct request_queue *q, struct request *rq)
{
	struct sstf_data *nd = q->elevator->elevator_data;

	if (rq->queuelist.prev == &nd->queue)
		return NULL;
	return list_entry(rq->queuelist.prev, struct request, queuelist);
}

static struct request *sstf_latter_request(struct request_queue *q, struct request *rq)
{
	struct sstf_data *nd = q->elevator->elevator_data;

	if (rq->queuelist.next == &nd->queue)
		return NULL;
	return list_entry(rq->queuelist.next, struct request, queuelist);
}

static struct elevator_type elevator_sstf = {
	.ops.sq = {
		.elevator_merge_req_fn	 = sstf_merged_requests,
		.elevator_dispatch_fn	 = sstf_dispatch,
		.elevator_add_req_fn	 = sstf_add_request,
		.elevator_init_fn		 = sstf_init_queue,
		.elevator_exit_fn		 = sstf_exit_queue,
		.elevator_former_req_fn	 = sstf_former_request,
		.elevator_latter_req_fn	 = sstf_latter_request,
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
