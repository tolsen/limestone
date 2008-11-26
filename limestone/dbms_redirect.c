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

#include "dbms_redirect.h"

static const char *lifetime_to_s(dav_redirectref_lifetime t)
{
    switch(t) {
        case DAV_REDIRECTREF_TEMPORARY:
            return "t";

        case DAV_REDIRECTREF_PERMANENT:
            return "p";

        default:
            return NULL;
    }

    return NULL;
}

dav_error *dbms_insert_redirectref(const dav_repos_db *d, 
                                   dav_repos_resource *r,
                                   const char *reftarget,
                                   dav_redirectref_lifetime t)
{
    dav_repos_query *q = NULL;
    dav_error *err = NULL;
    
    q = dbms_prepare(r->p, d->db, "INSERT INTO redirectrefs "
                     "(resource_id, reftarget, lifetime) "
                     "VALUES (?, ?, ?)");

    dbms_set_int(q, 1, r->serialno);
    dbms_set_string(q, 2, reftarget);
    dbms_set_string(q, 3, lifetime_to_s(t));

    if (dbms_execute(q))
        err = dav_new_error(r->p, HTTP_INTERNAL_SERVER_ERROR, 0,
                            "DBMS error while inserting into 'redirectrefs'");

    dbms_query_destroy(q);

    return err;
}

dav_error *dbms_update_redirectref(const dav_repos_db *d,
                                   dav_repos_resource *r,
                                   const char *reftarget,
                                   dav_redirectref_lifetime t)
{
    dav_repos_query *q = NULL;
    dav_error *err = NULL;
    const char *lifetime = lifetime_to_s(t);

    TRACE();

    const char *query = apr_psprintf(r->p, "UPDATE redirectrefs SET ");
    if (reftarget) {
        query = apr_pstrcat(r->p, query, "reftarget = '", 
                            dbms_escape(r->p, d->db, reftarget), "'", NULL);
        if (lifetime) {
            query = apr_pstrcat(r->p, query, ", lifetime = '", lifetime, 
                                "'", NULL);
        }
    }
    else {
        query = apr_pstrcat(r->p, query, "lifetime = '", lifetime, "'", NULL);
    }

    query = apr_pstrcat(r->p, query, " WHERE resource_id = ?", NULL);

    q = dbms_prepare(r->p, d->db, query);
    dbms_set_int(q, 1, r->serialno);

    if (dbms_execute(q))
        err = dav_new_error(r->p, HTTP_INTERNAL_SERVER_ERROR, 0,
                            "DBMS error in while updating 'redirectrefs'");

    dbms_query_destroy(q);
    return err;
}

dav_error *dbms_get_redirect_props(const dav_repos_db *d,
                                   dav_repos_resource *r)
{
    dav_repos_query *q = NULL;
    dav_error *err = NULL;

    TRACE();

    /* do nothing if we have already fetched redirect props */
    if (r->redirect_lifetime && r->reftarget) {
        return NULL;
    }

    q = dbms_prepare(r->p, d->db, "SELECT lifetime, reftarget "
                     "FROM redirectrefs WHERE resource_id = ?");
    dbms_set_int(q, 1, r->serialno);

    if (dbms_execute(q) || (dbms_next(q) <=0)) {
        err = dav_new_error(r->p, HTTP_INTERNAL_SERVER_ERROR, 0,
                            "DBMS error in reftargets lookup.");
        goto error;
    }

    if (0 == apr_strnatcmp(dbms_get_string(q, 1), "p")) 
        r->redirect_lifetime = DAV_REDIRECTREF_PERMANENT;
    else
        r->redirect_lifetime = DAV_REDIRECTREF_TEMPORARY;

    r->reftarget = dbms_get_string(q, 2);

    error:
        dbms_query_destroy(q);
        return err;
}
