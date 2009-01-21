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

#ifndef STONE_ARCH_BRIDGE_H
#define STONE_ARCH_BRIDGE_H

#include "dbms.h"

/** sabridge_insert_resource flags */
#define SABRIDGE_DELAY_BIND     1
#define SABRIDGE_DELAY_ACL      2


/**
 * Searches for a resource in the database and returns its live properties
 * r->uri is the search key; alternatively r->serialno can be the search 
 * key if r->uri is NULL. 
 * @param d DB connection struct containing the user, password, and DB name
 * @param r Out variable to hold the result
 * @return NULL on success, dav_error otherwise
 */
dav_error *sabridge_get_property(const dav_repos_db *d, dav_repos_resource *r);


/* @brief Creates an empty body for a medium resource. This includes creating
 *        a new entry into the media table and an empty file in secondary storage.
 * @param d The database
 * @param r The freshly created resource
 */
dav_error *sabridge_create_empty_body(const dav_repos_db *d,
                                      dav_repos_resource *r);

/** 
 * GET response 
 * @param db The DB connection struct
 * @param db_r The resource which needs to be delivered
 * @param output The response
 * @return NULL if success, dav_error otherwise
 */
dav_error *sabridge_deliver(dav_repos_db *db, dav_repos_resource *db_r,
			ap_filter_t *output);

void sabridge_new_dbr_from_dbr(const dav_repos_resource *db_r, 
                                dav_repos_resource **res_p);

/**
 * Inserts a *new* resource
 * @param d The DB connection struct
 * @param r The resource to insert
 * @param rec The request record
 * @param params Assert none, one or more of 
 * SABRIDGE_DELAY_BIND, SABRIDGE_DELAY_ACL
 * @return NULL on success else dav_error
 */
dav_error *sabridge_insert_resource(const dav_repos_db *d, 
                                    dav_repos_resource *r, 
                                    request_rec *rec, int params);

/**
 * Get the parent resource of a given resource
 * @param db_r Given resource
 * @param pparent The parent pointer
 * @return NULL on success else dav_error
 */
dav_error *sabridge_retrieve_parent(const dav_repos_resource *db_r,
                                    dav_repos_resource **pparent);

/**
 * Create a new collection, including any missing intermediates
 * @param db DB connection struct containing the user, password and DB name
 * @param db_r The resource representing the URI to insert
 * @return NULL on success else dav_error
 */
dav_error *sabridge_mkcol_w_parents(const dav_repos_db *db,
                                    dav_repos_resource *db_r);
/** 
 * Retrieves the children of a collection resource based on depth
 * If a resource has multiple binds among the children of the collection,
 * all the binds are chained using the dav_repos_resource:bind variable
 * 
 * @param db handle to the database
 * @param db_r the collection whose children are to be retrieved
 * @param depth can be 0, 1 or DAV_INFINITY as per the RFC2518
 * @param acl_priv The privilege to filter by
 * @param plink_head The head of the linked list of resources retrieved
 * @param plink_tail The tail of the linked list of resources retrieved
 * @param num_items The number of resources retrieved
 * 
 * @return NULL on success, error otherwise
 */
dav_error *sabridge_get_collection_children(const dav_repos_db *db,
                                            dav_repos_resource *db_r,
                                            int depth,
                                            const char *acl_priv,
                                            dav_repos_resource **plink_head,
                                            dav_repos_resource **plink_tail,
                                            int *num_items);

/**
 * Copy a medium-resource, create the destination if it doesn't exist
 * @param db The DB connection struct
 * @param r_src The resource to copy
 * @param r_dest The destination to copy to
 * @param rec The request record
 * @return NULL on success, error otherwise
 */
dav_error *sabridge_copy_medium_w_create(const dav_repos_db *db,
                                         dav_repos_resource *r_src,
                                         dav_repos_resource *r_dest,
                                         request_rec *rec);
/** 
 * Copies the contents of one medium onto another.
 * 
 * @param db handle to the database
 * @param r_src source medium
 * @param r_dest destination medium
 * 
 * @return NULL on success, error otherwise
 */
dav_error *sabridge_copy_medium(const dav_repos_db *db,
                                const dav_repos_resource *r_src,
                                dav_repos_resource *r_dest);
/** 
 * Creates a new graph that is a copy of the collection at source
 * 
 * @param d handle to the database
 * @param r_src source collection
 * @param r_dst the destination resource
 * @param depth depth to which to copy
 * @param rec has info about the user making the request
 * @param response multi status response to be set
 * 
 * @return NULL on success, error otherwise
 */
dav_error *sabridge_copy_coll_w_create(const dav_repos_db *d,
                                       dav_repos_resource *r_src,
                                       dav_repos_resource *r_dst,
                                       int depth,
                                       request_rec *rec,
                                       dav_response **response);

/**
 * Create a depth infinity copy of a collection resource.
 * @param d The DB connection struct
 * @param r_src The resource to copy
 * @param r_dst The destination to copy to
 * @param rec The request record
 * @return NULL on success, error otherwise
 */
dav_error *sabridge_depth_inf_copy_coll(const dav_repos_db *d,
                                        dav_repos_resource *r_src,
                                        dav_repos_resource *r_dst,
                                        request_rec *rec,
                                        dav_response **p_response);
/** 
 * Removes all the binds of a collection, deleting any orphaned children.
 * As of now, the collection has to be reachable from root for this function
 * to work correctly. This may change in future.
 * 
 * @param d handle to the database
 * @param coll the collection resource
 * 
 * @return NULL on success, error otherwise
 */
dav_error *sabridge_coll_clear_children(const dav_repos_db *d,
                                        dav_repos_resource *coll);
/** 
 * Makes dead properties of dest_id resource the same as dead properties
 * of source_id resource
 * 
 * @param pool 
 * @param d handle to database
 * @param src_id the source resource id
 * @param dest_id the destination resource id
 * 
 * @return NULL on success, error otherwise
 */
dav_error *sabridge_copy_dead_props(apr_pool_t *pool, const dav_repos_db *d,
                                    long src_id, long dest_id);
/** 
 * Makes a new resource and copies the contents(if medium) and dead properties
 * onto it.
 * 
 * @param d handle to the database 
 * @param db_r the resource to copy
 * @param rec contains information about the current user
 * @param copy_parent the ACL parent resource
 * @param pcopy_res will point to the new copy resource
 * 
 * @return NULL on success, error otherwise
 */
dav_error *sabridge_create_copy(const dav_repos_db *d,
                                dav_repos_resource *db_r,
                                request_rec *rec,
                                dav_repos_resource *copy_parent,
                                dav_repos_resource **pcopy_res);
/** 
 * Set the uri field to a path from root
 * 
 * @param d handle to the database
 * @param db_r resource with serialno, pool and root_path
 * 
 * @return NULL on success, error otherwise
 */
dav_error *sabridge_reverse_lookup(const dav_repos_db *d,
                                   dav_repos_resource *db_r);
/** 
 * Deletes *all* the orphaned resources rooted at this resource.
 * If db_r->uri is set, then it is unbound first.
 * 
 * @param db handle to the database
 * @param db_r the resource that should be removed
 * 
 * @return NULL on success, error otherwise
 */
dav_error *sabridge_unbind_resource(const dav_repos_db *db,
                                    dav_repos_resource *db_r);

/**
 * Check if the resource is connected to the root.
 * Delete it if it is no longer reachable.
 * @param d The DB connection struct
 * @param db_r The resource
 * @param deleted Set to 1 if resource was deleted, 0 otherwise
 * @return NULL on success, error otherwise
 */
dav_error *sabridge_delete_if_orphaned(const dav_repos_db *d,
                                       dav_repos_resource *db_r, int *deleted);
/** 
 * Removes all traces of the resource from the database. If the resource has 
 * some files on disk, they are deleted too.
 * 
 * @param db handle to the database
 * @param db_r resource to be deleted
 * 
 * @return NULL on success, error otherwise
 */
dav_error *sabridge_delete_resource(const dav_repos_db *db,
                                    dav_repos_resource *db_r);
/** 
 * Removes the files stored on disk for this resource
 * 
 * @param d file_dir identifies the directory where the files are stored
 * @param db_r resource whose body will be removed
 */
void sabridge_remove_body_from_disk(const dav_repos_db *d,
                                    dav_repos_resource *db_r);
/**
 * Get the parent id of the resource
 * @param db_r The resource
 * @return the parent id of the resource, 0 for failure.
 */
int sabridge_get_parent_id(dav_repos_resource *db_r);
/**
 * Check for ACL preconditions
 * @param acl The new ACL to check for
 * @param acldest The ACEs to check against
 * @return The HTTP status code
 */
int sabridge_acl_check_preconditions(const dav_acl *acl, 
                                     const dav_acl *acldest);
/**
 * Check for overlap given two sets of privileges
 * @param p First set of privileges
 * @param q second set of privileges
 * @return 1 if overlap, 0 otherwise
 */
int sabridge_check_privileges_overlap(const dav_privileges *p, 
                                      const dav_privileges *q);

const char *sabridge_getetag_dbr(const dav_repos_resource * db_r);

void sabridge_get_new_file(const dav_repos_db *db, 
                           dav_repos_resource *db_r, 
                           char **path);

void sabridge_get_resource_file(const dav_repos_db *db,
                                dav_repos_resource *db_r,
                                char **path);

void sabridge_put_resource_file(const dav_repos_db *db, 
                                dav_repos_resource *db_r,
                                char *file);

long sabridge_get_used_bytes(const dav_repos_db *d, dav_repos_resource *r,
                             int prin_only);

#endif /* STONE_ARCH_BRIDGE_H */
