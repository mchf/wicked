/*
 * Serialize and deserialize XML definitions, according to a given schema.
 *
 * Copyright (C) 2012, Olaf Kirch <okir@suse.de>
 */

#include <limits.h>
#include <stdlib.h>
#include <wicked/logging.h>
#include <wicked/xml.h>
#include "dbus-common.h"
#include "xml-schema.h"
#include "util_priv.h"
#include "debug.h"

#include <wicked/netinfo.h>
#include "dbus-objects/model.h"

static void		ni_dbus_define_scalar_types(ni_xs_scope_t *);
static void		ni_dbus_define_xml_notations(void);
static int		ni_dbus_xml_register_methods(ni_dbus_service_t *, ni_xs_service_t *);

static dbus_bool_t	ni_dbus_serialize_xml(xml_node_t *, const ni_xs_type_t *, ni_dbus_variant_t *);
static dbus_bool_t	ni_dbus_serialize_xml_scalar(xml_node_t *, const ni_xs_type_t *, ni_dbus_variant_t *);
static dbus_bool_t	ni_dbus_serialize_xml_struct(xml_node_t *, const ni_xs_type_t *, ni_dbus_variant_t *);
static dbus_bool_t	ni_dbus_serialize_xml_array(xml_node_t *, const ni_xs_type_t *, ni_dbus_variant_t *);
static dbus_bool_t	ni_dbus_serialize_xml_dict(xml_node_t *, const ni_xs_type_t *, ni_dbus_variant_t *);
static dbus_bool_t	ni_dbus_deserialize_xml(ni_dbus_variant_t *, const ni_xs_type_t *, xml_node_t *);
static dbus_bool_t	ni_dbus_deserialize_xml_scalar(ni_dbus_variant_t *, const ni_xs_type_t *, xml_node_t *);
static dbus_bool_t	ni_dbus_deserialize_xml_struct(ni_dbus_variant_t *, const ni_xs_type_t *, xml_node_t *);
static dbus_bool_t	ni_dbus_deserialize_xml_array(ni_dbus_variant_t *, const ni_xs_type_t *, xml_node_t *);
static dbus_bool_t	ni_dbus_deserialize_xml_dict(ni_dbus_variant_t *, const ni_xs_type_t *, xml_node_t *);
static char *		__ni_xs_type_to_dbus_signature(const ni_xs_type_t *, char *, size_t);
static char *		ni_xs_type_to_dbus_signature(const ni_xs_type_t *);

ni_xs_scope_t *
ni_dbus_xml_init(void)
{
	ni_xs_scope_t *schema;

	schema = ni_xs_scope_new(NULL, "dbus");
	ni_dbus_define_scalar_types(schema);
	ni_dbus_define_xml_notations();

	return schema;
}

/*
 * Register all services defined by the schema
 */
int
ni_dbus_xml_register_services(ni_xs_scope_t *scope)
{
	ni_xs_service_t *xs_service;

	NI_TRACE_ENTER_ARGS("scope=%s", scope->name);
	for (xs_service = scope->services; xs_service; xs_service = xs_service->next) {
		ni_dbus_service_t *service;
		const ni_var_t *attr;
		int rv;

		service = xcalloc(1, sizeof(*service));
		ni_string_dup(&service->name, xs_service->interface);
		service->user_data = xs_service;

		ni_debug_dbus("register dbus service description %s", service->name);
		ni_objectmodel_register_service(service);

		/* An interface needs to be attached to an object. Specify which object class
		 * this can attach to. */
		if ((attr = ni_var_array_get(&xs_service->attributes, "object-class")) != NULL) {
			const ni_dbus_class_t *class;

			if ((class = ni_objectmodel_get_class(attr->value)) == NULL) {
				ni_error("xml service definition for %s: unknown object-class \"%s\"",
						xs_service->interface, attr->value);
			} else {
				service->compatible = class;
			}
		}

		if ((rv = ni_dbus_xml_register_methods(service, xs_service)) < 0)
			return rv;
	}

	return 0;
}

int
ni_dbus_xml_register_methods(ni_dbus_service_t *service, ni_xs_service_t *xs_service)
{
	ni_dbus_method_t *method_array, *method;
	unsigned int nmethods = 0;
	ni_xs_method_t *xs_method;

	if (xs_service->methods == NULL)
		return 0;

	for (xs_method = xs_service->methods; xs_method; xs_method = xs_method->next)
		nmethods++;
	service->methods = method_array = xcalloc(nmethods + 1, sizeof(ni_dbus_method_t));

	method = method_array;
	for (xs_method = xs_service->methods; xs_method; xs_method = xs_method->next) {
		char sigbuf[64];
		unsigned int i;

		/* Skip private methods such as __newlink */
		if (xs_method->name == 0 || xs_method->name[0] == '_')
			continue;

		/* First, build the method signature */
		sigbuf[0] = '\0';
		for (i = 0; i < xs_method->arguments.count; ++i) {
			ni_xs_type_t *type = xs_method->arguments.data[i].type;
			unsigned int k = strlen(sigbuf);

			if (!__ni_xs_type_to_dbus_signature(type, sigbuf + k, sizeof(sigbuf) - k)) {
				ni_error("bad definition of service %s method %s: "
					 "cannot build dbus signature of argument[%u] (%s)",
					 service->name, xs_method->name, i,
					 xs_method->arguments.data[i].name);
				return -1;
			}
		}

		ni_string_dup((char **) &method->name, xs_method->name);
		ni_string_dup((char **) &method->call_signature, sigbuf);
		method->handler = NULL; /* need to define */
		method->user_data = xs_method;

		method++;
	}

	return 0;
}

/*
 * Serialize XML rep of an argument to a dbus call
 */
dbus_bool_t
ni_dbus_xml_serialize_arg(const ni_dbus_method_t *method, unsigned int narg,
					ni_dbus_variant_t *var, xml_node_t *node)
{
	ni_xs_method_t *xs_method = method->user_data;
	ni_xs_type_t *xs_type;

	ni_assert(xs_method);
	if (narg >= xs_method->arguments.count)
		return FALSE;

	ni_debug_dbus("%s: serializing argument %u (%s)",
			method->name, narg, xs_method->arguments.data[narg].name);
	xs_type = xs_method->arguments.data[narg].type;

	return ni_dbus_serialize_xml(node, xs_type, var);
}

xml_node_t *
ni_dbus_xml_deserialize_arguments(const ni_dbus_method_t *method,
				unsigned int num_vars, ni_dbus_variant_t *vars,
				xml_node_t *parent)
{
	xml_node_t *node = xml_node_new("arguments", parent);
	ni_xs_method_t *xs_method = method->user_data;
	unsigned int i;

	for (i = 0; i < num_vars; ++i) {
		xml_node_t *arg = xml_node_new(xs_method->arguments.data[i].name, node);

		if (!ni_dbus_deserialize_xml(&vars[i], xs_method->arguments.data[i].type, arg)) {
			xml_node_free(node);
			return FALSE;
		}
	}

	return node;
}

/*
 * Convert an XML tree to a dbus data object for serialization
 */
dbus_bool_t
ni_dbus_serialize_xml(xml_node_t *node, const ni_xs_type_t *type, ni_dbus_variant_t *var)
{
	switch (type->class) {
		case NI_XS_TYPE_SCALAR:
			return ni_dbus_serialize_xml_scalar(node, type, var);

		case NI_XS_TYPE_STRUCT:
			return ni_dbus_serialize_xml_struct(node, type, var);

		case NI_XS_TYPE_ARRAY:
			return ni_dbus_serialize_xml_array(node, type, var);

		case NI_XS_TYPE_DICT:
			return ni_dbus_serialize_xml_dict(node, type, var);

		default:
			ni_error("unsupported xml type class %u", type->class);
			return FALSE;
	}

	return TRUE;
}

/*
 * Create XML from a dbus data object
 */
dbus_bool_t
ni_dbus_deserialize_xml(ni_dbus_variant_t *var, const ni_xs_type_t *type, xml_node_t *node)
{
	switch (type->class) {
	case NI_XS_TYPE_SCALAR:
		return ni_dbus_deserialize_xml_scalar(var, type, node);

	case NI_XS_TYPE_STRUCT:
		return ni_dbus_deserialize_xml_struct(var, type, node);

	case NI_XS_TYPE_ARRAY:
		return ni_dbus_deserialize_xml_array(var, type, node);

	case NI_XS_TYPE_DICT:
		return ni_dbus_deserialize_xml_dict(var, type, node);

	default:
		ni_error("unsupported xml type class %u", type->class);
		return FALSE;
	}

	return TRUE;
}

/*
 * XML -> dbus_variant conversion for scalars
 */
dbus_bool_t
ni_dbus_serialize_xml_scalar(xml_node_t *node, const ni_xs_type_t *type, ni_dbus_variant_t *var)
{
	ni_xs_scalar_info_t *scalar_info = ni_xs_scalar_info(type);

	if (scalar_info->constraint.bitmap) {
		const ni_intmap_t *bits = scalar_info->constraint.bitmap->bits;
		unsigned long value = 0;
		xml_node_t *child;

		for (child = node->children; child; child = child->next) {
			unsigned int bb;

			if (ni_parse_int_mapped(child->name, bits, &bb) < 0 || bb >= 32) {
				ni_warn("%s: ignoring unknown or bad bit value <%s>",
						xml_node_location(node), child->name);
				continue;
			}

			value |= 1 << bb;
		}

		if (!ni_dbus_variant_init_signature(var, ni_xs_type_to_dbus_signature(type)))
			return FALSE;
		return ni_dbus_variant_set_ulong(var, value);
	}

	if (node->cdata == NULL) {
		ni_error("unable to serialize node %s - no data", node->name);
		return FALSE;
	}

	/* TBD: handle constants defined in the schema? */
	if (!ni_dbus_variant_parse(var, node->cdata, ni_xs_type_to_dbus_signature(type))) {
		ni_error("unable to serialize node %s - cannot parse value", node->name);
		return FALSE;
	}

	return TRUE;
}

/*
 * XML from dbus variant for scalars
 */
dbus_bool_t
ni_dbus_deserialize_xml_scalar(ni_dbus_variant_t *var, const ni_xs_type_t *type, xml_node_t *node)
{
	ni_xs_scalar_info_t *scalar_info = ni_xs_scalar_info(type);
	const char *value;

	if (var->type == DBUS_TYPE_ARRAY) {
		ni_error("%s: expected a scalar, but got an array or dict", __func__);
		return FALSE;
	}

	if (scalar_info->constraint.bitmap) {
		const ni_intmap_t *bits = scalar_info->constraint.bitmap->bits;
		unsigned long value = 0;
		unsigned int bb;

		if (!ni_dbus_variant_get_ulong(var, &value))
			return FALSE;

		for (bb = 0; bb < 32; ++bb) {
			const char *bitname;

			if ((value & (1 << bb)) == 0)
				continue;

			if ((bitname = ni_format_int_mapped(bb, bits)) != NULL) {
				xml_node_new(bitname, node);
			} else {
				ni_warn("unable to represent bit%u in <%s>", bb, node->name);
			}
		}

		return TRUE;
	}

	if (!(value = ni_dbus_variant_sprint(var))) {
		ni_error("%s: unable to represent variable value as string", __func__);
		return FALSE;
	}

	/* FIXME: make sure we properly quote any xml meta characters */
	xml_node_set_cdata(node, value);
	return TRUE;
}

/*
 * Serialize an array
 */
dbus_bool_t
ni_dbus_serialize_xml_array(xml_node_t *node, const ni_xs_type_t *type, ni_dbus_variant_t *var)
{
	ni_xs_array_info_t *array_info = ni_xs_array_info(type);
	ni_xs_type_t *element_type = array_info->element_type;
	xml_node_t *child;

	if (array_info->notation) {
		const ni_xs_notation_t *notation = array_info->notation;
		ni_opaque_t data = NI_OPAQUE_INIT;

		/* For now, we handle only byte arrays */
		if (notation->array_element_type != DBUS_TYPE_BYTE) {
			ni_error("%s: cannot handle array notation \"%s\"", __func__, notation->name);
			return FALSE;
		}
		if (node->cdata == NULL) {
			ni_error("%s: array not compatible with notation \"%s\"", __func__, notation->name);
			return FALSE;
		}
		if (!notation->parse(node->cdata, &data)) {
			ni_error("%s: cannot parse array with notation \"%s\"", __func__, notation->name);
			return FALSE;
		}
		ni_dbus_variant_set_byte_array(var, data.data, data.len);
		return TRUE;
	}

	if (!ni_dbus_variant_init_signature(var, ni_xs_type_to_dbus_signature(type)))
		return FALSE;

	for (child = node->children; child; child = child->next) {
		if (element_type->class == NI_XS_TYPE_SCALAR) {
			if (child->cdata == NULL) {
				ni_error("%s: NULL array element",__func__);
				return FALSE;
			}

			/* TBD: handle constants defined in the schema? */
			if (!ni_dbus_variant_array_parse_and_append_string(var, child->cdata)) {
				ni_error("%s: syntax error in array element",__func__);
				return FALSE;
			}
		} else {
			ni_error("%s: arrays of type %s not implemented yet", __func__, ni_xs_type_to_dbus_signature(element_type));
			return FALSE;
		}
	}

	return TRUE;
}

/*
 * XML from dbus variant for arrays
 */
dbus_bool_t
ni_dbus_deserialize_xml_array(ni_dbus_variant_t *var, const ni_xs_type_t *type, xml_node_t *node)
{
	ni_xs_array_info_t *array_info = ni_xs_array_info(type);
	ni_xs_type_t *element_type = array_info->element_type;
	unsigned int i, array_len;

	array_len = var->array.len;
	if (array_info->notation) {
		const ni_xs_notation_t *notation = array_info->notation;
		ni_opaque_t data = NI_OPAQUE_INIT;
		char buffer[256];

		/* For now, we handle only byte arrays */
		if (notation->array_element_type != DBUS_TYPE_BYTE) {
			ni_error("%s: cannot handle array notation \"%s\"", __func__, notation->name);
			return FALSE;
		}

		if (!ni_dbus_variant_is_byte_array(var)) {
			ni_error("%s: expected byte array, but got something else", __func__);
			return FALSE;
		}
		if (array_len > sizeof(data.data)) {
			ni_error("%s: cannot extract data from byte array - too long (len=%u)", __func__, var->array.len);
			return FALSE;
		}

		ni_opaque_set(&data, var->byte_array_value, array_len);
		if (!notation->print(&data, buffer, sizeof(buffer))) {
			ni_error("%s: cannot represent array with notation \"%s\"", __func__, notation->name);
			return FALSE;
		}
		xml_node_set_cdata(node, buffer);
		return TRUE;
	}

	if (element_type->class == NI_XS_TYPE_SCALAR) {
		/* An array of non-scalars always wraps each element in a variant */
		if (var->array.element_type == DBUS_TYPE_VARIANT) {
			ni_error("%s: expected an array of scalars, but got an array of variants",
					__func__);
			return FALSE;
		}

		for (i = 0; i < array_len; ++i) {
			const char *string;
			xml_node_t *child;

			if (!(string = ni_dbus_variant_array_print_element(var, i))) {
				ni_error("%s: cannot represent array element", __func__);
				return FALSE;
			}
			child = xml_node_new("e", node);
			xml_node_set_cdata(child, string);
		}
	} else {
		ni_error("%s: arrays of type %s not implemented yet", __func__, ni_xs_type_to_dbus_signature(element_type));
		return FALSE;
	}

	return TRUE;
}

/*
 * Serialize a dict
 */
dbus_bool_t
ni_dbus_serialize_xml_dict(xml_node_t *node, const ni_xs_type_t *type, ni_dbus_variant_t *dict)
{
	ni_xs_dict_info_t *dict_info = ni_xs_dict_info(type);
	xml_node_t *child;

	ni_assert(dict_info);

	ni_dbus_variant_init_dict(dict);
	for (child = node->children; child; child = child->next) {
		const ni_xs_type_t *child_type = ni_xs_dict_info_find(dict_info, child->name);
		ni_dbus_variant_t *child_var;

		if (child_type == NULL) {
			ni_warn("%s: ignoring unknown dict element \"%s\"", __func__, child->name);
			continue;
		}
		child_var = ni_dbus_dict_add(dict, child->name);
		if (!ni_dbus_serialize_xml(child, child_type, child_var))
			return FALSE;
	}
	return TRUE;
}

/*
 * Deserialize a dict
 */
dbus_bool_t
ni_dbus_deserialize_xml_dict(ni_dbus_variant_t *var, const ni_xs_type_t *type, xml_node_t *node)
{
	ni_xs_dict_info_t *dict_info = ni_xs_dict_info(type);
	ni_dbus_dict_entry_t *entry;
	unsigned int i;

	if (!ni_dbus_variant_is_dict(var)) {
		ni_error("unable to deserialize %s: expected a dict", node->name);
		return FALSE;
	}

	entry = var->dict_array_value;
	for (i = 0; i < var->array.len; ++i, ++entry) {
		const ni_xs_type_t *child_type;
		xml_node_t *child;

		/* Silently ignore dict entries we have no schema information for */
		if (!(child_type = ni_xs_dict_info_find(dict_info, entry->key))) {
			ni_debug_dbus("%s: ignoring unknown dict entry %s in node <%s>",
					__func__, entry->key, node->name);
			continue;
		}

		child = xml_node_new(entry->key, node);
		if (!ni_dbus_deserialize_xml(&entry->datum, child_type, child))
			return FALSE;
	}
	return TRUE;
}

/*
 * Serialize a struct
 */
dbus_bool_t
ni_dbus_serialize_xml_struct(xml_node_t *node, const ni_xs_type_t *type, ni_dbus_variant_t *var)
{
	ni_error("%s: not implemented yet", __func__);
	return FALSE;
}

static dbus_bool_t
ni_dbus_deserialize_xml_struct(ni_dbus_variant_t *var, const ni_xs_type_t *type, xml_node_t *node)
{
	ni_error("%s: not implemented yet", __func__);
	return FALSE;
}

/*
 * Get the dbus signature of a dbus-xml type
 */
static char *
__ni_xs_type_to_dbus_signature(const ni_xs_type_t *type, char *sigbuf, size_t buflen)
{
	ni_xs_scalar_info_t *scalar_info;
	ni_xs_array_info_t *array_info;
	unsigned int i = 0;

	ni_assert(buflen >= 2);
	switch (type->class) {
	case NI_XS_TYPE_SCALAR:
		scalar_info = ni_xs_scalar_info(type);
		sigbuf[i++] = scalar_info->type;
		sigbuf[i++] = '\0';
		break;

	case NI_XS_TYPE_ARRAY:
		array_info = ni_xs_array_info(type);
		sigbuf[i++] = DBUS_TYPE_ARRAY;

		/* Arrays of non-scalar types always wrap each element into a VARIANT */
		if (array_info->element_type->class != NI_XS_TYPE_SCALAR)
			sigbuf[i++] = DBUS_TYPE_VARIANT;

		if (!__ni_xs_type_to_dbus_signature(array_info->element_type, sigbuf + i, buflen - i))
			return NULL;
		break;

	case NI_XS_TYPE_DICT:
		ni_assert(buflen >= sizeof(NI_DBUS_DICT_SIGNATURE));
		strcpy(sigbuf, NI_DBUS_DICT_SIGNATURE);
		break;

	default:
		return NULL;

	}
	return sigbuf;
}

static char *
ni_xs_type_to_dbus_signature(const ni_xs_type_t *type)
{
	static char sigbuf[32];

	return __ni_xs_type_to_dbus_signature(type, sigbuf, sizeof(sigbuf));
}

/*
 * Scalar types for dbus xml
 */
static void
ni_dbus_define_scalar_types(ni_xs_scope_t *typedict)
{
	static struct dbus_xml_type {
		const char *	name;
		unsigned int	dbus_type;
	} dbus_xml_types[] = {
		{ "boolean",	DBUS_TYPE_BOOLEAN },
		{ "byte",	DBUS_TYPE_BYTE },
		{ "string",	DBUS_TYPE_STRING },
		{ "double",	DBUS_TYPE_DOUBLE },
		{ "uint16",	DBUS_TYPE_UINT16 },
		{ "uint32",	DBUS_TYPE_UINT32 },
		{ "uint64",	DBUS_TYPE_UINT64 },
		{ "int16",	DBUS_TYPE_INT16 },
		{ "int32",	DBUS_TYPE_INT32 },
		{ "int64",	DBUS_TYPE_INT64 },
		{ "object-path",DBUS_TYPE_OBJECT_PATH },

		{ NULL }
	}, *tp;

	for (tp = dbus_xml_types; tp->name; ++tp)
		ni_xs_scope_typedef(typedict, tp->name, ni_xs_scalar_new(tp->dbus_type));
}

/*
 * Array notations
 */
#include <netinet/in.h>
#include <arpa/inet.h>

static ni_opaque_t *
ni_parse_ipv4_opaque(const char *string_value, ni_opaque_t *data)
{
	struct in_addr addr;

	if (inet_pton(AF_INET, string_value, &addr) != 1)
		return NULL;
	memcpy(data->data, &addr, sizeof(addr));
	data->len = sizeof(addr);
	return data;
}

static ni_opaque_t *
ni_parse_ipv6_opaque(const char *string_value, ni_opaque_t *data)
{
	struct in6_addr addr;

	if (inet_pton(AF_INET6, string_value, &addr) != 1)
		return NULL;
	memcpy(data->data, &addr, sizeof(addr));
	data->len = sizeof(addr);
	return data;
}

static ni_opaque_t *
ni_parse_hwaddr_opaque(const char *string_value, ni_opaque_t *data)
{
	int len;

	len = ni_parse_hex(string_value, data->data, sizeof(data->data));
	if (len < 0)
		return NULL;
	data->len = len;
	return data;
}

static ni_xs_notation_t	__ni_dbus_notations[] = {
	{
		.name = "ipv4addr",
		.array_element_type = DBUS_TYPE_BYTE,
		.parse = ni_parse_ipv4_opaque
	}, {
		.name = "ipv6addr",
		.array_element_type = DBUS_TYPE_BYTE,
		.parse = ni_parse_ipv6_opaque
	}, {
		.name = "hwaddr",
		.array_element_type = DBUS_TYPE_BYTE,
		.parse = ni_parse_hwaddr_opaque
	},

	{ NULL }
};

void
ni_dbus_define_xml_notations(void)
{
	ni_xs_notation_t *na;

	for (na = __ni_dbus_notations; na->name; ++na)
		ni_xs_register_array_notation(na);
}