
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
/* Phase 3 step 2: implement index_load */
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
int index_save(const Index *index) { (void)index; return -1; }
int index_add(Index *index, const char *path) { (void)index;(void)path; return -1; }
