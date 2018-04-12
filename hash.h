#ifndef _HASH_H_
#define _HASH_H_

#define BLOCKSIZE 81

// Hash manipulation helper functions
char *hash(char *hash_val, FILE *f);
int check_hash(const char *hash1, const char *hash2);

#endif // _HASH_H_
