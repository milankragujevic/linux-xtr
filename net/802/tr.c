/*
 * NET3:	Token ring device handling subroutines
 * 
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Fixes:       3 Feb 97 Paul Norton <pnorton@cts.com> Minor routing fixes.
 *              Added rif table to /proc/net/tr_rif and rif timeout to
 *              /proc/sys/net/token-ring/rif_timeout.
 *        
 */

#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/trdevice.h>
#include <linux/skbuff.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/net.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <net/arp.h>

static void tr_source_route(struct sk_buff *skb, struct trh_hdr *trh, struct device *dev);
static void tr_add_rif_info(struct trh_hdr *trh, struct device *dev);
static void rif_check_expire(unsigned long dummy);

#define TR_SR_DEBUG 0

typedef struct rif_cache_s *rif_cache;

/*
 *	Each RIF entry we learn is kept this way
 */
 
struct rif_cache_s {	
	unsigned char addr[TR_ALEN];
	unsigned char iface[5];
	__u16 rcf;
	__u16 rseg[8];
	rif_cache next;
	unsigned long last_used;
	unsigned char local_ring;
};

#define RIF_TABLE_SIZE 32

/*
 *	We hash the RIF cache 32 ways. We do after all have to look it
 *	up a lot.
 */
 
rif_cache rif_table[RIF_TABLE_SIZE]={ NULL, };

#define RIF_TIMEOUT 60*10*HZ
#define RIF_CHECK_INTERVAL 60*HZ

/*
 *	Garbage disposal timer.
 */
 
static struct timer_list rif_timer;

int sysctl_tr_rif_timeout = RIF_TIMEOUT;

/*
 *	Put the headers on a token ring packet. Token ring source routing
 *	makes this a little more exciting than on ethernet.
 */
 
int tr_header(struct sk_buff *skb, struct device *dev, unsigned short type,
              void *daddr, void *saddr, unsigned len) 
{

	struct trh_hdr *trh=(struct trh_hdr *)skb_push(skb,dev->hard_header_len);
	struct trllc *trllc=(struct trllc *)(trh+1);

	trh->ac=AC;
	trh->fc=LLC_FRAME;

	if(saddr)
		memcpy(trh->saddr,saddr,dev->addr_len);
	else
		memset(trh->saddr,0,dev->addr_len); /* Adapter fills in address */

	/*
	 *	This is the stuff needed for IP encoding - IP over 802.2
	 *	with SNAP.
	 */
	 
	trllc->dsap=trllc->ssap=EXTENDED_SAP;
	trllc->llc=UI_CMD;
	
	trllc->protid[0]=trllc->protid[1]=trllc->protid[2]=0x00;
	trllc->ethertype=htons(type);

	/*
	 *	Build the destination and then source route the frame
	 */
	 
	if(daddr) 
	{
		memcpy(trh->daddr,daddr,dev->addr_len);
		tr_source_route(skb,trh,dev);
		return(dev->hard_header_len);
	}
	return -dev->hard_header_len;

}
	
/*
 *	A neighbour discovery of some species (eg arp) has completed. We
 *	can now send the packet.
 */
 
int tr_rebuild_header(struct sk_buff *skb) 
{
	struct trh_hdr *trh=(struct trh_hdr *)skb->data;
	struct trllc *trllc=(struct trllc *)(skb->data+sizeof(struct trh_hdr));
	struct device *dev = skb->dev;

	/*
	 *	FIXME: We don't yet support IPv6 over token rings
	 */
	 
	if(trllc->ethertype != htons(ETH_P_IP)) {
		printk("tr_rebuild_header: Don't know how to resolve type %04X addresses ?\n",(unsigned int)htons(trllc->ethertype));
		return 0;
	}

	if(arp_find(trh->daddr, skb)) {
			return 1;
	}
	else 
	{	
		tr_source_route(skb,trh,dev); 
		return 0;
	}
}
	
/*
 *	Some of this is a bit hackish. We intercept RIF information
 *	used for source routing. We also grab IP directly and don't feed
 *	it via SNAP.
 */
 
unsigned short tr_type_trans(struct sk_buff *skb, struct device *dev) 
{

	struct trh_hdr *trh=(struct trh_hdr *)skb->data;
	struct trllc *trllc=(struct trllc *)(skb->data+sizeof(struct trh_hdr));
	
	skb->mac.raw = skb->data;
	
	skb_pull(skb,dev->hard_header_len);
	
	tr_add_rif_info(trh, dev);

	if(*trh->daddr & 1) 
	{
		if(!memcmp(trh->daddr,dev->broadcast,TR_ALEN)) 	
			skb->pkt_type=PACKET_BROADCAST;
		else
			skb->pkt_type=PACKET_MULTICAST;
	}

	else if(dev->flags & IFF_PROMISC) 
	{
		if(memcmp(trh->daddr, dev->dev_addr, TR_ALEN))
			skb->pkt_type=PACKET_OTHERHOST;
	}

 	return trllc->ethertype;
}

/*
 *      Reformat the headers to make a "standard" frame. This is done
 *      in-place in the sk_buff. 
 */

void tr_reformat(struct sk_buff *skb, unsigned int hdr_len)
{
	struct trllc *llc = (struct trllc *)(skb->data+hdr_len);
	struct device *dev = skb->dev;
	unsigned char *olddata = skb->data;
	int slack;

	if (llc->dsap == 0xAA && llc->ssap == 0xAA)
	{
		slack = sizeof(struct trh_hdr) - hdr_len;
		skb_push(skb, slack);
		memmove(skb->data, olddata, hdr_len);
		memset(skb->data+hdr_len, 0, slack);
	}
	else
	{
		struct trllc *local_llc;
		slack = sizeof(struct trh_hdr) - hdr_len + sizeof(struct trllc);
		skb_push(skb, slack);
		memmove(skb->data, olddata, hdr_len);
		memset(skb->data+hdr_len, 0, slack);
		local_llc = (struct trllc *)(skb->data+dev->hard_header_len);
		local_llc->ethertype = htons(ETH_P_TR_802_2);
       	}
}

/*
 *	We try to do source routing... 
 */

static void tr_source_route(struct sk_buff *skb,struct trh_hdr *trh,struct device *dev) 
{
	int i, slack;
	unsigned int hash;
	rif_cache entry;
	unsigned char *olddata;

	/*
	 *	Broadcasts are single route as stated in RFC 1042 
	 */
	if(!memcmp(&(trh->daddr[0]),&(dev->broadcast[0]),TR_ALEN)) 
	{
		trh->rcf=htons((((sizeof(trh->rcf)) << 8) & TR_RCF_LEN_MASK)  
			       | TR_RCF_FRAME2K | TR_RCF_LIMITED_BROADCAST);
		trh->saddr[0]|=TR_RII;
	}
	else 
	{
		for(i=0,hash=0;i<TR_ALEN;hash+=trh->daddr[i++]);
		hash&=RIF_TABLE_SIZE-1;
		/*
		 *	Walk the hash table and look for an entry
		 */
		for(entry=rif_table[hash];entry && memcmp(&(entry->addr[0]),&(trh->daddr[0]),TR_ALEN);entry=entry->next);

		/*
		 *	If we found an entry we can route the frame.
		 */
		if(entry) 
		{
#if TR_SR_DEBUG
printk("source routing for %02X %02X %02X %02X %02X %02X\n",trh->daddr[0],
		  trh->daddr[1],trh->daddr[2],trh->daddr[3],trh->daddr[4],trh->daddr[5]);
#endif
			if(!entry->local_ring && (ntohs(entry->rcf) & TR_RCF_LEN_MASK) >> 8)
			{
				trh->rcf=entry->rcf;
				memcpy(&trh->rseg[0],&entry->rseg[0],8*sizeof(unsigned short));
				trh->rcf^=htons(TR_RCF_DIR_BIT);	
				trh->rcf&=htons(0x1fff);	/* Issam Chehab <ichehab@madge1.demon.co.uk> */

				trh->saddr[0]|=TR_RII;
#if TR_SR_DEBUG
				printk("entry found with rcf %04x\n", entry->rcf);
			}
			else
			{
				printk("entry found but without rcf length, local=%02x\n", entry->local_ring);
#endif
			}
			entry->last_used=jiffies;
		}
		else 
		{
			/*
			 *	Without the information we simply have to shout
			 *	on the wire. The replies should rapidly clean this
			 *	situation up.
			 */
			trh->rcf=htons((((sizeof(trh->rcf)) << 8) & TR_RCF_LEN_MASK)  
				       | TR_RCF_FRAME2K | TR_RCF_LIMITED_BROADCAST);
			trh->saddr[0]|=TR_RII;
#if TR_SR_DEBUG
			printk("no entry in rif table found - broadcasting frame\n");
#endif
		}
	}

	/* Compress the RIF here so we don't have to do it in the driver(s) */
	if (!(trh->saddr[0] & 0x80))
		slack = 18;
	else 
		slack = 18 - ((ntohs(trh->rcf) & TR_RCF_LEN_MASK)>>8);
	olddata = skb->data;
	skb_pull(skb, slack);
	memmove(skb->data, olddata, sizeof(struct trh_hdr) - slack);
}

/*
 *	We have learned some new RIF information for our source
 *	routing.
 */
 
static void tr_add_rif_info(struct trh_hdr *trh, struct device *dev)
{
	int i;
	unsigned int hash, rii_p = 0;
	rif_cache entry;

	/*
	 *	Firstly see if the entry exists
	 */

       	if(trh->saddr[0] & TR_RII)
	{
		trh->saddr[0]&=0x7f;
		if (((ntohs(trh->rcf) & TR_RCF_LEN_MASK) >> 8) > 2)
		{
			rii_p = 1;
	        }
	}

	for(i=0,hash=0;i<TR_ALEN;hash+=trh->saddr[i++]);
	hash&=RIF_TABLE_SIZE-1;
	for(entry=rif_table[hash];entry && memcmp(&(entry->addr[0]),&(trh->saddr[0]),TR_ALEN);entry=entry->next);

	if(entry==NULL) 
	{
#if TR_SR_DEBUG
printk("adding rif_entry: addr:%02X:%02X:%02X:%02X:%02X:%02X rcf:%04X\n",
		trh->saddr[0],trh->saddr[1],trh->saddr[2],
       		trh->saddr[3],trh->saddr[4],trh->saddr[5],
		ntohs(trh->rcf));
#endif
		/*
		 *	Allocate our new entry. A failure to allocate loses
		 *	use the information. This is harmless.
		 *
		 *	FIXME: We ought to keep some kind of cache size
		 *	limiting and adjust the timers to suit.
		 */
		entry=kmalloc(sizeof(struct rif_cache_s),GFP_ATOMIC);

		if(!entry) 
		{
			printk(KERN_DEBUG "tr.c: Couldn't malloc rif cache entry !\n");
			return;
		}

		if (rii_p)
		{
			entry->rcf = trh->rcf & htons((unsigned short)~TR_RCF_BROADCAST_MASK);
			memcpy(&(entry->rseg[0]),&(trh->rseg[0]),8*sizeof(unsigned short));
			entry->local_ring = 0;
		}
		else
		{
			entry->local_ring = 1;
		}

		memcpy(&(entry->addr[0]),&(trh->saddr[0]),TR_ALEN);
		memcpy(&(entry->iface[0]),dev->name,5);
		entry->next=rif_table[hash];
		entry->last_used=jiffies;
		rif_table[hash]=entry;
	} 	
	else	/* Y. Tahara added */
	{ 
		/*
		 *	Update existing entries
		 */
		if (!entry->local_ring) 
		    if (entry->rcf != (trh->rcf & htons((unsigned short)~TR_RCF_BROADCAST_MASK)) &&
			 !(trh->rcf & htons(TR_RCF_BROADCAST_MASK)))
		    {
#if TR_SR_DEBUG
printk("updating rif_entry: addr:%02X:%02X:%02X:%02X:%02X:%02X rcf:%04X\n",
		trh->saddr[0],trh->saddr[1],trh->saddr[2],
		trh->saddr[3],trh->saddr[4],trh->saddr[5],
		ntohs(trh->rcf));
#endif
			    entry->rcf = trh->rcf & htons((unsigned short)~TR_RCF_BROADCAST_MASK);
        		    memcpy(&(entry->rseg[0]),&(trh->rseg[0]),8*sizeof(unsigned short));
		    }                                         
           	entry->last_used=jiffies;               
	}
}

/*
 *	Scan the cache with a timer and see what we need to throw out.
 */

static void rif_check_expire(unsigned long dummy) 
{
	int i;
	unsigned long now=jiffies,flags;

	save_flags(flags);
	cli();

	for(i=0; i < RIF_TABLE_SIZE;i++) 
	{
		rif_cache entry, *pentry=rif_table+i;	
		while((entry=*pentry)) 
		{
			/*
			 *	Out it goes
			 */
			if((now-entry->last_used) > sysctl_tr_rif_timeout) 
			{
				*pentry=entry->next;
				kfree_s(entry,sizeof(struct rif_cache_s));
			}
			else
				pentry=&entry->next;
		}
	}
	restore_flags(flags);

	/*
	 *	Reset the timer
	 */
	 
	del_timer(&rif_timer);
	rif_timer.expires = jiffies + sysctl_tr_rif_timeout;
	add_timer(&rif_timer);

}

/*
 *	Generate the /proc/net information for the token ring RIF
 *	routing.
 */
 
#ifdef CONFIG_PROC_FS
int rif_get_info(char *buffer,char **start, off_t offset, int length, int dummy) 
{
	int len=0;
	off_t begin=0;
	off_t pos=0;
	int size,i,j,rcf_len,segment,brdgnmb;
	unsigned long now=jiffies;

	rif_cache entry;

	size=sprintf(buffer,
		     "if     TR address       TTL   rcf   routing segments\n\n");
	pos+=size;
	len+=size;

	for(i=0;i < RIF_TABLE_SIZE;i++) 
	{
		for(entry=rif_table[i];entry;entry=entry->next) {
			size=sprintf(buffer+len,"%s %02X:%02X:%02X:%02X:%02X:%02X %7li ",
				     entry->iface,entry->addr[0],entry->addr[1],entry->addr[2],entry->addr[3],entry->addr[4],entry->addr[5],
				     sysctl_tr_rif_timeout-(now-entry->last_used));
			len+=size;
			pos=begin+len;
			if (entry->local_ring)
			        size=sprintf(buffer+len,"local\n");
			else {
			        size=sprintf(buffer+len,"%04X", ntohs(entry->rcf));
				rcf_len = ((ntohs(entry->rcf) & TR_RCF_LEN_MASK)>>8)-2; 
				if (rcf_len)
				        rcf_len >>= 1;
				for(j = 1; j < rcf_len; j++) {
					if(j==1) {
						segment=ntohs(entry->rseg[j-1])>>4;
						len+=size;
						pos=begin+len;
						size=sprintf(buffer+len,"  %03X",segment);
					};
					segment=ntohs(entry->rseg[j])>>4;
					brdgnmb=ntohs(entry->rseg[j-1])&0x00f;
					len+=size;
					pos=begin+len;
					size=sprintf(buffer+len,"-%01X-%03X",brdgnmb,segment);
				}
				len+=size;
				pos=begin+len;
			        size=sprintf(buffer+len,"\n");
			}
			len+=size;
			pos=begin+len;

			if(pos<offset) 
			{
				len=0;
				begin=pos;
			}
			if(pos>offset+length)
				break;
	   	}
		if(pos>offset+length)
			break;
	}

	*start=buffer+(offset-begin); /* Start of wanted data */
	len-=(offset-begin);    /* Start slop */
	if(len>length)
		len=length;    /* Ending slop */
	return len;
}
#endif

/*
 *	Called during bootup.  We don't actually have to initialise
 *	too much for this.
 */
 
__initfunc(void rif_init(struct net_proto *unused))
{

	rif_timer.expires  = RIF_TIMEOUT;
	rif_timer.data     = 0L;
	rif_timer.function = rif_check_expire;
	init_timer(&rif_timer);
	add_timer(&rif_timer);

#ifdef CONFIG_PROC_FS
	proc_net_register(&(struct proc_dir_entry) {
	  PROC_NET_TR_RIF, 6, "tr_rif",
	    S_IFREG | S_IRUGO, 1, 0, 0,
	    0, &proc_net_inode_operations,
	    rif_get_info
        });   
#endif
}
