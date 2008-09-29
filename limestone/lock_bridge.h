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

#ifndef SAB_LOCK_H
#define SAB_LOCK_H

#include "dbms.h"

/* @brief Removes all the indirect locks on the resource and all its children
 *        that will vanish when resource uri is unbound.
 * @param lockdb The locks database
 * @param db_r The resource
 * @return NULL on success, error otherwise
 */
dav_error *sabridge_remove_indirect_locks_d_inf(dav_lockdb *lockdb,
                                                dav_repos_resource *db_r);

/* @brief Identifies whether the client supports locknull resources or
 *        locked empty resources
 * @param r The request record which contains the client User-Agent
 * @return 1 if the client requires locknull resources, 0 otherwise
 */
int use_locknull(request_rec *r);

#endif /* SAB_LOCK_H */
