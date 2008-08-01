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

#ifndef DAV_REPOS_DBMS_LOCKS_H
#define DAV_REPOS_DBMS_LOCKS_H

#include "dbms.h"
#include "dbms_bind.h" /* for dbms_bind_list */
#include "lock.h"

/* @brief Gets all the locks on a resource
 * @param lockdb The locks database
 * @param db_r The resource whose locks are fetched
 * @param resolve_indirect Flag to indicate whether indirect locks should be
 *                         resolved to their lockroot
 * @param locks The retrieved locks are returned through this pointer
 * @return NULL on success, error otherwise
 */
dav_error *dbms_get_locks(dav_lockdb *lockdb,
                          dav_repos_resource *db_r,
                          int resolve_indirect,
                          dav_lock **locks);

/* @brief Retrieves all the properties of the lock given the locktoken uuid and
          a resource on which this lock applies (direct or indirect)
 * @param lockdb The lock database
 * @param db_r The resource on which this lock applies directly or indirectly
 * @param locktoken The locktoken uuid
 * @param lock Used to return the retrieved lock
 * @return NULL on success, error otherwise
 */
dav_error *dbms_find_lock_by_token(dav_lockdb *lockdb, dav_repos_resource *db_r,
                                   const dav_locktoken *locktoken, dav_lock **lock);

/* @brief Deletes all the locks identified by their ids in the string. This function
          will at some point take care of checkin_on_unlock.
 * @param lockdb The locks database
 * @param exp_lock_ids A comma seperated string of the lockids
 * @return NULL on success, error otherwise
 */
dav_error *dbms_delete_exp_locks(dav_lockdb *lockdb, const char *exp_lock_ids);

/* @brief Checks whether the resource has any locks, direct or indirect
 * @param lockdb The locks database
 * @param db_r The resource to verify
 * @param locks_present Used to signal to caller whether any locks were found
 * @return NULL on success, error otherwise
 */
dav_error *dbms_resource_has_locks(dav_lockdb *lockdb,
                                   dav_repos_resource *db_r,
                                   int *locks_present);

/* @brief Add all the given locks to a resource as indirect locks
 * @param lockdb The locks database
 * @param db_r The resource to which the indirect locks will be added
 * @param lock A chain of locks
 * @return NULL on success, error otherwise
 */
dav_error *dbms_add_indirect_locks(dav_lockdb *lockdb,
                                   dav_repos_resource *db_r,
                                   const dav_lock *lock);

dav_error *dbms_add_indirect_locked_children(dav_lockdb *lockdb,
                                             dav_repos_resource *children,
                                             const dav_lock *lock);

/* @brief Insert a lock into the database
 * @param lockdb The locks database
 * @param db_r The lockroot
 * @param lock has the locktoken element
 * @return NULL on success, error otherwise
 */
dav_error *dbms_insert_lock(dav_lockdb *lockdb,
                            dav_repos_resource *db_r,
                            const dav_lock *lock);

/* @brief Associates with the lock all the binds in its lockroot
 * @param lockd The locks database
 * @param lock The lock
 * @param bind_list array of binds involved in the lockroot
 * @param size number of elements in the array
 * @return NULL on success, error otherwise
 */
dav_error *dbms_insert_binds_locks(dav_lockdb *lockdb, const dav_lock *lock,
                                   dbms_bind_list bind_list[], int size);

/* @brief Deletes a lock from the database
 * @param The resource on which the lock is present
 * @param locktoken The locktoken of the lock
 * @return NULL on success, error otherwise
 */
dav_error *dbms_remove_lock(dav_lockdb *lockdb, 
                            const dav_locktoken *locktoken);

/* @brief Removes all the locks with lockroots that have given prefix
 * @param lockdb The locks database
 * @param lockroot_prefix The lockroot prefix used to locate the locks to be
 *        removed
 * @return NULL on success, error otherwise 
 */
dav_error *dbms_remove_direct_locks_w_prefix(dav_lockdb *lockdb, 
                                             const char *lockroot_prefix);

/* @brief Frees all the resources that are indirectly locked by this lock
 * @param lockdb The locks database
 * @param lock The lock
 * @return NULL on success, error otherwise
 */
dav_error *dbms_remove_indirect_lock(dav_lockdb *lockdb,
                                     const dav_lock *lock,
                                     const dav_repos_resource *db_r);

/* @brief Refreshes a list of locks and sets the new expire time
 * @param lockdb The locks database
 * @param db_r The resource on which the locks exist
 * @param ltl The list of lock tokens
 * @param new_time The new time at which the locks should expire
 * @param locks Returns the newly refreshed locks
 */
dav_error *dbms_refresh_lock(dav_lockdb *lockdb,
                             dav_repos_resource *db_r,
                             dav_lock *lock, 
                             time_t new_time);

dav_error *dbms_get_locks_through_bind(dav_lockdb* lockdb,
                                       dav_repos_resource *coll,
                                       const dbms_bind_list *bind,
                                       dav_lock **p_locks);

dav_error *dbms_get_locks_not_directly_through_binds(dav_lockdb *lockdb,
                                                     dav_repos_resource *db_r,
                                                     dbms_bind_list *bind_list,
                                                     dav_lock **p_locks);

#endif /* DAV_REPOS_DBMS_LOCKS_H */
