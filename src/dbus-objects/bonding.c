/*
 * DBus encapsulation for bonding interfaces
 *
 * Copyright (C) 2011 Olaf Kirch <okir@suse.de>
 */

#include <sys/poll.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <getopt.h>
#include <errno.h>

#include <wicked/netinfo.h>
#include <wicked/logging.h>
#include <wicked/system.h>
#include <wicked/bonding.h>
#include <wicked/dbus-errors.h>
#include "dbus-common.h"
#include "model.h"
#include "debug.h"


/*
 * Create a new bonding interface
 */
ni_dbus_object_t *
ni_objectmodel_new_bond(ni_dbus_server_t *server, const ni_dbus_object_t *config, DBusError *error)
{
	ni_interface_t *cfg_ifp = ni_dbus_object_get_handle(config);
	ni_netconfig_t *nc = ni_global_state_handle(0);
	ni_interface_t *new_ifp;
	const ni_bonding_t *bond;
	int rv;

	bond = ni_interface_get_bonding(cfg_ifp);

	cfg_ifp->link.type = NI_IFTYPE_BOND;
	if (cfg_ifp->name == NULL) {
		static char namebuf[64];
		unsigned int num;

		for (num = 0; num < 65536; ++num) {
			snprintf(namebuf, sizeof(namebuf), "bond%u", num);
			if (!ni_interface_by_name(nc, namebuf)) {
				ni_string_dup(&cfg_ifp->name, namebuf);
				break;
			}
		}

		if (cfg_ifp->name == NULL) {
			dbus_set_error(error, DBUS_ERROR_FAILED,
					"Unable to create bonding interface - too many interfaces");
			return NULL;
		}
	}

	if ((rv = ni_system_bond_create(nc, cfg_ifp->name, bond, &new_ifp)) < 0) {
		dbus_set_error(error,
				DBUS_ERROR_FAILED,
				"Unable to create bonding interface: %s",
				ni_strerror(rv));
		return NULL;
	}

	if (new_ifp->link.type != NI_IFTYPE_BOND) {
		dbus_set_error(error,
				DBUS_ERROR_FAILED,
				"Unable to create bonding interface: new interface is of type %s",
				ni_linktype_type_to_name(new_ifp->link.type));
		return NULL;
	}

	return ni_objectmodel_register_interface(server, new_ifp);
}

/*
 * Bonding.delete method
 */
static dbus_bool_t
__ni_objectmodel_delete_bond(ni_dbus_object_t *object, const ni_dbus_method_t *method,
			unsigned int argc, const ni_dbus_variant_t *argv,
			ni_dbus_message_t *reply, DBusError *error)
{
	ni_netconfig_t *nc = ni_global_state_handle(0);
	ni_interface_t *ifp = object->handle;

	NI_TRACE_ENTER_ARGS("ifp=%s", ifp->name);
	if (ni_system_bond_delete(nc, ifp) < 0) {
		dbus_set_error(error, DBUS_ERROR_FAILED,
				"Error deleting bonding interface", ifp->name);
		return FALSE;
	}

	/* FIXME: destroy the object */
	ni_dbus_object_free(object);

	return TRUE;
}


/*
 * Helper function to obtain bonding config from dbus object
 */
static ni_bonding_t *
__ni_objectmodel_get_bonding(const ni_dbus_object_t *object, DBusError *error)
{
	ni_interface_t *ifp;
	ni_bonding_t *bond;

	if (!(ifp = ni_objectmodel_unwrap_interface(object, error)))
		return NULL;

	if (!(bond = ni_interface_get_bonding(ifp))) {
		dbus_set_error(error, DBUS_ERROR_FAILED, "Error getting bonding handle for interface");
		return NULL;
	}
	return bond;
}

static void *
ni_objectmodel_get_bonding(const ni_dbus_object_t *object, DBusError *error)
{
	return __ni_objectmodel_get_bonding(object, error);
}

/*
 * Get/set MII monitoring info
 */
static dbus_bool_t
__ni_objectmodel_bonding_get_miimon(const ni_dbus_object_t *object,
				const ni_dbus_property_t *property,
				ni_dbus_variant_t *result,
				DBusError *error)
{
	ni_bonding_t *bond;

	if (!(bond = __ni_objectmodel_get_bonding(object, error)))
		return FALSE;

	if (bond->monitoring != NI_BOND_MONITOR_MII) {
		dbus_set_error(error, NI_DBUS_ERROR_PROPERTY_NOT_PRESENT,
				"%s property %s not set",
				object->path, property->name);
		return FALSE;
	}

	ni_dbus_dict_add_uint32(result, "frequency", bond->miimon.frequency);
	ni_dbus_dict_add_uint32(result, "updelay", bond->miimon.updelay);
	ni_dbus_dict_add_uint32(result, "downdelay", bond->miimon.downdelay);
	ni_dbus_dict_add_uint32(result, "carrier-detect", bond->miimon.carrier_detect);
	return TRUE;
#if 0
	ni_dbus_variant_init_dict(result);
	ni_dbus_dict_add_uint32(result, "mode", bond->monitoring);
	switch (bond->monitoring) {
	case NI_BOND_MONITOR_ARP:
		ni_dbus_dict_add_uint32(result, "arp-interval", bond->arpmon.interval);
		ni_dbus_dict_add_uint32(result, "arp-validate", bond->arpmon.validate);
		var = ni_dbus_dict_add(result, "arp-targets");
		ni_dbus_variant_set_string_array(var,
				(const char **) bond->arpmon.targets.data,
				bond->arpmon.targets.count);
		break;

	case NI_BOND_MONITOR_MII:
		break;
	}
	return TRUE;
#endif
}

static dbus_bool_t
__ni_objectmodel_bonding_set_miimon(ni_dbus_object_t *object,
				const ni_dbus_property_t *property,
				const ni_dbus_variant_t *result,
				DBusError *error)
{
	ni_bonding_t *bond;

	if (!(bond = __ni_objectmodel_get_bonding(object, error)))
		return FALSE;

	bond->monitoring = NI_BOND_MONITOR_MII;

	ni_dbus_dict_get_uint32(result, "frequency", &bond->miimon.frequency);
	ni_dbus_dict_get_uint32(result, "updelay", &bond->miimon.updelay);
	ni_dbus_dict_get_uint32(result, "downdelay", &bond->miimon.downdelay);
	ni_dbus_dict_get_uint32(result, "carrier-detect", &bond->miimon.carrier_detect);
	return TRUE;
}

/*
 * Get/set ARP monitoring info
 */
static dbus_bool_t
__ni_objectmodel_bonding_get_arpmon(const ni_dbus_object_t *object,
				const ni_dbus_property_t *property,
				ni_dbus_variant_t *result,
				DBusError *error)
{
	ni_bonding_t *bond;
	ni_dbus_variant_t *var;

	if (!(bond = __ni_objectmodel_get_bonding(object, error)))
		return FALSE;

	if (bond->monitoring != NI_BOND_MONITOR_ARP) {
		dbus_set_error(error, NI_DBUS_ERROR_PROPERTY_NOT_PRESENT,
				"%s property %s not set",
				object->path, property->name);
		return FALSE;
	}

	ni_dbus_dict_add_uint32(result, "interval", bond->arpmon.interval);
	ni_dbus_dict_add_uint32(result, "validate", bond->arpmon.validate);
	var = ni_dbus_dict_add(result, "targets");
	ni_dbus_variant_set_string_array(var,
			(const char **) bond->arpmon.targets.data,
			bond->arpmon.targets.count);
	return TRUE;
}

static dbus_bool_t
__ni_objectmodel_bonding_set_arpmon(ni_dbus_object_t *object,
				const ni_dbus_property_t *property,
				const ni_dbus_variant_t *result,
				DBusError *error)
{
	ni_bonding_t *bond;
	ni_dbus_variant_t *var;

	if (!(bond = __ni_objectmodel_get_bonding(object, error)))
		return FALSE;

	bond->monitoring = NI_BOND_MONITOR_ARP;
	ni_dbus_dict_get_uint32(result, "interval", &bond->arpmon.interval);
	ni_dbus_dict_get_uint32(result, "validate", &bond->arpmon.validate);
	if ((var = ni_dbus_dict_get(result, "targets")) != NULL) {
		unsigned int i;

		if (!ni_dbus_variant_is_string_array(var)) {
			dbus_set_error(error, DBUS_ERROR_INVALID_ARGS,
				"%s.%s property - expected string array for attribute targets",
				object->path, property->name);
			return FALSE;
		}

		for (i = 0; i < var->array.len; ++i) {
			const char *s = var->string_array_value[i];

			ni_string_array_append(&bond->arpmon.targets, s);
		}
	}

	return TRUE;
}

/*
 * Get/set the list of slaves
 */
static dbus_bool_t
__ni_objectmodel_bonding_get_slaves(const ni_dbus_object_t *object,
				const ni_dbus_property_t *property,
				ni_dbus_variant_t *result,
				DBusError *error)
{
	ni_bonding_t *bond;
	unsigned int i;

	if (!(bond = __ni_objectmodel_get_bonding(object, error)))
		return FALSE;

	ni_dbus_dict_array_init(result);
	for (i = 0; i < bond->slave_names.count; ++i) {
		const char *slave_name = bond->slave_names.data[i];
		ni_dbus_variant_t *slave;

		slave = ni_dbus_dict_array_add(result);

		ni_dbus_dict_add_string(slave, "device", slave_name);
		if (bond->primary && ni_string_eq(bond->primary, slave_name))
			ni_dbus_dict_add_bool(slave, "primary", TRUE);
	}

	return TRUE;
}

static dbus_bool_t
__ni_objectmodel_bonding_set_slaves(ni_dbus_object_t *object,
				const ni_dbus_property_t *property,
				const ni_dbus_variant_t *result,
				DBusError *error)
{
	ni_bonding_t *bond;
	ni_dbus_variant_t *var;
	unsigned int i;

	if (!(bond = __ni_objectmodel_get_bonding(object, error)))
		return FALSE;

	if (!ni_dbus_variant_is_dict_array(result)) {
		dbus_set_error(error, DBUS_ERROR_INVALID_ARGS,
				"%s.%s property - expected dict array",
				object->path, property->name);
		return FALSE;
	}

	ni_string_free(&bond->primary);
	for (i = 0, var = result->variant_array_value; i < result->array.len; ++i, ++var) {
		dbus_bool_t is_primary = FALSE;
		const char *slave_name;

		if (!ni_dbus_dict_get_string(var, "device", &slave_name)) {
			dbus_set_error(error, DBUS_ERROR_INVALID_ARGS,
					"%s.%s property - missing device attribute",
					object->path, property->name);
			return FALSE;
		}
		ni_string_array_append(&bond->slave_names, slave_name);

		if (ni_dbus_dict_get_bool(var, "primary", &is_primary) && is_primary) {
			if (bond->primary) {
				dbus_set_error(error, DBUS_ERROR_INVALID_ARGS,
					"%s.%s property - duplicate primary device",
					object->path, property->name);
				return FALSE;
			}
			ni_string_dup(&bond->primary, slave_name);
		}
	}
	return TRUE;
}

#define BONDING_INT_PROPERTY(dbus_name, member_name, rw) \
	NI_DBUS_GENERIC_INT_PROPERTY(bonding, dbus_name, member_name, rw)
#define BONDING_UINT_PROPERTY(dbus_name, member_name, rw) \
	NI_DBUS_GENERIC_UINT_PROPERTY(bonding, dbus_name, member_name, rw)
#define BONDING_STRING_ARRAY_PROPERTY(dbus_name, member_name, rw) \
	NI_DBUS_GENERIC_STRING_ARRAY_PROPERTY(bonding, dbus_name, member_name, rw)

static ni_dbus_property_t	ni_objectmodel_bond_monitor_properties[] = {
	BONDING_UINT_PROPERTY(mode, mode, RO),

	/* If mode == NI_BOND_MONITOR_ARP */
	BONDING_UINT_PROPERTY(arp-interval, arpmon.interval, RO),
	BONDING_UINT_PROPERTY(arp-validate, arpmon.validate, RO),
	BONDING_STRING_ARRAY_PROPERTY(arp-targets, arpmon.targets, RO),

	/* If mode == NI_BOND_MONITOR_MII */
	BONDING_UINT_PROPERTY(mii-frequency, miimon.frequency, RO),
	BONDING_UINT_PROPERTY(mii-updelay, miimon.updelay, RO),
	BONDING_UINT_PROPERTY(mii-downdelay, miimon.downdelay, RO),
	BONDING_UINT_PROPERTY(mii-carrier-detect, miimon.carrier_detect, RO),

	{ NULL }
};
static ni_dbus_property_t	ni_objectmodel_bond_properties[] = {
	BONDING_UINT_PROPERTY(mode, mode, RO),

	__NI_DBUS_PROPERTY(
			DBUS_TYPE_ARRAY_AS_STRING NI_DBUS_DICT_SIGNATURE,
			slaves, __ni_objectmodel_bonding, RO),
	__NI_DBUS_PROPERTY(
			NI_DBUS_DICT_SIGNATURE,
			miimon, __ni_objectmodel_bonding, RO),
	__NI_DBUS_PROPERTY(
			NI_DBUS_DICT_SIGNATURE,
			arpmon, __ni_objectmodel_bonding, RO),

	NI_DBUS_GENERIC_DICT_PROPERTY(monitor, ni_objectmodel_bond_monitor_properties, RO),
	{ NULL }
};


static ni_dbus_method_t		ni_objectmodel_bond_methods[] = {
	{ "delete",		"",		__ni_objectmodel_delete_bond },
#if 0
	{ "addSlave",		DBUS_TYPE_OJECT_AS_STRING,	__ni_objectmodel_bond_add_slave },
	{ "removeSlave",	DBUS_TYPE_OJECT_AS_STRING,	__ni_objectmodel_bond_remove_slave },
#endif
	{ NULL }
};

ni_dbus_service_t	wicked_dbus_bond_service = {
	.name = WICKED_DBUS_BONDING_INTERFACE,
	.methods = ni_objectmodel_bond_methods,
	.properties = ni_objectmodel_bond_properties,
};

