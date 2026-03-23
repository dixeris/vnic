#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/netdevice.h>
#include <net/rtnetlink.h>

#define DRV_NAME	"vnic"

static struct net_device_ops vnic_netdev_ops = 
{
	.ndo_init		=	vnic_dev_init,
	.ndo_start_xmit		=	vnic_xmit,
	.ndo_validate		=	eth_validate_addr,
	.ndo_set_rx_mode	=	set_multicast_list,
	.ndo_set_ma_address	=	eth_mac_addr,
	.ndo_get_stats64	=	vnic_get_stats64,
	.ndo_change_carrier	=	vnic_change_carrier,
};


static void vnic_setup(struct net_device *dev) 
{
	//Initialize device structure 
	ether_setup(dev);

	dev->netdev_ops = &dummy_netdev_ops;
	dev->needs_free_netdev = true; 
r
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


static struct rtnl_link_ops dummy_link_ops __read_mostly =
{
	.kind = DRV_NAME,
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

