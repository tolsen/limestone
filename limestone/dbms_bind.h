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

#ifndef DBMS_BIND_H
#define DBMS_BIND_H

#include "dbms.h"

typedef struct dbms_bind_list {
    long bind_id;

    long parent_id;
    long resource_id;
    const char *bind_name;
    char *updated_at;

    const char *uri;

    struct dbms_bind_list *next;
} dbms_bind_list;

dav_error *dbms_lookup_uri(apr_pool_t *pool, const dav_repos_db *d,
                           const char *uri, const dbms_bind_list **p_bind);

dav_error *dbms_get_collection_max_updated_at(apr_pool_t *pool,
                                              const dav_repos_db *d,
                                              long collection_id,
                                              const char *updated_at,
                                              const char **p_max_updated_at);

/* Bind Functions */
dav_error *dbms_insert_bind(apr_pool_t *pool, const dav_repos_db * db,
                            int resource_id,
                            int collection_id, const char *bindname);

dav_error *dbms_insert_bind_list(apr_pool_t *pool, const dav_repos_db *d,
                                 dbms_bind_list *bind_list, int array);

dav_error *dbms_delete_bind(apr_pool_t *pool, const dav_repos_db *d,
                            long coll_id, long res_id, const char *bind_name);

dav_error *dbms_find_shortest_path_excl_binds(apr_pool_t *pool,
                                              const dav_repos_db *db,
                                              long from_res_id, long to_res_id,
                                              const dbms_bind_list *bind_list,
                                              char **shortest_path);

dav_error *dbms_find_shortest_path(apr_pool_t *pool, const dav_repos_db *db,
                                   long from_res_id, long to_res_id,
                                   char **shortest_path);

dav_error *dbms_get_bind_resource_id(apr_pool_t *pool, const dav_repos_db *db,
                                     long coll_id, const char *bind_name, 
                                     long *res_id);

dav_error *dbms_get_bind(apr_pool_t *pool, const dav_repos_db *db,
                         dbms_bind_list *bind);

dav_error *dbms_rebind_resource(apr_pool_t *pool, const dav_repos_db *db,
                                long src_parent_id, const char *src_bind_name,
                                long dst_parent_id, const char *dst_bind_name);

dav_error *dbms_get_path_binds(apr_pool_t *pool, const dav_repos_db *d,
                               const char *path,
                               dbms_bind_list **p_bindlist, int *size);

dav_error *dbms_detect_bind(apr_pool_t *pool, const dav_repos_db *db,
                            const dbms_bind_list *bind, const char *path, 
                            char **pprefix);

dav_error *dbms_get_child_binds(const dav_repos_db *db,dav_repos_resource *db_r,
                                int exclude_self, dbms_bind_list **p_bindlist);

dav_error *dbms_insert_cleanup_reqs(apr_pool_t *pool,
                                   const dav_repos_db *db,
                                   dbms_bind_list *bind_list);

dav_error *dbms_next_cleanup_req(apr_pool_t *pool, 
                                 const dav_repos_db *db, long *presource_id);

#endif
