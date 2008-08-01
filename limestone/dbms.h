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

#ifndef __DBMS_H__
#define __DBMS_H__

#include <apu.h>
#include <apr_general.h>
#include <unistd.h>
#include "dav_repos.h"

/**
 * Checked-state of the resource 
 */
typedef enum {
    DAV_RESOURCE_NOT_VERSIONED,
    DAV_RESOURCE_CHECKED_IN,
    DAV_RESOURCE_CHECKED_OUT
} dav_resource_checked_state;

/**
 * Autoversion flags
 */
typedef enum {
    DAV_AV_CHECKOUT_CHECKIN = 1,
    DAV_AV_CHECKOUT_UNLOCKED_CHECKIN,
    DAV_AV_CHECKOUT,
    DAV_AV_LOCKED_CHECKOUT,
    DAV_AV_NONE
} dav_repos_autoversion_t;


/** 
 * @struct dav_repos_property
 * A structure to store properties
 */
struct dav_repos_property {
    long serialno;      /** Serialno of the Resource linked to this property */
    long ns_id;         /** Namespace Info */
    const char* namespace_name;
    const char *name;   /** Name of the property */
    const char *xmlinfo;/** 'XML Information Items' linked with value */
    const char *value;
    struct dav_repos_property *next;
};

typedef struct dav_repos_property dav_repos_property;

/**
 * @struct dav_repos_resource
 * 
 * DBMS data transfer interface
 */
struct dav_repos_resource {
    
    /** Principal resource type as defined in mod_dav.h */
    dav_resource_type type;
    long serialno;
    
    /** Path to the repository root */
    const char *root_path;
    
    /** URI to resource (including root_path) */
    char *uri;

    /** The bind for this resource corresponding to uri above */
    int bind_id;
    
    /** Creation/Modification Dates in DATETIME string format */
    const char *created_at;	
    const char *updated_at;
    
    /** DAV:displayname for the resource */
    const char *displayname;
    const char *getcontentlanguage;
    const char *getcontenttype;
    long getcontentlength;
    const char *getetag;
    
    /** The dav_repos_* resource_types */
    int resourcetype;		

    /** serialno of the principal that created this resource */
    long creator_id;

    /** serialno of the principal that owns this resource */
    int owner_id;

    const char *lockdiscovery;
    const char *supportedlock;	
    
    int depth;

    /** value of DAV:resource-id */
    char *uuid;
    
    /** Live property hash */
    apr_hash_t *lpr_hash;	

    /** for dead property data */
    dav_repos_property *pr;	

    /** use for output value */
    apr_hash_t *pr_hash;	
    
    /** pr_hash_index to get first and next */
    apr_hash_index_t *pr_hash_index;	

    apr_hash_t *ns_id_hash;

    /* Version handling */
    /** version property hash */
    apr_hash_t *vpr_hash;
    	
    const char *creator_displayname;
    const char *comment;

    /** Root version of a VHR */
    long root_version_id;
    
    /** Value of DAV:auto-version */
    dav_repos_autoversion_t autoversion_type;
    
    /** 1 if version has no successor */
    int lastversion;		

    /** resource id of the last version of the vcr */
    long checked_id;

    /** checked status of the vcr */
    dav_resource_checked_state checked_state;

    /** the number corresponding to the last version of the vcr */
    long vr_num;

    /** version number of VR */
    int version;
    
    /** id of the VCR corresponding to a VHR or a VR */
    long vcr_id;

    /** id of the VHR corresponding to a VCR or a VR */
    long vhr_id;

    int checkin_on_unlock;

    /** sha1 string */
    const char *sha1str;

    /** ACL corresponding to this resource */
    dav_acl *acl;
    long parent_id;

    /* next link in a chain of resources */
    struct dav_repos_resource *next;	

    /* previously retrieved element in the chain that has the 
       same resource id as this one */
    struct dav_repos_resource *bind;

    apr_pool_t *p;
    		
    /** dav_resource corresponding to this dav_repos_resource */
    dav_resource *resource;
};

typedef struct dav_repos_resource dav_repos_resource;

/**
 * @struct dav_resource_private
 * 
 * Context needed to identify a resource 
 */
struct dav_resource_private {
    /** Memory storage pool associated with request */  
    apr_pool_t *pool;
    		
    /** Full pathname to resource */
    const char *pathname;	
    
    /** DB related data */
    dav_repos_db *db;		
    dav_repos_resource *db_r;
    
    /** Filesystem info */
    apr_finfo_t finfo;		
    request_rec *rec;		/* Save rec */
};

/** 
 * @struct dav_stream
 * Define the dav_stream structure for our use 
 */
struct dav_stream {
    apr_pool_t *p;
    request_rec *rec;
    dav_repos_db *db;
    dav_repos_resource *db_r;
    apr_file_t *file;
    char *path;
    int inserted;
    const char *content_type;
};

/* DB functions */
/**
 * Show error message of SQL operations
 * @param pool The pool to allocate from 
 * @param db The DB struct
 * @param db_error_message_str An Application level error message
 */
void db_error_message(apr_pool_t * pool, const dav_repos_dbms * db,
                      char *db_error_message_str);
/**
 * Connects to and opens the database
 * @param d DB connection struct containing the user, password, and DB name
 * @param p The pool to allocate from
 * @param s The server record
 * @return 0 indicating success
 */
int dbms_opendb(dav_repos_db *d, apr_pool_t *p, request_rec *r,
                const char *db_driver, const char *db_params);

/**
 * Disconnect from the database server
 * @param s The server record
 * @param d DB connection struct containing the user, password, and DB name
 * @return 0 indicating success
 */
void dbms_closedb(dav_repos_db * d);

/**
 * Insert media props of a given resource
 * @param d The DB connection struct
 * @param r The resource handle
 * @return NULL on success, dav_error otherwise
 */
dav_error *dbms_insert_media(const dav_repos_db * d, dav_repos_resource * r);

/**
 * Searches for a resource in the database and returns its live properties
 * r->uri is the search key; alternatively r->serialno can be the search 
 * key if r->uri is NULL. 
 * @param d DB connection struct containing the user, password, and DB name
 * @param r Out variable to hold the result
 * @return NULL on success, dav_error otherwise
 */
dav_error *dbms_get_property(const dav_repos_db * d, dav_repos_resource * r);

/**
 * Inserts a resource into the resources table
 * @param d DB connection struct containing the user, password, and DB name
 * @param r The resource to insert
 * @return NULL if success, dav_error otherwise
 */
dav_error *dbms_insert_resource(const dav_repos_db * d, dav_repos_resource * r);
/**
 * Set all live properties of the resource 
 * (assuming resource r is already in the database)
 * @param d DB connection struct containing the user, password, and DB name
 * @param r Identifies the resource to set the property on r->uri. 
 *          Contains the live property and value to set.
 * @return NULL on success, dav_error otherwise
 */
dav_error *dbms_set_property(const dav_repos_db * d,
	                     const dav_repos_resource * r);

/**
 * Update the props in media table of the resource
 * @param db The DB connection struct
 * @param db_r The resource for which media props are to be set
 * @return NULL on success, dav_error otherwise 
 */
dav_error *dbms_update_media_props(const dav_repos_db *db,
                                   dav_repos_resource *db_r);
/** 
 * Change the resource type of a resource
 * 
 * @param db handle to the database
 * @param db_r the resource whose type is to be changed
 * @param new_resource_type the new resource type
 * 
 * @return NULL on success, error otherwise
 */
dav_error *dbms_update_resource_type(const dav_repos_db *db,
                                     dav_repos_resource *db_r,
                                     int new_resource_type);
/**
 * Deletes the resource denoted by serialno, 
 * in turn media, content, acl get deleted. 
 * If file on disk then it is also deleted.
 * @param d DB connection struct containing the user, password, and DB name.
 * @param pool The pool to allocate from
 * @param db_r the resource to be deleted
 * @return NULL on success, error otherwise
 */
dav_error *dbms_delete_resource(const dav_repos_db * d, apr_pool_t * pool,
                                dav_repos_resource *db_r);
/**
 * Adds a new dead property record the the dead property table in the DB
 * @param d DB connection struct
 * @param r Identifies the resource to associate the property with
 * @param pr The property to insert
 * @return NULL on success, error otherwise
 */
dav_error *db_insert_property(const dav_repos_db * d,
	                      const dav_repos_resource * r,
		              const dav_repos_property * pr);
/**
 * Set the value of a dead property on a resource identified by serialno
 * @param d DB connection struct
 * @param r Identified the resource to set the property on
 * @param pr The dead property to set
 * @return NULL on success, error otherwise
 */
dav_error *dbms_set_dead_property(const dav_repos_db * d,
	                          const dav_repos_resource * r,
			          const dav_repos_property * pr);
/**
 * Get properties in a form of property link for every resource
 * @param d DB connection struct
 * @param db_r
 * @return NULL on success, error otherwise
 */
dav_error *dbms_fill_dead_property(const dav_repos_db * d,
	                           dav_repos_resource * db_r);
/**
 * Retrieve all children of a set of collections. The collections are
 * passed as linked list joined by dav_repos_resource's next member.
 * Currently the implementation is tied to sabridge_get_collection_children in
 * that if the bind member of a collection in the chain is set, then that
 * collection is ignored.
 * Modifying this function, depth=0 returns 0 and does nothing, 
 * depth=Infinity then returns list of descendents accesible from current collection
 * ACL checks added
 * @param d DB connection struct
 * @param db_r A pointer to a resource struct which identifies the collection
 * @param db_r_last Pointer to the last resource in the chain whose children
                    should be retrieved
 * @param acl_priv The ACL privilege to filter against
 * @param plink_head The head pointer of the linked-list of resultant resources
 * @param plink_tail The tail pointer of the linked-list of resultant resources
 * @param num_items The number of resources in the response
 * @return NULL for success, dav_error otherwise
 */
dav_error *dbms_get_collection_resource(const dav_repos_db * d,
                                        dav_repos_resource * db_r,
                                        dav_repos_resource * db_r_last,
                                        const char *acl_priv,
                                        dav_repos_resource **plink_head,
                                        dav_repos_resource **plink_tail,
                                        int *num_items);

/**
 * Copy media props of one resource to another
 * @param d The DB conneciton struct
 * @param r_src The source resource
 * @param r_dest THe resource to copy to
 * @return NULL for success, dav_error otherwise
 */
dav_error *dbms_copy_media_props(const dav_repos_db *d,
                                 const dav_repos_resource *r_src,
                                 dav_repos_resource *r_dest);

/**
 * Delete dead props of a resource
 * @param pool The pool to allocate from
 * @param d The DB connection struct
 * @param res_id The resource id of the resource
 * @return NULL for success, dav_error otherwise
 */
dav_error *dbms_delete_dead_props(apr_pool_t *pool, const dav_repos_db *d,
                                  long res_id);
/**
 * Copy dead props of one resource to another
 * @param pool The pool to allocate from
 * @param d The DB conneciton struct
 * @param src_id The source resource id
 * @param dest_id Resource Id of the resource to copy to
 * @return NULL for success, dav_error otherwise
 */
dav_error *dbms_copy_dead_props(apr_pool_t *pool, const dav_repos_db *d,
                                long src_id, long dest_id);
/** 
 * Deletes a dead property by serialno and property name
 * @param d DB connection struct
 * @param r A pointer to a resource struct identifying 
 *          the resource to be operated upon
 * @param pr A pointer to a property struct identifying the property to delete
 * @return NULL for success, dav_error otherwise
 */
dav_error *dbms_del_dead_property(const dav_repos_db * d,
	                          const dav_repos_resource * r,
			          const dav_repos_property * pr);

/**
 * Get namespace name given the namespace id
 * @param d DB connection struct
 * @param db_r the resource
 * @param ns_id the namespace id
 * @return NULL for error, namespace otherwise
 */
char *dbms_get_ns_name(const dav_repos_db *d, 
                       dav_repos_resource *db_r, 
                       long ns_id);

dav_error *dbms_get_namespace_id(apr_pool_t *pool, const dav_repos_db * d, 
                                 const char *namespace, long *ns_id);

/**
 * Get namespace
 * If new namespace, save it and return ns_id
 * 
 * @param d DB connection struct
 * @param db_r Pointer to a resource struct identifying the resource in question
 * @param namespace namespace
 * @return NULL for success, dav_error otherwise
 */
dav_error *dbms_get_ns_id(const dav_repos_db * d, dav_repos_resource * db_r,
                          const char *namespace, long *ns_id);

/** 
 * Returns a principal_id on a given principal-url 
 * @param d DB connection struct containing the user, password, and DB name 
 * @param url Identifies the principal on the basis of his url.  
 * @param pool The pool to allocate from
 * @return The principalId of the pincipal(if exists) 0 (on error) 
 */
dav_error *dbms_get_principal_id(apr_pool_t *pool, const dav_repos_db * d, 
                                 const char *url, long *prin_id);

/**
 * Get number of resources having a body with a given SHA1
 * @param pool The pool to allocate from
 * @param d The DB connection struct
 * @param sha1str The SHA1 string
 * @return The number of resources, -1 for failure
 */
long dbms_num_sha1_resources(apr_pool_t *pool, const dav_repos_db *d, 
                             const char *sha1str);

dav_error *dbms_add_constraint(apr_pool_t *pool, const dav_repos_db *db,
                               const char *table, const char *constraint);

dav_error *dbms_drop_constraint(apr_pool_t *pool, const dav_repos_db *db,
                                const char *table, const char *constraint);

dav_error *dbms_defer_all_constraints(apr_pool_t *pool, const dav_repos_db *d);

void dav_repos_update_dbr_resource(dav_repos_resource *db_r);

#endif
