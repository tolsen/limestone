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

#ifndef PROPS_H
#define PROPS_H

#include <httpd.h>
#include <http_protocol.h>
#include <http_log.h>
#include <http_core.h>		/* for ap_construct_url */
#include <mod_dav.h>

#include <apr_strings.h>
#include <apr_hash.h>
#include <apr_dbm.h>
#include <apr_file_io.h>

#include "dav_repos.h"
#include "dbms.h"
#include "util.h"

/* @brief Build dead property hash  
 * Build the hash before makeresponse
 * @param db DB connection struct containing the user, password, and DB name
 * @param db_r contains the uuid, root_path and the pool 
 */
void dav_repos_build_pr_hash(dav_repos_db * db, dav_repos_resource * db_r);

/* @brief insert lock info for search and prop. 
 * @param params The walker parameters
 * @param db_r The resource
 * @return NULL on success, error otherwise
 */
dav_error *dav_repos_insert_lock_prop(const dav_walk_params * params,
				      dav_repos_resource * db_r);

#endif /* PROPS_H */
