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

#include "lock_bridge.h"
#include "dbms_locks.h"
#include "bridge.h"
#include "dbms_bind.h"

/* @brief Removes indirect locks on a resource if the resource is not
   reachable from the lockroot
 * @param lockdb The locks database
 * @param db_r The resource
 * @param removed set to 1 if any locks were removed, 0 otherwise
 * @return NULL on success, error otherwise
 */
static dav_error *sabridge_remove_all_indirect(dav_lockdb *lockdb, 
                                               dav_repos_resource *db_r,
                                               int *removed)
{
    apr_pool_t *pool = lockdb->info->pool;
    dav_repos_db *db = lockdb->info->db;
    dav_lock *locks=NULL;
    dbms_get_locks(lockdb, db_r, 0, &locks);

    *removed = 0;
    while(locks) {
        char *path;
        dbms_find_shortest_path(pool, db, locks->info->res_id,
                                db_r->serialno, &path);
        if(!path) {
            dbms_remove_indirect_lock(lockdb, locks, db_r);
            *removed = 1;
        }
        locks = locks->next;
    }
    return NULL;
}

dav_error *sabridge_remove_indirect_locks_d_inf(dav_lockdb *lockdb,
                                                dav_repos_resource *db_r)
{
    apr_pool_t *pool = lockdb->info->pool;
    dav_repos_db *d = lockdb->info->db;
    dav_repos_resource *parent;
    dbms_bind_list *bind = apr_pcalloc(pool, sizeof(*bind));
    dav_error *err = NULL;
    dav_repos_resource *link_tail, *link_item, *dbr_next_bak;

    sabridge_retrieve_parent(db_r, &parent);
    /* disable bind temporarily */
    bind->parent_id = parent->serialno;
    bind->bind_name = basename(db_r->uri);
    err = dbms_get_bind(pool, d, bind);
    err = dbms_delete_bind(pool, d, bind->parent_id,
                           bind->resource_id, bind->bind_name);

    dbr_next_bak = db_r->next;
    db_r->next = NULL;
    link_item = link_tail = db_r;
    while (link_item) {
        int removed = 0;
        sabridge_remove_all_indirect(lockdb, link_item, &removed);
        if (removed &&
            (link_item->resourcetype == dav_repos_COLLECTION ||
             link_item->resourcetype == dav_repos_VERSIONED_COLLECTION))
            sabridge_get_collection_children
              (d, db_r, 1, NULL, &(link_tail->next), &link_tail, NULL);
        link_item = link_item->next;
    }
    
    /* re-enable the bind */
    err = dbms_insert_bind(pool, d, bind->resource_id,
                           bind->parent_id, bind->bind_name);
    db_r->next = dbr_next_bak;
    return err;
}

int use_locknull(request_rec *r)
{
    char *no_locknull[] = {
        "litmus",
        NULL
    };
    int i = 0;
    const char *ua;

    ua = apr_table_get(r->headers_in, "User-Agent");
    if (ua == NULL) return 1;

    while(no_locknull[i]) {
        if(ap_strstr_c(ua, no_locknull[i]))
            return 0;
        i++;
    }

    return 1;
}
