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

#include "deltav_util.h"

#include <apr_strings.h>

/* @brief make a version resource uri using checkin number   
 * @param db_r contains the uuid, root_path and the pool 
 * @param vnum version number 
 * @param is_history Is it a version history resource.    
 */
char *sabridge_mk_version_uri(dav_repos_resource *vhr, int vnum)
{
    return apr_psprintf(vhr->p, "%s/history/%ld/%d", 
                        vhr->root_path, vhr->serialno, vnum);
}

char* sabridge_mk_vhr_uri(dav_repos_resource *vhr)
{
    return apr_psprintf(vhr->p, "%s/history/%ld/vhr", 
                        vhr->root_path, vhr->serialno);
}

