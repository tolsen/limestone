/* ====================================================================
 * Copyright 2007 Lime Spot LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * ====================================================================
 */

#include "liveprops.h"
#include "dbms_quota.h"
#include "bridge.h"
#include <apr_strings.h>

/* forward-declare */
static const dav_hooks_liveprop dav_hooks_quota_liveprop;

/*
** The namespace URIs that we use. This list and the enumeration must
** stay in sync.
*/
static const char *const dav_quota_namespace_uris[] = {
    "DAV:",
    NULL			/* sentinel */
};

enum {
    dav_repos_URI_DAV,		/* the DAV: namespace URI */
};

/*
** Define each of the core properties that this provider will handle.
** Note that all of them are in the DAV: namespace, which has a
** provider-local index of 0.
*/
static const dav_liveprop_spec dav_quota_props[] = {
    {
     dav_repos_URI_DAV,
     "quota-used-bytes",
     DAV_PROPID_quota_used_bytes,
     0},
    {
     dav_repos_URI_DAV,
     "quota-available-bytes",
     DAV_PROPID_quota_available_bytes,
     0},

    {0}				/* sentinel */
};

static const dav_liveprop_group dav_quota_liveprop_group = {
    dav_quota_props,
    dav_quota_namespace_uris,
    &dav_hooks_quota_liveprop
};

static dav_prop_insert dav_quota_insert_prop(const dav_resource * resource,
                                             int propid,
                                             dav_prop_insert what,
                                             apr_text_header * phdr)
{
    dav_repos_db *db = resource->info->db;
    dav_repos_resource *db_r = resource->info->db_r;
    const dav_liveprop_spec *spec;
    apr_pool_t *pool = db_r->p;
    const char *name;
    const char *s = "";
    TRACE();

    if (propid != DAV_PROPID_quota_available_bytes &&
        propid != DAV_PROPID_quota_used_bytes)
        return DAV_PROP_INSERT_NOTDEF;

    if (!resource->exists || resource->type != DAV_RESOURCE_TYPE_REGULAR)
        return DAV_PROP_INSERT_NOTDEF;

    spec = get_livepropspec_from_id(dav_quota_props, propid);
    name = spec->name;

    if (what == DAV_PROP_INSERT_VALUE) {
        long nbytes = 0;
        switch (propid) {
        case DAV_PROPID_quota_available_bytes:
            dbms_get_available_bytes (db_r->p, db, db_r->owner_id, &nbytes);
            break;
        case DAV_PROPID_quota_used_bytes:
            nbytes = sabridge_get_used_bytes (db, db_r, 1);
            break;
        }
        s = apr_psprintf(pool, "<D:%s>%ld</D:%s>", name, nbytes, name);

    } else if (what == DAV_PROP_INSERT_SUPPORTED) {
	s = apr_psprintf(pool,
			 "<D:supported-live-property><D:prop>" DEBUG_CR
                         "<D:%s/>" DEBUG_CR
			 "</D:prop></D:supported-live-property>" DEBUG_CR,
			 name);
    }

    apr_text_append(pool, phdr, s);
    /* we inserted what was asked for */
    return what;
}

static int dav_quota_is_writable(const dav_resource * resource, int propid)
{
    TRACE();

    return 0;
}

static const dav_hooks_liveprop dav_hooks_quota_liveprop = {
    dav_quota_insert_prop,
    dav_quota_is_writable,
    dav_quota_namespace_uris,
    NULL,
    NULL,
    NULL,
    NULL
};

/* Find live prop and return its propid */
static int dav_quota_find_liveprop(const dav_resource * resource,
                                   const char *ns_uri, const char *name,
                                   const dav_hooks_liveprop ** hooks)
{
    return dav_do_find_liveprop(ns_uri, name, &dav_quota_liveprop_group, hooks);
}

static void dav_quota_insert_all_liveprops(request_rec * r,
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

    s = apr_psprintf(r->pool, "<D:quota-available-bytes/>" DEBUG_CR
                     "<D:quota-used-bytes/>");

    if (what == DAV_PROP_INSERT_NAME) {
        apr_text_append(r->pool, phdr, s);
    } else if (what == DAV_PROP_INSERT_SUPPORTED) {
        s = apr_psprintf(r->pool, "<D:supported-live-property><D:prop>" DEBUG_CR
                         "%s" DEBUG_CR "</D:prop></D:supported-live-property>", s);
        apr_text_append(r->pool, phdr, s);
    }

}

void dav_quota_register_liveprops(apr_pool_t *p)
{
    dav_hook_find_liveprop(dav_quota_find_liveprop, NULL, NULL,
                           APR_HOOK_MIDDLE);

    dav_hook_insert_all_liveprops(dav_quota_insert_all_liveprops, NULL,
                                  NULL, APR_HOOK_MIDDLE);
    /* register the namespace URIs */
    dav_register_liveprop_group(p, &dav_quota_liveprop_group);
}
