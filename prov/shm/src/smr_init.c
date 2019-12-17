/*
 * Copyright (c) 2015-2017 Intel Corporation. All rights reserved.
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

#include <rdma/fi_errno.h>

#include <ofi_prov.h>
#include "smr.h"
#include "smr_signal.h"


static void smr_resolve_addr(const char *node, const char *service,
			     char **addr, size_t *addrlen)
{
	char temp_name[NAME_MAX];

	if (service) {
		if (node)
			snprintf(temp_name, NAME_MAX - 1, "%s%s:%s",
				 SMR_PREFIX_NS, node, service);
		else
			snprintf(temp_name, NAME_MAX - 1, "%s%s",
				 SMR_PREFIX_NS, service);
	} else {
		if (node)
			snprintf(temp_name, NAME_MAX - 1, "%s%s",
				 SMR_PREFIX, node);
		else
			snprintf(temp_name, NAME_MAX - 1, "%s%d",
				 SMR_PREFIX, getpid());
	}

	*addr = strdup(temp_name);
	*addrlen = strlen(*addr) + 1;
	(*addr)[*addrlen - 1]  = '\0';
}

static int smr_check_cma_capability(void)
{
	int shm_fd;
	pid_t  pid;
	uintptr_t *shared_memory;
	int msize = sizeof(uintptr_t);
	int ret;
	char name[NAME_MAX];
	snprintf(name, NAME_MAX - 1, "%s_%d", "cma_check", getpid());

	// shm file to exchange address and result of cma read
	shm_fd = shm_open (name, O_CREAT | O_EXCL | O_RDWR, S_IRWXU | S_IRWXG);
	if (shm_fd < 0) {
		FI_WARN(&smr_prov, FI_LOG_CORE, "Error opening shm file for CMA check\n");
		return -FI_EINVAL;
	}
	ret = ftruncate(shm_fd, msize);
	if (ret < 0) {
		FI_WARN(&smr_prov, FI_LOG_CORE, "Error truncating shm file for CMA check\n");
		ret = -FI_EINVAL;
		goto err1;
	}
	shared_memory = (uintptr_t *) mmap(NULL, msize, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
	if (shared_memory == NULL) {
		FI_WARN(&smr_prov, FI_LOG_CORE, "Error mapping shm file for CMA check\n");
		ret = -FI_EINVAL;
		goto err1;
	}

	pid = fork();
	if (pid == 0) {
	        // child process receives memory address, executes CMA read, and sends back the result
	        usleep(1000);
	        char buf_child[8];
	        uintptr_t caddr;
	        memcpy(&caddr, shared_memory, sizeof(uintptr_t));

	        struct iovec local[1];
	        struct iovec remote[1];
	        local[0].iov_base = buf_child;
	        local[0].iov_len = 8;
	        remote[0].iov_base = (void *) (*shared_memory);
	        remote[0].iov_len = 8;
	        int nread;
	        pid_t ppid = getppid();
	        nread = process_vm_readv(ppid, local, 1, remote, 1, 0);
	        if (nread == -1) {
			FI_WARN(&smr_prov, FI_LOG_CORE, "Error child trying execute process_vm_readv on its parent: %s\n", strerror(errno));
			ret = nread;
		} else {
			ret = 0;
		}
	        memcpy(shared_memory, &ret, sizeof(int));
		exit(0);
	} else {
	        // parent process sends memory address and receives child's CMA read result
	        char buf[8];
	        memset(buf, 'A', 8);
	        uintptr_t paddr = (uintptr_t) buf;
	        memcpy(shared_memory, &paddr, sizeof(uintptr_t));
	        wait(NULL);
	        memcpy(&ret, shared_memory, sizeof(int));
	}
	munmap(shared_memory, msize);
err1:
	shm_unlink(name);
	return ret;
}

static int smr_getinfo(uint32_t version, const char *node, const char *service,
		       uint64_t flags, const struct fi_info *hints,
		       struct fi_info **info)
{
	struct fi_info *cur;
	uint64_t mr_mode, msg_order;
	int fast_rma;
	int cma_cap, ret;

	mr_mode = hints && hints->domain_attr ? hints->domain_attr->mr_mode :
						FI_MR_VIRT_ADDR;
	msg_order = hints && hints->tx_attr ? hints->tx_attr->msg_order : 0;
	fast_rma = smr_fast_rma_enabled(mr_mode, msg_order);

	ret = util_getinfo(&smr_util_prov, version, node, service, flags,
			   hints, info);
	if (ret)
		return ret;

	cma_cap = smr_check_cma_capability();

	for (cur = *info; cur; cur = cur->next) {
		if (!(flags & FI_SOURCE) && !cur->dest_addr)
			smr_resolve_addr(node, service, (char **) &cur->dest_addr,
					 &cur->dest_addrlen);

		if (!cur->src_addr) {
			if (flags & FI_SOURCE)
				smr_resolve_addr(node, service, (char **) &cur->src_addr,
						 &cur->src_addrlen);
			else
				smr_resolve_addr(NULL, NULL, (char **) &cur->src_addr,
						 &cur->src_addrlen);
		}
		if (fast_rma) {
			cur->domain_attr->mr_mode = FI_MR_VIRT_ADDR;
			cur->tx_attr->msg_order = FI_ORDER_SAS;
			cur->ep_attr->max_order_raw_size = 0;
			cur->ep_attr->max_order_waw_size = 0;
			cur->ep_attr->max_order_war_size = 0;
		}
		if (cma_cap != 0)
			cur->ep_attr->max_msg_size = SMR_INJECT_SIZE;
	}
	return 0;
}

static void smr_fini(void)
{
	struct smr_ep_name *ep_name;
	struct dlist_entry *tmp;

	dlist_foreach_container_safe(&ep_name_list, struct smr_ep_name,
				     ep_name, entry, tmp) {
		free(ep_name);
	}
}

struct fi_provider smr_prov = {
	.name = "shm",
	.version = FI_VERSION(SMR_MAJOR_VERSION, SMR_MINOR_VERSION),
	.fi_version = OFI_VERSION_LATEST,
	.getinfo = smr_getinfo,
	.fabric = smr_fabric,
	.cleanup = smr_fini
};

struct util_prov smr_util_prov = {
	.prov = &smr_prov,
	.info = &smr_info,
	.flags = 0
};

SHM_INI
{
	dlist_init(&ep_name_list);

	/* Signal handlers to cleanup tmpfs files on an unclean shutdown */
	smr_reg_sig_hander(SIGBUS);
	smr_reg_sig_hander(SIGSEGV);
	smr_reg_sig_hander(SIGTERM);
	smr_reg_sig_hander(SIGINT);

	return &smr_prov;
}
