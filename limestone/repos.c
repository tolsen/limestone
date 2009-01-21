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
#include <time.h>
#include <mod_dav.h>

#include <apr.h>
#include <apr_strings.h>
#include <apr_hash.h>
#include <apr_tables.h>
#include <apr_file_io.h>

#include "dav_repos.h"
#include "dbms.h"
#include "dbms_bind.h"
#include "util.h"
#include "bridge.h"
#include "deltav_bridge.h"      /* for build_vpr_hash */
#include "acl.h"
#include "bind.h"
#include "props.h"              /* for dav_repos_build_pr_hash */
#include "liveprops.h"          /* for dav_repos_build_lpr_hash */
#include "principal.h"          /* for dav_repos_create_user */
#include "dbms_principal.h"

dav_error *dav_repos_new_resource(request_rec *r, const char *root_path, 
                                  dav_resource **result_resource)
{   
    dav_resource_private *ctx;
    dav_resource *resource;
    dav_repos_db *db;
    dav_repos_resource *db_r;
    apr_pool_t *pool = r->pool;

    resource = apr_pcalloc(pool, sizeof(*resource));
    ctx = apr_pcalloc(pool, sizeof(*ctx));

    /* Read data from server_config */
    db = dav_repos_get_db(r);
    if (db == NULL)
        return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "Couldn't initialize connection to database");

    db_r = apr_pcalloc(pool, sizeof(*db_r));

    ctx->db = db;
    ctx->db_r = db_r;
    ctx->finfo = r->finfo;
    ctx->pool = pool;
    ctx->rec = r;

    /* let's fill db_r */
    db_r->p = pool;

    db_r->root_path = apr_pstrdup(pool, root_path);

    resource->info = ctx;
    resource->hooks = &dav_repos_hooks_repos;
    resource->pool = r->pool;

    /* Set return value */
    *result_resource = resource;
    db_r->resource = resource;
    return NULL;
}

/*
** get resource and return to result_resource
*/
static dav_error *dav_repos_get_resource(request_rec * r,
					 const char *root_prefix,
					 const char *label,
					 int use_checked_in,
					 dav_resource ** result_resource)
{
    char *s;
    /* Let's decide the type */
    dav_resource *resource;
    dav_repos_db *db;
    dav_repos_resource *db_r;
    apr_pool_t *pool = r->pool;
    dav_error *err = NULL;
    char *root_path;

    TRACE();

    root_path = apr_pstrdup(r->pool, root_prefix);
    dav_repos_chomp_slash(root_path);
    err = dav_repos_new_resource(r, root_path, result_resource);
    if (err) return err;
    resource = *result_resource;
    db_r = resource->info->db_r;
    db = resource->info->db;

    /*
     ** If there is anything in the path_info, then this indicates that the
     ** entire path was not used to specify the file/dir. We want to append
     ** it onto the filename so that we get a "valid" pathname for null
     ** resources.
     */
    s = apr_pstrcat(pool, r->filename, r->path_info, NULL);

    /* make sure the pathname does not have a trailing "/" */
    dav_repos_chomp_slash(s);

    resource->info->pathname = s;

    /* make sure the URI does not have a trailing "/" */
    s = apr_pstrdup(pool, r->uri);
    dav_repos_chomp_trailing_slash(s);

    resource->uri = s;
    db_r->uri = s;

    /* exist check */
    if((err = sabridge_get_property(db, db_r)))
        return err;

    if(db_r->serialno) {
        dav_repos_update_dbr_resource(db_r);

	/* Do I need to set finfo ? */
	r->finfo.fname = resource->info->pathname;
	r->finfo.size = db_r->getcontentlength;

        /* Need to set r->path_info to NULL if resource is locknull */
        /* see dav_get_resource_state */
        if (db_r->resourcetype == dav_repos_LOCKNULL)
            r->path_info = NULL;

	resource->working = (db_r->checked_state == DAV_RESOURCE_CHECKED_OUT);
    } else {
	/* Calculate depth */
	db_r->depth = ap_count_dirs((const char *) resource->info->pathname);
	resource->exists = 0;

	resource->type = DAV_RESOURCE_TYPE_REGULAR;

	r->path_info = "";
    }

    return NULL;
}

/**
 * Get the parent resource.
 * @param resource The resource whose parents needs to be found.
 * @param result_parent result struct containing the parent resource
 */
/* Should we return NULL, if no parent ? */
dav_error *dav_repos_get_parent_resource(const dav_resource * resource,
					 dav_resource ** result_parent)
{
    dav_resource_private *ctx = resource->info;
    dav_resource_private *parent_ctx;
    dav_resource *parent_resource;
    dav_repos_db *db = resource->info->db;
    apr_pool_t *pool = resource->pool;
    dav_repos_resource *db_r;
    char *dirpath;
    char *uri;
    char *resource_uri;
    dav_error *err = NULL;

    TRACE();

    if(resource->info->db_r->uri)
        resource_uri = apr_pstrdup(pool, resource->info->db_r->uri);
    else if(resource->uri)
        resource_uri = apr_pstrdup(pool, resource->uri);
    else
        return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "URI not set for dav_repos_get_parent_resource.");
        
    /* If given resource is root, then there is no parent */
    if (strcmp(resource_uri, "/") == 0 || 
        (ctx->pathname && strcmp(ctx->pathname, "/") == 0)) {
	*result_parent = NULL;
	return NULL;
    }

    /* ### optimize this into a single allocation! */

    /* Create private resource context descriptor */
    parent_ctx = apr_pcalloc(pool, sizeof(*parent_ctx));

    /* ### this should go away */
    parent_ctx->pool = pool;

    if(ctx->pathname) {
        dirpath = ap_make_dirstr_parent(pool, ctx->pathname);
        dav_repos_chomp_slash(dirpath);
        parent_ctx->pathname = dirpath;
    }
    parent_ctx->rec = ctx->rec;

    /* Set parent resource */
    parent_resource = apr_pcalloc(pool, sizeof(*parent_resource));

    parent_resource->info = parent_ctx;
    parent_resource->hooks = &dav_repos_hooks_repos;
    parent_resource->pool = pool;

    uri = ap_make_dirstr_parent(pool, resource_uri);
    dav_repos_chomp_trailing_slash(uri);
    parent_resource->uri = uri;

    /* fill DBR to check collection type */
    db_r = apr_pcalloc(resource->pool, sizeof(*db_r));
    parent_ctx->db_r = db_r;
    parent_ctx->db = db;

    db_r->p = pool;
    db_r->uri = apr_pstrdup(resource->pool, parent_resource->uri);
    /* Always set root_path for new resource */
    db_r->root_path = resource->info->db_r->root_path;
    db_r->resource = parent_resource;

    /* exist check */
    if((err = sabridge_get_property(db, db_r)))
        return err;

    if(db_r->serialno) {
        dav_repos_update_dbr_resource(db_r);
        /* Set the parent_id */
        resource->info->db_r->parent_id = db_r->serialno;

    } else {
	parent_resource->exists = 0;
    }

    *result_parent = parent_resource;
    db_r->resource = parent_resource;
    return NULL;
}

static int dav_repos_is_same_resource(const dav_resource * res1,
				      const dav_resource * res2)
{
    TRACE();

    if (res1->hooks != res2->hooks)
	return 0;

    return strcmp(res1->uri, res2->uri) == 0;
}

/* Check 
**   parent : res1
**   child  : res2
* TODO - Fix this according to BINDS spec.
*/
static int dav_repos_is_parent_resource(const dav_resource * res1,
					const dav_resource * res2)
{

    /**
     * TODO: Fix this. Currently since we are using multiple bindings 
     * and this function is being called in mod_dav.c 
     * we are always returning 0.     
     * Need to modify the behaviour and change mod_dav.c 
     **/
    return 0;

    const char *parent_uri;
    TRACE();

    if (res1->hooks != res2->hooks)
	return 0;

    if (res1->exists == 0)
	return 0;

    parent_uri = ap_make_dirstr_parent(res2->pool, res2->uri);
    return strcmp(res1->uri, parent_uri) == 0;
}

static dav_error *dav_repos_check_dst_parent(dav_resource *dst,
                                             dav_resource **dst_parent)
{
    apr_pool_t *pool = dst->pool;
    dav_error *err = NULL;

    TRACE();

    /* Check dest's parent */
    err = dav_repos_get_parent_resource(dst, dst_parent);

    if (err) {
	err = dav_push_error(pool, err->status, 0,
			     "Could not fetch parent resource information.",
			     err);
	return err;
    }

    if ((*dst_parent)->exists == 0 || (*dst_parent)->collection == 0) {
	err = dav_new_error(pool, HTTP_CONFLICT, 0,
			     "No parent collection.");
    }

    return err;
}

static dav_error *dav_repos_put_user(dav_stream *stream) {
    apr_pool_t *pool = stream->p;
    dav_repos_resource *db_r = stream->db_r;
    dav_repos_db *db = stream->db;
    dav_resource *resource = db_r->resource;
    dav_error *err = NULL;

    apr_xml_parser *parser = NULL;
    apr_xml_doc *doc = NULL;
    apr_xml_elem *passwd_elem = NULL, *displayname_elem = NULL, *email_elem = NULL;
    const char *passwd = NULL;

    TRACE();

    apr_file_open(&(stream->file), stream->path, APR_READ, APR_OS_DEFAULT, pool);
    apr_xml_parse_file(pool, &parser, &doc, stream->file, 200);
    if (!doc || !dav_validate_root_no_ns(doc, "user"))
        return dav_new_error(pool, HTTP_BAD_REQUEST, 0,
                             stream->path);
    passwd_elem = dav_find_child_no_ns(doc->root, "password");
    email_elem = dav_find_child_no_ns(doc->root, "email");
    displayname_elem = dav_find_child_no_ns(doc->root, "displayname");

    if (passwd_elem)
        apr_xml_to_text(pool, passwd_elem, APR_XML_X2T_INNER, 
                        doc->namespaces, NULL, &passwd, NULL);

    if (stream->inserted) {
        if (passwd_elem == NULL)
            return dav_new_error(pool, HTTP_BAD_REQUEST, 0,
                                 "password required for new user");
        if (email_elem == NULL)
            return dav_new_error(pool, HTTP_BAD_REQUEST, 0,
                                 "email required for new user");
        if (displayname_elem == NULL)
            return dav_new_error(pool, HTTP_BAD_REQUEST, 0,
                                 "displayname required for new user");

        err =  dav_repos_create_user(resource, passwd);
    } else {
        if (passwd_elem != NULL || email_elem != NULL) {
            /* Existing user trying to change password or email */
            apr_xml_elem *cur_passwd_elem = dav_find_child_no_ns(doc->root, "cur_password");
            if (cur_passwd_elem == NULL) {
                return dav_new_error(pool, HTTP_BAD_REQUEST, 0,
                                     "current password not provided");
            } else {
                const char *cur_passwd;
                apr_xml_to_text (pool, cur_passwd_elem, APR_XML_X2T_INNER, 
                                 doc->namespaces, NULL, &cur_passwd, NULL);
                const char *cur_pwhash = dbms_get_user_pwhash(pool, db, db_r->serialno);
                const char *pwhash = get_password_hash(pool, basename(db_r->uri), cur_passwd);
                if (!(cur_pwhash && pwhash && !strcmp(cur_pwhash, pwhash)))
                    return dav_new_error(pool, HTTP_BAD_REQUEST, 0,
                                         "password did not match");
            }

            if (passwd_elem != NULL)
                err = dav_repos_update_password(resource, passwd);
        }
    }

    if (displayname_elem) {
        apr_xml_to_text(pool, displayname_elem, APR_XML_X2T_INNER, 
                        doc->namespaces, NULL, &db_r->displayname, NULL);
        dbms_set_property(db, db_r);
    }
    if (email_elem) {
        const char *email = NULL;
        apr_xml_to_text (pool, email_elem, APR_XML_X2T_INNER, 
                         doc->namespaces, NULL, &email, NULL);
        dbms_set_principal_email(pool, db, db_r->serialno, email);
    }

    apr_file_close(stream->file);
    return NULL;
}

/**
 * Open stream for writing. 
 * @param resource Resource which will feed the stream
 * @param mode The mode in which stream is to be opened
 * @param stream  The stream to which we will write
 * @return 0 indicating success
 * Note that this is only called for write (PUT).
*/
static dav_error *dav_repos_open_stream(const dav_resource * resource,
					dav_stream_mode mode,
					dav_stream ** stream)
{
    dav_error *err = NULL;
    request_rec *rec = resource->info->rec;
    apr_pool_t *pool = resource->info->pool;
    dav_stream *ds;
    dav_repos_db *db = resource->info->db;
    dav_repos_resource *db_r = (dav_repos_resource *) resource->info->db_r;

    TRACE();

    ds = apr_pcalloc(pool, sizeof(*ds));
    ds->p = db_r->p;
    ds->rec = rec;
    ds->db = db;
    ds->db_r = db_r;

    if (!rec->content_type) {
        rec->content_type = apr_table_get(rec->headers_in, "Content-Type");
    }

    if (rec->content_type && *rec->content_type == 0)
        rec->content_type = NULL;
    
    if(rec->content_type)
        ds->content_type = apr_pstrdup(ds->p, rec->content_type);

    if (db_r->serialno == 0) {
        int insert_flags = 0;
        /* no autoversioning at this layer */
        if(strncmp(resource->uri, USER_PATH, strlen(USER_PATH)) == 0) {
            insert_flags = SABRIDGE_DELAY_ACL;
            db_r->resourcetype = dav_repos_USER;
        } else if(strncmp(resource->uri, GROUP_PATH, strlen(GROUP_PATH)) == 0) 
            db_r->resourcetype = dav_repos_GROUP;
        else
            db_r->resourcetype = dav_repos_RESOURCE;
        
        if((err = sabridge_insert_resource(db, db_r, rec, insert_flags)))
            return err;
        ds->inserted = 1;
    }

    if ((ds->path = (char *)apr_table_get(rec->notes, "put_stream_done"))) {
        *stream = ds;
        return NULL;
    }

    switch (mode) {
    case DAV_MODE_WRITE_TRUNC:
        sabridge_get_new_file(db, db_r, &(ds->path));
        break;
    case DAV_MODE_WRITE_SEEKABLE:
        sabridge_get_resource_file(db, db_r, &(ds->path));
        /* Should we fail if the given content_type is different from 
           the existing getcontenttype? */
	break;
    }

    if (apr_file_open(&(ds->file), ds->path,
                      APR_WRITE | APR_BINARY | APR_BUFFERED, APR_OS_DEFAULT,
                      db_r->p) != APR_SUCCESS)
        err =
          dav_new_error(db_r->p, HTTP_INTERNAL_SERVER_ERROR, 0,
                        "Unable to open file for write");
    
    *stream = ds;
    return err;
}

static dav_error *dav_repos_close_stream(dav_stream * stream, int commit)
{
    apr_pool_t *pool = stream->p;
    dav_repos_resource *db_r = stream->db_r;
    dav_repos_db *db = stream->db;
    dav_error *err = NULL;
    request_rec *r = db_r->resource->info->rec;

    TRACE();

    if (apr_table_get(stream->rec->notes, "put_stream_done") == NULL) {
        if (apr_file_close(stream->file) != APR_SUCCESS)
            return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                                 "Couldn't close stream file");
        apr_table_setn(stream->rec->notes, "put_stream_done", stream->path);
    }

    if (commit) {
        if (db_r->resourcetype == dav_repos_LOCKNULL) {
            err = sabridge_create_empty_body(db, db_r);
            if (err) return err;
            db_r->resourcetype = dav_repos_RESOURCE;
        }

        if(db_r->resourcetype == dav_repos_RESOURCE ||
           db_r->resourcetype == dav_repos_VERSIONED) {
            if (is_content_type_good(stream->path, r->content_type)) {
                /* if we were supplied a Content-Type by the client, use it */
                db_r->getcontenttype = r->content_type;
            }
            else {
                db_r->getcontenttype = get_mime_type(db_r->uri , stream->path);
            }

            compute_file_sha1(pool, stream->path, &(db_r->sha1str));
            db_r->getcontentlength = get_file_length(pool, stream->path);

            if((err = dbms_set_property(db, db_r)))
                return err;

            if ((err = dbms_update_media_props(db, db_r)))
                return err;
            sabridge_put_resource_file(db, db_r, stream->path);
        }
        else if(db_r->resourcetype == dav_repos_USER) {
            err = dav_repos_put_user(stream);
            if (err) return err;
        }

        apr_file_remove(stream->path, pool);
    } else {
        /* PUT aborted */
        if (stream->inserted)
            sabridge_delete_resource(db, db_r);
    }

    return err;
}

static dav_error *dav_repos_write_stream(dav_stream * stream,
					 const void *buf,
					 apr_size_t bufsize)
{
    apr_size_t s = bufsize;
    TRACE();

    if (apr_table_get(stream->rec->notes, "put_stream_done"))
        return NULL;

    /* We can't update sha1 here because of Content-Range PUTs */
    if (apr_file_write(stream->file, buf, &s) != APR_SUCCESS)
	return dav_new_error(stream->db_r->p, HTTP_INTERNAL_SERVER_ERROR,
			     0, "Unable to write to file.");
    if (s != bufsize)
	return dav_new_error(stream->db_r->p, HTTP_INTERNAL_SERVER_ERROR,
			     0, "Did not write all contents.");

     return NULL;
}

static dav_error *dav_repos_seek_stream(dav_stream * stream,
					apr_off_t abs_pos)
{
    apr_off_t p;
    TRACE();

    if (apr_table_get(stream->rec->notes, "put_stream_done"))
        return NULL;

    if (apr_file_seek(stream->file, APR_SET, &p) != APR_SUCCESS)
	return dav_new_error(stream->db_r->p, HTTP_INTERNAL_SERVER_ERROR,
			     0, "Unable to seek in file.");
    if (p != abs_pos)
	return dav_new_error(stream->db_r->p, HTTP_INTERNAL_SERVER_ERROR,
			     0, "Seek resulted in different position.");
    return NULL;
}

/**
 * Set appropriate response headers
 * @param r request struct
 * @param resource The resource whose properties were requested. 
*/
static dav_error *dav_repos_set_headers(request_rec * r,
					const dav_resource * resource)
{
    dav_repos_resource *db_r = (dav_repos_resource *) resource->info->db_r;
    const char *etag = dav_repos_getetag(resource);

    TRACE();

    if (!resource->exists)
	return NULL;

    /* make sure the proper mtime is in the request record */
    ap_update_mtime(r, dav_repos_parse_time(db_r->updated_at));

    /* ### note that these use r->filename rather than <resource> */
    ap_set_last_modified(r);

    /* For a directory, we will send text/html or text/xml. */
    if (db_r->resourcetype == dav_repos_COLLECTION) {
        /* do not send a Etag for a collection */
	r->content_type = "text/html";
    } else {

        /* generate our etag and place it into the output */
        if(etag)
            apr_table_setn(r->headers_out, "ETag", etag);

        /* we accept byte-ranges */
        apr_table_setn(r->headers_out, "Accept-Ranges", "bytes");

        if (db_r->getcontenttype) 
            r->content_type = db_r->getcontenttype;
    }

    return NULL;
}

static const char *pretty_print_uri(apr_pool_t *p, const char *req_uri)
{
    const char *uri = NULL;
    const char *href;
    char *last = NULL;
    char *token = NULL;
    const char *pretty_uri = NULL;

    uri = reverse_string(apr_pstrdup(p, req_uri));

    token = apr_strtok(apr_pstrdup(p, uri), "/", &last);
    if (token) {
        token = reverse_string(apr_pstrdup(p, token));
        if (uri[0] == '/')
            href = "./";
        else href = apr_psprintf(p, "./%s/", token);

        pretty_uri = apr_psprintf(p, "<A HREF=\"%s\">%s</A>", href, token);
        token = apr_strtok(NULL, "/", &last);
    }
    
    while(token != NULL) {
        token = reverse_string(apr_pstrdup(p, token));
        href = apr_pstrcat(p, href, "../", NULL);
        pretty_uri = apr_pstrcat(p, "<A HREF=\"", href ,"\">", token, "</A> > ", pretty_uri, NULL);
        token = apr_strtok(NULL, "/", &last);
    }

    return pretty_uri;
}

static dav_error *dav_repos_deliver(const dav_resource * resource,
				    ap_filter_t * output)
{
    char sep = '/';
    char *ztmp;

    apr_pool_t *pool = resource->pool;
    apr_bucket_brigade *bb;
    apr_status_t status;
    apr_bucket *bkt;

    dav_repos_db *db = resource->info->db;
    dav_repos_resource *tmp_r;
    dav_repos_resource *db_r = (dav_repos_resource *) resource->info->db_r;
    dav_error *err = NULL;

    TRACE();

    if (db_r->resourcetype == dav_repos_COLLECTION ||
        db_r->resourcetype == dav_repos_VERSIONED_COLLECTION) {
        request_rec *r = resource->info->rec;

        /* redirect to a URL with trailing slash if this is not a sub-request */
        if (!r->main && r->uri[strlen(r->uri) - 1] != '/') {
            char *correct_url = apr_pstrcat(pool, dav_get_response_href(r, r->uri), "/", NULL);
            apr_table_setn(r->headers_out, "Location", correct_url);
            return dav_new_error(pool, HTTP_MOVED_PERMANENTLY, 0,
                                 "Need / at the end of collection");
        }

        /** 
         * The html response sent for GET on a collection.
         */
	bb = apr_brigade_create(resource->pool, output->c->bucket_alloc);

	/* Will be filled children */
        err = sabridge_get_collection_children(db, db_r, 1, NULL,
                                               NULL, NULL, NULL);
	if (err) return err;

	ap_fprintf(output, bb,
		   "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2 Final//EN\">");

        ap_fprintf(output, bb, "<html>\n");

        if (db->css_uri) {
            ap_fprintf(output, bb, "<head> <link href=\"%s\" rel=\"stylesheet\""
                       "media=\"screen\" type=\"text/css\"/></head> \n", 
                       db->css_uri);
        }

	ap_fprintf(output, bb, "<body>\n <h2>%s</h2>\n", pretty_print_uri(pool, r->uri));

	/* Add '..' */
	ap_fprintf(output, bb, "\n<ul>\n");
	ztmp =
	    apr_pstrndup(pool, db_r->uri,
			 strrchr(db_r->uri, sep) + 1 - db_r->uri);

        ap_fprintf(output, bb,
                   "<li><a href=\"../\"><small>../</small></a><br>\n");

        /* Add the children */
        tmp_r = db_r->next;

	while(tmp_r) {
            const char *child_basename = basename(tmp_r->uri);

	    /* Collection */
	    if (tmp_r->resourcetype == dav_repos_COLLECTION) {
		ap_fprintf(output, bb, "<li><A HREF=\"%s/\">%s/</A><BR>\n",
			   child_basename, child_basename);
	    } else {
		ap_fprintf(output, bb, "<li><A HREF=\"%s\">%s</A><BR>\n",
			   child_basename, child_basename);
	    }
            tmp_r = tmp_r->next;
	}

	ap_fputs(output, bb,
		 " </ul>\n <hr noshade><em>Powered by "
		 "Limestone " VERSION "." "</em>\n</body></html>");

	bkt = apr_bucket_eos_create(output->c->bucket_alloc);
	APR_BRIGADE_INSERT_TAIL(bb, bkt);
	if ((status = ap_pass_brigade(output, bb)) != APR_SUCCESS) {
	    return dav_new_error(resource->pool,
				 HTTP_INTERNAL_SERVER_ERROR, 0,
				 "Could not write EOS to filter.");
	}
	return NULL;
    }
    return sabridge_deliver(db, db_r, output);
}

dav_error *dav_repos_create_resource(dav_resource *resource, int params)
{
    dav_repos_db *db = resource->info->db;
    dav_repos_resource *db_r = (dav_repos_resource *) resource->info->db_r;
    dav_error *err = NULL;

    /* Insert the new resource into the database */
    if(resource->collection) {
        db_r->resourcetype = dav_repos_COLLECTION;
        db_r->getcontenttype = apr_pstrdup(db_r->p, DIR_MAGIC_TYPE);
    }
    else if(!db_r->resourcetype)
        db_r->resourcetype = dav_repos_RESOURCE;
    
    if((err = sabridge_insert_resource(db, db_r, resource->info->rec, params)))
        return err;

    dav_repos_update_dbr_resource(db_r);
    return err;
}



/**
 * Create a collection
 * @param resource The resource that needs to be created as collection. 
 */
static dav_error *dav_repos_create_collection(dav_resource * resource)
{

    dav_repos_resource *db_r = (dav_repos_resource *) resource->info->db_r;
    dav_repos_db *db = resource->info->db;
    dav_error *err = NULL;

    TRACE();

    if (resource->type == DAV_RESOURCE_TYPE_VERSION ||
	resource->type == DAV_RESOURCE_TYPE_HISTORY)
	return dav_new_error(db_r->p, HTTP_CONFLICT, 0,
			     "Version resource.");

    db_r->getcontenttype = apr_pstrdup(db_r->p, DIR_MAGIC_TYPE);

    db_r->resourcetype = dav_repos_COLLECTION;
        
    if (db_r->serialno == 0) {
        err = sabridge_insert_resource(db, db_r, resource->info->rec, 0);
    } else { /* overwriting a locknull resource */
        dav_repos_resource *parent_dbr = NULL;

        err = dbms_set_property(db, db_r);
        if (err) return err;

        sabridge_new_dbr_from_dbr(db_r, &parent_dbr);
        parent_dbr->serialno = db_r->parent_id;
        err = dbms_get_collection_props(db, parent_dbr);
        if (err) return err;
        db_r->av_new_children = parent_dbr->av_new_children;
        err = dbms_insert_collection(db, db_r);
    }
    if (err) return err;

    resource->exists = 1;

    return err;
}

/**
 * Copy a resource 
 * @param src The source URI to be copied.
 * @param dst The desination URI
 * @param depth The depth to which the source URI has to be copied.
 * @param response The response pointer to be populated
*/
static dav_error *dav_repos_copy_resource(const dav_resource * src,
					  dav_resource * dst, int depth,
					  dav_response **p_response)
{
    dav_error *err = NULL;
    dav_resource *dst_parent;
    dav_repos_db *db = src->info->db;
    dav_repos_resource *db_r_src = (dav_repos_resource *) src->info->db_r;
    dav_repos_resource *db_r_dst = (dav_repos_resource *) dst->info->db_r;
    request_rec *rec = src->info->rec;

    TRACE();

    *p_response = NULL;

    /* If it's a version resource */
    if (src->type == DAV_RESOURCE_TYPE_HISTORY ||
	dst->type == DAV_RESOURCE_TYPE_VERSION ||
	dst->type == DAV_RESOURCE_TYPE_HISTORY) {
	return dav_new_error(db_r_src->p, HTTP_METHOD_NOT_ALLOWED , 0,
			     "Can not copy or move version resource.");
    }

    if((err = dav_repos_check_dst_parent(dst, &dst_parent)))
        return err;

    dav_repos_chomp_slash(db_r_src->uri);
    dav_repos_chomp_slash(db_r_dst->uri);
   
    /* check src and des url */
    if (strcmp(db_r_src->uri, db_r_dst->uri) == 0) {
	return dav_new_error(db_r_src->p, HTTP_BAD_REQUEST, 0,
			     "Source and destination are same while copying DBMS.");
    }

    switch (db_r_src->resourcetype) {
    case dav_repos_RESOURCE:
    case dav_repos_VERSIONED:
    case dav_repos_VERSION:
        err = sabridge_copy_medium_w_create(db, db_r_src, db_r_dst, rec);
        break;
    case dav_repos_COLLECTION:
    case dav_repos_VERSIONED_COLLECTION:
        err = sabridge_copy_coll_w_create(db, db_r_src, db_r_dst, depth,
                                          rec, p_response);
        break;
    }

    if (*p_response)
        err = dav_new_error(db_r_src->p, HTTP_MULTI_STATUS, 0,
                            "Error copying");

    return err;
}

/**
 * Move a resource 
 * @param src The source URI to be moved.
 * @param dst The desination URI
 * @param response The response pointer to be populated
 * TODO: ACL Handling
*/
static dav_error *dav_repos_move_resource(dav_resource * src,
					  dav_resource * dst,
					  dav_response ** response)
{
    TRACE();
    
    dav_error *err = NULL;
    dav_resource *parent;
    dav_repos_resource *db_r_src = (dav_repos_resource *) src->info->db_r;
    dav_repos_resource *db_r_dst = (dav_repos_resource *) dst->info->db_r;
    
    TRACE();

    /* If it's a version resource */
    if (src->type == DAV_RESOURCE_TYPE_VERSION ||
	src->type == DAV_RESOURCE_TYPE_HISTORY ||
	dst->type == DAV_RESOURCE_TYPE_VERSION ||
	dst->type == DAV_RESOURCE_TYPE_HISTORY) {
	return dav_new_error(db_r_src->p, HTTP_METHOD_NOT_ALLOWED , 0,
			     "Can not copy or move version resource.");
    }

    if((err = dav_repos_check_dst_parent(dst, &parent)))
        return err;

    /* check src and des url */
    if (strcmp(db_r_src->uri, db_r_dst->uri) == 0) {
	return dav_new_error(db_r_src->p, HTTP_BAD_REQUEST, 0,
			     "Source and destination are same while moving DBMS.");
    }

    /* move resources */
    err = dav_repos_rebind_resource(parent, basename(dst->uri), src, dst);
    
    return err;
}

// FIX IT - No need for response ?
/**
 * Remove resource
 * @param resource The resource to be removed
 * @param response The response pointer to be populated.  
*/
static dav_error *dav_repos_remove_resource(dav_resource * resource,
					    dav_response ** response)
{
    dav_repos_db *db = resource->info->db;
    dav_repos_resource *db_r = (dav_repos_resource *) resource->info->db_r;
    dav_error *err = NULL;

    TRACE();

    /* Version resource */
    if (resource->type == DAV_RESOURCE_TYPE_VERSION) {
	return dav_new_error(db_r->p, HTTP_METHOD_NOT_ALLOWED , 0,
			     "<DAV:no-version-delete>");
    }

    err = sabridge_unbind_resource(db, db_r);
    if(!err)
        resource->exists = 0;

    return err;
}

/**
 * Walk the (resource)tree using given params and depth 
 * and populate the response.
 * @param params Parameters to determine the walk 
 * @param depth The level (depth) to which we need to walk.
 * @param response The response pointer.
 */
static dav_error *dav_repos_walk(const dav_walk_params * params, int depth,
				 dav_response ** response)
{
    dav_error *err = NULL;
    apr_pool_t *pool = params->pool;
    dav_repos_db *db = params->root->info->db;
    dav_repos_resource *tmp_r;
    dav_repos_resource *db_r =
	(dav_repos_resource *) params->root->info->db_r;
    dav_walker_ctx *ctx = params->walk_ctx;


    TRACE();

    DBG2("Let's walk : %s in depth:%d", db_r->uri, depth);

    /* Let's start with NULL response */
    if (response) *response = NULL;

    /* 
     ** search using mysql 
     ** if not collection or depth=0, we have enough information
     */
    if ((db_r->resourcetype == dav_repos_COLLECTION
	 || db_r->resourcetype == dav_repos_VERSIONED_COLLECTION)
	&& depth != 0) {
	/* Will be filled children */
        char *priv = NULL;
        if (params->walk_type & DAV_WALKTYPE_AUTH)
            priv = "read";
	err = sabridge_get_collection_children
          (db, db_r, depth, priv, NULL, NULL, NULL);
        if (err) return err;
    }

    if (ctx->propfind_type) {
        db_r->ns_id_hash = apr_hash_make(pool);
    }

    /* 
     ** Lets walk through the results, 
     ** assemble walk resource, and call walker
     */
    for (tmp_r = db_r; tmp_r; tmp_r = tmp_r->next) {
	/* assemble walk resource */
	dav_walk_resource *wres = apr_pcalloc(pool, sizeof(*wres));
	dav_resource *resource = tmp_r->resource;

        if ((params->walk_type & DAV_WALKTYPE_IGNORE_BINDS) && tmp_r->bind)
            continue;

        dav_repos_update_dbr_resource(tmp_r);

	/* Make walk resource */
	wres->pool = pool;
	wres->resource = resource;
	wres->response = response ? *response : NULL;
	wres->walk_ctx = params->walk_ctx;

	/* 
	 * Build dead/live props hash
	 * Build version props hash
	 * It should be run even it's nulllock
	 */
        tmp_r->ns_id_hash = db_r->ns_id_hash;
        if (ctx->propfind_type) {
            dav_repos_build_lpr_hash(tmp_r);
        }

	/* Fill lock discovery for propfind prop */
	if (ctx->propfind_type == DAV_PROPFIND_IS_PROPNAME ||
	    ctx->propfind_type == DAV_PROPFIND_IS_PROP)
	    dav_repos_insert_lock_prop(params, tmp_r);

	/* Call walker */
	if ((err = (*params->func) 
             (wres, (tmp_r->resourcetype == dav_repos_COLLECTION || 
                     tmp_r->resourcetype == dav_repos_VERSIONED_COLLECTION) ? 
              DAV_CALLTYPE_COLLECTION : DAV_CALLTYPE_MEMBER) ) != NULL) {
	    /* ### maybe add a higher-level description? */
            if (response && wres->response)
                *response = wres->response;
	    return err;
	}

        /* Save response for now */
        if (response) *response = wres->response;
    }

    return NULL;
}

/**
 * Get the etag of resource
 * @param resource The resource whose etag needs to be found
*/
const char *dav_repos_getetag(const dav_resource * resource)
{
    TRACE();

    if (!resource->exists || resource->info == NULL
	|| resource->info->db_r == NULL)
	return NULL;

    return sabridge_getetag_dbr(resource->info->db_r);
}

/* Limebits specific, make this configurable */
const char *dav_repos_response_href_transform(request_rec *r, const char *uri)
{
    const char *result_uri = uri, *path;
    int path_len;
    TRACE();

    const char *host = apr_table_get(r->headers_in, "Host");
    if (strstr(host, r->server->defn_name)) {
        return result_uri;
    }

    if (!(path = (char *)apr_table_get(r->notes, "ls_response_href_t"))) {
        dav_repos_db *db = dav_repos_get_db(r);
        path = dbms_get_domain_path(r->pool, db, host);
        apr_table_setn(r->notes, "ls_response_href_t", path);
    }

    if (path) {
        path_len = strlen(path);
        if (0 == strncmp(uri, path, path_len)) {
            result_uri += path_len;
        }

        /* handle the case where path = uri */
        if (0 == strlen(result_uri)) {
            result_uri = "/";
        }
    }

    return result_uri;
}

dav_error *dav_repos_set_collection_type(dav_resource *resource, int resType)
{
    dav_repos_resource *db_r = (dav_repos_resource *) resource->info->db_r;
    dav_error *err = dav_new_error(db_r->p, HTTP_FORBIDDEN, 0,
                                   "This collection type not supported");

    TRACE();

    return err;
}

const dav_hooks_repository dav_repos_hooks_repos = {
    1,				/* special GET handling *//* 1 for GET handling, 0 for generic */
    dav_repos_get_resource,
    dav_repos_get_parent_resource,
    dav_repos_is_same_resource,
    dav_repos_is_parent_resource,
    dav_repos_open_stream,
    dav_repos_close_stream,
    dav_repos_write_stream,
    dav_repos_seek_stream,
    dav_repos_set_headers,
    dav_repos_deliver,
    dav_repos_create_collection,
    dav_repos_copy_resource,
    dav_repos_move_resource,
    dav_repos_remove_resource,
    dav_repos_walk,
    dav_repos_getetag,
    dav_repos_response_href_transform,
    dav_repos_set_collection_type
};
