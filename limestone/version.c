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

/* ====================================================================
 * Copyright (c) 2002, The Regents of the University of California.
 *
 * The Regents of the University of California MAKE NO REPRESENTATIONS OR
 * WARRANTIES ABOUT THE SUITABILITY OF THE SOFTWARE, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, OR
 * NON-INFRINGEMENT. The Regents of the University of California SHALL
 * NOT BE LIABLE FOR ANY DAMAGES SUFFERED BY LICENSEE AS A RESULT OF
 * USING, MODIFYING OR DISTRIBUTING THIS SOFTWARE OR ITS DERIVATIVES.
 * ====================================================================
 */
#include <httpd.h>
#include <http_config.h>
#include <http_protocol.h>
#include <http_log.h>
#include <http_request.h>
#include <http_core.h>		/* for ap_construct_url */
#include <mod_dav.h>

#include <apr.h>
#include <apr_strings.h>
#include <apr_hash.h>
#include <apr_tables.h>
#include <apr_file_io.h>

#include "dav_repos.h"
#include "dbms.h"
#include "dbms_deltav.h"
#include "util.h"
#include "bridge.h"
#include "deltav_bridge.h"
#include "liveprops.h"  /* for dav_repos_build_lpr_hash */
#include "deltav_util.h"
#include "version.h"
#include "acl.h"

/* ### should move these report names to a public header to share with
### the client (and third parties). */
static const dav_report_elem avail_reports[] = {
    {"DAV:", "version-tree"},
    {"DAV:", "expand-property"},
    {"DAV:", "locate-by-history"},
    {NULL},
};

static void dav_repos_get_vsn_options(apr_pool_t * p,
				      apr_text_header * phdr)
{
    /* Note: we append pieces with care for Web Folders's 63-char limit
       on the DAV: header */
    TRACE();

    apr_text_append(p, phdr, "version-control,checkout, checkin, report");
    apr_text_append(p, phdr, "uncheckout,version-controlled-collection");

}

static dav_error *dav_repos_get_option(const dav_resource * resource,
				       const apr_xml_elem * elem,
				       apr_text_header * option)
{
    TRACE();
    return NULL;
}

/*
 ** All resource are versionable 
 */
static int dav_repos_versionable(const dav_resource * resource)
{
    TRACE();
    return 1;
}

static dav_auto_version dav_repos_auto_versionable(const dav_resource *
						   resource)
{
    dav_repos_resource *db_r = (dav_repos_resource *) resource->info->db_r;

    TRACE();

    if (resource->type != DAV_RESOURCE_TYPE_REGULAR)
        return DAV_AUTO_VERSION_NEVER;

    if (!resource->exists)
	return DAV_AUTO_VERSION_NEVER;

    if (resource->exists && !resource->versioned)
	return DAV_AUTO_VERSION_NEVER;

    /* checked-in resource */
    if (resource->versioned && !resource->working)
        switch (db_r->autoversion_type) {
        case DAV_AV_CHECKOUT_UNLOCKED_CHECKIN:
            //            dbms_set_checkin_on_unlock(resource->info->db, db_r);
        case DAV_AV_CHECKOUT_CHECKIN:
        case DAV_AV_CHECKOUT:
            return DAV_AUTO_VERSION_ALWAYS;
        case DAV_AV_LOCKED_CHECKOUT:
            //            dbms_set_checkin_on_unlock(resource->info->db, db_r);
            return DAV_AUTO_VERSION_LOCKED;
        default:
            return DAV_AUTO_VERSION_NEVER;
        }

    /* checked-out resource */
    if (resource->versioned && resource->working) {
	if (db_r->autoversion_type == DAV_AV_CHECKOUT_CHECKIN)
	    return DAV_AUTO_VERSION_ALWAYS;
	else if (db_r->autoversion_type == DAV_AV_CHECKOUT_UNLOCKED_CHECKIN)
	    return DAV_AUTO_VERSION_LOCKED;
	else
	    return DAV_AUTO_VERSION_NEVER;
    }

    return DAV_AUTO_VERSION_NEVER;
}

/* Put a resource under version control. */
static dav_error *dav_repos_vsn_control(dav_resource * resource,
					const char *target)
{
    dav_repos_db *db = resource->info->db;
    dav_repos_resource *db_r = (dav_repos_resource *) resource->info->db_r;
    dav_error *err = NULL;

    TRACE();

    if (target)
	DBG1("TARGET: %s", target);

    if(!resource->exists) {
        /* check if it is locknull */

        /* resource doesn't exist. create a new one as per mod_dav.h */
        err = dav_repos_create_resource(resource, 0);
        if(err)
            return err;
    }
    /* postcondition: resource->exists */

    if(resource->type != DAV_RESOURCE_TYPE_REGULAR)
        return dav_new_error(resource->pool, HTTP_CONFLICT, 0,
                             "<DAV:must-be-versionable/>");

    if(resource->versioned) {
        /* Respond with success if the resource is already versioned */
        return NULL;
    }

    err = sabridge_vsn_control(db, db_r);

    /* update the fields to reflect new state */
    resource->versioned = 1;
    resource->working = 0;

    return NULL;
}

static dav_error *dav_repos_checkout(dav_resource * resource,
				     int auto_checkout,
				     int is_unreserved, int is_fork_ok,
				     int create_activity,
				     apr_array_header_t * activities,
				     dav_resource ** working_resource)
{
    dav_error *err = NULL;
    dav_repos_db *db = resource->info->db;
    dav_repos_resource *db_r = (dav_repos_resource *) resource->info->db_r;
    dav_repos_resource *vhr;

    TRACE();

    sabridge_new_dbr_from_dbr(db_r, &vhr);
    vhr->serialno = db_r->vhr_id;
    if((err = dbms_get_property(db, vhr)))
        return err;

    /* Check pre-condition */
    if (db_r->checked_state != DAV_RESOURCE_CHECKED_IN)
	return dav_new_error(db_r->p, HTTP_CONFLICT, 0,
			     "<DAV:must-be-checked-in/>");


    /* Auto-Versioning Stuff */
    if (auto_checkout) {
	// do auto versioning stuff
    } else {
	/* What should I feed? */
	*working_resource = apr_pcalloc(db_r->p, sizeof(*resource));
	memcpy(*working_resource, resource, sizeof(*resource));

	/* mod_dav needs only URI */
	(*working_resource)->uri =
	    sabridge_mk_version_uri(vhr, db_r->vr_num);

	/* Check post-condition */
	if ((*working_resource)->uri == NULL)
	    return dav_new_error(db_r->p, HTTP_INTERNAL_SERVER_ERROR, 0,
				 "Can't make VCR URI");
    }

    /* Let's set checkin and checkout value */
    err = dbms_set_checkin_out(db, db_r, DAV_RESOURCE_CHECKED_OUT, db_r->vr_num);
    if (err)
	return err;

    resource->working = 1;

    return NULL;
}

static dav_error *dav_repos_uncheckout(dav_resource * resource)
{
    dav_error *err = NULL;
    dav_repos_db *db = resource->info->db;
    dav_repos_resource *db_r = (dav_repos_resource *) resource->info->db_r;
    dav_repos_resource *db_r_version = NULL;
    dav_repos_resource *vhr;

    TRACE();

    /* Check pre-condition */
    if (db_r->checked_state != DAV_RESOURCE_CHECKED_OUT)
	return dav_new_error(db_r->p, HTTP_CONFLICT, 0,
			     "<DAV:must-be-checked-out/>");

    sabridge_new_dbr_from_dbr(db_r, &vhr);
    vhr->serialno = db_r->vhr_id;
    if((err = dbms_get_property(db, vhr)))
        return err;

    sabridge_new_dbr_from_dbr(db_r, &db_r_version);
    db_r_version->uri = sabridge_mk_version_uri(vhr, db_r->vr_num);
    if ((err = dbms_get_property(db, db_r_version)))
	return err;

    if(db_r->resourcetype == dav_repos_VERSIONED_COLLECTION)
        err = dbms_restore_vcc(db, db_r, db_r_version);
    else if(db_r->resourcetype == dav_repos_VERSIONED) {
        err = sabridge_copy_medium(db, db_r_version, db_r);
    }
    if(err) return err;
    
    /* Let's set checkin and checkout value */
    if((err = dbms_set_checkin_out(db, db_r, DAV_RESOURCE_CHECKED_IN, db_r->vr_num)))
	return err;

    resource->type = DAV_RESOURCE_TYPE_REGULAR;
    resource->working = 0;

    return NULL;
}

static dav_error *dav_repos_checkin(dav_resource * resource,
				    int keep_checked_out,
				    dav_resource ** version_resource)
{
    dav_error *err = NULL;
    dav_repos_db *db = resource->info->db;
    dav_repos_resource *db_r = (dav_repos_resource *) resource->info->db_r;
    dav_repos_resource *new_version;
    dav_repos_resource *vhr;

    TRACE();

    if (db_r->resourcetype != dav_repos_VERSIONED && 
        db_r->resourcetype != dav_repos_VERSIONED_COLLECTION)
        return dav_new_error(db_r->p, HTTP_CONFLICT, 0,
                             "<DAV:must-be-version-controlled-resource/>");

    /* Check pre-condition */
    if (db_r->checked_state != DAV_RESOURCE_CHECKED_OUT)
	return dav_new_error(db_r->p, HTTP_CONFLICT, 0,
			     "<DAV:must-be-checked-out/>");

    sabridge_new_dbr_from_dbr(db_r, &vhr);
    vhr->serialno = db_r->vhr_id;
    if((err = dbms_get_property(db, vhr)))
        return err;

    /* Let's create new version resource */
    err = sabridge_mk_new_version(db, db_r, vhr, db_r->vr_num+1, &new_version);
    if (err) return err;

    err = dbms_insert_version(db, new_version);
    if (err) goto error;

    /* Let's set checkin and checkout value */
    err = dbms_set_checkin_out
      (db, db_r, DAV_RESOURCE_CHECKED_IN, db_r->vr_num + 1);
    if (err) goto error;

    if (version_resource != NULL) {
	/* What should I feed? */
	*version_resource = apr_pcalloc(db_r->p, sizeof(*resource));
	memcpy(*version_resource, resource, sizeof(*resource));

	/* mod_dav needs only URI */
	(*version_resource)->uri =
	    sabridge_mk_version_uri(vhr, db_r->vr_num);

	/* Check post-condition */
	if ((*version_resource)->uri == NULL)
	    return dav_new_error(db_r->p, HTTP_INTERNAL_SERVER_ERROR, 0,
				 "Can't make VCR URI");
    }

    resource->type = DAV_RESOURCE_TYPE_REGULAR;
    resource->working = 0;

    return NULL;
    
 error:
    dbms_delete_resource(db, db_r->p, new_version);
    return err;
}

static dav_error *dav_repos_avail_reports(const dav_resource * resource,
					  const dav_report_elem ** reports)
{
    TRACE();

    /* ### further restrict to the public space? */
    if (resource->type != DAV_RESOURCE_TYPE_REGULAR) {
	*reports = NULL;
	return NULL;
    }

    *reports = avail_reports;
    return NULL;
}

static int dav_repos_report_label_header_allowed(const apr_xml_doc * doc)
{
    TRACE();
    return 0;
}

void send_xml(apr_bucket_brigade * bb, ap_filter_t * output,
		     const char *str)
{
    (void) apr_brigade_puts(bb, ap_filter_flush, output, str);
}

static dav_repos_report_elem *dav_repos_build_report_elem(apr_pool_t *
							  pool,
							  const apr_xml_doc
							  * doc)
{
    apr_xml_elem *elem;
    dav_repos_report_elem *root = NULL;
    dav_repos_report_elem *ret = NULL, *next;
    TRACE();

    /* Should we check the root is <version-control> ?? */

    elem = dav_find_child(doc->root, "prop");
    if (elem == NULL) {
	DBG0(" NO prop element");
	return root;
    }

    for (elem = elem->first_child; elem; elem = elem->next) {
	dav_repos_report_elem *new = apr_pcalloc(pool, sizeof(*new));
	/* Should I use apr_pstrdup?? */
	new->name = elem->name;

	DBG1("Add dav_repos_report_element : %s", new->name);

	/* Add link */
	new->next = root;
	root = new;
    }

    /* reverse order */
    while (root) {
	next = root->next;
	root->next = ret;
	ret = root;
	root = next;
    }
    return ret;
}

static dav_error *dav_repos_deliver_version_tree_report(request_rec * r,
							const dav_resource
							* resource,
							const apr_xml_doc *
							doc,
							ap_filter_t *
							output)
{
    /* this buffers the output for a bit and is automatically flushed,
       at appropriate times, by the Apache filter system. */
    apr_bucket_brigade *bb;
    dav_error *err = NULL;
    dav_repos_resource *vrs = NULL;
    apr_pool_t *pool = resource->pool;
    dav_repos_report_elem *tmp_elem;
    dav_repos_report_elem *report_elem;
    dav_repos_db *db = resource->info->db;
    dav_repos_resource *db_r = (dav_repos_resource *) resource->info->db_r;
    dav_repos_resource *vhr;

    TRACE();

    bb = apr_brigade_create(pool,	/* not the subpool! */
			    output->c->bucket_alloc);

    sabridge_new_dbr_from_dbr(db_r, &vhr);
    vhr->serialno = db_r->vhr_id;
    /* Change to get basic props */
    if((err = dbms_get_property(db, vhr)))
        return err;

    /* Let's find VRS */
    if ((err = dbms_get_vhr_versions(db, vhr, &vrs)) != NULL)
	return err;

    /* 
     ** Read XML and build dav_repos_report_elem
     */
    report_elem = dav_repos_build_report_elem(pool, doc);

    if (report_elem == NULL)
	return dav_new_error(pool,
			     HTTP_INTERNAL_SERVER_ERROR, 0,
			     "No element for report");

    send_xml(bb, output, DAV_XML_HEADER);
    send_xml(bb, output, DEBUG_CR);

    r->status = HTTP_MULTI_STATUS;

    send_xml(bb, output, "<D:multistatus xmlns:D=\"DAV:\">" DEBUG_CR);

    /*
     ** vrs should have a pool and uri
     */
    for (; vrs; vrs = vrs->next) {
	int need_404 = 0;

	/* Must have a pool */
	vrs->p = pool;
	/* set last version */
	//  vrs->lastversion = db_r->checkout!=-1?db_r->checkout-1:db_r->checkin-1;

	/* If null, we should skip */
	if (vrs->uri == NULL)
	    continue;

	/* Href display */
	send_xml(bb, output, "<D:response>" DEBUG_CR);
	send_xml(bb, output, "<D:href>");
	send_xml(bb, output, vrs->uri);
	send_xml(bb, output, "</D:href>" DEBUG_CR);

	/* 
	 **   build vpr hash 
	 */
	sabridge_build_vpr_hash(db, vrs);

	/*# TODO: Find props using hash */
	/* Found */
	send_xml(bb, output, "<D:propstat><D:prop>" DEBUG_CR);

	/* We support only DAV namespace, so don't care namespace */
	for (tmp_elem = report_elem; tmp_elem; tmp_elem = tmp_elem->next) {
	    const char *val;

	    /* It shouldn't be a NULL */
	    if (tmp_elem->name == NULL)
		continue;

	    /* Find value from version hash */
	    val = apr_hash_get(vrs->vpr_hash,
			       tmp_elem->name, APR_HASH_KEY_STRING);
	    if (val == NULL) {
		need_404 = 1;
		tmp_elem->found = 0;
	    } else {
		const char *str = apr_psprintf(pool, "<D:%s>%s</D:%s>"
					       DEBUG_CR,
					       tmp_elem->name, val,
					       tmp_elem->name);
		send_xml(bb, output, str);
		tmp_elem->found = 1;
	    }
	}
	send_xml(bb, output, "</D:prop>" DEBUG_CR);
	send_xml(bb, output,
		 "<D:status>HTTP/1.1 200 OK</D:status>" DEBUG_CR);
	send_xml(bb, output, "</D:propstat>" DEBUG_CR);

	/* Not found */
	if (need_404) {
	    send_xml(bb, output, "<D:propstat><D:prop>" DEBUG_CR);

	    /* We support only DAV namespace, so don't care namespace */
	    for (tmp_elem = report_elem; tmp_elem;
		 tmp_elem = tmp_elem->next) {
		if (tmp_elem->name && tmp_elem->found == 0) {
		    const char *str =
			apr_psprintf(pool, "<D:%s/>" DEBUG_CR,
				     tmp_elem->name);
		    send_xml(bb, output, str);
		}
	    }
	    send_xml(bb, output, "</D:prop>" DEBUG_CR);
	    send_xml(bb, output,
		     "<D:status>HTTP/1.1 404 Not Found</D:status>"
		     DEBUG_CR);
	    send_xml(bb, output, "</D:propstat>" DEBUG_CR);
	}

	send_xml(bb, output, "</D:response>" DEBUG_CR);

	/* flush the contents of the brigade */
	ap_fflush(output, bb);
    }

    send_xml(bb, output, "</D:multistatus>" DEBUG_CR);

    /* flush the contents of the brigade */
    ap_fflush(output, bb);

    return NULL;
}

/*
** TODO: having this function here is wrong.
*/
static const char *dav_repos_get_prop_value(dav_repos_db *db,
                                            dav_repos_resource *db_r, 
                                            const char *namespace,
                                            const char *propname)
{
    char *prop_value = NULL;

    TRACE();
    
    if (strcmp(namespace, "DAV:") == 0) {
        if (!strcmp(propname, "lockdiscovery"))
            return db_r->lockdiscovery;
        if (!strcmp(propname, "supportedlock"))
	    return db_r->supportedlock;

        if (!db_r->lpr_hash) dav_repos_build_lpr_hash(db_r);
        prop_value = apr_hash_get(db_r->lpr_hash, propname,
                                  APR_HASH_KEY_STRING);
        if (prop_value) return prop_value;

        if (!db_r->vpr_hash) sabridge_build_vpr_hash(db, db_r);
        prop_value = apr_hash_get(db_r->vpr_hash, propname,
                                  APR_HASH_KEY_STRING);
        if (prop_value) return prop_value;
    }

    return NULL;
}

static dav_error *dav_repos_expand_property_recurse(dav_repos_db * db,
						    dav_repos_resource *db_r,
						    apr_xml_elem * elem,
						    apr_bucket_brigade *bb,
						    ap_filter_t * output)
{
    apr_xml_elem *children;
    const char *prop_name;
    const char *ns_name = NULL;
    const char *prop_value = NULL;
    dav_error *err = NULL;

    TRACE();

    send_xml(bb, output, "<D:response>");

    /* print the href being handled currently */
    send_xml(bb, output, "<D:href>");
    send_xml(bb, output, db_r->uri);
    send_xml(bb, output, "</D:href>");

    send_xml(bb, output, "<D:propstat>");

    /* find the properties of current href that we should process */
    children = dav_find_child(elem, "property");

    /* iterate over all the properties */
    while (children != NULL) {
        char *hrefs;
        apr_xml_doc *doc_hrefs;
        apr_xml_parser *xml_parser;
        apr_xml_elem *href_elems;

	/* find the current property name */
	prop_name = dav_find_attr(children, "name");
        if(prop_name == NULL)
            return dav_new_error(db_r->p, HTTP_BAD_REQUEST, 0,
                                 "property element has no name attribute");

        /* determine its namespace */
        ns_name = dav_find_attr(children, "namespace");
        /* default value for namespace is "DAV:" */
        if(!ns_name) ns_name = "DAV:";

        send_xml(bb, output, "<D:prop>");
        send_xml(bb, output, apr_psprintf(db_r->p, "<D:%s>", prop_name));

        /* try to find the value of the property in the live properties
           of the resource */
        prop_value = dav_repos_get_prop_value(db, db_r, ns_name, prop_name);
        if (prop_value == NULL) {
            /* print 404 not found */
            send_xml(bb, output, "");
            goto close_prop;
        }

        xml_parser = apr_xml_parser_create(db_r->p);
        hrefs = apr_psprintf(db_r->p, "<D:hrefs xmlns:D=\"DAV:\">%s</D:hrefs>",
                             prop_value);

        if (0 != apr_xml_parser_feed(xml_parser, hrefs, strlen(hrefs)) ) {
            send_xml(bb, output, prop_value);
            goto close_prop;
        }
        
        apr_xml_parser_done(xml_parser, &doc_hrefs);
        href_elems = dav_find_child(doc_hrefs->root, "href");
        if(href_elems == NULL) {
                send_xml(bb, output, prop_value);
                goto close_prop;
        } 

        while (href_elems != NULL) {
            /* Fetch the properties of the href */
            dav_repos_resource *new_db_r = NULL;
            sabridge_new_dbr_from_dbr(db_r, &new_db_r);
            new_db_r->uri = apr_pstrdup(db_r->p,
                                        dav_xml_get_cdata(href_elems, db_r->p, 1));
            if((err = dbms_get_property(db, new_db_r)))
                return err;
            
            err = dav_repos_expand_property_recurse(db, new_db_r,
                                                    children, bb,
                                                    output);
            if(err) return err;
            
            href_elems = href_elems->next;
        }

    close_prop:
        send_xml(bb, output, apr_psprintf(db_r->p, "</D:%s>", prop_name));

        send_xml(bb, output, "</D:prop>");
            
        children = children->next;
    }
    
    send_xml(bb, output, "<D:status>HTTP/1.1 200 OK</D:status>" );

    send_xml(bb, output, "</D:propstat>");
    send_xml(bb, output, "</D:response>");

    return NULL;
}

static dav_error *dav_repos_deliver_expand_property_report(request_rec * r,
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
    dav_repos_db *db = resource->info->db;
    dav_repos_resource *db_r = (dav_repos_resource *) resource->info->db_r;
    dav_error *err = NULL;

    TRACE();

    bb = apr_brigade_create(pool,	/* not the subpool! */
			    output->c->bucket_alloc);

    send_xml(bb, output, DAV_XML_HEADER);
    send_xml(bb, output, DEBUG_CR);

    r->status = HTTP_MULTI_STATUS;

    send_xml(bb, output, "<D:multistatus xmlns:D=\"DAV:\">" DEBUG_CR);

    err = dav_repos_expand_property_recurse(db, db_r, doc->root, bb, output);
    if(err) return err;

    send_xml(bb, output, "</D:multistatus>");

    /* flush the contents of the brigade */
    ap_fflush(output, bb);

    return NULL;
}

static dav_error *dav_repos_deliver_locate_by_history(request_rec * r,
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
    apr_xml_elem *version_history_set;
    apr_xml_elem *version_history_hrefs;
    apr_hash_t *vhrs_ht;
    dav_repos_report_elem *prop_elems;
    dav_repos_resource *collection_res;
    dav_error *err = NULL;

    TRACE();

    vhrs_ht = apr_hash_make(pool);

    /* Get the ids of all the version-history-set elements */
    version_history_set = dav_find_child(doc->root, "version-history-set");
    version_history_hrefs = dav_find_child(version_history_set, "href");
    while (version_history_hrefs) {
	dav_repos_resource *vhr = NULL;
	sabridge_new_dbr_from_dbr(db_r, &vhr);

        vhr->uri =
	    apr_pstrdup(pool,
			dav_xml_get_cdata(version_history_hrefs, pool, 1));
	if((err = dbms_get_property(db, vhr)))
            return err;

	DBG2("setting %ld %s", vhr->serialno, vhr->uri);
	apr_hash_set(vhrs_ht, &(vhr->serialno), sizeof(vhr->serialno),
		     vhr->uri);

	version_history_hrefs = version_history_hrefs->next;
    }

    prop_elems = dav_repos_build_report_elem(pool, doc);

    bb = apr_brigade_create(pool,	/* not the subpool! */
			    output->c->bucket_alloc);

    /* Will be filled children */
    err = sabridge_get_collection_children(db, db_r, 1, NULL, NULL, NULL, NULL);
    if(err) return err;

    r->status = HTTP_MULTI_STATUS;

    send_xml(bb, output, "<D:multistatus xmlns:D=\"DAV:\">" DEBUG_CR);

    collection_res = db_r;
    while ((db_r = db_r->next) != NULL) {
	if (db_r->resourcetype == dav_repos_VERSIONED ||
	    db_r->resourcetype == dav_repos_VERSIONED_COLLECTION) {
	    if (apr_hash_get
		(vhrs_ht, &(db_r->vhr_id), sizeof(db_r->vhr_id))) {
		dav_repos_report_elem *iter_elem;
		send_xml(bb, output, "<D:response>");
		send_xml(bb, output, dav_repos_mk_href(pool, db_r->uri));
		send_xml(bb, output, "<D:propstat>");

		send_xml(bb, output, "<D:prop>");
		for (iter_elem = prop_elems; iter_elem;
		     iter_elem = iter_elem->next) {
		    const char *val;

		    //        dbms_build_ns_id_hash(db, db_r);
		    db_r->root_path = collection_res->root_path;
		    sabridge_build_vpr_hash(db, db_r);

		    /* Find value from version hash */
		    val = apr_hash_get(db_r->vpr_hash,
				       iter_elem->name,
				       APR_HASH_KEY_STRING);
		    const char *str =
			apr_psprintf(pool, "<D:%s>%s</D:%s>" DEBUG_CR,
				     iter_elem->name, val,
				     iter_elem->name);
		    send_xml(bb, output, str);

		}
		send_xml(bb, output, "</D:prop>");
		send_xml(bb, output,
			 "<D:status>HTTP/1.1 200 OK</D:status>" DEBUG_CR);

		send_xml(bb, output, "</D:propstat>");
		send_xml(bb, output, "</D:response>");

	    }
	}
    }
    send_xml(bb, output, "</D:multistatus>");

    /* flush the contents of the brigade */
    ap_fflush(output, bb);

    return NULL;

}

static dav_error *dav_repos_deliver_report(request_rec * r,
					   const dav_resource * resource,
					   const apr_xml_doc * doc,
					   ap_filter_t * output)
{
    if (doc && dav_validate_root(doc, "version-tree"))
	return dav_repos_deliver_version_tree_report(r, resource, doc,
						     output);
    else if (doc && dav_validate_root(doc, "expand-property"))
	return dav_repos_deliver_expand_property_report(r, resource, doc,
							output);
    else if (doc && dav_validate_root(doc, "locate-by-history"))
	return dav_repos_deliver_locate_by_history(r, resource, doc,
						   output);
    // FIXME: ACL Specific REPORTs, move to an appropriate file later.
    else if (doc && dav_validate_root(doc, "acl-principal-prop-set"))
	return dav_repos_deliver_acl_principal_prop_set(r, resource, doc,
						   output);
    
    else if (doc && dav_validate_root(doc, "principal-match"))
	return dav_repos_deliver_principal_match(r, resource, doc,
						   output);
    
    else if (doc && dav_validate_root(doc, "principal-property-search"))
	return dav_repos_deliver_principal_property_search(r, resource, doc,
						   output);
    
    else if (doc && dav_validate_root(doc, "principal-search-property-set"))
	return dav_repos_deliver_principal_search_property_set(r, resource, doc,
						   output);

    return dav_new_error(resource->pool,
			 HTTP_INTERNAL_SERVER_ERROR, 0,
			 "No element for report");

}

static dav_error *dav_repos_set_checkin_on_unlock(dav_resource *resource)
{
    dav_repos_db *db = resource->info->db;
    dav_repos_resource *db_r = (dav_repos_resource *) resource->info->db_r;

    TRACE();

    if (db_r->resourcetype != dav_repos_VERSIONED ||
        db_r->resourcetype != dav_repos_VERSIONED_COLLECTION )
        return dav_new_error(resource->pool,
                             HTTP_INTERNAL_SERVER_ERROR, 0,
                             "Cannot mark this resource for auto checkin");

    return dbms_set_checkin_on_unlock(db, db_r);
}

static int dav_repos_is_checkin_on_unlock(dav_resource *resource)
{
    dav_repos_resource *db_r = (dav_repos_resource *) resource->info->db_r;

    TRACE();

    return db_r->checkin_on_unlock;
}

const dav_hooks_vsn dav_repos_hooks_vsn = {
    dav_repos_get_vsn_options,
    dav_repos_get_option,
    dav_repos_versionable,
    dav_repos_auto_versionable,
    dav_repos_vsn_control,
    dav_repos_checkout,
    dav_repos_uncheckout,
    dav_repos_checkin,
    dav_repos_avail_reports,
    dav_repos_report_label_header_allowed,
    dav_repos_deliver_report,
    NULL,			/* update */
    NULL,			/* add_label */
    NULL,			/* remove_label */
    NULL,			/* can_be_workspace */
    NULL,			/* make_workspace */
    NULL,			/* can_be_activity */
    NULL,			/* make_activity */
    NULL,			/* merge */
    dav_repos_set_checkin_on_unlock,
    dav_repos_is_checkin_on_unlock,
    NULL                        /* ctx */
};
