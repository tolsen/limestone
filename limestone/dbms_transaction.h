#ifndef DBMS_TRANSACTION_H
#define DBMS_TRANSACTION_H

#include "dav_repos.h"
#include "dbms.h"
#include <apr_dbd.h>

typedef enum {
  READ_UNCOMMITTED,
  READ_COMMITTED,
  REPEATABLE_READ,
  SERIALIZABLE
} xaction_iso_level;

int dbms_set_session_xaction_iso_level(apr_pool_t *pool,
                                       const dav_repos_db *d,
                                       xaction_iso_level level);

dav_error *dbms_quota_pre_commit_checks(apr_pool_t *pool, const dav_repos_dbms *db);

struct dav_repos_transaction
{
    /* Pool to allocate from */
    apr_pool_t *pool;

    /* DB handle */
    const dav_repos_dbms *db;

    /* Transaction handle */
    apr_dbd_transaction_t *ap_trans;
};

typedef struct dav_repos_transaction dav_repos_transaction;

#endif  /* ifndef DBMS_TRANSACTION_H */
