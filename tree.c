
#include "tree.h"
#include "index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MODE_FILE 0100644
#define MODE_EXEC 0100755
#define MODE_DIR  0040000

int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);

uint32_t get_file_mode(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return 0;
    if (S_ISDIR(st.st_mode)) return MODE_DIR;
    if (st.st_mode & S_IXUSR) return MODE_EXEC;
    return MODE_FILE;
}
int tree_parse(const void *data, size_t len, Tree *tree_out) {
    tree_out->count = 0;
    const uint8_t *ptr = (const uint8_t *)data;
    const uint8_t *end = ptr + len;
    while (ptr < end && tree_out->count < MAX_TREE_ENTRIES) {
        TreeEntry *e = &tree_out->entries[tree_out->count];
        const uint8_t *sp = memchr(ptr,' ',end-ptr); if (!sp) return -1;
        char ms[16]={0}; size_t ml=sp-ptr; if(ml>=sizeof(ms)) return -1;
        memcpy(ms,ptr,ml); e->mode=strtol(ms,NULL,8); ptr=sp+1;
        const uint8_t *nb = memchr(ptr,'\0',end-ptr); if (!nb) return -1;
        size_t nl=nb-ptr; if(nl>=sizeof(e->name)) return -1;
        memcpy(e->name,ptr,nl); e->name[nl]='\0'; ptr=nb+1;
        if (ptr+HASH_SIZE>end) return -1;
        memcpy(e->hash.hash,ptr,HASH_SIZE); ptr+=HASH_SIZE;
        tree_out->count++;
    }
    return 0;
}
static int cmp_entries(const void *a, const void *b) {
    return strcmp(((const TreeEntry*)a)->name, ((const TreeEntry*)b)->name);
}
int tree_serialize(const Tree *tree, void **data_out, size_t *len_out) {
    uint8_t *buf = malloc(tree->count * 296); if (!buf) return -1;
    Tree s = *tree;
    qsort(s.entries, s.count, sizeof(TreeEntry), cmp_entries);
    size_t off = 0;
    for (int i = 0; i < s.count; i++) {
        int w = sprintf((char*)buf+off, "%o %s", s.entries[i].mode, s.entries[i].name);
        off += w+1;
        memcpy(buf+off, s.entries[i].hash.hash, HASH_SIZE); off+=HASH_SIZE;
    }
    *data_out=buf; *len_out=off; return 0;
}
static int write_tree_recursive(IndexEntry **entries, int count, int prefix_len, ObjectID *id_out) {
    Tree tree; tree.count = 0;
    int i = 0;
    while (i < count) {
        const char *path = entries[i]->path + prefix_len;
        const char *slash = strchr(path, '/');
        if (!slash) {
            TreeEntry *te = &tree.entries[tree.count++];
            te->mode = entries[i]->mode; te->hash = entries[i]->hash;
            strncpy(te->name, path, sizeof(te->name)-1);
            te->name[sizeof(te->name)-1] = '\0';
            i++;
        } else {
            int dnl = (int)(slash - path);
            char dir[256]; strncpy(dir, path, dnl); dir[dnl] = '\0';
            int j = i;
            while (j < count) {
                const char *p = entries[j]->path + prefix_len;
                if (strncmp(p, dir, dnl) != 0 || p[dnl] != '/') break;
                j++;
            }
            ObjectID sub;
            if (write_tree_recursive(entries+i, j-i, prefix_len+dnl+1, &sub) < 0) return -1;
            TreeEntry *te = &tree.entries[tree.count++];
            te->mode = MODE_DIR; te->hash = sub;
            strncpy(te->name, dir, sizeof(te->name)-1);
            te->name[sizeof(te->name)-1] = '\0';
            i = j;
        }
    }
    void *data; size_t len;
    if (tree_serialize(&tree, &data, &len) < 0) return -1;
    int ret = object_write(OBJ_TREE, data, len, id_out);
    free(data); return ret;
}

/* Phase 2 step 4: complete tree_from_index loading index entries */
int tree_from_index(ObjectID *id_out) {
    Index index;
    if (index_load(&index) < 0) return -1;
    IndexEntry *ptrs[MAX_INDEX_ENTRIES];
    for (int i = 0; i < index.count; i++) ptrs[i] = &index.entries[i];
    return write_tree_recursive(ptrs, index.count, 0, id_out);
}

__attribute__((weak)) int index_load(Index *index) { (void)index; return -1; }
