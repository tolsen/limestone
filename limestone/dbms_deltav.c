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

#include "dbms_deltav.h"
#include "dbms_api.h"
#include "apr_strings.h"
#include "deltav_util.h"        /* for mk_version_uri */
#include "bridge.h"             /* for sabridge_new_dbr_from_dbr */

/** 
 * Retrieve all properties of the resource defined by the DeltaV spec
 * @param d handle to the database
 * @param r resource whose properties should be retrieved. 
            The properties are retrieved using the @serialno
 * @return NULL on success, dav_error otherwise
 */
dav_error *dbms_get_deltav_props(const dav_repos_db *d, dav_repos_resource *r)
{
    dav_error *err = NULL;
    TRACE();

    switch(r->resourcetype) {
    case dav_repos_VERSIONED:
    case dav_repos_VERSIONED_COLLECTION:
        err = dbms_get_vcr_props(d, r);
        break;
    case dav_repos_VERSION:
    case dav_repos_COLLECTION_VERSION:
        err = dbms_get_version_resource_props(d, r);
        break;
    case dav_repos_VERSIONHISTORY:
        err = dbms_get_vhr_props(d, r);
        break;
    }

    return err;
}

/** 
 * Retrieve all DeltaV properties of a version controlled resource
 * @param d handle to the database
 * @param r resource whose properties are retrieved
 * @return NULL on success, dav_error otherwise
 */
dav_error *dbms_get_vcr_props(const dav_repos_db *d, dav_repos_resource *r)
{
    apr_pool_t *pool = r->p;
    dav_repos_query *q = NULL;
    int ierrno = 0;
    dav_error *err = NULL;
    char *checked_state;

    TRACE();

    q = dbms_prepare(pool, d->db,
                     "SELECT checked_state, checked_id, vhr_id, version_type, "
                     "       checkin_on_unlock "
                     "FROM vcrs WHERE resource_id=? ");
    dbms_set_int(q, 1, r->serialno);

    if (dbms_execute(q)) {
	dbms_query_destroy(q);
	return dav_new_error(r->p, HTTP_INTERNAL_SERVER_ERROR, 0, 
                             "dbms_execute error");
    }

    if ((ierrno = dbms_next(q)) < 0) {
	dbms_query_destroy(q);
	return dav_new_error(r->p, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "dbms_next error");
    }
    if (ierrno == 0) {
	dbms_query_destroy(q);
        r->checked_state = DAV_RESOURCE_NOT_VERSIONED;
	return err;
    }

    checked_state = dbms_get_string(q, 1);
    r->checked_state = checked_state[0]=='I'? 
      DAV_RESOURCE_CHECKED_IN : DAV_RESOURCE_CHECKED_OUT;
    r->checked_id = dbms_get_int(q, 2);
    r->vhr_id = dbms_get_int(q, 3);
    r->autoversion_type = dbms_get_int(q, 4);

    dbms_query_destroy(q);

    err = dbms_get_version_number(d, r);

    return err;
}

/** 
 * Get the version number of a version resource using its resource id
 * @param d handle to the database
 * @param r a resource whose checked_id is set to the resource_id of the version
            the version number is assigned to the vr_num field
 * @return NULL on success, dav_error otherwise
 */
dav_error *dbms_get_version_number(const dav_repos_db *d, dav_repos_resource *r)
{
    apr_pool_t *pool = r->p;
    dav_repos_query *q = NULL;
    int ierrno = 0;
    dav_error *err = NULL;

    TRACE();

    q = dbms_prepare(pool, d->db, 
                     "SELECT number FROM versions "
                     "WHERE resource_id=?");
    dbms_set_int(q, 1, r->checked_id);
    if (dbms_execute(q)) {
        dbms_query_destroy(q);
        return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                "DBMS Error");
    }

    if ((ierrno = dbms_next(q)) <= 0) {
        dbms_query_destroy(q);
        return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                "Could not lookup version number");
    }

    r->vr_num = dbms_get_int(q, 1);

    dbms_query_destroy(q);
     
    return err;
}

/**
 * Retrieves the resource id of a version of a VCR
 * 
 * @param db handle to the database
 * @param db_r VCR with @serialno set
 * @param version_num number of specific version of the VCR
 * @param version_id will be set to resource id of the version
 * 
 * @return NULL on success, error otherwise.
 */
dav_error *dbms_get_version_id(const dav_repos_db *db, dav_repos_resource *db_r,
                               int version_num, int *version_id)
{
    apr_pool_t *pool = db_r->p;
    dav_repos_query *q = NULL;
    int ierrno = 0;

    TRACE();

    q = dbms_prepare(pool, db->db, 
                     "SELECT resource_id FROM versions "
		     "WHERE number=? AND vcr_id=?");
    dbms_set_int(q, 1, version_num);
    dbms_set_int(q, 2, db_r->serialno);

    if (dbms_execute(q)) {
	dbms_query_destroy(q);
	db_error_message(pool, db->db, "dbms_execute error");
	return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
			     "DBMS Error");
    }

    if ((ierrno = dbms_next(q)) <= 0) {
	dbms_query_destroy(q);
	db_error_message(pool, db->db, "dbms_next error");
	return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
			     "DBMS Error");
    }

    *version_id = dbms_get_int(q, 1);
    dbms_query_destroy(q);

    return NULL;
}

/** 
 * Retrieves DeltaV properties of a version resource
 * @param d handle to the database
 * @param vr version resource
 * @return NULL on success, dav_error otherwise
 */
dav_error *dbms_get_version_resource_props(const dav_repos_db *d,
                                           dav_repos_resource *vr)
{
    apr_pool_t *pool = vr->p;
    dav_repos_query *q = NULL;
    int ierrno = 0;
    dav_error *err = NULL;
    dav_repos_resource *vcr = NULL;
    sabridge_new_dbr_from_dbr(vr, &vcr);

    TRACE();

    q = dbms_prepare(pool, d->db,
                     "SELECT number, vcr_id FROM versions "
                     "WHERE resource_id=?");
    dbms_set_int(q, 1, vr->serialno);

    if (dbms_execute(q)) {
	dbms_query_destroy(q);
	return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0, 
                             "dbms_execute error");
    }

    if ((ierrno = dbms_next(q)) < 0) {
	dbms_query_destroy(q);
	return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "dbms_next error");
    }
    if (ierrno == 0) {
	dbms_query_destroy(q);
	return err;
    }

    vr->version = dbms_get_int(q, 1);
    vr->vcr_id = dbms_get_int(q, 2);

    dbms_query_destroy(q);

    /* check if this is the last version of its VCR */
    vcr->serialno = vr->vcr_id;
    err = dbms_get_vcr_props(d, vcr);
    if(vcr->checked_id == vr->serialno)
        vr->lastversion = 1;

    return err;
}

/** 
 * Retrieve the root_version_id of a VHR
 * @param d handle to the database
 * @param vhr the version history resource
 * @return NULL on success, dav_error otherwise
 */
dav_error *dbms_get_vhr_root_version_id(const dav_repos_db *d,
                                        dav_repos_resource *vhr)
{
    apr_pool_t *pool = vhr->p;
    dav_repos_query *q = NULL;
    int ierrno;
    dav_error *err = NULL;

    TRACE();

    q = dbms_prepare(pool, d->db,
                     "SELECT root_version_id FROM vhrs "
                     "WHERE resource_id=? ");
    dbms_set_int(q, 1, vhr->serialno);

    if (dbms_execute(q)) {
	dbms_query_destroy(q);
	return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0, 
                             "dbms_execute error");
    }

    if ((ierrno = dbms_next(q)) < 0) {
	dbms_query_destroy(q);
	return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "dbms_next error");
    }
    if (ierrno == 0) {
	dbms_query_destroy(q);
	return err;
    }

    vhr->root_version_id = dbms_get_int(q, 1);
    
    dbms_query_destroy(q);

    return err;
}

/** 
 * Retrieve DeltaV properties of a Version History Resource
 * @param d handle to the database
 * @param vhr version history resource
 * @return NULL on success, dav_error otherwise
 */
dav_error *dbms_get_vhr_props(const dav_repos_db *d, dav_repos_resource *vhr)
{
    apr_pool_t *pool = vhr->p;
    dav_repos_query *q = NULL;
    int ierrno = 0;
    dav_error *err = NULL;
    char *checked_state;

    TRACE();

    q = dbms_prepare(pool, d->db,
                     "SELECT resource_id, checked_state, checked_id "
                     "FROM vcrs WHERE vhr_id=? ");
    dbms_set_int(q, 1, vhr->serialno);

    if (dbms_execute(q)) {
	dbms_query_destroy(q);
	return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0, 
                             "dbms_execute error");
    }

    if ((ierrno = dbms_next(q)) < 0) {
	dbms_query_destroy(q);
	return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "dbms_next error");
    }
    if (ierrno == 0) {
	dbms_query_destroy(q);
	return err;
    }

    vhr->vcr_id = dbms_get_int(q, 1);

    checked_state = dbms_get_string(q, 2);
    vhr->checked_state = checked_state[0]=='I'? 
      DAV_RESOURCE_CHECKED_IN : DAV_RESOURCE_CHECKED_OUT;

    vhr->checked_id = dbms_get_int(q, 3);

    dbms_query_destroy(q);

    err = dbms_get_vhr_root_version_id(d, vhr);
    
    return err;
}

/**
 * Set the checkin and checkout version for a resource
 * @param d DB connection struct containing the user, password, and DB name
 * @param db_r Identifies the resource to set the property on r->uri. 
 *          Contains the live property and value to set.
 * @checkin: Checkin version
 * @checkout: Checkout version
 * @return NULL, for success; Error, otherwise.
 */
dav_error *dbms_set_checkin_out(const dav_repos_db * db,
                                dav_repos_resource * db_r,
                                dav_resource_checked_state checked_state,
                                int version_num)
{
    apr_pool_t *pool = db_r->p;
    dav_repos_query *q = NULL;
    int checked_id;
    dav_error *err = NULL;

    TRACE();

    if(db_r->vr_num == version_num && db_r->checked_id != 0)
        checked_id = db_r->checked_id;
    else {
        err = dbms_get_version_id(db, db_r, version_num, &checked_id);
        if(err)
            return err;
    }

    q = dbms_prepare(pool, db->db, 
                     "UPDATE vcrs "
                     "SET checked_state=?, checked_id=? "
                     "WHERE resource_id=?");
    dbms_set_string(q, 1, checked_state==DAV_RESOURCE_CHECKED_IN ? "I" : "O");
    dbms_set_int(q, 2, checked_id);
    dbms_set_int(q, 3, db_r->serialno);

    if (dbms_execute(q)) {
	db_error_message(pool, db->db, "mysql_query error");
	dbms_query_destroy(q);
	return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
			     "DBMS Error");
    }
    dbms_query_destroy(q);

    db_r->checked_id = checked_id;
    db_r->checked_state = checked_state;
    db_r->vr_num = version_num;

    return NULL;
}

/**
 * Set the checkin and checkout version for a resource
 * @param d DB connection struct containing the user, password, and DB name
 * @param db_r Identifies the resource to set the property on r->uri. 
 *          Contains the live property and value to set.
 * @author: The author who changed the resource
 * @return NULL, for success; Error, otherwise.
 */
dav_error *dbms_insert_version(const dav_repos_db * db,
                               dav_repos_resource *new_version)
{
    apr_pool_t *pool = new_version->p;
    dav_repos_query *q = NULL;

    TRACE();

    /* Make an entry into the versions table */
    q = dbms_prepare(pool, db->db,
		     "INSERT INTO versions(resource_id, number,vcr_id, vhr_id) "
		     "VALUES(?, ?, ?, ?)");
    dbms_set_int(q, 1, new_version->serialno);
    dbms_set_int(q, 2, new_version->version);
    dbms_set_int(q, 3, new_version->vcr_id);
    dbms_set_int(q, 4, new_version->vhr_id);

    if (dbms_execute(q)) {
	dbms_query_destroy(q);
	db_error_message(pool, db->db, "dbms_execute error");
	return dav_new_error(pool,
			     HTTP_INTERNAL_SERVER_ERROR, 0, "DBMS Error");
    }
    dbms_query_destroy(q);

    return NULL;
}

dav_error *dbms_insert_vhr(const dav_repos_db *db,
                           dav_repos_resource *vhr)
{
    apr_pool_t *pool = vhr->p;
    dav_repos_query *q = NULL;

    /* make entry in vhrs */
    q = dbms_prepare(pool, db->db,
                     "INSERT INTO vhrs(resource_id, root_version_id) "
                     "VALUES(?, ?)");
    dbms_set_int(q, 1, vhr->serialno);
    dbms_set_int(q, 2, vhr->root_version_id);
    if (dbms_execute(q)) {
        dbms_query_destroy(q);
        db_error_message(pool, db->db, "dbms_execute error");
        return dav_new_error(pool,
                             HTTP_INTERNAL_SERVER_ERROR, 0,
                             "DBMS Error");
    }
    dbms_query_destroy(q);

    return NULL;
}

/** 
 * Make an entry into the vcrs table for an existing resource.
 * Also changes the type of the resource
 * 
 * @param db handle to the database
 * @param db_r resource which is being converted to a VCR
 * 
 * @return 
 */
dav_error *dbms_insert_vcr(const dav_repos_db *db,
                           dav_repos_resource *db_r)
{
    apr_pool_t *pool = db_r->p;
    dav_repos_query *q = NULL;

    TRACE();

    /* Create the vcrs entry */
    q = dbms_prepare(pool, db->db,
                     "INSERT INTO vcrs(resource_id, checked_id, vhr_id, "
                     "    checked_state, version_type, checkin_on_unlock)"
                     " VALUES ( ?, ?, ?, 'I', ?, 0)");
    dbms_set_int(q, 1, db_r->serialno);
    dbms_set_int(q, 2, db_r->checked_id);
    dbms_set_int(q, 3, db_r->vhr_id);

    db_r->checked_state = DAV_RESOURCE_CHECKED_IN;
    db_r->autoversion_type = DAV_AV_NONE;
    dbms_set_int(q, 4, db_r->autoversion_type);
    db_r->checkin_on_unlock = 1;

    if (dbms_execute(q)) {
        dbms_query_destroy(q);
        db_error_message(pool, db->db, "dbms_execute error");
        return dav_new_error(pool,
                             HTTP_INTERNAL_SERVER_ERROR, 0,
                             "DBMS Error");
    }
    dbms_query_destroy(q);

    return NULL;
}

/**
 * Set the DAV:auto-version property
 * @param
 * @param d DB connection struct containing the user, password, and DB name
 * @param db_r Identifies the resource to set the set the property on. 
 *             Should have db_r->serialno and db_r->autoversion_type set
 * @return NULL, for success; Error, otherwise.
 *
 */
dav_error *dbms_set_autoversion_type(const dav_repos_db * db,
				     dav_repos_resource * db_r,
                                     dav_repos_autoversion_t av_type)
{
    apr_pool_t *pool = db_r->p;
    dav_repos_query *q = NULL;

    TRACE();

    q = dbms_prepare(pool, db->db, 
                     "UPDATE vcrs "
                     "SET version_type=? "
		     "WHERE resource_id=?");
    dbms_set_int(q, 1, av_type);
    dbms_set_int(q, 2, db_r->serialno);

    if (dbms_execute(q)) {
	db_error_message(pool, db->db,
			 "Could not set auto-version property");
	dbms_query_destroy(q);
	return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
			     "DBMS Error");
    }
    dbms_query_destroy(q);

    db_r->autoversion_type = av_type;

    return NULL;
}

dav_error *dbms_set_checkin_on_unlock(const dav_repos_db *db,
                                      dav_repos_resource *db_r)
{
    apr_pool_t *pool = db_r->p;
    dav_repos_query *q = NULL;
    dav_error *err = NULL;

    TRACE();

    q = dbms_prepare(pool, db->db,
                     "UPDATE vcrs SET checkin_on_unlock=1 WHERE resource_id=?");
    dbms_set_int(q, 1, db_r->serialno);

    if (dbms_execute(q)) {
        err = dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                            "Couldn't unversion the vhr's vcrs");
    }
    dbms_query_destroy(q);

    if (err == NULL)
        db_r->checkin_on_unlock = 1;

    return err;
}

/* Copies VCBs
 * @param r_src the source collection
 * @param r_dest the collection version
 */
dav_error *dbms_copy_resource_collection_version(const dav_repos_db * db,
					  dav_repos_resource * r_src,
					  dav_repos_resource * r_dst,
					  request_rec * rec)
{
    apr_pool_t *pool = r_src->p;
    dav_repos_query *q = NULL;
    dav_error *err = NULL;

    TRACE();

    if ((r_src->resourcetype == dav_repos_COLLECTION
	 || r_src->resourcetype == dav_repos_VERSIONED_COLLECTION)
	&& r_dst->resourcetype == dav_repos_COLLECTION_VERSION) {
	/* copying from a collection to collection version */
	q = dbms_prepare(pool, db->db,
			 "INSERT INTO binds(name, collection_id, resource_id, updated_at) "
			 " (SELECT name, ?, vhr_id, updated_at "
			 "  FROM binds INNER JOIN vcrs ON binds.resource_id=vcrs.resource_id "
			 "  WHERE collection_id=?) ");

	dbms_set_int(q, 1, r_dst->serialno);
	dbms_set_int(q, 2, r_src->serialno);

	if (dbms_execute(q)) {
	    dbms_query_destroy(q);
	    err = dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                                "DBMS Error while intersting into 'binds'");
	}
	dbms_query_destroy(q);
    }
    return err;
}

dav_error *dbms_restore_vcc(const dav_repos_db *db,
                            dav_repos_resource *vcc,
                            dav_repos_resource *cvr)
{
    apr_pool_t *pool = vcc->p;
    dav_repos_query *q = NULL;

    /* delete all version controlled binds of the vcc */
    q = dbms_prepare(pool, db->db,
                     "DELETE FROM binds "
                     "WHERE collection_id=? AND resource_id IN "
                     "(SELECT resource_id FROM binds INNER JOIN vcrs USING resource_id WHERE collection_id=?)");
    dbms_set_int(q, 1, vcc->serialno);
    dbms_set_int(q, 2, vcc->serialno);
    
    if (dbms_execute(q)) {
        dbms_query_destroy(q);
        return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "DBMS Error");
    }
    dbms_query_destroy(q);
    
    /* delete any conflicting non-version controlled binds to the VCC that
       may have been introduced after checking out */
    q = dbms_prepare(pool, db->db,
                     "DELETE FROM binds "
                     "WHERE collection_id=? AND name IN (SELECT name FROM binds WHERE collection_id=?");
    dbms_set_int(q, 1, vcc->serialno);
    dbms_set_int(q, 2, cvr->serialno);
    
    if (dbms_execute(q)) {
        dbms_query_destroy(q);
        return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "DBMS Error");
    }
    dbms_query_destroy(q);
    
    /* copy and restore all the binds from the collection version */
    q = dbms_prepare(pool, db->db,
                     "INSERT INTO binds "
                     " (SELECT (NULL, name, ?, vcrs.resource_id, updated_at) "
                     "  FROM binds JOIN vcrs ON binds.resource_id=vcrs.vhr_id "
                     "  WHERE collection_id=?) ");
    dbms_set_int(q, 1, vcc->serialno);
    dbms_set_int(q, 2, cvr->serialno);

    if (dbms_execute(q)) {
        dbms_query_destroy(q);
        return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "DBMS Error");
    }
    dbms_query_destroy(q);

    return NULL;
}
 
/** 
 * Get all the versions of a VHR. Currently this function is only used for
 * deleting the versions, so only a few properties are retrieved
 * 
 * @param d handle to the database
 * @param vhr version history resource
 * @param pversions the versions will be returned from here
 * 
 * @return 
 */
dav_error *dbms_get_vhr_versions(const dav_repos_db *d,
                                 dav_repos_resource *vhr,
                                 dav_repos_resource **pversions)
{
    apr_pool_t *pool = vhr->p;
    dav_repos_query *q = NULL;
    dav_repos_resource *new_link_item, *link_tail, *dummy_link_head;
    int ierrno;

    TRACE();

    q = dbms_prepare
      (pool, d->db,
       "SELECT id, type, uuid, created_at, updated_at, contentlanguage, "
       "       media.size, media.mimetype, media.sha1, "
       "       versions.number, versions.vcr_id, principals.name "
       "FROM versions "
       "       INNER JOIN resources "
       "               ON versions.resource_id = resources.id "
       "       LEFT JOIN media "
       "              ON versions.resource_id = media.resource_id "
       "       LEFT JOIN principals "
       "              ON resources.creator_id = principals.resource_id "
       "WHERE versions.vhr_id=? ORDER BY versions.number ASC");
    dbms_set_int(q, 1, vhr->serialno);

    if (dbms_execute(q)) {
        dbms_query_destroy(q);
        return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "DBMS Error couldn't get versions of vhr");
    }

    dummy_link_head = apr_pcalloc(pool, sizeof(*dummy_link_head));
    link_tail = dummy_link_head;

    while (1 == (ierrno = dbms_next(q))) {
        sabridge_new_dbr_from_dbr(vhr, &new_link_item);
        link_tail->next = new_link_item;
        link_tail = new_link_item;

        new_link_item->p = pool;
        new_link_item->serialno = dbms_get_int(q, 1);
        new_link_item->resourcetype = 
            dav_repos_get_type_id(dbms_get_string(q, 2));
        new_link_item->uuid = dbms_get_string(q, 3);
        new_link_item->created_at = dbms_get_string(q, 4);
        new_link_item->updated_at = dbms_get_string(q, 5);
        new_link_item->getcontentlanguage = dbms_get_string(q, 6);
        
        if (new_link_item->resourcetype == dav_repos_VERSION) {
            new_link_item->getcontentlength = dbms_get_int(q, 7);
            new_link_item->getcontenttype = dbms_get_string(q, 8);
            new_link_item->sha1str = dbms_get_string(q, 9);
        }
        new_link_item->version = dbms_get_int(q, 10);
        new_link_item->uri = 
          sabridge_mk_version_uri(vhr, new_link_item->version);

        new_link_item->vcr_id = dbms_get_int(q, 11);
        new_link_item->creator_displayname = dbms_get_string(q, 12);
    }
    dbms_query_destroy(q);


    if (ierrno != 0) {
        *pversions = NULL;
        return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "DBMS Error retrieving vhr versions records");
    }

    if (new_link_item)
        new_link_item->lastversion = 1;

    *pversions = dummy_link_head->next;
    return NULL;
}

/** 
 * Changes the types of the VCRs of a VHR from VERSIONED_MEDIUM to MEDIUM
 * or VERSIONED_COLLECTION to COLLECTION
 * 
 * @param d handle to the database
 * @param vhr the version history resource
 * 
 * @return 
 */
dav_error *dbms_unversion_vhr_vcrs(const dav_repos_db *d,
                                   dav_repos_resource *vhr)
{
    apr_pool_t *pool = vhr->p;
    dav_repos_query *q;
    dav_error *err = NULL;

    TRACE();

    q = dbms_prepare(pool, d->db, "UPDATE resources SET type = ? "
                     "WHERE id IN"
                     " (SELECT resource_id FROM vcrs WHERE vhr_id=?) "
                     "AND type = ?");
                        
    dbms_set_string(q, 1, dav_repos_resource_types[dav_repos_RESOURCE]);
    dbms_set_int(q, 2, vhr->serialno);
    dbms_set_string(q, 3, dav_repos_resource_types[dav_repos_VERSIONED]);

    if (dbms_execute(q)) {
        err = dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                            "Couldn't unversion the vhr's vcrs");
    }
    dbms_query_destroy(q);

    if(err) return err;

    q = dbms_prepare(pool, d->db, "UPDATE resources SET type = ? "
                     "WHERE id IN"
                     " (SELECT resource_id FROM vcrs WHERE vhr_id=?) "
                     "AND type = ?");
                        
    dbms_set_string(q, 1, dav_repos_resource_types[dav_repos_COLLECTION]);
    dbms_set_int(q, 2, vhr->serialno);
    dbms_set_string(q, 3, dav_repos_resource_types[dav_repos_VERSIONED_COLLECTION]);

    if (dbms_execute(q)) {
        err = dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                            "Couldn't unversion the vhr's vcrs");
    }
    dbms_query_destroy(q);

    return err;
}
