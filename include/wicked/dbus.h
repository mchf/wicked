/*
 * Common DBus types and functions
 *
 * Copyright (C) 2011-2012 Olaf Kirch <okir@suse.de>
 */


#ifndef __WICKED_DBUS_H__
#define __WICKED_DBUS_H__

#include <dbus/dbus.h>
#include <wicked/types.h>
#include <wicked/util.h>

typedef struct DBusMessage	ni_dbus_message_t;
typedef struct ni_dbus_connection ni_dbus_connection_t;
typedef struct ni_dbus_object	ni_dbus_object_t;
typedef struct ni_dbus_service	ni_dbus_service_t;
typedef struct ni_dbus_class	ni_dbus_class_t;
typedef struct ni_dbus_server_object ni_dbus_server_object_t;
typedef struct ni_dbus_client_object ni_dbus_client_object_t;
typedef struct ni_dbus_dict_entry ni_dbus_dict_entry_t;

typedef struct ni_dbus_variant	ni_dbus_variant_t;
struct ni_dbus_variant {
	/* the dbus type of this value */
	int			type;

	/* Only valid if this variant is an array */
	struct {
		int		element_type;
		char *		element_signature;
		unsigned int	len;
	} array;

	/* Possible values */
	union {
		char *		string_value;
		char		byte_value;
		dbus_bool_t	bool_value;
		dbus_int16_t	int16_value;
		dbus_uint16_t	uint16_value;
		dbus_int32_t	int32_value;
		dbus_uint32_t	uint32_value;
		dbus_int64_t	int64_value;
		dbus_uint64_t	uint64_value;
		double		double_value;
		unsigned char *	byte_array_value;
		char **		string_array_value;
		ni_dbus_dict_entry_t *dict_array_value;
		ni_dbus_variant_t *variant_array_value;
		ni_dbus_variant_t *struct_value;
	};
};
#define NI_DBUS_VARIANT_INIT	{ .type = DBUS_TYPE_INVALID }

typedef struct ni_dbus_method	ni_dbus_method_t;

typedef dbus_bool_t		ni_dbus_method_handler_t(ni_dbus_object_t *object,
					const ni_dbus_method_t *method,
					unsigned int argc,
					const ni_dbus_variant_t *argv,
					ni_dbus_message_t *reply,
					DBusError *error);
typedef dbus_bool_t		ni_dbus_async_method_handler_t(ni_dbus_connection_t *connection,
					ni_dbus_object_t *object,
					const ni_dbus_method_t *method,
					ni_dbus_message_t *call);
/* Note: the server object may go away during execution of an async method, hence we do not
 * pass the object pointer into the completion handler. */
typedef dbus_bool_t		ni_dbus_async_method_completion_t(ni_dbus_connection_t *connection,
					const ni_dbus_method_t *method,
					ni_dbus_message_t *call,
					const ni_process_t *);

struct ni_dbus_method {
	const char *		name;
	const char *		call_signature;

	/* A method should set only one of these handlers */
	ni_dbus_method_handler_t *handler;
	ni_dbus_async_method_handler_t *async_handler;
	ni_dbus_async_method_completion_t *async_completion;

	void *			user_data;
};

typedef struct ni_dbus_property	ni_dbus_property_t;

typedef dbus_bool_t		ni_dbus_property_get_fn_t(const ni_dbus_object_t *,
					const ni_dbus_property_t *property,
					ni_dbus_variant_t *result,
					DBusError *error);
typedef dbus_bool_t		ni_dbus_property_set_fn_t(ni_dbus_object_t *,
					const ni_dbus_property_t *property,
					const ni_dbus_variant_t *value,
					DBusError *error);
typedef dbus_bool_t		ni_dbus_property_parse_fn_t(const ni_dbus_property_t *property,
					ni_dbus_variant_t *var,
					const char *value);

struct ni_dbus_property	{
	const char *			name;
	const char *			signature;

	struct {
		void *			(*get_handle)(const ni_dbus_object_t *, DBusError *);
		union {
			int *		int_offset;
			unsigned int *	uint_offset;
			uint16_t *	uint16_offset;
			char **		string_offset;
			ni_string_array_t *string_array_offset;
			const ni_dbus_property_t *dict_children;
		} u;
	} generic;

	ni_dbus_property_get_fn_t *	get;
	ni_dbus_property_set_fn_t *	set;
	ni_dbus_property_set_fn_t *	update;
	ni_dbus_property_parse_fn_t *	parse;
};

struct ni_dbus_service {
	char *				name;

	/* Declare which class we're compatible with. NULL if
	 * we're compatible with any kind of class */
	const ni_dbus_class_t *		compatible;

	const ni_dbus_method_t *	methods;
	const ni_dbus_method_t *	signals;
	const ni_dbus_property_t *	properties;

	void *				user_data;
};

struct ni_dbus_class {
	char *			name;
	const ni_dbus_class_t *	superclass;

	dbus_bool_t		(*init_child)(ni_dbus_object_t *);
	void			(*destroy)(ni_dbus_object_t *);
	dbus_bool_t		(*refresh)(ni_dbus_object_t *);
};

extern const ni_dbus_class_t	ni_dbus_anonymous_class;

struct ni_dbus_object {
	ni_dbus_object_t **	pprev;
	ni_dbus_object_t *	next;
	ni_dbus_object_t *	parent;

	const ni_dbus_class_t *	class;
	char *			name;		/* relative path */
	char *			path;		/* absolute path */
	void *			handle;		/* local object */
	ni_dbus_object_t *	children;
	const ni_dbus_service_t **interfaces;

	ni_dbus_server_object_t *server_object;
	ni_dbus_client_object_t *client_object;
};

typedef void			ni_dbus_async_callback_t(ni_dbus_object_t *proxy,
					ni_dbus_message_t *reply);
typedef void			ni_dbus_signal_handler_t(ni_dbus_connection_t *connection,
					ni_dbus_message_t *signal_msg,
					void *user_data);

extern ni_dbus_object_t *	ni_dbus_server_get_root_object(const ni_dbus_server_t *);
extern ni_dbus_object_t *	ni_dbus_server_register_object(ni_dbus_server_t *server,
					const char *object_path,
					const ni_dbus_class_t *object_class,
					void *object_handle);
extern dbus_bool_t		ni_dbus_server_unregister_object(ni_dbus_server_t *, void *);
extern ni_dbus_object_t *	ni_dbus_server_find_object_by_handle(ni_dbus_server_t *, const void *);
extern dbus_bool_t		ni_dbus_server_send_signal(ni_dbus_server_t *server, ni_dbus_object_t *object,
					const char *interface, const char *signal_name,
					unsigned int nargs, const ni_dbus_variant_t *args);

extern dbus_bool_t		ni_dbus_class_is_subclass(const ni_dbus_class_t *sub, const ni_dbus_class_t *super);

extern ni_dbus_object_t *	ni_dbus_object_new(const ni_dbus_class_t *,
					const char *path,
					void *handle);
extern ni_dbus_object_t *	ni_dbus_object_create(ni_dbus_object_t *root_object,
					const char *path,
					const ni_dbus_class_t *class,
					void *handle);
extern ni_dbus_object_t *	ni_dbus_object_lookup(ni_dbus_object_t *root_object, const char *path);
extern dbus_bool_t		ni_dbus_object_isa(const ni_dbus_object_t *, const ni_dbus_class_t *);
extern dbus_bool_t		ni_dbus_object_register_service(ni_dbus_object_t *object,
					const ni_dbus_service_t *);
extern const ni_dbus_method_t *	ni_dbus_service_get_method(const ni_dbus_service_t *service,
					const char *name);
extern const ni_dbus_method_t *	ni_dbus_service_get_signal(const ni_dbus_service_t *service,
					const char *name);

extern ni_dbus_server_t *	ni_dbus_object_get_server(const ni_dbus_object_t *);
extern ni_dbus_client_t *	ni_dbus_object_get_client(const ni_dbus_object_t *);
extern const char *		ni_dbus_object_get_path(const ni_dbus_object_t *);
extern void *			ni_dbus_object_get_handle(const ni_dbus_object_t *);
extern const ni_dbus_service_t *ni_dbus_object_get_service(const ni_dbus_object_t *, const char *);
extern const ni_dbus_service_t *ni_dbus_object_get_service_for_method(const ni_dbus_object_t *, const char *);
extern const ni_dbus_service_t *ni_dbus_object_get_service_for_signal(const ni_dbus_object_t *, const char *);
extern unsigned int		ni_dbus_object_get_all_services_for_method(const ni_dbus_object_t *object, const char *method,
					const ni_dbus_service_t **list, unsigned int list_size);
extern const char *		ni_dbus_object_get_default_interface(const ni_dbus_object_t *);
extern void			ni_dbus_object_set_default_interface(ni_dbus_object_t *, const char *);
extern void			ni_dbus_object_free(ni_dbus_object_t *);
extern dbus_bool_t		ni_dbus_object_get_property(const ni_dbus_object_t *, const char *, const ni_dbus_service_t *, ni_dbus_variant_t *);
extern dbus_bool_t		ni_dbus_object_set_properties_from_dict(ni_dbus_object_t *,
					const ni_dbus_service_t *interface,
					const ni_dbus_variant_t *dict);
extern dbus_bool_t		ni_dbus_object_get_properties_as_dict(const ni_dbus_object_t *object,
					const ni_dbus_service_t *interface,
					ni_dbus_variant_t *dict);
extern int			ni_dbus_object_translate_error(ni_dbus_object_t *, const DBusError *);

extern const ni_dbus_service_t *ni_dbus_get_standard_service(const char *);
extern const ni_dbus_property_t *ni_dbus_service_get_property(const ni_dbus_service_t *service, const char *name);
extern const ni_dbus_property_t *ni_dbus_service_lookup_property(const ni_dbus_service_t *service, const char *name);
extern const ni_dbus_property_t *ni_dbus_service_create_property(const ni_dbus_service_t *service, const char *name,
					ni_dbus_variant_t *dict,
					ni_dbus_variant_t **outdict);

extern void			ni_dbus_variant_init(ni_dbus_variant_t *);
extern dbus_bool_t		ni_dbus_variant_init_signature(ni_dbus_variant_t *, const char *);
extern void			ni_dbus_variant_copy(ni_dbus_variant_t *dst,
					const ni_dbus_variant_t *src);
extern void			ni_dbus_variant_destroy(ni_dbus_variant_t *);
extern const char *		ni_dbus_variant_sprint(const ni_dbus_variant_t *);
extern const char *		ni_dbus_variant_signature(const ni_dbus_variant_t *);
extern void			ni_dbus_variant_set_string(ni_dbus_variant_t *, const char *);
extern void			ni_dbus_variant_set_object_path(ni_dbus_variant_t *, const char *);
extern void			ni_dbus_variant_set_bool(ni_dbus_variant_t *, dbus_bool_t);
extern void			ni_dbus_variant_set_byte(ni_dbus_variant_t *, unsigned char);
extern void			ni_dbus_variant_set_uint16(ni_dbus_variant_t *, uint16_t);
extern void			ni_dbus_variant_set_int16(ni_dbus_variant_t *, int16_t);
extern void			ni_dbus_variant_set_uint32(ni_dbus_variant_t *, uint32_t);
extern void			ni_dbus_variant_set_int32(ni_dbus_variant_t *, int32_t);
extern void			ni_dbus_variant_set_uint64(ni_dbus_variant_t *, uint64_t);
extern void			ni_dbus_variant_set_int64(ni_dbus_variant_t *, int64_t);
extern void			ni_dbus_variant_set_double(ni_dbus_variant_t *, double);
extern dbus_bool_t		ni_dbus_variant_set_int(ni_dbus_variant_t *, int);
extern dbus_bool_t		ni_dbus_variant_set_uint(ni_dbus_variant_t *, unsigned int);
extern dbus_bool_t		ni_dbus_variant_set_long(ni_dbus_variant_t *, long);
extern dbus_bool_t		ni_dbus_variant_set_ulong(ni_dbus_variant_t *, unsigned long);
extern void			ni_dbus_variant_set_uuid(ni_dbus_variant_t *, const ni_uuid_t *);
extern dbus_bool_t		ni_dbus_variant_parse(ni_dbus_variant_t *var,
					const char *string_value, const char *signature);
extern dbus_bool_t		ni_dbus_variant_get_string(const ni_dbus_variant_t *, const char **);
extern dbus_bool_t		ni_dbus_variant_get_object_path(const ni_dbus_variant_t *, const char **);
extern dbus_bool_t		ni_dbus_variant_get_bool(const ni_dbus_variant_t *, dbus_bool_t *);
extern dbus_bool_t		ni_dbus_variant_get_byte(const ni_dbus_variant_t *, unsigned char *);
extern dbus_bool_t		ni_dbus_variant_get_uint16(const ni_dbus_variant_t *, uint16_t *);
extern dbus_bool_t		ni_dbus_variant_get_int16(const ni_dbus_variant_t *, int16_t *);
extern dbus_bool_t		ni_dbus_variant_get_uint32(const ni_dbus_variant_t *, uint32_t *);
extern dbus_bool_t		ni_dbus_variant_get_int32(const ni_dbus_variant_t *, int32_t *);
extern dbus_bool_t		ni_dbus_variant_get_uint64(const ni_dbus_variant_t *, uint64_t *);
extern dbus_bool_t		ni_dbus_variant_get_int64(const ni_dbus_variant_t *, int64_t *);
extern dbus_bool_t		ni_dbus_variant_get_double(const ni_dbus_variant_t *, double *);
extern dbus_bool_t		ni_dbus_variant_get_int(const ni_dbus_variant_t *, int *);
extern dbus_bool_t		ni_dbus_variant_get_uint(const ni_dbus_variant_t *, unsigned int *);
extern dbus_bool_t		ni_dbus_variant_get_long(const ni_dbus_variant_t *, long *);
extern dbus_bool_t		ni_dbus_variant_get_ulong(const ni_dbus_variant_t *, unsigned long *);
extern dbus_bool_t		ni_dbus_variant_get_uuid(const ni_dbus_variant_t *, ni_uuid_t *);
extern dbus_bool_t		ni_dbus_variant_get_byte_array_minmax(const ni_dbus_variant_t *,
					unsigned char *array, unsigned int *len,
					unsigned int minlen, unsigned int maxlen);
extern void			ni_dbus_variant_init_byte_array(ni_dbus_variant_t *);
extern void			ni_dbus_variant_set_byte_array(ni_dbus_variant_t *,
					const unsigned char *, unsigned int len);
extern dbus_bool_t		ni_dbus_variant_append_byte_array(ni_dbus_variant_t *, unsigned char);
extern void			ni_dbus_variant_init_string_array(ni_dbus_variant_t *);
extern void			ni_dbus_variant_set_string_array(ni_dbus_variant_t *,
					const char **, unsigned int len);
extern dbus_bool_t		ni_dbus_variant_append_string_array(ni_dbus_variant_t *, const char *);
extern void			ni_dbus_variant_init_object_path_array(ni_dbus_variant_t *);
extern dbus_bool_t		ni_dbus_variant_append_object_path_array(ni_dbus_variant_t *, const char *);
extern void			ni_dbus_variant_init_variant_array(ni_dbus_variant_t *);
extern ni_dbus_variant_t *	ni_dbus_variant_append_variant_element(ni_dbus_variant_t *);
extern const char *		ni_dbus_variant_array_print_element(const ni_dbus_variant_t *, unsigned int);

extern dbus_bool_t		ni_dbus_variant_is_array_of(const ni_dbus_variant_t *, const char *signature);
extern dbus_bool_t		ni_dbus_variant_is_byte_array(const ni_dbus_variant_t *);
extern dbus_bool_t		ni_dbus_variant_is_string_array(const ni_dbus_variant_t *);
extern dbus_bool_t		ni_dbus_variant_is_variant_array(const ni_dbus_variant_t *);
extern dbus_bool_t		ni_dbus_variant_is_dict_array(const ni_dbus_variant_t *);
extern dbus_bool_t		ni_dbus_variant_is_dict(const ni_dbus_variant_t *);

extern dbus_bool_t		ni_dbus_variant_array_parse_and_append_string(ni_dbus_variant_t *, const char *);

/* handle dicts */
extern void			ni_dbus_variant_init_dict(ni_dbus_variant_t *);
extern dbus_bool_t		ni_dbus_dict_add_entry(ni_dbus_variant_t *, const ni_dbus_dict_entry_t *);
extern dbus_bool_t		ni_dbus_dict_delete_entry(ni_dbus_variant_t *, const char *);
extern ni_dbus_variant_t *	ni_dbus_dict_add(ni_dbus_variant_t *, const char *);
extern dbus_bool_t		ni_dbus_dict_add_bool(ni_dbus_variant_t *, const char *, dbus_bool_t);
extern dbus_bool_t		ni_dbus_dict_add_int16(ni_dbus_variant_t *, const char *, int16_t);
extern dbus_bool_t		ni_dbus_dict_add_uint16(ni_dbus_variant_t *, const char *, uint16_t);
extern dbus_bool_t		ni_dbus_dict_add_int32(ni_dbus_variant_t *, const char *, int32_t);
extern dbus_bool_t		ni_dbus_dict_add_uint32(ni_dbus_variant_t *, const char *, uint32_t);
extern dbus_bool_t		ni_dbus_dict_add_int64(ni_dbus_variant_t *, const char *, int64_t);
extern dbus_bool_t		ni_dbus_dict_add_uint64(ni_dbus_variant_t *, const char *, uint64_t);
extern dbus_bool_t		ni_dbus_dict_add_string(ni_dbus_variant_t *, const char *, const char *);
extern dbus_bool_t		ni_dbus_dict_add_object_path(ni_dbus_variant_t *, const char *, const char *);
extern dbus_bool_t		ni_dbus_dict_add_double(ni_dbus_variant_t *, const char *, double);
extern dbus_bool_t		ni_dbus_dict_add_byte_array(ni_dbus_variant_t *, const char *,
					const unsigned char *byte_array, unsigned int len);
extern ni_dbus_variant_t *	ni_dbus_dict_get(const ni_dbus_variant_t *, const char *);
extern ni_dbus_variant_t *	ni_dbus_dict_get_entry(const ni_dbus_variant_t *, unsigned int, const char **);
extern ni_dbus_variant_t *	ni_dbus_dict_get_next(const ni_dbus_variant_t *, const char *, const ni_dbus_variant_t *);
extern dbus_bool_t		ni_dbus_dict_get_bool(const ni_dbus_variant_t *, const char *, dbus_bool_t *);
extern dbus_bool_t		ni_dbus_dict_get_int16(const ni_dbus_variant_t *, const char *, int16_t *);
extern dbus_bool_t		ni_dbus_dict_get_uint16(const ni_dbus_variant_t *, const char *, uint16_t *);
extern dbus_bool_t		ni_dbus_dict_get_int32(const ni_dbus_variant_t *, const char *, int32_t *);
extern dbus_bool_t		ni_dbus_dict_get_uint32(const ni_dbus_variant_t *, const char *, uint32_t *);
extern dbus_bool_t		ni_dbus_dict_get_int64(const ni_dbus_variant_t *, const char *, int64_t *);
extern dbus_bool_t		ni_dbus_dict_get_uint64(const ni_dbus_variant_t *, const char *, uint64_t *);
extern dbus_bool_t		ni_dbus_dict_get_string(const ni_dbus_variant_t *, const char *, const char **);
extern dbus_bool_t		ni_dbus_dict_get_object_path(const ni_dbus_variant_t *, const char *, const char **);
extern dbus_bool_t		ni_dbus_dict_get_double(const ni_dbus_variant_t *, const char *, double *);

extern void			ni_dbus_dict_array_init(ni_dbus_variant_t *);
extern ni_dbus_variant_t *	ni_dbus_dict_array_add(ni_dbus_variant_t *);

extern void			ni_dbus_array_array_init(ni_dbus_variant_t *, const char *);
extern ni_dbus_variant_t *	ni_dbus_array_array_add(ni_dbus_variant_t *);

extern dbus_bool_t		ni_dbus_generic_property_get_int(const ni_dbus_object_t *, const ni_dbus_property_t *,
					ni_dbus_variant_t *r, DBusError *);
extern dbus_bool_t		ni_dbus_generic_property_set_int(ni_dbus_object_t *, const ni_dbus_property_t *,
					const ni_dbus_variant_t *, DBusError *);
extern dbus_bool_t		ni_dbus_generic_property_parse_int(const ni_dbus_property_t *,
					ni_dbus_variant_t *, const char *);
extern dbus_bool_t		ni_dbus_generic_property_get_uint(const ni_dbus_object_t *, const ni_dbus_property_t *,
					ni_dbus_variant_t *r, DBusError *);
extern dbus_bool_t		ni_dbus_generic_property_set_uint(ni_dbus_object_t *, const ni_dbus_property_t *,
					const ni_dbus_variant_t *, DBusError *);
extern dbus_bool_t		ni_dbus_generic_property_parse_uint(const ni_dbus_property_t *,
					ni_dbus_variant_t *, const char *);
extern dbus_bool_t		ni_dbus_generic_property_get_uint16(const ni_dbus_object_t *, const ni_dbus_property_t *,
					ni_dbus_variant_t *r, DBusError *);
extern dbus_bool_t		ni_dbus_generic_property_set_uint16(ni_dbus_object_t *, const ni_dbus_property_t *,
					const ni_dbus_variant_t *, DBusError *);
extern dbus_bool_t		ni_dbus_generic_property_parse_uint16(const ni_dbus_property_t *,
					ni_dbus_variant_t *, const char *);
extern dbus_bool_t		ni_dbus_generic_property_get_string(const ni_dbus_object_t *, const ni_dbus_property_t *,
					ni_dbus_variant_t *r, DBusError *);
extern dbus_bool_t		ni_dbus_generic_property_set_string(ni_dbus_object_t *, const ni_dbus_property_t *,
					const ni_dbus_variant_t *, DBusError *);
extern dbus_bool_t		ni_dbus_generic_property_parse_string(const ni_dbus_property_t *,
					ni_dbus_variant_t *, const char *);
extern dbus_bool_t		ni_dbus_generic_property_get_string_array(const ni_dbus_object_t *, const ni_dbus_property_t *,
					ni_dbus_variant_t *r, DBusError *);
extern dbus_bool_t		ni_dbus_generic_property_set_string_array(ni_dbus_object_t *, const ni_dbus_property_t *,
					const ni_dbus_variant_t *, DBusError *);
extern dbus_bool_t		ni_dbus_generic_property_parse_string_array(const ni_dbus_property_t *,
					ni_dbus_variant_t *, const char *);

/*
 * Client side functions
 */
extern ni_dbus_client_t *	ni_dbus_client_open(const char *bus_type, const char *bus_name);
extern void			ni_dbus_client_free(ni_dbus_client_t *);
extern void			ni_dbus_client_add_signal_handler(ni_dbus_client_t *client,
					const char *sender,
					const char *object_path,
					const char *object_interface,
					ni_dbus_signal_handler_t *callback,
					void *user_data);
extern void			ni_dbus_client_set_call_timeout(ni_dbus_client_t *, unsigned int msec);
extern void			ni_dbus_client_set_error_map(ni_dbus_client_t *, const ni_intmap_t *);
extern int			ni_dbus_client_translate_error(ni_dbus_client_t *, const DBusError *);
extern ni_dbus_message_t *	ni_dbus_client_call(ni_dbus_client_t *client, ni_dbus_message_t *call,
					DBusError *error);
extern ni_dbus_object_t *	ni_dbus_client_object_new(ni_dbus_client_t *client,
					const ni_dbus_class_t *,
					const char *object_path,
					const char *default_interface,
					void *local_data);
extern ni_dbus_object_t *	ni_dbus_client_object_new_child(ni_dbus_object_t *parent,
					const char *name,
					const char *interface,
					void *local_data);
extern dbus_bool_t		ni_dbus_object_refresh_children(ni_dbus_object_t *);
extern ni_dbus_object_t *	ni_dbus_object_find_child(ni_dbus_object_t *parent, const char *name);
extern dbus_bool_t		ni_dbus_object_call_variant(const ni_dbus_object_t *,
					const char *interface, const char *method,
					unsigned int nargs, const ni_dbus_variant_t *args,
					unsigned int maxres, ni_dbus_variant_t *res,
					DBusError *error);
extern int			ni_dbus_object_call_simple(const ni_dbus_object_t *,
					const char *interface, const char *method,
					int arg_type, void *arg_ptr,
					int res_type, void *res_ptr);
extern int			ni_dbus_object_call_async(ni_dbus_object_t *obj,
					ni_dbus_async_callback_t *callback, const char *method, ...);

extern ni_dbus_message_t *	ni_dbus_object_call_new(const ni_dbus_object_t *, const char *method, ...);
extern ni_dbus_message_t *	ni_dbus_object_call_new_va(const ni_dbus_object_t *obj,
					const char *method, va_list *app);

extern int			ni_dbus_message_get_args(ni_dbus_message_t *, ...);
extern int			ni_dbus_message_get_args_variants(ni_dbus_message_t *msg,
					ni_dbus_variant_t *argv, unsigned int max_args);
extern dbus_bool_t		ni_dbus_message_serialize_variants(ni_dbus_message_t *msg,
					unsigned int nargs, const ni_dbus_variant_t *argv,
					DBusError *error);
extern dbus_bool_t		ni_dbus_message_append_byte(ni_dbus_message_t *, unsigned char);
extern dbus_bool_t		ni_dbus_message_append_uint16(ni_dbus_message_t *, uint16_t);
extern dbus_bool_t		ni_dbus_message_append_uint32(ni_dbus_message_t *, uint32_t);
extern dbus_bool_t		ni_dbus_message_append_uint64(ni_dbus_message_t *, uint64_t);
extern dbus_bool_t		ni_dbus_message_append_int16(ni_dbus_message_t *, int16_t);
extern dbus_bool_t		ni_dbus_message_append_int32(ni_dbus_message_t *, int32_t);
extern dbus_bool_t		ni_dbus_message_append_int64(ni_dbus_message_t *, int64_t);
extern dbus_bool_t		ni_dbus_message_append_string(ni_dbus_message_t *, const char *);
extern dbus_bool_t		ni_dbus_message_append_uuid(ni_dbus_message_t *, const ni_uuid_t *);

typedef struct ni_dbus_xml_validate_context {
	dbus_bool_t		(*metadata_callback)(xml_node_t *, const ni_xs_type_t *, const xml_node_t *, void *);
	int			(*prompt_callback)(xml_node_t *, const ni_xs_type_t *, const xml_node_t *, void *);
	void *			user_data;
} ni_dbus_xml_validate_context_t;

extern ni_xs_scope_t *		ni_dbus_xml_init(void);
extern int			ni_dbus_xml_register_services(ni_xs_scope_t *);
extern unsigned int		ni_dbus_xml_method_num_args(const ni_dbus_method_t *);
extern const xml_node_t *	ni_dbus_xml_get_argument_metadata(const ni_dbus_method_t *, unsigned int);
extern int			ni_dbus_xml_map_method_argument(const ni_dbus_method_t *method, unsigned int index,
						xml_node_t *doc_node, xml_node_t **ret_node, ni_bool_t *skip_call);
extern dbus_bool_t		ni_dbus_xml_validate_argument(const ni_dbus_method_t *, unsigned int,
						xml_node_t *, const ni_dbus_xml_validate_context_t *);
extern dbus_bool_t		ni_dbus_xml_serialize_arg(const ni_dbus_method_t *, unsigned int,
						ni_dbus_variant_t *, xml_node_t *);
extern dbus_bool_t		ni_dbus_xml_method_has_return(const ni_dbus_method_t *);
extern int			ni_dbus_serialize_return(const ni_dbus_method_t *, ni_dbus_variant_t *, xml_node_t *);
extern void			ni_dbus_serialize_error(DBusError *, xml_node_t *);
extern xml_node_t *		ni_dbus_xml_deserialize_arguments(const ni_dbus_method_t *method,
		                                unsigned int num_vars, ni_dbus_variant_t *vars,
						xml_node_t *parent,
						ni_tempstate_t *);
extern xml_node_t *		ni_dbus_xml_deserialize_properties(ni_xs_scope_t *, const char *,
						ni_dbus_variant_t *, xml_node_t *);

extern int			ni_dbus_xml_get_method_metadata(const ni_dbus_method_t *method,
						const char *node_name,
						xml_node_t **list, unsigned int max_nodes);
extern int			ni_dbus_xml_expand_element_reference(xml_node_t *doc_node, const char *expr_string,
						xml_node_t **ret_nodes, unsigned int max_nodes);

extern unsigned int		__ni_dbus_variant_offsets[256];

static inline void *
ni_dbus_variant_datum_ptr(ni_dbus_variant_t *variant)
{
	unsigned int type = variant->type;
	unsigned int offset;

	if (type > 255 || (offset = __ni_dbus_variant_offsets[type]) == 0)
		return NULL;
	return (void *) (((caddr_t) variant) + offset);
}

static inline const void *
ni_dbus_variant_datum_const_ptr(const ni_dbus_variant_t *variant)
{
	unsigned int type = variant->type;
	unsigned int offset;

	if (type > 255 || (offset = __ni_dbus_variant_offsets[type]) == 0)
		return NULL;
	return (const void *) (((const caddr_t) variant) + offset);
}

#endif /* __WICKED_DBUS_H__ */

