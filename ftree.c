#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <libgen.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "ftree.h"

#define MAX_BACKLOG 5
// #define ENABLE_DEBUG_LOG

#ifdef ENABLE_DEBUG_LOG
#define D(...)  {printf(__VA_ARGS__);}
#else
#define D(...) // nothing
#endif

/*
 * This function takes string of a path and a file/dir name as inputs,
 * and adds name to the end of the path, and then returns.
 */
char* generate_path(const char *path, char *name) {
    char *result;
    if (path[strlen(path) - 1] != '/') {
        result = malloc(strlen(path) + strlen(name) + 2);
        strcpy(result, path);
        strcat(result, "/");
        strcat(result, name);
    } else {
        result = malloc(strlen(path) + strlen(name) + 1);
        strcpy(result, path);
        strcat(result, name);
    }
    return result;
}

/*
 * This function takes string of source path and a string of host or
 * a unsigned short of port to intialize a client to synchronize files
 * with a server.
 */
char *str_parent; /* initialize a static string to store basename of source */

int rcopy_client(char *source, char *host, unsigned short port) {
    static int i = 0; /* counter of numbers of iteration */
    int num_child_process = 0; /* total number of child processes */
    int neg_flag = 0; /* error indicator */

    // Check if the file exits.
    struct stat stat_src;
    if (lstat(source, &stat_src) == -1) {
        perror("client: lstat");
        exit(1);
    }

    // Get absolute path.
    char abs_src[MAXPATH];
    realpath(source, abs_src);

    // Intialize components for socket connection.
    static int sock_fd;
    struct sockaddr_in server;
    struct hostent *hp;

    // First, set up socket connection.
    // Note: only establish once at 1st iteration.
    if (i == 0) {
        // Set up basename of source.
        str_parent = malloc(MAXPATH);
        strcpy(str_parent, basename(abs_src));

        // Get hostname.
        if ((hp = gethostbyname(host)) == NULL) {
            perror("client: gethostbyname");
            exit(1);
        }

        // Get file discriptor of socket.
        if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
            perror("client: socket");
            exit(1);
        }

        // Set the IP and port of the server to connect to.
        server.sin_family = AF_INET;
        server.sin_port = htons(port);
        server.sin_addr = *((struct in_addr *)hp->h_addr);

        // Connect to the server.
        if (connect(sock_fd, (struct sockaddr *)&server, sizeof(server)) == -1) {
            perror("client: connect");
            close(sock_fd);
            exit(1);
        }
        printf("Socket connection established.\n");
    }

    // Next, construct request struct.
    struct request req_src;

    // Ignore LINKS.
    if (S_ISLNK(stat_src.st_mode)) {
        return 0;

    // Construct fields of request struct for REGULAR FILE.
    } else if (S_ISREG(stat_src.st_mode)) {
        req_src.type = htonl(REGFILE); /* Type */
        strcpy(req_src.path, strstr(source, str_parent)); /* Path */
        req_src.mode = stat_src.st_mode; /* Mode */
        char blank[BLOCKSIZE]; /* Hash */
        char *hash_val;
        FILE *f = fopen(source, "rb");
        if (f == NULL) {
            if (errno == EACCES) {
                // try to upload a non-readable file
                perror("client: fopen");
                fprintf(stderr, "ERROR: %s\n", req_src.path);
                return 1;
            } else {
                perror("client: fopen");
                exit(1);
            }
        }
        // Compute hash of source file.
        hash_val = hash(blank, f);
        for (int i = 0; i < 8; i++) {
            req_src.hash[i] = hash_val[i];
        }
        if (fclose(f) == EOF) {
            perror("client: fopen");
            exit(1);
        }
        req_src.size = htonl(stat_src.st_size); /* Size */

    // Construct fields of request struct for DIRECTORY
    } else if (S_ISDIR(stat_src.st_mode)) {
        req_src.type = htonl(REGDIR); /* Type */

        char abs_path[MAXPATH]; /* Path */
        realpath(source, abs_path);
        strcpy(req_src.path, strstr(abs_path, str_parent));

        req_src.mode = stat_src.st_mode; /* Mode */

        /*
         * Hash
         * hash of a dir should be NULL, but NULL content cannot be pass
         * through socket. To make it work desirely, we make them all \0.
         */
        for(int i = 0; i < 8; i++) {
            req_src.hash[i] = '\0';
        }
        req_src.size = htonl(stat_src.st_size); /* Size */

    } else {
        // shouldn't get here.
        fprintf(stderr, "File type error!\n");
        exit(1);
    }

    // Then upload every field of this struct one at a time to the server.
    // The order is: type -> path -> mode -> hash -> size.
    if (write(sock_fd, &(req_src.type), sizeof(int)) == -1) {
        perror("client: write");
        close(sock_fd);
        exit(1);
    }
    if (write(sock_fd, req_src.path, MAXPATH) == -1) {
        perror("client: write");
        close(sock_fd);
        exit(1);
    }
    if (write(sock_fd, &(req_src.mode), 4) == -1) {
        perror("client: write");
        close(sock_fd);
        exit(1);
    }
    if (write(sock_fd, req_src.hash, BLOCKSIZE) == -1) {
        perror("client: write");
        close(sock_fd);
        exit(1);
    }
    if (write(sock_fd, &(req_src.size), sizeof(int)) == -1) {
        perror("client: write");
        close(sock_fd);
        exit(1);
    }

    // After, wait for response from the server.
    int tmp, rec_int;
    if (read(sock_fd, &tmp, sizeof(int)) == -1) {
        perror("client: read");
        close(sock_fd);
        exit(1);
    }
    rec_int = ntohl(tmp);

    // if server responds ERROR: report error then terminate.
    if (rec_int == ERROR) {
        fprintf(stderr, "ERROR: %s\n", req_src.path);
        return 1;

    // if server responds OK, current file identical with server.
    } else if (rec_int == OK) {
        // REGULAR FILE: file identical with server, terminate without error.
        if (S_ISREG(stat_src.st_mode)) {
            return 0;
        //DIRECTORY: Go through this directory.
        } else if (S_ISDIR(stat_src.st_mode)) {
            DIR *src_dirp = opendir(source);
            // if no read permission, error.
            if (src_dirp == NULL) {
                perror("client: opendir");
                fprintf(stderr, "ERROR: %s\n", req_src.path);
                return 1;
            }
            struct dirent *dp = readdir(src_dirp);
            char *src_child;
            while (dp != NULL){
                // Ignore the file named '.' at the beginning.
                if ((*dp).d_name[0] != '.'){
                    src_child = generate_path(abs_src, (*dp).d_name);
                    // Get information of child.
                    struct stat stat_src_child;
                    if (lstat(src_child, &stat_src_child) == -1) {
                        perror("client: lstat");
                        return 1;
                    }
                    // If child is a DIRECTORY.
                    if (S_ISDIR(stat_src_child.st_mode)) {
                        i++;
                        int re = rcopy_client(src_child, host, port);
                        // we free the memory that we have malloc'ed.
                        free(src_child);
                        if (re != 0) {
                            neg_flag = 1;
                        }
                    // If child is a REGULAR FILE.
                    } else if (S_ISREG(stat_src_child.st_mode)) {
                        i++;
                        int re = rcopy_client(src_child, host, port);
                        // we free the memory that we have malloc'ed.
                        free(src_child);
                        if (re != 0) {
                            neg_flag = 1;
                        }
                    // If child is a LINK.
                    } else if (S_ISLNK(stat_src_child.st_mode)) {
                        // Ignore links.
                    // If child is in strange type.
                    } else {
                        // shouldn't get here.
                        return 1;
                    }
                }
                // Move on to next child in directory.
                dp = readdir(src_dirp);
            }
        // File is in strange file type.
        } else {
            fprintf(stderr, "File type error!\n");
            exit(1);
        }

    // if server responds SENDFILE, file data are different, file needs to be send.
    } else if (rec_int == SENDFILE) {
        // Create new process to send file data.
        pid_t pid = fork();

        //CHILD process sends file data to server.
        if (pid == 0) {
            // First, set up new socket connection.
            int child_sock_fd;
            struct sockaddr_in child_server;
            struct hostent *child_hp;
            if ((child_hp = gethostbyname(host)) == NULL) {
                perror("child_client: gethostbyname");
                exit(1);
            }
            if ((child_sock_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
                perror("child_client: socket");
                exit(1);
            }

            // Set the IP and port of the server to connect to.
            child_server.sin_family = AF_INET;
            child_server.sin_port = htons(port);
            child_server.sin_addr = *((struct in_addr *)child_hp->h_addr);

            // Connect to the server.
            if (connect(child_sock_fd, (struct sockaddr *)&child_server, sizeof(child_server)) == -1) {
                perror("child_client: connect");
                close(child_sock_fd);
                exit(1);
            }

            // Next, identifies as TRANSFILE client and send request struct.
            struct request child_req_src = req_src;
            child_req_src.type = htonl(TRANSFILE);
            // Then upload every field of this struct one at a time to the server.
            // The order is: type -> path -> mode -> hash -> size.
            if (write(child_sock_fd, &(child_req_src.type), sizeof(int)) == -1) {
                perror("child_client: write");
                close(child_sock_fd);
                exit(1);
            }
            if (write(child_sock_fd, child_req_src.path, MAXPATH) == -1) {
                perror("child_client: write");
                close(child_sock_fd);
                exit(1);
            }
            if (write(child_sock_fd, &(child_req_src.mode), 4) == -1) {
                perror("child_client: write");
                close(child_sock_fd);
                exit(1);
            }
            if (write(child_sock_fd, child_req_src.hash, BLOCKSIZE) == -1) {
                perror("child_client: write");
                close(child_sock_fd);
                exit(1);
            }
            if (write(child_sock_fd, &(child_req_src.size), sizeof(int)) == -1) {
                perror("child_client: write");
                close(child_sock_fd);
                exit(1);
            }

            // Transmit data without waiting.
            FILE *src = fopen(source, "rb");
            char buffer[MAXDATA];
            int byte;
            while ((byte = fread(buffer, 1, MAXDATA, src)) != 0) {
                if (write(child_sock_fd, buffer, byte) == -1) {
                    return 1;
                }
            }
            if (fclose(src) != 0){
              perror("client: fclose");
              return 1;
            }

            // Then wait for server's message.
            int child_tmp, child_rec_int;
            if (read(child_sock_fd, &child_tmp, sizeof(int)) == -1) {
                perror("child_client: read");
                close(child_sock_fd);
                exit(1);
            }
            child_rec_int = ntohl(child_tmp);

            // if data transmit successes, end connection with server, terminate child process.
            if (child_rec_int == OK) {
                close(child_sock_fd);
                exit(0);

            // if data transmit proccess encounters ERROR, report error then terminates.
            } else {
                // upload file to a non-writable dir.
                fprintf(stderr, "child_client permission ERROR!\n");
                fprintf(stderr, "ERROR: %s\n", child_req_src.path);
                exit(1);
            }
        // PARENT process increments process count.
        } else {
            num_child_process++;
        }
    // if server responds strange message.
    } else {
        fprintf(stderr, "ERROR int received from the sever!\n");
        exit(1);
    }
    // Finally, wait for all children.
    for (int i = 1; i <= num_child_process; i++) {
        int tmp;
        char rvalue = 0;
        if (wait(&tmp) == -1) { // Waiting...
            perror("client: wait");
            exit(1);
        } else {
            if(WIFEXITED(tmp)) {
                rvalue = WEXITSTATUS(tmp);
            } else {
                return 1;
            }
        }
        // if child processes entoured error, notify error indicator.
        if (rvalue == 1){
            neg_flag = 1;
        }
    }
    return neg_flag;
}

// ================ client part ends ================



// ================ server part starts ================

/*
 * This function takes a file descriptor fd, a destination pointer dest, a size
 * and a fd_set pointer set as inputs, and read data from fd and store it into
 * dest. If it succeeds return 0, otherwise return 1.
 */
int read_struct_field(int fd, void *dest, int size, fd_set *set) {
    int n;
    if ((n = read(fd, dest, size)) == -1) {
        perror("server: read");
        close(fd);
        FD_CLR(fd, set);
        return 1;
    } else if (n == 0) {
        printf("CLIENT [%d] HAS DISCONNECTED.\n", fd);
        close(fd);
        FD_CLR(fd, set);
        return 1;
    }
    D("BYTES RECEIVED [%d]\n", n);
    return 0;
}

/*
 * This function takes a file descriptor fd and a int representing message as
 * inputs, then it converts the message into network byte order and sends a
 * response to the client.
 */
void respond(int fd, int message) {
    int response = htonl(message);
    D("RESPONSE: %d\n", message);
    if (write(fd, &response, sizeof(int)) == -1) {
        perror("server: write");
    }
}

/*
 * This function takes an unsigned short representing port number as
 * input, then it accepts the connection of its clients and synchronize
 * the data.
 */
void rcopy_server(unsigned short port) {
    // Create the socket FD.
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("server: socket");
        exit(1);
    }

    // Set information about the port (and IP) we want to be connected to.
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = INADDR_ANY;

    // This should always be zero. On some systems, it won't error if you
    // forget, but on others, you'll get mysterious errors. So zero it.
    memset(&server.sin_zero, 0, 8);

    // Bind the selected port to the socket.
    if (bind(sock_fd, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("server: bind");
        close(sock_fd);
        exit(1);
    }

    // Announce willingness to accept connections on this socket.
    if (listen(sock_fd, MAX_BACKLOG) < 0) {
        perror("server: listen");
        close(sock_fd);
        exit(1);
    }

    // First, we prepare to listen to multiple
    // file descriptors by initializing a set of file descriptors.
    int max_fd = sock_fd;
    fd_set all_fds, listen_fds;
    FD_ZERO(&all_fds);
    FD_SET(sock_fd, &all_fds);

    // Keep track of the status of each file descriptor.
    // We assume the maximum file descriptor is 1024.
    int state_of_client[1024] = {0};
    int client_data_left[1024] = {0};
    struct request struct_arr[1024];

    // The server process goes into an infinite loop.
    while (1) {
        // Select updates the fd_set it receives,
        // so we always use a copy and retain the original.
        listen_fds = all_fds;
        int nready = select(max_fd + 1, &listen_fds, NULL, NULL, NULL);
        if (nready == -1) {
            perror("server: select");
            exit(1);
        }
        // Is it the original socket? Create a new connection ...
        if (FD_ISSET(sock_fd, &listen_fds)) {
            int client_fd = accept(sock_fd, NULL, NULL);
            if (client_fd < 0) {
                perror("server: accept");
                close(sock_fd);
                exit(1);
            }
            // Update the maximum file descriptor.
            if (client_fd > max_fd) {
                max_fd = client_fd;
            }
            FD_SET(client_fd, &all_fds);
            D("Accepted connection\n");
        }

        // After each select call, loop over file descriptors that are ready to read.
        for (int fd = 3; fd <= max_fd; fd++) {
            if ((fd != sock_fd) && FD_ISSET(fd, &listen_fds)) {
                // Note: never reduces max_fd

                // We do this to prevent two connections changing one
                // struct simultaneously, which causes segfault.
                struct request *ser_rec = &struct_arr[fd];

                // Split into cases, and read each field of the struct.
                if (state_of_client[fd] == AWAITING_TYPE) {
                    if (read_struct_field(fd, &(ser_rec->type), sizeof(int), &all_fds)) {
                        state_of_client[fd] = AWAITING_TYPE;
                        continue;
                    }
                    ser_rec->type = ntohl(ser_rec->type);
                    state_of_client[fd] = AWAITING_PATH;
                    continue;
                } else if (state_of_client[fd] == AWAITING_PATH) {
                    if (read_struct_field(fd, ser_rec->path, MAXPATH, &all_fds)) {
                        state_of_client[fd] = AWAITING_TYPE;
                        continue;
                    }
                    state_of_client[fd] = AWAITING_PERM;
                    continue;
                } else if (state_of_client[fd] == AWAITING_PERM) {
                    if (read_struct_field(fd, &(ser_rec->mode), 4, &all_fds)) {
                        state_of_client[fd] = AWAITING_TYPE;
                        continue;
                    }
                    state_of_client[fd] = AWAITING_HASH;
                    continue;
                } else if (state_of_client[fd] == AWAITING_HASH) {
                    if (read_struct_field(fd, ser_rec->hash, BLOCKSIZE, &all_fds)) {
                        state_of_client[fd] = AWAITING_TYPE;
                        continue;
                    }
                    state_of_client[fd] = AWAITING_SIZE;
                    continue;
                } else if (state_of_client[fd] == AWAITING_SIZE) {
                    // This is the last part of the struct that we read.
                    if (read_struct_field(fd, &(ser_rec->size), sizeof(int), &all_fds)) {
                        state_of_client[fd] = AWAITING_TYPE;
                        continue;
                    }
                    ser_rec->size = ntohl(ser_rec->size);
                    state_of_client[fd] = AWAITING_TYPE;

                    D("%d\n", ser_rec->type);
                    // printf("%s\n", ser_rec->path);
                    D("%o\n", (ser_rec->mode) & 0777);
                    for (int i = 0; i < 8; i++) {
                        D("%hhx ", ser_rec->hash[i]);
                    }
                    D("\n%d\n", ser_rec->size);


                    client_data_left[fd] = ser_rec->size;
                    // We have received the whole struct.
                    if (ser_rec->type == REGFILE) {
                        // Try to open it.
                        FILE *f = fopen(ser_rec->path, "rb");
                        if (f == NULL) { // If an error occurs when we try to open this file.
                            if (errno == ENOENT) { // If the file doesn't exist.
                                respond(fd, SENDFILE);
                                continue;
                            } else {
                                perror("server: fopen");
                                fprintf(stderr, "ERROR: %s\n", ser_rec->path);
                                respond(fd, ERROR);
                                continue;
                            }
                        } else { // If NO error occurs when we try to open this file.
                            // This means that this file exits and we have already opened it.
                            struct stat stat_file;
                            lstat(ser_rec->path, &stat_file);
                            // If this is not a file, this means there is a mismatch.
                            if (!S_ISREG(stat_file.st_mode)) {
                                if (fclose(f) == EOF) {
                                    perror("server: fclose");
                                    continue;
                                }
                                fprintf(stderr, "NOT A FILE: %s\n", ser_rec->path);
                                // Mismatch, try to change the permission.
                                if (chmod(ser_rec->path, (ser_rec->mode) & 0777) == -1) {
                                    perror("server: chmod");
                                    respond(fd, ERROR);
                                    continue;
                                }
                                respond(fd, ERROR);
                                continue;
                            }
                            // If there is no mismatch.
                            // First check if their sizes are different.
                            if (ser_rec->size != stat_file.st_size) {
                                // If sizes are different, copy the file.
                                if (fclose(f) == EOF) {
                                    perror("server: fclose");
                                    continue;
                                }
                                // Send a SENDFILE message to the client.
                                respond(fd, SENDFILE);
                                continue;

                            } else { // If sizes are the same, we check hash and permission.
                                char blank[BLOCKSIZE];
                                char *hash_dest = hash(blank, f);
                                if (check_hash(ser_rec->hash, hash_dest) == 0) {
                                    if (fclose(f) == EOF) {
                                        perror("server: fclose");
                                        continue;
                                    }
                                    // If hash is same, then check permission.
                                    if (((stat_file.st_mode) & 0777) != ((ser_rec->mode) & 0777)) {
                                        if (chmod(ser_rec->path, (ser_rec->mode) & 0777) == -1) {
                                            perror("server: chmod");
                                            respond(fd, ERROR);
                                            continue;
                                        }
                                    }
                                    respond(fd, OK);
                                    continue;
                                } else {
                                    if (fclose(f) == EOF) {
                                        perror("fclose");
                                        continue;
                                    }
                                    // If hash is different, then copy the file.
                                    respond(fd, SENDFILE);
                                    continue;
                                }
                            }
                        }

                    // If the struct that we received is a directory.
                    } else if (ser_rec->type == REGDIR) {
                        printf("%s\n", ser_rec->path);
                        struct stat stat_dir;
                        if (lstat(ser_rec->path, &stat_dir) == -1) {
                            if (errno == ENOENT) { // If the directory doesn't exist.
                                // Make a directory, and properly set its permission.
                                if (mkdir(ser_rec->path, 0777) == -1) {
                                    perror("server: mkdir");
                                    respond(fd, ERROR);
                                    continue;
                                }
                                if (chmod(ser_rec->path, (ser_rec->mode) & 0777) == -1) {
                                    perror("server: chmod");
                                    respond(fd, ERROR);
                                    continue;
                                }
                            } else if (errno == EACCES) { // If we don't have read permission for dest directory.
                                perror("server: lstat");
                                // We change its permission if possible.
                                if (chmod(ser_rec->path, (ser_rec->mode) & 0777) == -1) {
                                    perror("server: chmod");
                                    respond(fd, ERROR);
                                    continue;
                                }
                            } else { // If due to other errors.
                                perror("server: lstat");
                                respond(fd, ERROR);
                                continue;
                            }
                            respond(fd, OK);
                            continue;
                        } else { // If NO error orrurs.
                            // Check the permission.
                            if (((stat_dir.st_mode) & 0777) != ((ser_rec->mode) & 0777)) {
                                if (chmod(ser_rec->path, (ser_rec->mode) & 0777) == -1) {
                                    perror("server: chmod");
                                    respond(fd, ERROR);
                                    continue;
                                }
                            }
                            if (S_ISDIR(stat_dir.st_mode)) {
                                respond(fd, OK);
                                continue;
                            } else {
                                // If it's not a directory, then there is a mismatch.
                                fprintf(stderr, "NOT A DIR: %s\n", ser_rec->path);
                                respond(fd, ERROR);
                                continue;
                            }

                        }

                    // If the type of struct received is TRANSFILE.
                    } else if (ser_rec->type == TRANSFILE) {
                        struct stat stat_file;
                        if (lstat(ser_rec->path, &stat_file) == 0) {
                            // If the file exits, we remove the file first.
                            if (remove(ser_rec->path) == -1) {
                                perror("server: remove");
                                respond(fd, ERROR);
                                continue;
                            }
                        }

                        // If the file doesn't exist.
                        // There is an exceptional case where the file size is 0.
                        // This means that there is no data to be transferred.
                        if (ser_rec->size == 0) {
                            // Create an empty file.
                            FILE *dest_empty_file = fopen(ser_rec->path,"w");
                            if (dest_empty_file == NULL) {
                                perror("server: fopen");
                                respond(fd, ERROR);
                                continue;
                            }
                            if (fclose(dest_empty_file) == EOF) {
                                perror("server: fopen");
                                respond(fd, ERROR);
                                continue;
                            }

                            if (chmod(ser_rec->path, (ser_rec->mode) & 0777) == -1) {
                                fprintf(stderr, "ERROR while changing empty file's permission: \n%s\n", ser_rec->path);
                                respond(fd, ERROR);
                            } else {
                                respond(fd, OK);
                            }
                            continue;

                        } else {
                            // No reply required.
                            state_of_client[fd] = AWAITING_DATA;
                            continue;

                        }

                    } else {
                        // Can't happen.
                        respond(fd, ERROR);
                        continue;
                    }
                    continue;

                } else if (state_of_client[fd] == AWAITING_DATA) {

                    char tmp[MAXDATA];
                    int bytes = read(fd, tmp, MAXDATA);
                    // Read the data into tmp.

                    if (bytes == -1) {
                        perror("server: read");
                        respond(fd, ERROR);
                        state_of_client[fd] = AWAITING_TYPE;
                        close(fd);
                        continue;
                    } else if (bytes == 0) {
                        // Bytes 0 indicates that socket is closed.
                        if (client_data_left[fd] != 0) {
                            fprintf(stderr, "ERROR!! EARLY TERMINATION.\n");
                            fprintf(stderr, "ERROR: %s\n", struct_arr[fd].path);
                            close(fd);
                            FD_CLR(fd, &all_fds);
                            state_of_client[fd] = AWAITING_TYPE;
                            continue;
                        } else {
                            fprintf(stderr, "SOMETHING STRANGE HAPPENS.\n");
                            close(fd);
                            FD_CLR(fd, &all_fds);
                            state_of_client[fd] = AWAITING_TYPE;
                            continue;
                        }
                    } else {
                        // If there is no error, open/create a file and append
                        // the data to it.
                        FILE *dest_file = fopen(ser_rec->path,"a");
                        if (dest_file == NULL) {
                            // Server get a file piece of data that under a non-writable dir.
                            perror("server: fopen");
                            fprintf(stderr, "ERROR: %s\n", ser_rec->path);
                            respond(fd, ERROR);
                            close(fd);
                            FD_CLR(fd, &all_fds);
                            state_of_client[fd] = AWAITING_TYPE;
                            continue;
                        }
                        // If no error in fopen, then we write data to it.
                        fwrite(tmp, bytes, 1, dest_file);
                        // Update the number of data left.
                        client_data_left[fd] -= bytes;
                        if (fclose(dest_file) == EOF) {
                            perror("server: fclose");
                            close(fd);
                            FD_CLR(fd, &all_fds);
                            state_of_client[fd] = AWAITING_TYPE;
                            continue;
                        }
                    }

                    // If data left is 0, i.e. file transfer is completed.
                    if (client_data_left[fd] == 0) {
                        printf("File transfer is completed!\n");
                        // Reset the state of client.
                        state_of_client[fd] = AWAITING_TYPE;
                        struct stat stat_file_received;
                        // Get the info of the file received.
                        lstat(ser_rec->path, &stat_file_received);

                        // First check if their sizes are different.
                        if (ser_rec->size != stat_file_received.st_size) {
                            // If sizes are different, we report the error.
                            fprintf(stderr, "ERROR! File received is different from the original file.\n");
                            fprintf(stderr, "ERROR: %s\n", ser_rec->path);
                            respond(fd, ERROR);

                        } else { // If sizes are the same, we check hash and permission.
                            char blank[BLOCKSIZE];
                            FILE *fm = fopen(ser_rec->path, "rb");
                            if (fm == NULL) {
                                perror("server: fopen");
                                continue;
                            }
                            // Calculate hash.
                            char *hash_dest = hash(blank, fm);
                            if (fclose(fm) == EOF) {
                                perror("server: fclose");
                                continue;
                            }
                            if (check_hash(ser_rec->hash, hash_dest) == 0) {
                                // If hash is same, then we change permission.
                                if (chmod(ser_rec->path, (ser_rec->mode) & 0777) == -1) {
                                    fprintf(stderr, "ERROR WHILE CHANGING PERMISSION: %s\n", ser_rec->path);
                                    respond(fd, ERROR);

                                } else {
                                    printf("%s\n", ser_rec->path);
                                    respond(fd, OK);
                                }

                            } else {
                                // If hashes are different, we report the error.
                                fprintf(stderr, "ERROR! File received is different from the original file.\n");
                                fprintf(stderr, "ERROR: %s\n", ser_rec->path);
                                respond(fd, ERROR);
                            }
                        }
                        close(fd);
                        FD_CLR(fd, &all_fds);

                    // If data left is less than 0, the file received should be
                    // different from the one sent from the client.
                    } else if (client_data_left[fd] < 0) {
                        // Reset the state of client.
                        state_of_client[fd] = AWAITING_TYPE;
                        // Report the error.
                        fprintf(stderr, "ERROR! File received is different from the original file.\n");
                        fprintf(stderr, "ERROR: %s\n", ser_rec->path);
                        respond(fd, ERROR);
                        close(fd);
                        FD_CLR(fd, &all_fds);
                    }
                    continue;
                } else {
                    // Something strange happens.
                    state_of_client[fd] = AWAITING_TYPE;
                    continue;
                }

            }
        }
    }
    // At last, we free the memory that we have malloc'ed.
    free(str_parent);
}
