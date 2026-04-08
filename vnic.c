#include <linux/minmax.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/ethtool.h>
#include <linux/etherdevice.h>
#include <linux/printk.h>
#include <linux/netdevice.h>
#include <linux/ip.h>
#include <linux/u64_stats_sync.h>
#include <linux/net_tstamp.h>
#include <linux/inet.h>
#include <net/rtnetlink.h>
#include <net/icmp.h>

#define DRV_NAME	"vnic"


static char *fwd_device_name = "wlo0";

struct vnic_private {
	struct net_device *fwd_device;
};
	
static __always_inline void swap_src_dst_mac(void *data, void *dst_ha)
{
	unsigned short *p = data;
	unsigned short *dst = dst_ha;

	p[3] = p[0];
	p[4] = p[1];
	p[5] = p[2];

	p[0] = dst[0];
	p[1] = dst[1];
	p[2] = dst[2];
}
	
static void vnic_get_stats64(struct net_device *dev, 
				struct rtnl_link_stats64 *stats)
{
	dev_get_tstats64(dev, stats);
}

/* * TODO: Implement packet forwarding logic
 * 1. flowi4 init -> 2. routing (ip_route_output_key) 
 * 3. neigh lookup (ip_neigh_for_gw) -> 4. MAC extraction (neigh->ha) 
 * 5. L2 encapsulation (eth_header) -> 6. Transmission (dev_queue_xmit)
*/
static int get_nxthop_ha(struct sk_buff *skb, void *dst_ha, 
		struct iphdr *iph, struct net_device *fwd_device)
{
	int ret = -1;
	struct rtable *rt;
	struct net *net = dev_net(fwd_device);
	struct neighbour *neigh; 
	struct flowi4 fl4;

	//flowi4 init 
	memset(&fl4, 0, sizeof(fl4));

	fl4.daddr = iph->daddr;
	fl4.saddr = iph->saddr;         
	fl4.flowi4_oif = fwd_device->ifindex; 
	fl4.flowi4_mark = skb->mark;
	fl4.flowi4_uid = sock_net_uid(net, NULL);
	fl4.flowi4_proto = IPPROTO_ICMP;
	fl4.flowi4_flags |= FLOWI_FLAG_ANYSRC;

	//get rtable for next hop 
	rt = ip_route_output_key(net, &fl4);

	if (IS_ERR(rt)) {
		int err_code = PTR_ERR(rt);

        pr_err("vnic: ip_route_output_key failed! dest: %pI4, err: %d\n",
               &fl4.daddr, err_code);
		return ret;
	}
	pr_info("after route_output_key\n");

	if (IS_ERR_OR_NULL(rt)) {
	    pr_err("vNIC ERROR: Routing lookup failed! PTR_ERR: %ld\n", PTR_ERR(rt));
	    return ret;
	}

	//ARP(neighbour) lookup 
	bool is_ipv6gw = 0; 
	neigh = ip_neigh_for_gw(rt, skb, &is_ipv6gw);
	pr_info("after ip_neighbor_for_gw\n");
	if (!IS_ERR(neigh)) {
		pr_info("successfully get neighbour\n");
		memcpy(dst_ha, neigh->ha, ETH_ALEN);
	        neigh_release(neigh);

		ret = 0;
	}


	ip_rt_put(rt);
	return ret; 
}

static int handle_ping_reply(struct sk_buff *skb, struct net_device *dev,
					struct net_device *fwd_device) 
{
	int ret;
	struct ethhdr *eth = eth_hdr(skb);
	pr_info("eth_hdr success\n");
	struct icmphdr *icmph = icmp_hdr(skb);
	pr_info("icmphdr success\n");
	struct iphdr *iph = ip_hdr(skb);
	pr_info("ip_hdr success\n");
	unsigned char dst_ha[ETH_ALEN];


	//change to ECHOREPLY
	icmph->type	=	ICMP_ECHOREPLY;
	
	//ip address swap
	pr_info("before swap\n");
	swap(iph->saddr,iph->daddr);
	pr_info("after swap\n");
	
	//MAC address setting 
	if(!get_nxthop_ha(skb, dst_ha, iph, fwd_device)) {
		pr_info("do swap\n");
		swap_src_dst_mac(eth, dst_ha);
	}


	//checksum recalculatation	
	
	//ip checksum 
	iph->check = 0;
	ip_send_check(iph);

	//icmp(transport layer) checksum 
	icmph->checksum = 0;
	//icmphdr checksum size is 16bit. we have to fold 32-bit checksum 
	//to 16bit 
	icmph->checksum = ip_compute_csum(icmph, ntohs(iph->tot_len) - ip_hdrlen(skb));
	
	//send 
	skb->dev	=	fwd_device;

	ret = dev_queue_xmit(skb);
	return ret;
}

static netdev_tx_t vnic_xmit(struct sk_buff *skb, struct net_device *dev)
{
	int ret; 
	unsigned int len = skb->len;
	struct vnic_private *priv = netdev_priv(dev);
	struct iphdr *iph;

	//to prevent network loop
	if(skb->mark == 0x77) {
		goto drop;
	}

	iph = ip_hdr(skb);

	//is this packet headed to myself or outside?
	if(iph->protocol == IPPROTO_ICMP && iph->daddr == in_aton("10.0.0.1")) 
	{
		pr_info("10.0.0.1 icmp\n");
		ret = handle_ping_reply(skb,dev,priv->fwd_device);
		if(likely(ret == NET_XMIT_SUCCESS || ret == NET_XMIT_CN)) {
			dev_sw_netstats_tx_add(dev, 1, len);
			return NETDEV_TX_OK;
		}
	}

	//if the packet is headed to outside 
	//change net_device structure to process skb 
	//mark first
	skb->mark = 0x77;
	skb->dev = priv->fwd_device;
	
	ret = dev_queue_xmit(skb);
	if(likely(ret == NET_XMIT_SUCCESS || ret == NET_XMIT_CN)) {
	
		dev_sw_netstats_tx_add(dev, 1, len);

	}
	
	return NETDEV_TX_OK;

drop:
	dev_kfree_skb(skb);
	printk(KERN_INFO "network loop occur for vnic! Drop the packet\n");
	return NETDEV_TX_OK;
}


static struct net_device_ops vnic_netdev_ops = 
{
	.ndo_start_xmit		=	vnic_xmit,
	.ndo_set_mac_address	=	eth_mac_addr,
	.ndo_get_stats64	=	vnic_get_stats64,
};

module_param(fwd_device_name, charp, 0444);
MODULE_PARM_DESC(fwd_device_name, "Physical device to attach to this");

static void vnic_setup(struct net_device *dev) 
{
	struct vnic_private *priv;
	//find fwd_device net_device struct by its name 
	struct net_device *fwd_device = dev_get_by_name(&init_net,
							fwd_device_name);

	if(!fwd_device) {
		printk(KERN_ERR "Cannot find specified physical net device");
		return; 
	}

	//Initialize device structure 
	ether_setup(dev);

	priv = netdev_priv(dev);	
	priv->fwd_device = fwd_device; 

	dev->netdev_ops = &vnic_netdev_ops;
	dev->needs_free_netdev = true; 

	//disable ARP and Multicast because this is virtual device. 	
	dev->flags |= IFF_NOARP;
	dev->flags &= ~IFF_MULTICAST;	

	//allow live physical address changing, packet sent without qdisc	
	dev->priv_flags |= IFF_LIVE_ADDR_CHANGE | IFF_NO_QUEUE;

	//feature options for virtual device 
	dev->features   |= NETIF_F_SG | NETIF_F_FRAGLIST;
	dev->features   |= NETIF_F_GSO_SOFTWARE;
	dev->features   |= NETIF_F_HW_CSUM | NETIF_F_HIGHDMA;
	dev->features   |= NETIF_F_GSO_ENCAP_ALL;

	dev->hw_features |= dev->features;
	dev->hw_enc_features |= dev->features;
	eth_hw_addr_random(dev);

	
}


static struct rtnl_link_ops vnic_link_ops __read_mostly =
{
	.kind = DRV_NAME,
	.priv_size = sizeof(struct vnic_private),
	.setup = vnic_setup,
};

static __exit void vnic_cleanup(void)
{
	rtnl_link_unregister(&vnic_link_ops);
	return;
}

static __init int vnic_init_module(void) 
{
	int err = 0;

	rtnl_lock();
	err = __rtnl_link_register(&vnic_link_ops);
	rtnl_unlock();


	return err;
}


MODULE_LICENSE("GPL");
module_init(vnic_init_module);
module_exit(vnic_cleanup);

//TODO  add delink logic(dev_put)
//thinking about skb_cloned() 
