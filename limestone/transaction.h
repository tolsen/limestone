#ifndef TRANSACTION_H
#define TRANSACTION_H

#include "dbms_transaction.h"

/* Uniform interface for DB-FS transactions */
struct dav_transaction_private
{
    /* filesystem errors */
    int fserrno;

    /* DB transaction struct, to be filled by DAL */
    dav_repos_transaction *db_trans;
};

dav_error *dav_repos_transaction_start(request_rec *r, dav_transaction **t);

int dav_repos_transaction_mode_set(dav_transaction *t, int mode);

dav_error *dav_repos_transaction_end(dav_transaction *t);

#endif  /* ifndef TRANSACTION_H */
