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

#include "search.h"
#include "dav_repos.h"  /* for dav_repos_get_db */
#include "acl.h"        /* for dav_repos_get_prin_by_name */

/* following are write-once read forever hashes */
/* use server pool for allocating them */
/* TODO: move this to child_init */
static apr_hash_t *liveprop_map = NULL;
static apr_hash_t *registered_ops = NULL;
static apr_hash_t *comp_ops_map = NULL;

static long get_ns_id(apr_pool_t *pool, search_ctx *sctx, const char *ns) {
    long *ns_id = apr_pcalloc(pool, sizeof(long));
    dbms_get_ns_id(sctx->db, sctx->db_r, ns, ns_id);
    apr_hash_set(sctx->namespace_map, ns_id, sizeof(long), ns);

    return *ns_id;
}

static dav_error *dav_repos_set_option_head(request_rec * r)
{
    /*DASL: <DAV:basicsearch>
     * DASL: <http://foo.bar.com/syntax1> 
     * DASL: <http://akuma.com/syntax2>
     */
    /*
       apr_table_addn(r->headers_out, "DASL",  "<http://foo.bar.com/syntax1>");
       apr_table_addn(r->headers_out, "DASL",  "<http://akuma.com/syntax2>");
     */
    apr_table_addn(r->headers_out, "DASL", "<DAV:basicsearch>");
    apr_table_addn(r->headers_out, "DASL", "<limebits:basicsearch xmlns:limebits=\"http://limebits.com/ns/1.0\">");
    return NULL;
}

/**
 * Handles the DASL SEARCH method
 * @param r The method request record
 * @return The HTTP response code
 */
static dav_error *dav_repos_search_resource(request_rec * r,
                                            dav_resource *resource,
					    dav_response ** res)
{
    int result;
    apr_xml_doc *doc = NULL;
    dav_repos_db *db_handle = NULL;
    search_ctx *sctx = apr_pcalloc(r->pool, sizeof(*sctx));
    dav_repos_resource *db_r = resource->info->db_r;
    dav_repos_query *q = NULL;
    
    TRACE();

    if ((result = ap_xml_parse_input(r, &doc)) != 0) {
	return dav_new_error(r->pool, HTTP_BAD_REQUEST, 0,
			     "ap_xml_parse_input error");
    }
    
    sctx->doc = doc;
    sctx->db_r = db_r;
    sctx->bind_uri_map = apr_hash_make(r->pool);
    sctx->prop_map = apr_hash_make(r->pool);
    sctx->bitmarks_map = apr_hash_make(r->pool);
    sctx->namespace_map = apr_hash_make(r->pool);
   
    /* Get db_handle from request_rec */
    db_handle = dav_repos_get_db(r);
    sctx->db = db_handle;

    /* We need XML doc */
    if (doc == NULL || doc->root == NULL) {
	return dav_new_error(r->pool, HTTP_BAD_REQUEST, 0, "doc is NULL");
    }

    /* Parse the XML search request */
    if ((result = parse_query(r, sctx)) != HTTP_OK) 
	return dav_new_error(r->pool, result, 0, sctx->err_msg);

    /* Build query */
    if ((result = build_query(r, sctx)) != HTTP_OK) 
	return dav_new_error(r->pool, result, 0, sctx->err_msg);

    /* Execute the query */
    q = dbms_prepare(r->pool, db_handle->db, sctx->query);
    if (dbms_execute(q)) {
	dbms_query_destroy(q);
	return dav_new_error(r->pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "Error in executing query");
    }

    /* Save query handle in context */
    sctx->q = q;

    /* Build the XML response */
    if ((result = build_xml_response(r->pool, sctx, res) != HTTP_OK)) {
	return dav_new_error(r->pool, HTTP_BAD_REQUEST, 0,
			     "An error occurred while building XML response!");
    }

    dbms_query_destroy(q);

    return NULL;
}

/**
 * Parses the XML body of search request and fills sctx
 * @param r The method request record
 * @param sctx Search Context
 * @return The HTTP response code
 */
int parse_query(request_rec *r, search_ctx *sctx)
{
    apr_xml_doc *doc = sctx->doc;
    apr_xml_elem *select_elem = NULL;
    apr_xml_elem *where_elem = NULL;
    apr_xml_elem *from_elem = NULL;
    apr_xml_elem *orderby_elem = NULL;
    apr_xml_elem *limit_elem = NULL;
    apr_xml_elem *offset_elem = NULL;
    apr_xml_elem *basicsearch_elem = NULL;
    int result;

    TRACE();

    /* TODO: Modify this method to be modular
     * Try using register/callback type system */
    /* basicsearch */
    basicsearch_elem = dav_find_child(doc->root, "basicsearch");
    if (basicsearch_elem == NULL) {
	sctx->err_msg =
	    apr_pstrdup(r->pool,
			"Requested search grammar not supported."
			" We support only <basicsearch>");
	return HTTP_BAD_REQUEST;
    }

    /* Find the select XML element */
    select_elem = dav_find_child(basicsearch_elem, "select"); 

    /* process the select portion */
    if (select_elem == NULL) {
        sctx->err_msg =
            apr_pstrdup(r->pool, "We don't have <select> element");
        return HTTP_BAD_REQUEST;
    }

    /* Parse select element */
    if ((result = parse_select(r, sctx, select_elem)) != HTTP_OK) {
        return result;
    }

    /* Find the where XML element */
    where_elem = dav_find_child(basicsearch_elem, "where");

    /* Parse where element */
    if (where_elem) {
        /* Register WHERE ops, allocating from process pool and lazily
         * evaluating it so as to avoid re-computing it for each request */
        register_ops(r->server->process->pool);

        if((result = parse_where(r, sctx, where_elem->first_child)) != HTTP_OK)
            return result;
        
    }

    /* Find the from XML element */
    from_elem = dav_find_child(basicsearch_elem, "from");
    
    /* process the from portion */
    if (from_elem == NULL) {
        sctx->err_msg =
            apr_pstrdup(r->pool, "We don't have <from> element");
        return HTTP_BAD_REQUEST;
    }

    /* Parse from element */
    if ((result = parse_from(r, sctx, from_elem)) != HTTP_OK) {
        return result;
    }

    /* Find the orderby XML element */
    orderby_elem = dav_find_child(basicsearch_elem, "orderby");

    /* Parse orderby element */
    if (orderby_elem) {
        if((result = parse_orderby(r, sctx, orderby_elem)) != HTTP_OK)
            return result;
    } else {
        sctx->orderby = " ";
    }

    /* Find the limit XML element */
    limit_elem = dav_find_child(basicsearch_elem, "limit");

    /* Parse limit element */
    if (limit_elem) {
        if((result = parse_limit(r, sctx, limit_elem)) != HTTP_OK)
            return result;
    }

    /* Find the offset XML element */
    apr_xml_elem *iter = basicsearch_elem->first_child;
    offset_elem = NULL;
    for (; iter; iter = iter->next) {
        if (!strcmp(iter->name, "offset")) {
                offset_elem = iter;
                break;
        }
    }

    /* Parse offset element */
    if (offset_elem) {
        if((result = parse_offset(r, sctx, offset_elem)) != HTTP_OK)
            return result;
    }

    return HTTP_OK;
}

/**
 * Builds the SQL query from the XML body of the request
 * @param r The method request record
 * @param sctx Search Context
 * @return The HTTP response code
 */
int build_query(request_rec *r, search_ctx *sctx)
{
    int result;

    TRACE();

    /* build individual parts of the query */

    result = build_query_select(r, sctx);
    result = build_query_where(r, sctx);
    result = build_query_from(r, sctx);
    result = build_query_orderby(r, sctx);
    result = build_query_limit(r, sctx);
    result = build_query_offset(r, sctx);

    /* Combine all the parts to form the final query */
    sctx->query = apr_pstrcat(r->pool, sctx->select, sctx->from, sctx->where,
			      sctx->orderby, sctx->limit, sctx->off, NULL);

    return HTTP_OK;
}

static char *get_prop_key(apr_pool_t *pool, const char *propname,
                          long ns_id)
{
    return apr_psprintf(pool, "%s_%ld", propname, ns_id);
}

static const char *get_ns_uri(apr_array_header_t *ns_xlate, int ns)
{
    const char *ns_uri;

    if(ns == APR_XML_NS_DAV_ID)
        ns_uri = "DAV:";
    else
        ns_uri = APR_XML_GET_URI_ITEM(ns_xlate, ns);

    return ns_uri;
}

/**
 * Processes the SELECT portion of the XML query
 * @param r The method request record
 * @param query The SQL query being constructed
 * @param cur_elem The current XML element of WHERE clause being processed
 * @param assoc The association list of dead props and their tables
 * @return The HTTP response code
 */
int parse_select(request_rec * r, search_ctx * sctx, apr_xml_elem *select_elem)
{
    apr_xml_elem *prop_elem, *bitmark_elem, *allprop_elem, *propi = NULL;
    apr_pool_t *ppool = r->server->process->pool;
    apr_pool_t *pool = r->pool;
    const char *ns, *attr;
    char *prop_key;
    long ns_id;

    TRACE();

    prop_elem = dav_find_child(select_elem, "prop");
    bitmark_elem = dav_find_child_no_ns(select_elem, "bitmark");
    allprop_elem = dav_find_child(select_elem, "allprop");

    if (prop_elem) {
        /* No prop information */
        if (!prop_elem->first_child || prop_elem->first_child->first_child)
        {
            sctx->err_msg = apr_pstrdup(r->pool, "No prop information");
            return HTTP_BAD_REQUEST;
        }

        /* Build props map */
        for(propi = prop_elem->first_child; propi; 
            propi = propi->next) {
            ns = get_ns_uri(sctx->doc->namespaces, propi->ns);
            ns_id = get_ns_id(pool, sctx, ns);
            prop_key = get_prop_key(pool, propi->name, ns_id);
            attr = prop_attr_lookup(ppool, pool, propi, prop_key);
            apr_hash_set(sctx->prop_map, prop_key, 
                         APR_HASH_KEY_STRING, attr);
        }
    }

    if (bitmark_elem) {
        if (bitmark_elem->first_child) {
            for (propi = bitmark_elem->first_child; propi;
                 propi = propi->next) {
                apr_hash_set(sctx->bitmarks_map, propi->name, 
                             APR_HASH_KEY_STRING, propi->name);
            }

            sctx->bitmark_support_req = 1;
        }
    }

    if (allprop_elem) {
        apr_hash_index_t *hi;
        apr_hash_t *liveprop_map = get_liveprop_map(ppool);
        const void *propname;
        void *attr;

        for(hi = apr_hash_first(pool, liveprop_map); hi;
            hi = apr_hash_next(hi)) {
            apr_hash_this(hi, &propname, NULL, &attr);
            char *prop_key = get_prop_key(pool, (const char *)propname, 
                                          get_ns_id(pool, sctx, "DAV:"));
            apr_hash_set(sctx->prop_map, prop_key,
                         APR_HASH_KEY_STRING, (char *)attr);
        }
    }

    if (!prop_elem && !allprop_elem && !sctx->bitmark_support_req) {
        /* Unknown element name */
        sctx->err_msg =
            apr_psprintf(r->pool,
                    "Unknown element name(%s) in select."
                    "Use <allprop> or <prop>", select_elem->name);
        return HTTP_BAD_REQUEST;
    }

    return HTTP_OK;
}

const char *prop_attr_lookup(apr_pool_t *ppool, apr_pool_t *pool,
                             apr_xml_elem *prop, char *prop_key)
{
    char *attr = NULL;
    if(prop->ns == APR_XML_NS_DAV_ID) {
        apr_hash_t *liveprop_map = get_liveprop_map(ppool);
        attr = apr_hash_get(liveprop_map, prop->name, 
                            APR_HASH_KEY_STRING);
    }
    else {
        /* dead property attribute = prop_key */
        attr = apr_psprintf(pool, "\"%s\"", prop_key);
    }

    return attr;
}

apr_hash_t *get_liveprop_map(apr_pool_t *pool)
{
    if(!liveprop_map) {
        liveprop_map = apr_hash_make(pool);
        apr_hash_set(liveprop_map, "creationdate", 
                     APR_HASH_KEY_STRING, "created_at");
        apr_hash_set(liveprop_map, "displayname", 
                     APR_HASH_KEY_STRING, "displayname");
        apr_hash_set(liveprop_map, "getcontentlanguage", 
                     APR_HASH_KEY_STRING, "contentlanguage");
        apr_hash_set(liveprop_map, "getcontenttype", 
                     APR_HASH_KEY_STRING, "mimetype");
        apr_hash_set(liveprop_map, "getcontentlength", 
                     APR_HASH_KEY_STRING, "size");
        apr_hash_set(liveprop_map, "getlastmodified", 
                     APR_HASH_KEY_STRING, "media.updated_at");
        apr_hash_set(liveprop_map, "getetag", 
                     APR_HASH_KEY_STRING, "sha1");
        apr_hash_set(liveprop_map, "lockdiscovery", 
                     APR_HASH_KEY_STRING, "locks.uuid");
        apr_hash_set(liveprop_map, "resourcetype", 
                     APR_HASH_KEY_STRING, "type");
        apr_hash_set(liveprop_map, "resource-id", 
                     APR_HASH_KEY_STRING, "resources.uuid");
        apr_hash_set(liveprop_map, "owner",
                     APR_HASH_KEY_STRING, "principals.name");
        apr_hash_set(liveprop_map, "lastmodified",
                     APR_HASH_KEY_STRING, "resources.lastmodified");
        apr_hash_set(liveprop_map, "popularity",
                     APR_HASH_KEY_STRING, "resources.views");
    }

    return liveprop_map;
}
/**
 * Parse the FROM portion of the XML query
 * @param r The method request record
 * @param sctx The search context
 * @param from_elem The current XML element of FROM clause being processed
 * @return The HTTP response code 
 */
int parse_from(request_rec * r, search_ctx * sctx, apr_xml_elem * from_elem)
{

    apr_xml_elem *scope_elem = NULL;
    int result;

    TRACE();

    /* Get scope element */
    scope_elem = dav_find_child(from_elem, "scope");

    /* Process the scope element */
    if (scope_elem == NULL) {
        sctx->err_msg =
            apr_pstrdup(r->pool, "The FROM portion of query does not have a "
                                 "<scope> element");
        return HTTP_BAD_REQUEST;
    }

    for(; scope_elem; scope_elem = scope_elem->next) {
        
        /* Parse the scope element(s) */
        if ((result = parse_scope(r, sctx, scope_elem)) != HTTP_OK) {
            return result;
        }
    }

    return HTTP_OK;
}

/**
 * Parse the <scope> element of the XML query
 * @param r The method request record
 * @param sctx The search context
 * @param scope_elem Current <scope> XML element of FROM clause being processed
 * @return The HTTP response code 
 */
int parse_scope(request_rec * r, search_ctx * sctx, apr_xml_elem * scope_elem)
{

    char *uri = NULL;
    const char *href = NULL;
    dav_repos_resource *db_ri = NULL;
    dav_repos_resource *iter = NULL;
    apr_xml_elem *cur_elem = scope_elem->first_child;
    int depth, nitems;

    TRACE();

    /* Read Href CDATA */
    href = dav_xml_get_cdata(cur_elem, r->pool, 1 /*strip white*/);
    if (href == NULL || strlen(href) == 0) {
        sctx->err_msg = apr_pstrdup(r->pool, "Missing href in <scope>");
        return HTTP_BAD_REQUEST;
    }

    /* If it is full URL, we need only path */
    if (ap_is_url(href)) {
        apr_uri_t uptr;

        if (apr_uri_parse(r->pool, href, &uptr) != APR_SUCCESS ||
                uptr.path == NULL) {
            sctx->err_msg = apr_pstrdup(r->pool, "Malformed HREF");
            return HTTP_BAD_REQUEST;
        }
        uri = apr_pstrdup(r->pool, uptr.path);
    }
    /* FIXME: space? */
    else if (*href == '/') {    /* absolute href */
        uri = apr_pstrdup(r->pool, href);
    } else {                    /* relative href */
        apr_uri_t uptr;
        href = apr_pstrcat(r->pool, r->uri, "/", href, NULL);
        if (apr_uri_parse(r->pool, href, &uptr) != APR_SUCCESS ||
                uptr.path == NULL) {
            sctx->err_msg = apr_pstrdup(r->pool, "Malformed HREF");
            return HTTP_BAD_REQUEST;
        }
        ap_getparents(uptr.path);
        uri = apr_pstrdup(r->pool, uptr.path);
    }

    /* Delete last slash */
    if (strlen(uri) > 1) {
        dav_repos_chomp_slash(uri);
    }

    if (cur_elem->next) {
        cur_elem = cur_elem->next;
    } else {
        sctx->err_msg = apr_pstrdup(r->pool, "Missing depth in <scope>");
        return HTTP_BAD_REQUEST;
    }
    
    /* get depth */
    depth = parse_depth(cur_elem, r->pool);

    /* Get and append bind id(s) */
    dav_resource *ri;

    /* first, check for is-bit query */
    if (sctx->is_bit_query) {
        /* validate uri */
        int uri_depth = 0, i;
        for (i = 0; i < strlen(uri); i++) {
            if (uri[i] == '/') {
                uri_depth++;
            }
        }

        if (uri_depth > 1 && strncmp(uri, "/home", 5) == 0) {
            int d = 3;
            i = 0;
            while(d && uri[i]) {
                if (uri[i++] == '/') {
                    d--;
                }
            }

            uri = apr_pstrndup(r->pool, uri, i);
            dav_get_resource_from_uri(uri, r, 0, NULL, &ri);
            db_ri = ri->info->db_r;
            sctx->b2_rid = db_ri->serialno;
        }

        return HTTP_OK;
    }
    
    dav_get_resource_from_uri(uri, r, 0, NULL, &ri);
    db_ri = ri->info->db_r;

    sabridge_get_collection_children(sctx->db, db_ri, depth, "read", &iter, 
                                     NULL, &nitems);
    db_ri->next = iter;
    iter = db_ri;
    while(iter) {
        /* Try to decouple binds from search */
        apr_hash_set(sctx->bind_uri_map, &iter->bind_id, 
                     sizeof(int), iter->uri);
        iter = iter->next;
    }

    /* TODO: include-versions */

    return HTTP_OK;
}

int parse_depth(apr_xml_elem *cur_elem, apr_pool_t *pool)
{
    const char *cdata;

    TRACE();

    cdata = dav_xml_get_cdata(cur_elem, pool, 1/*strip white*/);
    if(apr_strnatcmp(cdata, "infinity") == 0)
        return DAV_INFINITY;
    else 
        return atoi(cdata);
}

void register_ops(apr_pool_t *pool)
{
    if(!registered_ops) {
        registered_ops = apr_hash_make(pool); 
        /* Is some kind of partial evaluation possible ? 
         * missing Scheme :( */
        apr_hash_set(registered_ops, "eq", APR_HASH_KEY_STRING, 
                     parse_comp_ops);
        apr_hash_set(registered_ops, "lt", APR_HASH_KEY_STRING, 
                     parse_comp_ops);
        apr_hash_set(registered_ops, "gt", APR_HASH_KEY_STRING, 
                     parse_comp_ops);
        apr_hash_set(registered_ops, "lte", APR_HASH_KEY_STRING, 
                     parse_comp_ops);
        apr_hash_set(registered_ops, "gte", APR_HASH_KEY_STRING, 
                     parse_comp_ops);
        apr_hash_set(registered_ops, "like", APR_HASH_KEY_STRING, 
                     parse_comp_ops);
        apr_hash_set(registered_ops, "and", APR_HASH_KEY_STRING,
                     parse_log_ops);
        apr_hash_set(registered_ops, "or", APR_HASH_KEY_STRING,
                     parse_log_ops);
        apr_hash_set(registered_ops, "not", APR_HASH_KEY_STRING,
                     parse_log_ops);
        apr_hash_set(registered_ops, "is-collection", APR_HASH_KEY_STRING,
                     parse_is_coll);
        apr_hash_set(registered_ops, "is-bit", APR_HASH_KEY_STRING,
                     parse_is_bit);
        apr_hash_set(registered_ops, "is-defined", APR_HASH_KEY_STRING,
                     parse_is_defined);
    }
}

/**
 * Uses a recursive, depth first, in-order approach to construct the
 * WHERE clause of the SQL query. The resulting query is completely
 * parenthesized in order to maintain operator precedence.
 * @param r The method request record
 * @param sctx The search context
 * @param cur_elem The current XML element of WHERE clause being processed
 * @return The HTTP response code 
 */
int parse_where(request_rec * r, search_ctx * sctx,
		apr_xml_elem * cur_elem)
{
    /* Caller needs to handle cur_elem = NULL case */
    char *op = apr_pstrdup(r->pool, cur_elem->name);
    int (* parse_op )(request_rec *r, apr_xml_elem *elem, search_ctx *sctx);

    TRACE();

    parse_op = apr_hash_get(registered_ops, op, APR_HASH_KEY_STRING);

    if(parse_op)
        return parse_op(r, cur_elem, sctx);
    
    return HTTP_BAD_REQUEST;
}

apr_hash_t *get_comp_ops_map(apr_pool_t *pool)
{
    if(!comp_ops_map) {
        comp_ops_map = apr_hash_make(pool); 
        apr_hash_set(comp_ops_map, "eq", APR_HASH_KEY_STRING, 
                     "=");
        apr_hash_set(comp_ops_map, "lt", APR_HASH_KEY_STRING, 
                     "<");
        apr_hash_set(comp_ops_map, "gt", APR_HASH_KEY_STRING, 
                     ">");
        apr_hash_set(comp_ops_map, "lte", APR_HASH_KEY_STRING, 
                     "<=");
        apr_hash_set(comp_ops_map, "gte", APR_HASH_KEY_STRING, 
                     ">=");
        apr_hash_set(comp_ops_map, "like", APR_HASH_KEY_STRING, 
                     "LIKE");
    }
    
    return comp_ops_map;
}

int parse_comp_ops(request_rec *r, apr_xml_elem *cur_elem, search_ctx *sctx)
{
    apr_pool_t *pool = r->pool;
    apr_pool_t *ppool = r->server->process->pool;
    char *op;
    apr_xml_elem *prop = cur_elem->first_child->first_child;
    apr_xml_elem *literal = cur_elem->first_child->next;
    const char *type = NULL;

    TRACE();

    const char *ns = get_ns_uri(sctx->doc->namespaces, prop->ns);
    long ns_id = get_ns_id(pool, sctx, ns);
    char *prop_key = get_prop_key(pool, prop->name, ns_id);
    const char *attr = prop_attr_lookup(ppool, pool, prop, prop_key); 

    op = apr_hash_get(get_comp_ops_map(ppool), cur_elem->name, 
                      APR_HASH_KEY_STRING);

    if(!op || !attr)
        return HTTP_BAD_REQUEST;

    if(!sctx->where_cond)
        /* apr_pstrcat cannot handle NULL strings */
        sctx->where_cond = apr_psprintf(pool, " ");

    /* handle typed-literals, 
     * Only works if there is a corresponding type in Postgres */
    if (!strcmp(literal->name, "typed-literal")) {
        type = dav_find_attr(literal, "type");
        /* strip namespace prefixes */
        type = strchr(type, ':');
        type++;
    }

    if (type) {
        sctx->where_cond = 
            apr_pstrcat(pool, sctx->where_cond, " ( ", attr, "::", type, " ",
                        op, " ", literal->first_cdata.first->text, " )", NULL);
    }
    else {
        sctx->where_cond = 
            apr_pstrcat(pool, sctx->where_cond, "( ", attr, " ", op, " '", 
                        literal->first_cdata.first->text, "' )", NULL);
    }

    return HTTP_OK;
}

int parse_log_ops(request_rec *r, apr_xml_elem *cur_elem, search_ctx *sctx)
{
    apr_pool_t *pool = r->pool;
    const char *op = cur_elem->name;
    int result;
    apr_xml_elem *all_ops = cur_elem->first_child;
    apr_xml_elem *iter = NULL;

    TRACE();

    if(!op || !all_ops)
        return HTTP_BAD_REQUEST;

    if(!sctx->where_cond)
        /* apr_pstrcat cannot handle NULL strings */
        sctx->where_cond = apr_psprintf(pool, " ");

    if(apr_strnatcmp(op, "not") == 0) {
        sctx->where_cond = apr_pstrcat(pool, sctx->where_cond, "( NOT ", NULL);
        result = parse_where(r, sctx, all_ops);
        sctx->where_cond = apr_pstrcat(pool, sctx->where_cond, " )", NULL);
    }
    else {
        sctx->where_cond = apr_pstrcat(pool, sctx->where_cond, "( ", NULL);
        if((result = parse_where(r, sctx, all_ops)) != HTTP_OK)
            return result;

        for(iter = all_ops->next; iter; iter = iter->next) {
            /* validation required for op */
            sctx->where_cond = apr_pstrcat(pool, sctx->where_cond, " ", 
                                           op, " ", NULL);
            if((result = parse_where(r, sctx, iter)) != HTTP_OK)
                return result;
        }
        
        sctx->where_cond = apr_pstrcat(pool, sctx->where_cond, " )", NULL);
    }
            
    return result;
}

int parse_is_coll(request_rec *r, apr_xml_elem *cur_elem, search_ctx *sctx)
{
    apr_pool_t *pool = r->pool;

    TRACE();
    if(!sctx->where_cond)
        /* apr_pstrcat cannot handle NULL strings */
        sctx->where_cond = apr_psprintf(pool, " ");

    sctx->where_cond = 
        apr_pstrcat(pool, sctx->where_cond, 
                    "( type = 'Collection' )", NULL);

    return HTTP_OK;
}

int parse_is_bit(request_rec *r, apr_xml_elem *cur_elem, search_ctx *sctx)
{
    apr_pool_t *pool = r->pool;

    TRACE();
    sctx->is_bit_query = 1;

    if(!sctx->where_cond)
        /* apr_pstrcat cannot handle NULL strings */
        sctx->where_cond = apr_psprintf(pool, " ");

    sctx->where_cond = 
        apr_pstrcat(pool, sctx->where_cond, 
                    "( type = 'Collection' )", NULL);

    return HTTP_OK;
}

int parse_is_defined(request_rec *r, apr_xml_elem *cur_elem, search_ctx *sctx)
{
    apr_pool_t *pool = r->pool;
    apr_pool_t *ppool = r->server->process->pool;
    apr_xml_elem *prop = cur_elem->first_child->first_child;

    TRACE();

    const char *ns = get_ns_uri(sctx->doc->namespaces, prop->ns);
    long ns_id = get_ns_id(pool, sctx, ns);
    char *prop_key = get_prop_key(pool, prop->name, ns_id);
    const char *attr = prop_attr_lookup(ppool, pool, prop, prop_key); 

    apr_hash_set(sctx->prop_map, prop_key, APR_HASH_KEY_STRING, attr);

    if(!sctx->where_cond)
        /* apr_pstrcat cannot handle NULL strings */
        sctx->where_cond = apr_psprintf(pool, " ");

    sctx->where_cond = 
        apr_pstrcat(pool, sctx->where_cond, 
                    "( ", attr, " IS NOT NULL )", NULL);

    return HTTP_OK;
}

/**
 * Processes the ORDER BY portion of the XML query
 * @param r The method request record
 * @param sctx The search context
 * @param orderby_elem The orderby XML element
 * @return The HTTP response code
 */
int parse_orderby(request_rec * r, search_ctx * sctx,
		  apr_xml_elem * orderby_elem)
{
    apr_xml_elem *order_elem = NULL;
    int result;

    TRACE();
    
    /* Get order elem */
    order_elem = dav_find_child(orderby_elem, "order");

    /* Process the order element */
    if (order_elem == NULL) {
        sctx->err_msg =
            apr_pstrdup(r->pool, "The ORDERBY portion of query does not have "
                                 "a <order> element");
        return HTTP_BAD_REQUEST;
    }

    /* Initialize orderby part of the query */
    sctx->orderby = apr_psprintf(r->pool, " ORDER BY ");

    for(; order_elem; order_elem = order_elem->next) {
        /* Parse the order element(s) */
        if ((result = parse_order(r, sctx, order_elem)) != HTTP_OK) {
            return result;
        }

        if(order_elem->next)
            sctx->orderby = apr_pstrcat(r->pool, sctx->orderby, ", ", NULL);
    }
    
    return HTTP_OK;
}

int parse_order(request_rec *r, search_ctx *sctx, apr_xml_elem *order_elem)
{
    const char *attr = NULL;
    apr_xml_elem *prop_elem = order_elem->first_child;
    apr_pool_t *ppool = r->server->process->pool;

    TRACE();

    if (!order_elem->first_child) 
        return HTTP_BAD_REQUEST;

    /* TODO: Handle score */
    if(0 == apr_strnatcmp(prop_elem->name, "prop")) {
        apr_xml_elem *prop = prop_elem->first_child;
        if(prop) {
            const char *ns = get_ns_uri(sctx->doc->namespaces, prop->ns);
            long ns_id = get_ns_id(r->pool, sctx, ns);
            char *prop_key = 
                get_prop_key(r->pool, prop->name, ns_id);
            attr = prop_attr_lookup(ppool, r->pool, prop, prop_key); 
            sctx->orderby = apr_pstrcat(r->pool, sctx->orderby, attr, NULL);
        }
        else
            return HTTP_BAD_REQUEST;

        if(prop_elem->next &&
                (apr_strnatcmp(prop_elem->next->name,
                               "descending") == 0)) {
            sctx->orderby = apr_pstrcat(r->pool, sctx->orderby, " DESC ", 
                                        NULL);
        }
        else {
            sctx->orderby = apr_pstrcat(r->pool, sctx->orderby, " ASC ", 
                                        NULL);      
        }
    }

    return HTTP_OK;
}

/**
 * Processes the LIMIT portion of the XML query
 * @param r The method request record
 * @param sctx The search context
 * @param limit_elem The limit XML element
 * @return The HTTP response code
 */
int parse_limit(request_rec * r, search_ctx * sctx,
		apr_xml_elem * limit_elem)
{
    TRACE();
    apr_xml_elem *nresults_elem = dav_find_child(limit_elem, "nresults");

    if(nresults_elem) 
        sctx->nresults = dav_xml_get_cdata(nresults_elem, r->pool, 1);
    
    return HTTP_OK;
}

/**
 * Processes the OFFSET portion of the XML query
 * @param r The method request record
 * @param sctx The search context
 * @param offset_elem The offset XML element
 * @return The HTTP response code
 */
int parse_offset(request_rec * r, search_ctx * sctx,
		apr_xml_elem * offset_elem)
{
    TRACE();

    if(offset_elem) 
        sctx->offset = dav_xml_get_cdata(offset_elem, r->pool, 1);
    
    return HTTP_OK;
}

/**
 * Builds the XML body of the response from the SQL query results
 * @param r The method request record
 * @param search_results The result(s) of the SQL query
 */
int build_xml_response(apr_pool_t *pool, search_ctx *sctx, dav_response ** res)
{
    dav_response *tail;
    char **dbrow, **good_props, **bad_props;
    int i, results_count = 0, j, bind_id;
    const char *last_href = NULL, *href;
    apr_hash_t *bitmarks = NULL;
    apr_hash_index_t *hi;
    apr_array_header_t *values;
    const void *bitmark;
    void *value;
    const char *good_bitmarks = NULL, *bad_bitmarks = NULL;

    TRACE();

    tail = *res = NULL;
    good_props = apr_pcalloc(pool, sizeof(char *));
    bad_props = apr_pcalloc(pool, sizeof(char *));
    
    results_count = dbms_results_count(sctx->q);

    for(i=0; i<results_count+1; i++) {
        if (i != results_count) {
            dbrow = dbms_fetch_row_num(sctx->db->db, sctx->q, pool, i);

            if (sctx->is_bit_query) {
                href = dbrow[0];
            }
            else {
                bind_id = atoi(dbrow[0]);

                /* Get URI */
                href = apr_hash_get(sctx->bind_uri_map, &bind_id, sizeof(int));
            }
        }
        
        if (!last_href || apr_strnatcmp(href, last_href) != 0 || i == results_count ) {
            if (last_href) {
                apr_text_header hdr = { 0 };

                if(*good_props) {
                    apr_text_append(pool, &hdr, "<D:propstat>" DEBUG_CR
                                    "  <D:prop>" DEBUG_CR);

                    apr_text_append(pool, &hdr, *good_props);

                    apr_text_append(pool, &hdr,
                                    "  </D:prop>" DEBUG_CR
                                    "  <D:status>HTTP/1.1 200 OK</D:status>" DEBUG_CR
                                    "</D:propstat>" DEBUG_CR);
                    *good_props = NULL;
                }

                if(*bad_props) { 
                    apr_text_append(pool, &hdr, "<D:propstat>" DEBUG_CR
                            "  <D:prop>" DEBUG_CR);

                    apr_text_append(pool, &hdr, *bad_props);

                    apr_text_append(pool, &hdr, "  </D:prop>" DEBUG_CR
                            "  <D:status>HTTP/1.1 404 Not Found</D:status>" DEBUG_CR
                            "</D:propstat>" DEBUG_CR);
                    *bad_props = NULL;
                }
                if (bitmarks) {
                    for (hi = apr_hash_first(pool, bitmarks); hi;
                         hi = apr_hash_next(hi)) {
                        apr_hash_this(hi, &bitmark, NULL, &value);
                        values = (apr_array_header_t *)value;
                        if (values->nelts == 0) {
                            if (!bad_bitmarks) {
                                bad_bitmarks = apr_psprintf(pool, " ");
                            }
                            bad_bitmarks = apr_pstrcat(pool, bad_bitmarks,
                                "<", (const char *)bitmark, "/>", NULL);
                        }
                        else {
                            if (!good_bitmarks) {
                                good_bitmarks = apr_psprintf(pool, " ");
                            }
                            good_bitmarks = apr_pstrcat(pool, good_bitmarks,
                                "<", (const char *)bitmark,">",
                                apr_array_pstrcat(pool, values, ','),
                                "</", (const char *)bitmark, ">",
                                DEBUG_CR, NULL );
                        }
                    }
                }

                if (good_bitmarks) {
                    apr_text_append(pool, &hdr, "<bitmarkstat "
                    "xmlns=\"http://limebits.com/ns/1.0/\">" DEBUG_CR
                    "  <bitmark>" DEBUG_CR);

                    apr_text_append(pool, &hdr, good_bitmarks);

                    apr_text_append(pool, &hdr, 
                        "  </bitmark>" DEBUG_CR
                        "  <status>HTTP/1.1 200 OK</status>" DEBUG_CR
                        "</bitmarkstat>" DEBUG_CR);

                    good_bitmarks = NULL;
                }

                if (bad_bitmarks) {
                    apr_text_append(pool, &hdr, "<bitmarkstat "
                    "xmlns=\"http://limebits.com/ns/1.0/\">" DEBUG_CR
                    "  <bitmark>" DEBUG_CR);

                    apr_text_append(pool, &hdr, bad_bitmarks);

                    apr_text_append(pool, &hdr, 
                        "  </bitmark>" DEBUG_CR
                        "  <status>HTTP/1.1 404 Not Found</status>" DEBUG_CR
                        "</bitmarkstat>" DEBUG_CR);

                    bad_bitmarks = NULL;
                }

                dav_response *newres = apr_pcalloc(pool, sizeof(*newres));
                newres->status = 200;
                newres->href = apr_pstrdup(pool, last_href);
                newres->propresult.propstats = hdr.first;
                /* add this to multistatus response */
                if (!*res) {
                    tail = *res = newres;
                }
                else {
                    tail->next = newres;
                    tail = newres;
                }
            }

            if ( i != results_count ) {
                /* for each result build dav_response */
                j = search_mkresponse(pool, sctx, dbrow, good_props, bad_props);
                last_href = apr_pstrdup(pool, href);
                if (sctx->bitmark_support_req) {
                    bitmarks = apr_hash_copy(pool, sctx->bitmarks_map);
                    for (hi = apr_hash_first(pool, bitmarks); hi;
                         hi = apr_hash_next(hi)) {
                        apr_hash_this(hi, &bitmark, NULL, &value);
                        apr_hash_set(bitmarks, (const char *)bitmark, 
                            APR_HASH_KEY_STRING, 
                            apr_array_make(pool, 1, sizeof(char *)));
                    }
                }
            }
        }
        
        if (bitmarks) {
            values = apr_hash_get(bitmarks, dbrow[j], APR_HASH_KEY_STRING);
            char **v = apr_array_push(values);
            *v = apr_pstrdup(pool, dbrow[j+1]);
        }
         
    }

    return HTTP_OK;
}

/* only sets name, ns_id */
static dav_repos_property *get_prop_from_prop_key(apr_pool_t *pool,
                                                  char *prop_key)
{
    dav_repos_property *prop = apr_pcalloc(pool, sizeof(*prop));
    char *last = NULL, *token = NULL, *next_token = NULL;

    token = apr_strtok(apr_pstrdup(pool, prop_key), "_", &last);
    next_token = apr_strtok(NULL, "_", &last); 
    while(next_token) {
        if(prop->name)
            prop->name = apr_pstrcat(pool, prop->name, token, NULL);
        else
            prop->name = apr_pstrdup(pool, token);

        token = next_token;
        next_token = apr_strtok(NULL, "_", &last);
    }
    prop->ns_id = atoi(token);

    return prop;
}

int search_mkresponse(apr_pool_t *pool, search_ctx *sctx, char **dbrow,
                      char **good_props, char **bad_props)
{
    apr_hash_index_t *hi;
    const void *prop_key;
    char *propval;
    int i = 1;

    TRACE();

    for(hi = apr_hash_first(pool, sctx->prop_map); 
        hi;
        hi = apr_hash_next(hi)) {
        apr_hash_this(hi, &prop_key, NULL, NULL);
        dav_repos_property *prop = 
            get_prop_from_prop_key(pool, (char *)prop_key);
        prop->namespace_name = apr_hash_get(sctx->namespace_map, &prop->ns_id,
                                            sizeof(long));

        propval = dbrow[i++];

        if(!propval || !strlen(propval)) {
            if(!*bad_props) {
                 *bad_props = apr_psprintf(pool, " ");
            }
            *bad_props = apr_pstrcat(pool, *bad_props, "<", prop->name, 
                                     " xmlns=\"", prop->namespace_name, "\"/>",
                                     NULL);
            continue;
        }

        /* do some post-processing property values if required */
        if(prop->ns_id == get_ns_id(pool, sctx, "DAV:")) {
            if(strcmp(prop->name, "creationdate") == 0) {
                char *date = 
                    apr_pcalloc(pool, APR_RFC822_DATE_LEN * sizeof(char));
                dav_repos_format_strtime(DAV_STYLE_ISO8601, propval, date);
                propval = date;
            }
            else if(strcmp(prop->name, "getlastmodified") == 0) {
                char *date = 
                    apr_pcalloc(pool, APR_RFC822_DATE_LEN * sizeof(char));
                dav_repos_format_strtime(DAV_STYLE_RFC822, propval, date);
                propval = date;
            }
            else if(strcmp(prop->name, "resource-id") == 0) {
                propval = apr_psprintf(pool, "<D:href>urn:uuid:%s</D:href>",
                                       add_hyphens_to_uuid(pool, propval));
            }
            else if(strcmp(prop->name, "owner") == 0) {
                dav_principal *owner = 
                    dav_repos_get_prin_by_name(sctx->db_r->resource->info->rec,
                                               propval);
                propval = apr_psprintf(pool, "<D:href>%s</D:href>", 
                                       dav_repos_principal_to_s(owner));
            }
        }

        if(!*good_props) {
            *good_props = apr_psprintf(pool, " ");
        }

        *good_props = apr_pstrcat(pool, *good_props, "<", prop->name, 
                                 " xmlns=\"", prop->namespace_name, 
                                 "\">", propval, "</", prop->name, ">", 
                                 DEBUG_CR, NULL );
    }

    return i;
}

/*
 ** search hook functions
 */
const dav_hooks_search dav_repos_hooks_search = {
    dav_repos_set_option_head,
    dav_repos_search_resource
};

int build_query_select(request_rec *r, search_ctx *sctx)
{
    apr_hash_index_t *hi;
    void *val;

    TRACE();

    /* create the select part of the query */
    if (sctx->is_bit_query) {
        sctx->select = apr_psprintf(r->pool, 
            "SELECT '/' || b1.name || '/' || b2.name || '/' || b3.name || "
                "'/' || b4.name AS path");
    }
    else {
        sctx->select = apr_psprintf(r->pool, "SELECT binds.id");
    }

    for(hi = apr_hash_first(r->pool, sctx->prop_map); hi ; 
        hi = apr_hash_next(hi)) {
        apr_hash_this(hi, NULL, NULL, &val);
        sctx->select = apr_pstrcat(r->pool, sctx->select, ", ",
                                   (char *)val, NULL);
    }

    if (sctx->bitmark_support_req) {
        sctx->select = apr_pstrcat(r->pool, sctx->select, 
                        ", bitmarks.name, bitmarks.value", NULL);
    }

    return HTTP_OK;
}

int build_query_from(request_rec *r, search_ctx *sctx)
{
    apr_hash_index_t *hi;
    apr_pool_t *pool = r->pool;
    const void *prop_key;
    dav_repos_property *prop;

    TRACE();

    /* Live property tables */
    /* FIXME: JOIN relevant tables only, in case of liveprops */
    sctx->from = 
        apr_psprintf(pool, 
                     " FROM resources "
                     " LEFT JOIN locks ON resources.id = locks.resource_id " 
                     " LEFT JOIN media ON resources.id = media.resource_id "
                     " LEFT JOIN principals ON "
                     "principals.resource_id = resources.owner_id ");

    if (sctx->is_bit_query) {
        sctx->from = apr_pstrcat(pool, sctx->from, 
            " LEFT JOIN binds b4 ON b4.resource_id = resources.id"
            " INNER JOIN binds b3 ON b4.collection_id = b3.resource_id"
            " INNER JOIN binds b2 ON b3.collection_id = b2.resource_id"
            " INNER JOIN binds b1 ON b2.collection_id = b1.resource_id", NULL);

        if (sctx->bitmark_support_req) {
            sctx->from = apr_pstrcat(pool, sctx->from, 
            " LEFT JOIN binds b5 ON b5.name = resources.uuid "
            " INNER JOIN binds b6 ON b6.collection_id = b5.resource_id ", NULL);
        }
    }
    else {
        sctx->from = apr_pstrcat(pool, sctx->from, 
            " LEFT JOIN binds ON resources.id = binds.resource_id ", NULL);
    }

    for(hi = apr_hash_first(pool, sctx->prop_map); hi;
        hi = apr_hash_next(hi)) {
        apr_hash_this(hi, &prop_key, NULL, NULL);
        prop = get_prop_from_prop_key(pool, (char *)prop_key);
        if(prop->ns_id != get_ns_id(pool, sctx, "DAV:")) {
            /* Dead property */
            char *dead_property_subquery = 
                apr_psprintf(pool,
                             " LEFT JOIN ("
                                "SELECT resource_id, value AS \"%s\""
                                " FROM properties"
                                " WHERE name = '%s' AND namespace_id = %ld"
                             ") \"%s_table\" ON \"%s_table\".resource_id = resources.id ",
                             (char *)prop_key, prop->name, prop->ns_id,
                             (char *)prop_key, (char *)prop_key);
            sctx->from = apr_pstrcat(pool, sctx->from, dead_property_subquery, NULL);
        }
    }

    if (sctx->bitmark_support_req) {
        sctx->from = apr_pstrcat(pool, sctx->from,
                        " LEFT JOIN properties bitmarks "
                        "ON bitmarks.resource_id = b6.resource_id ", NULL);
    }

    return HTTP_OK;
}

int build_query_where(request_rec *r, search_ctx *sctx)
{
    apr_hash_index_t *hi;
    const void *bind, *bitmark;
    char *temp;

    TRACE();

    if (sctx->is_bit_query) {
        sctx->where = apr_psprintf(r->pool, 
            " WHERE b1.collection_id = %d AND b1.name = 'home'"
            " AND b3.name = 'bits'", ROOT_COLLECTION_ID);
    }
    else {    
        sctx->where = apr_psprintf(r->pool, " WHERE binds.id IN (");
        for(hi = apr_hash_first(r->pool, sctx->bind_uri_map); hi;
            hi = apr_hash_next(hi)) {
            apr_hash_this(hi, &bind, NULL, NULL);
            temp = apr_psprintf(r->pool, " %d,", *(int *)bind);
            sctx->where = apr_pstrcat(r->pool, sctx->where, temp, NULL);
        }

        /* correct the last ',' */
        sctx->where[strlen(sctx->where) - 1] = ')';
    }

    if (sctx->b2_rid) {
        temp = apr_psprintf(r->pool, " AND b2.resource_id = %d ", sctx->b2_rid);
        sctx->where = apr_pstrcat(r->pool, sctx->where, temp, NULL);
    }

    if(sctx->where_cond) {
        /* Add other WHERE conditions */
        sctx->where = apr_pstrcat(r->pool, sctx->where, " AND ", 
                                  sctx->where_cond, NULL);
    }

    if (sctx->bitmark_support_req) {
        sctx->where = apr_pstrcat(r->pool, sctx->where, 
                        " AND (bitmarks.name IN (", NULL);
        for(hi = apr_hash_first(r->pool, sctx->bitmarks_map); hi;
            hi = apr_hash_next(hi)) {
            apr_hash_this(hi, &bitmark, NULL, NULL);
            sctx->where = apr_pstrcat(r->pool, sctx->where, 
                            "'", (char *)bitmark, "',", NULL);
        }

        /* correct the last ',' */
        sctx->where[strlen(sctx->where) - 1] = ')';

        sctx->where = apr_pstrcat(r->pool, sctx->where, 
                        " OR bitmarks.name IS NULL)",
                        NULL);
    }

    return HTTP_OK;
}

int build_query_orderby(request_rec *r, search_ctx *sctx)
{
    TRACE();
    return HTTP_OK;
}

int build_query_limit(request_rec *r, search_ctx *sctx)
{
    TRACE();
    if(sctx->nresults) {
        sctx->limit = apr_psprintf(r->pool, " LIMIT %s", sctx->nresults);
    } else {
        sctx->limit = " ";
    }

    return HTTP_OK;
}

int build_query_offset(request_rec *r, search_ctx *sctx)
{
    TRACE();
    if(sctx->offset) {
        sctx->off = apr_psprintf(r->pool, " OFFSET %s", sctx->offset);
    }
    return HTTP_OK;
}
