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

#ifndef DELTAV_UTIL_H
#define DELTAV_UTIL_H

#include "dbms.h"

char *sabridge_mk_version_uri(dav_repos_resource *vcr, int vnum);

char* sabridge_mk_vhr_uri(dav_repos_resource *vhr);


#endif /* DELTAV_UTIL_H */
