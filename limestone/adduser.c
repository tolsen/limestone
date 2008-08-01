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

#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ne_alloc.h>
#include <ne_request.h>
#include <ne_session.h>
#include <ne_string.h>
#include <ne_uri.h>

#define PRODUCT "LimeStone_create_user"
#define VERSION "0.1"

#define AMP_ESCAPED "&amp;"
#define LT_ESCAPED "&lt;"
#define GT_ESCAPED "&gt;"

#include "root_path.h"
#define USER_PATH PREPEND_ROOT_PATH("/users/")

/* converts string to non-negative integer. returns -1 if there's an
   error during parsing */
static int atonni(const char *str)
{
    long value = -1;
    char *endptr;

    value = strtol(str, &endptr, 10);

    if ( (endptr - str != strlen(str)) || value < 0 || value > INT_MAX )
        return -1;

    return (int) value;
}
         
static void print_help(const char *exec_name)
{
    fprintf(stderr,
            "usage: %s -p password [OPTION] username\n"
            "Create a user on a LimeStone server.\n\n"
            "Mandatory arguments to long options are mandatory for short options too.\n"
            "   -p, --password=STRING\tuser password (required)\n"
            "   -d, --displayname=STRING\tuser's display name\n"
            "   -h, --host=HOSTNAME\tLimeStone server's hostname (default: localhost)\n"
            "   -P, --port=NUM\tport that LimeStone server listens on (default: 8080)\n"
            "       --help\tdisplay this help and exit\n",
            exec_name);
    return;
}

static void append_escaped_text(ne_buffer *buf, const char *escaped, int *altered)
{
    if (*altered)
        ne_buffer_altered(buf);

    ne_buffer_zappend(buf, escaped);

    *altered = 0;
}

static char *escape_xml_text(const char *text)
{
    ne_buffer *buf = ne_buffer_create();

    const char *psrc = text;
    char *pdest;
    int altered = 0;

    while (*psrc != '\0') {
        switch(*psrc) {
        case '&':
            append_escaped_text(buf, AMP_ESCAPED, &altered);
            break;
        case '<':
            append_escaped_text(buf, LT_ESCAPED, &altered);
            break;
        case '>':
            append_escaped_text(buf, GT_ESCAPED, &altered);
            break;
        default:
            if (!altered)
                pdest = &(buf->data[buf->used - 1]);
            *(pdest++) = *psrc;
            altered = 1;
            break;
        }
        psrc++;
    }

    if (altered) {
        *pdest = '\0';
        ne_buffer_altered(buf);
    }

    return ne_buffer_finish(buf);
}

char *new_user_put_body(const char *name, const char *password, const char *displayname)
{
    char *escaped_name = escape_xml_text(name);
    char *escaped_password = escape_xml_text(password);
    ne_buffer *buf = ne_buffer_create();

#ifdef DEBUG
    printf("new_user_put_body\n%s\n%s\n%s\n%s\n", name, password, escaped_name, escaped_password);
#endif

    ne_buffer_concat(buf,
                     "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
                     "<lb:user xmlns:lb='http://limebits.com/ns/1.0/' xmlns:D='DAV:'>\n"
                     "  <lb:name>", escaped_name, "</lb:name>\n"
                     "  <lb:password>", escaped_password, "</lb:password>\n",
                     "  <lb:email>", "anon@anon.com", "</lb:email>\n",
                     NULL);

    free(escaped_name);
    free(escaped_password);

    if (displayname) {
        char *escaped_displayname = escape_xml_text(displayname);
        ne_buffer_concat(buf,
                         "  <D:displayname>", escaped_displayname, "</D:displayname>\n",
                         NULL);

        free(escaped_displayname);
    }

    ne_buffer_zappend(buf, "</lb:user>\n");

    return ne_buffer_finish(buf);
}

char *user_path(const char *name)
{
    char *escaped_path;
    ne_buffer *buf = ne_buffer_create();
    ne_buffer_concat(buf, USER_PATH, name, NULL);

    escaped_path = ne_path_escape(buf->data);
    ne_buffer_destroy(buf);
    return escaped_path;
}

int main(int argc, char** argv)
{
    char *exec_name = strdup(argv[0]);
    
    int c;

    char *name = NULL;
    char *displayname = NULL;
    char *password = NULL;
  
    char *host = "localhost";
    int port = 8080;

    char *req_body;
    char *path;
    ne_session *session;
    ne_request *request;
    const ne_status *status;
    int success;
    FILE *result_stream;

    enum { HELP = CHAR_MAX + 1 };

    while(1) {
        static struct option long_options[] =
          {
              { "displayname", required_argument, 0, 'd' },
              { "host",        required_argument, 0, 'h' },
              { "password",    required_argument, 0, 'p' },
              { "port",        required_argument, 0, 'P' },
              { "help",        no_argument,       0, HELP },
              { 0, 0, 0, 0 }
          };

        int option_index = 0;

        c = getopt_long(argc, argv, "d:h:p:P:", long_options, &option_index);

        if (c == -1) break;

        switch(c) {
      
        case 'd':
            displayname = optarg;
            break;
      
        case 'h':
            host = optarg;
            break;

        case 'p':
            password = optarg;
            break;

        case 'P':
            port = atonni(optarg);
            if (port < 0) {
                fprintf(stderr, "invalid port: %s\n", optarg);
                exit(1);
            }
            break;

        case HELP:
            print_help(exec_name);
            return 0;
            break;

        case '?':
            print_help(exec_name);
            exit(1);
            break;

        default:
            abort();
            break;

        }
    }

    /* password is required */
    if ((optind != argc - 1) || (password == NULL)) {
        print_help(exec_name);
        exit(1);
    }

    name = argv[optind];
    
    req_body = new_user_put_body(name, password, displayname);
    path = user_path(name);

    session = ne_session_create("http", host, port);
    ne_set_useragent(session, PRODUCT "/" VERSION);

    request = ne_request_create(session, "PUT", path);
    ne_add_request_header(request, "If-None-Match", "*");
    ne_set_request_body_buffer(request, req_body, strlen(req_body));

#ifdef DEBUG
    printf("PUT %s\n\nBody:\n%s\n", path, req_body);
#endif

    if (ne_request_dispatch(request)) {
        success = 0;
        fprintf(stderr, "Request failed: %s\n", ne_get_error(session));
    } else {
        status = ne_get_status(request);
        success = (status->klass == 2);
        result_stream = success ? stdout : stderr;
        fprintf(result_stream, "%d %s\n", status->code, status->reason_phrase);
    }

    /* clean up */
    ne_request_destroy(request);
    ne_session_destroy(session);
    free(path);
    free(req_body);

    return !success;
}


      
      
