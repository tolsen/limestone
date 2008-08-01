
#include "liveprops.h"
#include "dbms.h"
#include <apr_strings.h>

/* forward-declare */
static const dav_hooks_liveprop dav_hooks_supported_liveprop;

/*
** The namespace URIs that we use. This list and the enumeration must
** stay in sync.
*/
static const char *const dav_supported_namespace_uris[] = {
    "DAV:",
    NULL			/* sentinel */
};

enum {
    dav_repos_URI_DAV,		/* the DAV: namespace URI */
};

static const dav_liveprop_spec dav_supported_props[] = {
    {
     dav_repos_URI_DAV,
     "supported-live-property-set",
     DAV_PROPID_supported_live_property_set,
     0},
    {
     dav_repos_URI_DAV,
     "supported-method-set",
     DAV_PROPID_supported_method_set,
     0},
    {
     dav_repos_URI_DAV,
     "supported-report-set",
     DAV_PROPID_supported_report_set,
     0},

    {0}				/* sentinel */
};

static const dav_liveprop_group dav_supported_liveprop_group = {
    dav_supported_props,
    dav_supported_namespace_uris,
    &dav_hooks_supported_liveprop
};

static dav_prop_insert dav_supported_insert_prop(const dav_resource * resource,
                                             int propid,
                                             dav_prop_insert what,
                                             apr_text_header * phdr)
{
    const dav_liveprop_spec *spec;
    apr_pool_t *pool = resource->pool;
    const char *name;
    const char *s;
    TRACE();

    spec = get_livepropspec_from_id(dav_supported_props, propid);
    name = spec->name;

    if (what == DAV_PROP_INSERT_VALUE) {
        dav_gen_supported_options(resource->info->rec, resource, propid, phdr);
    } else if (what == DAV_PROP_INSERT_SUPPORTED) {
	s = apr_psprintf(pool,
			 "<D:supported-live-property><D:prop>" DEBUG_CR
                         "<D:%s/>" DEBUG_CR
			 "</D:prop></D:supported-live-property>" DEBUG_CR,
			 name);
        apr_text_append(pool, phdr, s);
    }

    return what;
}

static int dav_supported_is_writable(const dav_resource * resource, int propid)
{
    return 0;
}

static const dav_hooks_liveprop dav_hooks_supported_liveprop = {
    dav_supported_insert_prop,
    dav_supported_is_writable,
    dav_supported_namespace_uris,
    NULL,
    NULL,
    NULL,
    NULL
};

/* Find live prop and return its propid */
static int dav_supported_find_liveprop(const dav_resource * resource,
                                   const char *ns_uri, const char *name,
                                   const dav_hooks_liveprop ** hooks)
{
    return dav_do_find_liveprop(ns_uri, name, &dav_supported_liveprop_group, hooks);
}

static void dav_supported_insert_all_liveprops(request_rec * r,
                                           const dav_resource * resource,
                                           dav_prop_insert what,
                                           apr_text_header * phdr)
{
    const char *s;

    /* don't try to find any liveprops if this isn't "our" resource */
    if (resource->hooks != &dav_repos_hooks_repos)
	return;

    TRACE();

    if (!resource->exists || resource->type != DAV_RESOURCE_TYPE_REGULAR)
        return;

    if (what == DAV_PROP_INSERT_VALUE)
        return;

    s = apr_psprintf(r->pool,
                     "<D:supported-live-property-set/>" DEBUG_CR
                     "<D:supported-method-set/>" DEBUG_CR
                     "<D:supported-report-set/>" DEBUG_CR);

    if (what == DAV_PROP_INSERT_NAME)
        apr_text_append(r->pool, phdr, s);
    else if (what == DAV_PROP_INSERT_SUPPORTED) {
        s = apr_psprintf(r->pool, "<D:supported-live-property><D:prop>" DEBUG_CR
                         "%s" DEBUG_CR "</D:prop></D:supported-live-property>", s);
        apr_text_append(r->pool, phdr, s);
    }
}

void dav_supported_register_liveprops(apr_pool_t *p)
{
    dav_hook_find_liveprop(dav_supported_find_liveprop, NULL, NULL,
                           APR_HOOK_MIDDLE);

    dav_hook_insert_all_liveprops(dav_supported_insert_all_liveprops, NULL,
                                  NULL, APR_HOOK_MIDDLE);
    /* register the namespace URIs */
    dav_register_liveprop_group(p, &dav_supported_liveprop_group);
}
