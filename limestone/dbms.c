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

#include <httpd.h>
#include <http_log.h>
#include <http_core.h>      /* for ap_construct_url */
#include <mod_dav.h>
#include <stdlib.h>

#include <apr.h>
#include <apr_strings.h>
#include <apr_hash.h>

#include <stdio.h>
#include <time.h>

#include "dav_repos.h"
#include "lock.h"
#include "dbms.h"
#include "dbms_bind.h"          /* for inserting and removing binds */
#include "dbms_principal.h"     /* for inserting and removing binds */
#include "util.h"               /* for time_apr_to_str */
#include "bridge.h"             /* for sabridge_new_dbr_from_dbr */
#include "dbms_api.h"
#include "acl.h"                /* dav_repos_get_principal_url */

/** 
 * @struct resource_list
 * Structure for temporarily storing results of a SELECT query on resources 
 */
struct resource_list {
    long id;
    const char *name;
    struct resource_list *next;
};

typedef struct resource_list resource_list;

void db_error_message(apr_pool_t * pool, const dav_repos_dbms * db,
                      char *db_error_message_str)
{
    ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL,
                 "Error: %s ErrorMessage:%s",
                 db_error_message_str, dbms_error(pool, db));
}

dav_error *dbms_select_unique_id(apr_pool_t *pool, const dav_repos_db *d,
                                 const char *unique_id)
{
    dav_repos_query *q = NULL;
    dav_error *err = NULL;

    TRACE();

    q = dbms_prepare(pool, d->db, "SELECT ?");
    dbms_set_string(q, 1, apr_pstrcat(pool, "UNIQUE_ID ", unique_id, NULL));

    if (dbms_execute(q))
        err = dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0, 
                            "couldn't SELECT UNIQUE_ID");
    dbms_query_destroy(q);
    return err;
}

int dbms_opendb(dav_repos_db * d, apr_pool_t *p, request_rec * r,
                const char *db_driver, const char *db_params)
{
    TRACE();

    if (r) {
        const char *unique_id = apr_table_get(r->subprocess_env, "UNIQUE_ID");
        d->db = dbms_api_opendb(p, r);
        if (!d->db)
            ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL,
                         "dbms_opendb: Error acquiring a database connection");
            
        if (d->db && unique_id)
            dbms_select_unique_id(p, d, unique_id);
    } else
        d->db = dbms_api_opendb_params(p, db_driver, db_params);

    if (d->db == NULL)
        return -1;

    if (APR_SUCCESS != 
        dbms_set_session_xaction_iso_level(p, d, SERIALIZABLE)) {
        ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL,
                     "dbms_opendb: Error setting transaction isolation level");
        return -1;
    }

    return 0;
}

void dbms_closedb(dav_repos_db * d)
{
    TRACE();

    dbms_api_closedb(d->db);
}

dav_error *dbms_get_resource(const dav_repos_db *d, dav_repos_resource *r)
{
    int ierrno = 0;
    dav_repos_query *q = NULL;
    dav_error *err = NULL;

    TRACE();

    /* fetch live properties */
    q = dbms_prepare(r->p, d->db, 
                     "SELECT r.created_at, r.displayname, r.contentlanguage, "
                     "r.owner_id, r.comment, r.creator_id, r.type, r.uuid, "
                     "r.limebar_state "
                     "FROM resources r WHERE r.id = ?");
    dbms_set_int(q, 1, r->serialno);
    if (dbms_execute(q)) {
        dbms_query_destroy(q);
        return dav_new_error(r->p, HTTP_INTERNAL_SERVER_ERROR, 0, 
                "dbms_execute error");
    }

    if ((ierrno = dbms_next(q)) < 0) {
        dbms_query_destroy(q);
        return dav_new_error(r->p, HTTP_INTERNAL_SERVER_ERROR, 0,
                "dbms_next error");
    }

    if (ierrno == 0) {
        dbms_query_destroy(q);
        return err;
    }

    r->resourcetype = dav_repos_get_type_id(dbms_get_string(q, 7));
    r->created_at = dbms_get_string(q, 1);
    r->displayname = dbms_get_string(q, 2);
    r->getcontentlanguage = dbms_get_string(q, 3);
    r->owner_id = dbms_get_int(q, 4);
    r->comment = dbms_get_string(q, 5);
    r->creator_id = dbms_get_int(q, 6);

    DBG1("ResourceType: %d\n", r->resourcetype);
    
    r->next = NULL;
    r->uuid = dbms_get_string(q, 8);
    r->uuid[32] = '\0';
    r->limebar_state = dbms_get_string(q, 9);

    dbms_query_destroy(q);
    return err;
}

dav_error *dbms_insert_media(const dav_repos_db * d, dav_repos_resource * r)
{
    dav_repos_query *q = NULL;
    apr_pool_t *pool = r->p;
    dav_error *err = NULL;
    int ierrno;

    q = dbms_prepare(pool, d->db,
                     "INSERT INTO media (resource_id, size, mimetype, sha1, "
                     "                   updated_at) "
                     "VALUES(?, ?, ?, ?, ?)");
    
    dbms_set_int(q, 1, r->serialno);
    dbms_set_int(q, 2, r->getcontentlength);
    dbms_set_string(q, 3, r->getcontenttype);
    dbms_set_string(q, 4, r->sha1str);

    r->updated_at = time_apr_to_str(pool, apr_time_now());
    dbms_set_string(q, 5, r->updated_at);
    
    if((ierrno = dbms_execute(q)))
        err = dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                            "DBMS error while inserting into 'media'");
    dbms_query_destroy(q);

    return err;
}

dav_error *dbms_get_media_props(const dav_repos_db *d, dav_repos_resource *r)
{
    apr_pool_t *pool = r->p;
    dav_repos_query *q = NULL;
    int ierrno = 0;
    dav_error *err = NULL;
     
    /* get Content-Length and Content-Type */
    if (r->resourcetype == dav_repos_RESOURCE
        || r->resourcetype == dav_repos_VERSIONED
        || r->resourcetype == dav_repos_VERSION) {
        q = dbms_prepare(pool, d->db,
                         "SELECT size, mimetype, sha1, updated_at "
                         "FROM media WHERE resource_id = ?");
        dbms_set_int(q, 1, r->serialno);
        
        if (dbms_execute(q)) {
            dbms_query_destroy(q);
            return dav_new_error(r->p, HTTP_INTERNAL_SERVER_ERROR, 0, 
                                 "dbms_execute error");
        }

        if ((ierrno = dbms_next(q)) < 0) {
            dbms_query_destroy(q);
            return dav_new_error(r->p, HTTP_INTERNAL_SERVER_ERROR, 0,
                                 "dbms_next error");
        }
        if (ierrno == 0) {
            dbms_query_destroy(q);
            return err;
        }

        r->getcontentlength = dbms_get_int(q, 1);
        r->getcontenttype = dbms_get_string(q, 2);
        r->sha1str = dbms_get_string(q, 3);
        r->updated_at = dbms_get_string(q, 4);
        
        dbms_query_destroy(q);
    }
        
    return err;
}

dav_error *dbms_get_collection_props(const dav_repos_db *d,
                                     dav_repos_resource *r)
{
    apr_pool_t *pool = r->p;
    dav_repos_query *q = NULL;
    int ierrno = 0;
    dav_error *err = NULL;
     
    /* get Content-Length and Content-Type */
    q = dbms_prepare(pool, d->db,
                     "SELECT auto_version_new_children "
                     "FROM collections WHERE resource_id = ?");
    dbms_set_int(q, 1, r->serialno);

    if (dbms_execute(q)) {
        dbms_query_destroy(q);
        return dav_new_error(r->p, HTTP_INTERNAL_SERVER_ERROR, 0, 
                                 "dbms_execute error");
    }

    if ((ierrno = dbms_next(q)) < 0) {
        dbms_query_destroy(q);
        return dav_new_error(r->p, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "dbms_next error");
    }
    if (ierrno == 0) {
        dbms_query_destroy(q);
        return err;
    }

    r->av_new_children = dbms_get_int(q, 1);
        
    dbms_query_destroy(q);
    return err;
}

dav_error *dbms_set_collection_new_children_av_type(const dav_repos_db *d,
                                                    dav_repos_resource *r)
{
    apr_pool_t *pool = r->p;
    dav_repos_query *q = NULL;
    dav_error *err = NULL;
     
    q = dbms_prepare(pool, d->db,
                     "UPDATE collections SET auto_version_new_children = ? "
                     " WHERE resource_id = ?");
    dbms_set_int(q, 1, r->av_new_children);
    dbms_set_int(q, 2, r->serialno);
        
    if (dbms_execute(q))
        err = dav_new_error(r->p, HTTP_INTERNAL_SERVER_ERROR, 0, 
                            "dbms_execute error");
    dbms_query_destroy(q);
    return err;
}

dav_error *dbms_insert_resource(const dav_repos_db * d, dav_repos_resource * r)
{
    dav_repos_query *q = NULL;
    int isql_result = 0;
    apr_pool_t *pool = r->p;
    dav_error *err = NULL;

    TRACE();

    
    q = dbms_prepare(pool, d->db,
                     "INSERT INTO resources (uuid, created_at, owner_id, "
                     "creator_id, type, "
                     "displayname, contentlanguage, limebar_state ) "
                     "VALUES ( ?, ?, ?, ?, ?, ?, ?, ? ) ");
    dbms_set_string(q, 1, r->uuid);
    dbms_set_string(q, 2, r->created_at);
    //dbms_set_string(q, 3, r->updated_at);
    dbms_set_int(q, 3, r->owner_id);
    dbms_set_int(q, 4, r->creator_id);
    dbms_set_string(q, 5, dav_repos_resource_types[r->resourcetype]);
    dbms_set_string(q, 6, r->displayname);
    dbms_set_string(q, 7, r->getcontentlanguage ? r->getcontentlanguage
                    : "en-US");
    dbms_set_string(q, 8, r->limebar_state);
    
    isql_result = dbms_execute(q);
    dbms_query_destroy(q);
    if (isql_result)
        return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0, 
                             "DBMS error during insert to 'resources'");
    
    r->serialno = dbms_insert_id(d->db, "resources", pool);
    return err;
}

dav_error *dbms_insert_collection(const dav_repos_db *d, dav_repos_resource *r)
{
    dav_repos_query *q = NULL;
    apr_pool_t *pool = r->p;
    dav_error *err = NULL;

    TRACE();
    
    q = dbms_prepare(pool, d->db,
                     "INSERT INTO collections (resource_id, auto_version_new_children) "
                     "VALUES ( ?, ? ) ");
    dbms_set_int(q, 1, r->serialno);
    dbms_set_int(q, 2, r->av_new_children);

    if (dbms_execute(q))
        err = dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0, 
                            "DBMS error during insert to 'collections'");

    dbms_query_destroy(q);
    return err;
}

dav_error *dbms_update_displayname(const dav_repos_db * d,
                                   const dav_repos_resource * r)
{
    dav_repos_query *q = NULL;
    apr_pool_t *pool = r->p;
    dav_error *err = NULL;

    TRACE();
    q = dbms_prepare(pool, d->db,
                     "UPDATE resources "
                     "SET displayname = ? "
                     "WHERE id = ?");
    dbms_set_string(q, 1, r->displayname);
    dbms_set_int(q, 2, r->serialno);

    if (dbms_execute(q))
        err = dav_new_error(pool,
                            HTTP_INTERNAL_SERVER_ERROR, 0,
                            "Couldn't set live props");
    dbms_query_destroy(q);
    return err;
}               /*End of dbms_set_property */

dav_error *dbms_set_property(const dav_repos_db * d,
                             const dav_repos_resource * r)
{
    dav_repos_query *q = NULL;
    apr_pool_t *pool = r->p;
    dav_error *err = NULL;

    TRACE();
    q = dbms_prepare(pool, d->db,
                     "UPDATE resources "
                     "SET created_at = ?, displayname = ?, uuid = ?, "
                     "contentlanguage = ?, type = ?,    creator_id = ?, "
                     "owner_id = ? WHERE id = ?");
    dbms_set_string(q, 1, r->created_at);
    dbms_set_string(q, 2, r->displayname);
    dbms_set_string(q, 3, r->uuid);
    dbms_set_string(q, 4, r->getcontentlanguage ? r->getcontentlanguage
                    : "en-US");
    dbms_set_string(q, 5, dav_repos_resource_types[r->resourcetype]);
    dbms_set_int(q, 6, r->creator_id);
    dbms_set_int(q, 7, r->owner_id);
    dbms_set_int(q, 8, r->serialno);

    if (dbms_execute(q))
        err = dav_new_error(pool,
                            HTTP_INTERNAL_SERVER_ERROR, 0,
                            "Couldn't set live props");
    dbms_query_destroy(q);
    return err;
}               /*End of dbms_set_property */

dav_error *dbms_set_limebar_state(const dav_repos_db *d,
                                  const dav_repos_resource *r)
{
    dav_repos_query *q = NULL;
    apr_pool_t *pool = r->p;
    dav_error *err = NULL;

    TRACE();
    q = dbms_prepare(pool, d->db,
                     "UPDATE resources "
                     "SET limebar_state = ? WHERE id = ?");
    dbms_set_string(q, 1, r->limebar_state);
    dbms_set_int(q, 2, r->serialno);

    if (dbms_execute(q))
        err = dav_new_error(pool,
                            HTTP_INTERNAL_SERVER_ERROR, 0,
                            "Couldn't set limebar_state props");
    dbms_query_destroy(q);
    return err;
}

dav_error *dbms_update_media_props(const dav_repos_db *d,
                                   dav_repos_resource *db_r)
{
    dav_repos_query *q = NULL;
    apr_pool_t *pool = db_r->p;
    dav_error *err = NULL;

    TRACE();

    q = dbms_prepare(pool, d->db,
                     "UPDATE media "
                     "SET size = ?, mimetype = ?, sha1 = ?, updated_at = ? "
                     "WHERE resource_id = ?");
    dbms_set_int(q, 1, db_r->getcontentlength);
    dbms_set_string(q, 2, db_r->getcontenttype);
    dbms_set_string(q, 3, db_r->sha1str);
    db_r->updated_at = time_apr_to_str(pool, apr_time_now());
    dbms_set_string(q, 4, db_r->updated_at);
    dbms_set_int(q, 5, db_r->serialno);

    if (dbms_execute(q))
        err = dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                            "Couldn't update media entry");
    dbms_query_destroy(q);
    return err;
}

dav_error *dbms_update_resource_type(const dav_repos_db *db,
                                     dav_repos_resource *db_r,
                                     int new_resource_type)
{
    apr_pool_t *pool = db_r->p;
    dav_repos_query *q = NULL;

    TRACE();

    q = dbms_prepare(pool, db->db,
                     "UPDATE resources SET type = ? WHERE id = ?");
    dbms_set_string(q, 1, dav_repos_resource_types[new_resource_type]);
    dbms_set_int(q, 2, db_r->serialno);
    if (dbms_execute(q)) {
        dbms_query_destroy(q);
        db_error_message(pool, db->db, "dbms_execute error: could not updated"
                         " resource_type");
        return dav_new_error(pool,
                             HTTP_INTERNAL_SERVER_ERROR, 0,
                             "DBMS Error");
    }
    dbms_query_destroy(q);

    db_r->resourcetype = new_resource_type;

    return NULL;
}

dav_error *dbms_delete_resource(const dav_repos_db * d, apr_pool_t *pool,
                                dav_repos_resource *db_r)
{
    dav_repos_query *q = NULL;

    TRACE();

    /* update quota */
    /* NOTE: ON DELETE triggers(mysql)/rules(pgsql) set on the media table 
     * will not be triggered by a cascading delete on the resources table,
     * hence we have to update the quota ourselves.
     * This can change in the future, once databases start handling cascading 
     * deletes & triggers/rules correctly. */
    if(db_r->resourcetype == dav_repos_RESOURCE
       || db_r->resourcetype == dav_repos_VERSIONED
       || db_r->resourcetype == dav_repos_VERSION) {
        q = dbms_prepare(pool, d->db, "UPDATE quota"
                         " SET used_quota = used_quota"
                         " - (SELECT size FROM media WHERE resource_id = ?)"
                         " WHERE principal_id"
                         " = (SELECT owner_id FROM resources WHERE id = ?)");
        dbms_set_int(q, 1, db_r->serialno);
        dbms_set_int(q, 2, db_r->serialno);

        if (dbms_execute(q)) {
            dbms_query_destroy(q);
            return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                                 "DBMS error in deleting resource");
        }

        dbms_query_destroy(q);
    }

    q = dbms_prepare(pool, d->db, 
                     "DELETE FROM resources WHERE id=?");
    dbms_set_int(q, 1, db_r->serialno);

    if (dbms_execute(q)) {
        dbms_query_destroy(q);
        return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "DBMS error in deleting resource");
    }

    dbms_query_destroy(q);

    return NULL;
}

dav_error *db_insert_property(const dav_repos_db * d,
                              const dav_repos_resource * r,
                              const dav_repos_property * pr)
{       
    apr_pool_t *pool = r->p;
    dav_repos_query *q = NULL;
    dav_error *err = NULL;

    TRACE();

    /*Create the insert command */
    q = dbms_prepare(pool, d->db, "INSERT INTO properties "
                     "(resource_id, namespace_id, name, xmlinfo, value) "
                     "VALUES(?, ?, ?, ?, ?)");
    dbms_set_int(q, 1, r->serialno);
    dbms_set_int(q, 2, pr->ns_id);
    dbms_set_string(q, 3, pr->name);
    dbms_set_string(q, 4, pr->xmlinfo);
    dbms_set_string(q, 5, pr->value);

    if (dbms_execute(q)) 
        err = dav_new_error(r->p, HTTP_INTERNAL_SERVER_ERROR, 0, 
                            "Could not insert dead prop");
    dbms_query_destroy(q);

    return err;
}               /*End of db_insert_property */

dav_error *dbms_set_dead_property(const dav_repos_db * d,
                                  const dav_repos_resource * r,
                                  const dav_repos_property * pr)
{
    int ierrno = 0;
    apr_pool_t *pool = r->p;
    dav_repos_query *q = NULL;
    dav_error *err = NULL;

    TRACE();

    /*Create the select command to get the serialno by serialno */
    q = dbms_prepare(pool, d->db,
                     "SELECT id FROM properties "
                     "WHERE resource_id = ? AND name = ? "
                     "AND namespace_id = ?");
    dbms_set_int(q, 1, r->serialno);
    dbms_set_string(q, 2, pr->name);
    dbms_set_int(q, 3, pr->ns_id);

    if (dbms_execute(q)) {
        dbms_query_destroy(q);
        return dav_new_error(r->p, HTTP_INTERNAL_SERVER_ERROR, 0, 
                             "dbms_execute error");
    }

    ierrno = dbms_next(q);
    dbms_query_destroy(q);
    if (ierrno == 0) {
        /*Insert a new property record */
        return db_insert_property(d, r, pr);
    } else if (ierrno == -1) {
        return dav_new_error(r->p, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "Failed when fetching dead props");
    }

    /*Create update command */
    q = dbms_prepare(pool, d->db, 
                     "UPDATE properties SET xmlinfo = ?, value = ? "
                     "WHERE resource_id = ? AND name = ? AND namespace_id = ?");
    dbms_set_string(q, 1, pr->xmlinfo);
    dbms_set_string(q, 2, pr->value);
    dbms_set_int(q, 3, r->serialno);
    dbms_set_string(q, 4, pr->name);
    dbms_set_int(q, 5, pr->ns_id);

    /*Execute the update command */
    if (dbms_execute(q)) {
        dbms_query_destroy(q);
        return dav_new_error(r->p, HTTP_INTERNAL_SERVER_ERROR, 0, 
                             "dbms_execute error");
    }
    dbms_query_destroy(q);

    return err;
}               /*End of dbms_set_dead_property */

dav_error *dbms_fill_dead_property(const dav_repos_db * d,
                                   dav_repos_resource * db_r)
{
    struct dav_repos_property *pproperty_link_tail = NULL;
    struct dav_repos_property *pnew_link_item = NULL;
    apr_pool_t *pool = db_r->p;
    dav_repos_query *q = NULL;
    dav_error *err = NULL;

    TRACE();

    if (db_r->ns_id_hash == NULL)
        db_r->ns_id_hash = apr_hash_make(pool);

    if (db_r->pr) {
        /* nothing to do, properties already filled */
        return NULL;
    }

    /*Create the search command */
    q = dbms_prepare(pool, d->db,
            "SELECT resource_id, namespace_id, properties.name, "
            "xmlinfo, value, namespaces.name "
            "FROM properties "
            "INNER JOIN namespaces "
            "ON namespace_id=namespaces.id "
            "WHERE resource_id=? ORDER BY properties.name");
    dbms_set_int(q, 1, db_r->serialno);

    if (dbms_execute(q)) {
        dbms_query_destroy(q);
        return dav_new_error(db_r->p, HTTP_INTERNAL_SERVER_ERROR, 0, 
                "dbms_execute error");
    }

    while (dbms_next(q)) {
        /*fetch every property for a resource */
        /*Malloc a new property structure */
        pnew_link_item = apr_pcalloc(db_r->p, sizeof(*pnew_link_item));

        if (db_r->pr == NULL) {
            /*It is the first property record */
            db_r->pr = pnew_link_item;
            pproperty_link_tail = pnew_link_item;
        } else {
            /*It is not the first property record */
            if (pproperty_link_tail)
                pproperty_link_tail->next = pnew_link_item;
            pproperty_link_tail = pnew_link_item;
        }

        /*Get the data of every column of this row to a link */

        pproperty_link_tail->serialno = dbms_get_int(q, 1);
        pproperty_link_tail->ns_id = dbms_get_int(q, 2);
        pproperty_link_tail->name = dbms_get_string(q, 3);
        pproperty_link_tail->xmlinfo = dbms_get_string(q, 4);
        pproperty_link_tail->value = dbms_get_string(q, 5);
        pproperty_link_tail->namespace_name = dbms_get_string(q, 6);

        apr_hash_set(db_r->ns_id_hash, pproperty_link_tail->namespace_name,
                APR_HASH_KEY_STRING, &pproperty_link_tail->ns_id);

        pproperty_link_tail->next = NULL;
    }           

    dbms_query_destroy(q);

    return err;
}

dav_error *dbms_get_collection_resource(const dav_repos_db *d,
                                        dav_repos_resource *db_r,
                                        dav_repos_resource *db_r_tail,
                                        const char* acl_priv,
                                        dav_repos_resource **plink_head,
                                        dav_repos_resource **plink_tail,
                                        int *num_items)
{
    apr_pool_t *pool = db_r->p;
    char **dbrow;
    struct dav_repos_resource *presult_link_tail = NULL;
    struct dav_repos_resource *pnew_link_item = NULL;
    dav_repos_resource *dummy_head = NULL;
    dav_repos_query *q = NULL;
    int num_children = 0, results_count = 0;
    request_rec *r = db_r->resource->info->rec;
    const dav_hooks_acl *acl_hooks = dav_get_acl_hooks(r);
    const char *query_str;
    const char *col_id_str, *col_ids_str = NULL;
    apr_hash_t *id_uri_hash;
    dav_repos_resource *iter;
    long principal_id;

    TRACE();

    dav_principal *principal = dav_principal_make_from_request(r);
    principal_id = dav_repos_get_principal_id(principal);

    id_uri_hash = apr_hash_make(pool);
    iter = db_r;
    do {
        if (iter->bind) continue;
        if (iter->resourcetype != dav_repos_COLLECTION &&
            iter->resourcetype != dav_repos_VERSIONED_COLLECTION)
            continue;
        apr_hash_set(id_uri_hash, &(iter->serialno), sizeof(long), iter->uri);
        col_id_str = apr_psprintf(pool, "%ld", iter->serialno);
        if (col_ids_str == NULL) col_ids_str = col_id_str;
        else 
            col_ids_str = apr_pstrcat(pool, col_ids_str, ",", col_id_str, NULL);

    } while (iter != db_r_tail && NULL != (iter = iter->next));

    if(acl_hooks && acl_priv) {
        /*Create the search command */
        query_str = apr_psprintf
          (pool,
           "SELECT resources.id, created_at, child_binds.name,"
           " child_binds.updated_at, contentlanguage, owner_id, comment,"
           " creator_id, type, size, mimetype, sha1, vcrs.checked_state,"
           " checked_version.number, uuid, vcrs.vhr_id, versions.vcr_id,"
           " vr_vcr.checked_id, vcrs.version_type, principals.name,"
           " versions.number, child_binds.id, vcrs.checkin_on_unlock,"
           " child_binds.collection_id, displayname, limebar_state, "
           " ("
           "SELECT aces.grantdeny"
           " FROM aces"
           " INNER JOIN dav_aces_privileges ap "
           "ON aces.id = ap.ace_id"
           " INNER JOIN ("
           "(SELECT transitive_group_id AS group_id"
           " FROM transitive_group_members"
           " WHERE transitive_member_id=%ld) "
           "UNION   (SELECT %ld)"
           ") membership "
           "ON aces.principal_id = membership.group_id"
           " INNER JOIN ("
           "SELECT par_priv.id AS par_priv_id"
           " FROM acl_privileges par_priv"
           " INNER JOIN acl_privileges chi_priv "
           "ON par_priv.lft <= chi_priv.lft"
           " AND par_priv.rgt >= chi_priv.rgt"
           " WHERE chi_priv.name = '%s' "
           ") privs "
           "ON ap.privilege_id = privs.par_priv_id"
           " INNER JOIN ("

           "SELECT par_res.resource_id AS resource_id,"
           " par_res.path AS par_res_path,"
           " chi_res.resource_id AS resource_tag"
           " FROM acl_inheritance AS par_res"
           " INNER JOIN acl_inheritance AS chi_res"
           " ON par_res.resource_id = "
           "   ANY(CAST(string_to_array(chi_res.path, ',') "
           "     AS INTEGER[])) "
           ") inherited_res "

           "ON aces.resource_id = inherited_res.resource_id"
           " WHERE inherited_res.resource_tag = child_binds.resource_id "
           "ORDER BY CHAR_LENGTH(par_res_path) DESC, protected DESC, id "
           "LIMIT 1"
           ") AS grantdeny"
           " FROM binds child_binds"
           " LEFT JOIN resources ON resources.id = child_binds.resource_id"
           " LEFT JOIN media ON media.resource_id = child_binds.resource_id"
           " LEFT JOIN principals "
           "ON resources.creator_id = principals.resource_id"
           " LEFT JOIN vcrs ON resources.id = vcrs.resource_id"
           " LEFT JOIN versions AS checked_version "
           "ON checked_version.resource_id = vcrs.checked_id"
           " LEFT JOIN versions ON versions.resource_id = resources.id"
           " LEFT JOIN vcrs AS vr_vcr "
           "ON vr_vcr.checked_id = versions.resource_id"
           " WHERE child_binds.collection_id IN (%s)",
           principal_id, principal_id, acl_priv, col_ids_str);
    } else {
        /*Create the search command */
        query_str = apr_psprintf
          (pool, 
           "SELECT id, "
           "       created_at, children.name, children.updated_at, "
           "       contentlanguage, owner_id, comment, "
           "       creator_id, type, size, "
           "       mimetype, sha1, vcrs.checked_state, "
           "       checked_version.number, uuid, vcrs.vhr_id, "
           "       versions.vcr_id, vr_vcr.checked_id, vcrs.version_type, "
           "       principals.name, versions.number, children.bind_id, "
           "       vcrs.checkin_on_unlock, children.parent_id, displayname, limebar_state "
           "FROM resources "
           "      INNER JOIN "
           "           ( SELECT binds.id as bind_id, resource_id, name, "
           "                    updated_at, binds.collection_id as parent_id"
           "              FROM binds "
           "              WHERE collection_id IN (%s)) "
           "          children "
           "          ON children.resource_id = resources.id "
           "      LEFT JOIN media ON resources.id = media.resource_id "
           "      LEFT JOIN principals "
           "          ON resources.creator_id = principals.resource_id "
           "      LEFT JOIN vcrs ON resources.id = vcrs.resource_id "
           "      LEFT JOIN versions AS checked_version "
           "          ON checked_version.resource_id = vcrs.checked_id "
           "      LEFT JOIN versions ON versions.resource_id = resources.id "
           "      LEFT JOIN vcrs AS vr_vcr "
           "          ON vr_vcr.checked_id = versions.resource_id",
           col_ids_str);
    }
    q = dbms_prepare(pool, d->db, query_str);
    if (dbms_execute(q)) {
        db_error_message(db_r->p, d->db, "dbms_execute error");
        dbms_query_destroy(q);
        return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "Can't get collection resource");
    }

    dummy_head = apr_pcalloc(db_r->p, sizeof(*dummy_head));
    presult_link_tail = dummy_head;
    presult_link_tail->next = NULL;
    presult_link_tail->pr = NULL;
    
    results_count = dbms_results_count(q);
    while (num_children < results_count) {
        const char *parent_uri;
        
        dbrow = dbms_fetch_row_num(d->db, q, pool, num_children);
        num_children++;

        if (acl_hooks && acl_priv && dbrow[26][0] != 'G')
            continue;

        /*fetch every row */
        /*Malloc a new structure */
        pnew_link_item = NULL;
        sabridge_new_dbr_from_dbr(db_r, &pnew_link_item);
        presult_link_tail->next = pnew_link_item;
        presult_link_tail = pnew_link_item;

        /*Get the data of every column of this row to a link */

        presult_link_tail->parent_id = atol(dbrow[23]);
        parent_uri = apr_hash_get(id_uri_hash, &presult_link_tail->parent_id,
                                  sizeof(presult_link_tail->parent_id));
        if (parent_uri && strcmp(parent_uri, "/")){
            presult_link_tail->uri = 
                apr_psprintf(db_r->p, "%s/%s", parent_uri, dbrow[2]);
        }
        else {
            presult_link_tail->uri = apr_psprintf(db_r->p, "/%s", dbrow[2]);
        }
        
        presult_link_tail->bind_id = atoi(dbrow[21]);

        presult_link_tail->serialno = atol(dbrow[0]);

        presult_link_tail->created_at =
          (dbrow[1] == NULL) ? NULL : apr_pstrdup(db_r->p, dbrow[1]);

        presult_link_tail->displayname =
          (dbrow[24] == NULL) ? "" : apr_pstrdup(db_r->p, dbrow[24]);

        presult_link_tail->updated_at =
          (dbrow[3] == NULL) ? NULL : apr_pstrdup(db_r->p, dbrow[3]);

        presult_link_tail->getcontentlanguage =
          (dbrow[4] == NULL) ? NULL : apr_pstrdup(db_r->p, dbrow[4]);

        presult_link_tail->owner_id = atoi(dbrow[5]);

        presult_link_tail->comment =
          (dbrow[6] == NULL) ? NULL : apr_pstrdup(db_r->p, dbrow[6]);

        presult_link_tail->creator_id = atoi(dbrow[7]);

        presult_link_tail->resourcetype = dav_repos_get_type_id(dbrow[8]);

        if (presult_link_tail->resourcetype == dav_repos_COLLECTION ||
            presult_link_tail->resourcetype == dav_repos_VERSIONED_COLLECTION) {
            presult_link_tail->getcontenttype = apr_pstrdup(db_r->p, DIR_MAGIC_TYPE);
        }
        if (presult_link_tail->resourcetype == dav_repos_RESOURCE ||
            presult_link_tail->resourcetype == dav_repos_VERSION ||
            presult_link_tail->resourcetype == dav_repos_VERSIONED) {

            presult_link_tail->getcontentlength = atoi(dbrow[9]);
            presult_link_tail->getcontenttype =
              (dbrow[10] == NULL) ? NULL : apr_pstrdup(db_r->p,
                                                       dbrow[10]);
            presult_link_tail->sha1str =
              (dbrow[11] == NULL) ? NULL : apr_pstrdup(db_r->p,
                                                       dbrow[11]);
        }


        if (presult_link_tail->resourcetype == dav_repos_VERSIONED || 
            presult_link_tail->resourcetype == dav_repos_VERSIONED_COLLECTION){
            char *checked_state = apr_pstrdup(db_r->p, dbrow[12]);
            int checked_version = atoi(dbrow[13]);
            /** TODO: Use a single flag for checkin & checkout */
            switch (checked_state[0]) {
            case 'I':
                presult_link_tail->checked_state = DAV_RESOURCE_CHECKED_IN;
                presult_link_tail->vr_num = checked_version;
                break;
            case 'O':
                presult_link_tail->checked_state = DAV_RESOURCE_CHECKED_OUT;
                presult_link_tail->vr_num = checked_version;
                break;
            }
            presult_link_tail->vhr_id = atoi(dbrow[15]);
            presult_link_tail->autoversion_type = atoi(dbrow[18]);
            presult_link_tail->checkin_on_unlock = atoi(dbrow[22]);
        }
    
        if (presult_link_tail->resourcetype == dav_repos_VERSION ||
            presult_link_tail->resourcetype == dav_repos_COLLECTION_VERSION) {
            int checked_id;
            presult_link_tail->vcr_id = atoi(dbrow[16]);
            if (dbrow[17]) {
                checked_id = atoi(dbrow[17]);
                presult_link_tail->version = atol(dbrow[20]);
                if (presult_link_tail->serialno == checked_id)
                    presult_link_tail->lastversion = 1;
            }
        }

        presult_link_tail->uuid = apr_pstrdup(db_r->p, dbrow[14]);
        presult_link_tail->creator_displayname =
          apr_pstrdup(db_r->p, dbrow[19]);

        presult_link_tail->limebar_state = apr_pstrdup(db_r->p, dbrow[25]);

        presult_link_tail->p = pool;

        presult_link_tail->next = NULL;
        presult_link_tail->pr = NULL;
    }               /*End of while (( dbrow = ... )) */

    dbms_query_destroy(q);

    if (plink_head)
        *plink_head = dummy_head->next;
    if (plink_tail)
        *plink_tail = dummy_head->next?presult_link_tail:NULL;
    if (num_items)
        *num_items = num_children;

    return NULL;
}               /*End of dbms_get_collection_resource */

dav_error *dbms_copy_media_props(const dav_repos_db *d,
                                 const dav_repos_resource *r_src,
                                 dav_repos_resource *r_dest)
{
    apr_pool_t *pool = r_src->p;
    dav_repos_query *q = NULL;

    TRACE();

    /* Copying contents of a row to another row of same table can be done 
     * easily using UPDATE, but, UPDATE has incompatible syntax across 
     * pgsql & mysql */
    q = dbms_prepare(pool, d->db, "DELETE FROM media WHERE resource_id = ?");
    dbms_set_int(q, 1, r_dest->serialno);
    dbms_execute(q);
    dbms_query_destroy(q);

    q = dbms_prepare(pool, d->db, "INSERT INTO media (resource_id, size, "
                     "mimetype, sha1, "
                     "updated_at) "
                     "SELECT ?, size, mimetype, sha1, ? "
                     "FROM media WHERE resource_id = ?");

    r_dest->updated_at = time_apr_to_str(pool, apr_time_now());
    dbms_set_int(q, 1, r_dest->serialno);
    dbms_set_string(q, 2, r_dest->updated_at);
    dbms_set_int(q, 3, r_src->serialno);

    if (dbms_execute(q)) {
        dbms_query_destroy(q);
        return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "DBMS Error in copying 'media'");
    }
    dbms_query_destroy(q);

    r_dest->getcontentlength = r_src->getcontentlength;
    r_dest->getcontenttype = r_src->getcontenttype;
    r_dest->sha1str = r_src->sha1str;

    return NULL;
}

dav_error *dbms_delete_dead_props(apr_pool_t *pool, const dav_repos_db *d,
                                  long res_id)
{
    dav_repos_query *q = NULL;

    TRACE();
    
    q = dbms_prepare(pool, d->db,
                     "DELETE FROM properties "
                     "WHERE resource_id=?");
    dbms_set_int(q, 1, res_id);

    if (dbms_execute(q)) {
        dbms_query_destroy(q);
        return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "DBMS Error in deleting dead properties");
    }
    dbms_query_destroy(q);

    return NULL;
}

dav_error *dbms_copy_dead_props(apr_pool_t *pool, const dav_repos_db *d,
                                long src_id, long dest_id)
{
    dav_repos_query *q = NULL;

    TRACE();

    q = dbms_prepare(pool, d->db,
                     "INSERT INTO"
                     " properties(namespace_id, name, resource_id, xmlinfo, value)"
                     " SELECT namespace_id, name, ?, xmlinfo, value "
                     "FROM properties WHERE resource_id = ?");
    dbms_set_int(q, 1, dest_id);
    dbms_set_int(q, 2, src_id);

    if (dbms_execute(q)) {
        dbms_query_destroy(q);
        return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "DBMS Error in copying dead properties");
    }
    dbms_query_destroy(q);

    return NULL;
}

dav_error *dbms_del_dead_property(const dav_repos_db * d,
                                  const dav_repos_resource * r,
                                  const dav_repos_property * pr)
{
    apr_pool_t *pool = r->p;
    dav_repos_query *q = NULL;
    dav_error *err = NULL;

    TRACE();

    q = dbms_prepare(pool, d->db,
                     "DELETE FROM properties WHERE resource_id=? and name=? "
                     "and namespace_id=?");
    dbms_set_int(q, 1, r->serialno);
    dbms_set_string(q, 2, pr->name);
    dbms_set_int(q, 3, pr->ns_id);

    if (dbms_execute(q)) 
        err = dav_new_error(r->p, HTTP_INTERNAL_SERVER_ERROR, 0,
                            "Error in the deleting dead prop");

    dbms_query_destroy(q);

    return err;
}               /*End of dbms_del_dead_property */

/** 
 * @brief Insert new namespace
 * Must build hash and try to find namespace first.
 * @param d The DB connection struct
 * @param db_r THe resource struct
 * @param namespace The namespace to insert
 * @return Namespace id of the new namespace, 0 if fail
 */
static long dbms_insert_ns(const dav_repos_db * d,
                           dav_repos_resource * db_r,
                           const char *namespace)
{
    long id;
    apr_pool_t *pool = db_r->p;
    dav_repos_query *q = NULL;

    TRACE();

    /* We need to add name space to DB and fill hash */
    q = dbms_prepare(pool, d->db,
                     "INSERT INTO namespaces (name) VALUES (?)");
    dbms_set_string(q, 1, namespace);

    if (dbms_execute(q)) {
        db_error_message(db_r->p, d->db, "mysql_query error");
        dbms_query_destroy(q);
        return 0;
    }
    id = dbms_insert_id(d->db, "namespaces", pool);
    dbms_query_destroy(q);

    return id;
}

char *dbms_get_ns_name(const dav_repos_db *d, 
                       dav_repos_resource *db_r, 
                       long ns_id)
{
    apr_pool_t *pool = db_r->p;
    dav_repos_query *q = NULL;
    char *retVal = NULL;

    if(ns_id < 1) return NULL;

    q = dbms_prepare(pool, d->db, 
                     "SELECT name FROM namespaces WHERE id = ?");
    dbms_set_int(q, 1, ns_id);
    
    if (dbms_execute(q)) {
        dbms_query_destroy(q);
        return NULL;
    }

    if (dbms_next(q) == 1)
        retVal = dbms_get_string(q, 1);

    dbms_query_destroy(q);
    
    return retVal;
}
    
dav_error *dbms_get_namespace_id(apr_pool_t *pool, const dav_repos_db * d, 
                                 const char *namespace, long *ns_id)
{
    dav_repos_query *q = NULL;
    *ns_id = 0;

    /* Get ns_id from DB */
    q = dbms_prepare(pool, d->db, "SELECT id FROM namespaces WHERE name=?");
    dbms_set_string(q, 1, namespace);

    /* Get ns_id */
    if (dbms_execute(q)) {
        dbms_query_destroy(q);
        return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "dbms_execute error");
    }

    if (dbms_next(q) == 1)
        *ns_id = dbms_get_int(q, 1);

    dbms_query_destroy(q);
    return NULL;
}

dav_error *dbms_get_ns_id(const dav_repos_db * d, dav_repos_resource * db_r, 
                          const char *namespace, long *ns_id)
{
    apr_pool_t *pool = db_r->p;
    dav_error *err = NULL;

    TRACE();

    if (db_r->ns_id_hash == NULL)
        /* figure out a way to share ns_id_hash among all resources
           involved in a request. use hooks ctx? */
        db_r->ns_id_hash = apr_hash_make(pool);
    else {
        const long *p_id = NULL;
        p_id = apr_hash_get(db_r->ns_id_hash, namespace, APR_HASH_KEY_STRING);
        if(p_id) {
            *ns_id = *p_id;
            return NULL;
        }
    }

    err = dbms_get_namespace_id(pool, d, namespace, ns_id);
    if (err) return err;

    if (*ns_id == 0)
        *ns_id = dbms_insert_ns(d, db_r, namespace);

    if (*ns_id > 0) {
        long *p_id = apr_pcalloc(pool, sizeof(*p_id));
        *p_id = *ns_id;
        apr_hash_set(db_r->ns_id_hash, namespace, APR_HASH_KEY_STRING, p_id);
    }
    else
        err = dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                            "Could not insert namespace");

    return err;
}

long dbms_num_sha1_resources(apr_pool_t *pool, const dav_repos_db *d,
                             const char *sha1str)
{
    dav_repos_query *q = NULL;
    long num_sha1s = 0;
    TRACE();

    q = dbms_prepare(pool, d->db,
                     "SELECT resource_id FROM media "
                     "WHERE sha1=?");
    dbms_set_string(q, 1, sha1str);

    if (dbms_execute(q)) {
        dbms_query_destroy(q);
        return -1;
    }

    while (dbms_next(q) == 1) {
        DBG1("medium_id :%ld\n", (long)dbms_get_int(q, 1));
        num_sha1s++;
    }

    
    dbms_query_destroy(q);
    DBG1("Num_sha1s : %ld", num_sha1s);
    return num_sha1s;
}

dav_error *dbms_drop_constraint(apr_pool_t *pool, const dav_repos_db *db,
                                const char *table, const char *constraint)
{
    dav_repos_query *q = NULL;
    const char *query = NULL;

    TRACE();

    if (db->dbms == PGSQL)
        query = apr_psprintf(pool, "ALTER TABLE %s DROP CONSTRAINT %s",
                             table, constraint);
    else if (db->dbms == MYSQL)
        query = apr_psprintf(pool, "ALTER TABLE %s DROP FOREIGN KEY `%s`",
                             table, constraint);

    q = dbms_prepare(pool, db->db, query);

    if (dbms_execute(q)) {
        dbms_query_destroy(q);
        db_error_message(pool, db->db, "dbms_execute error");
        return dav_new_error(pool,
                             HTTP_INTERNAL_SERVER_ERROR, 0, 
                             "DBMS Error: can't disable constraint");
    }

    dbms_query_destroy(q);

    return NULL;
}

dav_error *dbms_add_constraint(apr_pool_t *pool, const dav_repos_db *db,
                               const char *table, const char *constraint)
{
    dav_repos_query *q = NULL;
    const char *query;

    TRACE();

    query = apr_psprintf(pool, "ALTER TABLE %s ADD %s",
                         table, constraint);
    q = dbms_prepare(pool, db->db, query);

    if (dbms_execute(q)) {
        dbms_query_destroy(q);
        db_error_message(pool, db->db, "dbms_execute error");
        return dav_new_error(pool,
                             HTTP_INTERNAL_SERVER_ERROR, 0, 
                             "DBMS Error: couldn't add constraint");
    }

    dbms_query_destroy(q);

    return NULL;
}

dav_error *dbms_defer_all_constraints(apr_pool_t *pool, const dav_repos_db *d)
{
    dav_repos_query *q = NULL;

    TRACE();

    q = dbms_prepare(pool, d->db, "SET CONSTRAINTS ALL DEFERRED");

    if (dbms_execute(q)) {
        dbms_query_destroy(q);
        db_error_message(pool, d->db, "dbms_execute error");
        return dav_new_error(pool,
                             HTTP_INTERNAL_SERVER_ERROR, 0, 
                             "DBMS Error: couldn't defer all constraints");
    }

    dbms_query_destroy(q);

    return NULL;
}

char transaction_levels[4][50] = {
    "SET SESSION TRANSACTION LEVEL READ UNCOMMITTED",
    "SET SESSION TRANSACTION LEVEL READ COMMITTED",
    "SET SESSION TRANSACTION LEVEL REPEATABLE READ",
    "SET SESSION TRANSACTION LEVEL SERIALIZABLE"
};

/**
 * Set the isolation level 
 * level =1 => READ UNCOMMITTED 
 * level =2 => READ COMMITTED
 * level =3 => REPEATABLE READ
 * level =4 => SERIALIZABLE
 *
 * @return -1 on error and 0 on success
 */
int dbms_set_isolation_level(const dav_repos_db * d, apr_pool_t * pool,
                             int level)
{
    dav_repos_query *q = NULL;
    if((level>4)||(level<1))
        level=4;
    
    q = dbms_prepare(pool, d->db,transaction_levels[level-1]);
    
    if (dbms_execute(q)) {
        dbms_query_destroy(q);
        return -1;
    }

    dbms_query_destroy(q);
    return 0;
}

void dav_repos_update_dbr_resource(dav_repos_resource *db_r)
{
    dav_resource *resource = db_r->resource;

    TRACE();

    resource->uri = db_r->uri;
    resource->info->pathname = db_r->uri;
    resource->uuid = db_r->uuid;

    if (db_r->resourcetype != dav_repos_LOCKNULL)
        resource->exists = 1;

    switch (db_r->resourcetype) {
    case dav_repos_RESOURCE:
        db_r->type = resource->type = DAV_RESOURCE_TYPE_REGULAR;
        break;
    case dav_repos_COLLECTION:
        db_r->type = resource->type = DAV_RESOURCE_TYPE_REGULAR;
        resource->collection = APR_DIR;
        break;
    case dav_repos_VERSIONED_COLLECTION:
        db_r->type = resource->type = DAV_RESOURCE_TYPE_REGULAR;
        resource->collection = APR_DIR;
        resource->working = 
          (db_r->checked_state == DAV_RESOURCE_CHECKED_OUT);
        resource->versioned = 1;
        break;
    case dav_repos_COLLECTION_VERSION:
        db_r->type = resource->type = DAV_RESOURCE_TYPE_VERSION;
        resource->collection = APR_DIR;
        break;
    case dav_repos_VERSIONED:
        db_r->type = resource->type = DAV_RESOURCE_TYPE_REGULAR;
        resource->working = 
          (db_r->checked_state == DAV_RESOURCE_CHECKED_OUT);
        resource->versioned = 1;
        break;
    case dav_repos_VERSION:
        db_r->type = resource->type = DAV_RESOURCE_TYPE_VERSION;
        break;
    case dav_repos_VERSIONHISTORY:
        db_r->type = resource->type = DAV_RESOURCE_TYPE_HISTORY;
        break;
    case dav_repos_USER:
    case dav_repos_GROUP:
        db_r->type = resource->type = DAV_RESOURCE_TYPE_PRINCIPAL;
        break;
    case dav_repos_PRINCIPAL:
        db_r->type = resource->type = DAV_RESOURCE_TYPE_PRINCIPAL;
        break;
    case dav_repos_LOCKNULL:
        db_r->type = resource->type = DAV_RESOURCE_TYPE_REGULAR;
        resource->exists = 0;
        break;
    case dav_repos_REDIRECT:
        db_r->type = resource->type = DAV_RESOURCE_TYPE_REDIRECTREF;
        break;
    default:
        DBG0("We should never reach here - Repos.c - get_resource");
        break;
    }
}
