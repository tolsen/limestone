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

#ifndef __REDIRECT_H__
#define __REDIRECT_H__

#include "dbms_redirect.h"
#include "bridge.h"     /* for sabridge_insert_resource */

dav_error *dav_repos_create_redirectref(dav_resource *resource,
                                        const char *reftarget,
                                        dav_redirectref_lifetime t);

dav_error *dav_repos_update_redirectref(dav_resource *resource,
                                        const char *reftarget,
                                        dav_redirectref_lifetime t);

#endif /* __REDIRECT_H__ */
