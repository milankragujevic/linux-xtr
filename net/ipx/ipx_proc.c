/*
 *	IPX proc routines
 *
 * 	Copyright(C) Arnaldo Carvalho de Melo <acme@conectiva.com.br>, 2002
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/seq_file.h>
#include <linux/tcp.h>
#include <net/ipx.h>

#ifdef CONFIG_PROC_FS
static __inline__ struct ipx_interface *ipx_get_interface_idx(loff_t pos)
{
	struct ipx_interface *i;

	for (i = ipx_interfaces; pos && i; i = i->if_next)
		--pos;

	return i;
}

static void *ipx_seq_interface_start(struct seq_file *seq, loff_t *pos)
{
	loff_t l = *pos;

	spin_lock_bh(&ipx_interfaces_lock);
	return l ? ipx_get_interface_idx(--l) : (void *)1;
}

static void *ipx_seq_interface_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct ipx_interface *i;

	++*pos;
	if (v == (void *)1) {
		i = NULL;
		if (ipx_interfaces)
			i = ipx_interfaces;
		goto out;
	}
	i = v;
	i = i->if_next;
out:
	return i;
}

static void ipx_seq_interface_stop(struct seq_file *seq, void *v)
{
	spin_unlock_bh(&ipx_interfaces_lock);
}

static int ipx_seq_interface_show(struct seq_file *seq, void *v)
{
	struct ipx_interface *i;

	if (v == (void *)1) {
		seq_puts(seq, "Network    Node_Address   Primary  Device     "
			      "Frame_Type");
#ifdef IPX_REFCNT_DEBUG
		seq_puts(seq, "  refcnt");
#endif
		seq_puts(seq, "\n");
		goto out;
	}

	i = v;
	seq_printf(seq, "%08lX   ", (unsigned long int)ntohl(i->if_netnum));
	seq_printf(seq, "%02X%02X%02X%02X%02X%02X   ",
			i->if_node[0], i->if_node[1], i->if_node[2],
			i->if_node[3], i->if_node[4], i->if_node[5]);
	seq_printf(seq, "%-9s", i == ipx_primary_net ? "Yes" : "No");
	seq_printf(seq, "%-11s", ipx_device_name(i));
	seq_printf(seq, "%-9s", ipx_frame_name(i->if_dlink_type));
#ifdef IPX_REFCNT_DEBUG
	seq_printf(seq, "%6d", atomic_read(&i->refcnt));
#endif
	seq_puts(seq, "\n");
out:
	return 0;
}

static __inline__ struct ipx_route *ipx_get_route_idx(loff_t pos)
{
	struct ipx_route *r;

	for (r = ipx_routes; pos && r; r = r->ir_next)
		--pos;

	return r;
}

static void *ipx_seq_route_start(struct seq_file *seq, loff_t *pos)
{
	loff_t l = *pos;
	read_lock_bh(&ipx_routes_lock);
	return l ? ipx_get_route_idx(--l) : (void *)1;
}

static void *ipx_seq_route_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct ipx_route *r;

	++*pos;
	if (v == (void *)1) {
		r = NULL;
		if (ipx_routes)
			r = ipx_routes;
		goto out;
	}
	r = v;
	r = r->ir_next;
out:
	return r;
}

static void ipx_seq_route_stop(struct seq_file *seq, void *v)
{
	read_unlock_bh(&ipx_routes_lock);
}

static int ipx_seq_route_show(struct seq_file *seq, void *v)
{
	struct ipx_route *rt;

	if (v == (void *)1) {
		seq_puts(seq, "Network    Router_Net   Router_Node\n");
		goto out;
	}
	rt = v;
	seq_printf(seq, "%08lX   ", (unsigned long int)ntohl(rt->ir_net));
	if (rt->ir_routed)
		seq_printf(seq, "%08lX     %02X%02X%02X%02X%02X%02X\n",
			   (long unsigned int)ntohl(rt->ir_intrfc->if_netnum),
			   rt->ir_router_node[0], rt->ir_router_node[1],
			   rt->ir_router_node[2], rt->ir_router_node[3],
			   rt->ir_router_node[4], rt->ir_router_node[5]);
	else
		seq_puts(seq, "Directly     Connected\n");
out:
	return 0;
}

static __inline__ struct sock *ipx_get_socket_idx(loff_t pos)
{
	struct sock *s = NULL;
	struct ipx_interface *i;

	for (i = ipx_interfaces; pos && i; i = i->if_next) {
		spin_lock_bh(&i->if_sklist_lock);
		for (s = i->if_sklist; pos && s; s = s->next)
			--pos;
		if (!pos) {
			if (!s)
				spin_unlock_bh(&i->if_sklist_lock);
			break;
		}
		spin_unlock_bh(&i->if_sklist_lock);
	}

	return s;
}

static void *ipx_seq_socket_start(struct seq_file *seq, loff_t *pos)
{
	loff_t l = *pos;

	spin_lock_bh(&ipx_interfaces_lock);
	return l ? ipx_get_socket_idx(--l) : (void *)1;
}

static void *ipx_seq_socket_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct sock* sk;
	struct ipx_interface *i;
	struct ipx_opt *ipxs;

	++*pos;
	if (v == (void *)1) {
		sk = NULL;
		if (!ipx_interfaces)
			goto out;
		sk = ipx_interfaces->if_sklist;
		if (sk)
			spin_lock_bh(&ipx_interfaces->if_sklist_lock);
		goto out;
	}
	sk = v;
	if (sk->next) {
		sk = sk->next;
		goto out;
	}
	ipxs = ipx_sk(sk);
	i = ipxs->intrfc;
	spin_unlock_bh(&i->if_sklist_lock);
	sk = NULL;
	for (;;) {
		if (!i->if_next)
			break;
		i = i->if_next;
		spin_lock_bh(&i->if_sklist_lock);
		if (i->if_sklist) {
			sk = i->if_sklist;
			break;
		}
		spin_unlock_bh(&i->if_sklist_lock);
	}
out:
	return sk;
}

static int ipx_seq_socket_show(struct seq_file *seq, void *v)
{
	struct sock *s;
	struct ipx_opt *ipxs;

	if (v == (void *)1) {
#ifdef CONFIG_IPX_INTERN
		seq_puts(seq, "Local_Address               "
			      "Remote_Address              Tx_Queue  "
			      "Rx_Queue  State  Uid\n");
#else
		seq_puts(seq, "Local_Address  Remote_Address              "
			      "Tx_Queue  Rx_Queue  State  Uid\n");
#endif
		goto out;
	}

	s = v;
	ipxs = ipx_sk(s);
#ifdef CONFIG_IPX_INTERN
	seq_printf(seq, "%08lX:%02X%02X%02X%02X%02X%02X:%04X  ",
		   (unsigned long)htonl(ipxs->intrfc->if_netnum),
		   ipxs->node[0], ipxs->node[1], ipxs->node[2], ipxs->node[3],
		   ipxs->node[4], ipxs->node[5], htons(ipxs->port));
#else
	seq_printf(seq, "%08lX:%04X  ", (unsigned long) htonl(ipxs->intrfc->if_netnum),
		   htons(ipxs->port));
#endif	/* CONFIG_IPX_INTERN */
	if (s->state != TCP_ESTABLISHED)
		seq_printf(seq, "%-28s", "Not_Connected");
	else {
		seq_printf(seq, "%08lX:%02X%02X%02X%02X%02X%02X:%04X  ",
			   (unsigned long)htonl(ipxs->dest_addr.net),
			   ipxs->dest_addr.node[0], ipxs->dest_addr.node[1],
			   ipxs->dest_addr.node[2], ipxs->dest_addr.node[3],
			   ipxs->dest_addr.node[4], ipxs->dest_addr.node[5],
			   htons(ipxs->dest_addr.sock));
	}

	seq_printf(seq, "%08X  %08X  %02X     %03d\n",
		   atomic_read(&s->wmem_alloc), atomic_read(&s->rmem_alloc),
		   s->state, SOCK_INODE(s->socket)->i_uid);
out:
	return 0;
}

struct seq_operations ipx_seq_interface_ops = {
	.start  = ipx_seq_interface_start,
	.next   = ipx_seq_interface_next,
	.stop   = ipx_seq_interface_stop,
	.show   = ipx_seq_interface_show,
};

struct seq_operations ipx_seq_route_ops = {
	.start  = ipx_seq_route_start,
	.next   = ipx_seq_route_next,
	.stop   = ipx_seq_route_stop,
	.show   = ipx_seq_route_show,
};

struct seq_operations ipx_seq_socket_ops = {
	.start  = ipx_seq_socket_start,
	.next   = ipx_seq_socket_next,
	.stop   = ipx_seq_interface_stop,
	.show   = ipx_seq_socket_show,
};

static int ipx_seq_route_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &ipx_seq_route_ops);
}

static int ipx_seq_interface_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &ipx_seq_interface_ops);
}

static int ipx_seq_socket_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &ipx_seq_socket_ops);
}

static struct file_operations ipx_seq_interface_fops = {
	.open           = ipx_seq_interface_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = seq_release,
};

static struct file_operations ipx_seq_route_fops = {
	.open           = ipx_seq_route_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = seq_release,
};

static struct file_operations ipx_seq_socket_fops = {
	.open           = ipx_seq_socket_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = seq_release,
};

static struct proc_dir_entry *ipx_proc_dir;

int __init ipx_proc_init(void)
{
	struct proc_dir_entry *p;
	int rc = -ENOMEM;
       
	ipx_proc_dir = proc_mkdir("ipx", proc_net);

	if (!ipx_proc_dir)
		goto out;
	p = create_proc_entry("interface", S_IRUGO, ipx_proc_dir);
	if (!p)
		goto out_interface;

	p->proc_fops = &ipx_seq_interface_fops;
	p = create_proc_entry("route", S_IRUGO, ipx_proc_dir);
	if (!p)
		goto out_route;

	p->proc_fops = &ipx_seq_route_fops;
	p = create_proc_entry("socket", S_IRUGO, ipx_proc_dir);
	if (!p)
		goto out_socket;

	p->proc_fops = &ipx_seq_socket_fops;

	rc = 0;
out:
	return rc;
out_socket:
	remove_proc_entry("route", ipx_proc_dir);
out_route:
	remove_proc_entry("interface", ipx_proc_dir);
out_interface:
	remove_proc_entry("ipx", proc_net);
	goto out;
}

void __exit ipx_proc_exit(void)
{
	remove_proc_entry("interface", ipx_proc_dir);
	remove_proc_entry("route", ipx_proc_dir);
	remove_proc_entry("socket", ipx_proc_dir);
	remove_proc_entry("ipx", proc_net);
}

#else /* CONFIG_PROC_FS */

int __init ipx_proc_init(void)
{
	return 0;
}

void __exit ipx_proc_exit(void)
{
}

#endif /* CONFIG_PROC_FS */