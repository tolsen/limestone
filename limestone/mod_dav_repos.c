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

#include <httpd.h>
#include <http_config.h>
#include <http_protocol.h>
#include <http_log.h>
#include <http_core.h>		/* for ap_construct_url */
#include <http_request.h>       /* for ap_hook_create_request */
#include <mod_dav.h>
#include <apr_dbd.h>         /* for apr_dbd_init */

#include <apr_strings.h>
#include <apr_hash.h>
#include <stdlib.h>

#include "scoreboard.h"      /* for pre_mpm hook */

#include "dav_repos.h"
#include "dbms.h"
#include "liveprops.h"
#include "gc.h"

#define INHERIT_VALUE(parent, child, field) \
  ((child)->field ? (child)->field : (parent)->field)

#define DBMS_PROVIDER_NAME "repos"

/* Note: the "dav_repos" prefix is mandatory */
extern module AP_MODULE_DECLARE_DATA dav_repos_module;

static void *dav_repos_create_server_config(apr_pool_t * p, server_rec *s)
{
    dav_repos_server_conf *conf = apr_pcalloc(p, sizeof(*conf));

    /* defaults */
    conf->quota = 10*1024*1024; /* 10 MB */
    return conf;
}

static void *dav_repos_merge_server_config(apr_pool_t * p, void *base,
                                           void *overrides)
{
    dav_repos_server_conf *parent = base;
    dav_repos_server_conf *child = overrides;

    dav_repos_server_conf *newconf;
    newconf = (dav_repos_server_conf *) apr_pcalloc(p, sizeof(*newconf));

    newconf->tmp_dir = INHERIT_VALUE(parent, child, tmp_dir);
    newconf->file_dir = INHERIT_VALUE(parent, child, file_dir);

    newconf->db_driver = INHERIT_VALUE(parent, child, db_driver);
    newconf->dbms = INHERIT_VALUE(parent, child, dbms);
    newconf->db_params = INHERIT_VALUE(parent, child, db_params);

    newconf->use_gc = INHERIT_VALUE(parent, child, use_gc);
    newconf->keep_files = INHERIT_VALUE(parent, child, keep_files);
    newconf->css_uri = INHERIT_VALUE(parent, child, css_uri);
    newconf->xsl_403_uri = INHERIT_VALUE(parent, child, xsl_403_uri);
    newconf->quota = INHERIT_VALUE(parent, child, quota);

    return newconf;
}

/* @brief Get the db struct.  
 * @param r Request struct.  
 */
dav_repos_db *dav_repos_get_db(request_rec * r)
{
    dav_repos_db *db;

    if (r == NULL)
	return NULL;

    db = ap_get_module_config(r->request_config, &dav_repos_module);
    if (db) return db;

    db = ap_get_module_config(r->server->module_config, &dav_repos_module);
    db = memcpy(apr_palloc(r->pool, sizeof(*db)), db, sizeof(*db));

    if (dbms_opendb(db, r->pool, r, NULL, NULL))
        db = NULL;
    ap_set_module_config(r->request_config, &dav_repos_module, db);
    return db;
}

/*
 * Command handler for the DAV directive, which is TAKE1.
 */
static const char *dav_repos_tmp_dir_cmd(cmd_parms * cmd, void *config,
					 const char *arg1)
{
    dav_repos_server_conf *conf =
      ap_get_module_config(cmd->server->module_config, &dav_repos_module);
    conf->tmp_dir = apr_pstrdup(cmd->pool, arg1);
    return NULL;
}

static const char *dav_repos_file_dir_cmd(cmd_parms * cmd, void *config,
					  const char *arg1)
{
    dav_repos_server_conf *conf = 
      ap_get_module_config(cmd->server->module_config, &dav_repos_module);
    conf->file_dir = apr_pstrdup(cmd->pool, arg1);
    return NULL;

}

static const char *dav_repos_dbd_driver_cmd(cmd_parms * cmd,
                                            void *config, const char *arg1)
{
    dav_repos_server_conf *conf = 
      ap_get_module_config(cmd->server->module_config, &dav_repos_module);
    conf->db_driver = arg1;
    if (strcasecmp(arg1, "mysql") == 0)
        conf->dbms = MYSQL;
    else if (strcasecmp(arg1, "pgsql") == 0)
        conf->dbms = PGSQL;
    else return "This database driver is not supported";
    return NULL;
}

static const char *dav_repos_dbd_params_cmd(cmd_parms * cmd,
                                            void *config, const char *arg1)
{
    dav_repos_server_conf *conf = 
      ap_get_module_config(cmd->server->module_config, &dav_repos_module);
    conf->db_params = arg1;
    return NULL;
}

static const char *dav_repos_gc_cmd(cmd_parms *cmd, void *config)
{
    dav_repos_server_conf *conf = 
      ap_get_module_config(cmd->server->module_config, &dav_repos_module);
    conf->use_gc = 1;
    return NULL;
}

static const char *dav_repos_keep_files_cmd(cmd_parms *cmd, void *config, int flag)
{
    dav_repos_server_conf *conf = 
      ap_get_module_config(cmd->server->module_config, &dav_repos_module);
    conf->keep_files = flag;
    return NULL;
}

static const char *dav_repos_IndexCSS_cmd(cmd_parms *cmd, void *config, 
                                          const char *arg1)
{
    dav_repos_server_conf *conf = 
      ap_get_module_config(cmd->server->module_config, &dav_repos_module);
    conf->css_uri = apr_pstrdup(cmd->pool, arg1);
    return NULL;
}

static const char *dav_repos_xsl403_cmd(cmd_parms *cmd, void *config, 
                                          const char *arg1)
{
    dav_repos_server_conf *conf = 
      ap_get_module_config(cmd->server->module_config, &dav_repos_module);
    conf->xsl_403_uri = apr_pstrdup(cmd->pool, arg1);
    return NULL;
}

static const char *dav_repos_quota_cmd(cmd_parms *cmd, void *config, 
                                       const char *arg1)
{
    dav_repos_server_conf *conf = 
      ap_get_module_config(cmd->server->module_config, &dav_repos_module);

    if(arg1)
        conf->quota = atoi(arg1);

    return NULL;
}

static const command_rec dav_repos_cmds[] = {
    /* per directory/location */
    /* how can I make it mandatory */
    /* RSRC_CONF : For server */
    /* ACCESS_CONF : For dir */
    AP_INIT_TAKE1("DAVDBMSTmpDir", dav_repos_tmp_dir_cmd, NULL, RSRC_CONF,
		  "specify the MYSQL_TMP_DIR for a directory or location"),

    AP_INIT_TAKE1("DAVDBMSFileDir", dav_repos_file_dir_cmd, NULL,
		  RSRC_CONF,
		  "specify the directory for permanent external storage"),

    AP_INIT_TAKE1("DBDriver", dav_repos_dbd_driver_cmd, NULL, RSRC_CONF,
                  "SQL Driver"),

    AP_INIT_TAKE1("DBDParams", dav_repos_dbd_params_cmd, NULL, RSRC_CONF,
                  "SQL Driver Params"),

    AP_INIT_NO_ARGS("DAVLimestoneUseGC", dav_repos_gc_cmd, NULL, RSRC_CONF,
                    "Enable the Garbage Collection in a separate thread"),

    AP_INIT_FLAG("DAVLimestoneKeepFiles", dav_repos_keep_files_cmd, NULL, RSRC_CONF,
                    "disable deletion of unaccessible files"),

    AP_INIT_TAKE1("DAVLimestoneIndexCSS", 
                  dav_repos_IndexCSS_cmd, NULL, RSRC_CONF, 
                  "specify the URI of CSS stylesheet for directory indexes"),

    AP_INIT_TAKE1("DAVLimestoneXSL403", 
                  dav_repos_xsl403_cmd, NULL, RSRC_CONF, 
                  "specify the URI of XSL stylesheet for DAV:error responses"),

    AP_INIT_TAKE1("DAVLimestoneUserQuota", 
                  dav_repos_quota_cmd, NULL, RSRC_CONF, 
                  "specify per user quota"),
    {NULL}
};

/* Provider interface */
static const dav_provider dav_repos_provider = {
    &dav_repos_hooks_repos,	        /* repos */
    &dav_repos_hooks_propdb,	        /* prop */
    &dav_repos_hooks_locks,	        /* locks */
    &dav_repos_hooks_vsn,	        /* versioning */
    &dav_repos_hooks_binding,	        /* binding */
    &dav_repos_hooks_search,	        /* search */
    &dav_repos_hooks_acl,	        /* acl */
    &dav_repos_hooks_transaction,       /* transaction */
    &dav_repos_hooks_redirect,          /* redirect */
    NULL			        /* context */
};

static server_rec *server_main = NULL;

static int dav_repos_post_config(apr_pool_t * pconf, apr_pool_t * plog,
                                 apr_pool_t * ptemp, server_rec * s)
{
    ap_add_version_component(pconf, "Limestone/" VERSION);
    server_main = s;

    /* populate the mime_type_ext_map,
     * borrowed mostly from mime_post_config() in mod_mime.c */
    ap_configfile_t *f;
    char l[MAX_STRING_LEN];
    apr_status_t status;
    const char *types_confname = AP_TYPES_CONFIG_FILE;

    types_confname = ap_server_root_relative(pconf, types_confname);

    if ((status = ap_pcfg_openfile(&f, ptemp, types_confname)) != APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_ERR, status, s,
                     "could not open mime types config file %s.",
                     types_confname);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    dav_repos_mime_type_ext_map = apr_hash_make(pconf);

    while (!(ap_cfg_getline(l, MAX_STRING_LEN, f))) {
        const char *ll = l, *ct;

        if (l[0] == '#') {
            continue;
        }
        ct = ap_getword_conf(pconf, &ll);

        while (ll[0]) {
            char *ext = ap_getword_conf(pconf, &ll);
            ap_str_tolower(ext);
            apr_hash_set(dav_repos_mime_type_ext_map, ext, APR_HASH_KEY_STRING, ct);
        }
    }
    ap_cfg_closefile(f);

    /* populate the resource_types array */
    dav_repos_resource_types[dav_repos_RESOURCE] = "Resource";
    dav_repos_resource_types[dav_repos_COLLECTION] = "Collection";
    dav_repos_resource_types[dav_repos_PRINCIPAL] = "Principal";
    dav_repos_resource_types[dav_repos_USER] = "User";
    dav_repos_resource_types[dav_repos_GROUP] = "Group";
    dav_repos_resource_types[dav_repos_REDIRECT] = "Redirect";
    dav_repos_resource_types[dav_repos_VERSION] = "Version";
    dav_repos_resource_types[dav_repos_VERSIONED] = "VersionControlled";
    dav_repos_resource_types[dav_repos_VERSIONHISTORY] = "VersionHistory";
    dav_repos_resource_types[dav_repos_VERSIONED_COLLECTION] = "VersionedCollection";
    dav_repos_resource_types[dav_repos_COLLECTION_VERSION] = "CollectionVersion";
    dav_repos_resource_types[dav_repos_LOCKNULL] = "LockNull";

    return OK;
}

int dav_repos_get_type_id(const char *type) {
    int i;

    for(i=1; i<dav_repos_MAX_TYPES; i++) {
        if (strcmp(dav_repos_resource_types[i], type) == 0) {
            return i;
        }
    }

    return dav_repos_MAX_TYPES;
}

static int dav_repos_pre_mpm(apr_pool_t *pool, ap_scoreboard_e sb_type)
{
    server_rec *sp;
    for (sp = server_main; sp; sp = sp->next) {
        dav_repos_db *db = 
          ap_get_module_config(sp->module_config, &dav_repos_module);
        if (db->use_gc && db->dbms) dav_repos_garbage_collector(pool, db);
    }
    return OK;
}

static int dav_repos_create_request(request_rec *r)
{
    if (r->main) {
        dav_repos_db *db;
        db = ap_get_module_config(r->main->request_config, &dav_repos_module);
        if (db)
            ap_set_module_config(r->request_config, &dav_repos_module, db);
    }
    return OK;
}

static void register_hooks(apr_pool_t * p)
{
    /* apache hooks */
    ap_hook_post_config(dav_repos_post_config, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_pre_mpm(dav_repos_pre_mpm, NULL, NULL, APR_HOOK_MIDDLE);

    ap_hook_create_request(dav_repos_create_request, NULL, NULL, APR_HOOK_MIDDLE);

    /* live property handling */
    dav_repos_register_liveprops(p);
    dav_deltav_register_liveprops(p);
    dav_quota_register_liveprops(p);
    dav_acl_register_liveprops(p);
    dav_binds_register_liveprops(p);
    dav_supported_register_liveprops(p);
    dav_search_register_liveprops(p);
    dav_limebits_register_liveprops(p);
    dav_redirect_register_liveprops(p);

    /* register our provider */
    dav_register_provider(p, DBMS_PROVIDER_NAME, &dav_repos_provider);
}

module DAV_DECLARE_DATA dav_repos_module = {
    STANDARD20_MODULE_STUFF,
    NULL,	/* dir config creater */
    NULL,	/* dir merger --- default is to override */
    dav_repos_create_server_config,     /* server config */
    dav_repos_merge_server_config,	/* merge server config */
    dav_repos_cmds,		/* command table */
    register_hooks,		/* register hooks */
};
