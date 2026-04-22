
#include "commit.h"
#include "index.h"
#include "tree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);
int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out);
int commit_parse(const void *data, size_t len, Commit *commit_out) {
    (void)len; const char *p=(const char*)data; char hex[HASH_HEX_SIZE+1];
    if(sscanf(p,"tree %64s\n",hex)!=1) return -1;
    if(hex_to_hash(hex,&commit_out->tree)!=0) return -1;
    p=strchr(p,'\n')+1;
    if(strncmp(p,"parent ",7)==0){
        if(sscanf(p,"parent %64s\n",hex)!=1) return -1;
        if(hex_to_hash(hex,&commit_out->parent)!=0) return -1;
        commit_out->has_parent=1; p=strchr(p,'\n')+1;
    } else commit_out->has_parent=0;
    char ab[256]; uint64_t ts;
    if(sscanf(p,"author %255[^\n]\n",ab)!=1) return -1;
    char *ls=strrchr(ab,' '); if(!ls) return -1;
    ts=(uint64_t)strtoull(ls+1,NULL,10); *ls='\0';
    snprintf(commit_out->author,sizeof(commit_out->author),"%s",ab);
    commit_out->timestamp=ts;
    p=strchr(p,'\n')+1; p=strchr(p,'\n')+1; p=strchr(p,'\n')+1;
    snprintf(commit_out->message,sizeof(commit_out->message),"%s",p);
    return 0;
}
int commit_serialize(const Commit *commit, void **data_out, size_t *len_out) {
    char th[HASH_HEX_SIZE+1],ph[HASH_HEX_SIZE+1];
    hash_to_hex(&commit->tree,th);
    char buf[8192]; int n=0;
    n+=snprintf(buf+n,sizeof(buf)-n,"tree %s\n",th);
    if(commit->has_parent){hash_to_hex(&commit->parent,ph);n+=snprintf(buf+n,sizeof(buf)-n,"parent %s\n",ph);}
    n+=snprintf(buf+n,sizeof(buf)-n,"author %s %"PRIu64"\ncommitter %s %"PRIu64"\n\n%s",
        commit->author,commit->timestamp,commit->author,commit->timestamp,commit->message);
    *data_out=malloc(n+1); if(!*data_out) return -1;
    memcpy(*data_out,buf,n+1); *len_out=(size_t)n; return 0;
}
int commit_walk(commit_walk_fn callback, void *ctx) {
    ObjectID id; if(head_read(&id)!=0) return -1;
    while(1){
        ObjectType type; void *raw; size_t rl;
        if(object_read(&id,&type,&raw,&rl)!=0) return -1;
        Commit c; int rc=commit_parse(raw,rl,&c); free(raw);
        if(rc!=0) return -1;
        callback(&id,&c,ctx);
        if(!c.has_parent) break; id=c.parent;
    }
    return 0;
}
int head_read(ObjectID *id_out) {
    FILE *f=fopen(HEAD_FILE,"r"); if(!f) return -1;
    char line[512]; if(!fgets(line,sizeof(line),f)){fclose(f);return -1;} fclose(f);
    line[strcspn(line,"\r\n")]='\0';
    char rp[512];
    if(strncmp(line,"ref: ",5)==0){
        snprintf(rp,sizeof(rp),"%s/%s",PES_DIR,line+5);
        f=fopen(rp,"r"); if(!f) return -1;
        if(!fgets(line,sizeof(line),f)){fclose(f);return -1;} fclose(f);
        line[strcspn(line,"\r\n")]='\0';
    }
    return hex_to_hash(line,id_out);
}
int head_update(const ObjectID *new_commit) {
    FILE *f=fopen(HEAD_FILE,"r"); if(!f) return -1;
    char line[512]; if(!fgets(line,sizeof(line),f)){fclose(f);return -1;} fclose(f);
    line[strcspn(line,"\r\n")]='\0';
    char tp[520];
    if(strncmp(line,"ref: ",5)==0) snprintf(tp,sizeof(tp),"%s/%s",PES_DIR,line+5);
    else snprintf(tp,sizeof(tp),"%s",HEAD_FILE);
    char tmp[528]; snprintf(tmp,sizeof(tmp),"%s.tmp",tp);
    f=fopen(tmp,"w"); if(!f) return -1;
    char hex[HASH_HEX_SIZE+1]; hash_to_hex(new_commit,hex);
    fprintf(f,"%s\n",hex); fflush(f); fsync(fileno(f)); fclose(f);
    return rename(tmp,tp);
}

/* Phase 4 step 1: commit_create stub */
int commit_create(const char *message, ObjectID *commit_id_out) {
    (void)message; (void)commit_id_out; return -1;
}
