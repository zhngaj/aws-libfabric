/*
 * Copyright (c) 2013 Intel Corporation. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "psmx.h"

void psmx_cntr_check_trigger(struct psmx_fid_cntr *cntr)
{
	struct psmx_trigger *trigger;

	/* TODO: protect the trigger list with mutex */

	if (!cntr->trigger)
		return;

	trigger = cntr->trigger;
	while (trigger) {
		if (cntr->counter < trigger->threshold)
			break;

		cntr->trigger = trigger->next;
		switch (trigger->op) {
		case PSMX_TRIGGERED_SEND:
			_psmx_sendto(trigger->send.ep,
				     trigger->send.buf,
				     trigger->send.len,
				     trigger->send.desc,
				     trigger->send.dest_addr,
				     trigger->send.context,
				     trigger->send.flags);
			break;
		case PSMX_TRIGGERED_RECV:
			_psmx_recvfrom(trigger->recv.ep,
				       trigger->recv.buf,
				       trigger->recv.len,
				       trigger->recv.desc,
				       trigger->recv.src_addr,
				       trigger->recv.context,
				       trigger->recv.flags);
			break;
		case PSMX_TRIGGERED_TSEND:
			_psmx_tagged_sendto(trigger->tsend.ep,
					    trigger->tsend.buf,
					    trigger->tsend.len,
					    trigger->tsend.desc,
					    trigger->tsend.dest_addr,
					    trigger->tsend.tag,
					    trigger->tsend.context,
					    trigger->tsend.flags);
			break;
		case PSMX_TRIGGERED_TRECV:
			_psmx_tagged_recvfrom(trigger->trecv.ep,
					      trigger->trecv.buf,
					      trigger->trecv.len,
					      trigger->trecv.desc,
					      trigger->trecv.src_addr,
					      trigger->trecv.tag,
					      trigger->trecv.ignore,
					      trigger->trecv.context,
					      trigger->trecv.flags);
			break;
		case PSMX_TRIGGERED_WRITE:
			_psmx_writeto(trigger->write.ep,
				      trigger->write.buf,
				      trigger->write.len,
				      trigger->write.desc,
				      trigger->write.dest_addr,
				      trigger->write.addr,
				      trigger->write.key,
				      trigger->write.context,
				      trigger->write.flags,
				      trigger->write.data);
			break;

		case PSMX_TRIGGERED_READ:
			_psmx_readfrom(trigger->read.ep,
				       trigger->read.buf,
				       trigger->read.len,
				       trigger->read.desc,
				       trigger->read.src_addr,
				       trigger->read.addr,
				       trigger->read.key,
				       trigger->read.context,
				       trigger->read.flags);
			break;

		case PSMX_TRIGGERED_ATOMIC_WRITE:
			_psmx_atomic_writeto(trigger->atomic_write.ep,
					     trigger->atomic_write.buf,
					     trigger->atomic_write.count,
					     trigger->atomic_write.desc,
					     trigger->atomic_write.dest_addr,
					     trigger->atomic_write.addr,
					     trigger->atomic_write.key,
					     trigger->atomic_write.datatype,
					     trigger->atomic_write.atomic_op,
					     trigger->atomic_write.context,
					     trigger->atomic_write.flags);
			break;

		case PSMX_TRIGGERED_ATOMIC_READWRITE:
			_psmx_atomic_readwriteto(trigger->atomic_readwrite.ep,
						 trigger->atomic_readwrite.buf,
						 trigger->atomic_readwrite.count,
						 trigger->atomic_readwrite.desc,
						 trigger->atomic_readwrite.result,
						 trigger->atomic_readwrite.result_desc,
						 trigger->atomic_readwrite.dest_addr,
						 trigger->atomic_readwrite.addr,
						 trigger->atomic_readwrite.key,
						 trigger->atomic_readwrite.datatype,
						 trigger->atomic_readwrite.atomic_op,
						 trigger->atomic_readwrite.context,
						 trigger->atomic_readwrite.flags);
			break;

		case PSMX_TRIGGERED_ATOMIC_COMPWRITE:
			_psmx_atomic_compwriteto(trigger->atomic_compwrite.ep,
						 trigger->atomic_compwrite.buf,
						 trigger->atomic_compwrite.count,
						 trigger->atomic_compwrite.desc,
						 trigger->atomic_compwrite.compare,
						 trigger->atomic_compwrite.compare_desc,
						 trigger->atomic_compwrite.result,
						 trigger->atomic_compwrite.result_desc,
						 trigger->atomic_compwrite.dest_addr,
						 trigger->atomic_compwrite.addr,
						 trigger->atomic_compwrite.key,
						 trigger->atomic_compwrite.datatype,
						 trigger->atomic_compwrite.atomic_op,
						 trigger->atomic_compwrite.context,
						 trigger->atomic_compwrite.flags);
			break;
		default:
			psmx_debug("%s: %d unsupported op\n", __func__, trigger->op);
			break;
		}

		free(trigger);
	}
}

void psmx_cntr_add_trigger(struct psmx_fid_cntr *cntr, struct psmx_trigger *trigger)
{
	struct psmx_trigger *p, *q;

	/* TODO: protect the trigger list with mutex */

	q = NULL;
	p = cntr->trigger;
	while (p && p->threshold <= trigger->threshold) {
		q = p;
		p = p->next;
	}
	if (q)
		q->next = trigger;
	else
		cntr->trigger = trigger;
	trigger->next = p;

	psmx_cntr_check_trigger(cntr);
}

static uint64_t psmx_cntr_read(struct fid_cntr *cntr)
{
	struct psmx_fid_cntr *cntr_priv;

	cntr_priv = container_of(cntr, struct psmx_fid_cntr, cntr);

	return cntr_priv->counter;
}

static uint64_t psmx_cntr_readerr(struct fid_cntr *cntr)
{
	struct psmx_fid_cntr *cntr_priv;

	cntr_priv = container_of(cntr, struct psmx_fid_cntr, cntr);

	return cntr_priv->error_counter;
}

static int psmx_cntr_add(struct fid_cntr *cntr, uint64_t value)
{
	struct psmx_fid_cntr *cntr_priv;

	cntr_priv = container_of(cntr, struct psmx_fid_cntr, cntr);
	cntr_priv->counter += value;

	psmx_cntr_check_trigger(cntr_priv);

	if (cntr_priv->wait_obj == FI_WAIT_MUT_COND)
		pthread_cond_signal(&cntr_priv->cond);

	return 0;
}

static int psmx_cntr_set(struct fid_cntr *cntr, uint64_t value)
{
	struct psmx_fid_cntr *cntr_priv;

	cntr_priv = container_of(cntr, struct psmx_fid_cntr, cntr);
	cntr_priv->counter = value;

	psmx_cntr_check_trigger(cntr_priv);

	if (cntr_priv->wait_obj == FI_WAIT_MUT_COND)
		pthread_cond_signal(&cntr_priv->cond);

	return 0;
}

static int psmx_cntr_wait(struct fid_cntr *cntr, uint64_t threshold, int timeout)
{
	struct psmx_fid_cntr *cntr_priv;
	int ret = 0;

	cntr_priv = container_of(cntr, struct psmx_fid_cntr, cntr);

	switch (cntr_priv->wait_obj) {
	case FI_WAIT_NONE:
		while (cntr_priv->counter < threshold) {
			psmx_cq_poll_mq(NULL, cntr_priv->domain, NULL, 0, NULL);
			psmx_am_progress(cntr_priv->domain);
		}
		break;

	case FI_WAIT_MUT_COND:
		pthread_mutex_lock(&cntr_priv->mutex);
		while (cntr_priv->counter < threshold)
			pthread_cond_wait(&cntr_priv->cond, &cntr_priv->mutex);
			ret = fi_wait_cond(&cntr_priv->cond, &cntr_priv->mutex, timeout);
		pthread_mutex_unlock(&cntr_priv->mutex);
		break;

	default:
		return -EBADF;
	}

	return 0;
}

static int psmx_cntr_close(fid_t fid)
{
	struct psmx_fid_cntr *cntr;

	cntr = container_of(fid, struct psmx_fid_cntr, cntr.fid);
	free(cntr);

	return 0;
}

static int psmx_cntr_control(fid_t fid, int command, void *arg)
{
	struct psmx_fid_cntr *cntr;

	cntr = container_of(fid, struct psmx_fid_cntr, cntr.fid);

	switch (command) {
	case FI_SETOPSFLAG:
		cntr->flags = *(uint64_t *)arg;
		break;

	case FI_GETOPSFLAG:
		if (!arg)
			return -EINVAL;
		*(uint64_t *)arg = cntr->flags;
		break;

	case FI_GETWAIT:
		if (!arg)
			return -EINVAL;
		((void **)arg)[0] = &cntr->mutex;
		((void **)arg)[1] = &cntr->cond;
		break;

	default:
		return -ENOSYS;
	}

	return 0;
}

static struct fi_ops psmx_fi_ops = {
	.size = sizeof(struct fi_ops),
	.close = psmx_cntr_close,
	.bind = fi_no_bind,
	.sync = fi_no_sync,
	.control = psmx_cntr_control,
};

static struct fi_ops_cntr psmx_cntr_ops = {
	.size = sizeof(struct fi_ops_cntr),
	.read = psmx_cntr_read,
	.readerr = psmx_cntr_readerr,
	.add = psmx_cntr_add,
	.set = psmx_cntr_set,
	.wait = psmx_cntr_wait,
};

int psmx_cntr_open(struct fid_domain *domain, struct fi_cntr_attr *attr,
			struct fid_cntr **cntr, void *context)
{
	struct psmx_fid_domain *domain_priv;
	struct psmx_fid_cntr *cntr_priv;
	int events , wait_obj;
	uint64_t flags;

	events = FI_CNTR_EVENTS_COMP;
	wait_obj = FI_WAIT_NONE;
	flags = 0;

	switch (attr->events) {
	case FI_CNTR_EVENTS_COMP:
		events = attr->events;
		break;

	default:
		psmx_debug("%s: attr->events=%d, supported=%d\n", __func__,
				attr->events, FI_CNTR_EVENTS_COMP);
		return -EINVAL;
	}

	switch (attr->wait_obj) {
	case FI_WAIT_NONE:
	case FI_WAIT_MUT_COND:
		wait_obj = attr->wait_obj;
		break;

	default:
		psmx_debug("%s: attr->wait_obj=%d, supported=%d,%d\n", __func__,
				attr->wait_obj, FI_WAIT_NONE, FI_WAIT_MUT_COND);
		return -EINVAL;
	}

	domain_priv = container_of(domain, struct psmx_fid_domain, domain);
	cntr_priv = (struct psmx_fid_cntr *) calloc(1, sizeof *cntr_priv);
	if (!cntr_priv)
		return -ENOMEM;

	cntr_priv->domain = domain_priv;
	cntr_priv->events = events;
	cntr_priv->wait_obj = wait_obj;
	cntr_priv->flags = flags;
	cntr_priv->cntr.fid.fclass = FI_CLASS_CNTR;
	cntr_priv->cntr.fid.context = context;
	cntr_priv->cntr.fid.ops = &psmx_fi_ops;
	cntr_priv->cntr.ops = &psmx_cntr_ops;

	if (wait_obj == FI_WAIT_MUT_COND) {
		pthread_mutex_init(&cntr_priv->mutex, NULL);
		pthread_cond_init(&cntr_priv->cond, NULL);
	}

	*cntr = &cntr_priv->cntr;
	return 0;
}

