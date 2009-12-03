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

#include <apr.h>
#include <apr_strings.h>
#include <apr_hash.h>
#include <apr_tables.h>
#include <apr_file_io.h>
#include <apr_uuid.h>
#include <apr_sha1.h>
#include <apr_xml.h>    /* for apr_xml_elem */
#include <apr_fnmatch.h> /* for scanning in is_content_type_good */
#include <apr_md5.h>

#include <mod_dav.h>    /* for dav_error */

#include <ctype.h> /* for isspace */

#include "util.h"
#include "dav_repos.h"  /* for TRACE */

#ifdef USE_LIBMAGIC
#include <magic.h>      /* for guessing mime-types */ 
#endif
void dav_repos_format_strtime(int style, const char *t, char *buf)
{
    apr_time_t aprtime;
    apr_size_t size;
    apr_time_exp_t tx;

    TRACE();

    aprtime = dav_repos_parse_time(t);
    apr_time_exp_gmt(&tx, aprtime);

    if( style == DAV_STYLE_ISO8601 ) {
	apr_strftime(buf, &size, APR_CTIME_LEN, "%Y-%m-%dT%H:%M:%SZ", &tx);
    }
    else {	/*RFC 822 DATETIME format */
	apr_rfc822_date(buf, aprtime );
    }
}

void dav_repos_format_time(apr_time_t t, char *datetime) 
{
    apr_time_exp_t tx;
    apr_size_t size;
    
    if(t == 0) {
        sprintf(datetime, INF_TIME_STR);
    }
    else {
        apr_time_exp_gmt(&tx, t);
        apr_strftime(datetime, &size, APR_RFC822_DATE_LEN * sizeof(char), 
                     "%Y-%m-%d %X", &tx);
    }
}

apr_time_t dav_repos_parse_time(const char *t)
{
    apr_time_exp_t tx = {0};
    apr_time_t result;

    if(0 == strcmp(t, INF_TIME_STR))
        return 0;

    sscanf( t, "%d-%d-%d %d:%d:%d", &tx.tm_year, &tx.tm_mon, &tx.tm_mday, 
	    &tx.tm_hour, &tx.tm_min, &tx.tm_sec );

    tx.tm_year -= 1900; 	// apr_time_exp_t expects years after 1900
    tx.tm_mon--;		// apr_time_exp_t expects mon between 0 & 11

    apr_time_exp_get( &result, &tx );

    return result;
}

time_t time_datetime_to_ansi(const char *datetime)
{
    apr_time_t apr_time = dav_repos_parse_time(datetime);
    time_t ansi_time = apr_time_sec(apr_time);
    return ansi_time;
}

const char *time_ansi_to_datetime(apr_pool_t *pool, time_t ansi_time)
{
    char *datetime = apr_pcalloc(pool, APR_RFC822_DATE_LEN + 1);
    apr_time_t apr_time;
    apr_time_ansi_put(&apr_time, ansi_time);
    dav_repos_format_time(apr_time, datetime);
    return datetime;
}

const char *dav_repos_build_ns_name_key(const char *ns, const char *name,
					apr_pool_t * pool)
{
    /* Woops */
    if (ns == NULL || name == NULL || pool == NULL)
	return NULL;

    if (strlen(ns) == 0)
	ns = NULL_NAMESPACE;		/* No namespace */

    return apr_psprintf(pool, "%s\t%s", ns, name);
}

void dav_repos_chomp_slash(char *str)
{
    int len = strlen(str);
    while (len > 0 && str[len - 1] == '/') {
	str[len - 1] = '\0';
	len = strlen(str);
    }
}

void dav_repos_chomp_trailing_slash(char *str) {
    if (strlen(str) > 1) {
        dav_repos_chomp_slash(str);
    }
}

const char *dav_repos_mk_href(apr_pool_t * pool, const char *uri)
{
    if (uri == NULL)
	return NULL;

    return apr_pstrcat(pool, "<D:href>", apr_xml_quote_string(pool, uri, 0), 
                       "</D:href>", NULL);
}

const char *dav_find_attr(apr_xml_elem *elem, const char *attr_name)
{
     apr_xml_attr *iter_attr = NULL;

     if(elem)
	  iter_attr = elem->attr;

     while(iter_attr) {
	  if(strcmp(iter_attr->name, attr_name) == 0)
	       return iter_attr->value;
	  iter_attr = iter_attr->next;
     }

     return NULL;
}

dav_error *generate_path(char **path, apr_pool_t * pool,
                         const char *file_dir, const char *hash)
{
  char *dirpath;

  TRACE();

  dirpath = apr_psprintf(pool, "%s/%c%c/%c%c/%c%c/%c%c", file_dir,
                         hash[0], hash[1], hash[2], hash[3],
                         hash[4], hash[5], hash[6], hash[7]);
  ap_no2slash(dirpath);

  if (apr_dir_make_recursive(dirpath, APR_OS_DEFAULT, pool) != APR_SUCCESS) 
    return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                         "Unable to create directory for storage.");

  *path = apr_psprintf(pool, "%s/%s", dirpath, hash + 8);
  return NULL;
}

int remove_sha1_file(apr_pool_t *pool, const char *file_dir, const char
    *sha1str)
{
  char *sha1_file;

  TRACE();

  generate_path(&sha1_file, pool, file_dir, sha1str);
  DBG1("Removing file: %s", sha1_file);
  apr_file_remove(sha1_file, pool);
    
  return 0;
}

/* compact consecutive '/'s into a single '/' */
char *compact_uri(apr_pool_t *pool, const char *u)
{
    int i, j = 0;
    char *v = apr_pcalloc(pool, (strlen(u) + 1) * sizeof(char));

    v[j] = u[j];
    for(i = 1; i < strlen(u); i++) {
        if(v[j] != '/' || u[i] != '/') {
            j = j + 1;
            v[j] = u[i];
        }
    }

    return v;
}

char *get_parent_uri(apr_pool_t *pool, const char *uri)
{
    char *parent_uri = ap_make_dirstr_parent(pool, uri);
    dav_repos_chomp_slash(parent_uri);
    return parent_uri;
}

char *get_new_plain_uuid(apr_pool_t *pool)
{
    apr_uuid_t uuid;
    char buf[APR_UUID_FORMATTED_LENGTH + 1];

    TRACE();

    apr_uuid_get(&uuid);
    apr_uuid_format(buf, &uuid);

    return remove_hyphens_from_uuid(pool, buf);
}

char *add_hyphens_to_uuid(apr_pool_t *pool, const char *uuid)
{
    int i=0, j=0;
    char *formatted_uuid = 
      apr_pcalloc(pool, sizeof(char) * (APR_UUID_FORMATTED_LENGTH + 1));


    for (i=0; i<32; i++) {
        if(i==8 || i==12 || i==16 || i==20)
            formatted_uuid[j++] = '-';
        formatted_uuid[j++] = uuid[i];
    }
    return formatted_uuid;
}

long get_file_length(apr_pool_t *pool, const char *filename) 
{
    apr_finfo_t info = { 0 };
    apr_stat(&info, filename, APR_FINFO_SIZE, pool);
    return info.size;
}

void compute_file_sha1(apr_pool_t *pool, const char *filename,
                       const char **sha1ptr)
{
    apr_file_t *file;
    void *buf;
    apr_size_t size = get_file_length(pool, filename);
    apr_sha1_ctx_t context;
    unsigned char digest[APR_SHA1_DIGESTSIZE+1];
    char *sha1str="";
    int i,j;

    buf = apr_pcalloc(pool, size);
    /* memory map the file instead of reading into memory? */

    if (apr_file_open(&file, filename, APR_READ, 0, pool) != APR_SUCCESS)
        *sha1ptr = NULL;

    apr_file_read(file, buf, &size);

    apr_sha1_init(&context);
    apr_sha1_update(&context, buf, size);
    apr_sha1_final(digest, &context);
    digest[APR_SHA1_DIGESTSIZE] = '\0';
    for (i = 0; i < 5; i++) {
        for (j = 0; j < 4; j++) {
            char *temp = apr_psprintf(pool, "%02x", digest[i * 4 + j]);
            sha1str = apr_pstrcat(pool, sha1str, temp, NULL);
        }
    }
    
    *sha1ptr = sha1str;
}

char *remove_hyphens_from_uuid(apr_pool_t *pool, const char *uuid)
{
    char buf[33];
    int i=0, j=0;

    for (i = 0; i < strlen(uuid); i++)
	if (uuid[i] != '-')
	    buf[j++] = uuid[i];	// remove the '-' from standard uuid format
    buf[j] = '\0';

    return apr_pstrdup(pool, buf);
}

char *time_apr_to_str(apr_pool_t *pool, apr_time_t aprtime)
{
    char *buff = apr_pcalloc(pool, APR_RFC822_DATE_LEN * sizeof(char));
    dav_repos_format_time(aprtime, buff);
    return buff;
}

int get_num_tokens(char *uri)
{
    char *state;
    char *next_tok;
    int num_toks = 0;

    next_tok = apr_strtok(uri, "/", &state);
    while (next_tok) {
        num_toks ++;
        next_tok = apr_strtok(NULL, "/", &state);
    }
    return num_toks;
}

int is_content_type_good(const char* file_path, const char *content_type)
{
    int i;
    const char *bad_content_types[] = {
        "multipart/form-data",
        NULL
    };

    if(!content_type || !*content_type)
        return 0;

    for(i=0; bad_content_types[i] != NULL; i++)
        if(strncmp(content_type, bad_content_types[i], 
                   strlen(bad_content_types[i])) == 0)
            return 0;

    return 1;
}

char *reverse_string(char* str)
{
  int end= strlen(str)-1;
  int start = 0;

  while( start<end )
  {
    str[start] ^= str[end];
    str[end] ^=  str[start];
    str[start]^= str[end];

    ++start;
    --end;
  }

  return str;
}

char *strip_whitespace(char *data)
{
    int len;
    if (data == NULL || *data == '\0')
        return data;

    len = strlen(data);
    while (isspace(data[--len]) && len >= 0) data[len] = 0;
    while (data && *data && isspace(*data)) data++;
    return data;
}

const char *guess_mime_type(const char *file)
{
#ifdef USE_LIBMAGIC
    magic_t magic_db = magic_open(MAGIC_MIME);

    if(!magic_db) 
        goto error;

    if(magic_load(magic_db, NULL)) 
        goto error;

    const char *mime_type = magic_file(magic_db, file);
    if(!mime_type || !*mime_type)
        goto error;

    return mime_type;
error:
#endif
    return "application/octet-stream";
}

char *get_password_hash(apr_pool_t *pool, const char *user, const char *password)
{
    int i;
    unsigned char pwhash[APR_MD5_DIGESTSIZE] = {0};
    char exppwhash[2*APR_MD5_DIGESTSIZE+1]={0}, *pstring;

    /* Map the username:realm:password to its md5 hash */
    pstring = apr_psprintf(pool, "%s:users@limebits.com:%s",
                           user, password);
    apr_md5(pwhash, pstring, strlen(pstring));
    for(i=0; i < APR_MD5_DIGESTSIZE; i++)
        sprintf(exppwhash + 2*i, "%02x", pwhash[i]);
    return apr_pstrdup(pool, exppwhash);
}

const char *get_mime_type(const char *uri, const char *path)
{
    const char *filename = basename(uri);
    const char *ext = strrchr(filename, '.');
    const char *mime_type = NULL;

    /* lookup file extension in the mime_type_ext_map */
    if (ext && ++ext) {
        mime_type = apr_hash_get(dav_repos_mime_type_ext_map, ext, 
                                 APR_HASH_KEY_STRING);
    }

    /* if the mime_type lookup failed, try to guess the mime_type */
    if(mime_type == NULL) {
        mime_type = guess_mime_type(path);
    }

    return mime_type;
}

