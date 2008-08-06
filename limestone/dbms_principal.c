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

#include "dbms_principal.h"
#include <apr_strings.h>
#include "dbms_api.h"
#include "bridge.h"             /* for sabridge_new_dbr_from_dbr */
#include "acl.h"

dav_error *dbms_get_principal_id_from_name(apr_pool_t *pool, const dav_repos_db *d,
                                           const char *name, long *p_prin_id)
{
    dav_repos_query *q = NULL;
    long principal_id = 0;

    TRACE();

    q = dbms_prepare(pool, d->db,
                     "SELECT resource_id FROM principals "
                     "WHERE name = ?");
    dbms_set_string(q, 1, name);
    if (dbms_execute(q)) {
        dbms_query_destroy(q);
    	db_error_message(pool, d->db, "dbms_execute error");
        return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "Error retreving principal id from name");
    }

    if (dbms_next(q) == 1)
        principal_id = dbms_get_int(q, 1);

    dbms_query_destroy(q);
    *p_prin_id = principal_id;

    return NULL;
}

/* Create an entry in principals table */ 
dav_error *dbms_insert_principal(const dav_repos_db *d,
                                 dav_repos_resource *r)
{
    dav_repos_query *q = NULL;
    apr_pool_t *pool = r->p;
    dav_error *err = NULL;

    TRACE();

    q = dbms_prepare(pool, d->db, 
                     "INSERT INTO principals (name, resource_id) "
                     "VALUES(?, ?)");
    dbms_set_string(q, 1, r->displayname);
    dbms_set_int(q, 2, r->serialno);

    if (dbms_execute(q)) {
    	db_error_message(r->p, d->db, "dbms_execute error");
        err = dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                            "Error inserting principal");
    }
    dbms_query_destroy(q);
    return err;
}

dav_error *dbms_set_principal_email(apr_pool_t *pool, const dav_repos_db *d,
                                    long principal_id, const char *email)
{
    dav_repos_query *q = NULL;
    dav_error *err = NULL;

    TRACE();

    q = dbms_prepare(pool, d->db, 
                     "UPDATE principals SET email = ? WHERE resource_id= ?");
    dbms_set_string(q, 1, email);
    dbms_set_int(q, 2, principal_id);

    if (dbms_execute(q)) {
        dbms_query_destroy(q);
    	db_error_message(pool, d->db, "dbms_execute error");
        err = dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                            "Couldn't set principal email");
    }
    dbms_query_destroy(q);
    return err;
}

const char *dbms_get_principal_email(apr_pool_t *pool, const dav_repos_db *d,
                                     long principal_id)
{
    dav_repos_query *q = NULL;
    const char *email = NULL;
    TRACE();

    q = dbms_prepare (pool, d->db,
                      "SELECT email FROM principals WHERE resource_id = ?");
    dbms_set_int(q, 1, principal_id);

    dbms_execute(q);
    if (dbms_next(q) == 1)
        email = dbms_get_string(q, 1);
    dbms_query_destroy(q);
    return email;
}

apr_hash_t *dbms_get_domain_map(apr_pool_t *pool, const dav_repos_db *d,
                                long principal_id)
{
    dav_repos_query *q = NULL;
    apr_hash_t *domain_map = NULL;

    TRACE();

    q = dbms_prepare(pool, d->db, "SELECT path, domain FROM user_domains "
                                  "WHERE principal_id = ?");
    dbms_set_int(q, 1, principal_id);

    if (dbms_execute(q)) {
        dbms_query_destroy(q);
        db_error_message(pool, d->db, "dbms_execute error");
        return NULL;
    }

    int num = dbms_results_count(q);
    if (num) {
        domain_map = apr_hash_make(pool);
    }

    int i = 0;
    char **dbrow;

    while (i <  num) {
        dbrow = dbms_fetch_row_num(d->db, q, pool, i);
        i++;

        apr_hash_set(domain_map, dbrow[0], APR_HASH_KEY_STRING, dbrow[1]);
    }

    dbms_query_destroy(q);

    return domain_map;
}

static const char *domain_map_to_values(apr_pool_t *pool, apr_hash_t *domain_map, long principal_id)
{
    apr_hash_index_t *hi;
    void *domain;
    const void *path;
    const char *values = NULL;

    for(hi = apr_hash_first(pool, domain_map); hi; hi = apr_hash_next(hi)) {
        apr_hash_this(hi, &path, NULL, &domain);
        const char *value = apr_psprintf(pool, "(%ld, '%s', '%s')", principal_id,
                                         (char *)domain, (char *)path);

        if (NULL == values) {
            values = value;
        }
        else {
            values = apr_pstrcat(pool, values, ",", value, NULL);
        }
    }

    return values;
}

dav_error *dbms_set_domain_map(apr_pool_t *pool, const dav_repos_db *d,
                               long principal_id, apr_hash_t *domain_map)
{
    dav_repos_query *q = NULL;

    TRACE();

    q = dbms_prepare(pool, d->db, "DELETE FROM user_domains WHERE principal_id=?");
    dbms_set_int(q, 1, principal_id);

    if (dbms_execute(q)) {
        dbms_query_destroy(q);
        db_error_message(pool, d->db, "dbms_execute error");
        return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "Failed DELETE in dbms_set_domain_map");
    }

    dbms_query_destroy(q);

    if (NULL == domain_map) {
        return NULL;
    }

    q = dbms_prepare(pool, d->db, 
                     apr_psprintf(pool, "INSERT INTO user_domains"
                                        " (principal_id, domain, path) VALUES %s ", 
                                  domain_map_to_values(pool, domain_map, principal_id)));
    dbms_set_string(q, 1, domain_map_to_values(pool, domain_map, principal_id));
    if (dbms_execute(q)) {
        dbms_query_destroy(q);
        db_error_message(pool, d->db, "dbms_execute error");
        return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "Failed INSERT in dbms_set_domain_map 2");
    }

    dbms_query_destroy(q);

    return NULL;
}

const char *dbms_get_domain_path(apr_pool_t *pool, dav_repos_db *d, const char *host)
{
    dav_repos_query *q = NULL;
    const char *user = NULL, *path = NULL;
    
    TRACE();

    q = dbms_prepare(pool, d->db, "SELECT name, path "
                     "FROM principals INNER JOIN user_domains "
                     "ON principals.resource_id = user_domains.principal_id "
                     "WHERE domain = ? ORDER BY id LIMIT 1");
    dbms_set_string(q, 1, host);
    dbms_execute(q);
    if (1 == dbms_next(q)) {
        user = dbms_get_string(q, 1);
        path = dbms_get_string(q, 2);
    }
    dbms_query_destroy(q);

    if (user && path) {
       return apr_pstrcat(pool, "/home/", user, path, NULL);
    }
    
    return NULL;
}

/* Create an entry in users table */
dav_error *dbms_insert_user(const dav_repos_db *d, dav_repos_resource *r,
                            const char *pwhash)
{
    dav_repos_query *q = NULL;
    apr_pool_t *pool = r->p;
    dav_error *err = NULL;

    TRACE();

    q = dbms_prepare(pool, d->db, 
                     "INSERT INTO users (principal_id, pwhash) "
                     "VALUES (?, ?)");
    dbms_set_int(q, 1, r->serialno);
    dbms_set_string(q, 2, pwhash);

    if (dbms_execute(q)) {
        dbms_query_destroy(q);
    	db_error_message(r->p, d->db, "dbms_execute error");
        err = dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                            "Couldn't insert user");
    }
    dbms_query_destroy(q);
    return err;
}

dav_error *dbms_update_user(const dav_repos_db *d, dav_repos_resource *r,
                            const char *pwhash)
{
    dav_repos_query *q = NULL;
    apr_pool_t *pool = r->p;
    dav_error *err = NULL;

    TRACE();

    q = dbms_prepare(pool, d->db, 
                     "UPDATE users SET pwhash = ? WHERE principal_id= ?");
    dbms_set_string(q, 1, pwhash);
    dbms_set_int(q, 2, r->serialno);

    if (dbms_execute(q)) {
        dbms_query_destroy(q);
    	db_error_message(r->p, d->db, "dbms_execute error");
        err = dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                            "Couldn't update user");
    }
    dbms_query_destroy(q);
    return err;
}

const char *dbms_get_user_pwhash(apr_pool_t *pool, const dav_repos_db *d,
                                 long principal_id)
{
    dav_repos_query *q = NULL;
    const char *pwhash = NULL;
    TRACE();

    q = dbms_prepare (pool, d->db,
                      "SELECT pwhash FROM users WHERE principal_id = ?");
    dbms_set_int(q, 1, principal_id);

    dbms_execute(q);
    if (dbms_next(q) == 1)
        pwhash = dbms_get_string(q, 1);

    dbms_query_destroy(q);
    return pwhash;
}

dav_error *dbms_add_prin_to_group(apr_pool_t *pool, const dav_repos_db *d,
                                  long group_id, long principal_id)
{
    dav_repos_query *q = NULL;
    dav_error *err = NULL;

    TRACE();

    q = dbms_prepare(pool, d->db, 
                     "INSERT INTO group_members(group_id, member_id) "
                     "VALUES (?, ?)");
    dbms_set_int(q, 1, group_id);
    dbms_set_int(q, 2, principal_id);

    if (dbms_execute(q))
        err = dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                            "Couldn't add principal to group");
    dbms_query_destroy(q);

    return err;
}

int dbms_is_prin_in_grp(apr_pool_t *pool, const dav_repos_db *d,
                        long prin_id, long grp_id)
{
    dav_repos_query *q = NULL;
    int result = 0;
    TRACE();

    q = dbms_prepare (pool, d->db,
                      "SELECT member_id FROM group_members "
                      "WHERE group_id=? AND member_id=? ");
    dbms_set_int(q, 1, grp_id);
    dbms_set_int(q, 2, prin_id);

    if (dbms_execute(q))
        result = -1;
    else if (dbms_next(q) == 1)
        result = 1;
    else result = 0;

    dbms_query_destroy(q);
    return result;
}

/* check if a loop would be caused by addition of principal to group */
int dbms_will_loop_prin_add_grp(apr_pool_t *pool, const dav_repos_db *d,
                                long grp_id, long prin_id)
{
    dav_repos_query *q = NULL;
    int result;
    TRACE();

    q = dbms_prepare
      (pool, d->db,
       "SELECT * FROM "
       " (SELECT transitive_group_id AS cid FROM transitive_group_members "
       "  WHERE transitive_member_id=? AND transitive_count > 0 "
       " UNION SELECT ?) g "
       "   JOIN "
       " (SELECT transitive_member_id AS cid FROM transitive_group_members "
       "  WHERE transitive_group_id=? AND transitive_count > 0 "
       " UNION SELECT ?) m "
       "   ON g.cid = m.cid "
       "LIMIT 1 ");
    dbms_set_int(q, 1, grp_id);
    dbms_set_int(q, 2, grp_id);
    dbms_set_int(q, 3, prin_id);
    dbms_set_int(q, 4, prin_id);

    if (dbms_execute(q))
        result = -1;
    else if (dbms_next(q) == 1) 
        result = 1;
    else result = 0;
    dbms_query_destroy(q);
    return result;
}

dav_error *dbms_add_prin_to_grp_xitively(apr_pool_t *pool, 
                                         const dav_repos_db *d,
                                         long grp_id, long prin_id)
{
    dav_repos_query *q = NULL;
    dav_error *err = NULL;

    /* if prin A is already a member of grp G, return */

    /* get prin A and all its children. name this set C */
    /* get group G and all its parents, get the count(n) too. name this set P */

    /* for all c in C and p in P 
       if (c, p) exists in x_G_M,
         inc count of (c,p) by n
       else insert (c, p, count) to x_G_M
            where count is n */

    TRACE();

    /* Add the children of grpA to all the parents of grpB */

#define QUERY(SUBS)        "UPDATE transitive_group_members "           \
      "SET transitive_count = transitive_count + "                      \
      /* the count values returned are ones that aren't modified in this */ \
      /* UPDATE query */                                                \
      /* number of paths from parent of g to g */                       \
      SUBS "((SELECT t.transitive_count "                               \
      "          FROM (SELECT * FROM transitive_group_members) AS t"    \
      "          WHERE t.transitive_group_id = transitive_group_id "    \
      "                AND t.transitive_member_id=?), 1) "              \
      /* multiplied by */                                               \
      "    *   "                                                        \
      /* number of paths from prin to its child */                      \
      SUBS "((SELECT t.transitive_count "                               \
      "          FROM (SELECT * FROM transitive_group_members) AS t"    \
      "          WHERE t.transitive_group_id = ? "                      \
      "                AND t.transitive_member_id = transitive_member_id), 1)" \
      "WHERE (transitive_group_id, transitive_member_id) IN "           \
      "   (SELECT g.transitive_group_id, m.transitive_member_id "       \
      "    FROM ( SELECT transitive_group_id FROM transitive_group_members " \
      "           WHERE transitive_member_id=? "                        \
      "          UNION SELECT ?) g " /* grp and its parents */          \
      "             CROSS JOIN "                                        \
      "         ( SELECT transitive_member_id FROM transitive_group_members " \
      "           WHERE transitive_group_id=? "                         \
      "          UNION SELECT ?) m) " /* prin and its members */

    q = dbms_prepare
      (pool, d->db, d->dbms == MYSQL ? QUERY("IFNULL") : QUERY("COALESCE"));
    dbms_set_int(q, 1, grp_id);
    dbms_set_int(q, 2, prin_id);
    dbms_set_int(q, 3, grp_id);
    dbms_set_int(q, 4, grp_id);
    dbms_set_int(q, 5, prin_id);
    dbms_set_int(q, 6, prin_id);

    if (dbms_execute(q))
        err = dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                            "Couldn't add principal to group");
    dbms_query_destroy(q);
    if (err) return err;

    q = dbms_prepare
      (pool, d->db,
       "INSERT INTO transitive_group_members"
       " (transitive_group_id, transitive_member_id, transitive_count)"
       "   SELECT g.transitive_group_id, m.transitive_member_id, "
       "          g.n_to_grp * m.n_frm_prin"
       "   FROM ( (SELECT transitive_group_id, transitive_count AS n_to_grp "
       "           FROM transitive_group_members "
       "           WHERE transitive_member_id=?) "
       "          UNION (SELECT ?, 1) ) g " /* grp and its parents */
       "        CROSS JOIN "
       "        ( (SELECT transitive_member_id, transitive_count AS n_frm_prin"
       "           FROM transitive_group_members "
       "           WHERE transitive_group_id=?) "
       "          UNION (SELECT ?, 1) ) m" /* principal and its members(if any) */
       "   WHERE (g.transitive_group_id, m.transitive_member_id) NOT IN "
       "         (SELECT transitive_group_id, transitive_member_id "
       "          FROM transitive_group_members)");
    dbms_set_int(q, 1, grp_id);
    dbms_set_int(q, 2, grp_id);
    dbms_set_int(q, 3, prin_id);
    dbms_set_int(q, 4, prin_id);

    if (dbms_execute(q))
        err = dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                            "Couldn't add principal to group");
    dbms_query_destroy(q);

    return err;
}

dav_error *dbms_rem_prin_frm_grp(apr_pool_t *pool, const dav_repos_db *d,
                                 long grp_id, long prin_id)
{
    dav_repos_query *q = NULL;
    dav_error *err = NULL;

    TRACE();

    q = dbms_prepare(pool, d->db, 
                     "DELETE FROM group_members "
                     "WHERE group_id=? AND member_id=?");
    dbms_set_int(q, 1, grp_id);
    dbms_set_int(q, 2, prin_id);

    if (dbms_execute(q))
        err = dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                            "Couldn't add principal to group");
    dbms_query_destroy(q);

    return err;
}

dav_error *dbms_rem_prin_frm_grp_xitively(apr_pool_t *pool, 
                                          const dav_repos_db *d,
                                          long grp_id, long prin_id)
{
    dav_repos_query *q = NULL;
    dav_error *err = NULL;

    /* get all the groups of which grp is a member -- P */
    /* get prin A and all its children -- C */

    /* for all c in C and p in P 
       dec count of (c,p) in x_G_M by count of (p, G)
       if any of the counts become zero, remove those entries */
    TRACE();

    q = dbms_prepare
      (pool, d->db,
       "UPDATE transitive_group_members "
       "SET transitive_count = transitive_count - "
       /* the count values returned are ones that aren't modified in this 
          UPDATE query */
       /* number of paths from parent of g to g */
       "  (SELECT t.transitive_count "
       "   FROM (SELECT * FROM transitive_group_members) AS t"
       "   WHERE t.transitive_group_id = transitive_group_id "
       "         AND t.transitive_member_id=?) "
       /* multiplied by */
       "    *   "
       /* number of paths from prin to its child */
       "  (SELECT t.transitive_count "
       "   FROM (SELECT * FROM transitive_group_members) AS t"
       "   WHERE t.transitive_group_id = ? "
       "         AND t.transitive_member_id = transitive_member_id) "
       "WHERE (transitive_group_id, transitive_member_id) IN "
       "   (SELECT g.transitive_group_id, m.transitive_member_id "
       "    FROM ( SELECT transitive_group_id FROM transitive_group_members "
       "           WHERE transitive_member_id=? "
       "          UNION SELECT ?) g " /* grp and its parents */
       "                   JOIN "
       "         ( SELECT transitive_member_id FROM transitive_group_members "
       "           WHERE transitive_group_id=? "
       "          UNION SELECT ?) m) " /* prin and its members */
       );
    dbms_set_int(q, 1, grp_id);
    dbms_set_int(q, 2, prin_id);
    dbms_set_int(q, 3, grp_id);
    dbms_set_int(q, 4, grp_id);
    dbms_set_int(q, 5, prin_id);
    dbms_set_int(q, 6, prin_id);

    if (dbms_execute(q))
        err = dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                            "Couldn't add principal to group");
    dbms_query_destroy(q);
    if (err) return err;

    q = dbms_prepare
      (pool, d->db,
       "DELETE FROM transitive_group_members "
       "WHERE (transitive_group_id, transitive_member_id, transitive_count) IN "
       "   (SELECT g.transitive_group_id, m.transitive_member_id, 0 "
       "    FROM ( SELECT transitive_group_id FROM transitive_group_members "
       "           WHERE transitive_member_id=? "
       "          UNION SELECT ?) g " /* grp and its parents */
       "                JOIN "
       "         ( SELECT transitive_member_id FROM transitive_group_members "
       "           WHERE transitive_group_id=? "
       "          UNION SELECT ?) m) " /* prin and its members */
       );
    dbms_set_int(q, 1, grp_id);
    dbms_set_int(q, 2, grp_id);
    dbms_set_int(q, 3, prin_id);
    dbms_set_int(q, 4, prin_id);

    if (dbms_execute(q))
        err = dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                            "Couldn't add principal to group");
    dbms_query_destroy(q);
    return err;
}

int dbms_get_user_login_to_all_domains(apr_pool_t *pool, const dav_repos_db *d,
                                       long principal_id)
{
    dav_repos_query *q = NULL;
    int login_to_all_domains = 0;

    TRACE();

    q = dbms_prepare (pool, d->db,
                      "SELECT login_to_all_domains "
                      "FROM users WHERE principal_id = ?");
    dbms_set_int(q, 1, principal_id);

    dbms_execute(q);
    if (dbms_next(q) == 1) {
        const char *retval = dbms_get_string(q, 1);
        login_to_all_domains = (*retval == 't');
    }

    dbms_query_destroy(q);
    return login_to_all_domains;
}

dav_error *dbms_set_user_login_to_all_domains(apr_pool_t *pool, 
                                              const dav_repos_db *d,
                                              long principal_id,
                                              int login_to_all_domains)
{
    dav_repos_query *q = NULL;
    dav_error *err = NULL;

    TRACE();

    q = dbms_prepare(pool, d->db, 
                     "UPDATE users SET login_to_all_domains = ? "
                     "WHERE principal_id= ?");
    dbms_set_string(q, 1, login_to_all_domains ? "true" : "false");
    dbms_set_int(q, 2, principal_id);

    if (dbms_execute(q)) {
        dbms_query_destroy(q);
    	db_error_message(pool, d->db, "dbms_execute error");
        err = dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                            "Couldn't update user");
    }
    dbms_query_destroy(q);
    return err;
}

int dbms_get_principal_type_from_name(apr_pool_t *pool, const dav_repos_db *db,
                                      const char *name)
{
    dav_repos_query *q = NULL;
    int prin_type = 0;

    TRACE();

    q = dbms_prepare(pool, db->db,
                     "SELECT type FROM principals "
                     " INNER JOIN resources ON resource_id = id "
                     "WHERE name = ?");
    dbms_set_string(q, 1, name);
    if (dbms_execute(q) == 0 && dbms_next(q) == 1)
        prin_type = dav_repos_get_type_id(dbms_get_string(q, 1));

    dbms_query_destroy(q);
    return prin_type;
}

dav_error *dbms_get_group_members(const dav_repos_db *db,
                                  const dav_repos_resource *group_dbr, 
                                  dav_repos_resource **members)
{
    apr_pool_t *pool = group_dbr->p;
    dav_repos_query *q = NULL;
    dav_repos_resource *dummy_head, *presult_link_tail;
    
    TRACE();

    q = dbms_prepare(pool, db->db,
                     "SELECT resource_id, name, type "
                     "FROM principals "
                     "      INNER JOIN group_members ON resource_id = member_id "
                     "      INNER JOIN resources ON resource_id = resources.id "
                     "WHERE group_id = ? ");
    if (dbms_execute(q)) {
        dbms_query_destroy(q);
        return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "Can't get group members");
    }

    dummy_head = apr_pcalloc(pool, sizeof(*dummy_head));
    presult_link_tail = dummy_head;

    while (dbms_next(q)) {
        dav_repos_resource *member;
        const char *name = NULL;
        sabridge_new_dbr_from_dbr(group_dbr, &member);
        presult_link_tail->next = member;
        presult_link_tail = member;

        member->serialno = dbms_get_int(q, 1);
        name = dbms_get_string(q, 2);
        member->resourcetype = dav_repos_get_type_id(dbms_get_string(q, 3));

        if (member->resourcetype == dav_repos_GROUP)
            member->uri = apr_pstrcat(pool, PRINCIPAL_GROUP_PREFIX, name);
        else if (member->resourcetype == dav_repos_USER)
            member->uri = apr_pstrcat(pool, PRINCIPAL_USER_PREFIX, name);
    }
    *members = dummy_head->next;
    dbms_query_destroy(q);
    return NULL;
}

dav_error *dbms_calculate_group_changes(const dav_repos_db *db,
                                        const dav_repos_resource *group_dbr,
                                        apr_hash_t *new_members,
                                        apr_array_header_t **p_members_to_remove)
{
    apr_pool_t *pool = group_dbr->p;
    dav_repos_query *q = NULL;
    apr_hash_index_t *iter = NULL;
    const char *new_members_table = "";
    dav_error *err = NULL;
    apr_array_header_t *prins_to_remove =
      apr_array_make(pool, 10, sizeof(*group_dbr));
    
    TRACE();

    iter = apr_hash_first(pool, new_members);
    while (iter) {
        const char *member_name;
        apr_hash_this(iter, (void*)&member_name, NULL, NULL);
        new_members_table = apr_pstrcat
          (pool, new_members_table, *new_members_table ? " UNION " : "",
           " SELECT '", dbms_escape(pool, db->db, member_name), "' ", 
           *new_members_table ? " AS new_member " : "",NULL);
        iter = apr_hash_next(iter);
    }

    const char *query = apr_pstrcat
      (pool, 
       "SELECT new_members.name, resources.type, resources.id, "
       "       principals.resource_id=cur_members.member_id "
       "FROM principals "
       "    RIGHT OUTER JOIN (", new_members_table, ") new_members ",
       "                ON principals.name = new_members.name ",
       "    FULL OUTER JOIN "
       "       (SELECT * FROM group_members WHERE group_id = ?) cur_members "
       "                ON principals.resource_id = cur_members.member_id "
       "    LEFT OUTER JOIN resources ON principals.resource_id = resources.id "
       "                      OR cur_members.member_id=resources.id ", NULL);
    q = dbms_prepare(pool, db->db, query);
    dbms_set_int(q, 1, group_dbr->serialno);
    if (dbms_execute(q))
        err = dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                            "Can't calculate group changes");

    while (!err && dbms_next(q) == 1) {
        const char *name = dbms_get_string(q, 1);
        const char *type = dbms_get_string(q, 2);
        const long member_id = dbms_get_int(q, 3);
        const int is_already_a_member = (*(dbms_get_string(q, 4)) == 't');

        if (member_id == 0) {
            err = dav_new_error(pool, HTTP_CONFLICT, 0,
                                "New member does not exist");
            break;
        }
        
        if (is_already_a_member) {
            apr_hash_set(new_members, name, APR_HASH_KEY_STRING, NULL);
            continue;
        }
        dav_repos_resource *prin = NULL;
        if (*name == '\0')
            prin = apr_array_push(prins_to_remove);
        else {
            sabridge_new_dbr_from_dbr(group_dbr, &prin);
            apr_hash_set(new_members, name, APR_HASH_KEY_STRING, prin);
        }

        prin->serialno = member_id;
        prin->resourcetype = dav_repos_get_type_id(type);
    }
    if (!apr_is_empty_array(prins_to_remove))
        *p_members_to_remove = prins_to_remove;
    dbms_query_destroy(q);
    return err;
}
