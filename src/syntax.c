/*
 * Handle abstract syntax objects.
 * Should possibly move to netinfo.c
 *
 * Copyright (C) 2010 Olaf Kirch <okir@suse.de>
 */

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdarg.h>

#include <wicked/netinfo.h>
#include <wicked/xml.h>
#include "netinfo_priv.h"
#include "config.h"

static const char *	__ni_syntax_prepend_root(ni_syntax_t *, const char *);
static const char *	__ni_syntax_prepend_base(ni_syntax_t *, const char *);

ni_syntax_t *
ni_syntax_new(const char *schema, const char *base_path)
{
	if (!schema) {
		schema = ni_global.config->default_syntax;
		if (schema && !base_path)
			base_path = ni_global.config->default_syntax_path;
	}

	if (!schema) {
		if (ni_file_exists("/etc/SuSE-release"))
			schema = "suse";
		else if (ni_file_exists("/etc/redhat-release"))
			schema = "redhat";
		else {
			ni_error("Unable to determine default schema");
			return NULL;
		}
	}

	if (!strcasecmp(schema, "suse"))
		return __ni_syntax_sysconfig_suse(base_path);
	if (!strcasecmp(schema, "redhat"))
		return __ni_syntax_sysconfig_redhat(base_path);
	if (!strcasecmp(schema, "netcf"))
		return __ni_syntax_netcf(base_path);
	if (!strcasecmp(schema, "netcf-strict"))
		return __ni_syntax_netcf_strict(base_path);

	ni_error("schema \"%s\" not supported", schema);
	return NULL;
}

void
ni_syntax_set_root_directory(ni_syntax_t *syntax, const char *root_dir)
{
	ni_string_dup(&syntax->root_dir, root_dir);
}

void
ni_syntax_free(ni_syntax_t *syntax)
{
	free(syntax->base_path);
	free(syntax->root_dir);
	free(syntax);
}

/*
 * Build a pathname composed of root_dir, base_path and a format string.
 *  root_dir would be where a virtual image would be mounted.
 *  base_path would be something like /etc/sysconfig/network.
 * If root_dir is set, always prepend it.
 * If fmt is a relative path, prepend base_path if set.
 */
const char *
ni_syntax_build_path(ni_syntax_t *syntax, const char *fmt, ...)
{
	static char pathbuf[PATH_MAX];
	const char *result;
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(pathbuf, sizeof(pathbuf), fmt, ap);
	result = pathbuf;
	va_end(ap);

	if (result[0] != '/')
		result = __ni_syntax_prepend_base(syntax, result);
	return __ni_syntax_prepend_root(syntax, result);
}

const char *
ni_syntax_base_path(ni_syntax_t *syntax)
{
	return __ni_syntax_prepend_root(syntax, syntax->base_path);
}

static const char *
__ni_syntax_prepend_root(ni_syntax_t *syntax, const char *filename)
{
	static char pathbuf[PATH_MAX];

	if (syntax->root_dir == NULL)
		return filename;

	if (filename[0] == '/')
		++filename;
	snprintf(pathbuf, sizeof(pathbuf), "%s/%s", syntax->root_dir, filename);
	return pathbuf;
}

static const char *
__ni_syntax_prepend_base(ni_syntax_t *syntax, const char *filename)
{
	static char pathbuf[PATH_MAX];

	if (syntax->base_path == NULL)
		return filename;

	snprintf(pathbuf, sizeof(pathbuf), "%s/%s", syntax->base_path, filename);
	return pathbuf;
}

/*
 * Parse interface configuration contained in default
 * system configuration files.
 */
int
ni_syntax_get_interfaces(ni_syntax_t *syntax, ni_handle_t *nih)
{
	if (syntax->get_interfaces)
		return syntax->get_interfaces(syntax, nih);

	return ni_syntax_parse_file(syntax, nih, syntax->base_path);
}

int
ni_syntax_parse_data(ni_syntax_t *syntax, ni_handle_t *nih, const char *data)
{
	FILE *memstream;
	int rv = -1;

	memstream = fmemopen((void *) data, strlen(data), "r");
	if (memstream == NULL) {
		error("Unable to open memstream for data: %m");
	} else {
		rv = ni_syntax_parse_stream(syntax, nih, memstream);
		fclose(memstream);
	}

	return rv;
}

int
ni_syntax_parse_file(ni_syntax_t *syntax, ni_handle_t *nih, const char *filename)
{
	if (syntax->xml_to_interface) {
		xml_document_t *doc;
		int rv;

		/* Relocate filename relative to root and/or base_path */
		if (filename == syntax->base_path)
			filename = __ni_syntax_prepend_root(syntax, filename);
		else
			filename = ni_syntax_build_path(syntax, "%s", filename);
		if (!(doc = xml_document_read(filename))) {
			ni_error("unable to parse XML document %s", filename);
			return -1;
		}

		rv = ni_syntax_xml_to_all(syntax, nih, doc);

		xml_document_free(doc);
		return rv;
	}

	error("%s: syntax not capable of parsing config data", __FUNCTION__);
	return -1;
}

int
ni_syntax_parse_stream(ni_syntax_t *syntax, ni_handle_t *nih, FILE *input)
{
	if (syntax->xml_to_interface) {
		xml_document_t *doc;
		int rv;

		if (!(doc = xml_document_scan(input))) {
			error("%s: unable to parse XML document", __FUNCTION__);
			return -1;
		}

		rv = ni_syntax_xml_to_all(syntax, nih, doc);

		xml_document_free(doc);
		return rv;
	}

	error("%s: syntax not capable of parsing config data", __FUNCTION__);
	return -1;
}

int
ni_syntax_put_interfaces(ni_syntax_t *syntax, ni_handle_t *nih, FILE *outfile)
{
	if (syntax->put_interfaces)
		return syntax->put_interfaces(syntax, nih, outfile);

	if (syntax->xml_from_interface) {
		xml_document_t *doc;
		int rv = -1;

		doc = ni_syntax_xml_from_all(syntax, nih);
		if (!doc) {
			error("%s: problem building XML from ni_handle", syntax->schema);
			return -1;
		}

		if (outfile) {
			rv = xml_document_print(doc, outfile);
		} else
		if (!syntax->base_path || !strcmp(syntax->base_path, "-")) {
			rv = xml_document_print(doc, stdout);
		} else {
			const char *filename;

			/* FIXME: create temp file, and use separate commit at
			 * a later point to rename it to final destination.
			 * This allows atomic updates and rollback */
			filename = ni_syntax_build_path(syntax, "all.xml");

			rv = xml_document_write(doc, filename);
		}

		xml_document_free(doc);
		return rv;
	}

	error("%s: syntax not capable of writing global config", __FUNCTION__);
	return -1;
}

int
ni_syntax_put_one_interface(ni_syntax_t *syntax, ni_handle_t *nih, ni_interface_t *ifp, FILE *outfile)
{
	if (syntax->put_one_interface)
		return syntax->put_one_interface(syntax, nih, ifp, outfile);

	if (syntax->xml_from_interface) {
		xml_document_t *doc;
		int rv = -1;

		doc = xml_document_new();

		if (!syntax->xml_from_interface(syntax, nih, ifp, doc->root))
			goto error;

		if (outfile) {
			rv = xml_document_print(doc, outfile);
		} else
		if (!syntax->base_path || !strcmp(syntax->base_path, "-")) {
			rv = xml_document_print(doc, stdout);
		} else {
			const char *filename;

			/* FIXME: create temp file, and use separate commit at
			 * a later point to rename it to final destination.
			 * This allows atomic updates and rollback */
			filename = ni_syntax_build_path(syntax, "%s.xml", ifp->name);

			rv = xml_document_write(doc, filename);
		}

error:
		xml_document_free(doc);
		return rv;
	}

	error("%s: syntax not capable of writing a single interface", __FUNCTION__);
	return -1;
}

/*
 * Produce XML for a single interface and vice versa
 */
xml_node_t *
ni_syntax_xml_from_interface(ni_syntax_t *syntax, ni_handle_t *nih, const ni_interface_t *ifp)
{
	if (!syntax->xml_from_interface) {
		error("%s: syntax not capable of creating xml for interface", __FUNCTION__);
		return NULL;
	}
	return syntax->xml_from_interface(syntax, nih, ifp, NULL);
}

ni_interface_t *
ni_syntax_xml_to_interface(ni_syntax_t *syntax, ni_handle_t *nih, xml_node_t *xmlnode)
{
	ni_interface_t *ifp;

	if (!syntax->xml_to_interface) {
		error("%s: syntax not capable of creating interface from xml", __FUNCTION__);
		return NULL;
	}

	ifp = syntax->xml_to_interface(syntax, nih, xmlnode);

	/* The only occasion where we allow interfaces without name is in
	 * policy descriptors. */
	if (ifp->name == NULL) {
		ni_error("%s: interface descriptor without name", __FUNCTION__);
		return NULL;
	}

	return ifp;
}

/*
 * XML to/from interface stats
 */
xml_node_t *
ni_syntax_xml_from_interface_stats(ni_syntax_t *syntax, ni_handle_t *nih, const ni_interface_t *ifp)
{
	if (!syntax->xml_from_interface_stats) {
		ni_error("%s: syntax not capable of creating xml for interface stats", __FUNCTION__);
		return NULL;
	}
	return syntax->xml_from_interface_stats(syntax, nih, ifp, NULL);
}

int
ni_syntax_xml_to_interface_stats(ni_syntax_t *syntax, ni_handle_t *nih, ni_interface_t *ifp, xml_node_t *xmlnode)
{
	if (!syntax->xml_to_interface_stats) {
		ni_error("%s: syntax not capable of creating interface stats from xml", __FUNCTION__);
		return -1;
	}

	return syntax->xml_to_interface_stats(syntax, nih, ifp, xmlnode);
}

/*
 * XML to/from wireless scan results
 */
xml_node_t *
ni_syntax_xml_from_wireless_scan(ni_syntax_t *syntax, ni_handle_t *nih, const ni_wireless_scan_t *scan)
{
	if (!syntax->xml_from_wireless_scan) {
		ni_error("%s: syntax not capable of creating xml for wireless scan", __FUNCTION__);
		return NULL;
	}
	return syntax->xml_from_wireless_scan(syntax, scan, NULL);
}

ni_wireless_scan_t *
ni_syntax_xml_to_wireless_scan(ni_syntax_t *syntax, ni_handle_t *nih, xml_node_t *parent)
{
	return NULL;
}

/*
 * Produce XML for a single addrconf request and vice versa.
 */
xml_node_t *
ni_syntax_xml_from_addrconf_request(ni_syntax_t *syntax, ni_addrconf_request_t *request, xml_node_t *parent)
{
	if (!syntax->xml_from_request) {
		ni_error("%s: syntax not capable of representing addrconf request as XML", __FUNCTION__);
		return NULL;
	}
	return syntax->xml_from_request(syntax, request, parent);
}

ni_addrconf_request_t *
ni_syntax_xml_to_addrconf_request(ni_syntax_t *syntax, const xml_node_t *xmlnode, int af)
{
	if (!syntax->xml_to_request) {
		ni_error("%s: syntax not capable of creating request from xml", __FUNCTION__);
		return NULL;
	}
	return syntax->xml_to_request(syntax, xmlnode, af);
}

/*
 * Produce XML for NIS config and vice versa.
 */
xml_node_t *
ni_syntax_xml_from_nis(ni_syntax_t *syntax, const ni_nis_info_t *nis, xml_node_t *parent)
{
	if (!syntax->xml_from_nis) {
		ni_error("%s: syntax not capable of representing NIS config as XML", __FUNCTION__);
		return NULL;
	}
	return syntax->xml_from_nis(syntax, nis, parent);
}

ni_nis_info_t *
ni_syntax_xml_to_nis(ni_syntax_t *syntax, const xml_node_t *xmlnode)
{
	if (!syntax->xml_to_nis) {
		ni_error("%s: syntax not capable of creating NIS config from xml", __FUNCTION__);
		return NULL;
	}
	return syntax->xml_to_nis(syntax, xmlnode);
}

/*
 * Produce XML for resolver config and vice versa.
 */
xml_node_t *
ni_syntax_xml_from_resolver(ni_syntax_t *syntax, const ni_resolver_info_t *resolver, xml_node_t *parent)
{
	if (!syntax->xml_from_resolver) {
		ni_error("%s: syntax not capable of representing resolver config as XML", __FUNCTION__);
		return NULL;
	}
	return syntax->xml_from_resolver(syntax, resolver, parent);
}

ni_resolver_info_t *
ni_syntax_xml_to_resolver(ni_syntax_t *syntax, const xml_node_t *xmlnode)
{
	if (!syntax->xml_to_resolver) {
		ni_error("%s: syntax not capable of creating resolver config from xml", __FUNCTION__);
		return NULL;
	}
	return syntax->xml_to_resolver(syntax, xmlnode);
}

/*
 * Produce XML for a single lease and vice versa.
 */
xml_node_t *
ni_syntax_xml_from_lease(ni_syntax_t *syntax, ni_addrconf_lease_t *lease, xml_node_t *parent)
{
	if (!syntax->xml_from_lease) {
		ni_error("%s: syntax not capable of representing addrconf lease as XML", __FUNCTION__);
		return NULL;
	}
	return syntax->xml_from_lease(syntax, lease, parent);
}

ni_addrconf_lease_t *
ni_syntax_xml_to_lease(ni_syntax_t *syntax, const xml_node_t *xmlnode)
{
	if (!syntax->xml_to_lease) {
		error("%s: syntax not capable of creating lease from xml", __FUNCTION__);
		return NULL;
	}
	return syntax->xml_to_lease(syntax, xmlnode);
}

/*
 * Produce XML for all interfaces
 */
xml_document_t *
ni_syntax_xml_from_all(ni_syntax_t *syntax, ni_handle_t *nih)
{
	xml_document_t *doc = NULL;
	xml_node_t *root;
	ni_interface_t *ifp;

	if (!syntax->xml_from_interface) {
		error("%s: syntax not capable of creating xml for interface", __FUNCTION__);
		return NULL;
	}

	doc = xml_document_new();
	root = xml_document_root(doc);

	/* Produce all interfaces */
	for (ifp = nih->iflist; ifp; ifp = ifp->next) {
		if (syntax->xml_from_interface(syntax, nih, ifp, root) == NULL)
			goto error;
	}

	return doc;

error:
	if (doc)
		xml_document_free(doc);
	return NULL;
}

/*
 * Produce interfaces from XML
 */
int
ni_syntax_xml_to_all(ni_syntax_t *syntax, ni_handle_t *nih, const xml_document_t *doc)
{
	if (!doc)
		return -1;
	return __ni_syntax_xml_to_all(syntax, nih, doc->root);
}

int
__ni_syntax_xml_to_all(ni_syntax_t *syntax, ni_handle_t *nih, const xml_node_t *root)
{
	xml_node_t *child;

	if (!syntax->xml_to_interface) {
		error("%s: syntax not capable of creating interface from xml", __FUNCTION__);
		return -1;
	}

	if (!root)
		return -1;

	for (child = root->children; child; child = child->next) {
		ni_interface_t *ifp;

		if (strcmp(child->name, "interface"))
			continue;

		ifp = syntax->xml_to_interface(syntax, nih, child);
		if (ifp == NULL) {
			ni_error("%s: failed to parse configuration data", __FUNCTION__);
			return -1;
		}

		/* The only occasion where we allow interfaces without name is in
		 * policy descriptors. */
		if (ifp->name == NULL) {
			ni_error("%s: interface descriptor without name", __FUNCTION__);
			return -1;
		}
	}

	return 0;
}

/*
 * Handle policy_info representation
 */
int
__ni_syntax_xml_to_policy_info(ni_syntax_t *syntax, ni_policy_info_t *info, const xml_node_t *root)
{
	xml_node_t *child;

	if (!syntax->xml_to_policy) {
		error("%s: syntax not capable of creating interface from xml", __FUNCTION__);
		return -1;
	}

	if (!root)
		return -1;

	for (child = root->children; child; child = child->next) {
		ni_policy_t *policy;

		if (strcmp(child->name, "policy"))
			continue;

		policy = syntax->xml_to_policy(syntax, child);
		if (policy == NULL) {
			ni_error("%s: failed to parse policy element", __FUNCTION__);
			return -1;
		}

		ni_policy_info_append(info, policy);
	}

	return 0;
}

xml_node_t *
__ni_syntax_xml_from_policy_info(ni_syntax_t *syntax, const ni_policy_info_t *info)
{
	const ni_policy_t *policy;
	xml_node_t *root;

	if (!syntax->xml_from_policy) {
		error("%s: syntax not capable of creating xml for policy", __FUNCTION__);
		return NULL;
	}

	root = xml_node_new(NULL, NULL);

	for (policy = info->event_policies; policy; policy = policy->next) {
		if (syntax->xml_from_policy(syntax, policy, root) == NULL)
			goto error;
	}

	return root;

error:
	if (root)
		xml_node_free(root);
	return NULL;
}

/*
 * Handle ethtool representation
 */
ni_ethernet_t *
__ni_syntax_xml_to_ethernet(ni_syntax_t *syntax, const xml_node_t *node)
{
	ni_ethernet_t *ethernet;

	if (!syntax->xml_to_ethernet) {
		ni_error("%s: syntax not capable of creating ethernet info from xml", __FUNCTION__);
		return NULL;
	}

	if (!node)
		return NULL;


	ethernet = syntax->xml_to_ethernet(syntax, node);
	if (ethernet == NULL) {
		ni_error("%s: failed to parse ethernet element", __FUNCTION__);
		return NULL;
	}

	return ethernet;
}

xml_node_t *
__ni_syntax_xml_from_ethernet(ni_syntax_t *syntax, const ni_ethernet_t *ethernet, xml_node_t *parent)
{
	if (!syntax->xml_from_ethernet) {
		error("%s: syntax not capable of creating xml for ethernet", __FUNCTION__);
		return NULL;
	}

	return syntax->xml_from_ethernet(syntax, ethernet, parent);
}

