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

#ifndef DAV_REPOS_LOCKS_H
#define DAV_REPOS_LOCKS_H

#include "dbms.h"

struct dav_lockdb_private {
    request_rec *r;
    apr_pool_t *pool;
    dav_repos_db *db;
};

struct dav_locktoken {
    char *char_uuid;
    int deleted;
};

struct dav_lock_private {
    long res_id;
    long lock_id;
};

struct dav_lockdb_combined {
    dav_lockdb pub;
    dav_lockdb_private priv;
};

/**
 * This trick is to have the dav_lock variable and its dav_lock_private
 * close to each other in memory
 */
struct dav_lock_combined {
    dav_lock pub;
    dav_lock_private priv;
    dav_locktoken locktoken;
};

#define DAV_VALIDATE_A_LOCK 0x1
#define DAV_VALIDATE_ALL_LOCKS 0x2

dav_lock *dav_repos_alloc_lock(dav_lockdb *lockdb, dav_locktoken *lt);

#endif /* DAV_REPOS_LOCKS_H */
