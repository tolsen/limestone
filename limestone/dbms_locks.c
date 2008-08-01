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

#include "dbms_locks.h"
#include "dbms_api.h"
#include "util.h"

#define APR_WANT_MEMFUNC
#include <apr_want.h>
#include <apr_strings.h>
#include <time.h>

dav_error *dbms_get_locks_by_where_str(dav_lockdb *lockdb,
                                       dav_repos_resource *db_r,
                                       int resolve_indirect,
                                       dav_lock **locks,
                                       const char *where_str)
{
    apr_pool_t *pool = lockdb->info->pool;
    dav_repos_db *db = lockdb->info->db;
    dav_repos_query *q = NULL;
    int lock_root_id, indirect = 0;
    dav_lock *next_lock, *link_tail, *dummy_link_head;
    char *query_str;
    time_t curr_time = time(NULL);
    char *exp_lids=NULL; /* comma seperated expired lock ids */

    TRACE();

    query_str = apr_psprintf
      (pool, 
       "SELECT locks.resource_id, locks.uuid, locks.form, locks.depth, "
       "       locks.expires_at, locks.owner_info, principals.name, "
       "       locks.id, locks.lockroot "
       "FROM locks_resources "
       "       INNER JOIN locks ON locks.id=locks_resources.lock_id "
       "       INNER JOIN principals ON principals.resource_id=locks.owner_id "
       "WHERE %s", where_str);
    q = dbms_prepare(pool, db->db, query_str);

    if (dbms_execute(q)) {
        dbms_query_destroy(q);
        return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "DBMS Error retrieving locks");
    }
    
    link_tail = dummy_link_head = apr_pcalloc(pool, sizeof(dav_lock));
    while (dbms_next(q)) {
        char *scope;
        dav_locktoken *next_lt = apr_pcalloc(pool, sizeof(*next_lt));
        char *timeout_datetime;

        lock_root_id = dbms_get_int(q, 1);
        if (lock_root_id != db_r->serialno)
            indirect = 1;

        next_lt->char_uuid = dbms_get_string(q, 2);
        next_lock = dav_repos_alloc_lock(lockdb, next_lt);
        next_lock->type = DAV_LOCKTYPE_WRITE;

        scope = dbms_get_string(q, 3);
        if (scope[0] == 'S')
            next_lock->scope = DAV_LOCKSCOPE_SHARED;
        else if (scope[0] == 'X')
            next_lock->scope = DAV_LOCKSCOPE_EXCLUSIVE;
        else next_lock->scope = DAV_LOCKSCOPE_UNKNOWN;

        if (indirect && !resolve_indirect) {
            next_lock->info->res_id = db_r->serialno;
            next_lock->rectype = DAV_LOCKREC_INDIRECT;
        } else {
            next_lock->info->res_id = lock_root_id;
            next_lock->rectype = DAV_LOCKREC_DIRECT;
            next_lock->depth = dbms_get_int(q, 4)? DAV_INFINITY:0;
        }

        timeout_datetime = dbms_get_string(q, 5);
        next_lock->timeout = time_datetime_to_ansi(timeout_datetime);
        next_lock->owner = dbms_get_string(q, 6);
        next_lock->auth_user = dbms_get_string(q, 7);
        next_lock->info->lock_id = dbms_get_int(q, 8);
        next_lock->lockroot = apr_psprintf(pool, "%s%s", db_r->root_path, 
                                           dbms_get_string(q, 9));

        if (next_lock->timeout && curr_time >= next_lock->timeout) {
            long exp_lid = next_lock->info->lock_id;
            if (exp_lids)
                exp_lids = apr_psprintf(pool, "%s,%ld", exp_lids, exp_lid);
            else exp_lids = apr_psprintf(pool, "%ld", exp_lid);
        } else {
            link_tail->next = next_lock;
            link_tail = next_lock;
        }
    }
    dbms_query_destroy(q);
    *locks = dummy_link_head->next;

    if (exp_lids) dbms_delete_exp_locks(lockdb, exp_lids);
    return NULL;
}

dav_error *dbms_delete_exp_locks(dav_lockdb *lockdb, const char *exp_lock_ids)
{
    apr_pool_t *pool = lockdb->info->pool;
    dav_repos_db *d = lockdb->info->db;
    dav_repos_query *q = NULL;
    char *query_str;
    dav_error *err = NULL;
    char *exp_locknull_ids = NULL;

    TRACE();

    query_str =
      apr_psprintf(pool, "SELECT id FROM resources WHERE id IN "
                   "(SELECT resource_id FROM locks WHERE locks.id IN (%s)) "
                   " AND type = ? ", exp_lock_ids);
    q = dbms_prepare(pool, d->db, query_str);
    dbms_set_string(q, 1, dav_repos_resource_types[dav_repos_LOCKNULL]);

    if (dbms_execute(q))
        err = dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                            "DBMS Error retrieving locks");
    while (dbms_next(q)) {
      long locknull_res_id = dbms_get_int(q, 1);
        if (exp_locknull_ids == NULL)
	  exp_locknull_ids = apr_psprintf(pool, "%ld", locknull_res_id);
        else exp_locknull_ids = apr_psprintf(pool, ",%ld", locknull_res_id);
    }

    dbms_query_destroy(q);
    if (err) return err;

    query_str = 
      apr_psprintf(pool, "DELETE FROM locks WHERE id IN (%s)", exp_lock_ids);

    q = dbms_prepare(pool, d->db, query_str);
    if (dbms_execute(q))
        err = dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                            "DBMS Error retrieving locks");
    dbms_query_destroy(q);
    if (err) return err;

    if (exp_locknull_ids != NULL) {
        query_str = apr_psprintf
          (pool, "DELETE FROM resources WHERE id IN (%s) AND id NOT IN "
           "(SELECT resource_id FROM locks WHERE locks.resource_id IN (%s))",
           exp_locknull_ids, exp_locknull_ids);
        if (dbms_execute(q))
            err = dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                                "DBMS Error retrieving locks");
        dbms_query_destroy(q);
    }
    return err;
}

dav_error *dbms_get_locks(dav_lockdb *lockdb,
                          dav_repos_resource *db_r,
                          int resolve_indirect,
                          dav_lock **locks)
{
    char *where_str = 
      apr_psprintf(db_r->p, "locks_resources.resource_id=%ld", db_r->serialno);
    return dbms_get_locks_by_where_str(lockdb, db_r, resolve_indirect, locks,
                                       where_str);
}

dav_error *dbms_find_lock_by_token(dav_lockdb *lockdb, dav_repos_resource *db_r,
                                   const dav_locktoken *locktoken,
                                   dav_lock **lock)
{
    char *where_str =
      apr_psprintf(db_r->p, "locks_resources.resource_id=%ld AND "
                   "locks.uuid = '%s'", db_r->serialno, locktoken->char_uuid);

    return dbms_get_locks_by_where_str(lockdb, db_r, 0, lock, where_str);
}

dav_error *dbms_resource_has_locks(dav_lockdb *lockdb,
                                   dav_repos_resource *db_r,
                                   int *locks_present)
{
    apr_pool_t *pool = lockdb->info->pool;
    dav_repos_db *d = lockdb->info->db;
    dav_repos_query *q = NULL;
    int ierrno;

    q = dbms_prepare(pool, d->db,
                     "SELECT COUNT(lock_id) FROM locks_resources "
                     "WHERE resource_id=?");
    dbms_set_int(q, 1, db_r->serialno);

    if (dbms_execute(q)) {
        dbms_query_destroy(q);
        return dav_new_error(db_r->p, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "DBMS Error retrieving locks");
    }
    
    if ((ierrno = dbms_next(q)) < 0) {
        dbms_query_destroy(q);
        return dav_new_error(db_r->p, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "DBMS Error retrieving locks");
    }
    if (ierrno == 0) {
        dbms_query_destroy(q);
        return 0;
    }

    *locks_present = dbms_get_int(q, 1);
    dbms_query_destroy(q);
    return NULL;
}

dav_error *dbms_add_indirect_locks(dav_lockdb *lockdb,
                                   dav_repos_resource *db_r,
                                   const dav_lock *lock)
{
    apr_pool_t *pool = lockdb->info->pool;
    dav_repos_db *d = lockdb->info->db;
    dav_repos_query *q = NULL;
    dav_error *err = NULL;
    char *values_str;
    const dav_lock *iter = lock;

    if(!iter) return err;

    values_str = apr_psprintf(pool, "(%ld,%ld)", iter->info->lock_id,
                              db_r->serialno);
    while ((iter=iter->next)) {
        char *new_str = apr_psprintf(pool, ",(%ld,%ld)", 
                                     iter->info->lock_id, db_r->serialno);
        values_str = apr_pstrcat(pool, values_str, new_str, NULL);
    }
    q = dbms_prepare
      (pool, d->db,
       apr_psprintf(pool, 
                    "INSERT INTO locks_resources(lock_id, resource_id) "
                    "VALUES %s", values_str));
    if (dbms_execute(q)) {
        db_error_message(pool, d->db, "dbms_execute error");
        err = dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0, 
                "Could not insert indirect locks.");
    }
    dbms_query_destroy(q);
    return err;
}

dav_error *dbms_add_indirect_locked_children(dav_lockdb *lockdb,
                                             dav_repos_resource *children,
                                             const dav_lock *lock)
{
    apr_pool_t *pool = lockdb->info->pool;
    dav_repos_db *d = lockdb->info->db;
    dav_repos_query *q = NULL;
    dav_error *err = NULL;
    char *values_str;

    values_str = apr_psprintf(pool, "(%ld,%ld)", lock->info->lock_id,
                              children->serialno);
    while ((children=children->next)) {
        char *new_str = apr_psprintf(pool, ",(%ld,%ld)", 
                                     lock->info->lock_id, children->serialno);
        values_str = apr_pstrcat(pool, values_str, new_str, NULL);
    }
    q = dbms_prepare
      (pool, d->db,
       apr_psprintf(pool, 
                    "INSERT INTO locks_resources(lock_id, resource_id) "
                    "VALUES %s", values_str));
    if (dbms_execute(q)) {
        db_error_message(pool, d->db, "dbms_execute error");
        err = dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0, 
                "Could not insert indirect locks.");
    }
    dbms_query_destroy(q);
    return err;
}

dav_error *dbms_insert_lock(dav_lockdb *lockdb,
                            dav_repos_resource *db_r,
                            const dav_lock *lock)
{
    apr_pool_t *pool = lockdb->info->pool;
    dav_repos_db *d = lockdb->info->db;
    dav_repos_query *q = NULL;
    const char *lockroot = lock->lockroot + strlen(db_r->root_path);

    TRACE();

    q = dbms_prepare
      (pool, d->db, 
       "INSERT INTO locks(uuid,resource_id,owner_id,form,"
       "                  depth, expires_at, owner_info, lockroot) "
       "VALUES(?,?,"
       "           (SELECT principals.resource_id FROM principals "
       "           WHERE principals.name=?),"
       "       ?,?,?,?,?)");
    dbms_set_string(q, 1, lock->locktoken->char_uuid);
    dbms_set_int(q, 2, db_r->serialno);
    dbms_set_string(q, 3, lock->auth_user);
    dbms_set_string(q, 4, lock->scope==DAV_LOCKSCOPE_EXCLUSIVE?"X":"S");
    dbms_set_int(q, 5, lock->depth);
    dbms_set_string(q, 6, time_ansi_to_datetime(pool, lock->timeout));
    dbms_set_string(q, 7, lock->owner?lock->owner:"");
    dbms_set_string(q, 8, lockroot);

    if (dbms_execute(q)) {
        dbms_query_destroy(q);
        return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "Couldn't insert direct locks");
    }
    lock->info->lock_id = dbms_insert_id(d->db, "locks", pool);
    dbms_query_destroy(q);

    return NULL;
}

dav_error *dbms_insert_binds_locks(dav_lockdb *lockdb, const dav_lock *lock,
                                   dbms_bind_list bind_list[], int size)
{
    apr_pool_t *pool = lockdb->info->pool;
    dav_repos_db *d = lockdb->info->db;
    dav_repos_query *q = NULL;
    const char *values = NULL, *query_str = NULL;
    int i = 0;
    long lock_id = lock->info->lock_id;
    dav_error *err = NULL;

    TRACE();

    if (size == 0) return NULL;

    values = apr_psprintf(pool, "(%ld, %ld)", lock_id, bind_list[0].bind_id);
    for (i=1; i<size; i++)
        values = apr_psprintf(pool, "%s, (%ld, %ld)", 
                              values, lock_id, bind_list[i].bind_id);

    query_str = apr_psprintf(pool, "INSERT INTO binds_locks(lock_id, bind_id) "
                             "VALUES %s", values);
    q = dbms_prepare(pool, d->db, query_str);

    if (dbms_execute(q)) {
        db_error_message(pool, d->db, "dbms_execute error");
        err = dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0, 
                "Could not insert indirect locks.");
    }
    dbms_query_destroy(q);
    return err;
}

dav_error *dbms_remove_lock(dav_lockdb *lockdb, 
                            const dav_locktoken *locktoken)
{
    apr_pool_t *pool = lockdb->info->pool;
    dav_repos_db *d = lockdb->info->db;
    dav_repos_query *q = NULL;
    dav_error *err = NULL;
    TRACE();

    q = dbms_prepare(pool, d->db,
                     "DELETE FROM locks WHERE locks.uuid=?");
    dbms_set_string(q, 1, locktoken->char_uuid);

    if (dbms_execute(q))
        err = dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                            "Couldn't delete lock");
    dbms_query_destroy(q);
    return err;
}

dav_error *dbms_remove_direct_locks_w_prefix(dav_lockdb *lockdb, 
                                             const char *lockroot_prefix)
{
    apr_pool_t *pool = lockdb->info->pool;
    dav_repos_db *d = lockdb->info->db;
    dav_repos_query *q = NULL;
    dav_error *err = NULL;
    TRACE();

    q = dbms_prepare(pool, d->db,
                     "DELETE FROM locks WHERE locks.lockroot LIKE ?");
    dbms_set_string(q, 1, apr_psprintf(pool, "%s%%", lockroot_prefix));

    if(dbms_execute(q))
        err = dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                            "Couldn't delete children");
    dbms_query_destroy(q);
    return err;
}

dav_error *dbms_remove_indirect_lock(dav_lockdb *lockdb,
                                     const dav_lock *lock,
                                     const dav_repos_resource *db_r)
{
    apr_pool_t *pool = lockdb->info->pool;
    dav_repos_db *d = lockdb->info->db;
    dav_repos_query *q = NULL;
    dav_error *err = NULL;

    TRACE();

    q = dbms_prepare(pool, d->db,
                     "DELETE FROM locks_resources "
                     "WHERE  lock_id=? AND resource_id=?");
    dbms_set_int(q, 1, lock->info->lock_id);
    dbms_set_int(q, 2, db_r->serialno);

    if(dbms_execute(q))
        err = dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                            "Couldn't delete children");
    dbms_query_destroy(q);
    return err;

}

dav_error *dbms_refresh_lock(dav_lockdb *lockdb,
                             dav_repos_resource *db_r,
                             dav_lock *lock, 
                             time_t new_time)
{
    apr_pool_t *pool = lockdb->info->pool;
    dav_repos_db *d = lockdb->info->db;
    dav_repos_query *q = NULL;
    char *where_str=NULL;
    dav_error *err = NULL;
    dav_lock *plock = NULL;

    where_str = apr_psprintf
      (pool, 
       "(id IN (SELECT lock_id FROM locks_resources WHERE resource_id=%ld)) "
       "AND uuid='%s' AND locks.resource_id=locks_resources.resource_id ",
       db_r->serialno, lock->locktoken->char_uuid);

    err = dbms_get_locks_by_where_str(lockdb, db_r, 1, &plock, where_str);
    if (err) return err;
    if (plock == NULL)
        return dav_new_error(pool, HTTP_PRECONDITION_FAILED, 0,
                             "The submitted locktoken to refresh doesn't "
                             "exist on the resource");

    memcpy(lock, plock, sizeof(*plock));

    q = dbms_prepare(pool, d->db, 
                     "UPDATE locks SET expires_at=? WHERE id=?");
    dbms_set_string(q, 1, time_ansi_to_datetime(pool, new_time));
    dbms_set_int(q, 2, lock->info->lock_id);
    if (dbms_execute(q))
        err = dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                            "Couldn't refresh lock");
    dbms_query_destroy(q);

    return err;
}

dav_error *dbms_get_locks_through_bind(dav_lockdb* lockdb,
                                       dav_repos_resource *coll,
                                       const dbms_bind_list *bind,
                                       dav_lock **p_locks)
{
    apr_pool_t *pool = lockdb->info->pool;
    dav_error *err = NULL;
    const char *where_str;
    dav_lock *lock;

    TRACE();

    where_str = apr_psprintf
      (pool, "locks.id IN (SELECT lock_id FROM binds_locks WHERE bind_id=%ld)"
       " AND locks.resource_id = locks_resources.resource_id",
       bind->bind_id);

    err = dbms_get_locks_by_where_str(lockdb, coll, 1, p_locks, where_str);
    if (err) return err;

    /* TODO: check if this step can be optimized by using an 
       auto-increment field in the binds_locks table */
    for (lock = *p_locks; lock; lock=lock->next) {
        const char *lockroot = lock->lockroot + strlen(coll->root_path);
        char *prefix;

        err = dbms_detect_bind(pool, lockdb->info->db, bind, lockroot, &prefix);
        if (err) return err;

        lock->post_bind_uri = 
          apr_psprintf(pool, "%s", lockroot+strlen(prefix));
    }

    return NULL;
}

dav_error *dbms_get_locks_not_directly_through_binds(dav_lockdb *lockdb,
                                                     dav_repos_resource *db_r,
                                                     dbms_bind_list *bind_list,
                                                     dav_lock **p_locks)
{
    apr_pool_t *pool = db_r->p;
    const char *where_str;
    const char *bind_ids = NULL;

    TRACE();

    bind_ids = apr_psprintf(pool, "%ld", bind_list->bind_id);
    while ((bind_list=bind_list->next)) {
        if (bind_list->bind_id > 0) {
            char *next_id = apr_psprintf(pool, "%ld", bind_list->bind_id);
            bind_ids = apr_pstrcat(pool, bind_ids, ",", next_id, NULL);
        }
    }

    where_str = apr_psprintf
      (db_r->p, "locks_resources.resource_id=%ld AND locks.id NOT IN "
       "(SELECT lock_id FROM binds_locks WHERE bind_id IN (%s))", db_r->serialno,
       bind_ids);

    return dbms_get_locks_by_where_str(lockdb, db_r, 1, p_locks, where_str);
}
