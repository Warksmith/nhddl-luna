// Verified publication of LUNA-owned playlists and PSXCore binary descriptors.
#include "luna_psxcore_discset.h"
#include "psxcore_boot.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

static int missing(int result) {return result==-4 || result==-5;}
static const char *folder(const char *path) {
    iox_stat_t st;int e=fileXioGetStat(path,&st);
    if(e>=0)return FIO_S_ISDIR(st.mode)?NULL:"The disc-set folder is occupied by a file.";
    return missing(e) && fileXioMkdir(path,0777)>=0?NULL:"Cannot create the disc-set folder.";
}
static int exact(const char *path,const void *data,unsigned bytes) {
    iox_stat_t st;
    if(fileXioGetStat(path,&st)<0 || !FIO_S_ISREG(st.mode) || st.hisize || st.size!=bytes)return 0;
    unsigned char *copy=malloc(bytes);if(!copy)return 0;
    int fd=fileXioOpen(path,FIO_O_RDONLY,0),bad=fd<0;unsigned done=0;
    while(!bad && done<bytes) {
        int n=fileXioRead(fd,copy+done,bytes-done);
        if(n<=0 || (unsigned)n>bytes-done)bad=1;else done+=n;
    }
    if(fd>=0 && fileXioClose(fd)<0)bad=1;
    int match=!bad && !memcmp(copy,data,bytes);free(copy);return match;
}
static const char *fresh(const char *path,const void *data,unsigned bytes) {
    iox_stat_t st;if(!missing(fileXioGetStat(path,&st)))return "A staged disc-set file already exists. Preserve it for recovery.";
    int fd=fileXioOpen(path,FIO_O_WRONLY|FIO_O_CREAT,0666);
    if(fd<0)return "Cannot create the disc-set file.";
    unsigned done=0;int bad=0;
    while(!bad && done<bytes) {
        int n=fileXioWrite(fd,(const unsigned char *)data+done,bytes-done);
        if(n<=0 || (unsigned)n>bytes-done)bad=1;else done+=n;
    }
    if(fileXioClose(fd)<0)bad=1;
    return bad || !exact(path,data,bytes)?"Cannot verify the disc-set write. The staged file was preserved.":NULL;
}
static const char *discFolder(const char *root) {
    if(!root || strlen(root)!=6 || strncmp(root,"mass",4) || root[4]<'0' || root[4]>'9' || root[5]!=':')
        return "Unsupported disc-set drive.";
    char directory[64];snprintf(directory,sizeof(directory),"%s/POPS",root);
    const char *error=folder(directory);if(error)return error;
    snprintf(directory,sizeof(directory),"%s/POPS/LUNA_DISCSETS",root);return folder(directory);
}
static const char *identityPath(const char *root,const char *key,const char *member,char path[192]) {
    if(!key || strlen(key)!=32 || !np_file_path_valid(member,256,0))return "The disc identity path is invalid.";
    char input[320],hex[65];unsigned char hash[32];
    int n=snprintf(input,sizeof(input),"%s:%s",key,member);
    if(n<0 || (size_t)n>=sizeof(input))return "The disc identity path is too long.";
    for(int i=0;i<n;++i)if(input[i]>='A' && input[i]<='Z')input[i]+=32;
    np_boot_sha256(input,(size_t)n,hash);
    for(unsigned i=0;i<32;++i)snprintf(hex+i*2,3,"%02x",hash[i]);
    snprintf(path,192,"%s/POPS/LUNA_DISCSETS/%s.identity",root,hex);return NULL;
}
const char *lunaPsxDiscIdentityCheck(const char *root,const char *key,const char *member,
    uint64_t bytes,const unsigned char sha256[32]) {
    const char *error=discFolder(root);if(error)return error;
    char path[192],stage[200];error=identityPath(root,key,member,path);if(error)return error;
    struct {uint32_t magic,version;uint64_t bytes;unsigned char hash[32];} record={0x4944534c,1,bytes,{0}};
    memcpy(record.hash,sha256,32);
    if(exact(path,&record,sizeof(record)))return NULL;
    iox_stat_t st;
    if(!missing(fileXioGetStat(path,&st)))
        return "A disc image changed or its fingerprint is damaged. Use Manage discs > Revalidate image.";
    snprintf(stage,sizeof(stage),"%s.new",path);
    if(!exact(stage,&record,sizeof(record))) {error=fresh(stage,&record,sizeof(record));if(error)return error;}
    if(fileXioRename(stage,path)<0 || !exact(path,&record,sizeof(record)))return "Cannot save the verified disc fingerprint.";
    return NULL;
}
const char *lunaPsxDiscIdentityReset(const char *root,const char *key,const char *member) {
    const char *error=discFolder(root);if(error)return error;
    char path[192],source[200],previous[208];error=identityPath(root,key,member,path);if(error)return error;
    const char *suffix[]={"",".new"};
    for(unsigned i=0;i<2;++i) {
        snprintf(source,sizeof(source),"%s%s",path,suffix[i]);
        iox_stat_t st;int found=fileXioGetStat(source,&st);
        if(missing(found))continue;
        if(found<0)return "Cannot inspect the previous disc fingerprint.";
        snprintf(previous,sizeof(previous),"%s.previous",source);
        if(!missing(fileXioGetStat(previous,&st)))return "A previous fingerprint backup already exists. Preserve it before revalidating again.";
        if(fileXioRename(source,previous)<0)return "Cannot preserve the previous disc fingerprint.";
    }
    return NULL;
}
const char *lunaPsxDiscSetPublish(const char *root,const NpFileDiscSet *set,char relative[256]) {
    const char *error=discFolder(root);if(error)return error;
    char path[256],stage[264],hex[65];unsigned char hash[32];
    np_boot_sha256(set,sizeof(*set),hash);
    for(unsigned i=0;i<32;++i)snprintf(hex+i*2,3,"%02x",hash[i]);
    snprintf(relative,256,"/POPS/LUNA_DISCSETS/%s.bin",hex);
    snprintf(path,sizeof(path),"%s%s",root,relative);snprintf(stage,sizeof(stage),"%s.new",path);
    if(exact(path,set,sizeof(*set)))return NULL;
    iox_stat_t st;
    if(!missing(fileXioGetStat(path,&st)))return "A generated disc descriptor is damaged. Preserve it and rescan.";
    // A complete interrupted publication can be resumed without overwriting it.
    if(!exact(stage,set,sizeof(*set))) {error=fresh(stage,set,sizeof(*set));if(error)return error;}
    if(fileXioRename(stage,path)<0 || !exact(path,set,sizeof(*set)))return "Cannot publish the verified disc descriptor.";
    return NULL;
}
const char *lunaPsxPlaylistSave(const char *path,const TargetDiscSet *set,int replace) {
    if(!path || strlen(path)<7 || strlen(path)>=264 || strncmp(path,"mass",4) ||
       path[4]<'0' || path[4]>'9' || path[5]!=':' || !lunaPsxPlaylistFilename(path) || !set || set->count<2 || set->count>4)
        return "A disc set needs two to four VCD images.";
    char text[2048],stage[272],previous[272];int used=snprintf(text,sizeof(text),"#EXTM3U\n");
    for(unsigned i=0;i<set->count;++i) {
        int n=snprintf(text+used,sizeof(text)-(size_t)used,"%s\n",set->paths[i]);
        if(n<0 || (size_t)n>=sizeof(text)-(size_t)used)return "The playlist is too large.";
        used+=n;
    }
    TargetDiscSet parsed;const char *error=lunaPsxPlaylistParse(text,(size_t)used,path+6,&parsed);if(error)return error;
    const char *key=set->key[0]?set->key:parsed.key;
    char identified[2048];int length=snprintf(identified,sizeof(identified),"#LUNA-ID %s\n%s",key,text);
    if(length<0 || (size_t)length>=sizeof(identified))return "The playlist is too large.";
    error=lunaPsxPlaylistParse(identified,(size_t)length,path+6,&parsed);if(error)return error;
    snprintf(stage,sizeof(stage),"%s.new",path);snprintf(previous,sizeof(previous),"%s.prev",path);
    iox_stat_t st;int exists=fileXioGetStat(path,&st);
    if(exists<0 && !missing(exists))return "Cannot inspect the playlist destination.";
    if(exists>=0 && !replace)return "A playlist already exists at this path. Choose a different name.";
    if(!missing(fileXioGetStat(previous,&st)))return "A previous playlist needs recovery. Preserve its .prev file.";
    error=fresh(stage,identified,(unsigned)length);if(error)return error;
    if(exists>=0 && fileXioRename(path,previous)<0)return "Cannot preserve the previous playlist.";
    if(fileXioRename(stage,path)<0 || !exact(path,identified,(unsigned)length))return "Cannot publish the playlist. Recovery files were preserved.";
    // Keep the old playlist until the replacement is reopened and verified.
    if(exists>=0 && fileXioRemove(previous)<0)return "Playlist saved; its previous copy could not be removed. Preserve it.";
    return NULL;
}
