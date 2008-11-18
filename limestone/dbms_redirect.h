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

#ifndef DBMS_REDIRECT_H
#define DBMS_REDIRECT_H

#include <apr_strings.h>
#include <mod_dav.h>
#include "dbms.h"
#include "dbms_api.h"

dav_error *dbms_insert_redirectref(const dav_repos_db *db,
                                   dav_repos_resource *db_r,
                                   const char *reftarget,
                                   dav_redirectref_lifetime t);

dav_error *dbms_update_redirectref(const dav_repos_db *db,
                                   dav_repos_resource *db_r,
                                   const char *reftarget,
                                   dav_redirectref_lifetime t);

#endif /* DBMS_REDIRECT_H */
