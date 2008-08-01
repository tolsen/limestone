#include "transaction.h"
#include "dbms_transaction.h"
#include "dbms_api.h"
#include "dav_repos.h"

dav_error *dav_repos_transaction_start(request_rec *r, dav_transaction **t)
{
    int ierrno;
    dav_repos_db *db;
    apr_pool_t *pool = r->pool;
    dav_repos_transaction *db_trans = NULL;

    TRACE();

    db = dav_repos_get_db(r);
    if (db == NULL)
        return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "Couldn't initialize connection with database");

    if(0 != (ierrno = dbms_transaction_start(pool, db, &db_trans)))
       return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, ierrno,
                            "failed to start transaction");

    *t = apr_pcalloc(pool, sizeof(**t));
    (*t)->info = apr_pcalloc(pool, sizeof(dav_transaction_private));
    (*t)->info->db_trans = db_trans;
    (*t)->mode = DAV_TRANSACTION_COMMIT;

    return NULL;
}

int dav_repos_transaction_mode_set(dav_transaction *t, int mode)
{
    TRACE();
    return dbms_transaction_mode_set(t->info->db_trans, mode);
}

/* pre-commit DB checks */
/* NOTE: replace these checks by deferred constraints 
 * once they are supported by databases */
static dav_error *dav_repos_transaction_pre_commit_checks(dav_transaction *t)
{
    dav_repos_transaction *db_trans = t->info->db_trans;
    apr_pool_t *pool = db_trans->pool;
    dav_error *err;

    TRACE();

    if((err = dbms_quota_pre_commit_checks(pool, db_trans->db))
        && t->mode != DAV_TRANSACTION_IGNORE_ERRORS) {
        t->mode = dbms_transaction_mode_set(db_trans, DAV_TRANSACTION_ROLLBACK);
        return err;
    }

    /* handle fserr , rollback if mode is not ignore_errors */
    if(db_trans && t->info->fserrno && t->mode != DAV_TRANSACTION_IGNORE_ERRORS) 
        t->mode = dbms_transaction_mode_set(db_trans, DAV_TRANSACTION_ROLLBACK);

    return NULL;
}

dav_error *dav_repos_transaction_end(dav_transaction *t)
{
    dav_repos_transaction *db_trans = t->info->db_trans;
    int ierrno;
    dav_error *err;

    TRACE();

    err = dav_repos_transaction_pre_commit_checks(t);

    ierrno = dbms_transaction_end(db_trans);
    if (ierrno)
        err = dav_new_error(db_trans->pool, HTTP_INTERNAL_SERVER_ERROR, ierrno,
                            "failed to end transaction");
    return err;
}

const dav_hooks_transaction dav_repos_hooks_transaction = {
    dav_repos_transaction_start,
    dav_repos_transaction_mode_set,
    dav_repos_transaction_end,
    NULL
};
