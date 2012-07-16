/*
 *	DHCP6 supplicant -- main
 *
 *	Copyright (C) 2010-2012 Olaf Kirch <okir@suse.de>
 *	Copyright (C) 2012 Marius Tomaschewski <mt@suse.de>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License along
 *	with this program; if not, see <http://www.gnu.org/licenses/> or write
 *	to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *	Boston, MA 02110-1301 USA.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <signal.h>
#include <limits.h>
#include <net/if_arp.h>

#include <wicked/types.h>
#include <wicked/util.h>
#include <wicked/wireless.h>
#include <wicked/netinfo.h>
#include <wicked/objectmodel.h>
#include <wicked/logging.h>

#include "dhcp6/dbus-api.h"

#define CONFIG_DHCP6_STATE_FILE	"dhcp6-state.xml"

/* Hmm .. */
#ifndef no_argument
#define no_argument		0
#endif
#ifndef required_argument
#define required_argument	1
#endif
#ifndef optional_argument
#define optional_argument	2
#endif

enum {
	OPT_CONFIGFILE,
	OPT_DEBUG,
	OPT_FOREGROUND,
	OPT_NOFORK,
	OPT_NORECOVER,
	OPT_DBUS,
	OPT_INFO_REQUEST,
	OPT_LEASE_REQUEST,
	OPT_LEASE_RELEASE,
};

static struct option		options[] = {
	{ "config",		required_argument,	NULL,	OPT_CONFIGFILE },
	{ "debug",		required_argument,	NULL,	OPT_DEBUG },
	{ "foreground",		no_argument,		NULL,	OPT_FOREGROUND },
	{ "no-fork",		no_argument,		NULL,	OPT_NOFORK },
	{ "no-recovery",	no_argument,		NULL,	OPT_NORECOVER },
	{ "request-info",	required_argument,	NULL,	OPT_INFO_REQUEST },
	{ "request-lease",	required_argument,	NULL,	OPT_LEASE_REQUEST },
	{ "release-lease",	required_argument,	NULL,	OPT_LEASE_RELEASE },

	{ NULL,			no_argument,		NULL,	0 }
};

static int			opt_foreground = 1;
static int			opt_recover_leases = 0;
static char *			opt_state_file;

static ni_dbus_server_t *	dhcp6_dbus_server;
static int			dhcp6_term_sig = 0;


static void			dhcp6_supplicant(void);
static void			dhcp6_catch_term_signal(int);

extern ni_dhcp6_request_t *	ni_dhcp6_request_new(void);
static void			dhcp6_client_request_info(const char *ifname);
static void			dhcp6_client_request_lease(const char *ifname);
static void			dhcp6_client_release_lease(const char *ifname);

int
main(int argc, char **argv)
{
	const char *progname;
	const char *ifname = NULL;
	int action = OPT_DBUS;
	int c;

	progname = ni_basename(argv[0]);

	while ((c = getopt_long(argc, argv, "+", options, NULL)) != EOF) {
		switch (c) {
		default:
		usage:
			fprintf(stderr,
				"%s [options]\n"
				"This command understands the following options\n"
				"  --config filename\n"
				"        Read configuration file <filename> instead of system default.\n"
				"  --debug facility\n"
				"        Enable debugging for debug <facility>.\n"
				"  --foreground\n"
				"        Do not background the service.\n"
				"  --norecover\n"
				"        Disable automatic recovery of leases.\n"
				"  --request-info <ifname>\n"
				"        Send info request on interface and show it\n"
				"  --request-lease <ifname>\n"
				"        Send lease request on interface and show it\n"
				"  --release-lease <ifname>\n"
				"        Send lease release on interface and show it\n"
				, progname
			       );
			return 1;

		case OPT_CONFIGFILE:
			ni_set_global_config_path(optarg);
			break;

		case OPT_DEBUG:
			if (!strcmp(optarg, "help")) {
				printf("Supported debug facilities:\n");
				ni_debug_help(stdout);
				return 0;
			}
			if (ni_enable_debug(optarg) < 0) {
				fprintf(stderr, "Bad debug facility \"%s\"\n", optarg);
				return 1;
			}
			break;

		case OPT_FOREGROUND:
			opt_foreground = 1;
			break;

		case OPT_NORECOVER:
			opt_recover_leases = 0;
			break;

		case OPT_INFO_REQUEST:
			action = OPT_INFO_REQUEST;
			ifname = optarg;
			break;
		case OPT_LEASE_REQUEST:
			action = OPT_LEASE_REQUEST;
			ifname = optarg;
			break;
		case OPT_LEASE_RELEASE:
			action = OPT_LEASE_RELEASE;
			ifname = optarg;
			break;
		}
	}

	if (optind != argc)
		goto usage;

	if (ni_init() < 0)
		return 1;

	if (opt_state_file == NULL) {
		static char dirname[PATH_MAX];

		snprintf(dirname, sizeof(dirname), "%s/%s", ni_config_statedir(),
							CONFIG_DHCP6_STATE_FILE);
		opt_state_file = dirname;
	}

	/* We're using randomized timeouts. Seed the RNG */
	ni_srandom();

	switch(action) {
		case OPT_INFO_REQUEST:
			dhcp6_client_request_info(ifname);
		break;

		case OPT_LEASE_REQUEST:
			dhcp6_client_request_lease(ifname);
		break;

		case OPT_LEASE_RELEASE:
			dhcp6_client_release_lease(ifname);
		break;

		default:
			dhcp6_supplicant();
		break;
	}

	return 0;
}

/*
 * Just some test code...
 */
static void
dhcp6_client_request_info(const char *ifname)
{
	ni_dhcp6_device_t *dev;
	ni_netconfig_t *nc;
	ni_netdev_t *ifp;
	//ni_duid_t *duid;
	ni_dhcp6_request_t *req;

	/* Disable wireless AP scanning */
	ni_wireless_set_scanning(FALSE);

	if (!(nc = ni_global_state_handle(1)))
		ni_fatal("cannot refresh interface list!");

	ifp = ni_netdev_by_name(nc, ifname);
	if (!ifp)
		ni_fatal("unable to find interface with name %s", ifname);

	dev = ni_dhcp6_device_new(ifp->name, &ifp->link);
	if (!dev) {
		ni_fatal("Cannot create dhcp6 device for %s", ifp->name);
	}

	req = ni_dhcp6_request_new();
	if(!req) {
		ni_fatal("Cannot construct dhcp request for %s", ifp->name);
	}

	/*
	 * FIXME:
	 * Init request parameter, e.g. client-id, oro list, ... you'd like to see.
	 */
	req->info_only = TRUE;
	if(ni_dhcp6_acquire(dev, req) < 0) {
		ni_dhcp6_request_free(req);
		ni_fatal("Failed to schedule dhcp6 request");
	}

	signal(SIGTERM, dhcp6_catch_term_signal);
	signal(SIGINT,  dhcp6_catch_term_signal);

	while (dhcp6_term_sig == 0) {
		long timeout;

		timeout = ni_timer_next_timeout();
		ni_trace("next timeout in %ld", timeout);
		if (ni_socket_wait(timeout) < 0)
			ni_fatal("ni_socket_wait failed");
	}

	ni_debug_dhcp("caught signal %u, exiting", dhcp6_term_sig);
}

static void
dhcp6_client_request_lease(const char *ifname)
{
	ni_dhcp6_device_t *dev;
	ni_netconfig_t *nc;
	ni_netdev_t *ifp;
	//ni_duid_t *duid;
	ni_dhcp6_request_t *req;

	/* Disable wireless AP scanning */
	ni_wireless_set_scanning(FALSE);

	if (!(nc = ni_global_state_handle(1)))
		ni_fatal("cannot refresh interface list!");

	ifp = ni_netdev_by_name(nc, ifname);
	if (!ifp)
		ni_fatal("unable to find interface with name %s", ifname);

	dev = ni_dhcp6_device_new(ifp->name, &ifp->link);
	if (!dev) {
		ni_fatal("Cannot create dhcp6 device for %s", ifp->name);
	}

	req = ni_dhcp6_request_new();
	if(!req) {
		ni_fatal("Cannot construct dhcp request for %s", ifp->name);
	}

	/*
	 * FIXME:
	 * Init request parameter, e.g. client-id, oro list, ... you'd like to see.
	 */
	req->info_only = FALSE;
	if(ni_dhcp6_acquire(dev, req) < 0) {
		ni_dhcp6_request_free(req);
		ni_fatal("Failed to schedule dhcp6 request");
	}
	ni_dhcp6_device_set_request(dev, req);

	signal(SIGTERM, dhcp6_catch_term_signal);
	signal(SIGINT,  dhcp6_catch_term_signal);

	while (dhcp6_term_sig == 0) {
		long timeout;

		timeout = ni_timer_next_timeout();
		ni_trace("next timeout in %ld", timeout);
		if (ni_socket_wait(timeout) < 0)
			ni_fatal("ni_socket_wait failed");
	}

	ni_debug_dhcp("caught signal %u, exiting", dhcp6_term_sig);
}

static void
dhcp6_client_release_lease(const char *ifname)
{
	ni_fatal("Not implemented yet");
}

/*
 * Functions to support the DHCP6 DBus binding
 */
static ni_dbus_service_t	__wicked_dbus_dhcp6_interface = {
	.name			= NI_OBJECTMODEL_DHCP6_INTERFACE,	/* com.suse.Wicked.DHCP6 */
};

static void			dhcp6_discover_devices(ni_dbus_server_t *);
static void			dhcp6_interface_event(ni_netdev_t *, ni_event_t);
static void			dhcp6_protocol_event(enum ni_dhcp6_event, const ni_dhcp6_device_t *, ni_addrconf_lease_t *);
static void			dhcp6_recover_addrconf(const char *filename);


/*
 * Register DHCP6 dbus interface services
 */
static void
dhcp6_register_services(ni_dbus_server_t *server)
{
	ni_dbus_object_t *root_object = ni_dbus_server_get_root_object(server);
	ni_dbus_object_t *object;

	/*  Register the root object (com.suse.Wicked.DHCP6) */
	ni_dbus_object_register_service(root_object, &__wicked_dbus_dhcp6_interface);

	/* Register /com/suse/Wicked/DHCP6/Interface */
	object = ni_dbus_server_register_object(server, "Interface", &ni_dbus_anonymous_class, NULL);
	if (object == NULL)
		ni_fatal("Unable to create dbus object for interfaces");

	dhcp6_discover_devices(server);

	ni_dhcp6_set_event_handler(dhcp6_protocol_event);
}

/*
 * Add a newly discovered device
 */
static ni_bool_t
dhcp6_device_create(ni_dbus_server_t *server, const ni_netdev_t *ifp)
{
	ni_dhcp6_device_t *dev;

	dev = ni_dhcp6_device_new(ifp->name, &ifp->link);
	if (!dev) {
		ni_error("Cannot create dhcp device for %s", ifp->name);
		return FALSE;
	}

	ni_objectmodel_register_dhcp6_device(server, dev);
	ni_debug_dbus("Created device for %s", ifp->name);
	return TRUE;
}

/*
 * Remove a device that has disappeared
 */
static void
dhcp6_device_destroy(ni_dbus_server_t *server, const ni_netdev_t *ifp)
{
        ni_dhcp6_device_t *dev;

        ni_debug_dhcp("%s(%s, ifindex %d)", __func__, ifp->name, ifp->link.ifindex);
        if ((dev = ni_dhcp6_device_by_index(ifp->link.ifindex)) != NULL)
                ni_dbus_server_unregister_object(server, dev);
}

/*
 * Discover existing interfaces and create dhcp6 dbus devices
 */
static void
dhcp6_discover_devices(ni_dbus_server_t *server)
{
	ni_netconfig_t *nc;
	ni_netdev_t *	ifp;

	/* Disable wireless AP scanning */
	ni_wireless_set_scanning(FALSE);

	if (!(nc = ni_global_state_handle(1)))
		ni_fatal("cannot refresh interface list!");

	/* FIXME: for wireless devices, we should disable all the
	 * BSS discovery, it's not needed in the dhcp6 supplicant
	 */
	for (ifp = ni_netconfig_devlist(nc); ifp; ifp = ifp->next) {

		/* currently ether type only */
		if (ifp->link.arp_type != ARPHRD_ETHER)
			continue;

		(void)dhcp6_device_create(server, ifp);
	}
}

/*
 * Implement DHCP6 supplicant dbus service
 */
static void
dhcp6_supplicant(void)
{
	/* Initialize dbus server (com.suse.Wicked.DHCP6) */
	dhcp6_dbus_server = ni_server_listen_dbus(NI_OBJECTMODEL_DBUS_BUS_NAME_DHCP6);
	if (dhcp6_dbus_server == NULL)
		ni_fatal("Unable to initialize dbus server");

	dhcp6_register_services(dhcp6_dbus_server);

	/* open global RTNL socket to listen for kernel events */
	if (ni_server_listen_interface_events(dhcp6_interface_event) < 0)
		ni_fatal("unable to initialize netlink listener");

	if (!opt_foreground) {
		if (ni_server_background() < 0)
			ni_fatal("unable to background server");
		ni_log_destination_syslog("wickedd");
	}

	if (opt_recover_leases)
		dhcp6_recover_addrconf(opt_state_file);

	signal(SIGTERM, dhcp6_catch_term_signal);
	signal(SIGINT,  dhcp6_catch_term_signal);

	while (dhcp6_term_sig == 0) {
		long timeout;

		timeout = ni_timer_next_timeout();
		if (ni_socket_wait(timeout) < 0)
			ni_fatal("ni_socket_wait failed");
	}

	ni_debug_dhcp("caught signal %u, exiting", dhcp6_term_sig);
	/*
	ni_objectmodel_save_state(opt_state_file);
	*/

	exit(0);
}

/*
 * Remember signal for later handling
 */
static void
dhcp6_catch_term_signal(int sig)
{
	dhcp6_term_sig = sig;
}

/*
 * Recover lease information from the state.xml file.
 */
void
dhcp6_recover_addrconf(const char *filename)
{
	if (!ni_file_exists(filename)) {
		ni_debug_wicked("%s: %s does not exist, skip this", __func__, filename);
		return;
	}

	/* Recover the lease information of all interfaces. */
	if (!ni_objectmodel_recover_state(filename, NULL)) {
		ni_error("unable to recover dhcp6 state");
		return;
	}

	/* Now loop over all devices that have a request associated with them,
	 * and kickstart those.
	 */
	ni_dhcp6_restart();
}

static void
dhcp6_interface_event(ni_netdev_t *ifp, ni_event_t event)
{
	ni_netconfig_t *nc = ni_global_state_handle(0);
	ni_dhcp6_device_t *dev;
	ni_netdev_t *ofp;

	ni_debug_dhcp("Received event %d for interface %s [%u]",
			(int)event, ifp->name, ifp->link.ifindex);

        switch (event) {
        case NI_EVENT_DEVICE_CREATE:
        	/* check for duplicate ifindex */
        	ofp = ni_netdev_by_index(nc, ifp->link.ifindex);
        	if (ofp && ofp != ifp) {
        		ni_warn("duplicate ifindex in device-create event");
        		return;
        	}

        	/* Create dbus object */
        	dhcp6_device_create(dhcp6_dbus_server, ifp);
        break;

        case NI_EVENT_DEVICE_DELETE:
        	/* Delete dbus device object */
        	dhcp6_device_destroy(dhcp6_dbus_server, ifp);
        break;

        case NI_EVENT_LINK_DOWN:
        case NI_EVENT_LINK_UP:
        	dev = ni_dhcp6_device_by_index(ifp->link.ifindex);
        	if (dev != NULL)
        		ni_dhcp6_device_event(dev, event);
        break;

        case NI_EVENT_DEVICE_DOWN:
        	/* Someone has taken the interface down completely. Which means
		 * we shouldn't pretend we're still owning this device. So forget
		 * all leases and shut up. */
        	ni_debug_dhcp("device %s went down: discard any leases", ifp->name);
        	dev = ni_dhcp6_device_by_index(ifp->link.ifindex);
        	if (dev != NULL)
        		ni_dhcp6_device_stop(dev);
	break;

        default:
        break;
        }
}

static void
dhcp6_protocol_event(enum ni_dhcp6_event ev, const ni_dhcp6_device_t *dev, ni_addrconf_lease_t *lease)
{
	ni_dbus_variant_t argv[4];
	ni_dbus_object_t *dev_object;
	ni_dbus_variant_t *var;
	int argc = 0;

	ni_debug_dhcp("%s(ev=%u, dev=%d, uuid=%s)", __func__, ev, dev->link.ifindex,
			/* dev->config? ni_print_hex(dev->config->uuid.octets, 16) : */ "<none>");

	dev_object = ni_dbus_server_find_object_by_handle(dhcp6_dbus_server, dev);
	if (dev_object == NULL) {
		ni_warn("%s: no dbus object for device %s!", __func__,
				dev->ifname);
		return;
	}

	memset(argv, 0, sizeof(argv));

#if 0
	if (dev->config) {
		var = &argv[argc++];
		ni_dbus_variant_set_uuid(var, &dev->config->uuid);

		/* Make sure we copy the "update" flags to the lease; the
		 * server relies on us to provide this info */
		if (lease)
			lease->update = dev->config->update;
	}
#endif

	var = &argv[argc++];
	ni_dbus_variant_init_dict(var);
	if (lease) {
		if (!ni_objectmodel_get_addrconf_lease(lease, var)) {
			ni_warn("%s: could not extract lease data", __func__);
			goto done;
		}
	}

	switch (ev) {
	case NI_DHCP6_EVENT_ACQUIRED:
		if (lease == NULL) {
			ni_error("BUG: cannot send %s event without a lease handle",
					NI_OBJECTMODEL_LEASE_ACQUIRED_SIGNAL);
			goto done;
		}
		ni_dbus_server_send_signal(dhcp6_dbus_server, dev_object,
				NI_OBJECTMODEL_DHCP6_INTERFACE, NI_OBJECTMODEL_LEASE_ACQUIRED_SIGNAL,
				argc, argv);
		break;

	case NI_DHCP6_EVENT_RELEASED:
		ni_dbus_server_send_signal(dhcp6_dbus_server, dev_object,
				NI_OBJECTMODEL_DHCP6_INTERFACE, NI_OBJECTMODEL_LEASE_RELEASED_SIGNAL,
				argc, argv);
		break;

	case NI_DHCP6_EVENT_LOST:
		ni_dbus_server_send_signal(dhcp6_dbus_server, dev_object,
				NI_OBJECTMODEL_DHCP6_INTERFACE, NI_OBJECTMODEL_LEASE_LOST_SIGNAL,
				argc, argv);
		break;

	default:
		break;
	}

done:
	while (argc--)
		ni_dbus_variant_destroy(&argv[argc]);
}
