
#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <openssl/evp.h>

void hash_to_hex(const ObjectID *id, char *hex_out) {
    for (int i = 0; i < HASH_SIZE; i++) sprintf(hex_out + i*2, "%02x", id->hash[i]);
    hex_out[HASH_HEX_SIZE] = '\0';
}
int hex_to_hash(const char *hex, ObjectID *id_out) {
    if (strlen(hex) < HASH_HEX_SIZE) return -1;
    for (int i = 0; i < HASH_SIZE; i++) {
        unsigned int b;
        if (sscanf(hex + i*2, "%2x", &b) != 1) return -1;
        id_out->hash[i] = (uint8_t)b;
    }
    return 0;
}
void compute_hash(const void *data, size_t len, ObjectID *id_out) {
    unsigned int hl;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(ctx, data, len);
    EVP_DigestFinal_ex(ctx, id_out->hash, &hl);
    EVP_MD_CTX_free(ctx);
}
void object_path(const ObjectID *id, char *path_out, size_t path_size) {
    char hex[HASH_HEX_SIZE+1]; hash_to_hex(id, hex);
    snprintf(path_out, path_size, "%s/%.2s/%s", OBJECTS_DIR, hex, hex+2);
}
int object_exists(const ObjectID *id) {
    char path[512]; object_path(id, path, sizeof(path));
    return access(path, F_OK) == 0;
}

/* Phase 1 step 2: add deduplication and shard directory creation */
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out) {
    const char *ts = (type==OBJ_BLOB)?"blob":(type==OBJ_TREE)?"tree":"commit";
    char header[64];
    int hl = snprintf(header, sizeof(header), "%s %zu", ts, len);
    size_t full_len = hl + 1 + len;
    uint8_t *full = malloc(full_len);
    if (!full) return -1;
    memcpy(full, header, hl); full[hl] = '\0'; memcpy(full+hl+1, data, len);
    ObjectID id; compute_hash(full, full_len, &id);
    if (object_exists(&id)) { if (id_out) *id_out = id; free(full); return 0; }
    char hex[HASH_HEX_SIZE+1]; hash_to_hex(&id, hex);
    char shard[512]; snprintf(shard, sizeof(shard), "%s/%.2s", OBJECTS_DIR, hex);
    mkdir(shard, 0755);
    free(full);
    return -1; /* atomic write not yet implemented */
}
int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out) {
    (void)id; (void)type_out; (void)data_out; (void)len_out; return -1;
}
