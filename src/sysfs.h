/*
 * Routines for loading and storing sysconfig files
 *
 * Copyright (C) 2009-2012 Olaf Kirch <okir@suse.de>
 */

#ifndef __NETINFO_SYSFS_H__
#define __NETINFO_SYSFS_H__

#include <wicked/bridge.h>
#include <wicked/pci.h>

extern int	ni_sysfs_netif_get_int(const char *, const char *, int *);
extern int	ni_sysfs_netif_get_long(const char *, const char *, long *);
extern int	ni_sysfs_netif_get_uint(const char *, const char *, unsigned int *);
extern int	ni_sysfs_netif_get_ulong(const char *, const char *, unsigned long *);
extern int	ni_sysfs_netif_get_string(const char *, const char *, char **);
extern int	ni_sysfs_netif_put_int(const char *, const char *, int);
extern int	ni_sysfs_netif_put_long(const char *, const char *, long);
extern int	ni_sysfs_netif_put_uint(const char *, const char *, unsigned int);
extern int	ni_sysfs_netif_put_ulong(const char *, const char *, unsigned long);
extern int	ni_sysfs_netif_put_string(const char *, const char *, const char *);
extern int	ni_sysfs_netif_printf(const char *, const char *, const char *, ...);
extern ni_bool_t ni_sysfs_netif_exists(const char *, const char *);
extern int	ni_sysfs_bonding_available(void);
extern int	ni_sysfs_bonding_get_masters(ni_string_array_t *list);
extern int	ni_sysfs_bonding_is_master(const char *);
extern int	ni_sysfs_bonding_add_master(const char *);
extern int	ni_sysfs_bonding_delete_master(const char *);
extern int	ni_sysfs_bonding_get_slaves(const char *, ni_string_array_t *);
extern int	ni_sysfs_bonding_add_slave(const char *, const char *);
extern int	ni_sysfs_bonding_delete_slave(const char *, const char *);
extern int	ni_sysfs_bonding_get_attr(const char *, const char *, char **);
extern int	ni_sysfs_bonding_set_attr(const char *, const char *, const char *);
extern int	ni_sysfs_bonding_get_arp_targets(const char *, ni_string_array_t *);
extern int	ni_sysfs_bonding_add_arp_target(const char *, const char *);
extern int	ni_sysfs_bonding_delete_arp_target(const char *, const char *);
extern int	ni_sysfs_bonding_set_list_attr(const char *, const char *, const ni_string_array_t *);
extern void	ni_sysfs_bridge_get_config(const char *, ni_bridge_t *);
extern int	ni_sysfs_bridge_update_config(const char *, const ni_bridge_t *);
extern void	ni_sysfs_bridge_get_status(const char *, ni_bridge_status_t *);
extern int	ni_sysfs_bridge_get_port_names(const char *, ni_string_array_t *);
extern void	ni_sysfs_bridge_port_get_config(const char *, ni_bridge_port_t *);
extern int	ni_sysfs_bridge_port_update_config(const char *, const ni_bridge_port_t *);
extern void	ni_sysfs_bridge_port_get_status(const char *, ni_bridge_port_status_t *);
extern ni_pci_dev_t *ni_sysfs_netdev_get_pci(const char *ifname);

extern int	ni_sysctl_ipv6_ifconfig_is_present(const char *ifname);
extern int	ni_sysctl_ipv6_ifconfig_get_uint(const char *, const char *, unsigned int *);
extern int	ni_sysctl_ipv6_ifconfig_set_uint(const char *, const char *, unsigned int);

extern int	ni_sysctl_ipv4_ifconfig_is_present(const char *ifname);
extern int	ni_sysctl_ipv4_ifconfig_get_uint(const char *, const char *, unsigned int *);
extern int	ni_sysctl_ipv4_ifconfig_set_uint(const char *, const char *, unsigned int);

#endif /* __NETINFO_SYSFS_H__ */
