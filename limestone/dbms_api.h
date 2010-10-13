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

#ifndef DBMS_API_H
#define DBMS_API_H

#include "dbms_transaction.h"   /* required for  dav_repos_transaction */
#ifndef DAV_REPOS_DBMS_OPAQUE_T
#define DAV_REPOS_DBMS_OPAQUE_T
struct dav_repos_dbms;
typedef struct dav_repos_dbms dav_repos_dbms;
#endif

/**
 * This piece of code provides a JDBC-like API for query execution. In
 * particular, it provides for parameter substitution (with parameter value
 * escaping.) This *may* actually improve performance if you have multiple
 * queries sharing some common parameters. An abstract example:
 *
 * dav_repos_query *q;
 * apr_pool_t *pool;
 * apr_pool_create(&pool, NULL);
 *
 * q = dbms_prepare(pool, db,
 *   "select address from employee where name = ? and officenum = ?");
 * dbms_set_string(q, 1, "Chris Knight");
 * dbms_set_int(q, 2, 295);
 *
 * err = dbms_execute(q);
 *
 * if (err) { apr_pool_destroy(pool); exit(ERR); }
 * ... (use the result as you would normally)
 * dbms_set_string(pool, q, 2, 101);
 * err = dbms_execute(pool, q);
 * if (err) { apr_pool_destroy(pool); exit(ERR); }
 *
 * apr_pool_destroy(pool);
 * 
 */

struct dav_repos_query;
typedef struct dav_repos_query dav_repos_query;

/** 
 * Opens the database and returns a handle
 * @param pool - The memory pool to allocate from
 * @param s - Any additional arguments required by the provider
              e.g mod_dbd provider needs the server record
 * @return A handle to the database. NULL on error
 */
dav_repos_dbms *dbms_api_opendb(apr_pool_t *pool, void *p);

dav_repos_dbms *dbms_api_opendb_params(apr_pool_t *pool,
                                       const char *driver, const char *params);

/** 
 * Closes an open database connection
 * @param db - The handle to the database
 * @param s - Any additional arguments requried by the provider
              e.g mod_dbd provider needs the server record
 */
void dbms_api_closedb(dav_repos_dbms *db);

/**
 * Creates a new dav_repos_query object. Returns null when there is a failure.
 * @param pool - The memory pool to allocate from.
 * @param dbms - The database to perform the query on.
 * @param query - The query you would like to execute. Any parameters you will
 *   pass to this query should be denoted by "?" placeholders in the query
 *   string.  I.e. "select address from employee where name = ?"  Note you
 *   should *not* place quotes around placeholders you plan on replacing with
 *   strings.
 * @return 0 on success
 * @see #dbms_set_int
 */
dav_repos_query *dbms_prepare(apr_pool_t * pool,
			      const dav_repos_dbms * dbms,
			      const char *query);

/**
 * Replaces a placeholder "?" with an integer value.
 * @param query - The query that you would like to place the value in.
 * @param num - The number of the "?" to replace (the first is #1, etc.)
 * @param value - The value to place at that position.
 * @return 0 on success
 * @see #dbms_set_float
 * @see #dbms_set_string
 */
int dbms_set_int(dav_repos_query * query,
		 const int num, const long value);

/**
 * Replaces a placeholder "?" with an floating-point value.
 * @param query - The query that you would like to place the value in.
 * @param num - The number of the "?" to replace (the first is #1, etc.)
 * @param value - The value to place at that position.
 * @return 0 on success
 * @see #dbms_set_int
 * @see #dbms_set_string
 */
int dbms_set_float(dav_repos_query * query,
		   const int num, const double value);

/**
 * Replaces a placeholder "?" with a string value. Note, this creates an
 * escaped version of this string in the dbms's memory pool.
 * @param query - The query that you would like to place the value in.
 * @param num - The number of the "?" to replace (the first is #1, etc.)
 * @param value - The value to place at that position.
 * @return 0 on success
 * @see #dbms_set_int
 * @see #dbms_set_float
 */
int dbms_set_string(dav_repos_query * query, const int num,
		    const char *value);

/**
 * Escape a given string
 * making it safe for including in a DBMS query
 * @param pool - The pool to allocate from
 * @param db - The db handle
 * @param string - String that you want to escape
 * @return The escaped string
 */
const char *dbms_escape(apr_pool_t *pool, const dav_repos_dbms *db,
                        const char *string);

/**
 * Executes the query.
 * @param query - The query to execute
 * @return 0 on success
 */
int dbms_execute(dav_repos_query * query);

/**
 * @param query - the query handle
 * @return The no of rows affected by the query
 */ 
int dbms_nrows(dav_repos_query *query);

/**
 * Fetches the next row of results.
 * @param query - the query handle
 * @return 1 if success, 0 if no remaining rows, -1 if there is an error.
 */
int dbms_next(dav_repos_query * query);

/**
 * Retrieve an integer value from the database.
 * @param query - the query struct
 * @param column - The column number in the result set, starting with 1.
 * @return The value of the column in the current row.
 * @see #dbms_next
 * @see #dbms_get_string
 */
long long dbms_get_int(dav_repos_query * query, int column);

/**
 * Retrieve an string value from the database. Note that the memory buffer
 * allocated for this is taken from the pool, so be sure to duplicate it
 * before you destroy the pool!
 * @param query - the query struct
 * @param column - The column number in the result set, starting with 1.
 * @return The value of the column in the current row.
 * @see #dbms_next
 * @see #dbms_get_int
 */
char *dbms_get_string(dav_repos_query * query, int column);

/**
 * Releases any resources allocated for this query.
 * @param query - the query handle
 * @return 0 on success.
 */
int dbms_query_destroy(dav_repos_query * query);

/** 
 * Fetches the next row of results
 * @deprecated Use dbms_next and dbms_get_* functions instead
 * @param db - handle to the database
 * @param query - the query which was executed to get the results
 * @param pool - pool from which the results
 * @return the result row converted into an array of strings
 */
char **dbms_fetch_row(const dav_repos_dbms * db, dav_repos_query *query,
		      apr_pool_t * pool);

/** 
 * Fetches the row of results according to number
 * @deprecated Use dbms_next and dbms_get_* functions instead
 * @param db - handle to the database
 * @param query - the query which was executed to get the results
 * @param pool - pool from which the results
 * @param num - the row id
 * @return the result row converted into an array of strings
 */
char **dbms_fetch_row_num(const dav_repos_dbms * db, dav_repos_query *query,
		      apr_pool_t * pool, int num);

/**
 * Generates an error message with database specifics. Remember, the error
 * message string may come from this pool, so do something with it before
 * destroying the pool.
 * @param pool - pool from which the error string should be created
 * @param db - handle to the database
 * @return the error message
 */
const char *dbms_error(apr_pool_t * pool, const dav_repos_dbms * db);

/**
 * Returns number of tuples in the result
 * @param q - query
 * @return number of tuples in result
 **/
int dbms_results_count(dav_repos_query* q);

/** 
 * Starts a transaction
 * @param pool - pool to allocate resources from
 * @param db - handle to database
 * @param trans - handle to the newly created transaction
 * @return 0 on success, error number otherwise
 */
int dbms_transaction_start(apr_pool_t * pool, const dav_repos_db *d,
                           dav_repos_transaction **trans);

/** 
 * Ends a transaction
 * @param trans - transaction handle
 * @return 0 on success, error number otherwise
 */
int dbms_transaction_end(dav_repos_transaction *trans);

/** 
 * Set the mode of the transaction.
 * @param trans - transaction handle
 * @param mode - The mode to set.
 * @return the new mode.
 */
int dbms_transaction_mode_set(dav_repos_transaction *trans, int mode);

#endif /* DBMS_API_H */
