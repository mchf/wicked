/*
 * DHCP client for wicked - handle lease files
 *
 * Copyright (C) 2010-2012 Olaf Kirch <okir@suse.de>
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <limits.h>
#include <unistd.h>

#include <wicked/netinfo.h>
#include <wicked/addrconf.h>
#include <wicked/logging.h>
#include <wicked/xml.h>
#include "netinfo_priv.h"

static const char *		__ni_addrconf_lease_file_path(int, int, const char *);

#if 1
int
ni_addrconf_lease_file_write(const char *ifname, ni_addrconf_lease_t *lease)
{
	ni_error("%s: currently not implemented", __func__);
	return -1;
}

ni_addrconf_lease_t *
ni_addrconf_lease_file_read(const char *ifname, int type, int family)
{
	ni_error("%s: currently not implemented", __func__);
	return NULL;
}
#else
/*
 * Write a lease to a file
 */
int
ni_addrconf_lease_file_write(const char *ifname, ni_addrconf_lease_t *lease)
{
	const char *filename;
	xml_node_t *xml = NULL;
	FILE *fp;

	filename = __ni_addrconf_lease_file_path(lease->type, lease->family, ifname);
	if (lease->state == NI_ADDRCONF_STATE_RELEASED) {
		ni_debug_dhcp("removing %s", filename);
		return unlink(filename);
	}

	ni_debug_dhcp("writing lease to %s", filename);
	if (ni_netcf_lease_to_xml(lease, NULL, &xml) < 0) {
		ni_error("cannot store lease: unable to represent lease as XML");
		goto failed;
	}

	if ((fp = fopen(filename, "w")) == NULL) {
		ni_error("unable to open %s for writing: %m", filename);
		goto failed;
	}

	xml_node_print(xml, fp);
	fclose(fp);

	xml_node_free(xml);
	return 0;

failed:
	if (xml)
		xml_node_free(xml);
	unlink(filename);
	return -1;
}

/*
 * Read a lease from a file
 */
ni_addrconf_lease_t *
ni_addrconf_lease_file_read(const char *ifname, int type, int family)
{
	ni_addrconf_lease_t *lease;
	const char *filename;
	xml_node_t *xml = NULL, *lnode;
	FILE *fp;

	filename = __ni_addrconf_lease_file_path(type, family, ifname);

	ni_debug_dhcp("reading lease from %s", filename);
	if ((fp = fopen(filename, "r")) == NULL) {
		if (errno != ENOENT)
			ni_error("unable to open %s for reading: %m", filename);
		return NULL;
	}

	xml = xml_node_scan(fp);
	fclose(fp);

	if (xml == NULL) {
		ni_error("unable to parse %s", filename);
		return NULL;
	}

	if (xml->name == NULL)
		lnode = xml->children;
	else
		lnode = xml;
	if (!lnode || !lnode->name || strcmp(lnode->name, "lease")) {
		ni_error("%s: does not contain a lease", filename);
		xml_node_free(xml);
		return NULL;
	}

	if (ni_netcf_xml_to_lease(lnode, &lease) < 0) {
		ni_error("%s: unable to parse lease xml", filename);
		xml_node_free(xml);
		return NULL;
	}

	xml_node_free(xml);
	return lease;
}
#endif

/*
 * Remove a lease file
 */
void
ni_addrconf_lease_file_remove(const char *ifname, int type, int family)
{
	const char *filename;

	filename = __ni_addrconf_lease_file_path(type, family, ifname);
	ni_debug_dhcp("removing %s", filename);
	unlink(filename);
}

static const char *
__ni_addrconf_lease_file_path(int type, int family, const char *ifname)
{
	static char pathname[PATH_MAX];

	snprintf(pathname, sizeof(pathname), "%s/lease-%s-%s-%s.xml",
			ni_config_statedir(),
			ni_addrconf_type_to_name(type),
			ni_addrfamily_type_to_name(family),
			ifname);
	return pathname;
}

