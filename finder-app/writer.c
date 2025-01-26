/******************************************************************************
 * writer.c
 *
 * Usage: writer <writefile> <writestr>
 *
 * Author: Matt Hartnett
 *
 * 1) Creates or overwrites the file <writefile> with the contents of <writestr>.
 * 2) Logs actions using syslog with LOG_USER facility.
 * 3) On error, prints an error message, logs the error, and returns with exit code 1.
 *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

int main(int argc, char *argv[])
{
    /* Open a connection to syslog */
    openlog("writer", LOG_CONS | LOG_PID | LOG_NDELAY, LOG_USER);

    /* Check for the correct number of arguments */
    if (argc != 3) {
        fprintf(stderr, "Error: Invalid number of arguments.\n");
        syslog(LOG_ERR, "Invalid number of arguments. Expected 2, got %d", argc - 1);
        closelog();
        exit(1);
    }

    const char *writefile = argv[1];
    const char *writestr = argv[2];

    /* Attempt to create or overwrite the file */
    FILE *fp = fopen(writefile, "w");
    if (fp == NULL) {
        fprintf(stderr, "Error: Cannot open file %s for writing. Error: %s\n",
                writefile, strerror(errno));
        syslog(LOG_ERR, "Failed to open file %s: %s", writefile, strerror(errno));
        closelog();
        exit(1);
    }

    /* Write the string to the file */
    if (fputs(writestr, fp) == EOF) {
        fprintf(stderr, "Error: Failed to write to file %s. Error: %s\n",
                writefile, strerror(errno));
        syslog(LOG_ERR, "Failed to write to file %s: %s", writefile, strerror(errno));
        fclose(fp);
        closelog();
        exit(1);
    }

    /* Log debug message */
    syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);

    /* Clean up */
    fclose(fp);
    closelog();

    return 0;
}
