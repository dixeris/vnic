#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/ethtool.h>
#include <linux/etherdevice.h>
#include <linux/printk.h>
#include <linux/netdevice.h>
#include <linux/u64_stats_sync.h>
#include <linux/net_tstamp.h>
#include <net/rtnetlink.h>

#define DRV_NAME	"vnic"


static char *fwd_device_name = "wlo0";

struct vnic_private {
	struct net_device *fwd_device;
};
	
	
static void vnic_get_stats64(struct net_device *dev, 
				struct rtnl_link_stats64 *stats)
{
	dev_get_tstats64(dev, stats);
}

static netdev_tx_t vnic_xmit(struct sk_buff *skb, struct net_device *dev)
{
	int ret; 
	unsigned int len = skb->len;
	struct vnic_private *priv = netdev_priv(dev);

	//change net_device structure to process skb 
	skb->dev = priv->fwd_device;
	
	ret = dev_queue_xmit(skb);
	if(likely(ret == NET_XMIT_SUCCESS || ret == NET_XMIT_CN)) {
	
		dev_sw_netstats_tx_add(dev, 1, len);
	}
	
	return ret;
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

