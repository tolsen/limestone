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

#ifndef REPOS_LIVEPROPS_H
#define REPOS_LIVEPROPS_H

#include <mod_dav.h>
#include "dbms.h"

struct dav_liveprop_rollback {
    const dav_liveprop_spec *spec;
    int operation;

    void *rollback_data;

};

/* @brief Gets the liveprop spec with given propid from the array
 * @param lps The array of liveprop specifications of a provider
 * @param propid The propid to search for
 * @return The liveprop spec with given propid
 */
const dav_liveprop_spec *get_livepropspec_from_id(const dav_liveprop_spec lps[],
                                                  int propid);

/* @brief Build live property   
 * @param db_r contains the uuid, root_path and the pool 
 */
void dav_repos_build_lpr_hash(dav_repos_resource * db_r);

/* @brief Return a string of all the liveprop names in the array
 * @param pool The pool to allocate the string from
 * @param lps The array of liveprop specifications
 * @return String listing all the liveprop names
 */
const char *list_all_liveprops(apr_pool_t *pool, const dav_liveprop_spec lps[]);

/* @brief Registers the Basic liveprops
 * @param p Pool
 */
void dav_repos_register_liveprops(apr_pool_t *p);

/* @brief Registers the versioning liveprops
 * @param p Pool
 */
void dav_deltav_register_liveprops(apr_pool_t *p);

/* @brief Registers the Quota and Size liveprops
 * @param p Pool
 */
void dav_quota_register_liveprops(apr_pool_t *p);

/* @brief Registers the ACL liveprops
 * @param p Pool
 */
void dav_acl_register_liveprops(apr_pool_t *p);

/* @brief Registers the supported-*-set liveprops
 * @param p Pool
 */
void dav_supported_register_liveprops(apr_pool_t *p);

/* @brief Registers the binds RFC liveprops
 * @param p Pool
 */
void dav_binds_register_liveprops(apr_pool_t *p);

/* @brief Registers the SEARCH draft liveprops
 * @param p Pool
 */
void dav_search_register_liveprops(apr_pool_t *p);

/* @brief Registers custom LimeBits liveproperties
 * @param p Pool
 */
void dav_limebits_register_liveprops(apr_pool_t *p);

#endif /* REPOS_LIVEPROPS_H */
