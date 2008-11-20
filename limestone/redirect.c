/* ====================================================================
 * Copyright 2008 Lime Spot LLC
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

#include "redirect.h"

dav_error *dav_repos_create_redirectref(dav_resource *resource,
                                        const char *reftarget,
                                        dav_redirectref_lifetime t)
{
    dav_repos_resource *db_r = resource->info->db_r;
    dav_repos_db *db = resource->info->db;
    dav_error *err = NULL;

    TRACE();

    db_r->resourcetype = dav_repos_REDIRECT;
    if ((err = sabridge_insert_resource(db, db_r, resource->info->rec, 0)))
        return err;

    return dbms_insert_redirectref(db, db_r, reftarget, t);
}

dav_error *dav_repos_update_redirectref(dav_resource *resource,
                                        const char *reftarget,
                                        dav_redirectref_lifetime t)
{
    dav_repos_resource *db_r = resource->info->db_r;
    dav_repos_db *db = resource->info->db;

    TRACE();

    return dbms_update_redirectref(db, db_r, reftarget, t);
}

dav_redirectref_lifetime dav_repos_get_lifetime(dav_resource *resource)
{
    dav_repos_resource *db_r = resource->info->db_r;
    dav_repos_db *db = resource->info->db;

    TRACE();

    dbms_get_redirect_props(db, db_r);

    return db_r->redirect_lifetime;
}

const char *dav_repos_get_reftarget(dav_resource *resource)
{
    dav_repos_resource *db_r = resource->info->db_r;
    dav_repos_db *db = resource->info->db;

    TRACE();

    dbms_get_redirect_props(db, db_r);

    return db_r->reftarget;
}

/* redirect hooks */
const dav_hooks_redirect dav_repos_hooks_redirect = {
    dav_repos_create_redirectref,
    dav_repos_update_redirectref,
    dav_repos_get_reftarget,
    dav_repos_get_lifetime,
    NULL        // ctx
};
