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

#ifndef __dav_repos_bind_H
#define __dav_repos_bind_H

#include <mod_dav.h>
/*
dav_error *dav_repos_unbind_resource(dav_resource *resource,
                                     const dav_resource *base_resource, 
                                     const char *segment);
*/
dav_error *dav_repos_rebind_resource(const dav_resource *collection,
                                     const char *segment,
                                     dav_resource *href_res,
                                     dav_resource *new_bind);
/*
dav_error *dav_repos_bind_resource(const dav_resource *resource,
                                   dav_resource *binding,
                                   dav_resource *binding_parent,
                                   const char *segment);
*/
#endif
