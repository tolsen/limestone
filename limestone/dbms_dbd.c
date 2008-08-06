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

#include <apr_strings.h>
#include <unistd.h> /* for getpid */

#include "dav_repos.h"

#include "dbms_dbd.h"

#ifdef DEBUG
static int query_count = 0;
#endif

dav_repos_dbms *dbms_api_opendb(apr_pool_t *pool, void *p)
{
    dav_repos_dbms *db = apr_pcalloc(pool, sizeof(dav_repos_dbms));
    request_rec *r = p;
    db->ap_dbd_dbms = ap_dbd_acquire(r);
    if (db->ap_dbd_dbms) return db;
    else return NULL;
}

dav_repos_dbms *dbms_api_opendb_params(apr_pool_t *pool,
                                       const char *driver, const char *params)
{
    dav_repos_dbms *db = apr_pcalloc(pool, sizeof(dav_repos_dbms));
    const apr_dbd_driver_t *dbd_driver;
    apr_dbd_t *dbd_handle;

    if (APR_SUCCESS != apr_dbd_get_driver(pool, driver, &dbd_driver))
        return NULL;

    if (APR_SUCCESS != apr_dbd_open(dbd_driver, pool, params, &dbd_handle))
        return NULL;

    db->ap_dbd_dbms = apr_pcalloc(pool, sizeof(ap_dbd_t));
    db->ap_dbd_dbms->driver = dbd_driver;
    db->ap_dbd_dbms->handle = dbd_handle;
    db->apr_dbd = 1;
    return db;
}

void dbms_api_closedb(dav_repos_dbms *dbms)
{
    ap_dbd_t *db = dbms->ap_dbd_dbms;
    if (dbms->apr_dbd)
        apr_dbd_close(db->driver, db->handle);
}

const char *dbms_error(apr_pool_t * pool, const dav_repos_dbms * repos_db)
{
    ap_dbd_t *db = repos_db->ap_dbd_dbms;
    return apr_pstrdup(pool, apr_dbd_error(db->driver, db->handle, 0));
}

dav_repos_query *dbms_prepare(apr_pool_t * pool,
			      const dav_repos_dbms * dbms,
			      const char *query)
{
    dav_repos_query *q;
    int i, query_len;

    query_len = strlen(query);

    q = (dav_repos_query *) apr_pcalloc(pool, sizeof(dav_repos_query));
    apr_pool_create(&(q->pool), pool);
    q->ppool = pool;
    q->db = (dav_repos_dbms *) dbms;

    q->query_string = (char *) apr_pstrndup(pool, query, query_len);
    q->state = DAV_REPOS_STATE_PREPARED;
    for (i = 0, q->param_count = 0; i < query_len; i++) {
	if (query[i] == '?')
	    q->param_count++;
    }
    /* should I use apr_pcalloc instead? */
    q->parameters =
	(char **) apr_pcalloc(pool, sizeof(char *) * q->param_count);
    for (i = 0; i < q->param_count; i++)
	q->parameters[i] = NULL;

#ifdef DEBUG
    query_count++;
    DBG1("QUERY CREATED: %d\n", query_count);
#endif
    return q;
}

int dbms_set_int(dav_repos_query * query,
		 const int num, const long value)
{

    if (num < 1 || num > query->param_count) 
	return -1;
    query->parameters[num - 1] = apr_psprintf(query->pool, "%ld", value);
    return 0;
}

int dbms_set_float(dav_repos_query * query,
		   const int num, const double value)
{

    if (num < 1 || num > query->param_count)
	return -1;
    query->parameters[num - 1] = apr_psprintf(query->pool, "%f", value);
    return 0;
}

int dbms_set_string(dav_repos_query * query, const int num,
		    const char *value)
{

    const char *param;

    if (num < 1 || num > query->param_count)
	return -1;

    if (!value) {
	query->parameters[num - 1] = apr_pstrdup(query->pool, "null");
	return 0;
    }

    param = dbms_escape(query->pool, query->db, value);
    query->parameters[num - 1] = apr_psprintf(query->pool, "'%s'", param);
    return 0;
}

const char *dbms_escape(apr_pool_t *pool, const dav_repos_dbms *db,
                        const char *string) 
{
    return apr_dbd_escape(db->ap_dbd_dbms->driver, pool, 
                          string, db->ap_dbd_dbms->handle);

}

int dbms_execute(dav_repos_query * query)
{
    int full_length, query_string_length;
    int i, j, k, error;
    char *escquery;

    full_length = query_string_length = strlen(query->query_string);

    for (i = 0; i < query->param_count; i++) {
	if (!query->parameters[i])
	    return 0;		/* parameter was never set */
	full_length += strlen(query->parameters[i]) - 1;	/* dont count orig ? */
    }

    if (query->param_count == 0) {
	escquery = apr_pstrdup(query->pool, query->query_string);
    } else {

	/* make space for the trailing '\0' */
	escquery = apr_pcalloc(query->pool, full_length + 1);

	for (i = 0, j = 0, k = 0; i < query_string_length; i++) {

	    if (query->query_string[i] == '?') {
		strcpy(escquery + j, query->parameters[k]);
		j += strlen(query->parameters[k++]);
	    } else {
		escquery[j++] = query->query_string[i];
	    }
	}
	escquery[j] = 0;
    }

    //DBG2("[%d]Query to execute: %s\n", getpid(), escquery);
    DBG1("Query to execute: %s\n", escquery);

    if (!strncasecmp("select", escquery, 6)) {
	query->is_select = 1;
	error =
	    apr_dbd_select(query->db->ap_dbd_dbms->driver, query->pool,
			   query->db->ap_dbd_dbms->handle, &(query->results),
			   escquery, 1);
	if (error) {
	    DBG1("\nError Code returned:%d in apr_dbd_select\n", error);
	    query->state = DAV_REPOS_STATE_ERROR;
	    return error;
	}

	query->colcount =
	    apr_dbd_num_cols(query->db->ap_dbd_dbms->driver, query->results);
    } else {
	error =
	    apr_dbd_query(query->db->ap_dbd_dbms->driver, 
                          query->db->ap_dbd_dbms->handle,
			  &(query->nrows), escquery);
	if (error) {
	    DBG1("\nError Code returned:%d in apr_dbd_query\n", error);
	    query->state = DAV_REPOS_STATE_ERROR;
	    return error;
	}
    }

    query->state = DAV_REPOS_STATE_EXECUTED;
    return 0;
}

/**
 * Return the number of rows affected
 * For SELECT queries this will be zero
 */
int dbms_nrows(dav_repos_query *query) {
    return query->nrows;
}

int dbms_next(dav_repos_query * query)
{
    int errno;

    if (query->state != DAV_REPOS_STATE_EXECUTED || !query->results)
	return -1;

    errno =
	apr_dbd_get_row(query->db->ap_dbd_dbms->driver, query->pool, query->results,
			&(query->row), -1);

    if (errno == -1)
	return 0;
    if (errno)
	return -1;
    return 1;
}

long long dbms_get_int(dav_repos_query * query, int column)
{
    const char *value;
    if (!query->row || column < 1 || column > query->colcount + 1)
	return 0;
    value = 
      apr_dbd_get_entry(query->db->ap_dbd_dbms->driver, query->row, column-1);
    if (value) return atoll(value);
    return -1;
}

char *dbms_get_string(dav_repos_query * query, int column)
{
    if (!query->row || column < 1 || column > query->colcount + 1)
	return NULL;
    return apr_pstrdup(query->ppool,
		       apr_dbd_get_entry(query->db->ap_dbd_dbms->driver, query->row,
					 column - 1));
}

int dbms_query_destroy(dav_repos_query * query)
{
    apr_pool_destroy(query->pool);
#ifdef DEBUG
    query_count--;
    DBG1("QUERY DESTROYED: %d\n", query_count);
#endif
    return 0;
}

long dbms_insert_id(const dav_repos_dbms * db, const char *table, 
                    apr_pool_t * pool)
{
    dav_repos_query *q;
    long id;
    char *max_id_query = apr_psprintf(pool, "SELECT MAX(id) FROM %s", table);
    
    q = dbms_prepare(pool, db, max_id_query);
    dbms_execute(q);
    dbms_next(q);
    id = dbms_get_int(q, 1);
    dbms_query_destroy(q);

    return id;
}

char **dbms_fetch_row(const dav_repos_dbms * db,
		      dav_repos_query *query, apr_pool_t * pool)
{

    apr_dbd_row_t *row = query->row;
    int ncols, i;
    char **dbrow;
    apr_dbd_results_t * res = query->results;

    if (apr_dbd_get_row(db->ap_dbd_dbms->driver, pool, res, &row, -1) == -1)
	return NULL;
    ncols = apr_dbd_num_cols(db->ap_dbd_dbms->driver, res);
    dbrow = (char **) apr_pcalloc(pool, ncols * sizeof(char *));
    for (i = 0; i < ncols; i++)
	dbrow[i] =
	    apr_pstrdup(pool, apr_dbd_get_entry(db->ap_dbd_dbms->driver, row, i));

    return dbrow;
}

char **dbms_fetch_row_num(const dav_repos_dbms * db,
		      dav_repos_query *query, apr_pool_t * pool, int num)
{

    apr_dbd_row_t *row = query->row;
    int ncols, i;
    char **dbrow;
    apr_dbd_results_t * res = query->results;

    if (apr_dbd_get_row(db->ap_dbd_dbms->driver, pool, res, &row, num + 1) == -1)
	return NULL;
    ncols = apr_dbd_num_cols(db->ap_dbd_dbms->driver, res);
    dbrow = (char **) apr_pcalloc(pool, ncols * sizeof(char *));
    for (i = 0; i < ncols; i++)
	dbrow[i] =
	    apr_pstrdup(pool, apr_dbd_get_entry(db->ap_dbd_dbms->driver, row, i));

    return dbrow;
}

int dbms_results_count(dav_repos_query* q)
{
    return apr_dbd_num_tuples(q->db->ap_dbd_dbms->driver, q->results);
}
