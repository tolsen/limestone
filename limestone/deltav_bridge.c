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

#include "deltav_bridge.h"
#include "deltav_util.h"

#include "dbms_deltav.h"

#include "dbms_bind.h"  /* for inserting new binds */
#include "bridge.h"     /* for sabridge_new_dbr_from_dbr */
#include "util.h"
#include "bridge.h"

#include <apr_strings.h>

dav_error *sabridge_vsn_control(const dav_repos_db *db,
                                dav_repos_resource *db_r)
{
    dav_error *err = NULL;
    int new_resourcetype = 0;
    dav_repos_resource *new_version;
    dav_repos_resource *vhr;

    /* Change the resource type */
    if (db_r->resourcetype == dav_repos_RESOURCE)
        new_resourcetype = dav_repos_VERSIONED;
    else if (db_r->resourcetype == dav_repos_COLLECTION)
        new_resourcetype = dav_repos_VERSIONED_COLLECTION;

    /* Start a transaction here */
    err = dbms_update_resource_type(db, db_r, new_resourcetype);

    /* create the VHR to hold the versions of this resource */
    err = sabridge_mk_vhr(db, db_r, &vhr);

    /* create the location where the versions will be stored */
    err = sabridge_mk_versions_collection(db, vhr);

    /* make a copy of the present state of the resource */
    err = sabridge_mk_new_version(db, db_r, vhr, 1, &new_version);

    if (db->dbms == MYSQL) {
        /* disable foriegn key checks for a while as MYSQL doesn't support 
         * deferrable constraints. Drop fk_vh_root_version of vhrs and 
         * fk_ve_vc_resource of versions table */
        dbms_drop_constraint(db_r->p, db, "vhrs", "fk_vh_root_version");
        dbms_drop_constraint(db_r->p, db, "versions", "fk_ve_vc_resource");
    }

    /* make a VHR entry */
    vhr->root_version_id = new_version->serialno;
    err = dbms_insert_vhr(db, vhr);

    new_version->vhr_id = vhr->serialno;
    db_r->vhr_id = vhr->serialno;

    /* make the new version entry */
    err = dbms_insert_version(db, new_version);

    /* make an entry into the VCR */
    db_r->checked_id = new_version->serialno;
    err = dbms_insert_vcr(db, db_r);

    if (db->dbms == MYSQL) {
        /* turn on foreign key checks again */
        dbms_add_constraint(db_r->p, db, "vhrs",
                            "CONSTRAINT fk_vh_root_version "
                            "FOREIGN KEY (root_version_id) "
                            "REFERENCES versions (resource_id) "
                            "ON DELETE CASCADE");
        dbms_add_constraint(db_r->p, db, "versions",
                            "CONSTRAINT fk_ve_vc_resource "
                            "FOREIGN KEY (vcr_id) "
                            "REFERENCES vcrs (resource_id) "
                            "ON DELETE SET NULL");
    }
    
    /* abort transaction if err */ 
    return err;
}

dav_error *sabridge_mk_versions_collection(const dav_repos_db *db,
                                           dav_repos_resource *vhr)
{
    apr_pool_t *pool = vhr->p;
    dav_repos_resource *vrs_coll = NULL;
    dav_error *err = NULL;

    TRACE();

    sabridge_new_dbr_from_dbr(vhr, &vrs_coll);

    vrs_coll->uri = 
      ap_make_dirstr_parent(pool, sabridge_mk_version_uri(vhr, 0));

    err = sabridge_mkcol_w_parents(db, vrs_coll);

    return err;
}

/** 
 * Creates a new (version history) resource
 * 
 * @param db handle to the database
 * @param vcr the resource for which a version history is being created
 * @param p_vhr the new resource will be returned by this variable
 * 
 * @return NULL on success, error otherwise
 */
dav_error *sabridge_mk_vhr(const dav_repos_db *db,
                           dav_repos_resource *vcr,
                           dav_repos_resource **p_vhr)
{
    apr_pool_t *pool = vcr->p;
    dav_repos_resource *vhr = NULL, *vhr_parent = NULL;
    dav_error *err = NULL;
    request_rec *rec = vcr->resource->info->rec;

    const dav_hooks_acl *acl_hooks = dav_get_acl_hooks(rec);

    TRACE();

    sabridge_new_dbr_from_dbr(vcr, &vhr);
    vhr->resourcetype = dav_repos_VERSIONHISTORY;
    /* we can not have dav_repos_insert_resource create a bind for the VHR 
       as we can calculate the URI only after the uuid is generated */
    vhr->uri = NULL;

    /* insert a new resource of type version history */
    if((err = dav_repos_create_resource(vhr->resource, 
                                        SABRIDGE_DELAY_BIND | 
                                        SABRIDGE_DELAY_ACL)))
        return err;

    /* calcuate the URI where the VHR will be bound */
    vhr->uri = sabridge_mk_vhr_uri(vhr);
 
    /* Create a bind in the history collection for this VHR */
    sabridge_new_dbr_from_dbr(vhr, &vhr_parent);
    vhr_parent->uri = ap_make_dirstr_parent(pool, vhr->uri);
    sabridge_mkcol_w_parents(db, vhr_parent);
    err = dbms_insert_bind(pool, db, vhr->serialno,
                     vhr_parent->serialno, basename(vhr->uri));

    vhr->parent_id = vhr_parent->serialno;
    
    /* Create an ACL for this resource */
    if(acl_hooks) {
        const dav_principal *owner = dav_principal_make_from_request(rec);
        err = (*acl_hooks->create_initial_acl)(owner, vhr->resource);
    }
    
    *p_vhr = vhr;

    return err;
}

dav_error *sabridge_mk_new_version(const dav_repos_db *db,
                                   dav_repos_resource *vcr,
                                   dav_repos_resource *vhr, int vr_num,
                                   dav_repos_resource **new_vr)
{
    apr_pool_t *pool = vcr->p;
    dav_repos_resource *new_version = NULL;
    dav_error *err = NULL;

    TRACE();

    sabridge_new_dbr_from_dbr(vcr, &new_version);
    new_version->uri = sabridge_mk_version_uri(vhr, vr_num);
    new_version->serialno = 0;
    new_version->getcontenttype = apr_pstrdup(pool, vcr->getcontenttype);
    /* Let db_insert_resource find creator_id based on creator_displayname */
    new_version->creator_id = 0;
    new_version->creator_displayname =
      apr_pstrdup(pool, vcr->resource->info->rec->user);

    /* create the first version of the resource */
    if (vcr->resourcetype == dav_repos_VERSIONED_COLLECTION) {
        new_version->resourcetype = dav_repos_COLLECTION_VERSION;
        dav_repos_create_resource(new_version->resource, 0);
        err = dbms_copy_resource_collection_version(db, vcr, new_version,
                                              vcr->resource->info->rec);
    } else if (vcr->resourcetype == dav_repos_VERSIONED) {
        new_version->resourcetype = dav_repos_VERSION;
        dav_repos_create_resource(new_version->resource, 0);
        err = sabridge_copy_medium(db, vcr, new_version);
    }

    new_version->vcr_id = vcr->serialno;
    new_version->version = vr_num;
    new_version->vhr_id = vcr->vhr_id;

    *new_vr = new_version;
    return err;
}

void sabridge_build_vcr_pr_hash(dav_repos_db *db, dav_repos_resource *db_r)
{
    apr_pool_t *pool = db_r->p;
    char *vr_uri;
    const char *href;
    char *buff;
    dav_repos_resource *vhr = NULL;

    if(db_r->resourcetype != dav_repos_VERSIONED &&
       db_r->resourcetype != dav_repos_VERSIONED_COLLECTION)
        return;

    sabridge_new_dbr_from_dbr(db_r, &vhr);
    vhr->serialno = db_r->vhr_id;
    dbms_get_property(db, vhr);

    /* Check in /Out status */
    vr_uri = sabridge_mk_version_uri(vhr, db_r->vr_num);
    href = dav_repos_mk_href(pool, vr_uri);
    if (db_r->checked_state == DAV_RESOURCE_CHECKED_IN) {
        apr_hash_set(db_r->vpr_hash, "checked-in", APR_HASH_KEY_STRING,
                     href);
        DBG1("checked-in: %s", href);
    } else if (db_r->checked_state == DAV_RESOURCE_CHECKED_OUT) {
        apr_hash_set(db_r->vpr_hash, "checked-out",
                     APR_HASH_KEY_STRING, href);
        DBG1("checked-out: %s", href);
        apr_hash_set(db_r->vpr_hash, "predecessor-set",
                     APR_HASH_KEY_STRING, href);

        buff = apr_psprintf(pool, "DAV:forbidden");
        apr_hash_set(db_r->vpr_hash, "checkout-fork",
                     APR_HASH_KEY_STRING, buff);
        apr_hash_set(db_r->vpr_hash, "checkin-fork",
                     APR_HASH_KEY_STRING, buff);

    }

    /* DAV:version-history RFC3253 Sec 5.2.1 */
    apr_hash_set(db_r->vpr_hash, "version-history",
                 APR_HASH_KEY_STRING, 
                 dav_repos_mk_href(pool, sabridge_mk_vhr_uri(vhr)));

    /* DAV:auto-version RFC 3253 Sec 3.2.2 */
    switch (db_r->autoversion_type) {
    case DAV_AV_CHECKOUT_CHECKIN:
        buff = apr_pstrdup(pool, "<D:checkout-checkin/>");
        break;
    case DAV_AV_CHECKOUT_UNLOCKED_CHECKIN:
        buff = apr_pstrdup(pool, "<D:checkout-unlocked-checkin/>");
        break;
    case DAV_AV_CHECKOUT:
        buff = apr_pstrdup(pool, "<D:checkout/>");
        break;
    case DAV_AV_LOCKED_CHECKOUT:
        buff = apr_pstrdup(pool, "<D:locked-checkout/>");
        break;
    default:
        buff = NULL;
    }
    if(buff)
        apr_hash_set(db_r->vpr_hash, "auto-version", APR_HASH_KEY_STRING,
                     buff);

    if(db_r->resourcetype == dav_repos_VERSIONED_COLLECTION) {
        apr_hash_set(db_r->vpr_hash, "eclipsed-set", APR_HASH_KEY_STRING, "");
    }

}

void sabridge_build_vr_pr_hash(dav_repos_db *db, dav_repos_resource *db_r)
{
    apr_pool_t *pool = db_r->p;
    char *buff;
    char *vname;
    const char *predecessor_set = "";
    const char *successor_set = "";
    dav_repos_resource *vcr_resource = NULL, *vhr_resource = NULL;

    if (db_r->resourcetype != dav_repos_VERSION &&
        db_r->resourcetype != dav_repos_COLLECTION_VERSION)
        return;

    buff = ap_make_dirstr_parent(pool, db_r->uri);

    if (db_r->version != 1) {
        predecessor_set =
          dav_repos_mk_href(pool,
                            apr_psprintf(pool, "%s%d", buff,
                                         db_r->version - 1));
    }

    /* Predecessor set */
    apr_hash_set(db_r->vpr_hash, "predecessor-set",
                 APR_HASH_KEY_STRING, predecessor_set);

    /* Add successor */
    if (db_r->lastversion == 0) {
        successor_set =
          dav_repos_mk_href(pool,
                            apr_psprintf(pool, "%s/%d", buff,
                                         db_r->version + 1));
    }
    /* Add Successor set  for hash table */
    apr_hash_set(db_r->vpr_hash, "successor-set",
                 APR_HASH_KEY_STRING, successor_set);

    /* checkout-set */
    sabridge_new_dbr_from_dbr(db_r, &vcr_resource);
    vcr_resource->serialno = db_r->vcr_id;
    if(db_r->resourcetype==dav_repos_VERSION)
        vcr_resource->resourcetype = dav_repos_VERSIONED;
    else vcr_resource->resourcetype = dav_repos_VERSIONED_COLLECTION;
    sabridge_reverse_lookup(db, vcr_resource);

    dbms_get_vcr_props(db, vcr_resource);
    if (vcr_resource->checked_state == DAV_RESOURCE_CHECKED_OUT
        && vcr_resource->checked_id == db_r->serialno)
        apr_hash_set(db_r->vpr_hash, "checkout-set",
                     APR_HASH_KEY_STRING, dav_repos_mk_href(pool,
                                                            vcr_resource->
                                                            uri));

    /* Version name */
    vname = apr_psprintf(pool, "V%d", db_r->version);
    apr_hash_set(db_r->vpr_hash, "version-name",
                 APR_HASH_KEY_STRING, vname);

    /* Display name */
    if (db_r->creator_displayname)
        apr_hash_set(db_r->vpr_hash, "creator-displayname",
                     APR_HASH_KEY_STRING, db_r->creator_displayname);

    /* Comment */
    if (db_r->comment)
        apr_hash_set(db_r->vpr_hash, "comment",
                     APR_HASH_KEY_STRING, db_r->comment);

    /* getcontentlength */
    buff = apr_psprintf(pool, "%ld", db_r->getcontentlength);
    apr_hash_set(db_r->vpr_hash, "getcontentlength",
                 APR_HASH_KEY_STRING, buff);

    /* getlast modified */
	
    if (db_r->resourcetype == dav_repos_VERSION) {
        char date[APR_RFC822_DATE_LEN] = "";
        dav_repos_format_strtime(DAV_STYLE_RFC822, db_r->updated_at, date);
        apr_hash_set(db_r->vpr_hash, "getlastmodified",
                     APR_HASH_KEY_STRING, apr_pstrdup(pool, date)); 
    }

    buff = apr_psprintf(pool, "DAV:forbidden");
    apr_hash_set(db_r->vpr_hash, "checkout-fork",
                 APR_HASH_KEY_STRING, buff);
    apr_hash_set(db_r->vpr_hash, "checkin-fork",
                 APR_HASH_KEY_STRING, buff);

    /* DAV:version-history RFC3253 Sec 5.3.1 */
    /* version-history */
    sabridge_new_dbr_from_dbr(vcr_resource, &vhr_resource);
    vhr_resource->serialno = vcr_resource->vhr_id;
    vhr_resource->uri = NULL;
    dbms_get_property(db, vhr_resource);
    apr_hash_set(db_r->vpr_hash, "version-history",
                 APR_HASH_KEY_STRING, dav_repos_mk_href(pool,
                                                        sabridge_mk_vhr_uri
                                                        (vhr_resource)));


    if(db_r->resourcetype == dav_repos_COLLECTION_VERSION) {
        dav_repos_resource *child_vhrs, *old_next;
        old_next = db_r->next;
        sabridge_get_collection_children(db, db_r, 1, NULL,
                                         NULL, NULL, NULL);
        child_vhrs = db_r->next;
        db_r->next = old_next;
        buff = "";
        while(child_vhrs) {
            buff = apr_pstrcat(pool, buff,
                               "<D:version-controlled-binding>",
                               "<D:binding-name>",child_vhrs->displayname,"</D:binding-name>",
                               "<D:version-history>",
                               dav_repos_mk_href(pool, sabridge_mk_vhr_uri(child_vhrs)),
                               "</D:version-history>",
                               "</D:version-controlled-binding>", NULL);
            child_vhrs = child_vhrs->next;
        }
        apr_hash_set(db_r->vpr_hash, "version-controlled-binding-set", APR_HASH_KEY_STRING, buff);
    }

}

void sabridge_build_vhr_pr_hash(dav_repos_db *d, dav_repos_resource *vhr)
{
    apr_pool_t *pool = vhr->p;
    const char *root_version_href = NULL;
    dav_repos_resource *versions = NULL;
    char *version_set = "";
    
    if(vhr->resourcetype != dav_repos_VERSIONHISTORY)
        return;

    if(vhr->root_version_id == 0)
        dbms_get_vhr_root_version_id(d, vhr);

    dbms_get_vhr_versions(d, vhr, &versions); 

    while (versions) {
        const char *href = dav_repos_mk_href(pool, versions->uri);
        if (!root_version_href && 
            (versions->serialno == vhr->root_version_id))
            root_version_href = href;
        version_set = apr_pstrcat(pool, version_set, href, NULL);
        versions = versions->next;
    }

    /* Root version */
    apr_hash_set(vhr->vpr_hash, "root-version", APR_HASH_KEY_STRING,
                 root_version_href);

    apr_hash_set(vhr->vpr_hash, "version-set", APR_HASH_KEY_STRING,
                 version_set);

}

/* @brief Build version property   
 * @param db DB connection struct containing the user, password, and DB name
 * @param db_r contains the uuid, root_path and the pool 
 * Should not return for <allprop> 
*/
void sabridge_build_vpr_hash(dav_repos_db * db, dav_repos_resource * db_r)
{
    apr_pool_t *pool = db_r->p;

    /* Let's build version hash */
    db_r->vpr_hash = apr_hash_make(pool);

    switch(db_r->resourcetype){
    case dav_repos_VERSIONED:
    case dav_repos_VERSIONED_COLLECTION:
        sabridge_build_vcr_pr_hash(db, db_r);
        break;
    case dav_repos_VERSION:
    case dav_repos_COLLECTION_VERSION:
        sabridge_build_vr_pr_hash(db, db_r);
        break;
    case dav_repos_VERSIONHISTORY:;
        sabridge_build_vhr_pr_hash(db, db_r);
        break;
    default:
        db_r->vpr_hash = NULL;
    }
    return;
}

dav_error *sabridge_remove_vhr_vrs(const dav_repos_db *d,
                                   dav_repos_resource *vhr)
{
    dav_repos_resource *vhr_vrs;
    dav_error *err = NULL;

    TRACE();

    dbms_get_vhr_versions(d, vhr, &vhr_vrs);
    while (vhr_vrs) {
        if (vhr_vrs->resourcetype == dav_repos_VERSION) {
            sabridge_remove_body_from_disk(d, vhr_vrs);
        } else if (vhr_vrs->resourcetype == dav_repos_COLLECTION_VERSION)
            sabridge_coll_clear_children(d, vhr_vrs);

        err = dbms_delete_resource(d, vhr->p, vhr_vrs);
        if (err) return err;
        vhr_vrs = vhr_vrs->next;
    }
    return err;
}

/** 
 * Removes all the versions in the VHR and un-version-controls any VCRs that 
 * had their versions in this VHR
 * 
 * @param d handle to the database
 * @param vhr the version  history resource
 * 
 * @return NULL on success, error otherwise
 */
dav_error *sabridge_remove_vhr(const dav_repos_db *d,
                               dav_repos_resource *vhr)
{
    dav_error *err = NULL;

    TRACE();

    err = dbms_unversion_vhr_vcrs(d, vhr);
    if (err) return err;
    err = sabridge_remove_vhr_vrs(d, vhr);
    return err;
}
