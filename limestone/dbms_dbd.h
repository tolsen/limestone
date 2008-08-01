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

#ifndef DBMS_DBD_H
#define DBMS_DBD_H

#include "dbms_api.h"

#include <apr_dbd.h>
#include <mod_dbd.h>

#include <apr_pools.h>

/* Parameter types */
#define DAV_REPOS_TYPE_INT 1
#define DAV_REPOS_TYPE_FLOAT 2
#define DAV_REPOS_TYPE_STRING 3

/* Query status */
#define DAV_REPOS_STATE_PREPARED 1
#define DAV_REPOS_STATE_EXECUTED 2
#define DAV_REPOS_STATE_ERROR 3

struct dav_repos_dbms {
    ap_dbd_t *ap_dbd_dbms;
    int apr_dbd;
};

struct dav_repos_query {
    dav_repos_dbms *db;		/* handle to the database */

    apr_pool_t *ppool;		/* parent pool */
    apr_pool_t *pool;		/* child pool for this query */

    int is_select;		/* does this query return any result */
    short int state;		/* one of DAV_REPOS_STATE_* */

    char *query_string;		/* query string with question marks */
    char **parameters;		/* parameters set by dbms_set_ functions */
    short int *parameter_type;	/* one of DAV_REPOS_TYPE_* corresponding to each parameter */
    int param_count;		/* total number of parameters */

    apr_dbd_results_t *results; /* results returned after executionn */
    int colcount;		/* number of columns */
    int nrows;			/* number of rows */
    apr_dbd_row_t *row;		/* current row of results */
};

#endif				/* DBMS_DBD_H */
