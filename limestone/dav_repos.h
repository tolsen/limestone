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

#ifndef __dav_repos_H__
#define __dav_repos_H__

#include <limits.h>     /* for INT_MAX */
#include <mod_dav.h>

/* it is recommended to use LS_ERROR as error state 
 * in functions returning int */
#define LS_ERROR        -INT_MAX

/* REMOVE DEBUG INFO */
#undef DBG0
#undef DBG1
#undef DBG2
#undef DBG3
#define DBG0(A) printf(A)
#define DBG1(A,B) printf(A,B)
#define DBG2(A,B,C) printf(A,B,C)
#define DBG3(A,B,C,D) printf(A,B,C,D)
/*
#include <http_log.h>
#define DBG0(f)          ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, NULL,\
                                      ("%s %d "f), APLOG_MARK)
#define DBG1(f,a1)       ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, NULL,\
                                      "%s %d "f, APLOG_MARK, a1)
#define DBG2(f,a1,a2)    ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, NULL,\
                                      "%s %d "f, APLOG_MARK, a1, a2)
#define DBG3(f,a1,a2,a3) ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, NULL,\
                                      "%s %d "f, APLOG_MARK, a1, a2, a3)
*/
/** Collection Id of the root directory */
#define ROOT_COLLECTION_ID  2
/** Updated_at for the root directory */
#define ROOT_UPDATED_AT "2006-07-14 13:15:49"

#define TRACE() DBG1("\n- TRACE : %s\n",  __func__ )

#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "$URL$ $Rev$ Static Build"
#endif

#ifdef __CYGWIN__
#define atoll(x)	strtoll(x, 0, 10)
#define atoull(x)	strtoull(x, 0, 10)
#endif

#ifndef DAV_REPOS_DBMS_OPAQUE_T
#define DAV_REPOS_DBMS_OPAQUE_T

/* type_id's */
enum {
    dav_repos_RESOURCE = 1,              
    dav_repos_COLLECTION,            
    dav_repos_PRINCIPAL,             
    dav_repos_USER,                  
    dav_repos_GROUP,                 
    dav_repos_REDIRECT,              
    dav_repos_VERSION,               
    dav_repos_VERSIONED,             
    dav_repos_VERSIONHISTORY,        
    dav_repos_VERSIONED_COLLECTION,  
    dav_repos_COLLECTION_VERSION,    
    dav_repos_LOCKNULL,              
    dav_repos_MAX_TYPES
}; 

/* global */
apr_hash_t *dav_repos_mime_type_ext_map;
const char *dav_repos_resource_types[dav_repos_MAX_TYPES];

int dav_repos_get_type_id(const char *type);

struct dav_repos_dbms;
typedef struct dav_repos_dbms dav_repos_dbms;
#endif

typedef struct {
    const char *tmp_dir;
    const char *file_dir;

    const char *db_driver;
    enum { UNKNOWN = 0, MYSQL = 1, PGSQL = 2 } dbms;
    const char *db_params;

    int use_gc;
    int keep_files;
    const char *css_uri;
    int quota; 

    dav_repos_dbms *db;
} dav_repos_server_conf;

/* dav_repos_server_conf is in repos.h */
typedef dav_repos_server_conf dav_repos_db;


/* our hooks structures; these are gathered into a dav_provider */
extern const dav_hooks_repository       dav_repos_hooks_repos;
extern const dav_hooks_propdb           dav_repos_hooks_propdb;
extern const dav_hooks_search           dav_repos_hooks_search;
//extern const dav_hooks_liveprop   dav_repos_hooks_liveprop;
extern const dav_hooks_vsn              dav_repos_hooks_vsn;
extern const dav_hooks_binding 	        dav_repos_hooks_binding;
extern const dav_hooks_locks            dav_repos_hooks_locks;
extern const dav_hooks_acl              dav_repos_hooks_acl;
extern const dav_hooks_transaction      dav_repos_hooks_transaction;

/*
 ** Get DB handler from request_rec
 */
dav_repos_db *dav_repos_get_db(request_rec * r);

/* handle the SEARCH method */
int dav_repos_method_search(request_rec * r);

/* some prototype */
const char *dav_repos_getetag(const dav_resource * resource);
dav_error *dav_repos_get_parent_resource(const dav_resource * resource,
					 dav_resource ** result_parent);
dav_error *dav_repos_create_resource(dav_resource *resource, int params);

dav_error *dav_repos_new_resource(request_rec *r, const char *root_path, 
                                  dav_resource **result_resource);

#endif
