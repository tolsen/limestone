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
 ** ACL methods 
 */

#include "acl.h"
#include "bridge.h"             /* for sabridge_get_collection_children */
#include "liveprops.h"          /* for dav_repos_build_lpr_hash */
#include "dbms_principal.h"     /* for dbms_get_principal_id_from_name */

#define AHKS APR_HASH_KEY_STRING

/* FIXME: This should correspond with DB */
#define PRINCIPAL_ALL_ID                3
#define PRINCIPAL_AUTHENTICATED_ID      4
#define PRINCIPAL_UNAUTHENTICATED_ID    5

dav_principal *dav_repos_get_prin_by_name(request_rec *r, const char *name)
{
    dav_principal *principal = apr_pcalloc(r->pool, sizeof(dav_principal));

    if(!name) {
        principal->type = PRINCIPAL_UNAUTHENTICATED;
        return principal;
    }

    if(!strcmp(name, "all")) 
        principal->type = PRINCIPAL_ALL;
    else if(!strcmp(name, "authenticated"))
        principal->type = PRINCIPAL_AUTHENTICATED;
    else if(!strcmp(name, "unauthenticated"))
        principal->type = PRINCIPAL_UNAUTHENTICATED;
    else {
        dav_repos_cache *cache = sabridge_get_cache(r);
        int *value, resource_type;

        if (!(value = (int *)apr_hash_get(cache->principal_type, name, AHKS))) {
            resource_type = dbms_get_principal_type_from_name(r->pool, 
                    dav_repos_get_db(r), name);
            value = apr_pcalloc(r->pool, sizeof(*value));
            *value = resource_type;
            apr_hash_set(cache->principal_type, name, AHKS, value);
        }
        else {
            resource_type = *value;    
        }

        principal = dav_principal_make_from_url(r, 
                          apr_pstrcat(r->pool, principal_href_prefix(r), 
                          resource_type == dav_repos_GROUP ?
                          PRINCIPAL_GROUP_PREFIX : PRINCIPAL_USER_PREFIX,
                          name, NULL));
    }
    return principal;
}

long dav_repos_get_principal_id(const dav_principal *principal)
{
    switch(principal->type) {
        case PRINCIPAL_ALL:
            return PRINCIPAL_ALL_ID;

        case PRINCIPAL_AUTHENTICATED:
            return PRINCIPAL_AUTHENTICATED_ID;

        case PRINCIPAL_UNAUTHENTICATED:
            return PRINCIPAL_UNAUTHENTICATED_ID;

        case PRINCIPAL_HREF:
            return principal->resource->info->db_r->serialno;

        default:
            break;
    }

    return 0l;
}

const char *dav_repos_principal_to_s(const dav_principal *principal)
{
    dav_resource *resource = principal->resource;
    request_rec *r;
    switch(principal->type) {
        case PRINCIPAL_ALL:
            return "all";

        case PRINCIPAL_AUTHENTICATED:
            return "authenticated";

        case PRINCIPAL_UNAUTHENTICATED:
            return "unauthenticated";

        case PRINCIPAL_HREF:
            r = resource->info->rec;
            return apr_pstrcat(resource->pool, principal_href_prefix(r),
                               resource->uri, NULL);

        default:
            break;
    }

    return NULL;
}

/** 
 * Computes if a certain principal is allowed
 * to use a given permission on a resource. 
 * @param principal The principal who wants to do something. 
 * @param resource The resource an which the action should be taken. 
 * @param permission The permission type of the privilege. 
 * @return success state of the function, 
 *         defines if the action is allowed or not. 
 */
int dav_repos_is_allow(const dav_principal * principal,
		       dav_resource * resource,
		       dav_acl_permission_type permission)
{
    int retVal = FALSE;
    dav_repos_resource *db_r;
    dav_repos_db *db;
    apr_pool_t *pool;

    TRACE();

    if (!resource) {
        /* Let other hooks handle this,
         * sending a FALSE will force a 403 */
        return TRUE;   
    }
    if (resource->hooks != &dav_repos_hooks_repos) {
        return TRUE;
    }

    db = resource->info->db;
    db_r = (dav_repos_resource *) resource->info->db_r;
    pool = db_r->p;

    dav_privilege *privilege = dav_privilege_new_by_type(pool, permission);
    const char *privilege_name =
	apr_pstrdup(pool, dav_get_privilege_name(privilege));
    long priv_ns_id;

    sabridge_get_namespace_id(db, db_r, dav_get_privilege_namespace(privilege), &priv_ns_id);

    retVal = dbms_is_allow(db, priv_ns_id, privilege_name, principal, db_r);

    if(!retVal) {
        /* add a need-privileges error tag */
        const char *res_elem = 
            apr_psprintf(pool, "  <D:resource>"DEBUG_CR
                                "    <D:href>%s</D:href>"DEBUG_CR
                                "    <D:privilege><D:%s/></D:privilege>"DEBUG_CR
                               "  </D:resource>"DEBUG_CR, 
                               apr_xml_quote_string(pool, resource->uri, 0), 
                               privilege_name);

        const char *errormsg =
            apr_psprintf(pool, "Principal:%s needs %s privilege to access %s",
                         dav_repos_principal_to_s(principal),
                         privilege_name, resource->uri);

        int status = dav_get_permission_denied_status(resource->info->rec);
        dav_error *err = NULL;

        if(status == HTTP_FORBIDDEN) {
            const char *prolog = NULL;
            if (db->xsl_403_uri) {
                prolog = apr_psprintf(pool, "<?xml-stylesheet "
                                      "type=\"text/xsl\" href=\"%s\" ?>" 
                                      DEBUG_CR, db->xsl_403_uri);
            }
            err = dav_new_error_tag(pool, status, 0, errormsg, NULL,
                                    "need-privileges", res_elem, prolog);
        }
        else {
            err = dav_new_error(pool, status, 0, "Error in dav_repos_is_allow");
        }

        resource->err = err;
    }

    return retVal;
}

dav_error *dav_repos_inherit_aces(dav_resource *resource)
{
    dav_repos_resource *db_r = resource->info->db_r;
    const dav_repos_db *db = resource->info->db;
    apr_pool_t *pool = db_r->p;
    int parent_id;
    
    TRACE();

    if(!(parent_id =  sabridge_get_parent_id(db_r)))
        return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "Could not get parent id for ACL Inheritance");
    return dbms_inherit_parent_aces(db, db_r, parent_id);
}

dav_error *dav_repos_create_initial_acl(const dav_principal *owner, 
                                        dav_resource *resource)
{
    dav_repos_resource *db_r = resource->info->db_r;
    const dav_repos_db *db = resource->info->db;
    apr_pool_t *pool = db_r->p;
    dav_error *err = NULL;
    
    db_r->acl = dav_acl_new(pool, resource, owner, owner);
    if((err = acl_create_initial_acl(db, db_r, owner)))
        return err;

    /* ensure that we have inherited aces */
    return dav_repos_inherit_aces(resource);
}
    
/** 
 * Sets the ACL of a resource. This is the function mod_dav called. 
 * Only own ACEs will be set. Inherited ACEs will not be touched! 
 * @param d DB connection struct containing the user, password, and DB name. 
 * @param r The resource on which the acl should be set. 
 * @param acl pointer to the acl to be set. 
 * @param response pointer to a dav_response pointer. 
 * @return dav_error 
 */

dav_error *dav_repos_set_acl(const dav_acl * acl, dav_response ** response)
{
    const dav_resource *resource = dav_get_acl_resource(acl);
    dav_repos_resource *db_r = (dav_repos_resource *) resource->info->db_r;
    dav_repos_db *db = resource->info->db;
    dav_acl *protected_acl = NULL;
    dav_ace_iterator *iter;
    int retVal = OK;
    dav_error *err = NULL;

    TRACE();

    /* Clear all non-protected, non-inherited ACEs */
    if((retVal = dbms_delete_own_aces(db, db_r)) != OK)
        goto error;

    /* Gel all protected, non-inherited ACEs */
    if((retVal = dbms_get_protected_aces(db, db_r, &protected_acl))!= OK)
        goto error;

    /* Self check for conflicting ACEs */
    if((retVal = sabridge_acl_check_preconditions(acl, acl)) != OK)
        return dav_new_error_tag(db_r->p, retVal, 0, NULL, NULL, 
                                 "no-ace-conflict", NULL, NULL);

    /* Check for conflicts with protected ACEs */
    if(sabridge_acl_check_preconditions(acl, protected_acl) != OK)
        return dav_new_error_tag(db_r->p, HTTP_FORBIDDEN, 0, NULL, NULL, 
                                 "no-protected-ace-conflict", NULL, NULL);

    iter = dav_acl_iterate(acl);
    while (dav_ace_iterator_more(iter)) {
	const dav_ace *ace = dav_ace_iterator_next(iter);
	err = dbms_add_ace(db, db_r, ace, db_r->serialno);
        if(err) return err;
    }

error:
    if(retVal != OK) err = dav_new_error(db_r->p, retVal, 0, 
                                         "could not set ACL");

    return err;
}

/** 
 * Returns the acl of a resource. 
 * @param resource The resource an which the action should be taken.  
 * @return The acl of that resource. 
 */
dav_acl *dav_repos_get_current_acl(const dav_resource * resource)
{
    dav_acl *retVal = NULL;
    dav_repos_resource *db_r;
    dav_repos_db *db;
    if (!resource) {
	return retVal;
    }
    if (resource->hooks != &dav_repos_hooks_repos) {
	return retVal;
    }
    db = resource->info->db;
    db_r = (dav_repos_resource *) resource->info->db_r;

    TRACE();
    
    dbms_get_acl(db, db_r);
    return db_r->acl;
}

/** 
 * Returns the propfind XML output for a acl. 
 * @param resource The resource an which the action should be taken.  
 * @return The string with the xml code for the propfind response. 
 */
char *acl_build_propfind_output(const dav_resource * resource)
{
    char *str_xml_acl = "";
    dav_acl *current_acl = NULL;
    dav_repos_resource *db_r;
    request_rec *r = resource->info->rec;
    dav_repos_db *db;
    TRACE();
    if (!resource) {
	return str_xml_acl;
    }
    db = resource->info->db;
    db_r = (dav_repos_resource *) resource->info->db_r;
    current_acl = (*dav_repos_hooks_acl.get_current_acl) (resource);
    if (current_acl) {
	dav_ace_iterator *iter;
	iter = dav_acl_iterate(current_acl);
	while (dav_ace_iterator_more(iter)) {
	    const dav_ace *ace = dav_ace_iterator_next(iter);
	    const dav_principal *ace_principal = dav_get_ace_principal(ace);
            const dav_prop_name *ace_property = dav_get_ace_property(ace);
	    const dav_privileges *ace_privileges = dav_get_ace_privileges(ace);
            const char *principal_url = dav_repos_principal_to_s(ace_principal);

	    /* Build XML. */
	    char *str_tmp = NULL;
	    char *str_acepriv = "";
	    char *str_acehead = NULL;
	    char *str_acebody = NULL;
	    char *grant_deny = NULL;
            char *str_protected = NULL;
	    const char *inherited = NULL;
	    if (dav_is_deny_ace(ace))
		grant_deny = "deny";

	    else
		grant_deny = "grant";

            if(ace_property && !strcmp(ace_property->ns, "") && !strcmp(ace_property->name, "self")) {
                str_acehead = apr_psprintf(db_r->p, "<D:ace>" DEBUG_CR
                                           "<D:principal>" DEBUG_CR
                                           "<D:self/>" DEBUG_CR
                                           "</D:principal>" DEBUG_CR
                                           "<D:%s>", grant_deny);
            }
            else if(ace_property) {
                str_acehead = apr_psprintf(db_r->p, "<D:ace>" DEBUG_CR
                                           "<D:principal>" DEBUG_CR
                                           "<D:property>" DEBUG_CR
                                           "<R:%s xmlns:R=\"%s\"/>" DEBUG_CR
                                           "</D:property>" DEBUG_CR
                                           "</D:principal>" DEBUG_CR
                                           "<D:%s>", ace_property->name,
                                           ace_property->ns, grant_deny);
            } 
            else if(strstr(principal_url, PRINCIPAL_USER_PREFIX) ||
                    strstr(principal_url, PRINCIPAL_GROUP_PREFIX)) {
                str_acehead = 
                    apr_psprintf(db_r->p, 
                        "<D:ace>" DEBUG_CR
			"<D:principal>" DEBUG_CR
			"<D:href>%s</D:href>" DEBUG_CR
			"</D:principal>" DEBUG_CR
			"<D:%s>",
			apr_xml_quote_string(db_r->p, principal_url, 0), 
                        grant_deny);
            }
            else {
                str_acehead = apr_psprintf(db_r->p, "<D:ace>" DEBUG_CR
                                           "  <D:principal>" DEBUG_CR
                                           "    <D:%s/>" DEBUG_CR
				           "  </D:principal>" DEBUG_CR
				           "<D:%s>",
				           principal_url, grant_deny);
            }

	    dav_privilege_iterator *iter;
	    iter = dav_privilege_iterate(ace_privileges);
	    while (dav_privilege_iterator_more(iter)) {
		const dav_privilege *privilege =
		    dav_privilege_iterator_next(iter);
		const char *privilege_name =
		    dav_get_privilege_name(privilege);
		str_tmp =
		    apr_psprintf(db_r->p,
				 "<D:privilege>" DEBUG_CR "<D:%s/>"
				 DEBUG_CR "</D:privilege>",
				 privilege_name);
		str_tmp = apr_pstrcat(db_r->p, str_acepriv, str_tmp, NULL);
		str_acepriv = apr_pstrdup(db_r->p, str_tmp);
	    }

            if(dav_is_protected_ace(ace))
                str_protected = apr_psprintf(db_r->p, "</D:%s><D:protected/>",
                                             grant_deny);
            else str_protected = apr_psprintf(db_r->p, "</D:%s>", grant_deny);

	    inherited = dav_get_ace_inherited(ace);
	    if ((inherited == NULL) || (strcmp(inherited, "") == 0))
		str_acebody = apr_psprintf(db_r->p, "</D:ace>");
	    else str_acebody = 
                apr_psprintf(db_r->p, 
                    "<D:inherited>" DEBUG_CR
                    "<D:href>%s%s</D:href>" DEBUG_CR
                    "</D:inherited>" DEBUG_CR
                    "</D:ace>",
                    principal_href_prefix(r), 
                    apr_xml_quote_string(db_r->p, inherited, 0));

	    str_xml_acl =
		apr_pstrcat(db_r->p, str_xml_acl, str_acehead, str_acepriv,
			    str_protected, str_acebody, NULL);
	}
    }
    return str_xml_acl;
}


/** 
 * Returns the propfind XML output for a supported-privilege-set demand. 
 * @param resource The resource an which the action should be taken.  
 * @return The string with the xml code for the propfind response. 
 */
char *acl_supported_privilege_set(const dav_resource * resource)
{
    char *privilege_set_xml = NULL;
    dav_repos_resource *db_r;
    dav_repos_db *db;

    TRACE();

    if (!resource)
	return privilege_set_xml;
    db = resource->info->db;
    db_r = (dav_repos_resource *) resource->info->db_r;
    apr_pool_t *pool = db_r->p;

    privilege_set_xml = apr_psprintf(pool,
      "<D:supported-privilege>" DEBUG_CR 
      " <D:privilege><D:all/></D:privilege>" DEBUG_CR 
      " <D:description xml:lang=\"en\">Any operation</D:description>" DEBUG_CR
      " <D:supported-privilege>" DEBUG_CR
      "  <D:privilege><D:write/></D:privilege>" DEBUG_CR
      "  <D:description xml:lang=\"en\">" DEBUG_CR
      "   Write any object" DEBUG_CR
      "  </D:description>" DEBUG_CR 
      "   <D:supported-privilege>" DEBUG_CR
      "    <D:privilege><D:write-content/></D:privilege>" DEBUG_CR
      "    <D:description xml:lang=\"en\">" DEBUG_CR
      "     Write resource content" DEBUG_CR
      "    </D:description>" DEBUG_CR
      "   </D:supported-privilege>");

    /* report bind, unbind only if resource is a collection */
    if(db_r->resourcetype == dav_repos_COLLECTION ||
       db_r->resourcetype == dav_repos_COLLECTION_VERSION) {
        privilege_set_xml = apr_pstrcat(pool, privilege_set_xml,
      "   <D:supported-privilege>" DEBUG_CR 
      "    <D:privilege><D:unbind/></D:privilege>" DEBUG_CR
      "    <D:description xml:lang=\"en\">" DEBUG_CR
      "     Unbind a child" DEBUG_CR
      "    </D:description>" DEBUG_CR 
      "   </D:supported-privilege>" DEBUG_CR 
      "   <D:supported-privilege>" DEBUG_CR
      "    <D:privilege><D:bind/></D:privilege>" DEBUG_CR
      "    <D:description xml:lang=\"en\">" DEBUG_CR
      "     Bind a child" DEBUG_CR
      "    </D:description>" DEBUG_CR
      "   </D:supported-privilege>" DEBUG_CR, NULL);
    }

    privilege_set_xml = apr_pstrcat(pool, privilege_set_xml,
      "   <D:supported-privilege>" DEBUG_CR 
      "    <D:privilege><D:write-acl/></D:privilege>" DEBUG_CR 
      "    <D:description xml:lang=\"en\">" DEBUG_CR
      "     Write ACL" DEBUG_CR 
      "    </D:description>" DEBUG_CR
      "   </D:supported-privilege>" DEBUG_CR 
      "   <D:supported-privilege>" DEBUG_CR
      "    <D:privilege><D:write-properties/></D:privilege>" DEBUG_CR 
      "    <D:description xml:lang=\"en\">" DEBUG_CR
      "     Write properties" DEBUG_CR
      "    </D:description>" DEBUG_CR
      "   </D:supported-privilege>" DEBUG_CR 
      "  </D:supported-privilege>" DEBUG_CR DEBUG_CR
      "  <D:supported-privilege>" DEBUG_CR 
      "   <D:privilege><D:unlock/></D:privilege>" DEBUG_CR
      "   <D:description xml:lang=\"en\">" DEBUG_CR
      "    Unlock resource" DEBUG_CR
      "   </D:description>" DEBUG_CR
      "  </D:supported-privilege>" DEBUG_CR DEBUG_CR 
      "  <D:supported-privilege>" DEBUG_CR
      "   <D:privilege><D:read/></D:privilege>" DEBUG_CR
      "   <D:description xml:lang=\"en\">" DEBUG_CR
      "    Read any object" DEBUG_CR
      "   </D:description>" DEBUG_CR
      "   <D:supported-privilege>" DEBUG_CR
      "    <D:privilege>" DEBUG_CR
      "     <D:read-current-user-privilege-set/>" DEBUG_CR
      "    </D:privilege>" DEBUG_CR
      "    <D:description xml:lang=\"en\">" DEBUG_CR
      "     Read current user privilege set property" DEBUG_CR
      "    </D:description>" DEBUG_CR
      "   </D:supported-privilege>" DEBUG_CR
      "   <D:supported-privilege>" DEBUG_CR 
      "    <D:privilege><D:read-acl/></D:privilege>" DEBUG_CR
      "    <D:description xml:lang=\"en\">" DEBUG_CR
      "     Allows propfind to DAV:acl" DEBUG_CR
      "    </D:description>" DEBUG_CR
      "   </D:supported-privilege>" DEBUG_CR
      "  </D:supported-privilege>" DEBUG_CR
      "</D:supported-privilege>" DEBUG_CR, NULL);
    return privilege_set_xml;
}

/** 
 * Returns the propfind XML output for a acl-restrictions demand. 
 * @param resource The resource an which the action should be taken.  
 * @return The string with the xml code for the propfind response. 
 */
char *acl_restrictions(const dav_resource * resource)
{
    char *privilege_set_xml = NULL;
    dav_repos_resource *db_r;
    dav_repos_db *db;

    TRACE();

    if (!resource)
	return privilege_set_xml;
    db = resource->info->db;
    db_r = (dav_repos_resource *) resource->info->db_r;

    privilege_set_xml = apr_psprintf(db_r->p, "" DEBUG_CR);

    return privilege_set_xml;
}

/** 
 * Returns the propfind XML output for a inherited-acl-set demand. 
 * @param resource The resource an which the action should be taken.  
 * @return The string with the xml code for the propfind response. 
 */
char *acl_inherited_acl_set(const dav_resource * resource)
{
    char *inherited_set_xml = NULL;
    dav_repos_resource *db_r;
    dav_repos_db *db;
    dav_ace_iterator *iter;
    const char *last_inherited = " ";

    TRACE();

    if (!resource)
	return inherited_set_xml;
    db = resource->info->db;
    db_r = (dav_repos_resource *) resource->info->db_r;
    dav_acl *acl = dav_repos_get_current_acl(resource);

    inherited_set_xml = apr_psprintf(db_r->p, "" DEBUG_CR);

    iter = dav_acl_iterate(acl);
    while(dav_ace_iterator_more(iter)) {
        const dav_ace *ace = dav_ace_iterator_next(iter);
        const char *inherited = dav_get_ace_inherited(ace);
        if(inherited && strcmp(inherited, last_inherited)) {
            last_inherited = inherited;
            inherited_set_xml = apr_pstrcat(db_r->p, inherited_set_xml,
                                            "<D:href>", inherited, 
                                            "</D:href>" DEBUG_CR, NULL);
        }
    }

    return inherited_set_xml;
}

/** 
 * Returns the propfind XML output for a current-user-privilege-set demand. 
 * @param resource The resource an which the action should be taken.  
 * @return The string with the xml code for the propfind response. 
 */
char *acl_current_user_privilege_set(const dav_resource * resource)
{
    char *privilege_set_xml = "";
    dav_repos_resource *db_r;
    dav_repos_db *db;
    char *user;
    long principal_id;
    apr_pool_t *pool;
    char *tmp;
    const dav_privileges *privileges;
    dav_privilege_iterator *iter;

    TRACE();

    if (!resource)
	return privilege_set_xml;

    db = resource->info->db;
    db_r = (dav_repos_resource *) resource->info->db_r;
    pool = db_r->p;
    user = apr_pstrdup(pool, resource->info->rec->user);
    dbms_get_principal_id_from_name(pool, db, user, &principal_id);
    privileges = dbms_get_privileges(db, pool, principal_id, db_r->serialno);

    iter = dav_privilege_iterate(privileges);
    while (dav_privilege_iterator_more(iter)) {
	const dav_privilege *privilege = dav_privilege_iterator_next(iter);
	tmp =
	    apr_psprintf(pool,
			 "<D:privilege><D:%s/></D:privilege>" DEBUG_CR,
			 dav_get_privilege_name(privilege));
	privilege_set_xml =
	    apr_pstrcat(pool, privilege_set_xml, tmp, NULL);
    }

    return privilege_set_xml;
}

/** 
 * Created a initial acl for a new resource containing the owner, group 
 * and sets inherited permissions. 
 * @param d DB connection struct containing the user, password, and DB name. 
 * @param r The resource of which the privilege is. 
 * @param owner The owner of this resource. 
 * @return NULL for success. 
 */
dav_error *acl_create_initial_acl(const dav_repos_db * d, 
                                  dav_repos_resource * r,
			          const dav_principal *owner )
{
    apr_pool_t *pool = r->p;
    const dav_privilege *privilege =
	dav_privilege_new_by_type(pool, DAV_PERMISSION_ALL);
    dav_privileges *privileges = dav_privileges_new(pool);
    dav_prop_name *owner_property = NULL;

    TRACE();

    owner_property = dav_ace_property_new(pool, "DAV:", "owner");

    dav_add_privilege(privileges, privilege);
    dav_ace *ace = dav_ace_new(pool, owner, owner_property, privileges, 0, 
                               NULL /*inherited*/, 1 /*is_protected*/);

    if(r->acl)
        dav_add_ace(r->acl, ace);

    return dbms_add_ace(d, r, ace, r->serialno);
}

/*
 * ACL Report Functions
 */

dav_error *dav_repos_deliver_acl_principal_prop_set(request_rec * r,
						    const dav_resource *
						    resource,
						    const apr_xml_doc *
						    doc,
						    ap_filter_t * output)
{
    /* this buffers the output for a bit and is automatically flushed,
       at appropriate times, by the Apache filter system. */
    apr_bucket_brigade *bb;

    apr_pool_t *pool = resource->pool;
    dav_repos_db *db = resource->info->db;
    dav_repos_resource *db_r = (dav_repos_resource *) resource->info->db_r;
    apr_xml_elem *props;
    dav_repos_resource *principals = NULL;

    TRACE();

    props = dav_find_child(doc->root, "prop");

    dbms_get_principals(db, pool, db_r, principals);

    bb = apr_brigade_create(pool, output->c->bucket_alloc);
    r->status = HTTP_MULTI_STATUS;
    send_xml(bb, output, "<D:multistatus xmlns:D=\"DAV:\">" DEBUG_CR);

    while (principals != NULL) {
	sabridge_get_property(db, principals);
	dav_repos_build_lpr_hash(principals);
	send_xml(bb, output, "<D:response>");
	send_xml(bb, output, dav_repos_mk_href(pool, principals->uri));
	send_xml(bb, output, "<D:propstat>");
	send_xml(bb, output, "<D:prop>");
	for (props = props->first_child; props; props = props->next) {
	    const char *val;
	    val =
		apr_hash_get(principals->lpr_hash, props->name,
			     APR_HASH_KEY_STRING);
	    const char *str =
		apr_psprintf(pool, "<D:%s>%s</D:%s>" DEBUG_CR,
			     props->name, apr_xml_quote_string(pool, val, 0), 
                             props->name);
	    send_xml(bb, output, str);
	}

	send_xml(bb, output, "</D:prop>");
	send_xml(bb, output,
		 "<D:status>HTTP/1.1 200 OK</D:status>" DEBUG_CR);

	send_xml(bb, output, "</D:propstat>");
	send_xml(bb, output, "</D:response>");

	principals = principals->next;
    }

    send_xml(bb, output, "</D:multistatus>");

    /* flush the contents of the brigade */
    ap_fflush(output, bb);

    return NULL;

}

dav_error *dav_repos_deliver_principal_match(request_rec * r,
					     const dav_resource * resource,
					     const apr_xml_doc * doc,
					     ap_filter_t * output)
{
    /* this buffers the output for a bit and is automatically flushed,
       at appropriate times, by the Apache filter system. */
    apr_bucket_brigade *bb;

    apr_pool_t *pool = resource->pool;
    dav_repos_db *db = resource->info->db;
    dav_repos_resource *db_r = (dav_repos_resource *) resource->info->db_r;
    apr_xml_elem *principal_properties;
    char *req_username = r->user;
    long principal_id;
    dav_error *err = NULL;

    TRACE();

    if (!req_username) req_username = "unauthenticated";

    if((err = dbms_get_principal_id_from_name(pool, db, req_username, 
                                              &principal_id))) {
        return err;
    }

    principal_properties =
	dav_find_child(doc->root, "principal-property");

    sabridge_get_collection_children(db, db_r, DAV_INFINITY, "read",
                                     NULL, NULL, NULL);

    bb = apr_brigade_create(pool, output->c->bucket_alloc);
    r->status = HTTP_MULTI_STATUS;
    send_xml(bb, output, "<D:multistatus xmlns:D=\"DAV:\">" DEBUG_CR);

    while ((db_r = db_r->next)) {
	// Currently supporting DAV:owner only
	if ((principal_properties && 
             !strcmp(principal_properties->name, "owner") && 
             db_r->owner_id == principal_id) || 
            (!principal_properties &&	// Found no principal_properties
             !strcmp(db_r->displayname, req_username)))
	{
	    send_xml(bb, output, "<D:response>");
	    const char *str =
		apr_psprintf(pool, "<D:href>%s</D:href>"
			     DEBUG_CR, apr_xml_quote_string(pool, db_r->uri, 0));
	    send_xml(bb, output, str);
	    send_xml(bb, output,
		     "<D:status>HTTP/1.1 200 OK</D:status>" DEBUG_CR);
	    send_xml(bb, output, "</D:response>");
	}
    }

    send_xml(bb, output, "</D:multistatus>");

    /* flush the contents of the brigade */
    ap_fflush(output, bb);
    return NULL;
}

dav_error *dav_repos_deliver_principal_property_search(request_rec * r,
						       const dav_resource *
						       resource,
						       const apr_xml_doc *
						       doc,
						       ap_filter_t *
						       output)
{
    /* this buffers the output for a bit and is automatically flushed,
       at appropriate times, by the Apache filter system. */
    apr_bucket_brigade *bb;

    apr_pool_t *pool = resource->pool;
    dav_repos_db *db = resource->info->db;
    dav_repos_resource *db_r = (dav_repos_resource *) resource->info->db_r;
    apr_xml_elem *principal_property_search;
    apr_xml_elem *elem;
    apr_xml_elem *prop;
    apr_xml_elem *props;
    apr_xml_elem *match;
    int flag;

    TRACE();

    principal_property_search = dav_find_child(doc->root,
					       "principal-property-search");

    props = dav_find_child(doc->root, "prop");

    sabridge_get_collection_children(db, db_r, 1, "read",
                                     NULL, NULL, NULL);

    bb = apr_brigade_create(pool, output->c->bucket_alloc);
    r->status = HTTP_MULTI_STATUS;
    send_xml(bb, output, "<D:multistatus xmlns:D=\"DAV:\">" DEBUG_CR);

    while (db_r != NULL) {
	flag = 1;
	for (elem = principal_property_search->first_child;
	     elem && flag; elem = elem->next) {
	    if (!strcmp(elem->name, "property-search")) {
		prop = dav_find_child(elem, "prop");
		match = dav_find_child(elem, "match");
		dav_repos_build_lpr_hash(db_r);
		const char *val = apr_hash_get(db_r->lpr_hash,
					       prop->first_child->name,
					       APR_HASH_KEY_STRING);
		if (!strstr(val, match->first_cdata.first->text))
		    flag = 0;
	    }
	}
	if (flag) {
	    send_xml(bb, output, "<D:response>");
	    send_xml(bb, output,
		     dav_repos_mk_href(pool, db_r->uri));
	    send_xml(bb, output, "<D:propstat>");
	    send_xml(bb, output, "<D:prop>");
	    for (props = props->first_child; props; props = props->next) {
		const char *val;
		val =
		    apr_hash_get(db_r->lpr_hash, props->name,
				 APR_HASH_KEY_STRING);
		const char *str =
		    apr_psprintf(pool, "<D:%s>%s</D:%s>" DEBUG_CR,
				 props->name, 
                                 apr_xml_quote_string(pool, val, 0), 
                                 props->name);
		send_xml(bb, output, str);
	    }

	    send_xml(bb, output, "</D:prop>");
	    send_xml(bb, output,
		     "<D:status>HTTP/1.1 200 OK</D:status>" DEBUG_CR);

	    send_xml(bb, output, "</D:propstat>");
	    send_xml(bb, output, "</D:response>");
	}

	db_r = db_r->next;
    }

    send_xml(bb, output, "</D:multistatus>");

    /* flush the contents of the brigade */
    ap_fflush(output, bb);

    return NULL;
}

dav_error *dav_repos_deliver_principal_search_property_set(request_rec * r,
							   const
							   dav_resource *
							   resource,
							   const
							   apr_xml_doc *
							   doc,
							   ap_filter_t *
							   output)
{
    apr_bucket_brigade *bb;
    apr_pool_t *pool = resource->pool;

    TRACE();

    bb = apr_brigade_create(pool, output->c->bucket_alloc);
    send_xml(bb, output,
	     "<D:principal-search-property-set xmlns:D=\"DAV:\">"
	     DEBUG_CR);
    send_xml(bb, output, "<D:principal-search-property>" DEBUG_CR);
    send_xml(bb, output, "<D:prop>" DEBUG_CR);
    send_xml(bb, output, "<D:displayname/>" DEBUG_CR);
    send_xml(bb, output, "</D:prop>" DEBUG_CR);
    send_xml(bb, output,
	     "<D:description xml:lang=\"en\">Full name</D:description>"
	     DEBUG_CR);
    send_xml(bb, output, "</D:principal-search-property>" DEBUG_CR);
    send_xml(bb, output, "</D:principal-search-property-set>" DEBUG_CR);
    ap_fflush(output, bb);

    return NULL;
}

void dav_repos_update_principal_property_aces(dav_resource *resource,
                                              const dav_prop_name *name,
                                              const char *value)
{
    dav_repos_db *db = resource->info->db;
    dav_repos_resource *db_r = resource->info->db_r;
    long principal_id, ns_id;
    const dav_principal *principal;

    TRACE();

    /* get principal-id corresponding to the principal-uri 
     * represented by 'value' */
    principal = dav_principal_make_from_url(resource->info->rec,
                                            value);
    principal_id = dav_repos_get_principal_id(principal);

    /* if there was an error in resolving the principal,
     * set it to 'superuser' for now. */
    if(principal_id <= 0) principal_id = SUPERUSER_PRINCIPAL_ID;

    if(dbms_get_ns_id(db, db_r, name->ns, &ns_id))
        return;

    /* update the relevant ace(s) */
    dbms_update_principal_property_aces(db, db_r, ns_id, name->name, 
                                        principal_id);

    return;
}

const char *principal_href_prefix(request_rec *r)
{
    const char *server_name = r->server->server_hostname;
    int port = r->server->port;
    if (port && port != 80) {
        server_name = apr_psprintf(r->pool, "%s:%d", server_name, port);
    } 
    return apr_pstrcat(r->pool, "http://", server_name, NULL);
}

/* 
 * acl hook functions 
 */
const dav_hooks_acl dav_repos_hooks_acl = {
    dav_repos_get_prin_by_name,
    dav_repos_is_allow,
    dav_repos_create_initial_acl,
    dav_repos_set_acl,
    dav_repos_get_current_acl,
    dav_repos_update_principal_property_aces,
    dav_repos_inherit_aces,
    NULL			// ctx; 
};
