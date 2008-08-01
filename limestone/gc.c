#include "gc.h"
#include <apr_thread_proc.h>
#include "http_log.h"
#include "dbms.h"
#include "bridge.h"
#include "http_config.h" /* for ap_get_module_config */
#include "dbms_api.h" /* for transaction_start and _end */
#include "dbms_bind.h"

extern module AP_MODULE_DECLARE_DATA dav_repos_module;

apr_status_t gc_stop(void *data);
void *gc_main(apr_thread_t *thread, void *data);

int dav_repos_garbage_collector(apr_pool_t *proc_pool,
                                dav_repos_db *db)
{
    apr_status_t rv;
    apr_threadattr_t *tattr;
    apr_thread_t *thread;
    apr_pool_t *pool;

    TRACE();

    apr_pool_create(&pool, proc_pool);

    rv = apr_threadattr_create(&tattr, pool);
    /* create a detached thread */
    rv = apr_threadattr_detach_set(tattr, 1);

    db = memcpy(malloc(sizeof(dav_repos_db)), db, sizeof(dav_repos_db));

    rv = apr_thread_create(&thread, tattr, gc_main, db, pool);
    apr_pool_cleanup_register(pool, db, gc_stop, apr_pool_cleanup_null);

    ap_log_error(APLOG_MARK, APLOG_NOTICE, 0, NULL, 
                 "Starting GC as a thread on pid %d", getpid());

    return 0;
}

apr_status_t gc_stop(void *pdata){
    dav_repos_db *db = pdata;
    TRACE();

    ap_log_error(APLOG_MARK, APLOG_NOTICE, 0, NULL, 
                 "Stopping GC thread on pid %d", getpid());
    db->use_gc = 0;
    do { apr_sleep(apr_time_from_sec(1)); } while (!db->use_gc);
    ap_log_error(APLOG_MARK, APLOG_NOTICE, 0, NULL, 
                 "Stopped GC thread on pid %d", getpid());
    return APR_SUCCESS;
}

void *gc_main(apr_thread_t *thread, void *pdata)
{
    apr_pool_t *pool;
    dav_repos_db *db = pdata;

    TRACE();

    apr_pool_create(&pool, NULL);
    if (dbms_opendb(db, pool, NULL, db->db_driver, db->db_params)) {
        apr_pool_destroy(pool);

        ap_log_error(APLOG_MARK, APLOG_NOTICE, 0, NULL,
                     "Garbage Collector couldn't connect to DBMS");
        return NULL;
    }

    ap_log_error(APLOG_MARK, APLOG_NOTICE, 0, NULL, "Garbage Collector Started");

    apr_pool_t *sub_pool;
    apr_pool_create(&sub_pool, pool);
    while(db->use_gc) {
        dav_repos_transaction *xaction;
        dav_repos_resource *db_r;
        dbms_bind_list *child_binds = NULL;
        long resource_id = 0;
        int deleted = 0;
        dav_error *err = NULL;

        DBG0("\nGC_TRANSACTION_START\n");

        /* make a subpool */
        dbms_transaction_start(sub_pool, db, &xaction);
        dbms_transaction_mode_set(xaction, DAV_TRANSACTION_COMMIT);

        dbms_next_cleanup_req (sub_pool, db, &resource_id);
        if (resource_id == 0) {
            dbms_transaction_end(xaction);
            DBG0("\nGC_TRANSACTION_END\n");
            apr_pool_clear(sub_pool);
            apr_sleep(apr_time_from_sec(1));
            continue;
        }

        db_r = apr_pcalloc(sub_pool, sizeof(*db_r));
        db_r->p = sub_pool;
        db_r->serialno = resource_id;
        dbms_get_property(db, db_r);

        err = dbms_get_child_binds(db, db_r, 1, &child_binds);
        if (err)
            ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL,
                         "error getting child binds");

        sabridge_delete_if_orphaned(db, db_r, &deleted);

        if (deleted) {
            err = dbms_insert_cleanup_reqs(sub_pool, db, child_binds);
            if (err) 
                ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL,
                             "error inserting child binds cleanup reqs ");
        }

        dbms_transaction_end(xaction);
        DBG1("\nGC_TRANSACTION_END: %ld\n", resource_id);
        /* delete the subpool */
        apr_pool_clear(sub_pool);

    }

    dbms_closedb(db);
    apr_pool_destroy(pool);

    ap_log_error(APLOG_MARK, APLOG_NOTICE, 0, NULL, "Garbage Collector Stopped");
    db->use_gc = 1;
    return NULL;
}
