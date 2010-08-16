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

#ifndef __SEARCH_H__
#define __SEARCH_H__

#include <httpd.h>
#include <http_config.h>
#include <http_protocol.h>
#include <http_log.h>
#include <http_core.h>		/* for ap_construct_url */
#include <mod_dav.h>
#include <apr_strings.h>
#include <apr_hash.h>

#include "dav_repos.h"
#include "dbms.h"
#include "util.h"
#include "bridge.h"
#include "dbms_api.h"

typedef struct {
    const char *propname;       /* DAV: name for the prop */
    const char *attribute;      /* Attribute name in the database */
} prop_map_t;

typedef struct {
    dav_repos_resource *db_r;   /* Resource corresponding to Request-URI */
    dav_repos_db *db;           /* DB handle */
    dav_repos_query *q;         /* Query handle */
    char *query;
    char *select;
    char *from;
    char *where;
    char *where_cond;
    char *orderby;
    char *limit;
    const char *offset;
    char *off;
    char *err_msg;
    const char *nresults;       /* No of results to limit to */
    apr_hash_t *bind_uri_map;   /* bind_id:URI mapping */
    apr_hash_t *prop_map;	/* Propname:Attribute map */
    apr_hash_t *bitmarks_map;	/* Bitmarks:Attribute map */
    apr_hash_t *namespace_map;	/* ns_id:namespace map */
    apr_xml_doc *doc;
    int media_props_req;        /* set if media properties where queried */
    int is_bit_query;           /* set if WHERE clause filters on is-bit */
    int bitmark_support_req;    /* set if bitmarks support required */
    const char *b4_name;        /* set to name of bind 4 in is-bit query */
    int b2_rid;                 /* set to the resource_id on which to filter
                                   the second bind for a is-bit query */
} search_ctx;

typedef struct dead_prop_list dead_prop_list;

typedef struct {
    const char *href;
    const char *name;
    const char *value;
} bitmark;

int parse_query(request_rec * r, search_ctx * sctx);

int build_query(request_rec * r, search_ctx * sctx);

int build_xml_response(apr_pool_t *pool, search_ctx * sctx,
		       dav_response ** res);

int search_mkresponse(apr_pool_t *pool, search_ctx *sctx, char **dbrow,
                      char **good_props, char **bad_props);

int parse_select(request_rec *r, search_ctx *sctx, apr_xml_elem *select_elem);

int parse_props(request_rec * r, search_ctx * sctx, apr_xml_elem *select_elem);

int parse_from(request_rec * r, search_ctx * sctx, apr_xml_elem * from_elem);

int parse_scope(request_rec * r, search_ctx * sctx, apr_xml_elem * scope_elem);

int parse_depth(apr_xml_elem *cur_elem, apr_pool_t *pool);

void register_ops(apr_pool_t *pool);

int parse_where(request_rec * r, search_ctx * sctx,
		apr_xml_elem * where_elem);

int parse_comp_ops(request_rec *r, apr_xml_elem *cur_elem, search_ctx *sctx);

int parse_log_ops(request_rec *r, apr_xml_elem *cur_elem, search_ctx *sctx);

int parse_is_coll(request_rec *r, apr_xml_elem *cur_elem, search_ctx *sctx);

int parse_is_bit(request_rec *r, apr_xml_elem *cur_elem, search_ctx *sctx);

int parse_is_defined(request_rec *r, apr_xml_elem *cur_elem, search_ctx *sctx);

int parse_orderby(request_rec * r, search_ctx * sctx,
		  apr_xml_elem * orderby_elem);

int parse_order(request_rec *r, search_ctx *sctx,
                apr_xml_elem *order_elem);

int parse_limit(request_rec * r, search_ctx * sctx,
		apr_xml_elem * limit_elem);

int parse_offset(request_rec * r, search_ctx * sctx,
		apr_xml_elem * offset_elem);

int build_query_select(request_rec *r, search_ctx *sctx);

int build_query_from(request_rec *r, search_ctx *sctx);

int build_query_where(request_rec *r, search_ctx *sctx);

int build_query_orderby(request_rec *r, search_ctx *sctx);

int build_query_limit(request_rec *r, search_ctx *sctx);

int build_query_offset(request_rec *r, search_ctx *sctx);

const char *prop_attr_lookup(apr_pool_t *ppool, apr_pool_t *pool, 
                             apr_xml_elem *prop, char *prop_key, search_ctx *sctx);

apr_hash_t *get_liveprop_map(apr_pool_t *pool);

dav_error *dav_repos_deliver_property_stats(request_rec * r,
					    const dav_resource * resource,
					    const apr_xml_doc * doc,
					    ap_filter_t * output);

#endif
