#include <net/xfrm.h>
#include <net/ip.h>

static u32      xfrm_policy_genid;
static rwlock_t xfrm_policy_lock = RW_LOCK_UNLOCKED;

struct xfrm_policy *xfrm_policy_list[XFRM_POLICY_MAX];

extern struct dst_ops xfrm4_dst_ops;

/* Limited flow cache. Its function now is to accelerate search for
 * policy rules.
 *
 * Flow cache is private to cpus, at the moment this is important
 * mostly for flows which do not match any rule, so that flow lookups
 * are absolultely cpu-local. When a rule exists we do some updates
 * to rule (refcnt, stats), so that locality is broken. Later this
 * can be repaired.
 */

struct flow_entry
{
	struct flow_entry	*next;
	struct flowi		fl;
	u8			dir;
	u32			genid;
	struct xfrm_policy	*pol;
};

static kmem_cache_t *flow_cachep;

struct flow_entry **flow_table;

#define FLOWCACHE_HASH_SIZE	1024

static inline u32 flow_hash(struct flowi *fl)
{
	u32 hash = fl->fl4_src ^ fl->uli_u.ports.sport;

	hash = ((hash & 0xF0F0F0F0) >> 4) | ((hash & 0x0F0F0F0F) << 4);

	hash ^= fl->fl4_dst ^ fl->uli_u.ports.dport;
	hash ^= (hash >> 10);
	hash ^= (hash >> 20);
	return hash & (FLOWCACHE_HASH_SIZE-1);
}

static int flow_lwm = 2*FLOWCACHE_HASH_SIZE;
static int flow_hwm = 4*FLOWCACHE_HASH_SIZE;

static int flow_number[NR_CPUS] __cacheline_aligned;

#define flow_count(cpu)		(flow_number[cpu])

static void flow_cache_shrink(int cpu)
{
	int i;
	struct flow_entry *fle, **flp;
	int shrink_to = flow_lwm/FLOWCACHE_HASH_SIZE;

	for (i=0; i<FLOWCACHE_HASH_SIZE; i++) {
		int k = 0;
		flp = &flow_table[cpu*FLOWCACHE_HASH_SIZE+i];
		while ((fle=*flp) != NULL && k<shrink_to) {
			k++;
			flp = &fle->next;
		}
		while ((fle=*flp) != NULL) {
			*flp = fle->next;
			if (fle->pol)
				xfrm_pol_put(fle->pol);
			kmem_cache_free(flow_cachep, fle);
		}
	}
}

struct xfrm_policy *flow_lookup(int dir, struct flowi *fl)
{
	struct xfrm_policy *pol;
	struct flow_entry *fle;
	u32 hash = flow_hash(fl);
	int cpu;

	local_bh_disable();
	cpu = smp_processor_id();

	for (fle = flow_table[cpu*FLOWCACHE_HASH_SIZE+hash];
	     fle; fle = fle->next) {
		if (memcmp(fl, &fle->fl, sizeof(fle->fl)) == 0 &&
		    fle->dir == dir) {
			if (fle->genid == xfrm_policy_genid) {
				if ((pol = fle->pol) != NULL)
					atomic_inc(&pol->refcnt);
				local_bh_enable();
				return pol;
			}
			break;
		}
	}

	pol = xfrm_policy_lookup(dir, fl);

	if (fle) {
		/* Stale flow entry found. Update it. */
		fle->genid = xfrm_policy_genid;

		if (fle->pol)
			xfrm_pol_put(fle->pol);
		fle->pol = pol;
		if (pol)
			atomic_inc(&pol->refcnt);
	} else {
		if (flow_count(cpu) > flow_hwm)
			flow_cache_shrink(cpu);

		fle = kmem_cache_alloc(flow_cachep, SLAB_ATOMIC);
		if (fle) {
			flow_count(cpu)++;
			fle->fl = *fl;
			fle->genid = xfrm_policy_genid;
			fle->dir = dir;
			fle->pol = pol;
			if (pol)
				atomic_inc(&pol->refcnt);
			fle->next = flow_table[cpu*FLOWCACHE_HASH_SIZE+hash];
			flow_table[cpu*FLOWCACHE_HASH_SIZE+hash] = fle;
		}
	}
	local_bh_enable();
	return pol;
}

void __init flow_cache_init(void)
{
	int order;

	flow_cachep = kmem_cache_create("flow_cache",
					sizeof(struct flow_entry),
					0, SLAB_HWCACHE_ALIGN,
					NULL, NULL);

	if (!flow_cachep)
		panic("NET: failed to allocate flow cache slab\n");

	for (order = 0;
	     (PAGE_SIZE<<order) < (NR_CPUS*sizeof(struct flow_entry *)*FLOWCACHE_HASH_SIZE);
	     order++)
		/* NOTHING */;

	flow_table = (struct flow_entry **)__get_free_pages(GFP_ATOMIC, order);

	if (!flow_table)
		panic("Failed to allocate flow cache hash table\n");

	memset(flow_table, 0, PAGE_SIZE<<order);
}


/* Allocate xfrm_policy. Not used here, it is supposed to be used by pfkeyv2
 * SPD calls.
 */

struct xfrm_policy *xfrm_policy_alloc(void)
{
	struct xfrm_policy *policy;

	policy = kmalloc(sizeof(struct xfrm_policy), GFP_KERNEL);

	if (policy) {
		memset(policy, 0, sizeof(struct xfrm_policy));
		atomic_set(&policy->refcnt, 1);
		policy->lock = RW_LOCK_UNLOCKED;
	}
	return policy;
}

/* Destroy xfrm_policy: descendant resources must be released to this moment. */

void __xfrm_policy_destroy(struct xfrm_policy *policy)
{
	int i;

	if (!policy->dead)
		BUG();

	for (i=0; i<policy->xfrm_nr; i++) {
		if (policy->xfrm_vec[i].resolved)
			BUG();
	}

	if (policy->bundles)
		BUG();

	kfree(policy);
}

/* Rule must be locked. Release descentant resources, announce
 * entry dead. The rule must be unlinked from lists to the moment.
 */

void xfrm_policy_kill(struct xfrm_policy *policy)
{
	struct dst_entry *dst;
	int i;

	policy->dead = 1;

	for (i=0; i<policy->xfrm_nr; i++) {
		if (policy->xfrm_vec[i].resolved) {
			xfrm_state_put(policy->xfrm_vec[i].resolved);
			policy->xfrm_vec[i].resolved = NULL;
		}
	}

	while ((dst = policy->bundles) != NULL) {
		policy->bundles = dst->next;
		dst_free(dst);
	}
}


/* Find policy to apply to this flow. */

struct xfrm_policy *xfrm_policy_lookup(int dir, struct flowi *fl)
{
	struct xfrm_policy *pol;
	unsigned long now = xtime.tv_sec;

	read_lock(&xfrm_policy_lock);
	for (pol = xfrm_policy_list[dir]; pol; pol = pol->next) {
		struct xfrm_selector *sel = &pol->selector;

		if (xfrm4_selector_match(sel, fl) && now < pol->expires) {
			pol->lastuse = now;
			atomic_inc(&pol->refcnt);
			break;
		}
	}
	read_unlock(&xfrm_policy_lock);
	return pol;
}

/* Resolve list of templates for the flow, given policy. */

static int
xfrm_tmpl_resolve(struct xfrm_policy *policy, struct flowi *fl,
		  struct xfrm_state **xfrm)
{
	int i, error;
	u32 daddr = fl->fl4_dst;

	for (i = 0; i < policy->xfrm_nr; i++) {
		struct xfrm_tmpl *tmpl = &policy->xfrm_vec[i];
		if (tmpl->mode)
			daddr = tmpl->id.daddr.xfrm4_addr;
		if (tmpl->resolved) {
			if (tmpl->resolved->km.state != XFRM_STATE_VALID) {
				error = -EINVAL;
				goto fail;
			}
			xfrm[i] = tmpl->resolved;
			atomic_inc(&tmpl->resolved->refcnt);
		} else {
			xfrm[i] = xfrm_state_find(daddr, fl, tmpl);
			if (xfrm[i] == NULL) {
				error = -ENOMEM;
				goto fail;
			}
			if (xfrm[i]->km.state == XFRM_STATE_VALID)
				continue;

			i++;
			if (xfrm[i]->km.state == XFRM_STATE_ERROR)
				error = -EINVAL;
			else
				error = -EAGAIN;
			goto fail;
		}
	}
	return 0;

fail:
	for (i--; i>=0; i--)
		xfrm_state_put(xfrm[i]);
	return error;
}

/* Check that the bundle accepts the flow and its components are
 * still valid.
 */

static int xfrm_bundle_ok(struct xfrm_dst *xdst, struct flowi *fl)
{
	do {
		if (xdst->u.dst.ops != &xfrm4_dst_ops)
			return 1;

		if (!xfrm4_selector_match(&xdst->u.dst.xfrm->sel, fl))
			return 0;
		if (xdst->u.dst.xfrm->km.state != XFRM_STATE_VALID ||
		    xdst->u.dst.path->obsolete > 0)
			return 0;
		xdst = (struct xfrm_dst*)xdst->u.dst.child;
	} while (xdst);
	return 0;
}


/* Allocate chain of dst_entry's, attach known xfrm's, calculate
 * all the metrics... Shortly, bundle a bundle.
 */

int
xfrm_bundle_create(struct xfrm_policy *policy, struct xfrm_state **xfrm,
		   struct flowi *fl, struct dst_entry **dst_p)
{
	struct dst_entry *dst, *dst_prev;
	struct rtable *rt = (struct rtable*)(*dst_p);
	u32 remote = fl->fl4_dst;
	u32 local  = fl->fl4_src;
	int i;
	int err;
	int header_len = 0;

	dst = dst_prev = NULL;

	for (i = 0; i < policy->xfrm_nr; i++) {
		struct dst_entry *dst1 = dst_alloc(&xfrm4_dst_ops);

		if (unlikely(dst1 == NULL)) {
			err = -ENOBUFS;
			goto error;
		}

		dst1->xfrm = xfrm[i];
		if (!dst)
			dst = dst1;
		else
			dst_prev->child = dst1;
		dst_prev = dst1;
		if (xfrm[i]->props.mode) {
			remote = xfrm[i]->id.daddr.xfrm4_addr;
			local  = xfrm[i]->props.saddr.xfrm4_addr;
		}
		header_len += xfrm[i]->props.header_len;
	}

	if (remote != fl->fl4_dst) {
		struct flowi fl_tunnel = { .nl_u = { .ip4_u =
						     { .daddr = remote,
						       .saddr = local }
					           }
				         };
		err = ip_route_output_key(&rt, &fl_tunnel);
		if (err)
			goto error;
		dst_release(*dst_p);
		*dst_p = &rt->u.dst;
	}
	dst_prev->child = &rt->u.dst;
	for (dst_prev = dst; dst_prev != &rt->u.dst; dst_prev = dst_prev->child) {
		struct xfrm_dst *x = (struct xfrm_dst*)dst_prev;
		x->u.rt.fl = *fl;

		dst_prev->dev = rt->u.dst.dev;
		if (rt->u.dst.dev)
			dev_hold(rt->u.dst.dev);
		dst_prev->flags		= DST_HOST;
		dst_prev->lastuse	= jiffies;
		dst_prev->header_len	= header_len;
		memcpy(&dst_prev->metrics, &rt->u.dst.metrics, sizeof(&dst_prev->metrics));
		dst_prev->path		= &rt->u.dst;

		/* Copy neighbout for reachability confirmation */
		dst_prev->neighbour	= neigh_clone(rt->u.dst.neighbour);
		dst_prev->input		= rt->u.dst.input;
		dst_prev->output	= dst_prev->xfrm->type->output;
		if (rt->peer)
			atomic_inc(&rt->peer->refcnt);
		x->u.rt.peer = rt->peer;
		x->u.rt.rt_flags = rt->rt_flags;
		x->u.rt.rt_type = rt->rt_type;
		x->u.rt.rt_src = rt->rt_src;
		x->u.rt.rt_src = rt->rt_src;
		x->u.rt.rt_dst = rt->rt_dst;
		x->u.rt.rt_gateway = rt->rt_gateway;
		x->u.rt.rt_spec_dst = rt->rt_spec_dst;
		header_len -= x->u.dst.xfrm->props.header_len;
	}
	*dst_p = dst;
	return 0;

error:
	if (dst)
		dst_free(dst);
	return err;
}

/* Main function: finds/creates a bundle for given flow.
 *
 * At the moment we eat a raw IP route. Mostly to speed up lookups
 * on interfaces with disabled IPsec.
 */
int xfrm_lookup(struct dst_entry **dst_p, struct flowi *fl,
		struct sock *sk, int flags)
{
	struct xfrm_policy *policy;
	struct xfrm_state *xfrm[XFRM_MAX_DEPTH];
	struct rtable *rt = (struct rtable*)*dst_p;
	struct dst_entry *dst;
	int err;
	u32 genid;

	/* To accelerate a bit...  */
	if ((rt->u.dst.flags & DST_NOXFRM) || !xfrm_policy_list[XFRM_POLICY_OUT])
		return 0;

	fl->oif = rt->u.dst.dev->ifindex;
	fl->fl4_src = rt->rt_src;

restart:
	genid = xfrm_policy_genid;
	policy = flow_lookup(XFRM_POLICY_OUT, fl);
	if (!policy)
		return 0;

	switch (policy->action) {
	case XFRM_POLICY_BLOCK:
		/* Prohibit the flow */
		xfrm_pol_put(policy);
		return -EPERM;

	case XFRM_POLICY_ALLOW:
		if (policy->xfrm_nr == 0) {
			/* Flow passes not transformed. */
			xfrm_pol_put(policy);
			return 0;
		}

		/* Try to find matching bundle.
		 *
		 * LATER: help from flow cache. It is optional, this
		 * is required only for output policy.
		 */
		read_lock_bh(&policy->lock);
		for (dst = policy->bundles; dst; dst = dst->next) {
			struct xfrm_dst *xdst = (struct xfrm_dst*)dst;
			if (xdst->u.rt.fl.fl4_dst == fl->fl4_dst &&
			    xdst->u.rt.fl.fl4_src == fl->fl4_src &&
			    xdst->u.rt.fl.oif == fl->oif &&
			    xfrm_bundle_ok(xdst, fl)) {
				dst_clone(dst);
				break;
			}
		}
		read_unlock_bh(&policy->lock);

		if (dst)
			break;

		err = xfrm_tmpl_resolve(policy, fl, xfrm);
		if (unlikely(err)) {
			if (err == -EAGAIN) {
				struct task_struct *tsk = current;
				DECLARE_WAITQUEUE(wait, tsk);
				if (!flags)
					goto error;

				__set_task_state(tsk, TASK_INTERRUPTIBLE);
				add_wait_queue(km_waitq, &wait);
				err = xfrm_tmpl_resolve(policy, fl, xfrm);
				if (err == -EAGAIN)
					schedule();
				__set_task_state(tsk, TASK_RUNNING);
				remove_wait_queue(km_waitq, &wait);

				if (err == -EAGAIN && signal_pending(current)) {
					err = -ERESTART;
					goto error;
				}
				if (err == -EAGAIN ||
				    genid != xfrm_policy_genid)
					goto restart;
			}
			if (err)
				goto error;
		}

		dst = &rt->u.dst;
		err = xfrm_bundle_create(policy, xfrm, fl, &dst);
		if (unlikely(err)) {
			int i;
			for (i=0; i<policy->xfrm_nr; i++)
				xfrm_state_put(xfrm[i]);
			err = -EPERM;
			goto error;
		}

		write_lock_bh(&policy->lock);
		if (unlikely(policy->dead)) {
			/* Wow! While we worked on resolving, this
			 * policy has gone. Retry. It is not paranoia,
			 * we just cannot enlist new bundle to dead object.
			 */
			write_unlock_bh(&policy->lock);

			xfrm_pol_put(policy);
			if (dst) {
				dst_release(dst);
				dst_free(dst);
			}
			goto restart;
		}
		dst->next = policy->bundles;
		policy->bundles = dst;
		write_unlock_bh(&policy->lock);
	}
	*dst_p = dst;
	ip_rt_put(rt);
	xfrm_pol_put(policy);
	return 0;

error:
	ip_rt_put(rt);
	xfrm_pol_put(policy);
	*dst_p = NULL;
	return err;
}

/* When skb is transformed back to its "native" form, we have to
 * check policy restrictions. At the moment we make this in maximally
 * stupid way. Shame on me. :-) Of course, connected sockets must
 * have policy cached at them.
 */

static inline int
xfrm_state_ok(struct xfrm_tmpl *tmpl, struct xfrm_state *x)
{
	return	x->id.proto == tmpl->id.proto &&
		(x->id.spi == tmpl->id.spi || !tmpl->id.spi) &&
		x->props.mode == tmpl->mode &&
		(tmpl->algos & (1<<x->props.algo)) &&
		(!x->props.mode || !tmpl->saddr.xfrm4_addr ||
		 tmpl->saddr.xfrm4_addr == x->props.saddr.xfrm4_addr);
}

static inline int
xfrm_policy_ok(struct xfrm_tmpl *tmpl, struct sec_path *sp, int idx)
{
	for (; idx < sp->len; idx++) {
		if (xfrm_state_ok(tmpl, sp->xvec[idx]))
			return ++idx;
	}
	return -1;
}

static inline void
_decode_session(struct sk_buff *skb, struct flowi *fl)
{
	struct iphdr *iph = skb->nh.iph;
	u8 *xprth = skb->nh.raw + iph->ihl*4;

	if (!(iph->frag_off & htons(IP_MF | IP_OFFSET))) {
		switch (iph->protocol) {
		case IPPROTO_UDP:
		case IPPROTO_TCP:
		case IPPROTO_SCTP:
			if (pskb_may_pull(skb, xprth + 4 - skb->data)) {
				u16 *ports = (u16 *)xprth;

				fl->uli_u.ports.sport = ports[0];
				fl->uli_u.ports.dport = ports[1];
			}
			break;

		case IPPROTO_ESP:
			if (pskb_may_pull(skb, xprth + 4 - skb->data)) {
				u32 *ehdr = (u32 *)xprth;

				fl->uli_u.spi = ehdr[0];
			}
			break;

		case IPPROTO_AH:
			if (pskb_may_pull(skb, xprth + 8 - skb->data)) {
				u32 *ah_hdr = (u32*)xprth;

				fl->uli_u.spi = ah_hdr[1];
			}
			break;

		default:
			fl->uli_u.spi = 0;
			break;
		};
	} else {
		memset(fl, 0, sizeof(struct flowi));
	}
	fl->proto = iph->protocol;
	fl->fl4_dst = iph->daddr;
	fl->fl4_src = iph->saddr;
}

int __xfrm_policy_check(int dir, struct sk_buff *skb)
{
	struct xfrm_policy *pol;
	struct flowi fl;

	_decode_session(skb, &fl);

	/* First, check used SA against their selectors. */
	if (skb->sp) {
		int i;
		for (i=skb->sp->len-1; i>=0; i++) {
			if (!xfrm4_selector_match(&skb->sp->xvec[i]->sel, &fl))
				return 0;
		}
	}

	pol = flow_lookup(dir, &fl);

	if (!pol)
		return 1;

	if (pol->action == XFRM_POLICY_ALLOW) {
		if (pol->xfrm_nr != 0) {
			struct sec_path *sp;
			int i, k;

			if ((sp = skb->sp) == NULL)
				goto reject;

			/* For each tmpl search corresponding xfrm.
			 * Order is _important_. Later we will implement
			 * some barriers, but at the moment barriers
			 * are implied between each two transformations.
			 */
			for (i = pol->xfrm_nr-1, k = 0; i >= 0; i--) {
				k = xfrm_policy_ok(pol->xfrm_vec+i, sp, k);
				if (k < 0)
					goto reject;
			}
		}
		xfrm_pol_put(pol);
		return 1;
	}

reject:
	xfrm_pol_put(pol);
	return 0;
}

int __xfrm_route_forward(struct sk_buff *skb)
{
	struct flowi fl;

	_decode_session(skb, &fl);

	return xfrm_lookup(&skb->dst, &fl, NULL, 0) == 0;
}

static struct dst_entry *xfrm4_dst_check(struct dst_entry *dst, u32 cookie)
{
	dst_release(dst);
	return NULL;
}

static void xfrm4_dst_destroy(struct dst_entry *dst)
{
	xfrm_state_put(dst->xfrm);
	dst->xfrm = NULL;
}

static void xfrm4_link_failure(struct sk_buff *skb)
{
	/* Impossible. Such dst must be popped before reaches point of failure. */
	return;
}

static struct dst_entry *xfrm4_negative_advice(struct dst_entry *dst)
{
	if (dst) {
		if (dst->obsolete) {
			dst_release(dst);
			dst = NULL;
		}
	}
	return dst;
}


static int xfrm4_garbage_collect(void)
{
	int i;
	struct xfrm_policy *pol;
	struct dst_entry *dst, **dstp, *gc_list = NULL;

	read_lock_bh(&xfrm_policy_lock);
	for (i=0; i<XFRM_POLICY_MAX; i++) {
		for (pol = xfrm_policy_list[i]; pol; pol = pol->next) {
			write_lock(&pol->lock);
			dstp = &pol->bundles;
			while ((dst=*dstp) != NULL) {
				if (atomic_read(&dst->__refcnt) == 0) {
					*dstp = dst->next;
					dst->next = gc_list;
					gc_list = dst;
				} else {
					dstp = &dst->next;
				}
			}
			write_unlock(&pol->lock);
		}
	}
	read_unlock_bh(&xfrm_policy_lock);

	while (gc_list) {
		dst = gc_list;
		gc_list = dst->next;
		dst_destroy(dst);
	}

	return (atomic_read(&xfrm4_dst_ops.entries) > xfrm4_dst_ops.gc_thresh*2);
}

static void xfrm4_update_pmtu(struct dst_entry *dst, u32 mtu)
{
	struct dst_entry *path = dst->path;

	if (mtu < 68 + dst->header_len)
		return;

	path->ops->update_pmtu(path, mtu);
}

/* Well... that's _TASK_. We need to scan through transformation
 * list and figure out what mss tcp should generate in order to
 * final datagram fit to mtu. Mama mia... :-)
 *
 * Apparently, some easy way exists, but we used to choose the most
 * bizarre ones. :-) So, raising Kalashnikov... tra-ta-ta.
 *
 * Consider this function as something like dark humour. :-)
 */
static int xfrm4_get_mss(struct dst_entry *dst, u32 mtu)
{
	int res = mtu - dst->header_len;

	for (;;) {
		struct dst_entry *d = dst;
		int m = res;

		do {
			struct xfrm_state *x = d->xfrm;
			if (x) {
				if (x->type->get_max_size)
					m = x->type->get_max_size(d->xfrm, m);
				else
					m += x->props.header_len;
			}
		} while ((d = d->child) != NULL);

		if (m <= mtu)
			break;
		res -= (m - mtu);
		if (res < 88)
			return mtu;
	}

	return res + dst->header_len;
}

struct dst_ops xfrm4_dst_ops = {
	.family =		AF_INET,
	.protocol =		__constant_htons(ETH_P_IP),
	.gc =			xfrm4_garbage_collect,
	.check =		xfrm4_dst_check,
	.destroy =		xfrm4_dst_destroy,
	.negative_advice =	xfrm4_negative_advice,
	.link_failure =		xfrm4_link_failure,
	.update_pmtu =		xfrm4_update_pmtu,
	.get_mss =		xfrm4_get_mss,
	.gc_thresh =		1024,
	.entry_size =		sizeof(struct xfrm_dst),
};

void __init xfrm_init(void)
{
	xfrm4_dst_ops.kmem_cachep = kmem_cache_create("xfrm4_dst_cache",
						      sizeof(struct xfrm_dst),
						      0, SLAB_HWCACHE_ALIGN,
						      NULL, NULL);

	if (!xfrm4_dst_ops.kmem_cachep)
		panic("IP: failed to allocate xfrm4_dst_cache\n");

	flow_cache_init();

	xfrm_state_init();
	xfrm_input_init();
	ah4_init();
}