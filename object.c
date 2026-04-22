
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
    char final_path[512]; object_path(&id, final_path, sizeof(final_path));
    char tmp[512]; snprintf(tmp, sizeof(tmp), "%s.tmp", final_path);
    int fd = open(tmp, O_CREAT|O_WRONLY|O_TRUNC, 0644);
    if (fd < 0) { free(full); return -1; }
    if (write(fd, full, full_len) != (ssize_t)full_len) { close(fd); free(full); return -1; }
    fsync(fd); close(fd); free(full);
    if (rename(tmp, final_path) < 0) return -1;
    int dfd = open(shard, O_RDONLY);
    if (dfd >= 0) { fsync(dfd); close(dfd); }
    if (id_out) *id_out = id;
    return 0;
}

/* Phase 1 step 4: implement object_read with file reading and header parsing */
int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out) {
    char path[512]; object_path(id, path, sizeof(path));
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    fseek(f, 0, SEEK_END); long fsz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc(fsz); if (!buf) { fclose(f); return -1; }
    if ((long)fread(buf, 1, fsz, f) != fsz) { free(buf); fclose(f); return -1; }
    fclose(f);
    uint8_t *np = memchr(buf, '\0', fsz); if (!np) { free(buf); return -1; }
    if      (strncmp((char*)buf,"blob ",5)==0)   *type_out = OBJ_BLOB;
    else if (strncmp((char*)buf,"tree ",5)==0)   *type_out = OBJ_TREE;
    else if (strncmp((char*)buf,"commit ",7)==0) *type_out = OBJ_COMMIT;
    else { free(buf); return -1; }
    size_t off = (size_t)(np+1-buf), dlen = (size_t)fsz - off;
    void *out = malloc(dlen); if (!out) { free(buf); return -1; }
    memcpy(out, np+1, dlen);
    *data_out = out; *len_out = dlen; free(buf);
    return 0; /* integrity check not yet added */
}
