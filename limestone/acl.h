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

/*
 ** ACL method
 */

#ifndef __DAV_ACL_H__
#define __DAV_ACL_H__

#include "dbms_acl.h"
#include "version.h"

#define SUPERUSER_PRINCIPAL_ID  1

/* TODO: generate URI prefix from root_path, 
 * or make it configurable via httpd.conf */
#include "root_path.h"
#define PRINCIPAL_URI_PREFIX           PREPEND_ROOT_PATH("/users/")

dav_principal *dav_repos_get_prin_by_name(request_rec *r, const char *username);

long dav_repos_get_principal_id(const dav_principal *principal);

const char *dav_repos_principal_to_s(const dav_principal *principal);

int dav_repos_is_allow(const dav_principal * principal,
		       dav_resource * resource,
		       dav_acl_permission_type permission);

dav_error *dav_repos_set_acl(const dav_acl * acl, dav_response ** response);

dav_acl *dav_repos_get_current_acl(const dav_resource * resource);

char *acl_build_propfind_output(const dav_resource * resource);

char *acl_supported_privilege_set(const dav_resource * resource);

char *acl_restrictions(const dav_resource * resource);

char *acl_inherited_acl_set(const dav_resource * resource);

char *acl_current_user_privilege_set(const dav_resource * resource);

dav_error *acl_create_initial_acl(const dav_repos_db * d, 
                                  dav_repos_resource * r,
			          const dav_principal *owner);

int acl_take_parent_aces(dav_repos_db * d, dav_repos_resource * r,
			 dav_resource * r_parent);

int acl_delete_own_aces(const dav_repos_db * d, dav_repos_resource * r);

int acl_delete_inherited_aces(const dav_repos_db * d,
			      dav_repos_resource * r);

int acl_hand_down_new_aces(const dav_repos_db * d,
			   dav_repos_resource * r, dav_acl * acl);

int acl_renew_all_aces(dav_repos_db * d, dav_repos_resource * r);

dav_error *dav_repos_deliver_acl_principal_prop_set(request_rec * r,
						    const dav_resource *
						    resource,
						    const apr_xml_doc *
						    doc,
						    ap_filter_t * output);

dav_error *dav_repos_deliver_principal_match(request_rec * r,
					     const dav_resource * resource,
					     const apr_xml_doc * doc,
					     ap_filter_t * output);

dav_error *dav_repos_deliver_principal_property_search(request_rec * r,
						       const dav_resource *
						       resource,
						       const apr_xml_doc *
						       doc,
						       ap_filter_t *
						       output);

dav_error *dav_repos_deliver_principal_search_property_set(request_rec * r,
							   const
							   dav_resource *
							   resource,
							   const
							   apr_xml_doc *
							   doc,
							   ap_filter_t *
							   output);

const char *principal_href_prefix(request_rec *r);
#endif
