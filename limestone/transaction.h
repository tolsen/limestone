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

#endif  /* ifndef TRANSACTION_H */
