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

#ifndef __DBMS_QUOTA_H__
#define __DBMS_QUOTA_H__

#include "dav_repos.h"

dav_error *dbms_insert_quota(apr_pool_t *pool, const dav_repos_db *d,
                             long principal_id, unsigned long quota);

dav_error *dbms_get_available_bytes(apr_pool_t *pool, const dav_repos_db *d,
                                    long owner_id, long *num_avail_bytes);

#endif
