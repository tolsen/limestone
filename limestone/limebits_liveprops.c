#include "dav_repos.h"
#include "liveprops.h"
#include "dbms_acl.h"
#include "dbms_principal.h"
#include "bridge.h"
#include <apr_strings.h>

/* forward-declare */
static const dav_hooks_liveprop dav_hooks_limebits_liveprop;

/*
** The namespace URIs that we use. This list and the enumeration must
** stay in sync.
*/
static const char *const dav_limebits_namespace_uris[] = {
    "DAV:",
    LIMEBITS_NS,
    NULL			/* sentinel */
};

enum {
    dav_repos_URI_DAV,		/* the DAV: namespace URI */
    dav_repos_URI_LB            /* the lb: namespace URI */
};

enum {
    LB_PROPID_BEGIN = 30000,

    LB_PROPID_email_id,

    LB_PROPID_domain_map,

    LB_PROPID_used_bytes,

    LB_PROPID_login_to_all_domains,

    LB_PROPID_auto_version_new_children,

    LB_PROPID_limebar_state,

    LB_PROPID_END
};

/*
** Define each of the core properties that this provider will handle.
** Note that all of them are in the DAV: namespace, which has a
** provider-local index of 0.
*/
static const dav_liveprop_spec dav_limebits_props[] = {
    {
     dav_repos_URI_LB,
     "email",
     LB_PROPID_email_id,
     1},

    {
     dav_repos_URI_LB,
     "domain-map",
     LB_PROPID_domain_map,
     1},

    {
     dav_repos_URI_LB,
     "used-bytes",
     LB_PROPID_used_bytes,
     0},

    {
     dav_repos_URI_LB,
     "login-to-all-domains",
     LB_PROPID_login_to_all_domains,
     1},

    {
     dav_repos_URI_LB,
     "auto-version-new-children",
     LB_PROPID_auto_version_new_children,
     1},

    {
     dav_repos_URI_LB,
     "limebar-state",
     LB_PROPID_limebar_state,
     1},

    {0}				/* sentinel */
};

static const dav_liveprop_group dav_limebits_liveprop_group = {
    dav_limebits_props,
    dav_limebits_namespace_uris,
    &dav_hooks_limebits_liveprop
};

static int is_allow_read_private_properties(const dav_resource *resource) {
    dav_repos_resource *db_r = resource->info->db_r;
    dav_repos_db *db = resource->info->db;
    request_rec *r = resource->info->rec;
    long priv_ns_id;
    dav_principal *principal = dav_repos_get_prin_by_name(r, r->user);
    dbms_get_namespace_id(db_r->p, db, dav_limebits_namespace_uris[1], &priv_ns_id);
    int is_allow = dbms_is_allow(db, priv_ns_id, "read-private-properties",
                                 principal, db_r);
    return is_allow;
}

static const char *domain_map_to_xml(apr_pool_t *pool, apr_hash_t *domain_map)
{
    apr_hash_index_t *hi;
    void *path;
    const void *domain;
    const char *xmlstr = NULL;

    if(NULL == domain_map) {
        return NULL;
    }

    for(hi = apr_hash_first(pool, domain_map); hi;
        hi = apr_hash_next(hi)) {

        apr_hash_this(hi, &domain, NULL, &path);
        xmlstr = apr_pstrcat(pool, xmlstr ? xmlstr : " ", 
                             "<lb:domain-map-entry>", 
                             "<lb:domain>", (char *)domain, "</lb:domain>", 
                             "<lb:path>", (char *)path, "</lb:path>",
                             "</lb:domain-map-entry>", NULL);
    }

    return xmlstr;
}

static dav_prop_insert dav_limebits_insert_prop(const dav_resource * resource,
                                                int propid,
                                                dav_prop_insert what,
                                                apr_text_header * phdr)
{
    dav_repos_resource *db_r = resource->info->db_r;
    dav_repos_db *db = resource->info->db;
    const dav_liveprop_spec *spec;
    apr_pool_t *pool = db_r->p;
    const char *name, *namespace;
    const char *s = "";
    TRACE();

    spec = get_livepropspec_from_id(dav_limebits_props, propid);
    name = spec->name;
    namespace = dav_limebits_namespace_uris[spec->ns];

    if (what == DAV_PROP_INSERT_VALUE) {
        if (propid == LB_PROPID_email_id) {
            if (!is_allow_read_private_properties(resource))
                return DAV_PROP_INSERT_FORBIDDEN;
            s = dbms_get_user_email(db_r->p, db, db_r->serialno);
        }
        else if (propid == LB_PROPID_domain_map) {
            apr_hash_t *domain_map = dbms_get_domain_map(db_r->p, db, db_r->serialno);
            s = domain_map_to_xml(db_r->p, domain_map);
        } else if (propid == LB_PROPID_used_bytes) {
            long used_bytes = sabridge_get_used_bytes(db, db_r, 0);
            s = apr_psprintf(pool, "%ld", used_bytes);
        } else if (propid == LB_PROPID_login_to_all_domains) {
            long login_to_all_domains;
            if (!is_allow_read_private_properties(resource))
                return DAV_PROP_INSERT_FORBIDDEN;
            login_to_all_domains = dbms_get_user_login_to_all_domains
              (db_r->p, db, db_r->serialno);
            if (login_to_all_domains)
                s = "true";
            else s = "false";
        } else if (propid == LB_PROPID_auto_version_new_children) {
            if (!db_r->av_new_children)
                dbms_get_collection_props(db, db_r);

            switch (db_r->av_new_children) {
            case DAV_AV_CHECKOUT_CHECKIN:
                s = "<D:checkout-checkin/>";
                break;
            case DAV_AV_CHECKOUT_UNLOCKED_CHECKIN:
                s = "<D:checkout-unlocked-checkin/>";
                break;
            case DAV_AV_CHECKOUT:
                s = "<D:checkout/>";
                break;
            case DAV_AV_LOCKED_CHECKOUT:
                s = "<D:locked-checkout/>";
                break;
            case DAV_AV_VERSION_CONTROL:
                s = "<lb:version-control/>";
                break;
            default:
                s = NULL;
                break;
            }
        } else if (propid == LB_PROPID_limebar_state) {
            s = db_r->limebar_state;
            s = s ? s : "";
        }

        if (s == NULL) {
            return DAV_PROP_INSERT_NOTSUPP;
        }
        else {
            s = apr_psprintf(db_r->p, "<lb:%s xmlns:lb='%s'>%s</lb:%s>", 
                             name, namespace, s, name);
        }

    } else if (what == DAV_PROP_INSERT_SUPPORTED) {
	s = apr_psprintf(pool,
			 "<D:supported-live-property><D:prop>" DEBUG_CR
                         "<lb:%s xmlns:lb='%s'/>" DEBUG_CR
			 "</D:prop></D:supported-live-property>" DEBUG_CR,
			 name, namespace);
    }

    apr_text_append(pool, phdr, s);
    /* we inserted what was asked for */
    return what;
}

static int dav_limebits_is_writable(const dav_resource * resource, int propid)
{
    TRACE();

    return 1;
}

static dav_error *dav_limebits_patch_validate(const dav_resource * resource,
                                              const apr_xml_elem * elem,
                                              int operation,
                                              void **context,
                                              int *defer_to_dead)
{
    dav_repos_resource *db_r = (dav_repos_resource *) resource->info->db_r;
    dav_elem_private *priv = elem->priv;

    TRACE();

    *context = (void *)get_livepropspec_from_id(dav_limebits_props, priv->propid);
    if (priv->propid == LB_PROPID_email_id) {
        if (operation == DAV_PROP_OP_DELETE && db_r->resourcetype == dav_repos_USER)
            return dav_new_error(resource->pool, HTTP_FORBIDDEN,
                                 0, "lb:email can't be deleted on principals");
        if (!is_allow_read_private_properties(resource))
            return dav_new_error(resource->pool, 
                                 dav_get_permission_denied_status(resource->info->rec),
                                 0, "lb:read-private-propertes privilege needed");
    }
    else if (priv->propid == LB_PROPID_domain_map) {
        if (operation == DAV_PROP_OP_DELETE && db_r->resourcetype == dav_repos_USER)
            return dav_new_error(resource->pool, HTTP_FORBIDDEN,
                                 0, "lb:domain_map can't be deleted on principals");
    } else if (priv->propid == LB_PROPID_login_to_all_domains) {
        if (operation == DAV_PROP_OP_DELETE &&
            db_r->resourcetype == dav_repos_USER)
            return dav_new_error
              (resource->pool, HTTP_FORBIDDEN,
               0, "lb:login-to-all-domains can't be deleted on principals");
        if (!is_allow_read_private_properties(resource))
            return dav_new_error(resource->pool, 
                                 dav_get_permission_denied_status(resource->info->rec),
                                 0, "lb:read-private-propertes privilege needed");
    } else if (priv->propid == LB_PROPID_auto_version_new_children) {
        if (resource->collection && operation == DAV_PROP_OP_DELETE) {
            return NULL;
        }
        if (resource->collection) {
	    char *av_value = elem->first_child ?
		apr_pstrdup(resource->pool, elem->first_child->name) : "";
	    if (!(strcmp(av_value, "checkout-checkin") &&
		  strcmp(av_value, "version-control")));
            else if (!(strcmp(av_value, "checkout-unlocked-checkin") &&
                       strcmp(av_value, "checkout") &&
                       strcmp(av_value, "locked-checkout")))
                return dav_new_error
                  (resource->pool, HTTP_FORBIDDEN, 0, apr_psprintf
                   (resource->pool,"%s not supported currently", av_value));
	    else
		return dav_new_error
                  (resource->pool, HTTP_BAD_REQUEST, 0,
                   "Undefined value for lb:auto-version-new-children");
        }
    } else if (priv->propid == LB_PROPID_limebar_state) {
        if (operation == DAV_PROP_OP_DELETE) {
            return dav_new_error(resource->pool, HTTP_FORBIDDEN, 0,
                                 "lb:limebar-state can't be deleted");
        }
    }

    return NULL;
}

apr_xml_elem *lb_find_child(const apr_xml_elem *elem, const char *tagname)
{
    apr_xml_elem *child = elem->first_child;

    for (; child; child = child->next)
        if (!strcmp(child->name, tagname))
            return child;
    return NULL;
}

static apr_hash_t *domain_map_elem_to_hash(apr_pool_t *pool, 
                                           const apr_xml_elem *elem,
                                           const char *filter)
{
    apr_xml_elem *iter;
    apr_hash_t *domain_map = NULL;

    TRACE();

    for(iter = elem->first_child; iter; iter = iter->next) {
        apr_xml_elem *domain = lb_find_child(iter, "domain");
        const char *domainstr = dav_xml_get_cdata(domain, pool, 1 /* strip white */);
        if (strstr(domainstr, filter)) {
            continue;   /* skip illegal domains */
        }
        apr_xml_elem *path = lb_find_child(iter, "path");
        const char *pathstr = dav_xml_get_cdata(path, pool, 1 /* strip white */);
        if (strstr(pathstr, "!lime") || strstr(pathstr, "..")) {
            continue; /* skip illegal paths */
        }
        if (NULL == domain_map) {
            domain_map = apr_hash_make(pool);
        }

        apr_hash_set(domain_map, domainstr, APR_HASH_KEY_STRING, pathstr);
    }

    return domain_map;
}

static dav_error *dav_limebits_patch_exec(const dav_resource * resource,
                                          const apr_xml_elem * elem,
                                          int operation,
                                          void *context,
                                          dav_liveprop_rollback **rollback_ctx)
{
    dav_repos_resource *db_r = (dav_repos_resource *) resource->info->db_r;
    dav_repos_db *db = resource->info->db;
    const dav_liveprop_spec *spec = context;
    dav_liveprop_rollback *rollback_info;
    dav_error *err = NULL;

    TRACE();

    rollback_info = apr_pcalloc(resource->pool, sizeof(dav_liveprop_rollback));
    rollback_info->spec = spec;
    rollback_info->operation = operation;
    *rollback_ctx = rollback_info;

    if (spec->propid == LB_PROPID_email_id) {
        const char *email = dbms_get_user_email(db_r->p, db, db_r->serialno);
        rollback_info->rollback_data = (char *)email;
        
        if (operation == DAV_PROP_OP_SET) {
            apr_xml_to_text (db_r->p, elem, APR_XML_X2T_INNER, 
                             NULL, NULL, &email, NULL);
            err = sabridge_set_user_email(db_r->p, db, db_r->serialno, email);
        }
    }
    else if (spec->propid == LB_PROPID_domain_map) {
        /* fetch the current value in case we need to rollback later */
        apr_hash_t *domain_map = dbms_get_domain_map(db_r->p, db, db_r->serialno);
        rollback_info->rollback_data = domain_map;

        /* set the new value */
        if (operation == DAV_PROP_OP_SET) {
            apr_hash_t *new_domain_map = 
                domain_map_elem_to_hash(db_r->p, elem,
                                        resource->info->rec->server->defn_name);
            err = dbms_set_domain_map(db_r->p, db, db_r->serialno, new_domain_map);
        }
    } else if (spec->propid == LB_PROPID_login_to_all_domains) {
        long cur_val = dbms_get_user_login_to_all_domains
          (db_r->p, db, db_r->serialno);
        
        if (operation == DAV_PROP_OP_SET) {
            const char *val_str;
            long new_val;
            apr_xml_to_text (db_r->p, elem, APR_XML_X2T_INNER, 
                             NULL, NULL, &val_str, NULL);
            new_val = (0 == strcmp(val_str, "true"));
            if (new_val != cur_val) {
                err = dbms_set_user_login_to_all_domains
                  (db_r->p, db, db_r->serialno, new_val);
                rollback_info->rollback_data = (char *)(cur_val ? "t" : "f");
            }
        }
    } else if (spec->propid == LB_PROPID_auto_version_new_children) {
        char *av_value = elem->first_child ?
          apr_pstrdup(resource->pool, elem->first_child->name) : "";

        rollback_info->rollback_data =
          apr_psprintf(resource->pool, "%d", db_r->autoversion_type);

        if (operation == DAV_PROP_OP_DELETE)
            db_r->av_new_children = DAV_AV_NONE;
        else if (!strcmp(av_value, "checkout-checkin")) {
            db_r->av_new_children = DAV_AV_CHECKOUT_CHECKIN;
        } else if (!strcmp(av_value, "checkout-unlocked-checkin")) {
            db_r->av_new_children = DAV_AV_CHECKOUT_UNLOCKED_CHECKIN;
        } else if (!strcmp(av_value, "checkout")) {
            db_r->av_new_children = DAV_AV_CHECKOUT;
        } else if (!strcmp(av_value, "locked-checkout")) {
            db_r->av_new_children = DAV_AV_LOCKED_CHECKOUT;
        } else if (!strcmp(av_value, "version-control")) {
            db_r->av_new_children = DAV_AV_VERSION_CONTROL;
        }
    } else if (spec->propid == LB_PROPID_limebar_state) {
        const char *limebar_state = NULL;
        if (operation == DAV_PROP_OP_SET) {
            apr_xml_to_text (db_r->p, elem, APR_XML_X2T_INNER, 
                             NULL, NULL, &limebar_state, NULL);
            db_r->limebar_state = limebar_state;
            err = dbms_set_limebar_state(db, db_r);
        }
    }

    return err;
}

static void dav_limebits_patch_commit(const dav_resource * resource,
                                      int operation,
                                      void *context,
                                      dav_liveprop_rollback *rollback_ctx)
{
    dav_repos_resource *db_r = (dav_repos_resource *) resource->info->db_r;
    dav_repos_db *db = resource->info->db;

    TRACE();

    if (rollback_ctx->spec->propid == LB_PROPID_auto_version_new_children)
	if (resource->collection
	    && rollback_ctx->operation == DAV_PROP_OP_SET) {
	    int av_type;
	    sscanf(rollback_ctx->rollback_data, "%d", &av_type);
	    if (db_r->av_new_children != av_type)
		dbms_set_collection_new_children_av_type(db, db_r);
	}
}

static dav_error *dav_limebits_patch_rollback(const dav_resource * resource,
                                              int operation,
                                              void *context,
                                              dav_liveprop_rollback * rollback_ctx)
{
    dav_repos_resource *db_r = (dav_repos_resource *) resource->info->db_r;
    dav_repos_db *db = resource->info->db;
    dav_error *err = NULL;

    TRACE();

    if (rollback_ctx->spec->propid == LB_PROPID_email_id) {
        err = sabridge_set_user_email(db_r->p, db, db_r->serialno, 
                                      rollback_ctx->rollback_data);
    }
    else if (rollback_ctx->spec->propid == LB_PROPID_domain_map) {
        err = dbms_set_domain_map(db_r->p, db, db_r->serialno, 
                                  rollback_ctx->rollback_data);
    } else if (rollback_ctx->spec->propid == LB_PROPID_login_to_all_domains) {
        const char *val = rollback_ctx->rollback_data;
        if (val)
            err = dbms_set_user_login_to_all_domains
              (db_r->p, db, db_r->serialno, (*val == 't'));
    }

    return err;
}

static const dav_hooks_liveprop dav_hooks_limebits_liveprop = {
    dav_limebits_insert_prop,
    dav_limebits_is_writable,
    dav_limebits_namespace_uris,
    dav_limebits_patch_validate,
    dav_limebits_patch_exec,
    dav_limebits_patch_commit,
    dav_limebits_patch_rollback
};

/* Find live prop and return its propid */
static int dav_limebits_find_liveprop(const dav_resource * resource,
                                      const char *ns_uri, const char *name,
                                      const dav_hooks_liveprop ** hooks)
{
    return dav_do_find_liveprop(ns_uri, name, &dav_limebits_liveprop_group, hooks);
}

static void dav_limebits_insert_all_liveprops(request_rec * r,
                                              const dav_resource * resource,
                                              dav_prop_insert what,
                                              apr_text_header * phdr)
{
    TRACE();

    return;
}

void dav_limebits_register_liveprops(apr_pool_t *p)
{
    dav_hook_find_liveprop(dav_limebits_find_liveprop, NULL, NULL,
                           APR_HOOK_MIDDLE);

    dav_hook_insert_all_liveprops(dav_limebits_insert_all_liveprops, NULL,
                                  NULL, APR_HOOK_MIDDLE);
    /* register the namespace URIs */
    dav_register_liveprop_group(p, &dav_limebits_liveprop_group);
}
