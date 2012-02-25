/*
 * dbus encapsulation for ethernet interfaces
 *
 * Copyright (C) 2011, 2012 Olaf Kirch <okir@suse.de>
 */

#include <wicked/netinfo.h>
#include <wicked/logging.h>
#include <wicked/ethernet.h>
#include <wicked/system.h>
#include <wicked/dbus-errors.h>
#include "dbus-common.h"
#include "model.h"

#include <wicked/ethernet.h>

static ni_interface_t *	__ni_objectmodel_ethernet_device_arg(const ni_dbus_variant_t *);

/*
 * Ethernet.changeDevice method
 */
static dbus_bool_t
ni_objectmodel_ethernet_setup(ni_dbus_object_t *object, const ni_dbus_method_t *method,
			unsigned int argc, const ni_dbus_variant_t *argv,
			ni_dbus_message_t *reply, DBusError *error)
{
	ni_netconfig_t *nc = ni_global_state_handle(0);
	ni_interface_t *ifp, *cfg;
	dbus_bool_t rv = FALSE;

	/* we've already checked that argv matches our signature */
	ni_assert(argc == 1);

	if (!(ifp = ni_objectmodel_unwrap_interface(object, error)))
		return FALSE;

	if (!(cfg = __ni_objectmodel_ethernet_device_arg(&argv[0]))) {
		ni_dbus_error_invalid_args(error, object->path, method->name);
		goto out;
	}

	if (ni_system_ethernet_setup(nc, ifp, cfg->ethernet) < 0) {
		dbus_set_error(error, DBUS_ERROR_FAILED, "failed to set up ethernet device");
		goto out;
	}

	rv = TRUE;

out:
	if (cfg)
		ni_interface_put(cfg);
	return rv;
}

/*
 * Common helper function to extract bonding device info from a dbus dict
 */
static ni_interface_t *
__ni_objectmodel_ethernet_device_arg(const ni_dbus_variant_t *dict)
{
	ni_dbus_object_t *dev_object;
	ni_interface_t *dev;
	dbus_bool_t rv;

	dev = ni_interface_new(NULL, NULL, 0);
	dev->link.type = NI_IFTYPE_ETHERNET;

	dev_object = ni_objectmodel_wrap_interface(dev);
	rv = ni_dbus_object_set_properties_from_dict(dev_object, &ni_objectmodel_ethernet_service, dict);
	ni_dbus_object_free(dev_object);

	if (!rv) {
		ni_interface_put(dev);
		dev = NULL;
	}
	return dev;
}

/*
 * Functions for dealing wit Ethernet properties
 */
void *
ni_objectmodel_get_ethernet(const ni_dbus_object_t *object, DBusError *error)
{
	ni_interface_t *ifp;
	ni_ethernet_t *eth;

	if (!(ifp = ni_objectmodel_unwrap_interface(object, error)))
		return NULL;

	if (!(eth = ni_interface_get_ethernet(ifp))) {
		dbus_set_error(error, DBUS_ERROR_FAILED, "Error getting ethernet handle for interface");
		return NULL;
	}
	return eth;
}

static dbus_bool_t
__ni_objectmodel_ethernet_get_address(const ni_dbus_object_t *object,
				const ni_dbus_property_t *property,
				ni_dbus_variant_t *result,
				DBusError *error)
{
	ni_interface_t *dev;

	if (!(dev = ni_objectmodel_unwrap_interface(object, error)))
		return FALSE;
	ni_dbus_variant_set_byte_array(result, dev->link.hwaddr.data, dev->link.hwaddr.len);
	return TRUE;
}

static dbus_bool_t
__ni_objectmodel_ethernet_set_address(ni_dbus_object_t *object,
				const ni_dbus_property_t *property,
				const ni_dbus_variant_t *argument,
				DBusError *error)
{
	ni_interface_t *dev;
	unsigned int len;

	if (!(dev = ni_objectmodel_unwrap_interface(object, error)))
		return FALSE;

	if (!ni_dbus_variant_get_byte_array_minmax(argument, dev->link.hwaddr.data, &len,
					0, sizeof(dev->link.hwaddr.data)))
		return FALSE;

	dev->link.hwaddr.len = len;
	return TRUE;
}

static dbus_bool_t
__ni_objectmodel_ethernet_get_offload(const ni_dbus_object_t *object,
				const ni_dbus_property_t *property,
				ni_dbus_variant_t *result,
				DBusError *error)
{
	ni_ethernet_t *eth;
	
	if (!(eth = ni_objectmodel_get_ethernet(object, error)))
		return FALSE;

	if (eth->offload.rx_csum != NI_ETHERNET_SETTING_DEFAULT)
		ni_dbus_dict_add_uint32(result, "rx-csum", eth->offload.rx_csum);
	if (eth->offload.tx_csum != NI_ETHERNET_SETTING_DEFAULT)
		ni_dbus_dict_add_uint32(result, "tx-csum", eth->offload.tx_csum);
	if (eth->offload.scatter_gather != NI_ETHERNET_SETTING_DEFAULT)
		ni_dbus_dict_add_uint32(result, "scatter-gather", eth->offload.scatter_gather);
	if (eth->offload.tso != NI_ETHERNET_SETTING_DEFAULT)
		ni_dbus_dict_add_uint32(result, "tso", eth->offload.tso);
	if (eth->offload.ufo != NI_ETHERNET_SETTING_DEFAULT)
		ni_dbus_dict_add_uint32(result, "ufo", eth->offload.ufo);
	if (eth->offload.gso != NI_ETHERNET_SETTING_DEFAULT)
		ni_dbus_dict_add_uint32(result, "gso", eth->offload.gso);
	if (eth->offload.gro != NI_ETHERNET_SETTING_DEFAULT)
		ni_dbus_dict_add_uint32(result, "gro", eth->offload.gro);
	if (eth->offload.lro != NI_ETHERNET_SETTING_DEFAULT)
		ni_dbus_dict_add_uint32(result, "lro", eth->offload.lro);

	return TRUE;
}

static dbus_bool_t
__ni_objectmodel_ethernet_set_offload(ni_dbus_object_t *object,
				const ni_dbus_property_t *property,
				const ni_dbus_variant_t *argument,
				DBusError *error)
{
	ni_ethernet_t *eth;
	
	if (!(eth = ni_objectmodel_get_ethernet(object, error)))
		return FALSE;

	if (!ni_dbus_variant_is_dict(argument))
		return FALSE;

	if (!ni_dbus_dict_get_uint32(argument, "rx-csum", &eth->offload.rx_csum))
		eth->offload.rx_csum = NI_ETHERNET_SETTING_DEFAULT;
	if (!ni_dbus_dict_get_uint32(argument, "tx-csum", &eth->offload.tx_csum))
		eth->offload.tx_csum = NI_ETHERNET_SETTING_DEFAULT;
	if (!ni_dbus_dict_get_uint32(argument, "scatter-gather", &eth->offload.scatter_gather))
		eth->offload.scatter_gather = NI_ETHERNET_SETTING_DEFAULT;
	if (!ni_dbus_dict_get_uint32(argument, "tso", &eth->offload.tso))
		eth->offload.tso = NI_ETHERNET_SETTING_DEFAULT;
	if (!ni_dbus_dict_get_uint32(argument, "ufo", &eth->offload.ufo))
		eth->offload.ufo = NI_ETHERNET_SETTING_DEFAULT;
	if (!ni_dbus_dict_get_uint32(argument, "gso", &eth->offload.gso))
		eth->offload.gso = NI_ETHERNET_SETTING_DEFAULT;
	if (!ni_dbus_dict_get_uint32(argument, "gro", &eth->offload.gro))
		eth->offload.gro = NI_ETHERNET_SETTING_DEFAULT;
	if (!ni_dbus_dict_get_uint32(argument, "lro", &eth->offload.lro))
		eth->offload.lro = NI_ETHERNET_SETTING_DEFAULT;

	return TRUE;
}



#define ETHERNET_UINT_PROPERTY(dbus_name, member_name, rw) \
	NI_DBUS_GENERIC_UINT_PROPERTY(ethernet, dbus_name, member_name, rw)

const ni_dbus_property_t	ni_objectmodel_ethernet_property_table[] = {
	ETHERNET_UINT_PROPERTY(link-speed, link_speed, RO),
	ETHERNET_UINT_PROPERTY(port-type, port_type, RO),
	ETHERNET_UINT_PROPERTY(duplex, duplex, RO),
	ETHERNET_UINT_PROPERTY(autoneg-enable, autoneg_enable, RO),

	__NI_DBUS_PROPERTY(
			DBUS_TYPE_ARRAY_AS_STRING DBUS_TYPE_BYTE_AS_STRING,
			address, __ni_objectmodel_ethernet, RO),
	__NI_DBUS_PROPERTY(
			NI_DBUS_DICT_SIGNATURE,
			offload, __ni_objectmodel_ethernet, RO),

	{ NULL }
};

static ni_dbus_method_t		ni_objectmodel_ethernet_methods[] = {
	{ "changeDevice",	"",			ni_objectmodel_ethernet_setup },
	{ NULL }
};

ni_dbus_service_t	ni_objectmodel_ethernet_service = {
	.name		= WICKED_DBUS_ETHERNET_INTERFACE,
	.methods	= ni_objectmodel_ethernet_methods,
	.properties	= ni_objectmodel_ethernet_property_table,
};

