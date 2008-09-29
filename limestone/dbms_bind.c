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

#include "dbms_bind.h"
#include "dbms_api.h"
#include <apr_strings.h>
#include "util.h" /* for format_time */

dav_error *dbms_find_shortest_path_excl_binds(apr_pool_t *pool,
                                              const dav_repos_db *db,
                                              long from_res_id, long to_res_id,
                                              const dbms_bind_list *excl_binds,
                                              char **shortest_path)
{
    dav_repos_query *q = NULL;
    char *query_str;
    char *forefathers_str;
    char *new_forefathers=NULL;
    apr_hash_t *shortest_path_hash;
    long *hash_key_id;

    TRACE();

    *shortest_path = NULL;

    if (from_res_id == to_res_id) {
        *shortest_path = "";
        return NULL;
    }

    shortest_path_hash = apr_hash_make(pool);

    forefathers_str = apr_psprintf(pool, "%ld", to_res_id);
    hash_key_id = apr_pcalloc(pool, sizeof(long));
    *hash_key_id = to_res_id;
    apr_hash_set(shortest_path_hash, hash_key_id, sizeof(long), "");

    do {

        if(new_forefathers != NULL)
            forefathers_str = 
              apr_pstrcat(pool, forefathers_str, ",", new_forefathers, NULL);
        new_forefathers = NULL;
        
        query_str = apr_psprintf(pool, 
                                 "SELECT resource_id, collection_id, name "
                                 " FROM binds WHERE resource_id IN (%s) ",
                                 forefathers_str);
        if (excl_binds) {
            const char *bind_ids;
            bind_ids = apr_psprintf(pool, "%ld", excl_binds->bind_id);
            while ((excl_binds = excl_binds->next)) {
                if (excl_binds->bind_id > 0) {
                    char *next_id = apr_psprintf(pool, "%ld", excl_binds->bind_id);
                    bind_ids = apr_pstrcat(pool, bind_ids, ",", next_id, NULL);
                }
            }
            query_str = apr_pstrcat(pool, query_str, "AND id NOT IN (",
                                    bind_ids,") ", NULL);

        }

        q = dbms_prepare(pool, db->db, query_str);
        if (dbms_execute(q)) {
            dbms_query_destroy(q);
            return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0, 
                                 "Couldn't get all parent binds");
        }

        while(dbms_next(q) == 1) {
            long res_id = dbms_get_int(q, 1);
            long coll_id = dbms_get_int(q, 2);
            char *bind_name = dbms_get_string(q, 3);
            char *prev_sp, *parent_sp; /* rename prev_sp */
            char *curr_forefather;

            DBG3("ResId %ld, parId %ld, bindName %s\n", res_id, coll_id, bind_name);

            prev_sp = apr_hash_get(shortest_path_hash, &coll_id, sizeof(long));
            if(prev_sp)
                continue;

            curr_forefather = apr_psprintf(pool, "%ld", coll_id);
            
            if (new_forefathers) 
                new_forefathers = apr_pstrcat(pool, new_forefathers, ",",
                                              curr_forefather, NULL);
            else
                new_forefathers = curr_forefather;

            prev_sp = apr_hash_get(shortest_path_hash, &res_id, sizeof(long));
            parent_sp = apr_pstrcat(pool, "/", bind_name, prev_sp, NULL);

            if(coll_id == from_res_id) {
                *shortest_path = parent_sp;
                dbms_query_destroy(q);
                return NULL;
            }

            hash_key_id = apr_pcalloc(pool, sizeof(long));
            *hash_key_id = coll_id;
            apr_hash_set(shortest_path_hash, hash_key_id, sizeof(long),
                         parent_sp);
            
        }
        
        dbms_query_destroy(q);
    } while (new_forefathers);

    return NULL;
}

dav_error *dbms_find_shortest_path(apr_pool_t *pool, const dav_repos_db *db,
                                   long from_res_id, long to_res_id,
                                   char **shortest_path)
{
    return dbms_find_shortest_path_excl_binds(pool, db, from_res_id, to_res_id,
                                              NULL, shortest_path);
}

dav_error *dbms_delete_bind(apr_pool_t *pool, const dav_repos_db *d,
                            long coll_id, long res_id, const char *bind_name)
{
    dav_repos_query *q = NULL;

    TRACE();
    q = dbms_prepare(pool, d->db,
                     "DELETE FROM binds "
                     "WHERE collection_id=? AND resource_id =? AND name=?");
    dbms_set_int(q, 1, coll_id);
    dbms_set_int(q, 2, res_id);
    dbms_set_string(q, 3, bind_name);

    if (dbms_execute(q)) {
        dbms_query_destroy(q);
	return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0, 
                            "Could not update binds table in the database.");
    }

    dbms_query_destroy(q);
    return NULL;
}

/**
 * Create a bind with the given parameters, all sanity checks should have been 
 * performed and resources should exist
 *
 * @param pool to allocate memory
 * @param db DB connection struct containing the user, password and DB name
 * @param resource_id identifies the resource that needs to be bound
 * @param collection_id identifies the collection on which resource
 * needs to be bound
 * @param bindname The name to which the resource is bound
 *
 * @return NULL on success, error otherwise
 */
dav_error *dbms_insert_bind(apr_pool_t *pool, const dav_repos_db * db, 
                            int resource_id, 
                            int collection_id, const char* bindname)
{
    dav_repos_query *q = NULL;
    dav_error *err = NULL;
    
    TRACE();
    
    /* Bind the resource */
    if (bindname == NULL) {
        err = dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0, 
                "Unexpected : bindname is NULL");
    }

    q = dbms_prepare(pool, db->db,
                     "INSERT INTO binds(name, collection_id, "
                     "resource_id, updated_at) VALUES(?,?,?,?)");
    dbms_set_string(q, 1, bindname);
    dbms_set_int(q, 2, collection_id);
    dbms_set_int(q, 3, resource_id);
    dbms_set_string(q, 4, time_apr_to_str(pool, apr_time_now()));
    
    if (dbms_execute(q)) {
        db_error_message(pool, db->db, "dbms_execute error");
        err = dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0, 
                "Could not insert into binds table.");
    }

    dbms_query_destroy(q);
    return err;
}

dav_error *dbms_get_bind_resource_id(apr_pool_t *pool, const dav_repos_db *db,
                                     long coll_id, const char *bind_name, 
                                     long *res_id)
{
    dav_repos_query *q = NULL;
    int ierrno;

    TRACE();

    q = dbms_prepare(pool, db->db, 
                     "SELECT resource_id FROM binds "
                     "WHERE collection_id=? AND name=? ");
    dbms_set_int(q, 1, coll_id);
    dbms_set_string(q, 2, bind_name);

    if (dbms_execute(q)) {
        dbms_query_destroy(q);
        db_error_message(pool, db->db, "dbms_execute error");
        return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0, 
                             "Error querying database");
    }
    
    ierrno = dbms_next(q);

    if(ierrno <= 0)
        *res_id = 0;
    else
        *res_id = dbms_get_int(q, 1);

    dbms_query_destroy(q);

    return NULL;
}

/** 
 * Inserts a list of new binds in one query
 * 
 * @param pool to allocate memory
 * @param d handle to the database
 * @param bind_list list of new binds, either an array or a list chained by
                    the @dbms_bind_list.next element
 * @param array If this is 0, the @bind_list is treated as a chained list.
                Otherwise, @bind_list is considered an array with @array number
                of elements
 * 
 * @return NULL on success, error otherwise
 */
dav_error *dbms_insert_bind_list(apr_pool_t *pool, const dav_repos_db *d,
                                 dbms_bind_list *bind_list, int array)
{
    dav_repos_query *q = NULL;
    char *query_str;
    int bytes_used=0;
    int i = 0;
    dav_error *err = NULL;
    char *updated_at = apr_pcalloc(pool, APR_RFC822_DATE_LEN * sizeof(char));
    apr_time_t T = apr_time_now();

    TRACE();
    
    dav_repos_format_time(T, updated_at);

    if(array <= 0)
        return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "inserting a chain not supported yet");

    if (array)
        /* FIXME magic number. find optimal value and put in global variable */
        query_str = apr_pcalloc(pool, (300+150*array)*sizeof(char));

    if (array > 0) {
        bind_list[i].bind_name = dbms_escape(pool, d->db, bind_list[i].bind_name);
        bytes_used += sprintf(query_str+bytes_used, 
                              "INSERT INTO binds(collection_id,name,resource_id,"
                              "updated_at) VALUES (%ld, '%s', %ld, '%s')",
                              bind_list[i].parent_id, bind_list[i].bind_name,
                              bind_list[i].resource_id, updated_at);
    }


    for (i = 1; i < array; i++) {
        bind_list[i].bind_name = dbms_escape(pool, d->db, bind_list[i].bind_name);
        bytes_used += sprintf(query_str+bytes_used, ",(%ld, '%s', %ld, '%s')",
                              bind_list[i].parent_id, bind_list[i].bind_name,
                              bind_list[i].resource_id, updated_at);
    }

    q = dbms_prepare(pool, d->db, query_str);
    if (dbms_execute(q)) {
        err = dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0, 
                "Could not insert into binds table.");
    }
    dbms_query_destroy(q);
    return err;
}

dav_error *dbms_get_bind(apr_pool_t *pool, const dav_repos_db *db,
                         dbms_bind_list *bind)
{
    dav_repos_query *q = NULL;
    int ierrno;

    TRACE();

    q = dbms_prepare(pool, db->db, 
                     "SELECT id, resource_id, updated_at FROM binds "
                     "WHERE collection_id=? AND name=? ");
    dbms_set_int(q, 1, bind->parent_id);
    dbms_set_string(q, 2, bind->bind_name);

    if (dbms_execute(q)) {
        dbms_query_destroy(q);
        db_error_message(pool, db->db, "dbms_execute error");
        return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0, 
                             "Error querying database");
    }
    
    ierrno = dbms_next(q);

    if(ierrno < 0) {
        dbms_query_destroy(q);
        return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0, 
                             "Error querying database");
    }
    
    bind->bind_id = dbms_get_int(q, 1);
    bind->resource_id = dbms_get_int(q, 2);
    bind->updated_at = dbms_get_string(q, 3);

    dbms_query_destroy(q);

    return NULL;

}

dav_error *dbms_rebind_resource(apr_pool_t *pool, const dav_repos_db *d,
                                long src_parent_id, const char *src_bind_name,
                                long dst_parent_id, const char *dst_bind_name)
{
    dav_repos_query *q = NULL;
    
    TRACE();
    q = dbms_prepare(pool, d->db,
                     "UPDATE binds SET collection_id=?, name=?, updated_at=? "
                     "WHERE collection_id=? AND name=?");
    dbms_set_int(q, 1, dst_parent_id);
    dbms_set_string(q, 2, dst_bind_name);
    dbms_set_string(q, 3, time_apr_to_str(pool, apr_time_now()));
    dbms_set_int(q, 4, src_parent_id);
    dbms_set_string(q, 5, src_bind_name);
    
    if (dbms_execute(q)) {
        dbms_query_destroy(q);
	return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0, 
                             "Could update binds to rebind");
    }
    
    dbms_query_destroy(q);
    return NULL;
}

dav_error *dbms_get_path_binds(apr_pool_t *pool, const dav_repos_db *d,
                               const char *path,
                               dbms_bind_list **p_bindlist, int *p_size)
{
    int num_tokens, i;
    dbms_bind_list *bindlist;
    char *state;
    char *uri = apr_pstrdup(pool, path);

    TRACE();

    if (p_bindlist) *p_bindlist = NULL;
    if (p_size) *p_size = 0;

    num_tokens = get_num_tokens(apr_pstrdup(pool, uri));
    bindlist = apr_pcalloc(pool, sizeof(dbms_bind_list) * num_tokens);
    
    if (num_tokens == 0) return NULL;

    bindlist[0].parent_id = ROOT_COLLECTION_ID;
    bindlist[0].bind_name = apr_strtok(uri, "/", &state);
    dbms_get_bind(pool, d, &(bindlist[0]));
    for (i=1; i<num_tokens; i++) {
        bindlist[i].parent_id = bindlist[i-1].resource_id;
        bindlist[i].bind_name = apr_pstrdup(pool,apr_strtok(NULL, "/", &state));
        dbms_get_bind(pool, d, &bindlist[i]);
    }
    
    *p_bindlist = bindlist;
    *p_size = num_tokens;
    return NULL;
}

dav_error *dbms_detect_bind(apr_pool_t *pool, const dav_repos_db *db,
                            const dbms_bind_list *bind, const char *path, 
                            char **pprefix)
{
    char *state;
    char *uri = apr_pstrdup(pool, path);
    char *prefix="";
    dbms_bind_list next_bind;

    TRACE();

    next_bind.parent_id = ROOT_COLLECTION_ID;
    next_bind.bind_name = apr_strtok(uri, "/", &state);
    dbms_get_bind(pool, db, &next_bind);
    while(next_bind.bind_name) {
        prefix = apr_psprintf(pool, "%s/%s", prefix, next_bind.bind_name);
        if (next_bind.bind_id == bind->bind_id)
            break;

        next_bind.parent_id = next_bind.resource_id;
        next_bind.bind_name = apr_pstrdup(pool,apr_strtok(NULL, "/", &state));
        dbms_get_bind(pool, db, &next_bind);
    }
    
    *pprefix = prefix;
    return NULL;
}

dav_error *dbms_get_child_binds(const dav_repos_db *db,dav_repos_resource *db_r,
                                int exclude_self, dbms_bind_list **p_bindlist)
{
    apr_pool_t *pool = db_r->p;
    dav_repos_query *q = NULL;
    int ierrno;
    char *query;
    dbms_bind_list *bind_list_head = NULL, *bind = NULL;

    TRACE();

    if (exclude_self)
        query = "SELECT id, resource_id, updated_at FROM binds "
          "WHERE collection_id=? AND resource_id != collection_id";
    else
        query = "SELECT id, resource_id, updated_at FROM binds "
          "WHERE collection_id=? ";

    q = dbms_prepare(pool, db->db, query);
    dbms_set_int(q, 1, db_r->serialno);

    if (dbms_execute(q)) {
        dbms_query_destroy(q);
        db_error_message(pool, db->db, "dbms_execute error");
        return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0, 
                             "Error querying database");
    }
    
    while ((ierrno = dbms_next(q)) == 1){
        bind = apr_palloc(pool, sizeof(dbms_bind_list));
        bind->bind_id = dbms_get_int(q, 1);
        bind->resource_id = dbms_get_int(q, 2);
        bind->updated_at = dbms_get_string(q, 3);
        bind->next = bind_list_head;
        bind_list_head = bind;
    }

    if(ierrno < 0) {
        dbms_query_destroy(q);
        return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0, 
                             "Error querying database");
    }
    dbms_query_destroy(q);

    *p_bindlist = bind_list_head;

    return NULL;
}

dav_error *dbms_insert_cleanup_reqs(apr_pool_t *pool,
                                   const dav_repos_db *db,
                                   dbms_bind_list *bind)
{
    dav_repos_query *q = NULL;
    const char *val_str="", *q_str;
    dav_error *err = NULL;

    if (bind)
        val_str = apr_psprintf(pool, "SELECT %ld AS id ", bind->resource_id);
    else
        return NULL;

    while ((bind = bind->next) != NULL)
    /* TODO: find a better way to create a table from a list */
        val_str = apr_pstrcat
          (pool, val_str, 
           apr_psprintf(pool, " UNION SELECT %ld ", bind->resource_id), NULL );

    q_str = apr_pstrcat(pool, "INSERT INTO cleanup(resource_id) "
                        " SELECT id FROM (", val_str ,")t ", NULL);

    q = dbms_prepare(pool, db->db, q_str);

    if (dbms_execute(q)) {
        db_error_message(pool, db->db, "dbms_execute error");

        err = dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0, 
                            q_str);
    }

    dbms_query_destroy(q);
    return err;
}

dav_error *dbms_next_cleanup_req(apr_pool_t *pool, 
                                 const dav_repos_db *db, long *presource_id)
{
    dav_repos_query *q = NULL;
    long cleanup_id = 0;
    char *q_str = "SELECT id, resource_id FROM cleanup ORDER BY id LIMIT 1";

    TRACE();

    q = dbms_prepare(pool, db->db, q_str);
    if (dbms_execute(q)) {
        dbms_query_destroy(q);
        *presource_id = 0;
        return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "Couldn't get cleanup request");
    }

    if (dbms_next(q) < 1)
        *presource_id = 0;
    else {
        cleanup_id = dbms_get_int(q, 1);
        *presource_id = dbms_get_int(q, 2);
    }
    dbms_query_destroy(q);

    if (cleanup_id > 0) {
        q = dbms_prepare(pool, db->db, "DELETE FROM cleanup WHERE id=?");
        dbms_set_int(q, 1, cleanup_id);
        dbms_execute(q);
        dbms_query_destroy(q);
    }

    DBG1("Got cleanup request %ld", *presource_id);

    return NULL;
}
