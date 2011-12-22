/*
 * Translation between internal representation and RedHat ifcfg files
 *
 * Copyright (C) 2009-2011 Olaf Kirch <okir@suse.de>
 */
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <ctype.h>
#include <net/if_arp.h>

#include <wicked/netinfo.h>
#include <wicked/addrconf.h>
#include <wicked/bridge.h>
#include <wicked/bonding.h>
#include <wicked/vlan.h>
#include "netinfo_priv.h"
#include "sysconfig.h"


#define _PATH_NETCONFIG_DIR		"/etc/sysconfig/network"

static ni_interface_t *	__ni_redhat_read_interface(ni_netconfig_t *, const char *);
static int		__ni_redhat_sysconfig2ifconfig(ni_netconfig_t *, ni_interface_t *, ni_sysconfig_t *);
static int		__ni_redhat_startmode_set(ni_ifbehavior_t *, const char *);

static void		__ni_redhat_sysconfig2bridge(ni_interface_t *ifp, ni_sysconfig_t *);
static int		__ni_redhat_get_static_ipv4(ni_interface_t *, ni_sysconfig_t *);
static void		try_bonding_master(ni_netconfig_t *, ni_interface_t *, ni_sysconfig_t *);
static void		try_bonding_slave(ni_netconfig_t *, ni_interface_t *, ni_sysconfig_t *);
static void		try_bridge_port(ni_netconfig_t *, ni_interface_t *, ni_sysconfig_t *);
static void		try_vlan(ni_netconfig_t *, ni_interface_t *, ni_sysconfig_t *);
static void		try_wireless(ni_netconfig_t *, ni_interface_t *, ni_sysconfig_t *);

/*
 * Refresh network configuration by reading all ifcfg files.
 */
int
ni_sysconfig_read_redhat(ni_netconfig_t *nc, const char *root_dir)
{
	ni_string_array_t files = NI_STRING_ARRAY_INIT;
	char pathbuf[PATH_MAX], *base_dir;
	int i;

	base_dir = _PATH_NETCONFIG_DIR;
	if (root_dir) {
		snprintf(pathbuf, sizeof(pathbuf), "%s%s", root_dir, base_dir);
		base_dir = pathbuf;
	}
	if (!ni_sysconfig_scandir(base_dir, "ifcfg-", &files)) {
		ni_error("No ifcfg files found");
		return -1;
	}

	__ni_global_seqno++;
	for (i = 0; i < files.count; ++i) {
		const char *filename = files.data[i];
		ni_interface_t *ifp;

		snprintf(pathbuf, sizeof(pathbuf), "%s/%s", base_dir, filename);
		ifp = __ni_redhat_read_interface(nc, pathbuf);
		if (ifp == NULL)
			goto failed;
	}

	ni_string_array_destroy(&files);
	return 0;

failed:
	ni_string_array_destroy(&files);
	return -1;
}

/*
 * Read the configuration of a single interface from a sysconfig file
 */
static ni_interface_t *
__ni_redhat_read_interface(ni_netconfig_t *nc, const char *filename)
{
	char *ifname = NULL;
	ni_interface_t *ifp;
	ni_sysconfig_t *sc;

	sc = ni_sysconfig_read(filename);
	if (!sc) {
		ni_error("Error parsing %s", filename);
		goto error;
	}

	/* RH expects the DEVICE=... to take precedence over whatever is
	 * specified in the filename */
	ni_sysconfig_get_string(sc, "DEVICE", &ifname);
	if (ifname == NULL) {
		ni_error("%s: no DEVICE specified", filename);
		goto error;
	}

	/* Beware - bonding slaves may create their master interface
	 * on the fly */
	ifp = ni_interface_by_name(nc, ifname);
	if (ifp == NULL) {
		ifp = ni_interface_new(nc, ifname, 0);
		if (!ifp) {
			ni_error("Failed to alloc interface %s", ifname);
			goto error;
		}
	} else if (ifp->seq == __ni_global_seqno) {
		ni_error("duplicate definition of interface %s", ifp->name);
		return NULL;
	}

	if (__ni_redhat_sysconfig2ifconfig(nc, ifp, sc) < 0)
		goto error;

	ni_sysconfig_destroy(sc);
	ni_string_free(&ifname);
	ifp->seq = __ni_global_seqno;
	return ifp;

error:
	if (sc)
		ni_sysconfig_destroy(sc);
	ni_string_free(&ifname);
	return NULL;
}

static int
__ni_redhat_sysconfig2ifconfig(ni_netconfig_t *nc, ni_interface_t *ifp, ni_sysconfig_t *sc)
{
	char *value = NULL, *hwaddr = NULL;
	char *iftype = NULL;
	int onboot = 0;

	if (ni_sysconfig_get_boolean(sc, "ONBOOT", &onboot) < 0)
		return -1;
	__ni_redhat_startmode_set(&ifp->startmode, onboot? "onboot" : "manual");

	if (ni_sysconfig_get_string(sc, "BOOTPROTO", &value) >= 0 && value != NULL) {
		if (!strcmp(value, "dhcp")) {
			ni_afinfo_addrconf_enable(&ifp->ipv4, NI_ADDRCONF_DHCP);
		} else if (!strcmp(value, "none")) {
			ifp->ipv4.addrconf = NI_ADDRCONF_MASK(NI_ADDRCONF_STATIC);
			ifp->ipv6.addrconf = NI_ADDRCONF_MASK(NI_ADDRCONF_STATIC) | NI_ADDRCONF_MASK(NI_ADDRCONF_AUTOCONF);
		} else
			ni_warn("%s: unhandled BOOTPROTO \"%s\"", sc->pathname, value);
	}
	ni_string_free(&value);

	if (ni_sysconfig_get_string(sc, "HWADDR", &hwaddr) >= 0 && hwaddr) {
		if (ni_link_address_parse(&ifp->link.hwaddr, NI_IFTYPE_ETHERNET, hwaddr) < 0)
			return -1;
		ni_string_free(&hwaddr);
	}

	if (ni_sysconfig_get_integer(sc, "MTU", &ifp->link.mtu) < 0)
		return -1;

	if (ni_afinfo_addrconf_test(&ifp->ipv4, NI_ADDRCONF_STATIC))
		__ni_redhat_get_static_ipv4(ifp, sc);

	/* RedHat has TYPE=Bridge for bridge devices */
	if (ni_sysconfig_get_string(sc, "TYPE", &iftype) >= 0 && iftype) {
		if (!strcasecmp(iftype, "bridge"))
			__ni_redhat_sysconfig2bridge(ifp, sc);
	}

	if (ifp->link.type == NI_IFTYPE_UNKNOWN)
		try_bonding_master(nc, ifp, sc);

	if (ifp->link.type == NI_IFTYPE_UNKNOWN)
		try_vlan(nc, ifp, sc);

	if (ifp->link.type == NI_IFTYPE_UNKNOWN)
		try_wireless(nc, ifp, sc);

	/* Guess the interface type */
	if (ifp->link.type == NI_IFTYPE_UNKNOWN)
		ni_interface_guess_type(ifp);

	try_bonding_slave(nc, ifp, sc);
	try_bridge_port(nc, ifp, sc);

	/* FIXME: What to do with these:
		USERCONTROL
	 */

	return 0;
}

/*
 * Given a base name and a suffix (eg "IPADDR" and "_1"), build a variable name
 * and look it up.
 */
static int
__ni_sysconfig_get_ipv4addr(ni_sysconfig_t *sc, const char *name, ni_sockaddr_t *result)
{
	ni_var_t *var;
	var = ni_sysconfig_get(sc, name);

	memset(result, 0, sizeof(*result));
	if (var == NULL || var->value == NULL || var->value[0] == '\0')
		return 0;

	return ni_address_parse(result, var->value, AF_INET);
}

/*
 * Given a suffix like "" or "_1", try to get the IP address and prefix length.
 * If successful, create a new ni_address, attach it to the interface object
 * and return it. 
 */
static int
__ni_redhat_get_static_ipv4(ni_interface_t *ifp, ni_sysconfig_t *sc)
{
	ni_sockaddr_t address, netmask;
	unsigned int prefix_len = 32;

	if (__ni_sysconfig_get_ipv4addr(sc, "IPADDR", &address) < 0
	 || __ni_sysconfig_get_ipv4addr(sc, "NETMASK", &netmask) < 0)
		return -1;

	/* Not clear what an empty NETMASK means on RH. Probably default
	 * to IP address class... */
	if (netmask.ss_family == AF_UNSPEC) {
		prefix_len = 24;
	} else {
		prefix_len = ni_netmask_bits(&netmask);
	}

	if (!ni_address_new(ifp, address.ss_family, prefix_len, &address))
		return -1;

	return 0;
}

/*
 * Handle bonding.
 *
 * On Redhat, the master/slave relationship is defined in separate sysconfig files.
 * The slaves have SLAVE=yes MASTER=bondX, whereas the master has just BONDING_OPTS
 */
static void
try_bonding_master(ni_netconfig_t *nc, ni_interface_t *ifp, ni_sysconfig_t *sc)
{
	ni_bonding_t *bonding;

	if (!strncmp(ifp->name, "bond", 4)) {
		ifp->link.type = NI_IFTYPE_BOND;

		bonding = ni_interface_get_bonding(ifp);
		ni_sysconfig_get_string(sc, "BONDING_OPTS", &bonding->module_opts);
		ni_bonding_parse_module_options(bonding);
		ni_trace("primary=%s", bonding->primary);
	}
}

static void
try_bonding_slave(ni_netconfig_t *nc, ni_interface_t *ifp, ni_sysconfig_t *sc)
{
	ni_interface_t *master;
	ni_var_t *var;
	int is_slave = 0;
	ni_bonding_t *bonding;

	if (ni_sysconfig_get_boolean(sc, "SLAVE", &is_slave) < 0 || !is_slave)
		return;

	var = ni_sysconfig_get(sc, "MASTER");
	if (!var || !var->value || !var->value[0]) {
		ni_error("%s: slave interface with no MASTER", ifp->name);
		return;
	}

	master = ni_interface_by_name(nc, var->value);
	if (master == NULL) {
		master = ni_interface_new(nc, var->value, 0);
		master->link.type = NI_IFTYPE_BOND;
	} else if (master->link.type != NI_IFTYPE_BOND) {
		ni_error("%s: specifies MASTER=%s which is not a bonding device",
				ifp->name, master->name);
		return;
	}

	bonding = ni_interface_get_bonding(master);
	ni_bonding_add_slave(bonding, ifp->name);
}

/*
 * Bridge devices are recognized by TYPE=Bridge
 */
static void
__ni_redhat_sysconfig2bridge(ni_interface_t *ifp, ni_sysconfig_t *sc)
{
	ni_bridge_t *bridge;
	ifp->link.type = NI_IFTYPE_BRIDGE;
	ni_var_t *var;

	/* Create the interface's bridge data */
	bridge = ni_interface_get_bridge(ifp);
	if ((var = ni_sysconfig_get(sc, "STP")) != NULL)
		ni_bridge_set_stp(bridge, var->value);
	if ((var = ni_sysconfig_get(sc, "DELAY")) != NULL)
		ni_bridge_set_forward_delay(bridge, var->value);
}

/*
 * Recognize bridge port. This works a lot like bonding, ie we create
 * the bridge device if it doesn't exist, but do not set its type.
 */
static void
try_bridge_port(ni_netconfig_t *nc, ni_interface_t *ifp, ni_sysconfig_t *sc)
{
	ni_interface_t *master;
	ni_bridge_t *bridge;
	ni_var_t *var;

	var = ni_sysconfig_get(sc, "BRIDGE");
	if (!var || !var->value || !var->value[0])
		return;

	master = ni_interface_by_name(nc, var->value);
	if (master == NULL) {
		master = ni_interface_new(nc, var->value, 0);
		master->link.type = NI_IFTYPE_BRIDGE;
	} else if (master->link.type != NI_IFTYPE_BRIDGE) {
		ni_error("%s: specifies BRIDGE=%s which is not a bonding device",
				ifp->name, master->name);
		return;
	}

	bridge = ni_interface_get_bridge(master);
	ni_bridge_add_port_name(bridge, ifp->name);
}

/*
 * Wireless interfaces are recognized by WIRELESS=yes
 */
static void
try_wireless(ni_netconfig_t *nc, ni_interface_t *ifp, ni_sysconfig_t *sc)
{
	/* TBD */
}

/*
 * VLAN interfaces are recognized by their name (ethM.N)
 */
static void
try_vlan(ni_netconfig_t *nc, ni_interface_t *ifp, ni_sysconfig_t *sc)
{
	unsigned int eth_num, vlan_tag;
	char namebuf[32];
	ni_vlan_t *vlan;
	int is_vlan = 0;

	if (ni_sysconfig_get_boolean(sc, "VLAN", &is_vlan) < 0 || !is_vlan)
		return;

	if (sscanf(ifp->name, "eth%u.%u", &eth_num, &vlan_tag) != 2) {
		ni_error("%s: not a valid VLAN name", ifp->name);
		return;
	}

	ifp->link.type = NI_IFTYPE_VLAN;

	vlan = ni_interface_get_vlan(ifp);
	vlan->tag = vlan_tag;

	snprintf(namebuf, sizeof(namebuf), "eth%u", eth_num);
	ni_string_dup(&vlan->physdev_name, namebuf);
}

/*
 * Produce sysconfig files
 */


/*
 * Mapping ONBOOT values to behaviors and vice versa
 */
static struct __ni_ifbehavior_map __ni_redhat_startmodes[] = {
	{
		"manual",
		.behavior.ifaction = {
			[NI_IFACTION_MANUAL_UP]	= { .action = NI_INTERFACE_START,
						    .mandatory = 1,
						    .wait = 30
						  },
			[NI_IFACTION_MANUAL_DOWN]= { .action = NI_INTERFACE_STOP },
			[NI_IFACTION_BOOT]	= { .action = NI_INTERFACE_IGNORE, },
			[NI_IFACTION_SHUTDOWN]	= { .action = NI_INTERFACE_IGNORE, },
			[NI_IFACTION_LINK_UP]	= { .action = NI_INTERFACE_IGNORE, },
			[NI_IFACTION_LINK_DOWN]	= { .action = NI_INTERFACE_IGNORE, },
		}
	},
	{
		"onboot",
		.behavior.ifaction = {
			[NI_IFACTION_MANUAL_UP]	= { .action = NI_INTERFACE_START,
						    .mandatory = 1,
						    .wait = 30
						  },
			[NI_IFACTION_MANUAL_DOWN]= { .action = NI_INTERFACE_STOP },
			[NI_IFACTION_BOOT]	= { .action = NI_INTERFACE_START,
						    .mandatory = 1,
						    .wait = 30
						  },
			[NI_IFACTION_SHUTDOWN]	= { .action = NI_INTERFACE_STOP, },
			[NI_IFACTION_LINK_UP]	= { .action = NI_INTERFACE_START, },
			[NI_IFACTION_LINK_DOWN]	= { .action = NI_INTERFACE_STOP, },
		}
	},

	{ NULL }
};

static int
__ni_redhat_startmode_set(ni_ifbehavior_t *beh, const char *name)
{
	const ni_ifbehavior_t *match = NULL;

	if (name) {
		if (!strcmp(name, "on")
		 || !strcmp(name, "boot")
		 || !strcmp(name, "onboot"))
			name = "auto";

		match = __ni_netinfo_get_behavior(name, __ni_redhat_startmodes);
	}

	if (match)
		*beh = *match;
	else
		*beh = __ni_redhat_startmodes[0].behavior;
	return 0;
}
