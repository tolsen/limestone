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

/* ====================================================================
 * Copyright (c) 2002, The Regents of the University of California.
 *
 * The Regents of the University of California MAKE NO REPRESENTATIONS OR
 * WARRANTIES ABOUT THE SUITABILITY OF THE SOFTWARE, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, OR
 * NON-INFRINGEMENT. The Regents of the University of California SHALL
 * NOT BE LIABLE FOR ANY DAMAGES SUFFERED BY LICENSEE AS A RESULT OF
 * USING, MODIFYING OR DISTRIBUTING THIS SOFTWARE OR ITS DERIVATIVES.
 * ====================================================================
 */

#include "props.h"

struct dav_namespace_map {
    const apr_array_header_t *namespaces;
};


struct dav_db {
    apr_pool_t *pool;
    dav_repos_db *mdb;
    dav_repos_resource *mdb_r;

    /* when used as a property database: */
    int version;		/* *minor* version of this db */

    dav_buffer ns_table;	/* table of namespace URIs */
    short ns_count;		/* number of entries in table */
    int ns_table_dirty;		/* ns_table was modified */
    apr_hash_t *uri_index;

    dav_buffer wb_key;		/* work buffer for dav_gdbm_key */

    apr_datum_t iter;		/* iteration key */
};

static dav_error *dav_repos_db_open(apr_pool_t * p,
				    const dav_resource * resource, int ro,
				    dav_db ** pdb)
{
    dav_db *db = apr_pcalloc(p, sizeof(*db));

    TRACE();
    /* Set some values for DB */
    db->pool = p;
    db->mdb = resource->info->db;
    db->mdb_r = (dav_repos_resource *) resource->info->db_r;
    *pdb = db;

    db->uri_index = apr_hash_make(p);

    return NULL;
}

static void dav_repos_db_close(dav_db * db)
{
    TRACE();
}

static dav_error *dav_repos_db_define_namespaces(dav_db * db,
						 dav_xmlns_info * xi)
{
    return NULL;
}

void dav_repos_build_pr_hash(dav_repos_db * db, dav_repos_resource * db_r)
{
    const char *key;
    apr_pool_t *pool = db_r->p;
    dav_repos_property *dead_prop;

    /* Let's build hash */
    db_r->pr_hash = apr_hash_make(pool);

    /* fill dead properties if not already done */
    if(!db_r->pr) dbms_fill_dead_property(db, db_r);

    /* Dead properties */
    for (dead_prop = db_r->pr; dead_prop; dead_prop = dead_prop->next) {
	key = dav_repos_build_ns_name_key(dead_prop->namespace_name,
                                          dead_prop->name, pool);
	apr_hash_set(db_r->pr_hash, key, APR_HASH_KEY_STRING, dead_prop);
#ifdef DEBUG
        DBG2("KEY: [%s]%s", key, dead_prop->name);
#endif
    }
}

dav_error *dav_repos_insert_lock_prop(const dav_walk_params * params,
				      dav_repos_resource * db_r)
{
    dav_error *err = NULL;
    dav_resource *resource = NULL;
    dav_resource_private *info = NULL;

    dav_walker_ctx *ctx = params->walk_ctx;

    TRACE();

    /* Initilize */
    db_r->lockdiscovery = NULL;
    db_r->supportedlock = NULL;

    if (params->lockdb != NULL) {
	dav_lock *locks = NULL;

	resource = apr_pcalloc(db_r->p, sizeof(*resource));
	info = apr_pcalloc(db_r->p, sizeof(*info));
	info->db_r = db_r;
	resource->exists = 1;
	resource->uri = db_r->uri;
	resource->info = info;

	if ((err =
	     dav_lock_query(params->lockdb, resource, &locks)) != NULL) {
	    return dav_push_error(db_r->p, err->status, 0,
				  "DAV:lockdiscovery could not be "
				  "determined due to a problem fetching "
				  "the locks for this resource.", err);
	}

	/* fast-path the no-locks case */
	if (locks) {
	    /*
	     ** This may modify the buffer. value may point to
	     ** wb_lock.pbuf or a string constant.
	     */
	    db_r->lockdiscovery =
		dav_lock_get_activelock(ctx->r, locks, NULL);

	    /* Do we need strdup here */
	    /* db_r->lockdiscovery = apr_pstrdup(db_r->p, wb_lock.buf); */
	}

	/* 
	 * Get supported lock info 
	 * Our lock hook will not use resource though.
	 */
	db_r->supportedlock =
	    (*params->lockdb->hooks->get_supportedlock) (resource);
    }

    return NULL;
}

static void dav_repos_get_ns_name_key(const char *key, dav_prop_name * pname,
			       apr_pool_t * pool)
{
    char *str;
    char *strtok_state;
    pname->ns = pname->name = NULL;

    /* Woops */
    if (key == NULL || pool == NULL)
	return;

    str = apr_pstrdup(pool, key);
    pname->ns = apr_strtok(str, "\t", &strtok_state);
    pname->name = apr_strtok(NULL, "\t", &strtok_state);
}

static char *get_dead_prop_elem(apr_pool_t *pool, dav_repos_property *pr)
{
    char *last = NULL;
    char *xmlinfo = apr_pstrdup(pool, pr->xmlinfo);

    /* get xml tag from xmlinfo 
     * e.g. xmlinfo: <test xmlns="http://www.example.com">
     * the tag to extract is test */
    char *tag = apr_strtok(xmlinfo, " >", &last);
    tag++;      /* skip the starting '<' */
    char *end_tag = apr_psprintf(pool, "</%s>", tag);

    /* now concatenate xmlinfo, value, end_tag to form a complete xml elem */
    return apr_pstrcat(pool, pr->xmlinfo, pr->value, end_tag, NULL);
}

static dav_error *dav_repos_db_output_value(dav_db * db,
					    const dav_prop_name * name,
					    dav_xmlns_info * xi,
					    apr_text_header * phdr,
					    int *found)
{
    const char *s = NULL;
    const char *key;
    dav_repos_property *dead_prop;

    /* Get resource records */
    dav_repos_resource *dbr = db->mdb_r;
    apr_pool_t *pool = db->pool;

    TRACE();

    if (dbr == NULL) {
	*found = 0;
	return NULL;
    }

    /* Get the key */
    key = dav_repos_build_ns_name_key(name->ns, name->name, pool);

    /* ensure pr_hash is set */
    if(!dbr->pr_hash) dav_repos_build_pr_hash(db->mdb, dbr);

    /* Get the dead prop */
    dead_prop = apr_hash_get(dbr->pr_hash, key, APR_HASH_KEY_STRING);

    if (dead_prop != NULL) {
        s = get_dead_prop_elem(pool, dead_prop); 
    } else {
        DBG1("NOFPUND [%s]\n", key);
    }

    if (s) {
	*found = 1;
	apr_text_append(pool, phdr, s);
    } else {
	*found = 0;
    }

    return NULL;
}

static dav_error *dav_repos_db_map_namespaces(dav_db * db,
					      const apr_array_header_t *
					      namespaces,
					      dav_namespace_map ** mapping)
{
    dav_namespace_map *m = apr_pcalloc(db->pool, sizeof(*m));

    TRACE();

    m->namespaces = namespaces;
    *mapping = m;

    return NULL;
}

static dav_error *dav_repos_db_store(dav_db * db,
				     const dav_prop_name * name,
				     const apr_xml_elem * elem,
				     dav_namespace_map * mapping)
{
    size_t l_val;
    char *val, *xmlinfo, *start_tag, *fullxml;
    dav_repos_property *pr = apr_pcalloc(db->pool, sizeof(*pr));
    dav_repos_resource *dbr = db->mdb_r;
    dav_error *err = NULL;

    TRACE();

    if (dbr->type == DAV_RESOURCE_TYPE_VERSION ||
	dbr->type == DAV_RESOURCE_TYPE_HISTORY)
	return dav_new_error(db->pool, HTTP_CONFLICT, 0,
			     "You can not set props for version resource.");


    /* Litmus test <aa xmlns=""> case */
    /*
       if(name->ns==NULL || strlen(name->ns)==0) {
       return dav_new_error(db->pool, HTTP_BAD_REQUEST, 0,
       "Invalid namespace declaration.");
       }
     */
    /* quote all the values in the element */
    /* ### be nice to do this without affecting the element itself */
    /* ### of course, the cast indicates Badness is occurring here */
    apr_xml_quote_elem(db->pool, (apr_xml_elem *) elem);

    /* generate a text blob for the contents of elem */
    apr_xml_to_text(db->pool, elem, APR_XML_X2T_INNER, 
                    mapping->namespaces, NULL, (const char **) &val, &l_val);

    /* generate a text blob for 
     * start/end tags, contents, ns defs and xml:lang of elem */
    apr_xml_to_text(db->pool, elem, APR_XML_X2T_FULL_NS_LANG, 
                    mapping->namespaces, NULL, 
                    (const char **) &fullxml, &l_val);

    /* stop at the end of start tag */
    start_tag = strstr(fullxml, ">");

    /* get start_tag as xmlinfo */
    int xmlinfosize = strlen(fullxml)-strlen(start_tag)+1;
    xmlinfo = apr_pstrndup(db->pool, fullxml, xmlinfosize);

    /* Warning : val[0] value is NULL, so you should use 'val+1' */

    /* Let's fill fill pr */
    if((err = dbms_get_ns_id(db->mdb, db->mdb_r, name->ns, &pr->ns_id)))
        return err;
    pr->name = name->name;
    pr->value = val;
    pr->xmlinfo = xmlinfo;

    if((err = dbms_set_dead_property(db->mdb, db->mdb_r, pr)))
        return err;

    return NULL;
}


static dav_error *dav_repos_db_remove(dav_db * db,
				      const dav_prop_name * name)
{
    dav_repos_property *pr = apr_pcalloc(db->pool, sizeof(*pr));
    dav_error *err = NULL;

    TRACE();
    if((err = dbms_get_ns_id(db->mdb, db->mdb_r, name->ns, &pr->ns_id)))
        return err;
    pr->name = name->name;

    return dbms_del_dead_property(db->mdb, db->mdb_r, pr);
}


static int dav_repos_db_exists(dav_db * db, const dav_prop_name * name)
{
    /* Get resource records */
    dav_repos_resource *dbr = db->mdb_r;

    /* Build the key */
    const char *key;

    TRACE();

    if (dbr == NULL)
	return 0;

    key = dav_repos_build_ns_name_key(name->ns, name->name, dbr->p);

    /* ensure pr_hash is set */
    if(!dbr->pr_hash) dav_repos_build_pr_hash(db->mdb, dbr);

    /* Get the the prop */
    if (apr_hash_get(dbr->pr_hash, key, APR_HASH_KEY_STRING))
	return 1;

    return 0;
}

static dav_error *dav_repos_db_first_name(dav_db * db,
					  dav_prop_name * pname)
{
    const char *key = NULL;
    apr_ssize_t klen;
    dav_repos_property *pr;

    /* Get resource records */
    dav_repos_resource *dbr = db->mdb_r;

    TRACE();

    if (dbr == NULL) {
	pname->ns = NULL;
	pname->name = NULL;
	return NULL;
    }

    /* ensure pr_hash is set */
    if(!dbr->pr_hash) dav_repos_build_pr_hash(db->mdb, dbr);

    dbr->pr_hash_index = apr_hash_first(db->pool, dbr->pr_hash);
    if (dbr->pr_hash_index == NULL) {
	pname->ns = NULL;
	pname->name = NULL;
	return NULL;
    }

    apr_hash_this(dbr->pr_hash_index, (void *) &key, &klen, (void *) &pr);

    /* Get ns and name from the key */
    dav_repos_get_ns_name_key(key, pname, db->pool);

    return NULL;
}

static dav_error *dav_repos_db_next_name(dav_db * db,
					 dav_prop_name * pname)
{
    const char *key = NULL;
    apr_ssize_t klen;
    dav_repos_property *pr;

    /* Get resource records */
    dav_repos_resource *dbr = db->mdb_r;

    TRACE();
    pname->ns = NULL;
    pname->name = NULL;

    if (dbr == NULL) {
	return NULL;
    }

    /* Run first_name first */
    if (dbr->pr_hash_index == NULL)
	return NULL;

    dbr->pr_hash_index = apr_hash_next(dbr->pr_hash_index);

    /* End of hash */
    if (dbr->pr_hash_index == NULL)
	return NULL;

    apr_hash_this(dbr->pr_hash_index, (void *) &key, &klen, (void *) &pr);

    /* Get ns and name from the key */
    dav_repos_get_ns_name_key(key, pname, db->pool);

    return NULL;
}

static dav_error *dav_repos_db_get_rollback(dav_db * db,
					    const dav_prop_name * name,
					    dav_deadprop_rollback **
					    prollback)
{
    TRACE();
    return NULL;
}

static dav_error *dav_repos_db_apply_rollback(dav_db * db,
					      dav_deadprop_rollback *
					      rollback)
{
    TRACE();
    return NULL;
}

const dav_hooks_propdb dav_repos_hooks_propdb = {
    dav_repos_db_open,
    dav_repos_db_close,
    dav_repos_db_define_namespaces,
    dav_repos_db_output_value,
    dav_repos_db_map_namespaces,
    dav_repos_db_store,
    dav_repos_db_remove,
    dav_repos_db_exists,
    dav_repos_db_first_name,
    dav_repos_db_next_name,
    dav_repos_db_get_rollback,
    dav_repos_db_apply_rollback,
};
