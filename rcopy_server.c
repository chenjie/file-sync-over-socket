#include <stdio.h>
#include <unistd.h>
#include <libgen.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdlib.h>

#include "ftree.h"

#ifndef PORT
  #define PORT 30000
#endif

int main(int argc, char **argv) {

    if(argc != 2) {
        printf("Usage: rcopy_server PATH_PREFIX\n");
        printf("\t PATH_PREFIX - The path on the server used as the path prefix for the destination\n");
        exit(1);
    }
    /* NOTE:  The directory PATH_PREFIX/sandbox/dest will be the directory in
     * which the source files and directories will be copied.  It therefore
	 * needs rwx permissions.  The directory PATH_PREFIX/sandbox will have
	 * write and execute permissions removed to prevent clients from trying
	 * to create files and directories above the dest directory.
     */

    // create the sandbox directory
    char path[MAXPATH];
    strncpy(path, argv[1], MAXPATH);
    strncat(path, "/", MAXPATH - strlen(path) + 1);
    strncat(path, "sandbox", MAXPATH - strlen(path) + 1);

    if(mkdir(path, 0700) == -1){
        if(errno != EEXIST) {
            fprintf(stderr, "couldn't open %s\n", path);
            perror("mkdir");
            exit(1);
        }
    }

    // create the dest directory
    strncat(path, "/", MAXPATH - strlen(path) + 1);
    strncat(path, "dest", MAXPATH - strlen(path) + 1);
    if(mkdir(path, 0700) == -1){
        if(errno != EEXIST) {
            fprintf(stderr, "couldn't open %s\n", path);
            perror("mkdir");
            exit(1);
        }
    }

    // change into the dest directory.
    chdir(path);

    // remove write and access perissions for sandbox
    if(chmod("..", 0400) == -1) {
        perror("chmod");
        exit(1);
    }

    /* IMPORTANT: All path operations in rcopy_server must be relative to
     * the current working directory.
     */
    rcopy_server(PORT);

    // Should never get here!
    fprintf(stderr, "Server reached exit point.");
    return 1;
}
