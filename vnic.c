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

struct vnic_private {
	
};
	
	
static void vnic_get_stats64(struct net_device *dev, 
				struct rtnl_link_stats64 *stats)
{
	dev_get_tstats64(dev,stats);
}

static netdev_tx_t vnic_xmit(struct sk_buff *skb, struct net_device *dev)
{

	dev_sw_netstats_tx_add(dev, 1, skb->len);
	
	skb_tx_timestamp(skb);
	dev_kfree_skb(skb);
	
	return NETDEV_TX_OK;
}

static struct net_device_ops vnic_netdev_ops = 
{
	.ndo_start_xmit		=	vnic_xmit,
	.ndo_set_mac_address	=	eth_mac_addr,
	.ndo_get_stats64	=	vnic_get_stats64,
};

static void vnic_setup(struct net_device *dev) 
{
	//Initialize device structure 
	ether_setup(dev);

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
	return;
}

static __init int vnic_init_module(void) 
{
	int i, err = 0;

	rtnl_lock();
	err = __rtnl_link_register(&vnic_link_ops);
	rtnl_unlock();


	return err;
}


MODULE_LICENSE("GPL");
module_init(vnic_init_module);
module_exit(vnic_cleanup);

