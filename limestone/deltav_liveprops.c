
#include "liveprops.h"
#include "deltav_bridge.h"
#include "dbms_deltav.h" /* for set_autoversion_type */

#include <apr_strings.h>

/* forward-declare */
static const dav_hooks_liveprop dav_hooks_deltav_liveprop;

/*
** The namespace URIs that we use. This list and the enumeration must
** stay in sync.
*/
static const char *const dav_deltav_namespace_uris[] = {
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
static const dav_liveprop_spec dav_deltav_props[] = {
    {
     dav_repos_URI_DAV,
     "checked-in",
     DAV_PROPID_checked_in,
     0},
    {
     dav_repos_URI_DAV,
     "checked-out",
     DAV_PROPID_checked_out,
     0},
    {
     dav_repos_URI_DAV,
     "version-history",
     DAV_PROPID_version_history,
     0},
    {
     dav_repos_URI_DAV,
     "checkout-fork",
     DAV_PROPID_checkout_fork,
     0},
    {
     dav_repos_URI_DAV,
     "checkin-fork",
     DAV_PROPID_checkin_fork,
     0},
    {
     dav_repos_URI_DAV,
     "auto-version",
     DAV_PROPID_auto_version,
     1},
    {
     dav_repos_URI_DAV,
     "eclipsed-set",
     DAV_PROPID_eclipsed_set,
     0},
    {
     dav_repos_URI_DAV,
     "predecessor-set",
     DAV_PROPID_predecessor_set,
     0},
    {
     dav_repos_URI_DAV,
     "successor-set",
     DAV_PROPID_successor_set,
     0},
    {
     dav_repos_URI_DAV,
     "checkout-set",
     DAV_PROPID_checkout_set,
     0},
    {
     dav_repos_URI_DAV,
     "version-name",
     DAV_PROPID_version_name,
     0},
    {
     dav_repos_URI_DAV,
     "creator-displayname",
     DAV_PROPID_creator_displayname,
     0},
    {
     dav_repos_URI_DAV,
     "comment",
     DAV_PROPID_comment,
     0},
    {
     dav_repos_URI_DAV,
     "root-version",
     DAV_PROPID_root_version,
     0},
    {
     dav_repos_URI_DAV,
     "version-set",
     DAV_PROPID_version_set,
     0},
    {
     dav_repos_URI_DAV,
     "version-controlled-binding-set",
     DAV_PROPID_version_controlled_binding_set,
     0},

    {0}				/* sentinel */
};

static const dav_liveprop_group dav_deltav_liveprop_group = {
    dav_deltav_props,
    dav_deltav_namespace_uris,
    &dav_hooks_deltav_liveprop
};

static dav_prop_insert dav_deltav_insert_prop(const dav_resource * resource,
                                              int propid,
                                              dav_prop_insert what,
                                              apr_text_header * phdr)
{
    const dav_liveprop_spec *spec;
    const char *value = NULL;
    const char *name = NULL;
    const char *s = NULL;
    apr_pool_t *pool = resource->pool;
    dav_repos_resource *dbr = (dav_repos_resource *) resource->info->db_r;

    TRACE();

    if (!resource->exists)
	return DAV_PROP_INSERT_NOTDEF;

    /* find propname using prop id */
    spec = get_livepropspec_from_id(dav_deltav_props, propid);
    name = spec->name;

    /* ### what the heck was this property? */
    if (name == NULL)
	return DAV_PROP_INSERT_NOTDEF;

    /* Build version property hash and get value */
    if (!dbr->vpr_hash)
        sabridge_build_vpr_hash(resource->info->db, dbr);
    if (dbr->vpr_hash)
        value = apr_hash_get(dbr->vpr_hash, name, APR_HASH_KEY_STRING);

    /* ### Not found in the hash */
    if (value == NULL)
	return DAV_PROP_INSERT_NOTSUPP;

    /* Do something according to what */
    if (what == DAV_PROP_INSERT_VALUE) {
	s = apr_psprintf(pool, "<D:%s>%s</D:%s>" DEBUG_CR, name, value,
			 name);
    } else if (what == DAV_PROP_INSERT_NAME) {
	s = apr_psprintf(pool, "<D:%s/>" DEBUG_CR, name);
    } else {
	/* assert: what == DAV_PROP_INSERT_SUPPORTED */
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

static int dav_deltav_is_writable(const dav_resource * resource, int propid)
{
    int i;

    TRACE();

    /* Try to find is_writable */
    for (i = 0; dav_deltav_props[i].name; i++) {
	if (propid == dav_deltav_props[i].propid) {
	    return dav_deltav_props[i].is_writable;
	}
    }

    /* It is writable */
    return 1;
}

static dav_error *dav_deltav_patch_validate(const dav_resource * resource,
                                            const apr_xml_elem * elem,
                                            int operation,
                                            void **context,
                                            int *defer_to_dead)
{
    dav_elem_private *priv = elem->priv;

    TRACE();

    *context = (void *)get_livepropspec_from_id(dav_deltav_props, priv->propid);
    if (priv->propid == DAV_PROPID_auto_version) {
	*defer_to_dead = 0;
	if (resource->versioned && operation == DAV_PROP_OP_SET) {
	    char *av_value = elem->first_child ?
		apr_pstrdup(resource->pool, elem->first_child->name) : "";
	    if (!(strcmp(av_value, "checkout-checkin")))
                ;
            else if(!(strcmp(av_value, "checkout-unlocked-checkin") &&
                      strcmp(av_value, "checkout") &&
                      strcmp(av_value, "locked-checkout")))
                return dav_new_error
                  (resource->pool, HTTP_FORBIDDEN, 0, apr_psprintf
                   (resource->pool, "%s supported currently", av_value));
            else
		return dav_new_error(resource->pool, HTTP_BAD_REQUEST, 0,
				     "Undefined value given for DAV:auto-version ");
	} else
	    return dav_new_error(resource->pool, HTTP_FORBIDDEN, 0,
				 "Not a set operation on a VCR");
    }
    return NULL;
}

static dav_error *dav_deltav_patch_exec(const dav_resource * resource,
                                        const apr_xml_elem * elem,
                                        int operation,
                                        void *context,
                                        dav_liveprop_rollback **rollback_ctx)
{

    dav_repos_resource *db_r = (dav_repos_resource *) resource->info->db_r;
    const dav_liveprop_spec *spec = context;
    dav_liveprop_rollback *rollback_info;
    TRACE();


    if (spec->propid == DAV_PROPID_auto_version)
	if (resource->versioned && operation == DAV_PROP_OP_SET) {
	    char *av_value =
		apr_pstrdup(resource->pool, elem->first_child->name);

	    rollback_info =
		apr_pcalloc(resource->pool,
			    sizeof(dav_liveprop_rollback));
	    rollback_info->spec = spec;
	    rollback_info->operation = operation;
	    rollback_info->rollback_data =
		apr_psprintf(resource->pool, "%d", db_r->autoversion_type);

	    *rollback_ctx = rollback_info;

	    if (!(strcmp(av_value, "checkout-checkin")))
		db_r->autoversion_type = DAV_AV_CHECKOUT_CHECKIN;
	    else if (!(strcmp(av_value, "checkout-unlocked-checkin")))
		db_r->autoversion_type =
		    DAV_AV_CHECKOUT_UNLOCKED_CHECKIN;
	    else if (!(strcmp(av_value, "checkout")))
		db_r->autoversion_type = DAV_AV_CHECKOUT;
	    else if (!(strcmp(av_value, "locked-checkout")))
		db_r->autoversion_type = DAV_AV_LOCKED_CHECKOUT;
	}

    return NULL;
}

static void dav_deltav_patch_commit(const dav_resource * resource,
                                    int operation,
                                    void *context,
                                    dav_liveprop_rollback *rollback_ctx)
{
    dav_repos_resource *db_r = (dav_repos_resource *) resource->info->db_r;
    dav_repos_db *db = resource->info->db;

    TRACE();

    if (rollback_ctx->spec->propid == DAV_PROPID_auto_version)
	if (resource->versioned
	    && rollback_ctx->operation == DAV_PROP_OP_SET) {
	    int av_type;
	    sscanf(rollback_ctx->rollback_data, "%d", &av_type);
	    if (db_r->autoversion_type != av_type)
		dbms_set_autoversion_type(db, db_r, db_r->autoversion_type);
	}

}

static dav_error *dav_deltav_patch_rollback(const dav_resource * resource,
                                            int operation,
                                            void *context,
                                            dav_liveprop_rollback * rollback_ctx)
{
    dav_repos_resource *db_r = (dav_repos_resource *) resource->info->db_r;

    TRACE();

    if (rollback_ctx->spec->propid == DAV_PROPID_auto_version)
	if (resource->versioned
	    && rollback_ctx->operation == DAV_PROP_OP_SET) {
	    int av_type;
	    sscanf((char *) rollback_ctx->rollback_data, "%d", &av_type);
	    db_r->autoversion_type = av_type;
	}
    return NULL;
}

static const dav_hooks_liveprop dav_hooks_deltav_liveprop = {
    dav_deltav_insert_prop,
    dav_deltav_is_writable,
    dav_deltav_namespace_uris,
    dav_deltav_patch_validate,
    dav_deltav_patch_exec,
    dav_deltav_patch_commit,
    dav_deltav_patch_rollback
};

/* Find live prop and return its propid */
static int dav_deltav_find_liveprop(const dav_resource * resource,
                                    const char *ns_uri, const char *name,
                                    const dav_hooks_liveprop ** hooks)
{
    TRACE();

    /* don't try to find any liveprops if this isn't "our" resource */
    if (resource->hooks != &dav_repos_hooks_repos)
	return 0;

    if (!strcmp(ns_uri, "DAV:")) {
	int i;
	/* find propid */
	for (i = 0; dav_deltav_props[i].name; i++) {
	    if (!strcmp(name, dav_deltav_props[i].name)) {
                *hooks = dav_deltav_liveprop_group.hooks;
		return dav_deltav_props[i].propid;
            }
	}
    }

    /* Not found */
    return 0;
}

static void dav_deltav_insert_all_liveprops(request_rec * r,
                                            const dav_resource * resource,
                                            dav_prop_insert what,
                                            apr_text_header * phdr)
{
    apr_ssize_t klen;
    const char *s;
    const char *key, *val;
    apr_hash_index_t *hindex;
    dav_repos_resource *db_r;

    /* don't try to find any liveprops if this isn't "our" resource */
    if (resource->hooks != &dav_repos_hooks_repos)
	return;

    TRACE();

    if (what == DAV_PROP_INSERT_VALUE) return;

    db_r = (dav_repos_resource *) resource->info->db_r;

    if (!resource->exists)
	return;

    sabridge_build_vpr_hash(resource->info->db, db_r);
    if (db_r->vpr_hash == NULL) return;

    if (what == DAV_PROP_INSERT_SUPPORTED)
        apr_text_append(r->pool, phdr, "<D:supported-live-property>" 
                        DEBUG_CR "<D:prop>" DEBUG_CR);

    /* Read live props from hash */
    for (hindex = apr_hash_first(r->pool, db_r->vpr_hash);
	 hindex; hindex = apr_hash_next(hindex)) {
	apr_hash_this(hindex, (void *) &key, &klen, (void *) &val);
        s = apr_psprintf(r->pool, "<D:%s/>" DEBUG_CR, key);
	apr_text_append(r->pool, phdr, s);
    }

    if (what == DAV_PROP_INSERT_SUPPORTED)
        apr_text_append(r->pool, phdr, "</D:prop>" DEBUG_CR
                        "</D:supported-live-property>" DEBUG_CR);
}

void dav_deltav_register_liveprops(apr_pool_t *p)
{
    dav_hook_find_liveprop(dav_deltav_find_liveprop, NULL, NULL,
                           APR_HOOK_MIDDLE);

    dav_hook_insert_all_liveprops(dav_deltav_insert_all_liveprops, NULL,
                                  NULL, APR_HOOK_MIDDLE);
    /* register the namespace URIs */
    dav_register_liveprop_group(p, &dav_deltav_liveprop_group);
}
