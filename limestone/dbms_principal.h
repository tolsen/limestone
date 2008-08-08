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

#ifndef DBMS_PRINCIPAL_H
#define DBMS_PRINCIPAL_H

#include "dbms.h"
#include <apr_tables.h>

/* Principal Functions */
dav_error *dbms_get_principal_id_from_name(apr_pool_t *pool, const dav_repos_db *d,
                                           const char *name, long *prin_id);

dav_error *dbms_insert_principal(const dav_repos_db *d,
                                 dav_repos_resource *r);

dav_error *dbms_set_principal_email(apr_pool_t *pool, const dav_repos_db *d,
                                    long principal_id, const char *email);

const char *dbms_get_principal_email(apr_pool_t *pool, const dav_repos_db *d,
                                     long principal_id);

apr_hash_t *dbms_get_domain_map(apr_pool_t *pool, const dav_repos_db *d,
                                long principal_id);

const char *dbms_get_domain_path(apr_pool_t *pool, dav_repos_db *d, const char *host);

dav_error *dbms_set_domain_map(apr_pool_t *pool, const dav_repos_db *d,
                               long principal_id, apr_hash_t *domain_map);

dav_error *dbms_insert_user(const dav_repos_db *d, dav_repos_resource *r,
                            const char *pwhash);

dav_error *dbms_update_user(const dav_repos_db *d, dav_repos_resource *r,
                            const char *pwhash);

const char *dbms_get_user_pwhash(apr_pool_t *pool, const dav_repos_db *d,
                                 long principal_id);

int dbms_is_prin_in_grp(apr_pool_t *pool, const dav_repos_db *d,
                        long prin_id, long grp_id);

int dbms_will_loop_prin_add_grp(apr_pool_t *pool, const dav_repos_db *d,
                                long prin_id, long grp_id);

dav_error *dbms_add_prin_to_group(apr_pool_t *pool, const dav_repos_db *d,
                                  long group_id, long principal_id);

dav_error *dbms_add_prin_to_grp_xitively(apr_pool_t *pool, 
                                         const dav_repos_db *d,
                                         long grp_id, long prin_id);

dav_error *dbms_rem_prin_frm_grp(apr_pool_t *pool, const dav_repos_db *d,
                                 long grp_id, long prin_id);

dav_error *dbms_rem_prin_frm_grp_xitively(apr_pool_t *pool, 
                                          const dav_repos_db *d,
                                          long grp_id, long prin_id);

int dbms_get_user_login_to_all_domains(apr_pool_t *pool, const dav_repos_db *d,
                                       long principal_id);

dav_error *dbms_set_user_login_to_all_domains(apr_pool_t *pool, 
                                              const dav_repos_db *d,
                                              long principal_id,
                                              int login_to_all_domains);

int dbms_get_principal_type_from_name(apr_pool_t *pool, const dav_repos_db *db,
                                      const char *name);

dav_error *dbms_get_group_members(const dav_repos_db *db,
                                  const dav_repos_resource *group_dbr, 
                                  apr_array_header_t **p_members);

dav_error *dbms_calculate_group_changes(const dav_repos_db *db,
                                        const dav_repos_resource *group_dbr,
                                        apr_hash_t *new_members,
                                        apr_array_header_t **members_to_remove);

#endif /* DBMS_PRINCIPAL_H */
