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
#include "util.h"
#include "bridge.h" /* for getetag_dbr */
#include <apr_strings.h>
#include <ctype.h>

#define DAV_DISPLAYNAME_LIMIT 256

/* forward-declare */
static const dav_hooks_liveprop dav_repos_hooks_liveprop;

/*
** The namespace URIs that we use. This list and the enumeration must
** stay in sync.
*/
static const char *const dav_repos_namespace_uris[] = {
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
static const dav_liveprop_spec dav_repos_props[] = {
    {
     dav_repos_URI_DAV,
     "creationdate",
     DAV_PROPID_creationdate,
     0},
    {
     dav_repos_URI_DAV,
     "displayname",
     DAV_PROPID_displayname,
     1},
    {
     dav_repos_URI_DAV,
     "getcontentlanguage",
     DAV_PROPID_getcontentlanguage,
     1},
    {
     dav_repos_URI_DAV,
     "getcontentlength",
     DAV_PROPID_getcontentlength,
     0},
    {
     dav_repos_URI_DAV,
     "getcontenttype",
     DAV_PROPID_getcontenttype,
     1},
    {
     dav_repos_URI_DAV,
     "getetag",
     DAV_PROPID_getetag,
     0},
    {
     dav_repos_URI_DAV,
     "getlastmodified",
     DAV_PROPID_getlastmodified,
     0},

    {0}				/* sentinel */
};

static const dav_liveprop_group dav_repos_liveprop_group = {
    dav_repos_props,
    dav_repos_namespace_uris,
    &dav_repos_hooks_liveprop
};

/**
 * Insert prop with propid
 * @param resource the resource whose prop needs to be edited
 * @param propid the property identified with propid which needs to be inserted. 
 * @param what the value to be inserted.
 * @param phdr the first piece of text in the list.
*/
static dav_prop_insert dav_repos_insert_prop(const dav_resource * resource,
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
    DBG1("propid: %d", propid);

    /*
     ** None of FS provider properties are defined if the resource does not
     ** exist. Just bail for this case.
     **
     ** Even though we state that the FS properties are not defined, the
     ** client cannot store dead values -- we deny that thru the is_writable
     ** hook function.
     */
    if (dbr->serialno == 0)
	return DAV_PROP_INSERT_NOTDEF;

    /* find propname using prop id */
    spec = get_livepropspec_from_id(dav_repos_props, propid);
    name = spec->name;

    /* ### what the heck was this property? */
    if (name == NULL)
	return DAV_PROP_INSERT_NOTDEF;

    /* ensure lpr_hash is set */
    if(!dbr->lpr_hash) dav_repos_build_lpr_hash(dbr);

    /* Get value */
    value = apr_hash_get(dbr->lpr_hash, name, APR_HASH_KEY_STRING);

    /* ### Not found in the hash */
    if (value == NULL)
	return DAV_PROP_INSERT_NOTDEF;

    /* Do something according to what */
    if (what == DAV_PROP_INSERT_VALUE)
	s = apr_psprintf(pool, "<D:%s>%s</D:%s>", name, value, name);
    else if (what == DAV_PROP_INSERT_NAME)
	s = apr_psprintf(pool, "<D:%s/>" DEBUG_CR, name);
    else  /* assert: what == DAV_PROP_INSERT_SUPPORTED */
	s = apr_psprintf(pool,
			 "<D:supported-live-property><D:prop><D:%s/>"
			 "</D:prop></D:supported-live-property>", name);
    apr_text_append(pool, phdr, s);

    /* we inserted what was asked for */
    return what;
}

void dav_repos_build_lpr_hash(dav_repos_resource * db_r)
{
    const char *s;
    apr_pool_t *pool = db_r->p;

    /* an HTTP-date can be 29 chars plus a null term */
    /* a 64-bit size can be 20 chars plus a null term */

    /* Let's build hash */
    db_r->lpr_hash = apr_hash_make(pool);

    /* set live properties */
    char *creationdate = apr_pcalloc(pool, APR_RFC822_DATE_LEN * sizeof(char));
    if (db_r->created_at != NULL) {
	dav_repos_format_strtime(DAV_STYLE_ISO8601, db_r->created_at, creationdate);
	apr_hash_set(db_r->lpr_hash, "creationdate", APR_HASH_KEY_STRING, creationdate);
    }

    char *getlastmodified = apr_pcalloc(pool, APR_RFC822_DATE_LEN * sizeof(char));
    if (db_r->updated_at != NULL) {
	dav_repos_format_strtime(DAV_STYLE_RFC822, db_r->updated_at, getlastmodified);
	apr_hash_set(db_r->lpr_hash, "getlastmodified", APR_HASH_KEY_STRING,
		 apr_pstrdup(pool, getlastmodified));
    }

    if (db_r->displayname != NULL) {
	apr_hash_set(db_r->lpr_hash, "displayname", APR_HASH_KEY_STRING,
		     db_r->displayname);
    }

    if (db_r->resourcetype == dav_repos_COLLECTION ||
        db_r->resourcetype == dav_repos_VERSIONED_COLLECTION)
        return;

    if (db_r->getcontentlength != DAV_REPOS_NODATA) {
	s = apr_psprintf(pool, "%ld", db_r->getcontentlength);
	apr_hash_set(db_r->lpr_hash, "getcontentlength",
		     APR_HASH_KEY_STRING, s);
    }

    apr_hash_set(db_r->lpr_hash, "getetag", APR_HASH_KEY_STRING,
		 (char *)sabridge_getetag_dbr(db_r));

    if (db_r->getcontenttype) {
	apr_hash_set(db_r->lpr_hash, "getcontenttype", APR_HASH_KEY_STRING,
		     db_r->getcontenttype);
    }

    if (db_r->getcontentlanguage) {
        apr_hash_set(db_r->lpr_hash, "getcontentlanguage", 
                     APR_HASH_KEY_STRING, db_r->getcontentlanguage);
    }

}

/**
 * Check if a property of resource is writable. 
 * @param resource the resource whose propid status needs to be figured out
 * @param propid the propid whose status needs to be figured out
 * @return 1 indicating success
 */
static int dav_repos_is_writable(const dav_resource * resource, int propid)
{
    const dav_liveprop_spec *spec;
    TRACE();

    spec = get_livepropspec_from_id(dav_repos_props, propid);
    if (spec)
        return spec->is_writable;

    return 0;
}

/* @brief Check whether the Language-Tag conforms to RFC 2616 Sec 3.10
 *        The grammar is
 *           language-tag  = primary-tag *( "-" subtag )
 *           primary-tag   = 1*8ALPHA
 *           subtag        = 1*8ALPHA
 * @param pool Pool to allocated resources from
 * @param lang_tag The language tag provided by user
 */
static int validate_language_tag(apr_pool_t *pool, const char *lang_tag)
{
    char *last;
    char *tag;

    tag = apr_strtok(apr_pstrdup(pool, lang_tag), "-", &last);
    if (tag == NULL) return -1;

    do {
        int i = 0;
        while (tag[i])
            if (i > 8 || !isalpha(tag[i++]))
                return -1;
    } while ((tag = apr_strtok(NULL, "-", &last)) != NULL);

    return 0;
}

static dav_error * dav_repos_patch_validate(const dav_resource * resource,
                                            const apr_xml_elem * elem,
                                            int operation,
                                            void **context,
                                            int *defer_to_dead)
{
    apr_pool_t *pool = resource->pool;
    dav_elem_private *priv = elem->priv;
    dav_repos_resource *db_r = resource->info->db_r;
    dav_repos_db *db = resource->info->db;
    char *path;
    const char *data;
    apr_size_t size;


    if (operation == DAV_PROP_OP_DELETE)
        return dav_new_error(pool, HTTP_CONFLICT, 0,
                             "This property cannot be removed");

    *context = (void *)get_livepropspec_from_id(dav_repos_props, priv->propid);
    switch(priv->propid) {
    case DAV_PROPID_displayname:
        if (elem->first_cdata.first->text == NULL ||
            strlen(elem->first_cdata.first->text) > DAV_DISPLAYNAME_LIMIT)
            return dav_new_error(pool, HTTP_CONFLICT, 0,
                                 "Invalid value specified");
        break;
    case DAV_PROPID_getcontentlanguage:
        if (validate_language_tag(pool, elem->first_cdata.first->text))
            return dav_new_error(pool, HTTP_CONFLICT, 0,
                                 "Invalid value specified");
        break;
    case DAV_PROPID_getcontenttype:
        apr_xml_to_text(pool, elem, APR_XML_X2T_INNER, NULL, NULL, &data, &size);
        data = strip_whitespace((char*)data);
        sabridge_get_resource_file(db, db_r, &path);
        if (!is_content_type_good(path, data))
            return dav_new_error(pool, HTTP_CONFLICT, 0,
                                 "Couldn't pass filter");
        break;
    default:
        return dav_new_error(pool, HTTP_FORBIDDEN, 0,
                             "Cannot be modified");
    }

    return NULL;
}

dav_error * dav_repos_patch_exec(const dav_resource *resource,                     
                                 const apr_xml_elem *elem,
                                 int operation,
                                 void *context,
                                 dav_liveprop_rollback **rollback_ctx)
{
    dav_repos_resource *db_r = resource->info->db_r;
    const dav_liveprop_spec *spec = (const dav_liveprop_spec *)context;
    const char *data;
    apr_size_t size;

    apr_xml_to_text(db_r->p, elem, APR_XML_X2T_INNER, NULL, NULL, &data, &size);

    switch (spec->propid) {
    case DAV_PROPID_displayname:
        db_r->displayname = data;
        break;
    case DAV_PROPID_getcontentlanguage:
        db_r->getcontentlanguage = data;
        break;
    case DAV_PROPID_getcontenttype:
        db_r->getcontenttype = strip_whitespace((char*)data);
        break;
    }
    return NULL;
}

static void dav_repos_patch_commit(const dav_resource *resource,
                                   int operation,
                                   void *context,
                                   dav_liveprop_rollback *rollback_ctx)
{
    const dav_liveprop_spec *spec = context;
    
    switch (spec->propid) {
    case DAV_PROPID_displayname:
    case DAV_PROPID_getcontentlanguage:
        dbms_set_property(resource->info->db, resource->info->db_r);
        break;
    case DAV_PROPID_getcontenttype:
        dbms_update_media_props(resource->info->db, resource->info->db_r);
        break;
    }
}

static dav_error *dav_repos_patch_rollback(const dav_resource *resource,
                                           int operation,
                                           void *context,
                                           dav_liveprop_rollback *rollback_ctx)
{
    return NULL;
}

static const dav_hooks_liveprop dav_repos_hooks_liveprop = {
    dav_repos_insert_prop,
    dav_repos_is_writable,
    dav_repos_namespace_uris,
    dav_repos_patch_validate,
    dav_repos_patch_exec,
    dav_repos_patch_commit,
    dav_repos_patch_rollback
};

/* Find love prop and return its propid */
static int dav_repos_find_liveprop(const dav_resource * resource,
                                   const char *ns_uri, const char *name,
                                   const dav_hooks_liveprop ** hooks)
{
    TRACE();

    return dav_do_find_liveprop(ns_uri, name, &dav_repos_liveprop_group, hooks);
}


/* Insert all live props from live prop hash */
static void dav_repos_insert_all_liveprops(request_rec * r,
                                           const dav_resource * resource,
                                           dav_prop_insert what,
                                           apr_text_header * phdr)
{
    apr_ssize_t klen;
    const char *s = "";
    const char *key, *val;
    apr_hash_index_t *hindex;
    dav_repos_resource *db_r;

    /* don't try to find any liveprops if this isn't "our" resource */
    if (resource->hooks != &dav_repos_hooks_repos)
	return;

    TRACE();

    db_r = (dav_repos_resource *) resource->info->db_r;

    if (!resource->exists) {
	/* a lock-null resource */
	/*
	 ** ### technically, we should insert empty properties. dunno offhand
	 ** ### what part of the spec said this, but it was essentially thus:
	 ** ### "the properties should be defined, but may have no value".
	 */
	apr_text_append(r->pool, phdr, "<D:resourcetype/>");
	return;
    }

    if (what == DAV_PROP_INSERT_SUPPORTED)
        apr_text_append(r->pool, phdr, "<D:supported-live-property><D:prop>");

    /* ensure lpr_hash is set */
    if(!db_r->lpr_hash) dav_repos_build_lpr_hash(db_r);

    /* Read live props from hash */
    for (hindex = apr_hash_first(r->pool, db_r->lpr_hash);
	 hindex; hindex = apr_hash_next(hindex)) {
	apr_hash_this(hindex, (void *) &key, &klen, (void *) &val);
        switch (what) {
        case DAV_PROP_INSERT_VALUE:
            s = apr_psprintf(r->pool, "<D:%s>%s</D:%s>"DEBUG_CR, key, val, key);
            break;
        case DAV_PROP_INSERT_NAME:
        case DAV_PROP_INSERT_SUPPORTED:
            s = apr_psprintf(r->pool, "<D:%s/>", key);
            break;
        default:
            break;
        }
	apr_text_append(r->pool, phdr, s);
    }

    if (what == DAV_PROP_INSERT_SUPPORTED)
        apr_text_append(r->pool, phdr, "</D:prop></D:supported-live-property>");
}

void dav_repos_register_liveprops(apr_pool_t *p)
{
    dav_hook_find_liveprop(dav_repos_find_liveprop, NULL, NULL,
			   APR_HOOK_MIDDLE);
    dav_hook_insert_all_liveprops(dav_repos_insert_all_liveprops, NULL,
				  NULL, APR_HOOK_MIDDLE);

    /* register the namespace URIs */
    dav_register_liveprop_group(p, &dav_repos_liveprop_group);
}

/* utility function */
const dav_liveprop_spec *get_livepropspec_from_id(const dav_liveprop_spec lps[],
                                                  int propid)
{
    while (lps->name) {
        if (lps->propid == propid)
            return lps;
        lps ++;
    }
    return NULL;
}

const char *list_all_liveprops(apr_pool_t *pool, const dav_liveprop_spec lps[])
{
    const char *result = "";

    while (lps->name) {
        result = apr_pstrcat(pool, result, "<D:", lps->name, "/>", NULL);
        lps ++;
    }
    return result;
}
