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

#ifndef DBMS_DELTAV_H
#define DBMS_DELTAV_H

#include "dbms.h"

/* DeltaV functions */
dav_error *dbms_insert_vhr(const dav_repos_db *db, dav_repos_resource *db_r);

dav_error *dbms_update_vhr(const dav_repos_db *db, dav_repos_resource *db_r);

dav_error *dbms_insert_version(const dav_repos_db * db,
                               dav_repos_resource * vr);

dav_error *dbms_insert_vcr(const dav_repos_db *db, dav_repos_resource *vcr);

dav_error *dbms_set_checkin_out(const dav_repos_db * db, 
                                dav_repos_resource * r,
                                dav_resource_checked_state checked_state,
                                int version_num);

dav_error *dbms_set_autoversion_type(const dav_repos_db * db,
                                     dav_repos_resource * r,
                                     dav_repos_autoversion_t av_type);

dav_error *dbms_set_checkin_on_unlock(const dav_repos_db *db,
                                      dav_repos_resource *db_r);

dav_error *dbms_get_deltav_props(const dav_repos_db *d, 
                                 dav_repos_resource *r);

dav_error *dbms_get_vcr_props(const dav_repos_db *d, dav_repos_resource *r);

dav_error *dbms_get_version_resource_props(const dav_repos_db *d, dav_repos_resource *r);

dav_error *dbms_get_vhr_props(const dav_repos_db *d, dav_repos_resource *r);

dav_error *dbms_get_vhr_root_version_id(const dav_repos_db *d,
                                 dav_repos_resource *vhr);

dav_error *dbms_get_version_number(const dav_repos_db *d, dav_repos_resource *r);

dav_error *dbms_get_version_id(const dav_repos_db *d, dav_repos_resource *r,
                               int version_num, int *version_id);

dav_error *dbms_get_vcr_versions(const dav_repos_db * db,
                                 dav_repos_resource * vcr,
                                 dav_repos_resource ** vrs,
                                 dav_repos_resource **pvrs_tail);

dav_error *dbms_copy_resource_collection_version(const dav_repos_db * db,
					  dav_repos_resource * r_src,
					  dav_repos_resource * r_dst,
					  request_rec * rec);

dav_error *dbms_restore_vcc(const dav_repos_db *db,
                            dav_repos_resource *vcc,
                            dav_repos_resource *cvr);

dav_error *dbms_get_vhr_versions(const dav_repos_db *d,
                                 dav_repos_resource *vhr,
                                 dav_repos_resource **pversions);

dav_error *dbms_unversion_vhr_vcrs(const dav_repos_db *d,
                                   dav_repos_resource *vhr);

#endif /* DBMS_DELTAV_H */
