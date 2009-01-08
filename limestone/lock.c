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

#include "lock.h"
#include "util.h"
#include <apr_strings.h>
#include <http_request.h>

#include "dbms_locks.h"
#include "lock_bridge.h"
#include "bridge.h"
#include "dav_repos.h"  /* for dav_repos_get_db */
/*
 ** This must be forward-declared so the open_lockdb function can use it.
 */
extern const dav_hooks_locks dav_repos_hooks_locks;

/*
 ** dav_repos_get_supportedlock:  Returns a static string for all supportedlock
 **    properties. I think we save more returning a static string than
 **    constructing it every time, though it might look cleaner.
 */
static const char *dav_repos_get_supportedlock(const dav_resource *
					       resource)
{
    static const char supported[] = DEBUG_CR
	"<D:lockentry>" DEBUG_CR
	"<D:lockscope><D:exclusive/></D:lockscope>" DEBUG_CR
	"<D:locktype><D:write/></D:locktype>" DEBUG_CR
	"</D:lockentry>" DEBUG_CR
	"<D:lockentry>" DEBUG_CR
	"<D:lockscope><D:shared/></D:lockscope>" DEBUG_CR
	"<D:locktype><D:write/></D:locktype>" DEBUG_CR
	"</D:lockentry>" DEBUG_CR;

    TRACE();

    return supported;
}

dav_lock *dav_repos_alloc_lock(dav_lockdb *lockdb, dav_locktoken *lt)
{
    struct dav_lock_combined *comb;
    apr_pool_t *pool = lockdb->info->pool;

    TRACE();

    comb = apr_pcalloc(pool, sizeof(*comb));
    comb->pub.info = &comb->priv;

    if (lt) comb->pub.locktoken = lt;
    else {
        dav_locktoken *new_lt = apr_pcalloc(pool, sizeof(dav_locktoken));
        new_lt->char_uuid = get_new_plain_uuid(pool);
        comb->pub.locktoken = new_lt;
    }

    /* As of now, we support only write locks */
    comb->pub.type = DAV_LOCKTYPE_UNKNOWN;

    return &comb->pub;
}

static dav_error *dav_repos_parse_locktoken(apr_pool_t *p,
                                            const char *char_token,
                                            dav_locktoken **locktoken_p)
{
    dav_locktoken *new_locktoken;

    TRACE();

    new_locktoken = apr_pcalloc(p, sizeof(*new_locktoken));

    if (ap_strstr_c(char_token, "DAV:no-lock") == char_token) {
        new_locktoken->char_uuid = NULL; /*TODO: Verify that this is OK */
    } else if (ap_strstr_c(char_token, "opaquelocktoken:") == char_token) {
        new_locktoken->char_uuid = remove_hyphens_from_uuid(p, char_token+16);
    } else
        return dav_new_error(p, HTTP_BAD_REQUEST,
                             DAV_ERR_LOCK_UNK_STATE_TOKEN,
			     "The lock token uses an unknown State-token format"
                             " and could not be parsed.");

    *locktoken_p = new_locktoken;
    return NULL;
}

static const char *dav_repos_format_locktoken(apr_pool_t *p,
                                              const dav_locktoken *locktoken)
{
    char *formatted_uuid;
    TRACE();

    formatted_uuid = add_hyphens_to_uuid(p, locktoken->char_uuid);
    return apr_psprintf(p, "opaquelocktoken:%s", formatted_uuid);
}

static int dav_repos_compare_locktoken(const dav_locktoken *lt1,
                                       const dav_locktoken *lt2)
{
    TRACE();

    if (lt1 && lt2 && lt1->char_uuid && lt2->char_uuid)
        return strcmp(lt1->char_uuid, lt2->char_uuid);

    return 1;
}

static dav_error *dav_repos_open_lockdb(request_rec *r, int ro, int force,
                                        dav_lockdb **lockdb)
{
    struct dav_lockdb_combined *comb;

    TRACE();

    comb = apr_pcalloc(r->pool, sizeof(*comb));
    comb->pub.hooks = &dav_repos_hooks_locks;
    comb->pub.ro = ro;
    comb->pub.info = &comb->priv;

    comb->priv.r = r;
    comb->priv.pool = r->pool;
    comb->priv.db = dav_repos_get_db(r);
    if (comb->priv.db == NULL)
        return dav_new_error(r->pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "Couldn't initialzise connection to database");

    *lockdb = &comb->pub;
    return NULL;
}

static void dav_repos_close_lockdb(dav_lockdb *lockdb)
{
    TRACE();
}

static dav_error *dav_repos_remove_locknull_state(dav_lockdb *lockdb,
                                                  dav_resource *res)
{
    dav_repos_db *db = lockdb->info->db;
    apr_pool_t *pool = lockdb->info->pool;
    dav_repos_resource *db_r = res->info->db_r;
    int new_restype;
    request_rec *rec = res->info->rec;
    const dav_hooks_acl *acl_hooks = dav_get_acl_hooks(rec);
    const dav_principal *user = dav_principal_make_from_request(rec);
    dav_error *err = NULL;

    TRACE();

    if (res->type != DAV_RESOURCE_TYPE_REGULAR)
        return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "locknull can only become a resource or coll");
    if (!res->collection) { 
        if (!res->versioned)
            new_restype = dav_repos_RESOURCE;
        else new_restype = dav_repos_VERSIONED;
    } else {
        if (!res->versioned)
            new_restype = dav_repos_COLLECTION;
        else new_restype = dav_repos_VERSIONED_COLLECTION;
    }

    /* create initial ACL */
    if(acl_hooks) {
        err = (*acl_hooks->create_initial_acl)(user, res);
        if(err) return err;
    }

    return dbms_update_resource_type(db, db_r, new_restype);
}

static dav_error *dav_repos_create_lock(dav_lockdb *lockdb,
                                        dav_resource *resource,
                                        dav_lock **lock)
{
    dav_lock *new_lock;
    dav_repos_db *db = lockdb->info->db;
    dav_repos_resource *db_r = resource->info->db_r;
    request_rec *rec = resource->info->rec;
    const dav_hooks_acl *acl_hooks = dav_get_acl_hooks(rec);

    TRACE();
    
    if (db_r->serialno == 0) {
        if (use_locknull(rec)) {
            dav_error *err = NULL;
            db_r->resourcetype = dav_repos_LOCKNULL;
            /* locknull resources do not have their own aces,
             * so delay ACL creation */
            err = sabridge_insert_resource(db, db_r, rec, SABRIDGE_DELAY_ACL);
            if (err) return err;

            dav_repos_update_dbr_resource(db_r);

            if(acl_hooks) {
                /* inherit aces */
                err = (*acl_hooks->inherit_aces)(resource);
                if(err) return err;
            }
        } else {
            dav_resource *non_const_res = (dav_resource *)resource;
            non_const_res->collection = 0;
            dav_repos_create_resource(non_const_res, 0);
        }
    }

    new_lock = dav_repos_alloc_lock(lockdb, NULL);
    new_lock->info->res_id = resource->info->db_r->serialno;
    *lock = new_lock;
    return NULL;
}

static dav_error *dav_repos_get_locks(dav_lockdb *lockdb,
                                      const dav_resource *resource,
                                      int calltype,
                                      dav_lock **locks)
{
    TRACE();

    *locks = NULL;
    return dbms_get_locks(lockdb, resource->info->db_r, 
                          calltype==DAV_GETLOCKS_RESOLVED, locks);
}

static dav_error *dav_repos_find_lock(dav_lockdb *lockdb,
                                      const dav_resource *resource,
                                      const dav_locktoken *locktoken,
                                      int partial_ok,
                                      dav_lock **lock)
{
    TRACE();

    *lock = NULL;
    return dbms_find_lock_by_token(lockdb, resource->info->db_r,
                                   locktoken, lock);
}

static dav_error *dav_repos_has_locks(dav_lockdb *lockdb,
                                     const dav_resource *resource,
                                     int *locks_present)
{
    TRACE();
    
    return dbms_resource_has_locks(lockdb, resource->info->db_r, 
                                   locks_present);
}

static dav_error *dav_repos_append_locks(dav_lockdb *lockdb,
                                         const dav_resource *resource,
                                         int make_indirect,
                                         const dav_lock *lock)
{
    dav_repos_resource *db_r = resource->info->db_r;
    dav_repos_db *d = resource->info->db;
    dav_error *err = NULL;
    dbms_bind_list *bind_list;
    int level;
    const dav_lock *l_i = NULL;
    TRACE();


    if (!make_indirect) {
        err = dbms_insert_lock(lockdb, db_r, lock);
        dbms_get_path_binds(db_r->p, d, lock->lockroot+strlen(db_r->root_path),
                            &bind_list, &level);
        if (!err) err = dbms_insert_binds_locks(lockdb, lock, bind_list, 
                                                level);
    }

    for (l_i = lock; l_i && !err; l_i = l_i->next)
        err = dbms_remove_indirect_lock(lockdb, l_i, db_r);

    if (err) return err;
    return dbms_add_indirect_locks(lockdb, db_r, lock);
}

static dav_error *dav_repos_remove_lock(dav_lockdb *lockdb,
                                        const dav_resource *resource,
                                        const dav_locktoken *locktoken)
{
    dav_repos_resource *db_r;
    dav_error *err = NULL;
    TRACE();

    if (locktoken)
        return dbms_remove_lock(lockdb, locktoken);

    db_r = resource->info->db_r;

    /* locktoken is NULL for DELETE and MOVE methods. 
       Should remove directs of all children of this resource,
       and indirects of all children that are not reachable from
       the lockroot in another path. */

    if (resource && locktoken == NULL)
        err = dbms_remove_direct_locks_w_prefix
          (lockdb, db_r->uri+strlen(db_r->root_path));

    if (err) return err;


    return sabridge_remove_indirect_locks_d_inf(lockdb, db_r);
}

static dav_error *dav_repos_refresh_locks(dav_lockdb *lockdb,
                                          const dav_resource *resource,
                                          dav_lock *locks,
                                          time_t new_time)
{
    dav_repos_resource *db_r = resource->info->db_r;
    dav_repos_db *db = resource->info->db;
    dav_error *err = NULL;
    TRACE();

    if (new_time == 1) {
        dav_lock *lock;
        for (lock = locks; lock; lock = lock->next) {
            dav_repos_resource *lockroot_dbr = NULL;
            dbms_bind_list *bind_list;
            int level;

            sabridge_new_dbr_from_dbr(db_r, &lockroot_dbr);
            lockroot_dbr->uri = apr_pstrdup(db_r->p, lock->lockroot);
            err = sabridge_get_property(db, lockroot_dbr);
            if (err) return err;

            err = dbms_insert_lock(lockdb, lockroot_dbr, lock);

            err = dbms_get_path_binds(db_r->p, db, 
                                      lock->lockroot+strlen(db_r->root_path),
                                      &bind_list, &level);
            if (!err) err = dbms_insert_binds_locks(lockdb, lock, bind_list,
                                                    level);
            if (lock->depth != 0)
                sabridge_get_collection_children(db, lockroot_dbr, DAV_INFINITY,
                                                 NULL, NULL, NULL, NULL);
            err = dbms_add_indirect_locked_children(lockdb, lockroot_dbr, lock);
        }

    } else err = dbms_refresh_lock(lockdb, db_r, locks, new_time);

    return err;
}

static dav_error *dav_repos_lookup_resource(dav_lockdb *lockdb,
                                            const dav_locktoken *locktoken,
                                            const dav_resource *start_res,
                                            const dav_resource **resource)
{
    apr_pool_t *pool = lockdb->info->pool;
    dav_lock *lock;
    char *uri;
    const char *root_path = start_res->info->db_r->root_path;
    request_rec *rec;

    TRACE();

    dav_repos_find_lock(lockdb, start_res, locktoken, 0, &lock);

    if (lock == NULL)
        return dav_new_error(pool, HTTP_CONFLICT, 0, "Locktoken not legal");

    uri = apr_psprintf(pool, "%s%s", root_path,
                            lock->lockroot);
    rec = ap_sub_req_lookup_uri(uri, start_res->info->rec, NULL);

    dav_repos_hooks_repos.get_resource
      (rec, root_path, NULL, 0, (dav_resource **)resource);

    return NULL;
}

dav_error *dav_repos_get_bind_locks(dav_lockdb *lockdb,
                                    dav_bind *bind,
                                    dav_lock **p_locks)
{
    apr_pool_t *pool = lockdb->info->pool;
    dav_repos_resource *coll = bind->collection->info->db_r;
    dbms_bind_list *bind_pvt = bind->info;
    dav_error *err;

    TRACE();

    if (!bind_pvt){
        bind_pvt = apr_pcalloc(pool, sizeof(*bind_pvt));
        bind_pvt->parent_id = coll->serialno;
        bind_pvt->bind_name = bind->bind_name;

        err = dbms_get_bind(pool, lockdb->info->db, bind_pvt);
        if (err) return err;

        bind->info = bind_pvt;
    }

    return dbms_get_locks_through_bind(lockdb, coll, bind_pvt, p_locks);
}

dav_error *dav_repos_get_locks_not_through_binds(dav_lockdb *lockdb,
                                                 const dav_resource *resource,
                                                 const dav_bind *bind1,
                                                 const dav_bind *bind2,
                                                 dav_lock **p_locks)
{
    dav_repos_resource *db_r = resource->info->db_r;
    dav_repos_db *db = resource->info->db;
    dbms_bind_list *binds = NULL;
    dav_error *err = NULL;

    TRACE();

    if (bind1) {
        binds = apr_pcalloc(db_r->p, 2*sizeof(*binds));
        binds[0].parent_id = bind1->collection->info->db_r->serialno;
        binds[0].bind_name = bind1->bind_name;
        err = dbms_get_bind(db_r->p, db, binds);
        if (err) return err;
    }

    if (bind2) {
        binds[0].next = &binds[1];
        binds[1].parent_id = bind2->collection->info->db_r->serialno;
        binds[1].bind_name = bind2->bind_name;
        err = dbms_get_bind(db_r->p, db, &binds[1]);
        if (err) return err;
    }
    
    if (binds) {
        dav_lock *l_i = NULL, *l_i_prev = NULL;
        err = dbms_get_locks_not_directly_through_binds(lockdb, db_r, binds, p_locks);
        for (l_i = *p_locks; l_i; l_i = l_i->next) {
            if (l_i->info->res_id != db_r->serialno) {
                char *spath = NULL;
                dbms_find_shortest_path_excl_binds(db_r->p, db, l_i->info->res_id,
                                                   db_r->serialno, binds, &spath);
                if (!spath && l_i_prev)
                    l_i_prev->next = l_i->next;
                else if (!spath && !l_i_prev)
                    *p_locks = l_i->next;
                else l_i_prev = l_i;
            } else l_i_prev = l_i;
        }
    }
    else 
        err = dbms_get_locks(lockdb, db_r, 1, p_locks);
    return err;
}


const dav_hooks_locks dav_repos_hooks_locks = {
    dav_repos_get_supportedlock,
    dav_repos_parse_locktoken,
    dav_repos_format_locktoken,
    dav_repos_compare_locktoken,
    dav_repos_open_lockdb,
    dav_repos_close_lockdb,
    dav_repos_remove_locknull_state,
    dav_repos_create_lock,
    dav_repos_get_locks,
    dav_repos_find_lock,
    dav_repos_has_locks,
    dav_repos_append_locks,
    dav_repos_remove_lock,
    dav_repos_refresh_locks,
    dav_repos_lookup_resource,
    dav_repos_get_bind_locks,
    dav_repos_get_locks_not_through_binds,
    NULL		/* ctx */
};
