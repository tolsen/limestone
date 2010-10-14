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

#include "dbms_acl.h"
#include "bridge.h"     /* for sabridge_reverse_lookup */
#include <stdlib.h>

#define SUPER_USER_ID   1

/** 
 * Returns the principal name corresponding to a principal-id 
 * @param d DB connection struct containing the user, password, and DB name. 
 * @param r The resource on wich the principal will get from. 
 * @param principal_id The id of the principal. 
 * @return principal-url on success, NULL otherwise. 
 */
const char *dbms_lookup_prin_id(apr_pool_t *pool,
                                const dav_repos_db * d,
	   		        int principal_id)
{
    dav_repos_query *q = NULL;
    const char *prin_name;
    int ierrno = 0;
    TRACE();

    q = dbms_prepare(pool, d->db,
		     "SELECT name FROM principals WHERE resource_id = ?");
    dbms_set_int(q, 1, principal_id);
    dbms_execute(q);

    if ((ierrno = dbms_next(q)) <= 0) {
	dbms_query_destroy(q);
	return NULL;
    }

    prin_name = dbms_get_string(q, 1);
    dbms_query_destroy(q);

    return prin_name;
}

/**
 * Get all principals in the ACL for current resource
 * @param db DB connection struct
 * @param pool The pool to allocate from
 * @param resource_id id of the resource
 * @param principals The linked list of principals
 */
int dbms_get_principals(const dav_repos_db *db, apr_pool_t *pool,
			dav_repos_resource *r, dav_repos_resource *principals)
{
    dav_repos_query *q = NULL;

    q = dbms_prepare(pool, db->db, 
                     "SELECT DISTINCT name"
                     " FROM principals INNER JOIN aces"
		      " ON aces.principal_id = principals.resource_id "
                     "WHERE aces.resource_id = ?");
    dbms_set_int(q, 1, r->serialno);
    dbms_execute(q);

    int i, nresults;
    nresults = dbms_results_count(q);
    char ***results = apr_pcalloc(pool, nresults*sizeof(char **));
    for(i=0; i<nresults; i++) {
        results[i] = dbms_fetch_row_num(db->db, q, pool, i);
    }
    dbms_query_destroy(q);

    for(i=0; i<nresults; i++) {
        dav_principal *cur_principal = 
            dav_repos_get_prin_by_name(r->resource->info->rec, results[i][0]);
        if(cur_principal->resource) {
            dav_repos_resource *iter = cur_principal->resource->info->db_r;
            iter->next = principals;
            principals = iter;
        }
    }

    return 0;
}

/**
 * Get all protected, non-inherited ACEs for current resource
 * @param d DBMS connection struct
 * @param r Current resource
 * @return HTTP status for the operation
 */
int dbms_get_protected_aces(const dav_repos_db *d, dav_repos_resource *r, 
                            dav_acl **acl)
{
    dav_repos_query *q = NULL;
    apr_pool_t *pool = r->p;
    request_rec *rec = r->resource->info->rec;
    const char *prin_name = dbms_lookup_prin_id(r->p, d, r->owner_id);
    const dav_principal *owner_principal = 
        dav_repos_get_prin_by_name(rec, prin_name);
    int retVal = OK;
    
    TRACE();
    
    *acl = dav_acl_new(pool, r->resource, owner_principal, owner_principal);

    q = dbms_prepare(pool, d->db,
                     "SELECT principals.name, grantdeny, privilege_id, "
                        "namespaces.name, property_name "
                        "FROM aces "
                     "LEFT JOIN namespaces "
                        "ON namespaces.id = property_namespace_id "
                     "INNER JOIN principals "
                        "ON aces.principal_id = principals.resource_id "
                     "INNER JOIN dav_aces_privileges "
                        "ON dav_aces_privileges.ace_id = aces.id "
                     "WHERE aces.resource_id = ? AND protected = 't'");
    
    dbms_set_int(q, 1, r->serialno);
    if(dbms_execute(q))
        return HTTP_INTERNAL_SERVER_ERROR;

    int i, nresults;
    nresults = dbms_results_count(q);
    char ***results = apr_pcalloc(pool, nresults*sizeof(char **));
    for(i=0; i<nresults; i++) {
        results[i] = dbms_fetch_row_num(d->db, q, pool, i);
    }

    dbms_query_destroy(q);

    for(i=0; i<nresults; i++) {
        int is_deny = (strcmp(results[i][1], ACL_GRANT)) ? 1 : 0;
        dav_ace *ace;

        const dav_principal *principal;
        dav_privileges *privileges = dav_privileges_new(pool);
        const dav_privilege *privilege =
            dav_privilege_new_by_type(pool, atoi(results[i][2]));
        dav_add_privilege(privileges, privilege);
        principal = dav_repos_get_prin_by_name(r->resource->info->rec, 
                                               results[i][0]);

        char *ns = results[i][3];
        char *name = results[i][4];
        dav_prop_name *property = NULL;

        if(name && apr_strnatcmp(name, "")) 
            property = dav_ace_property_new(pool, ns, name);

        ace = dav_ace_new(pool, principal, property, privileges, is_deny,
                          NULL /*inherited*/, 1 /*is_protected*/ );

        if(ace)
            dav_add_ace(*acl, ace);
    }

    return retVal;
}
    
/** 
 * Deletes all the non-protected, non-inherited ACEs of the resource 
 * @param d DB connection struct containing the user, password, and DB name. 
 * @param r The resource of wich the ACL should be deletet. 
 * @return The success status of the operation. 
 */
int dbms_delete_own_aces(const dav_repos_db * d, dav_repos_resource * r)
{
    int retVal = OK;
    dav_repos_query *q = NULL;
    apr_pool_t *pool = r->p;
    TRACE();

    q = dbms_prepare(pool, d->db,
		     "DELETE FROM aces WHERE resource_id = ? AND protected = 'f'");
    dbms_set_int(q, 1, r->serialno);
    if(dbms_execute(q))
        retVal = HTTP_INTERNAL_SERVER_ERROR;
    dbms_query_destroy(q);
    return retVal;
}

/** 
 * Adds a additional privilege to a existing ace. 
 * @param d DB connection struct containing the user, password, and DB name. 
 * @param r The resource on wich the ace depends. 
 * @param privilege The new privilege for the ace. 
 * @param ace_id The id of the ace to wich the new privilege will be added. 
 * @return The success status of the operation. 
 */
int dbms_add_ace_privilege(const dav_repos_db * d,
			   const dav_repos_resource * r,
			   const dav_privilege * privilege, int ace_id)
{
    int retVal = OK;
    dav_repos_query *q = NULL;

    TRACE();

    int privilege_id = dbms_get_privilege_id(d, r, privilege);
    if(privilege_id < 1)
        /* Unknown privilege */
        return HTTP_FORBIDDEN;

    q = dbms_prepare(r->p, d->db,
		     "INSERT INTO dav_aces_privileges(ace_id, privilege_id) "
                     "VALUES(?,?)");
    dbms_set_int(q, 1, ace_id);
    dbms_set_int(q, 2, privilege_id);
    dbms_execute(q);
    dbms_query_destroy(q);
    return retVal;
}

/** 
 * Adds a new ACE to a given ACL. 
 * @param d DB connection struct containing the user, password, and DB name. 
 * @param r The resource on wich the ACL depends. 
 * @param privilege The new ACE for the ACL. 
 * @param acl_id The id of the ACL to wich the new ACE will be added. 
 * @return The ace_id. 
 */
dav_error *dbms_add_ace(const dav_repos_db * d, dav_repos_resource * r,
	                const dav_ace * ace, int acl_id)
{
    int is_deny;
    const dav_principal *ace_principal;
    const dav_privileges *ace_privileges;
    const dav_prop_name *ace_property;
    const char *ace_inherited;
    dav_repos_query *q = NULL;
    dav_privilege_iterator *iter;
    dav_error *err = NULL;
    apr_pool_t *pool = r->p;
    int is_protected;
    long ns_id = 0;
    int retVal = OK;
    long principal_id = 0;
    TRACE();

    is_deny = dav_is_deny_ace(ace);
    is_protected = dav_is_protected_ace(ace);
    ace_principal = dav_get_ace_principal(ace);
    ace_privileges = dav_get_ace_privileges(ace);
    ace_inherited = dav_get_ace_inherited(ace);
    ace_property = dav_get_ace_property(ace);
    if(ace_property && ace_property->ns)
        dbms_get_ns_id(d, r, ace_property->ns, &ns_id);

    principal_id = dav_repos_get_principal_id(ace_principal);
    
    q = dbms_prepare(pool, d->db,
		     "INSERT INTO aces(grantdeny, resource_id, principal_id, protected, "
                                      "property_namespace_id, property_name) "
                     "VALUES(?, ?, ?, ?, ?, ?) RETURNING id");

    dbms_set_string(q, 1, is_deny ? ACL_DENY : ACL_GRANT);
    dbms_set_int(q, 2, r->serialno);
    dbms_set_int(q, 3, principal_id);
    dbms_set_string(q, 4, is_protected ? DB_TRUE : DB_FALSE);
    dbms_set_int(q, 5, ns_id);
    dbms_set_string(q, 6, ace_property ? ace_property->name : NULL);
    dbms_execute(q);
    dbms_next(q);
    long ace_id = dbms_get_int(q, 1);

    dbms_query_destroy(q);

    if(!ace_id) {
	return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "DBMS error while inserting into 'aces'");
    }
        
    iter = dav_privilege_iterate(ace_privileges);
    while (dav_privilege_iterator_more(iter)) {
	const dav_privilege *privilege = dav_privilege_iterator_next(iter);
	if((retVal = dbms_add_ace_privilege(d, r, privilege, ace_id)) != OK)
            return dav_new_error_tag(pool, retVal, 0, NULL, NULL, 
                                     "not-supported-privilege", NULL, NULL);
    }

    return err;
}

/** 
 * Gets the ACL of a resource. 
 * @param d DB connection struct containing the user, password, and DB name. 
 * @param r The resource which acl should be get. 
 * @param resource The resource which acl should be get. 
 * @param acl pointer to a null-terminated acl struct. 
 * @return success state of the function. 
 */
int dbms_get_acl(const dav_repos_db * d, dav_repos_resource * r)
{
    dav_repos_query *q = NULL;
    apr_pool_t *pool = r->p;
    int owner_id;
    int group_id;
    dav_principal *owner_principal;
    dav_principal *group_principal;
    char ***results;

    TRACE();
    
    owner_id = r->owner_id;
    group_id = owner_id;	//FIXME 
    const char *prin_name = dbms_lookup_prin_id(r->p, d, owner_id);
    owner_principal = 
        dav_repos_get_prin_by_name(r->resource->info->rec, prin_name);

    group_principal = owner_principal;	//FIXME 
    dav_acl *acl  = dav_acl_new(pool, r->resource, owner_principal, 
                                group_principal);
    
    q = dbms_prepare(pool, d->db,
                     /* select the ACE info we need */
                     "SELECT principals.name, namespaces.name, property_name, "
                        "grantdeny, privilege_id, protected, "
                        "inherited_res.resource_id, aces.id "
                        "FROM aces "

                     /* alongwith privileges */
                     "INNER JOIN dav_aces_privileges "
                        "ON aces.id = dav_aces_privileges.ace_id "
                     
                     /* and principals */
                     "INNER JOIN principals "
                        "ON aces.principal_id = principals.resource_id "
                     
                     /* get all inherited ACE's */   
                     "INNER JOIN "
                     "(SELECT par_res.resource_id AS resource_id, "
                        "par_res.path AS par_res_path "
                        "FROM acl_inheritance AS par_res "
                        "INNER JOIN acl_inheritance AS chi_res "
                        "ON par_res.resource_id = "
                          "ANY(CAST(string_to_array(chi_res.path, ',') "
                             "AS INTEGER[])) "
                        "WHERE chi_res.resource_id = ? ) inherited_res "
                      "ON aces.resource_id = inherited_res.resource_id "

                     /* resolve namespace id's */
                     "LEFT JOIN namespaces "
                        "ON namespaces.id = property_namespace_id "

                     /* non-inherited ACE's first, then inherited ACEs 
                      * in reverse order of inheritance 
                      * ( farthest parent first ) */
                     "ORDER BY protected DESC, CHAR_LENGTH(par_res_path) DESC, aces.id");
    dbms_set_int(q, 1, r->serialno);
    dbms_execute(q);

    dav_privileges *privileges;
    char *ns, *name; 
    int prev_ace_id = -1, ace_pending = 0, i, ace_id;
    dav_ace *ace = NULL;
    const dav_principal *principal;
    const dav_privilege *privilege;
    char *inherited = NULL;
    dav_prop_name *property = NULL;
    int is_inherited, is_protected, is_deny;

    int nresults = dbms_results_count(q);
    results = apr_pcalloc(pool, nresults*sizeof(char **));
    for(i=0; i<nresults; i++) 
        results[i] = dbms_fetch_row_num(d->db, q, pool, i);

    dbms_query_destroy(q);

    for(i=0; i<nresults; i++) {
        ace_id = atoi(results[i][7]);
        if(prev_ace_id != ace_id) {
            prev_ace_id = ace_id;
            if (ace_pending) {
                ace = dav_ace_new(pool, principal, property, privileges, is_deny,
                                  inherited, is_protected);
                dav_add_ace(acl, ace);
            }
            privileges = NULL;
            inherited = NULL;
            property = NULL;
        }
        ns = results[i][1];
        name = results[i][2];
        if(name && apr_strnatcmp(name,"")) 
            property = dav_ace_property_new(pool, ns, name);

	is_deny = (strcmp(results[i][3], ACL_GRANT)) ? 1 : 0;

	principal = dav_repos_get_prin_by_name(r->resource->info->rec, 
                                               results[i][0]);

	if (!privileges) {
            privileges = dav_privileges_new(pool);
        }

	privilege = dav_privilege_new_by_type(pool, atoi(results[i][4]));
	dav_add_privilege(privileges, privilege);
        
        int parent_id = atoi(results[i][6]);
        is_inherited = (parent_id != r->serialno);
        is_protected = (strcmp(results[i][5], DB_TRUE) == 0); 

        if(is_inherited) {
            dav_repos_resource *parent_dbr = NULL;
            sabridge_new_dbr_from_dbr(r, &parent_dbr);
            parent_dbr->serialno = parent_id;
            sabridge_reverse_lookup(d, parent_dbr);
            inherited = parent_dbr->uri;
        }

        ace_pending = 1;
    }
    
    if (ace_pending) {
        ace = dav_ace_new(pool, principal, property, privileges, is_deny,
                          inherited, is_protected);
        dav_add_ace(acl, ace);
    }

    r->acl = acl;
    return 1;
}

static const char *make_member_query(apr_pool_t *pool, long p_id, int order)
{
    return apr_psprintf(pool, 
                        " SELECT %d AS p_id, transitive_group_id AS group_id"
                         " FROM transitive_group_members"
                         " WHERE transitive_member_id = %ld"
                        " UNION (SELECT %d, %ld)", order, p_id, order, p_id);
}

/**
 * Check permissions of a principal on a given (resource, privilege)
 * @param db DB connection struct
 * @param privilege Name of the privilege
 * @param principal Name of the principal
 * @param resource_id of the resource
 * @return TRUE if granted
 */
int dbms_is_allow(const dav_repos_db * db, long priv_ns_id, 
                  const char *privilege, const dav_principal *principal, 
                  dav_repos_resource *r)
{
    int retVal = FALSE;
    dav_repos_query *q = NULL;
    long p_id = dav_repos_get_principal_id(principal);
    const dav_principal *iter = principal;
    apr_pool_t *pool = r->p;

    TRACE();

    /* NOTE: short circuiting ACL checks for super user */
    if(p_id == SUPER_USER_ID) {
        return TRUE;
    }

    /* first look in cache */
    if(r->ace_cache && 
        r->ace_cache->principal_id == p_id && 
        r->ace_cache->priv_ns_id == priv_ns_id &&
        strcmp(r->ace_cache->privilege, privilege) == 0) {
        return r->ace_cache->is_allow;
    }

    int order = 1;
    const char *members_query = make_member_query(pool, p_id, order);
    while((iter = iter->next)) {
        order = order + 1;
        p_id = dav_repos_get_principal_id(iter);
        members_query = apr_pstrcat(pool, members_query, " UNION ",
                                    make_member_query(pool, p_id, order), NULL);
    }
    int max_order = order;

    const char *is_allow_query = 
        apr_psprintf(pool, 
		     /* Get the relevant ACE */
                     "SELECT DISTINCT ON (p_id) aces.grantdeny, p_id FROM aces "
                     "INNER JOIN dav_aces_privileges ap "
                     "ON aces.id = ap.ace_id "
                     
                     /* Join with a membership table */
                     "INNER JOIN "
                     "(%s) membership ON aces.principal_id = membership.group_id "

                     /* check the aggregate privileges */
                     "INNER JOIN "
                     "(SELECT par_priv.id AS par_priv_id "
                     "FROM acl_privileges par_priv "
                     "INNER JOIN acl_privileges chi_priv "
                     "ON par_priv.lft <= chi_priv.lft "
                        "AND par_priv.rgt >= chi_priv.rgt "
                     "WHERE chi_priv.name = ? AND chi_priv.priv_namespace_id = ?) privs "
                     "ON ap.privilege_id = privs.par_priv_id "

                     /* join aces to resource in question and any resources it
                      * may inherit ACLs from */
                     "INNER JOIN "
                     "(SELECT par_res.resource_id AS resource_id, "
                        "par_res.path AS par_res_path "
                        "FROM acl_inheritance AS par_res "
                        "INNER JOIN acl_inheritance AS chi_res "
                        "ON par_res.resource_id = "
                          "ANY(CAST(string_to_array(chi_res.path, ',') "
                             "AS INTEGER[])) "
                        "WHERE chi_res.resource_id = ? ) inherited_res "
                      "ON aces.resource_id = inherited_res.resource_id "

                     /* give priority to own aces over those inherited
                      * then protected aces, and to aces higher in the list
                        submitted by the client(translates to a lower id) */
                     "ORDER BY p_id, protected DESC, CHAR_LENGTH(par_res_path) DESC,"
                     " id ", members_query);

    q = dbms_prepare(pool, db->db, is_allow_query);
    dbms_set_string(q, 1, privilege);
    dbms_set_int(q, 2, priv_ns_id);
    dbms_set_int(q, 3, r->serialno);

    if (dbms_execute(q)) {
	dbms_query_destroy(q);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    order = 1;
    iter = principal;
    int next_order = 0;
    int next_is_allow = 0;
    while(iter) {
        p_id = dav_repos_get_principal_id(iter);
        if (next_order < order) {
            if (dbms_next(q) > 0) {
                next_order = dbms_get_int(q, 2);
                next_is_allow = (strcmp(dbms_get_string(q, 1), ACL_GRANT) == 0);
                if (next_order == 1) {
                    retVal = next_is_allow;
                }
            }
            else {
                next_order = max_order + 1;
            }
        }

        r->ace_cache = apr_pcalloc(pool, sizeof(ace_cache_t));
        r->ace_cache->principal_id = p_id;
        r->ace_cache->priv_ns_id = priv_ns_id;
        r->ace_cache->privilege = privilege;

        if (next_order == order) {
            r->ace_cache->is_allow = next_is_allow;
        }

        order = order + 1;
        iter = iter->next;
    }

    dbms_query_destroy(q);
    return retVal;
}

/**
 * Get all privileges for given (user, resource)
 * @param db DB connection struct
 * @param pool The pool to allocate from
 * @param user The name of the user
 * @param resource_id id of the resource
 */
dav_privileges *dbms_get_privileges(const dav_repos_db * db,
				    apr_pool_t * pool, long principal_id,
				    long resource_id)
{
    dav_privileges *privileges = dav_privileges_new(pool);
    dav_repos_query *q = NULL;

    q = dbms_prepare
      (pool, db->db,
       "SELECT privilege_namespace, privilege_name"
       " FROM ("
               "SELECT namespaces.name AS privilege_namespace, "
               "       outer_priv.name AS privilege_name, ("
                       "SELECT aces.grantdeny"
                       " FROM aces INNER JOIN dav_aces_privileges ap"
                                   " ON aces.id = ap.ace_id "
                                   "INNER JOIN ("
                                                "("
                                                  "SELECT transitive_group_id"
                                                  " AS group_id"
                                                  " FROM transitive_group_members"
                                                  " WHERE transitive_member_id=?"
                                                  ") "
                                                "UNION (SELECT ?)"
                                                ") membership"
                                   " ON aces.principal_id = membership.group_id "
                                   "INNER JOIN ("
                                                "SELECT par_priv.id AS par_priv_id,"
                                                " chi_priv.id AS chi_priv_id"
                                                " FROM acl_privileges par_priv "
                                                     "INNER JOIN"
                                                     " acl_privileges chi_priv"
                                                     " ON par_priv.lft <= chi_priv.lft"
                                                     " AND par_priv.rgt >= chi_priv.rgt"
                                                     ") privs"
                                   " ON ap.privilege_id = privs.par_priv_id "
                                   "INNER JOIN "
                                   "(SELECT par_res.resource_id AS resource_id, "
                                      "par_res.path AS par_res_path "
                                      "FROM acl_inheritance AS par_res "
                                      "INNER JOIN acl_inheritance AS chi_res "
                                      "ON par_res.resource_id = "
                                         "ANY(CAST(string_to_array(chi_res.path, ',') "
                                           "AS INTEGER[])) "
                                      "WHERE chi_res.resource_id = ? ) inherited_res "
                                   "ON aces.resource_id = inherited_res.resource_id "
       
                       " WHERE chi_priv_id = outer_priv.id"
                       " ORDER BY protected DESC, CHAR_LENGTH(par_res_path) DESC, id LIMIT 1)"
                       " AS action"
                       " FROM acl_privileges outer_priv "
                              "INNER JOIN "
                              " namespaces "
                              "  ON namespaces.id = outer_priv.priv_namespace_id"
               ") AS final_priv"
       " WHERE action = 'G'");
    dbms_set_int(q, 1, principal_id);
    dbms_set_int(q, 2, principal_id);
    dbms_set_int(q, 3, resource_id);

    dbms_execute(q);

    while (dbms_next(q) > 0) {
	const dav_privilege *privilege =
          dav_privilege_new_by_name(pool, dbms_get_string(q, 1), dbms_get_string(q, 2));
	dav_add_privilege(privileges, privilege);
    }
    dbms_query_destroy(q);

    return privileges;
}

/**
 * Add a (resource, parent) entry into acl_inheritance table,
 * if not already done.
 * @param d DBMS connection struct
 * @param db_r The child resource
 * @param parent Resource_id of the parent
 */
dav_error *dbms_inherit_parent_aces(const dav_repos_db *d, 
                                    const dav_repos_resource *db_r, 
                                    int parent)
{
    dav_error *err = NULL;
    dav_repos_query *q = NULL;
    apr_pool_t *pool = db_r->p;

    TRACE();

    /* check if the resource is already in acl_inheritance */
    q = dbms_prepare(pool, d->db, "SELECT path FROM acl_inheritance"
                                  " WHERE resource_id = ?");
    dbms_set_int(q, 1, db_r->serialno);
    if(dbms_execute(q)) {
        dbms_query_destroy(q);
        return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "DBMS Error during select "
                             "on acl_inheritance");
    }

    if(dbms_next(q) > 0) {
        /* resource already a part of acl_inheritance, nothing to do */
        dbms_query_destroy(q);
        return err;
    }
    dbms_query_destroy(q);

    /* Insert the new entry (resource, parent) with proper lft & rgt values */
    q = dbms_prepare(pool, d->db,
                     "INSERT INTO acl_inheritance (resource_id, path)"
                     " SELECT ?, a.path || ',' || ?"
                     " FROM acl_inheritance a WHERE a.resource_id = ?");
    dbms_set_int(q, 1, db_r->serialno);
    dbms_set_int(q, 2, db_r->serialno);
    dbms_set_int(q, 3, parent);

    if(dbms_execute(q))
        err =  dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "DBMS Error during update "
                             "on acl_inheritance");

    dbms_query_destroy(q);

    /* Set the private members of dav_acl */
    if(db_r->acl) {
        dav_acl_private *priv = apr_pcalloc(pool, sizeof(*priv));
        priv->parent_id = parent;
        db_r->acl->info = priv;
    }

    return err;
}

/**
 * Update the principal-property ace with the new principal value of 
 * the property
 * @param d database handle
 * @param db_r resource
 * @prop_ns_id the namespace id of the property
 * @prop_name the property name
 * @principal_id serialno of the new principal
 */
dav_error *dbms_update_principal_property_aces(dav_repos_db *d, 
                                               dav_repos_resource *db_r,
                                               int prop_ns_id, 
                                               const char *prop_name, 
                                               int principal_id)
{
    dav_repos_query *q = NULL;
    apr_pool_t *pool = db_r->p;
    dav_error *err = NULL;

    TRACE();

    q = dbms_prepare(pool, d->db, "UPDATE aces SET principal_id = ? "
                     "WHERE resource_id = ? AND property_namespace_id = ? "
                     "AND property_name = ?");
    dbms_set_int(q, 1, principal_id);
    dbms_set_int(q, 2, db_r->serialno);
    dbms_set_int(q, 3, prop_ns_id);
    dbms_set_string(q, 4, prop_name);

    if (dbms_execute(q))
        err =  dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "DBMS Error during update of property aces");

    dbms_query_destroy(q);
    return err;
}

#define AHKS APR_HASH_KEY_STRING

/**
 * Get the privilege id
 * @param d database handle
 * @param pool the pool to allocate from
 * @param privilege the privilege
 * @return privilege_id
 */
int dbms_get_privilege_id(const dav_repos_db *d, const dav_repos_resource *db_r, 
                          const dav_privilege *privilege)
{
    int privilege_id = 0;
    dav_repos_query *q = NULL;
    long priv_ns_id = 0;
    const char *privilege_name = dav_get_privilege_name(privilege);
    apr_pool_t *pool = db_r->p;
    dav_repos_cache *cache = d->cache;
    int *value;

    TRACE();

    sabridge_get_namespace_id(d, db_r, dav_get_privilege_namespace(privilege), 
                            &priv_ns_id);

    const char *key = apr_psprintf(db_r->p, "%ld:%s", priv_ns_id, privilege_name);

    if ((value = (int *)apr_hash_get(cache->privileges, key, AHKS))) {
        return *value;
    }

    q = dbms_prepare(pool, d->db, 
                     "SELECT id FROM acl_privileges WHERE name = ? AND priv_namespace_id = ?");
    dbms_set_string(q, 1, privilege_name);
    dbms_set_int(q, 2, priv_ns_id);
    dbms_execute(q);

    if (dbms_next(q) <= 0) {
	dbms_query_destroy(q);
    } else {
	privilege_id = dbms_get_int(q, 1);
	dbms_query_destroy(q);
    }

    value = apr_pcalloc(cache->pool, sizeof(*value));
    *value = privilege_id;
    apr_hash_set(cache->privileges, key, AHKS, value);

    return privilege_id;
}

/**
 * Change the parent acl of a resource
 * @param d database handle
 * @param db_r the resource
 * @param new_parent_id serialno of the new parent
 * @return NULL for success, dav_error otherwise
 */
dav_error *dbms_change_acl_parent(dav_repos_db *d,
                                  dav_repos_resource *db_r,
                                  int new_parent_id)
{
    dav_repos_query *q = NULL;
    apr_pool_t *pool = db_r->p;
    char *orig_path, *new_parent_path, *new_path; 
    dav_error *err = NULL;

    TRACE();

    /* Get current path */
    q = dbms_prepare(pool, d->db, "SELECT path FROM acl_inheritance"
                                  " WHERE resource_id = ?");
    dbms_set_int(q, 1, db_r->serialno);
    dbms_execute(q);
    if(dbms_next(q) <= 0) {
        dbms_query_destroy(q);
        /* Not currently part of acl_inheritance, just add it */
        return dbms_inherit_parent_aces(d, db_r, new_parent_id);
    } else {
        orig_path = dbms_get_string(q, 1);
        dbms_query_destroy(q);
    }

    /* Get new_parent_path */
    q = dbms_prepare(pool, d->db, "SELECT path FROM acl_inheritance"
                                  " WHERE resource_id = ?");
    dbms_set_int(q, 1, new_parent_id);
    dbms_execute(q);
    if(dbms_next(q) <= 0) {
        dbms_query_destroy(q);
        return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "invalid parent specified in change_acl_parent");
    } else {
        new_parent_path = dbms_get_string(q, 1);
        dbms_query_destroy(q);
    }

    new_path = apr_psprintf(pool, "%s,%ld", new_parent_path, db_r->serialno);

    /* Update path(s) */
    q = dbms_prepare(pool, d->db, "UPDATE acl_inheritance"
                                  " SET path = replace(path, ?, ?)"
                                  " WHERE path LIKE ?"
                                  " OR path = ?");
    dbms_set_string(q, 1, orig_path);
    dbms_set_string(q, 2, new_path);
    dbms_set_string(q, 3, apr_pstrcat(pool, orig_path, ",%", NULL));
    dbms_set_string(q, 4, orig_path);

    if (dbms_execute(q))
        err = dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                            "DBMS Error during update to acl_inheritance "
                            "(moving subtree)");
    dbms_query_destroy(q);

    return err;
}
