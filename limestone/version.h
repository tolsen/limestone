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

#ifndef __dav_repos_version_H__
#define __dav_repos_version_H__

#include <mod_dav.h>

/*
 ** This structure is used to make report response
 **
 ** "nmspace" should be valid XML and URL-quoted. mod_dav will place
 ** double-quotes around it and use it in an xmlns declaration.
 */
typedef struct dav_repos_report_elem {
    const char *nmspace;	/* namespace of the XML report element */
    const char *name;		/* element name for the XML report */
    int found;			/* Check the name is found or not */

    struct dav_repos_report_elem *next;
} dav_repos_report_elem;


void send_xml( apr_bucket_brigade * bb, ap_filter_t * output, const char *str );

#endif
