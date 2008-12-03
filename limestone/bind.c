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

#include <httpd.h>
#include <http_config.h>
#include <http_protocol.h>
#include <http_log.h>
#include <http_request.h>
#include <http_core.h>		/* for ap_construct_url */
#include <mod_dav.h>

#include <apr.h>
#include <apr_strings.h>
#include <apr_hash.h>
#include <apr_tables.h>
#include <apr_file_io.h>

#include "dav_repos.h"
#include "bridge.h" /* for unbind_resource */
#include "dbms.h"
#include "dbms_bind.h" /* for dbms bind functions */
#include "util.h"
#include "bind.h"
#include "dbms_acl.h" /* for dbms_change_acl_parent */

static int dav_repos_is_bindable(const dav_resource *resource)
{
    TRACE();
    if (resource->type == DAV_RESOURCE_TYPE_HISTORY)
        return 0;
    return 1;
}

dav_error *dav_repos_bind_resource(const dav_resource *resource,
                                   const dav_resource *collection,
                                   const char *segment,
                                   dav_resource *binding)
{
    dav_repos_db *db = resource->info->db;
    dav_repos_resource *db_r = (dav_repos_resource *) resource->info->db_r;
    dav_error *err = NULL;

    TRACE();
    
    /* FIXME: use parent_id of binding instead of binding_parent serialno */
    err = dbms_insert_bind(db_r->p, db, resource->info->db_r->serialno,
                           collection->info->db_r->serialno, segment);

    /* think of a better way to do this */
    if (binding) {
        /* check if this can be done without an extra query */
        dbms_get_property(db, binding->info->db_r);
        dav_repos_update_dbr_resource(binding->info->db_r);
    }
    return err;
}

dav_error *dav_repos_rebind_resource(const dav_resource *collection, 
                                     const char *segment,
                                     dav_resource *href_res, 
                                     dav_resource *new_bind)
{ 
    apr_pool_t *pool = href_res->pool;
    dav_repos_db *db = href_res->info->db;
    dav_error *err = NULL;
    dav_repos_resource *coll_dbr = collection->info->db_r;
    dav_repos_resource *href_dbr = href_res->info->db_r;
    dav_repos_resource *new_bind_dbr = new_bind->info->db_r;
    dav_repos_resource *href_parent_dbr = NULL;
    char *path;
    
    TRACE();

    err = sabridge_retrieve_parent(href_dbr, &href_parent_dbr);
    
    if (!err && new_bind->exists)
        err = dbms_delete_bind(pool, db, coll_dbr->serialno, 
                               new_bind_dbr->serialno, segment);
    if (err) return err;

    err= dbms_rebind_resource(pool, db, href_parent_dbr->serialno,
                              basename(href_dbr->uri),
                              coll_dbr->serialno, segment);

    //    if (!err) err = sabridge_delete_if_orphaned(db, href_res->info->db_r, NULL);
    if (!err && new_bind->exists) {
        char *uri_bak = new_bind_dbr->uri;
        new_bind_dbr->uri = NULL;
        err = sabridge_unbind_resource(db, new_bind_dbr);
        new_bind_dbr->uri = uri_bak;
    }

    if (!err) err = dbms_get_property(db, href_dbr);
    if (!err) dav_repos_update_dbr_resource(href_dbr);
    if (!err) err = dbms_get_property(db, new_bind_dbr);
    if (!err) dav_repos_update_dbr_resource(new_bind_dbr);
    if (new_bind_dbr->resourcetype == dav_repos_RESOURCE) {
        if (!err) err = generate_path(&path, new_bind_dbr->p, db->file_dir, 
                                      new_bind_dbr->sha1str);
        if (!err) new_bind_dbr->getcontenttype = 
                                    get_mime_type(new_bind_dbr->uri, path);
        if (!err) err = dbms_update_media_props(db, new_bind_dbr);
    }
    if (!err) err = dbms_change_acl_parent(db, href_dbr, coll_dbr->serialno);

    return err;
}

dav_error *dav_repos_unbind_resource(dav_resource *resource,
                                     const dav_resource *collection, 
                                     const char *segment)
{
    dav_repos_resource *db_r = resource->info->db_r;
    dav_repos_db *db = resource->info->db;
    TRACE();

    return sabridge_unbind_resource(db, db_r);
} 

const dav_hooks_binding dav_repos_hooks_binding = {
    dav_repos_is_bindable,
    dav_repos_bind_resource,
    dav_repos_unbind_resource,
    dav_repos_rebind_resource
};
