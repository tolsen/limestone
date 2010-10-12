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

#ifndef __DBMS_ACL_H__
#define __DBMS_ACL_H__

#include <httpd.h>
#include <http_config.h>
#include <http_protocol.h>
#include <http_log.h>
#include <http_core.h>		/* for ap_construct_url */
#include <mod_dav.h>
#include <apr_strings.h>
#include <apr_hash.h>

#include "dav_repos.h"
#include "dbms_api.h"
#include "dbms.h"
#include "util.h"
#include "acl.h"                /* for dav_repos_get_principal_url */

#define ACL_GRANT "G"		/* grant entry in the database */
#define ACL_DENY "D"		/* deny entry in the database */
#define DB_TRUE "t"             /* truth value in the database */
#define DB_FALSE "f"

/**
 * Nested Set ACL Inheritance specific fields
 */
struct dav_acl_private {
    int parent_id;
    int lft;
    int rgt;
};

const char *dbms_lookup_prin_id(apr_pool_t *pool,
                                const dav_repos_db * d,
                                int principal_id);

int dbms_get_principals(const dav_repos_db *db, apr_pool_t *pool,
			dav_repos_resource *r, dav_repos_resource *principals);

int dbms_get_protected_aces(const dav_repos_db *d, dav_repos_resource *r, 
                            dav_acl **acl);

int dbms_delete_own_aces(const dav_repos_db * d, dav_repos_resource * r);

int dbms_add_ace_privilege(const dav_repos_db * d,
			   const dav_repos_resource * r,
			   const dav_privilege * privilege, int ace_id);

dav_error *dbms_add_ace(const dav_repos_db * d, dav_repos_resource * r,
		 const dav_ace * ace, int acl_id);

int dbms_get_acl(const dav_repos_db * d, dav_repos_resource * r);

int dbms_is_allow(const dav_repos_db * db, long priv_ns_id, const char *privilege, 
                  const dav_principal *principal, dav_repos_resource *r);

dav_privileges *dbms_get_privileges(const dav_repos_db * db,
				    apr_pool_t * pool, long principal_id,
				    long resource_id);

dav_error *dbms_inherit_parent_aces(const dav_repos_db *d, 
                                    const dav_repos_resource *db_r, 
                                    int parent);

dav_error *dbms_update_principal_property_aces(dav_repos_db *d, 
                                               dav_repos_resource *db_r,
                                               int prop_ns_id, 
                                               const char *prop_name,
                                               int principal_id);

int dbms_get_privilege_id(const dav_repos_db *d, const dav_repos_resource *db_r, 
                          const dav_privilege *privilege);

dav_error *dbms_change_acl_parent(dav_repos_db *d,
                                  dav_repos_resource *db_r,
                                  int new_parent_id);
#endif
