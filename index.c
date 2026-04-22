
#include "index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);
IndexEntry* index_find(Index *index, const char *path) {
    for (int i=0;i<index->count;i++) if(strcmp(index->entries[i].path,path)==0) return &index->entries[i];
    return NULL;
}
int index_remove(Index *index, const char *path) {
    for (int i=0;i<index->count;i++) {
        if(strcmp(index->entries[i].path,path)==0) {
            int r=index->count-i-1;
            if(r>0) memmove(&index->entries[i],&index->entries[i+1],r*sizeof(IndexEntry));
            index->count--; return index_save(index);
        }
    }
    fprintf(stderr,"error: '%s' is not in the index\n",path); return -1;
}
int index_status(const Index *index) {
    printf("Staged changes:\n"); int sc=0;
    for(int i=0;i<index->count;i++){printf("  staged:     %s\n",index->entries[i].path);sc++;}
    if(!sc) printf("  (nothing to show)\n"); printf("\n");
    printf("Unstaged changes:\n"); int uc=0;
    for(int i=0;i<index->count;i++){
        struct stat st;
        if(stat(index->entries[i].path,&st)!=0){printf("  deleted:    %s\n",index->entries[i].path);uc++;}
        else if(st.st_mtime!=(time_t)index->entries[i].mtime_sec||st.st_size!=(off_t)index->entries[i].size){printf("  modified:   %s\n",index->entries[i].path);uc++;}
    }
    if(!uc) printf("  (nothing to show)\n"); printf("\n");
    printf("Untracked files:\n"); int nc=0;
    DIR *dir=opendir(".");
    if(dir){struct dirent *ent;
        while((ent=readdir(dir))!=NULL){
            if(!strcmp(ent->d_name,".")||!strcmp(ent->d_name,"..")||!strcmp(ent->d_name,".pes")||!strcmp(ent->d_name,"pes")||strstr(ent->d_name,".o")) continue;
            int t=0; for(int i=0;i<index->count;i++) if(!strcmp(index->entries[i].path,ent->d_name)){t=1;break;}
            if(!t){struct stat st; stat(ent->d_name,&st); if(S_ISREG(st.st_mode)){printf("  untracked:  %s\n",ent->d_name);nc++;}}
        } closedir(dir);
    }
    if(!nc) printf("  (nothing to show)\n"); printf("\n"); return 0;
}
int index_load(Index *index) {
    index->count = 0;
    FILE *f = fopen(INDEX_FILE,"r"); if(!f) return 0;
    char hex[HASH_HEX_SIZE+1];
    while(index->count < MAX_INDEX_ENTRIES) {
        IndexEntry *e = &index->entries[index->count];
        int r = fscanf(f,"%o %64s %llu %u %511s",&e->mode,hex,(unsigned long long*)&e->mtime_sec,&e->size,e->path);
        if(r==EOF) break; if(r!=5){fclose(f);return -1;}
        if(hex_to_hash(hex,&e->hash)<0){fclose(f);return -1;}
        index->count++;
    }
    fclose(f); return 0;
}
static int cmp_ptrs(const void *a, const void *b) {
    return strcmp((*(const IndexEntry**)a)->path,(*(const IndexEntry**)b)->path);
}
/* Phase 3 step 4: fix index_save to use pointer array, avoiding 5MB stack copy */
int index_save(const Index *index) {
    const IndexEntry *ptrs[MAX_INDEX_ENTRIES];
    for(int i=0;i<index->count;i++) ptrs[i]=&index->entries[i];
    qsort(ptrs,index->count,sizeof(IndexEntry*),cmp_ptrs);
    char tmp[512]; snprintf(tmp,sizeof(tmp),"%s.tmp",INDEX_FILE);
    FILE *f=fopen(tmp,"w"); if(!f) return -1;
    for(int i=0;i<index->count;i++){
        char hex[HASH_HEX_SIZE+1]; hash_to_hex(&ptrs[i]->hash,hex);
        fprintf(f,"%o %s %llu %u %s\n",ptrs[i]->mode,hex,(unsigned long long)ptrs[i]->mtime_sec,ptrs[i]->size,ptrs[i]->path);
    }
    fflush(f); fsync(fileno(f)); fclose(f);
    return rename(tmp,INDEX_FILE);
}
int index_add(Index *index, const char *path) { (void)index;(void)path; return -1; }
