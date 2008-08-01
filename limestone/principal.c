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


#include <apr_strings.h>

#include "dav_repos.h"
#include "bridge.h" /* for insert_resource */
#include "dbms.h"
#include "dbms_principal.h"
#include "util.h"
#include "acl.h"

dav_error *sabridge_create_home_folder(const dav_repos_db *d,
                                       dav_repos_resource *r,
                                       dav_repos_resource **phome_folder)
{
    apr_pool_t *pool = r->p;
    dav_repos_resource *hf;
    const dav_principal *owner;
    request_rec *rec = r->resource->info->rec;
    const dav_hooks_acl *acl_hooks = dav_get_acl_hooks(rec);
    dav_error *err = NULL;
    const char *user = basename(r->uri);

    TRACE();

    sabridge_new_dbr_from_dbr(r, &hf);
    hf->uri = apr_psprintf(pool, "%s/home/%s", hf->root_path, user);
    hf->owner_id = r->serialno;
    hf->resourcetype = dav_repos_COLLECTION;
    sabridge_insert_resource(d, hf, rec, SABRIDGE_DELAY_ACL);

    owner = dav_principal_make_from_url(rec, r->uri);
    err = (*acl_hooks->create_initial_acl)(owner, hf->resource);

    *phome_folder = hf;

    return err;
}

dav_error *sabridge_add_prin_to_grp(apr_pool_t *pool,
                                    const dav_repos_db *db,
                                    long grp_id, long prin_id)
{
    dav_error *err = NULL;

    TRACE();

    /* check if prin is already in group */
    if (0 != dbms_is_prin_in_grp(pool, db, grp_id, prin_id))
        return dav_new_error(pool, HTTP_PRECONDITION_FAILED, 0,
                             "Principal already in group");
    /* check if addition will cause a loop */
    if (0 != dbms_will_loop_prin_add_grp(pool, db, grp_id, prin_id))
        return dav_new_error(pool, HTTP_PRECONDITION_FAILED, 0,
                             "Can't add principal to group");

    err = dbms_add_prin_to_group(pool, db, grp_id, prin_id);
    if (err) return err;

    err = dbms_add_prin_to_grp_xitively(pool, db, grp_id, prin_id);
    return err;
}

dav_error *sabridge_rem_prin_frm_grp(apr_pool_t *pool,
                                     const dav_repos_db *db,
                                     long grp_id, long prin_id)
{
    dav_error *err = NULL;

    TRACE();

    err = dbms_rem_prin_frm_grp(pool, db, grp_id, prin_id);
    if (err) return err;

    err = dbms_rem_prin_frm_grp_xitively(pool, db, grp_id, prin_id);

    return err;
}

/* Assumes that an entry has already been made in the resources table.
 * Sets the password, updates group memberships, and creates a home folder */
dav_error *dav_repos_create_user(dav_resource *resource,
                                 const char* passwd)
{
    dav_repos_db *db = resource->info->db;
    dav_repos_resource *db_r = (dav_repos_resource *) resource->info->db_r;
    dav_error *err = NULL;
    dav_repos_resource *home_folder;
    long group_id;
    const char *user = basename(db_r->uri);
    request_rec *rec = resource->info->rec;
    const dav_hooks_acl *acl_hooks = dav_get_acl_hooks(rec);

    TRACE();

    err = dbms_insert_user(db, db_r, get_password_hash(db_r->p, user, passwd));
    if (err) return err;

    /* Change owner or principal resource to self */
    err = (*acl_hooks->create_initial_acl)
      (dav_repos_get_prin_by_name(rec, user), resource);

    /* make user member of Authenticate and (transitively) All groups */
    err = dbms_get_principal_id_from_name(db_r->p, db, "authenticated", &group_id);
    if (err) return err;
    err = sabridge_add_prin_to_grp(db_r->p, db, group_id, db_r->serialno);
    if (err) return err;

    /* Create home folder for user */
    err = sabridge_create_home_folder(db, db_r, &home_folder);
    
    return err;
}

dav_error *dav_repos_update_password(const dav_resource *resource,
                                     const char *passwd)
{
    dav_repos_db *db = resource->info->db;
    dav_repos_resource *db_r = (dav_repos_resource *) resource->info->db_r;
    const char *user = basename(db_r->uri);
    return dbms_update_user(db, db_r, get_password_hash(db_r->p, user, passwd));
}

dav_error *dav_repos_create_group(const dav_resource *resource,
                                         const char *created)
{
    dav_repos_db *db = resource->info->db;
    dav_repos_resource *db_r = (dav_repos_resource *) resource->info->db_r;
    dav_error *err = NULL;

    TRACE();

    db_r->resourcetype = dav_repos_GROUP;
    err = sabridge_insert_resource(db, db_r, resource->info->rec, 0);

    return err;
}
