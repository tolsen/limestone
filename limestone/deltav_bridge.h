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

#ifndef DELTAV_BRIDGE_H
#define DELTAV_BRIDGE_H
#include "dbms.h"

dav_error *sabridge_vsn_control(const dav_repos_db *db,
                                dav_repos_resource *db_r);

dav_error *sabridge_mk_new_version(const dav_repos_db *db, 
                                   dav_repos_resource *vcr, 
                                   dav_repos_resource *vhr, int vr_num,
                                   dav_repos_resource **new_vr);

dav_error *sabridge_mk_vhr(const dav_repos_db *db, dav_repos_resource *db_r,
                           dav_repos_resource **vhr);

dav_error *sabridge_mk_versions_collection(const dav_repos_db *db,
                                           dav_repos_resource *db_r);

/* Build version property */
void sabridge_build_vpr_hash(dav_repos_db * db,
                             dav_repos_resource * db_r);

dav_error *sabridge_remove_vhr(const dav_repos_db *d, 
                               dav_repos_resource *vhr);

#endif /* DELTAV_BRIDGE_H */
