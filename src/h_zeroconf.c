/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * distcc -- A simple distributed compiler system
 *
 * Copyright (C) 2011 by Ihor Kaharlichenko <madkinder@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 */

#include <config.h>

/*
#include <stdio.h>
#include <assert.h>
#include <time.h>
*/
#include <stdlib.h>
#include <string.h>

#include <avahi-client/client.h>
#include <avahi-client/lookup.h>

#include <avahi-common/simple-watch.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>

#include "distcc.h"
#include "trace.h"
#include "util.h"
#include "zeroconf.h"


#define USAGE \
"usage: h_zeroconf\n"

const char *rs_program_name = __FILE__;

static AvahiSimplePoll *simple_poll = NULL;

static unsigned int hosts_pending = 0U;
static int no_more_hosts = 0;

static void resolve_callback(
    AvahiServiceResolver *r,
    AvahiIfIndex UNUSED(interface),
    AvahiProtocol UNUSED(protocol),
    AvahiResolverEvent event,
    const char *UNUSED(name),
    const char *UNUSED(type),
    const char *UNUSED(domain),
    const char *UNUSED(host_name),
    const AvahiAddress *address,
    uint16_t port,
    AvahiStringList *txt,
    AvahiLookupResultFlags flags,
    void *UNUSED(userdata)) {

    if (event == AVAHI_RESOLVER_FOUND) {
        if (flags & AVAHI_LOOKUP_RESULT_LOCAL) {
            char a[AVAHI_ADDRESS_STR_MAX];
            char * cc_machine = 0;
            char * cc_version = 0;
            char * gnuhost = 0;
            char * options = 0;
            int cpus = -1;
            AvahiStringList *i;

            avahi_address_snprint(a, sizeof(a), address);

            for (i = txt; i; i = i->next) {
                char *key, *value;

                if (avahi_string_list_get_pair(i, &key, &value, NULL) < 0)
                    continue;

                if (!strcmp(key, "cpus")) {
                    cpus = atoi(value);
                    avahi_free(value);
                } else if (!strcmp(key, "cc_machine")) {
                    cc_machine = value;
                } else if (!strcmp(key, "cc_version")) {
                    cc_version = value;
                } else if (!strcmp(key, "gnuhost")) {
                    gnuhost = value;
                } else if (!strcmp(key, "options")) {
                    options = value;
                }

                avahi_free(key);
            }

            printf("%s:%u %s %s %s %s %d\n",
                    a, port, cc_machine, cc_version, gnuhost, options, cpus);

            if (options)
                avahi_free(options);
            if (cc_version)
                avahi_free(cc_version);
            if (cc_machine)
                avahi_free(cc_machine);
            if (gnuhost)
                avahi_free(gnuhost);
        }
    }

    avahi_service_resolver_free(r);

    hosts_pending--;
    if (no_more_hosts && hosts_pending == 0)
        avahi_simple_poll_quit(simple_poll);
}

static void browse_callback(
    AvahiServiceBrowser *b,
    AvahiIfIndex interface,
    AvahiProtocol protocol,
    AvahiBrowserEvent event,
    const char *name,
    const char *type,
    const char *domain,
    AvahiLookupResultFlags UNUSED(flags),
    void* userdata) {

    AvahiClient *c = (AvahiClient *)userdata;

    switch (event) {
        case AVAHI_BROWSER_NEW:
            if (!(avahi_service_resolver_new(c, interface, protocol, name, type, domain, AVAHI_PROTO_UNSPEC, 0, resolve_callback, c)))
                rs_log_error("Failed to resolve service '%s': %s\n", name, avahi_strerror(avahi_client_errno(c)));
            else
                hosts_pending++;
            break;

        case AVAHI_BROWSER_FAILURE:
            rs_log_error("Failed to browse services: %s\n", avahi_strerror(avahi_client_errno(avahi_service_browser_get_client(b))));
            avahi_simple_poll_quit(simple_poll);
            break;
        case AVAHI_BROWSER_ALL_FOR_NOW:
        case AVAHI_BROWSER_CACHE_EXHAUSTED:
            if (hosts_pending)
                no_more_hosts = 1;
            else
                avahi_simple_poll_quit(simple_poll);
            break;
        case AVAHI_BROWSER_REMOVE:
        default:
            break;
    }
}

int main(int UNUSED(argc), char ** UNUSED(argv)) {
    AvahiClient *client = NULL;
    AvahiServiceBrowser *service_browser = NULL;
    int error;
    int ret = 1;

    rs_trace_set_level(RS_LOG_WARNING);

    if (!(simple_poll = avahi_simple_poll_new())) {
        rs_log_error("Failed to create simple poll object.\n");
        goto fail;
    }

    client = avahi_client_new(avahi_simple_poll_get(simple_poll), 0, NULL, NULL, &error);

    if (!client) {
        rs_log_error("Failed to create client: %s\n.", avahi_strerror(error));
        goto fail;
    }

    service_browser = avahi_service_browser_new(client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, DCC_DNS_SERVICE_TYPE, NULL, 0, browse_callback, client);
    if (!service_browser) {
        rs_log_error("Failed to create service browser: %s\n.", avahi_strerror(avahi_client_errno(client)));
        goto fail;
    }

    avahi_simple_poll_loop(simple_poll);

    ret = 0;

fail:

    if (service_browser)
        avahi_service_browser_free(service_browser);

    if (client)
        avahi_client_free(client);

    if (simple_poll)
        avahi_simple_poll_free(simple_poll);

    return ret;
}
