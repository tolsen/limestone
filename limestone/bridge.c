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

#include "dav_repos.h"
#include "bridge.h"
#include "dbms.h"
#include "dbms_bind.h"
#include "dbms_redirect.h"      /* dbms_get_redirect_props */
#include "dbms_deltav.h"

#include "util.h"
#include "acl.h" /* for create_initial_acl */
#include "deltav_bridge.h" /* for sabridge_remove_vhr */
#include "version.h" /* for dav_repos_version_control */
#include "dbms_principal.h"
#include "dbms_quota.h"

#include <apr_strings.h>
#include <apr_uuid.h>

dav_error *sabridge_get_property(const dav_repos_db *d, dav_repos_resource *r)
{
    apr_pool_t *pool = r->p;
    dav_error *err = NULL;
    long serialno = r->serialno;
    const dbms_bind_list *bind = NULL;
    int uri_not_found = 0;

    TRACE();

    if (r->serialno)
        serialno = r->serialno;
    else if (r->uri && r->root_path && strstr(r->uri, r->root_path)) {
        err = dbms_lookup_uri(pool, d, r->uri + strlen(r->root_path), &bind);
        if (err || bind == NULL) return err;

        serialno = bind->resource_id;
        if (bind->resource_id == -1) {
            uri_not_found = 1;
            serialno = bind->parent_id;
        }
    }

    if (!serialno > 0)
        return err;

    r->serialno = serialno;
    dbms_get_resource(d, r);

    if (uri_not_found && r->resourcetype != dav_repos_REDIRECT) {
        r->serialno = r->resourcetype = r->owner_id = r->creator_id = 0;
        r->created_at = r->displayname = r->getcontentlanguage = NULL;
        r->comment = NULL;
        r->next = NULL;
        r->uuid = NULL;
        return err;
    }

    if (bind) {
        r->bind_id = bind->bind_id;
        r->updated_at = bind->updated_at;
    }

    if (r->resourcetype == dav_repos_REDIRECT) {
        dbms_get_redirect_props(d, r);
        if (bind) {
            const char *absent_uri;
            if (bind->uri)
                absent_uri = r->uri + strlen(bind->uri);
            else
                absent_uri = "";
            r->reftarget = apr_pstrcat(pool, r->reftarget, absent_uri, NULL);
        }
    }

    if (r->resourcetype == dav_repos_COLLECTION
        || r->resourcetype == dav_repos_VERSIONED_COLLECTION) {
        err = dbms_get_collection_props(d, r);
        if (err) return err;
        
        /* collection contenttype hack */
        r->getcontenttype = apr_pstrdup(r->p, DIR_MAGIC_TYPE);
        if (err) return err;
    }

    if((err = dbms_get_media_props(d, r)))
        return err;

    err = dbms_get_deltav_props(d, r);

    return err;
}

dav_error *sabridge_create_empty_body(const dav_repos_db *d,
                                      dav_repos_resource *r)
{
    char *empty_file_path;
    apr_file_t *empty_file;
    dav_error *err = NULL;
    apr_pool_t *pool = r->p;

    /* hardcoding sha1 of an empty file */
    r->sha1str = "da39a3ee5e6b4b0d3255bfef95601890afd80709";

    err = dbms_insert_media(d, r);
    if (err) return err;

    /* Create the empty sha1 file */
    generate_path(&empty_file_path, pool, d->file_dir, r->sha1str);
    if (apr_file_open
        (&empty_file, empty_file_path, APR_READ | APR_CREATE,
         APR_OS_DEFAULT, pool) == APR_SUCCESS)
        apr_file_close(empty_file);
    else
        return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "Filesystem error");

    return NULL;
}

dav_error *sabridge_deliver(dav_repos_db * db, dav_repos_resource * db_r,
			ap_filter_t * output)
{
    dav_error *err = NULL;
    char *filename = NULL;
    apr_file_t *fd;
    apr_bucket_brigade *bb;
    apr_bucket *bkt;

    TRACE();

    err = generate_path(&filename, db_r->p, db->file_dir, db_r->sha1str);
    if (err)
        return err;

    DBG1("TESTCDK: %s", filename);
    if (apr_file_open(&fd, filename, APR_READ | APR_BINARY, APR_OS_DEFAULT,
		      db_r->p) != APR_SUCCESS) {
	return dav_new_error(db_r->p, HTTP_INTERNAL_SERVER_ERROR, 0,
			     "Could not open file");
    }

    bb = apr_brigade_create(db_r->p, output->c->bucket_alloc);

    /* ### this does not handle large files. but this is test code anyway */
    bkt = apr_bucket_file_create(fd, 0,
				 (apr_size_t) db_r->getcontentlength,
				 db_r->p, output->c->bucket_alloc);
    APR_BRIGADE_INSERT_TAIL(bb, bkt);

    bkt = apr_bucket_eos_create(output->c->bucket_alloc);
    APR_BRIGADE_INSERT_TAIL(bb, bkt);

    if (ap_pass_brigade(output, bb) != APR_SUCCESS)
	err = dav_new_error(db_r->p, HTTP_INTERNAL_SERVER_ERROR, 0,
			    "Could not write contents to filter.");

    if (!db->file_dir)
	apr_file_remove(filename, db_r->p);	//ignore the error?
    return err;
}

void sabridge_new_dbr_from_dbr(const dav_repos_resource *db_r,
                                dav_repos_resource **res_p)
{
    if (*res_p != NULL) {
        apr_pool_t *pool = db_r->p;
        dav_repos_resource *new_dbr = *res_p;
        new_dbr->p = pool;
        new_dbr->root_path = db_r->root_path;
        return;
    }

    if (db_r->resource && db_r->resource->info) {
        dav_resource *new_resource = NULL;
        dav_repos_new_resource(db_r->resource->info->rec, db_r->root_path,
                               &new_resource);
        *res_p = new_resource->info->db_r;
    }
}

dav_error *sabridge_insert_resource(const dav_repos_db *d, 
                                    dav_repos_resource *r, 
                                    request_rec *rec, int params)
{
    apr_pool_t *pool = r->p;
    dav_error *err = NULL;
    const dav_hooks_acl *acl_hooks = dav_get_acl_hooks(rec);

    if (!r->parent) {
        if (r->parent_id > 0) {
            sabridge_new_dbr_from_dbr(r, &r->parent);
            r->parent->serialno = r->parent_id;
            err = dbms_get_resource(d, r->parent);
            err = dbms_get_collection_props(d, r->parent);
            if (err) goto error;
        } else if (r->uri) {
            sabridge_retrieve_parent(r, &r->parent);
        }
    }

    /* Get a new UUID */
    r->uuid = get_new_plain_uuid(r->p);
    
    /* Set created/updated times */
    r->created_at = time_apr_to_str(r->p, apr_time_now());
    r->updated_at = r->created_at;

    /* Set owner/creator IDs */
    const dav_principal *principal = dav_principal_make_from_request(rec);
    r->creator_id = dav_repos_get_principal_id(principal);

    if (!r->owner_id)
        r->owner_id = r->creator_id;

    if (!r->displayname) {
        if (r->uri) {
            r->displayname = apr_pstrdup(pool, basename(r->uri));
        }
        else {
            r->displayname = "";
        }
    }

    if (r->getcontenttype == NULL)
        r->getcontenttype = apr_pstrdup(pool, "application/octet-stream");

    if (r->parent)
        r->limebar_state = r->parent->limebar_state;

    /* Create a 'resources' table entry */
    if((err = dbms_insert_resource(d, r)))
        return err;

    if (r->resourcetype == dav_repos_RESOURCE
	|| r->resourcetype == dav_repos_VERSIONED
	|| r->resourcetype == dav_repos_VERSION) {
        err = sabridge_create_empty_body(d, r);
        if (err) goto error;
    }

    if (r->resourcetype == dav_repos_COLLECTION
        || r->resourcetype == dav_repos_VERSIONED_COLLECTION) {
        r->av_new_children = r->parent->av_new_children;
        err = dbms_insert_collection(d, r);
        if (err) goto error;
    }
    
    if (r->resourcetype == dav_repos_USER
        || r->resourcetype == dav_repos_GROUP) {
        err = dbms_insert_principal(d, r, basename(r->uri));
        if (err) goto error;
    }

    if (r->resourcetype == dav_repos_USER) {
        err = dbms_insert_quota(pool, d, r->serialno, d->quota);
        if (err) goto error;
        r->owner_id = r->serialno;
        err = dbms_set_property(d, r);
        if (err) goto error;
    }

    /* Create the bind if url is set */
    if (r->uri && (params & SABRIDGE_DELAY_BIND) == 0) {
        err = dbms_insert_bind(pool, d, r->serialno, 
                                r->parent->serialno, basename(r->uri));
        if (err) goto error;
    }

    /* Create an ACL for this resource */
    if(acl_hooks && !(params & SABRIDGE_DELAY_ACL)) {
        const dav_principal *owner = dav_principal_make_from_request(rec);
        err = (*acl_hooks->create_initial_acl)(owner, r->resource);
        if (err) goto error;
    }

    return NULL;

 error:
    /* we don't need to delete the resource because we use transactions */
    return err;
}				/*End of sabridge_insert_resource */

dav_error *sabridge_retrieve_parent(const dav_repos_resource *db_r,
                                    dav_repos_resource **pparent)
{
    dav_error *err = NULL;
    dav_resource *parent_resource = NULL;
    err = dav_repos_get_parent_resource(db_r->resource, &parent_resource);
    dav_repos_resource *parent = parent_resource->info->db_r;
    *pparent = parent;
    return err;
}

dav_error *sabridge_mkcol_w_parents(const dav_repos_db *db,
                                    dav_repos_resource *db_r)
{
    apr_pool_t *pool = db_r->p;
    dav_error *err = NULL;
    
    char *last = NULL;		/* apr_strtok internal state */
    char *next_dir;

    int search_if_coll_exists = 1;

    int curr_base = ROOT_COLLECTION_ID;
    long new_coll_id;
    char *path;
    dav_repos_resource *new_coll_res = NULL;

    TRACE();

    sabridge_new_dbr_from_dbr(db_r, &new_coll_res);
    new_coll_res->resourcetype = dav_repos_COLLECTION;

    path = db_r->uri + strlen(db_r->root_path);
    DBG1("path %s\n", path);
    next_dir = apr_strtok(apr_pstrdup(pool, path), "/", &last);

    while(next_dir != NULL) {
	/* Check if folder already exists */
	if (search_if_coll_exists) {

            dbms_get_bind_resource_id(pool, db, curr_base, next_dir,
                                      &new_coll_id);
	    if (new_coll_id == 0) {
		search_if_coll_exists = 0;
	    } else {
		curr_base = new_coll_id;
		next_dir = apr_strtok(NULL, "/", &last);
		continue;
	    }
	}

	new_coll_res->serialno = 0;
        new_coll_res->parent_id = curr_base;

	if((err = dav_repos_create_resource
            (new_coll_res->resource, SABRIDGE_DELAY_BIND)))
            return err;
        new_coll_id = new_coll_res->serialno;
        
        if((err = dbms_insert_bind(pool, db, new_coll_id, curr_base, next_dir)))
            return err;

	curr_base = new_coll_id;
	next_dir = apr_strtok(NULL, "/", &last);
    }

    db_r->serialno = new_coll_id;

    return err;
}

dav_error *sabridge_get_collection_children(const dav_repos_db *db,
                                            dav_repos_resource *db_r,
                                            int depth,
                                            const char *acl_priv,
                                            dav_repos_resource **plink_head,
                                            dav_repos_resource **plink_tail,
                                            int *pnum_items)
{
    apr_hash_t *retr_rs; /* hash storing retrieved resources */
    dav_error *err = NULL;
    dav_repos_resource *link_head, *link_tail;
    dav_repos_resource *next_level_children, *next_level_tail;
    int num_new_items, total_items;

    TRACE();

    if (depth == 0) {
        if (plink_head) *plink_head = NULL;
        if (plink_tail) *plink_tail = NULL;
        return NULL;
    }

    err = dbms_get_collection_resource(db, db_r, db_r, acl_priv,
                                       &link_head, &link_tail, &num_new_items);
    if (err) return err;
    total_items = num_new_items;

    retr_rs = apr_hash_make(db_r->p);
    apr_hash_set(retr_rs, &(db_r->serialno), sizeof(long), (void*)db_r);
    
    next_level_children = link_head;
    while (next_level_children) {
        dav_repos_resource *iter = next_level_children;
        do {
            iter->bind = apr_hash_get(retr_rs, &(iter->serialno), sizeof(long));
            /* associate the id with the last retrieved bind to it */
            apr_hash_set(retr_rs, &(iter->serialno), sizeof(long), iter);
        } while ((iter = iter->next) != NULL);

        if (depth == DAV_INFINITY) {
            /* update next_level_children to the next level */
            err = dbms_get_collection_resource
              (db, next_level_children, next_level_tail, acl_priv, 
               &next_level_children, &next_level_tail, &num_new_items);
            if (err) return err;

            if (next_level_children) {
                link_tail->next = next_level_children;
                link_tail = next_level_tail;
                total_items += num_new_items;
            }

        } else next_level_children = next_level_tail = NULL;
    }

    /* pass on the children to the caller */
    if (plink_head) *plink_head = link_head;
    else db_r->next = link_head;
    if (plink_tail) *plink_tail = link_tail;
    if (pnum_items) *pnum_items = total_items;
    return NULL;
}

dav_error *sabridge_copy_medium_w_create(const dav_repos_db *d,
                                         dav_repos_resource *r_src,
                                         dav_repos_resource *r_dst,
                                         request_rec *rec)
{
    dav_error *err = NULL;
    
    TRACE();

    if (r_dst->serialno && r_dst->resourcetype != dav_repos_RESOURCE &&
        r_dst->resourcetype != dav_repos_VERSIONED &&
        r_dst->resourcetype != dav_repos_LOCKNULL) {
        err = sabridge_unbind_resource(d, r_dst);
        if (err) return err;
        r_dst->serialno = 0;
    }

    if (!r_dst->serialno) {
        r_dst->resourcetype = dav_repos_RESOURCE;
        sabridge_insert_resource(d, r_dst, rec, 0);
    }
    
    if (r_dst->resourcetype == dav_repos_LOCKNULL)
        sabridge_create_empty_body(d, r_dst);

    err = sabridge_copy_medium(d, r_src, r_dst);
    return err;
}

dav_error *sabridge_copy_medium(const dav_repos_db *db,
                                const dav_repos_resource *r_src,
                                dav_repos_resource *r_dest)
{
    apr_pool_t *pool = r_src->p;
    dav_error *err = NULL;

    TRACE();

    if (r_dest->sha1str)
        sabridge_remove_body_from_disk(db, r_dest);

    if (strcmp(r_src->displayname, r_dest->displayname)) {
        r_dest->displayname = r_src->displayname;
        dbms_update_displayname(db, r_dest);
    }

    err = dbms_copy_media_props(db, r_src, r_dest);
    if (err) return err;

    err = sabridge_copy_dead_props(pool, db, r_src->serialno, r_dest->serialno);

    /*
    if (err) return err;

    return sabridge_copy_bitmarks(pool, db, r_src->resource->info->rec, 
                                  r_src, r_dest);*/

    return err;
}

dav_error *sabridge_copy_coll_w_create(const dav_repos_db *d,
                                       dav_repos_resource *r_src,
                                       dav_repos_resource *r_dst,
                                       int depth,
                                       request_rec *rec,
                                       dav_response **response)
{
    apr_pool_t *pool = r_src->p;
    int create_dest = 0;
    int dest_prev_id = 0;
    dav_repos_resource *dst_parent;
    dav_error *err = NULL;

    TRACE();

    if (r_dst->serialno == 0)
        create_dest = 1;
    else if (r_dst->resourcetype != dav_repos_COLLECTION &&
             r_dst->resourcetype != dav_repos_VERSIONED_COLLECTION &&
             r_dst->resourcetype != dav_repos_LOCKNULL) {
        create_dest = 1;
        dest_prev_id = r_dst->serialno;
    }

    if (create_dest) {
        r_dst->resourcetype = dav_repos_COLLECTION;
        err = sabridge_retrieve_parent(r_dst, &dst_parent);
        if (err) return err;
        r_dst->parent_id = dst_parent->serialno;

        err = sabridge_insert_resource(d, r_dst, rec, SABRIDGE_DELAY_BIND);
        if (err) return err;
    }

    err = sabridge_copy_dead_props(pool, d, r_src->serialno, r_dst->serialno);
    if (err) return err;

    /*
    err = sabridge_copy_bitmarks(pool, d, rec, r_src, r_dst);
    if (err) return err;*/

    if (depth == DAV_INFINITY)
        err = sabridge_depth_inf_copy_coll(d, r_src, r_dst, rec, response);
    if (err) return err;

    if (create_dest) {
        if (dest_prev_id) {
            dav_repos_resource *prev_dst = NULL;
            sabridge_new_dbr_from_dbr(r_dst, &prev_dst);
            prev_dst->serialno = dest_prev_id;
            prev_dst->uri = r_dst->uri;
            err = sabridge_unbind_resource(d, prev_dst);
            if (err) return err;
        }

        err = dbms_insert_bind(pool, d, r_dst->serialno, dst_parent->serialno,
                               basename(r_dst->uri));
        if (err) return err;
    }

    return NULL;    
}

dav_error *sabridge_copy_collection(const dav_repos_db *d,
                                    const dav_repos_resource *src,
                                    dav_repos_resource *dst)
{
    dav_error *err = NULL;

    TRACE();

    if (strcmp(src->displayname, dst->displayname)) {
        dst->displayname = src->displayname;
        err = dbms_update_displayname(d, dst);
        if (err) return err;
    }

    err = sabridge_copy_dead_props(src->p, d, src->serialno, dst->serialno);

    return err;
    /*
    if (err) return err;

    return sabridge_copy_bitmarks(src->p, d, src->resource->info->rec,
                                  src, dst);*/
}

dav_error *sabridge_copy_if_compatible(const dav_repos_db *d,
                                       const dav_repos_resource *src,
                                       dav_repos_resource *dst,
                                       int *p_copied)
{
    dav_error *err = NULL;
    int checkin = 0;

    TRACE();

    *p_copied = 0;

    switch (src->resourcetype) {
    case dav_repos_RESOURCE:
    case dav_repos_VERSIONED:
        if (dst->checked_state == DAV_RESOURCE_CHECKED_IN) {
            if (dst->autoversion_type == DAV_AV_CHECKOUT_CHECKIN) {
                err = dav_repos_checkout
                  (dst->resource, 1, 0, 0, 0, NULL, NULL);
                if (err) return err;
                checkin = 1;
            } else
                return dav_new_error
                  (src->p, HTTP_CONFLICT, 0,
                   "DAV:cannot-modify-version-controlled-content");
        }
    case dav_repos_VERSION:

        switch (dst->resourcetype) {
        case dav_repos_LOCKNULL:
            sabridge_create_empty_body(d, dst);
        case dav_repos_RESOURCE:
        case dav_repos_VERSIONED:
            err = sabridge_copy_medium(d, src, dst);
            if (err) return err;
            *p_copied = 1;
        }
        break;
    case dav_repos_COLLECTION:
    case dav_repos_VERSIONED_COLLECTION:
        switch (dst->resourcetype) {
        case dav_repos_LOCKNULL:
        case dav_repos_COLLECTION:
        case dav_repos_VERSIONED_COLLECTION:
            err = sabridge_copy_collection(d, src, dst);
            if (err) return err;
            *p_copied = 1;
        }
        break;
    }

    if (checkin)
        err = dav_repos_checkin(dst->resource, 0, NULL);

    return err;
}

dav_error *sabridge_clear_unused(const dav_repos_db *d,
                                 dav_repos_resource *res,
                                 apr_hash_t *in_use)
{
    apr_pool_t *pool = res->p;
    apr_hash_t *cleared = apr_hash_make(pool);
    dav_repos_resource *iter, *link_tail;
    dav_error *err = NULL;


    TRACE();

    for (iter = link_tail = res; iter; iter = iter->next) {
        if (apr_hash_get(cleared, &iter->serialno, sizeof(long)))
            continue;

        if (apr_hash_get(in_use, &iter->serialno, sizeof(long))) {
            if (iter->resourcetype == dav_repos_COLLECTION ||
                iter->resourcetype == dav_repos_VERSIONED_COLLECTION)
                err = sabridge_get_collection_children
                  (d, iter, 1, NULL, &link_tail->next, &link_tail, NULL);
        } else {
            err = sabridge_unbind_resource(d, iter);
            if (err) return err;
        }
        apr_hash_set(cleared, &iter->serialno, sizeof(long), (void *)1);
    }
    return NULL;
}

#define AHKS APR_HASH_KEY_STRING

dav_error *sabridge_depth_inf_copy_coll(const dav_repos_db *d,
                                        dav_repos_resource *r_src,
                                        dav_repos_resource *r_dst,
                                        request_rec *rec,
                                        dav_response **p_response)
{
    apr_pool_t *pool = r_src->p;
    apr_hash_t *copy_map = apr_hash_make(pool);
    apr_hash_t *dst_in_use = apr_hash_make(pool);
    dbms_bind_list *copy_binds_list;
    int num_items=0, i=0;
    dav_repos_resource *iter;
    dav_error *err = NULL;

    TRACE();

    apr_hash_set(copy_map, r_src->uri, AHKS, r_dst);
    apr_hash_set(dst_in_use, &r_dst->serialno, sizeof(long), (void *)1);

    sabridge_get_collection_children(d, r_src, DAV_INFINITY, "read",
                                     &iter, NULL, &num_items);
    copy_binds_list = apr_pcalloc(pool, sizeof(dbms_bind_list)*num_items);

    while (iter) {
        dav_repos_resource *copy_res = NULL;
        dav_repos_resource *parent_copy = 
          apr_hash_get(copy_map, get_parent_uri(iter->p, iter->uri), AHKS);
        int dst_reused = 0;

        if (iter->bind)
            copy_res = apr_hash_get(copy_map, iter->bind->uri, AHKS);
        else if (apr_hash_get(dst_in_use, &parent_copy->serialno, sizeof(parent_copy->serialno))){
            dav_repos_resource *corr_child = NULL;
            sabridge_new_dbr_from_dbr(r_dst, &corr_child);
            corr_child->uri = apr_psprintf(pool, "%s%s", r_dst->uri,
                                           iter->uri + strlen(r_src->uri));
            if ((err = sabridge_get_property(d, corr_child)))
                return err;
            if (corr_child->serialno != 0 && 
                !apr_hash_get(dst_in_use, &corr_child->serialno, sizeof(long))){
                err = sabridge_copy_if_compatible(d, iter, corr_child, &dst_reused);
                if (err) {
                    dav_response *resp = apr_pcalloc(pool, sizeof(*resp));
                    resp->href = corr_child->uri;
                    resp->desc = err->desc;
                    resp->status = err->status;
                    resp->next = *p_response;
                    *p_response = resp;
                    dst_reused = 1;
                }
                if (dst_reused) {
                    copy_res = corr_child;
                    apr_hash_set(dst_in_use, &corr_child->serialno, 
                                 sizeof(long), (void *)1);
                }
            }
        }

        if (copy_res == NULL) {
            err = sabridge_create_copy(d, iter, rec, parent_copy, &copy_res);
            if (err) return err;
        }

        apr_hash_set(copy_map, iter->uri, AHKS, copy_res);

        if (!dst_reused) {
            copy_binds_list[i].parent_id = parent_copy->serialno;
            copy_binds_list[i].bind_name = basename(iter->uri);
            copy_binds_list[i].resource_id = copy_res->serialno;
            i++;
        }
        
        iter = iter->next;
    }

    err = sabridge_clear_unused(d, r_dst, dst_in_use);
    if (err) return err;
    if (i > 0) err = dbms_insert_bind_list(pool, d, copy_binds_list, i);
    return err;
}

#undef AHKS

dav_error *sabridge_coll_clear_children(const dav_repos_db *d,
                                        dav_repos_resource *coll)
{
    dav_repos_resource *iter;
    dav_error *err = NULL;

    sabridge_get_collection_children(d, coll, 1, NULL, &iter, NULL, NULL);
    while (iter) {
        if (iter->bind) {
            dbms_delete_bind(coll->p, d, coll->serialno, iter->serialno,
                             basename(iter->uri));
        } else {
            err = sabridge_unbind_resource(d, iter);
            if (err) return err;
        }
        iter = iter->next;
    }
    return NULL;
}

dav_error *sabridge_copy_dead_props(apr_pool_t *pool, const dav_repos_db *d,
                                    long src_id, long dest_id)
{
    dav_error *err = NULL;

    TRACE();

    err = dbms_delete_dead_props(pool, d, dest_id);
    if (err) return err;

    err = dbms_copy_dead_props(pool, d, src_id, dest_id);
    return err;
}

static dav_repos_resource *get_bitmarks_coll(apr_pool_t *pool, 
                                             const dav_repos_db *d,
                                             const dav_repos_resource *r)
{
    dav_repos_resource *bitmarks_coll = NULL;
    sabridge_new_dbr_from_dbr(r, &bitmarks_coll);
    bitmarks_coll->uri = apr_psprintf(pool, "/bitmarks/%s", r->uuid);
    sabridge_get_property(d, bitmarks_coll);

    return bitmarks_coll;
}

dav_error *sabridge_copy_bitmarks(apr_pool_t *pool, const dav_repos_db *d,
                                  request_rec *r, 
                                  const dav_repos_resource *src, 
                                  const dav_repos_resource *dst)
{
    dav_error *err = NULL;
    dav_repos_resource *src_bitmarks_coll = NULL;
    dav_repos_resource *dst_bitmarks_coll = NULL;

    TRACE();
    
    /* if src is a bitmark collection, we have nothing to copy */
    if (src->uri && strlen(src->uri) > 10 
        && (strncmp("/bitmarks/", src->uri, 10) == 0)) {
        return err;
    }

    src_bitmarks_coll = get_bitmarks_coll(pool, d, src);
    if (!src_bitmarks_coll->serialno)
        return err;     /* we don't have any bitmarks to copy */

    dst_bitmarks_coll = get_bitmarks_coll(pool, d, dst);

    return sabridge_copy_coll_w_create(d, src_bitmarks_coll, 
                                       dst_bitmarks_coll, DAV_INFINITY,
                                       r, NULL);
}

dav_error *sabridge_create_copy(const dav_repos_db *d,
                                dav_repos_resource *db_r,
                                request_rec *rec,
                                dav_repos_resource *copy_parent,
                                dav_repos_resource **pcopy_res)
{
    dav_repos_resource *copy = NULL;
    dav_error *err = NULL;

    TRACE();

    sabridge_new_dbr_from_dbr(db_r, &copy);
    copy->parent_id = copy_parent->serialno;
    copy->parent = copy_parent;
    copy->displayname = db_r->displayname;
    copy->uri = NULL;

    if (db_r->resourcetype == dav_repos_RESOURCE ||
        db_r->resourcetype == dav_repos_VERSIONED ||
        db_r->resourcetype == dav_repos_VERSION) {
        /* No autoversioning for now */
        copy->resourcetype = dav_repos_RESOURCE;
        dav_repos_create_resource
          (copy->resource, SABRIDGE_DELAY_BIND );
        err = sabridge_copy_medium(d, db_r, copy);
        if (err) return err;
        switch (copy_parent->av_new_children) {
        case DAV_AV_CHECKOUT_CHECKIN:
        case DAV_AV_CHECKOUT_UNLOCKED_CHECKIN:
        case DAV_AV_CHECKOUT:
        case DAV_AV_LOCKED_CHECKOUT:
        case DAV_AV_VERSION_CONTROL:
            err = dav_repos_vsn_control(copy->resource, NULL);
            break;
        default:
            ;
        }
        if (err) return err;
    } else if (db_r->resourcetype == dav_repos_COLLECTION ||
               db_r->resourcetype == dav_repos_VERSIONED_COLLECTION) {
        /* No autoversioning for now */
        copy->resourcetype = dav_repos_COLLECTION;
        dav_repos_create_resource(copy->resource, SABRIDGE_DELAY_BIND );
        err = sabridge_copy_collection(d, db_r, copy);
        if (err) return err;
    }
    *pcopy_res = copy;
    return NULL;
}

dav_error *sabridge_reverse_lookup(const dav_repos_db *d,
                                   dav_repos_resource *db_r)
{
    dav_error *err = NULL;
    char *path_from_root = NULL;

    err = dbms_find_shortest_path(db_r->p, d, ROOT_COLLECTION_ID,
                                  db_r->serialno, &path_from_root);
    if(err) return err;

    if (strcmp(path_from_root, "") == 0) {
        path_from_root = "/";
    }

    db_r->uri = apr_psprintf(db_r->p, "%s%s", db_r->root_path,
                             path_from_root);

    return NULL;
}

dav_error *sabridge_delete_if_orphaned(const dav_repos_db *db,
                                       dav_repos_resource *db_r, int *deleted)
{
    char *path_from_root = NULL;
    dav_error *err = NULL;

    err = dbms_find_shortest_path(db_r->p, db, ROOT_COLLECTION_ID,
                                  db_r->serialno, &path_from_root);
    if (err) return err;

    if (path_from_root) {
        if (deleted) *deleted = 0;
    } else {
        err = sabridge_delete_resource(db, db_r);
        if (err) return err;
        if (deleted) *deleted = 1;
    }
    return NULL;
}

dav_error *sabridge_unbind_resource(const dav_repos_db *db,
                                    dav_repos_resource *db_r)
{
    apr_pool_t *pool = db_r->p;
    dav_repos_resource *link_tail;
    dav_repos_resource *iter;
    dav_repos_resource *db_r_next_bak = db_r->next;
    int deleted;
    dav_error *err = NULL;

    if (db->use_gc) {
        if (db_r->uri) {
            dav_repos_resource *parent = NULL;
            err = sabridge_retrieve_parent(db_r, &parent);
            if (err) return err;
            err = dbms_delete_bind(pool, db, parent->serialno, db_r->serialno,
                                   basename(db_r->uri));
        }
        if (!err) {
            dbms_bind_list *bind = apr_pcalloc(pool, sizeof(dbms_bind_list));
            bind->resource_id = db_r->serialno;
            err = dbms_insert_cleanup_reqs(pool, db, bind);
        }
        return err;
    }
    
    db_r->next = NULL;
    iter = db_r;
    link_tail = iter;

    if (db_r->uri && !db_r->parent_id) {
        dav_repos_resource *parent = NULL;
        err = sabridge_retrieve_parent(db_r, &parent);
        if (err) return err;
        db_r->parent_id = parent->serialno;
    }

    while(iter && !err) {
        dav_repos_resource *child_link_head = NULL, *child_link_tail = NULL;
        if(iter->bind) {
            iter = iter->next;
            continue;
        }

        if (iter->resourcetype == dav_repos_COLLECTION ||
            iter->resourcetype == dav_repos_VERSIONED_COLLECTION)
            err = sabridge_get_collection_children
              (db, iter, 1, NULL, &child_link_head, &child_link_tail, NULL);
        if (!err && iter->uri)
            err = dbms_delete_bind(pool, db, iter->parent_id, iter->serialno,
                                   basename(iter->uri));
        if (!err) err = sabridge_delete_if_orphaned(db, iter, &deleted);

        if (!err && deleted && child_link_head) {
            link_tail->next = child_link_head;
            link_tail = child_link_tail;
        }

        iter = iter->next;
    }
    db_r->next = db_r_next_bak;

    return err;
}

dav_error *sabridge_delete_resource(const dav_repos_db *d,
                                    dav_repos_resource *db_r)
{
    dav_error *err = NULL;

    TRACE();

    switch (db_r->resourcetype) {
    case dav_repos_VERSIONHISTORY:
        err = sabridge_remove_vhr(d, db_r);
        if(err) return err;
        break;
    case dav_repos_RESOURCE:
    case dav_repos_VERSIONED:
    case dav_repos_VERSION:
        sabridge_remove_body_from_disk(d, db_r);
        break;
    }

    err = dbms_delete_resource(d, db_r->p, db_r);
    return err;
}

void sabridge_remove_body_from_disk(const dav_repos_db *d,
                                    dav_repos_resource *db_r)
{
    TRACE();

    /* remove file if there is only one remaining body pointing to it */
    if ( !d->keep_files && dbms_num_sha1_resources(db_r->p, d, db_r->sha1str) == 1)
        remove_sha1_file(db_r->p, d->file_dir, db_r->sha1str);
}

int sabridge_get_parent_id(dav_repos_resource *db_r)
{
    dav_repos_resource *parent = NULL;

    if(db_r->parent_id)
        return db_r->parent_id;
    else {
        if(db_r->uri || db_r->resource->uri) {
            if((sabridge_retrieve_parent(db_r, &parent)))
                return 0;
            else
                return parent->serialno;
        }
    }
    return 0;
}

int sabridge_acl_check_preconditions(const dav_acl *acl, const dav_acl *acldest)
{
    int retVal = OK;
    dav_ace_iterator *i, *j;
    
    i = dav_acl_iterate(acl);
    while(dav_ace_iterator_more(i)) {
        const dav_ace *ace_i = dav_ace_iterator_next(i);
        int is_deny_i = dav_is_deny_ace(ace_i);
        const dav_principal *principal_i = dav_get_ace_principal(ace_i);
        const dav_privileges *privileges_i = dav_get_ace_privileges(ace_i);
        const char *principal_name_i = basename(dav_repos_principal_to_s(principal_i));
        
        j = dav_acl_iterate(acldest);
        while(dav_ace_iterator_more(j)) {
            const dav_ace *ace_j = dav_ace_iterator_next(j);

            int is_deny_j = dav_is_deny_ace(ace_j);

            /* check if ace_i & ace_j are conflicting */
            if(!(is_deny_i * is_deny_j) && (is_deny_i + is_deny_j)) {
                /* one is a grant another is a deny */
                const dav_principal *principal_j = dav_get_ace_principal(ace_j);
                const char *principal_name_j = basename(dav_repos_principal_to_s(principal_j));
        
                /* now check principals */
                /* should we be matching complete URIs instead of names here? */
                if(0 == strcmp(principal_name_i, principal_name_j)) {
                    /* both aces belong to same principal */
                    const dav_privileges *privileges_j = dav_get_ace_privileges(ace_j);
                    
                    /* now check privileges */
                    if(sabridge_check_privileges_overlap(privileges_i, privileges_j))
                        retVal = HTTP_CONFLICT;
                }
            }
        }
    }
    
    return retVal;
}

int sabridge_check_privileges_overlap(const dav_privileges *p, 
                                      const dav_privileges *q)
{
    dav_privilege_iterator *i, *j;

    i = dav_privilege_iterate(p);
    while(dav_privilege_iterator_more(i)) {
        const dav_privilege *privilege_p = dav_privilege_iterator_next(i);

        j = dav_privilege_iterate(q);
        while(dav_privilege_iterator_more(j)) {
            const dav_privilege *privilege_q = dav_privilege_iterator_next(j);
            if(0 == strcmp(privilege_p->name, privilege_q->name))
                return 1;
        }
    }

    return 0;
}

const char *sabridge_getetag_dbr(const dav_repos_resource * db_r)
{
    if (db_r->sha1str) {
        return apr_psprintf(db_r->p, "\"%s\"", db_r->sha1str);
    }

    return NULL;
}

void sabridge_get_new_file(const dav_repos_db *db, 
                           dav_repos_resource *db_r, 
                           char **path)
{
    apr_file_t *empty_file;
    char *empty_file_path;

    empty_file_path = apr_psprintf(db_r->p, "%s/%s", db->tmp_dir, db_r->uuid);

    if (apr_file_open
        (&empty_file, empty_file_path, APR_READ | APR_CREATE | APR_TRUNCATE,
         APR_OS_DEFAULT, db_r->p) == APR_SUCCESS)
        apr_file_close(empty_file);

    *path = empty_file_path;
}

void sabridge_get_resource_file(const dav_repos_db *db,
                                dav_repos_resource *db_r,
                                char **path)
{
    char *file_path;
    char *sha1_file;

    file_path = apr_psprintf(db_r->p, "%s/%s", db->tmp_dir, db_r->uuid);
    generate_path(&sha1_file, db_r->p, db->file_dir, db_r->sha1str);

    apr_file_copy(sha1_file, file_path, APR_FILE_SOURCE_PERMS, db_r->p);

    *path = file_path;
}

void sabridge_put_resource_file(const dav_repos_db *db, 
                                dav_repos_resource *db_r,
                                char *file)
{
    char *sha1_file;

    generate_path(&sha1_file, db_r->p, db->file_dir, db_r->sha1str);

    if (APR_SUCCESS != apr_file_rename(file, sha1_file, db_r->p))
        DBG2("Error while moving file from %s to %s", file, sha1_file);
}

/**
 * Get used bytes for a given resource
 * @param d DB connection struct
 * @param r the resource
 * @param prin_only count only resources owned by principal making this request
 * @return the bytes used by r
 */
long sabridge_get_used_bytes(const dav_repos_db *d, dav_repos_resource *r,
                             int prin_only)
{
    dav_repos_resource *iter;
    int used_bytes = 0;
    int num_items = 0;
    long user_id;
    TRACE();

    if (prin_only) {
        const dav_principal *principal = 
          dav_principal_make_from_request(r->resource->info->rec);
        user_id = dav_repos_get_principal_id(principal);
    }

    if (r->resourcetype != dav_repos_COLLECTION && r->owner_id == user_id)
        return r->getcontentlength;
    else
        sabridge_get_collection_children(d, r, DAV_INFINITY, "read",
                                         &iter, NULL, &num_items);

    while (iter) {

        if (!iter->bind &&
            (iter->resourcetype == dav_repos_RESOURCE ||
            iter->resourcetype == dav_repos_VERSIONED ||
            iter->resourcetype == dav_repos_VERSION) &&
            (!prin_only || iter->owner_id == user_id)) 
            used_bytes += iter->getcontentlength;

        iter = iter->next;
    }

    return used_bytes;
}

dav_error *sabridge_verify_user_email_unique(apr_pool_t *pool,
                                             const dav_repos_db *d,
                                             const char *email)
{
    const char *errmsg = apr_pstrcat(pool, "email conflict for ", email, NULL);
    dav_error *err = dav_new_error_tag(pool, HTTP_CONFLICT, 0 /* error_id */,
                                       errmsg, LIMEBITS_NS, "email-available",
                                       NULL /* content */, 
                                       NULL /* prolog */);

    TRACE();

    if (dbms_is_email_available(pool, d, email)) {
        return NULL;
    }

    return err;
}

/**
 * Tries to set email for user, checking if it is already taken
 */
dav_error *sabridge_set_user_email(apr_pool_t *pool, const dav_repos_db *d,
                                   long principal_id, const char *email)
{
    dav_error *err = sabridge_verify_user_email_unique(pool, d, email);

    if (err) {
        return err;
    }

    return dbms_set_user_email(pool, d, principal_id, email);
}

#define AHKS APR_HASH_KEY_STRING

dav_error *sabridge_get_namespace_id(const dav_repos_db *d, const dav_repos_resource *db_r,
                                     const char *namespace, long *ns_id)
{
    dav_repos_cache *cache = sabridge_get_cache(db_r->resource->info->rec);
    long *value;

    if (!(value = (long *)apr_hash_get(cache->namespaces, namespace, AHKS))) {
        dav_error *err = dbms_get_namespace_id(db_r->p, d, namespace, ns_id);
        if (err) {
            return err;    
        }

        value = apr_pcalloc(db_r->p, sizeof(*value));
        *value = *ns_id;
        apr_hash_set(cache->namespaces, namespace, AHKS, value);
    }
    
    *ns_id = *value;
    return NULL;
}

dav_repos_cache *sabridge_get_cache(request_rec *r)
{
    request_rec *root = r;
    while(root->main) {
        root = root->main;    
    }
    
    return (dav_repos_cache *)apr_table_get(root->notes, "dav_repos_cache");
}
