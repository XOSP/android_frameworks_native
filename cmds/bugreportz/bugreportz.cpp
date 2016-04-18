/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cutils/properties.h>
#include <cutils/sockets.h>

static constexpr char VERSION[] = "1.0";

static void show_usage() {
  fprintf(stderr,
          "usage: bugreportz [-h | -v]\n"
          "  -h: to display this help message\n"
          "  -v: to display the version\n"
          "  or no arguments to generate a zipped bugreport\n");
}

static void show_version() {
  fprintf(stderr, "%s\n", VERSION);
}

int main(int argc, char *argv[]) {

    if (argc > 1) {
        /* parse arguments */
        int c;
        while ((c = getopt(argc, argv, "vh")) != -1) {
            switch (c) {
                case 'h':
                    show_usage();
                    return EXIT_SUCCESS;
                case 'v':
                    show_version();
                    return EXIT_SUCCESS;
                default:
                    show_usage();
                    return EXIT_FAILURE;
            }
        }
        // passed an argument not starting with -
        if (optind > 1 || argv[optind] != nullptr) {
            show_usage();
            return EXIT_FAILURE;
        }
    }

    // TODO: code below was copy-and-pasted from bugreport.cpp (except by the timeout value);
    // should be reused instead.

    // Start the dumpstatez service.
    property_set("ctl.start", "dumpstatez");

    // Socket will not be available until service starts.
    int s;
    for (int i = 0; i < 20; i++) {
        s = socket_local_client("dumpstate", ANDROID_SOCKET_NAMESPACE_RESERVED, SOCK_STREAM);
        if (s >= 0)
            break;
        // Try again in 1 second.
        sleep(1);
    }

    if (s == -1) {
        printf("Failed to connect to dumpstatez service: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    // Set a timeout so that if nothing is read in 10 minutes, we'll stop
    // reading and quit. No timeout in dumpstate is longer than 60 seconds,
    // so this gives lots of leeway in case of unforeseen time outs.
    struct timeval tv;
    tv.tv_sec = 10 * 60;
    tv.tv_usec = 0;
    if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == -1) {
        printf("WARNING: Cannot set socket timeout: %s\n", strerror(errno));
    }

    while (1) {
        char buffer[65536];
        ssize_t bytes_read = TEMP_FAILURE_RETRY(
                read(s, buffer, sizeof(buffer)));
        if (bytes_read == 0) {
            break;
        } else if (bytes_read == -1) {
            // EAGAIN really means time out, so change the errno.
            if (errno == EAGAIN) {
                errno = ETIMEDOUT;
            }
            printf("\nBugreport read terminated abnormally (%s).\n",
                    strerror(errno));
            break;
        }

        ssize_t bytes_to_send = bytes_read;
        ssize_t bytes_written;
        do {
            bytes_written = TEMP_FAILURE_RETRY(
                    write(STDOUT_FILENO, buffer + bytes_read - bytes_to_send,
                            bytes_to_send));
            if (bytes_written == -1) {
                printf(
                        "Failed to write data to stdout: read %zd, trying to send %zd (%s)\n",
                        bytes_read, bytes_to_send, strerror(errno));
                return EXIT_FAILURE;
            }
            bytes_to_send -= bytes_written;
        } while (bytes_written != 0 && bytes_to_send > 0);
    }

    close(s);
    return EXIT_SUCCESS;
}
