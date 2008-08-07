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
#include "acl.h"
#include "principal.h"
#include "dbms_principal.h"

#include <apr_strings.h>

/* forward-declare */
static const dav_hooks_liveprop dav_hooks_acl_liveprop;

/*
** The namespace URIs that we use. This list and the enumeration must
** stay in sync.
*/
static const char *const dav_acl_namespace_uris[] = {
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
static const dav_liveprop_spec dav_acl_props[] = {
    {
     dav_repos_URI_DAV,
     "owner",
     DAV_PROPID_owner,
     0},
    {
     dav_repos_URI_DAV,
     "supported-privilege-set",
     DAV_PROPID_supported_privilege_set,
     0},
    {
     dav_repos_URI_DAV,
     "current-user-privilege-set",
     DAV_PROPID_current_user_privilege_set,
     0},
    {
     dav_repos_URI_DAV,
     "acl",
     DAV_PROPID_acl,
     0},
    {
     dav_repos_URI_DAV,
     "acl-restrictions",
     DAV_PROPID_acl_restrictions,
     0},
    {
     dav_repos_URI_DAV,
     "inherited-acl-set",
     DAV_PROPID_inherited_acl_set,
     0},
    {
     dav_repos_URI_DAV,
     "principal-collection-set",
     DAV_PROPID_principal_collection_set,
     0},
    {
     dav_repos_URI_DAV,
     "group-member-set",
     DAV_PROPID_group_member_set,
     1},

    {0}				/* sentinel */
};

static const dav_liveprop_group dav_acl_liveprop_group = {
    dav_acl_props,
    dav_acl_namespace_uris,
    &dav_hooks_acl_liveprop
};

static dav_prop_insert dav_acl_insert_prop(const dav_resource * resource,
                                           int propid,
                                           dav_prop_insert what,
                                           apr_text_header * phdr)
{
    dav_repos_db *db = resource->info->db;
    dav_repos_resource *db_r = resource->info->db_r;
    apr_pool_t *pool = db_r->p;
    const dav_liveprop_spec *spec;
    const char *s, *name, *owner, *val = "";
    dav_principal *principal;
    request_rec *r = resource->info->rec;

    TRACE();

    spec = get_livepropspec_from_id(dav_acl_props, propid);
    name = spec->name;

    /* ACL liveprops are not defined for locknull resources */
    if(db_r->resourcetype == dav_repos_LOCKNULL)
        return DAV_PROP_INSERT_NOTDEF;

    if (what == DAV_PROP_INSERT_SUPPORTED) {
	s = apr_psprintf(pool,
                         "<D:supported-live-property><D:prop>" DEBUG_CR
                         "<D:%s/>" DEBUG_CR
                         "</D:prop></D:supported-live-property>" DEBUG_CR,
                         name);
        apr_text_append(pool, phdr, s);
        return what;
    } else if (what != DAV_PROP_INSERT_VALUE)
        return what;

    switch (propid) {
    case DAV_PROPID_owner:
        owner = dbms_lookup_prin_id(pool, db, db_r->owner_id);
        principal = dav_repos_get_prin_by_name(db_r->resource->info->rec, owner);
        val = apr_psprintf(pool, "<D:href>%s</D:href>", dav_repos_principal_to_s(principal));
        break;
    case DAV_PROPID_supported_privilege_set:
        val = acl_supported_privilege_set(resource);
        break;
    case DAV_PROPID_current_user_privilege_set:
        principal = dav_principal_make_from_request(resource->info->rec);
        if (!dav_repos_is_allow(principal, (dav_resource *)resource,
                                DAV_PERMISSION_READ_CURRENT_USER_PRIVILEGE_SET))
            return DAV_PROP_INSERT_FORBIDDEN;
        val = acl_current_user_privilege_set(resource);
        break;
    case DAV_PROPID_acl:
        principal = dav_principal_make_from_request(resource->info->rec);
        if (!dav_repos_is_allow(principal, (dav_resource *)resource, 
                                DAV_PERMISSION_READ_ACL))
            return DAV_PROP_INSERT_FORBIDDEN;
        val = acl_build_propfind_output(resource);
        break;
    case DAV_PROPID_acl_restrictions:
        val = acl_restrictions(resource);
        break;
    case DAV_PROPID_inherited_acl_set:
        val = acl_inherited_acl_set(resource);
        break;
    case DAV_PROPID_principal_collection_set:
        if (db_r->resourcetype != dav_repos_GROUP)
            return DAV_PROP_INSERT_NOTSUPP;
        val = apr_pstrcat(pool, "<D:href>%s%s</D:href><D:href>%s%s</D:href>",
                          principal_href_prefix(r), PRINCIPAL_USER_PREFIX,
                          principal_href_prefix(r), PRINCIPAL_GROUP_PREFIX,NULL);
        break;
    case DAV_PROPID_group_member_set:
        val = get_group_member_set(resource);
        break;
    }
    s = apr_psprintf(pool, "<D:%s>%s</D:%s>", name, val, name);
    apr_text_append(pool, phdr, s);

    return what;
}

static int dav_acl_is_writable(const dav_resource * resource, int propid)
{
    int i;

    TRACE();

    /* Try to find is_writable */
    for (i = 0; dav_acl_props[i].name; i++) {
	if (propid == dav_acl_props[i].propid) {
	    return dav_acl_props[i].is_writable;
	}
    }

    /* If not found */
    return 0;
}

const char *get_name_from_principal_URL(request_rec *rec,
                                        const char *principal_URL)
{
    const char *name = NULL;
    const char *prin_href_pref = principal_href_prefix(rec);

    TRACE();

    if (principal_URL == NULL)
        return NULL;

    if (principal_URL != strstr(principal_URL, prin_href_pref))
        return NULL;

    principal_URL = principal_URL + strlen(prin_href_pref);

    if(strstr(principal_URL, PRINCIPAL_USER_PREFIX))
        name = principal_URL + strlen(PRINCIPAL_USER_PREFIX);
    else if(strstr(principal_URL, PRINCIPAL_GROUP_PREFIX))
        name = principal_URL + strlen(PRINCIPAL_GROUP_PREFIX);
    return name;
}

static dav_error *dav_acl_patch_validate(const dav_resource * resource,
                                         const apr_xml_elem * elem,
                                         int operation,
                                         void **context,
                                         int *defer_to_dead)
{
    apr_pool_t *pool = resource->pool;
    dav_repos_resource *db_r = resource->info->db_r;
    dav_repos_db *db = resource->info->db;
    request_rec *rec = resource->info->rec;
    dav_elem_private *priv = elem->priv;
    dav_error *err = NULL;

    TRACE();

    if (priv->propid == DAV_PROPID_group_member_set) {
	*defer_to_dead = 0;
        if (db_r->resourcetype!=dav_repos_GROUP || operation!=DAV_PROP_OP_SET)
            return dav_new_error(pool, HTTP_FORBIDDEN, 0,
                                 "Not a set operation on a group");
            
        apr_xml_elem *href_elem = dav_find_child(elem, "href");
        apr_hash_t *new_members = apr_hash_make(pool);
        apr_array_header_t *to_remove;
        while (href_elem && !err) {
            const char *prin_uri = dav_xml_get_cdata(href_elem, pool, 1);
            const char *prin_name = get_name_from_principal_URL(rec, prin_uri);
            if (prin_name == NULL)
                err = dav_new_error
                  (pool, HTTP_CONFLICT, 0, "Not a DAV:principal-URL");
            else
                apr_hash_set(new_members, prin_name, APR_HASH_KEY_STRING, "");
            href_elem = href_elem->next;
        }
        if (err) return err;
        err = dbms_calculate_group_changes(db, db_r, new_members, &to_remove);
        if (err) return err;
        apr_hash_set(new_members, "-to-remove-", APR_HASH_KEY_STRING, to_remove);
        *context = new_members;
    }
    return err;
}

static dav_error *dav_acl_patch_exec(const dav_resource * resource,
                                     const apr_xml_elem * elem,
                                     int operation,
                                     void *context,
                                     dav_liveprop_rollback **rollback_ctx)
{
    dav_repos_resource *db_r = resource->info->db_r;
    const dav_repos_db *db = resource->info->db;
    dav_elem_private *priv = elem->priv;
    dav_error *err = NULL;

    if (priv->propid == DAV_PROPID_group_member_set) {
        apr_hash_t *new_members = (apr_hash_t*)context;
        apr_array_header_t *to_remove = (apr_array_header_t *)apr_hash_get
          (new_members, "-to-remove-", APR_HASH_KEY_STRING);
        apr_hash_set(new_members, "-to-remove-", APR_HASH_KEY_STRING, NULL);
        dav_repos_resource *prin = NULL;
        int i = 0;
        for (i = 0; to_remove && i < to_remove->nelts; i++) {
            prin = ((dav_repos_resource **)to_remove->elts)[i];
            err = sabridge_rem_prin_frm_grp
              (db_r->p, db, db_r->serialno, prin->serialno);
            if (err) return err;
        }
        
        apr_hash_index_t *iter = apr_hash_first(db_r->p, new_members);
        while (iter) {
            apr_hash_this(iter, NULL, NULL, (void *)&prin);
            err = sabridge_add_prin_to_grp
              (db_r->p, db, db_r->serialno, prin->serialno);
            if (err) return err;
            iter = apr_hash_next(iter);
        }
    }
    return NULL;
}

static void dav_acl_patch_commit(const dav_resource * resource,
                                 int operation,
                                 void *context,
                                 dav_liveprop_rollback *rollback_ctx)
{

}

static const dav_hooks_liveprop dav_hooks_acl_liveprop = {
    dav_acl_insert_prop,
    dav_acl_is_writable,
    dav_acl_namespace_uris,
    dav_acl_patch_validate,
    dav_acl_patch_exec,
    dav_acl_patch_commit,
    NULL
};

/* Find live prop and return its propid */
static int dav_acl_find_liveprop(const dav_resource * resource,
                                 const char *ns_uri, const char *name,
                                 const dav_hooks_liveprop ** hooks)
{
    return dav_do_find_liveprop(ns_uri, name, &dav_acl_liveprop_group, hooks);
}

static void dav_acl_insert_all_liveprops(request_rec * r,
                                         const dav_resource * resource,
                                         dav_prop_insert what,
                                         apr_text_header * phdr)
{
    const char *s, *val = "";

    /* don't try to find any liveprops if this isn't "our" resource */
    if (resource->hooks != &dav_repos_hooks_repos)
	return;

    TRACE();

    if (!resource->exists || resource->type != DAV_RESOURCE_TYPE_REGULAR)
        return;

    if (what == DAV_PROP_INSERT_VALUE)
        return;

    s = list_all_liveprops(r->pool, dav_acl_props);

    if (what == DAV_PROP_INSERT_NAME)
        val = s;
    else if (what == DAV_PROP_INSERT_SUPPORTED)
        val = apr_pstrcat(r->pool, "<D:supported-live-property><D:prop>", s,
                          "</D:prop></D:supported-live-property>",  NULL);
    apr_text_append(r->pool, phdr, val);
}

void dav_acl_register_liveprops(apr_pool_t *p)
{
    dav_hook_find_liveprop(dav_acl_find_liveprop, NULL, NULL,
                           APR_HOOK_MIDDLE);

    dav_hook_insert_all_liveprops(dav_acl_insert_all_liveprops, NULL,
                                  NULL, APR_HOOK_MIDDLE);
    /* register the namespace URIs */
    dav_register_liveprop_group(p, &dav_acl_liveprop_group);
}
