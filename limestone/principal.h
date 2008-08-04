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

#ifndef __PRINCIPAL__H
#define __PRINCIPAL__H

#include <mod_dav.h>
#include "root_path.h"

#define USER_PATH       PREPEND_ROOT_PATH("/users")
#define GROUP_PATH      PREPEND_ROOT_PATH("/groups")

dav_error *dav_repos_create_user(dav_resource *resource, const char* passwd);

dav_error *dav_repos_update_password(const dav_resource *resource,
                                     const char *passwd);

dav_error *dav_repos_create_group(const dav_resource *resource,
                                  const char *created);

char *get_group_member_set(const dav_resource *group);

#endif  /* #ifndef __PRINCIPAL__H */

