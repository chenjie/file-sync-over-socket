# file-sync-over-socket
A file backup program that transfers files from client side to server side sandbox using socket in C. After the file transfer is done, it automatically checks the integrity of the file by calculating a new hash value and comparing it with the one that server has received. If they match, the process is completed. Otherwise, client will be asked to resend that file.  
Just like the local version, multiple processes are used to transfer files concurrently, which speeds up the program.  
```
pid_t pid = fork(); // Handle recursive calls for each subdirectory.
```

## Getting Started

### Prerequisites

* GCC
* Terminal (in Unix) OR PowerShell (in Windows)

### Download source code and compile
The following instructions are presented using Terminal in macOS:
```
# Change to HOME directory
$ cd ~

# Clone this repo and 'cd' into it
$ git clone https://github.com/jellycsc/file-sync-over-socket.git
$ cd file-sync-over-socket/

# Let's compile both client and server
# Note: You can change the port number in the Makefile.
$ make
gcc -DPORT=18229 -g -Wall -std=gnu99 -c rcopy_client.c
gcc -DPORT=18229 -g -Wall -std=gnu99 -c ftree.c
gcc -DPORT=18229 -g -Wall -std=gnu99 -c hash_functions.c
gcc -DPORT=18229 -g -Wall -std=gnu99 -o rcopy_client rcopy_client.o ftree.o hash_functions.o
gcc -DPORT=18229 -g -Wall -std=gnu99 -c rcopy_server.c
gcc -DPORT=18229 -g -Wall -std=gnu99 -o rcopy_server rcopy_server.o ftree.o hash_functions.o
```

### Usage
Client:
```
Usage: rcopy_client SRC HOST
	 SRC - The file or directory to copy to the server
	 HOST - The hostname of the server
```
Server:
```
Usage: rcopy_server PATH_PREFIX
	 PATH_PREFIX - The path on the server used as the path prefix for the destination
```

### Example
Client:
```
# Let's start by transferring a sample Java project folder
$ ./rcopy_client workspace1/ localhost
Socket connection established.
Copy completed successfully
```
Server:
```
$ ./rcopy_server dest/
workspace1
workspace1/final_test
workspace1/final_test/src
workspace1/final_test/src/final_test
File transfer is completed!
workspace1/final_test/src/final_test/B.java
File transfer is completed!
workspace1/final_test/src/final_test/Super.java
File transfer is completed!
workspace1/final_test/src/final_test/in.java
File transfer is completed!
workspace1/final_test/src/final_test/Child1.java
File transfer is completed!
workspace1/final_test/src/final_test/Test.java
File transfer is completed!
workspace1/final_test/src/final_test/Child11.java
File transfer is completed!
workspace1/final_test/src/final_test/Test1.java
File transfer is completed!
workspace1/final_test/src/final_test/A.java
workspace1/final_test/src/haha
File transfer is completed!
workspace1/final_test/src/haha/Test.java
CLIENT [4] HAS DISCONNECTED.
```
All client files have been successfully backed up to `sandbox` folder on the server, which has permission `0400`. Having said that, this prevents clients from trying to create files and directories above the dest directory.

## Authors

| Name                    | GitHub                                     | Email
| ----------------------- | ------------------------------------------ | -------------------------
| Chenjie (Jack) Ni       | [jellycsc](https://github.com/jellycsc)    | nichenjie2013@gmail.com
| Jialiang (Jerry) Yi     | [JerryGor](https://github.com/JerryGor)    | 515252309jerry@gmail.com

## Thoughts and future improvements

* Processes can be replaced with [threads](http://man7.org/linux/man-pages/man7/pthreads.7.html). The later ones are more light-weighted with less overheads. 

## Contributing to this project

1. Fork it ( https://github.com/jellycsc/file-sync-over-socket/fork )
2. Create your feature branch (`git checkout -b my-new-feature`)
3. Commit your changes (`git commit -m 'Add some feature'`)
4. Push to your feature branch (`git push origin my-new-feature`)
5. Create a new Pull Request

Details are described [here](https://git-scm.com/book/en/v2/GitHub-Contributing-to-a-Project).

## Bug Reporting [![GitHub issues](https://img.shields.io/github/issues/jellycsc/file-sync-over-socket.svg)](https://github.com/jellycsc/file-sync-over-socket/issues/)

Please click `issue` button above↑ to report any issues related to this project  
OR you can shoot an email to <nichenjie2013@gmail.com>

