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
#include "dbms_quota.h"
#include "dbms_api.h"

dav_error *dbms_insert_quota(apr_pool_t *pool, const dav_repos_db *d,
                             long principal_id, unsigned long quota)
{
    dav_repos_query *q = NULL;
    int isql_result = 0;
    dav_error *err = NULL;

    TRACE();

    
    q = dbms_prepare(pool, d->db,
                     "INSERT INTO quota (principal_id, used_quota, total_quota) "
                     "VALUES ( ?, ?, ?) ");
    dbms_set_int(q, 1, principal_id);
    dbms_set_int(q, 2, 0);
    dbms_set_int(q, 3, quota);

    isql_result = dbms_execute(q);
    dbms_query_destroy(q);
    if (isql_result)
	err = dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0, 
                            "DBMS error during insert to 'quota'");
    return err;
}

/**
 * Get the number of bytes available to a particular principal
 * @param pool The pool to allocate from
 * @param d DB connection handle
 * @param owner_id The principal_id of the principal
 * @return available bytes, LS_ERROR on error
 */
dav_error *dbms_get_available_bytes(apr_pool_t *pool, const dav_repos_db *d,
                                    long owner_id, long *p_navail_bytes)
{
    dav_repos_query *q = NULL;
    dav_error *err = NULL;

    TRACE();

    *p_navail_bytes = 0;

    q = dbms_prepare(pool, d->db, "SELECT total_quota - used_quota "
                                  "FROM quota WHERE principal_id = ?");
    dbms_set_int(q, 1, owner_id);
    
    if (dbms_execute(q) || (1 != dbms_next(q)))
        err = dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                            "DBMS error while fetching available bytes");
    else
        *p_navail_bytes = dbms_get_int(q, 1);
    dbms_query_destroy(q);

    return err;
}
