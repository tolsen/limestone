#include "dbms_transaction.h"
#include <mod_dav.h>
#include <apr_strings.h>
#include "dav_repos.h"
#include "dbms_dbd.h"

int dbms_set_session_xaction_iso_level(apr_pool_t *pool,
                                       const dav_repos_db *d,
                                       xaction_iso_level level)
{
    dav_repos_query *q = NULL;
    const char *query = NULL,*iso_level = NULL;
    int ret;

    TRACE();

    if (d->dbms == PGSQL)
        query = "SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL ";
    else if (d->dbms == MYSQL)
        query = "SET SESSION TRANSACTION ISOLATION LEVEL ";

    switch (level) {
    case READ_UNCOMMITTED:
        iso_level = "READ UNCOMMITTED";
        break;
    case READ_COMMITTED:
        iso_level = "READ COMMITTED";
        break;
    case REPEATABLE_READ:
        iso_level = "REPEATABLE READ";
        break;
    case SERIALIZABLE:
        iso_level = "SERIALIZABLE";
        break;
    }

    query = apr_pstrcat(pool, query, iso_level, NULL);
    q = dbms_prepare(pool, d->db, query);
    ret = dbms_execute(q);
    dbms_query_destroy(q);

    return ret;
}

int dbms_transaction_start(apr_pool_t * pool, 
                           const dav_repos_db *d,
                           dav_repos_transaction **trans)
{
    int ierrno;
    const dav_repos_dbms *db= d->db;
    apr_dbd_transaction_t *ap_trans = NULL;
    const apr_dbd_driver_t *driver = db->ap_dbd_dbms->driver;
    apr_dbd_t *handle = db->ap_dbd_dbms->handle;

    TRACE();

    ierrno = apr_dbd_transaction_start(driver, pool, handle, &ap_trans);

    if(!ierrno) {
        /* Fill dav_repos_transaction */
        *trans = apr_pcalloc(pool, sizeof(**trans));
        (*trans)->pool = pool;
        (*trans)->db = db;
        (*trans)->ap_trans = ap_trans;
    }


    if (d->dbms == PGSQL)
        dbms_defer_all_constraints(pool, d);

    return ierrno;
}

int dbms_transaction_end(dav_repos_transaction *trans)
{
    const dav_repos_dbms *db = trans->db;
    apr_pool_t *pool = trans->pool;

    TRACE();

    return apr_dbd_transaction_end(db->ap_dbd_dbms->driver, 
                                     pool, trans->ap_trans);
}

int dbms_transaction_mode_set(dav_repos_transaction *trans, int mode)
{
    int ap_mode;

    TRACE();

    switch(mode) {
    
    case DAV_TRANSACTION_COMMIT:
        ap_mode = APR_DBD_TRANSACTION_COMMIT;
        break;
    
    case DAV_TRANSACTION_ROLLBACK:
        ap_mode = APR_DBD_TRANSACTION_ROLLBACK;
        break;

    case DAV_TRANSACTION_IGNORE_ERRORS:
        ap_mode = APR_DBD_TRANSACTION_IGNORE_ERRORS;
        break;

    default:
        ap_mode = APR_DBD_TRANSACTION_COMMIT;
    }

    return apr_dbd_transaction_mode_set(trans->db->ap_dbd_dbms->driver,
                                        trans->ap_trans, ap_mode);
}

dav_error *dbms_quota_pre_commit_checks(apr_pool_t *pool, const dav_repos_dbms *db)
{
    dav_repos_query *q;
    int ierrno;
    dav_error *err = NULL;

    TRACE();

    /* quota checks */
    q = dbms_prepare(pool, db, "SELECT principal_id FROM quota"
                     " WHERE used_quota > total_quota AND total_quota > 0");

    if(dbms_execute(q)) {
        dbms_query_destroy(q);
        return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 
                             0, "dbms_execute error");
    }

    ierrno = dbms_next(q);
    if(ierrno < 0) {
        dbms_query_destroy(q);
        return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 
                             0, "dbms_execute error");
    }

    if(ierrno > 0) {
        dbms_query_destroy(q);
        return dav_new_error_tag(pool, HTTP_INSUFFICIENT_STORAGE,
                                 DAV_ERR_QUOTA_INSUFFICIENT,
                                 "Quota restrictions prevent this request "
                                 "from being completed.", NULL,
                                 "quota-not-exceeded", NULL, NULL);
    }

    dbms_query_destroy(q);
    return err;
}
