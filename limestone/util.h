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

#ifndef __dav_repos_util_H__
#define __dav_repos_util_H__

#define DAV_REPOS_MAX_NAMESPACE 1024
#define DAV_REPOS_NODATA -1

#define NULL_NAMESPACE  " "     /* change this to something appropriate */

#define INF_TIME_STR "9999-12-31 00:00:00"

/*-----------------------------------------------------------------
 * Utility functions
 *----------------------------------------------------------------*/

/** 
 * @brief Format the date-time string according to RFC_822.  
 * @param style - format of date-time string. 
 * @param t - input date-time string. 
 * @param buf - buffer, must be atleat APR_RFC822_DATE_LEN chars in size
 */
void dav_repos_format_strtime(int style, const char *t, char *buf);

/**
 * @brief Format apr_time_t into a string,
 * suitable to be stored in the database
 * @param t the input time in apr_time_t format
 * @param datetime the resulting datetime string, 
 * must be atleat APR_RFC822_DATE_LEN chars in size 
 */
void dav_repos_format_time(apr_time_t t, char *datetime);

/**
 * @brief Parse the input date-time string.  
 * @param t - input date-time string.
 * @return parsed time in apr_time_t format
 */
apr_time_t dav_repos_parse_time(const char *t);

/**
 * @brief Parse the input date-time string.  
 * @param datetime - input date-time string.
 * @return parsed time in time_t format
 */
time_t time_datetime_to_ansi(const char *datetime);

/**
 * @brief Format time_t into a string,
 * suitable to be stored in the database
 * @param pool The pool to allocate from
 * @param ansi_time the input time in time_t format
 * @return the resulting datetime string 
 */
const char *time_ansi_to_datetime(apr_pool_t *pool, time_t ansi_time);

/**
 * Build the ns-name key to store the prop in a hash
 * @param ns The namespace
 * @param name Name of the prop
 * @param pool The pool to allocate from
 * @return the generated key
 */
const char *dav_repos_build_ns_name_key(const char *ns, const char *name,
					apr_pool_t * pool);

/** 
 * @brief make sure the pathname does not have a trailing "/".  
 * The trailing slash is not removed if it is the root URI '/'
 * @param str - input string.
 */
void dav_repos_chomp_slash(char *str);

void dav_repos_chomp_trailing_slash(char *str);

/** 
 * @brief Make href XML elem    
 * @param pool The pool to allocate from
 * @param uri The href to construct.  
 * @return the URI constructed.  
 */
const char *dav_repos_mk_href(apr_pool_t * pool, const char *uri);

/** 
 * @brief To search a given attribute in XML data.   
 * @param elem - The XML data. 
 * @param attr_name - The attribute name to be searched in the data.
 * @return The value of the attribute searched, NULL if not found
 */
const char *dav_find_attr(apr_xml_elem *elem, const char *attr_name);

/**
 * @brief Return a path into the filesystem for storege
 * @param path The returned path
 * @param pool The pool to allocate from
 * @param file_dir The DAVDBMSFileDir configuration directive
 * @param hash The (sha1)hash used to compute the path.
 * @return NULL for success, dav_error otherwise
 */
dav_error *generate_path(char **path, apr_pool_t * pool,
                         const char *file_dir, const char *hash);

/**
 * Removes the sha1 file 
 * @param pool The pool to allocate from
 * @param file_dir DAVDBMSFileDir directive
 * @param sha1str The SHA1 string
 */
int remove_sha1_file(apr_pool_t *pool, const char *file_dir, const char
                     *sha1str);

char *compact_uri(apr_pool_t *pool, const char *u);

/**
 * Get the parent uri 
 * @param pool The pool to allocate from
 * @param uri The URI whose parent is to be found
 * @return The parent uri
 */
char *get_parent_uri(apr_pool_t *pool, const char *uri);

/** 
 * Returns a new UUID string after removing all the hyphens
 * @param pool the uuid string is allocated from this
 * @return character string containing a new UUID without hyphens
 */
char *get_new_plain_uuid(apr_pool_t *pool);

/**
 * Add hyphens to UUID string wherever required 
 * @param pool The pool to allocate from
 * @param uuid The original UUID string
 * @return hyphenated UUID string
 */
char *add_hyphens_to_uuid(apr_pool_t *pool, const char *uuid);

/**
 * Get the file size
 * @param pool The pool to allocate from
 * @param filename The given file
 * @return The size of the file
 */
long get_file_length(apr_pool_t *pool, const char *filename);

/**
 * Compute SHA1 of a given file
 * @param pool The pool to allocate from
 * @param filename The given file
 * @param sha1ptr The resultant SHA1
 */
void compute_file_sha1(apr_pool_t *pool, const char *filename,
                       const char **sha1ptr);

/**
 * Remove hyphens from the standard UUID string
 * @param pool The pool to allocate from
 * @param uuid The original uuid string
 * @return UUID string without the hyphens
 */
char *remove_hyphens_from_uuid(apr_pool_t *pool, const char *uuid);

/** 
 * Returns the time in a string suitable for storing in the database
 * @param pool the time string is allocated from this
 * @param aprtime the time in apr_time_t format
 * @return the time in a string
 */
char *time_apr_to_str(apr_pool_t *pool, apr_time_t aprtime);

/**
 * Get the number of tokens in the '/' separated URI
 * @param uri The URI
 * @return the number of tokens
 */
int get_num_tokens(char *uri);

/**
 * Scan the file with given content type
 * @param file_path The location of the file to scan
 * @param contenttype The contenttype provided by client
 * @return 0 if the content type is dangerous, 1 otherwise
 */
int is_content_type_good(const char *file_path, const char *contenttype);

char *reverse_string(char* str);

/**
 * Strip leading and trailing whitespace from data
 * @param string the given string
 * @return substring which doesn't have leading and trailing whitespace
 */
char *strip_whitespace(char *string);

/**
 * Guess the mime-type of a file
 * @param file 
 * @return mime_type
 */
const char *guess_mime_type(const char *file);

char *get_password_hash(apr_pool_t *pool, const char *user, const char *password);

const char *get_mime_type(const char *uri, const char *path);

#endif
