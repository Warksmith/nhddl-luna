// Explicit PS1 playlists, discovery and complete-image measurements for LUNA.
#include "luna_psxcore_discset.h"
#include "luna_psxcore_library.h"
#include "psxcore_boot.h"
#include "psxcore_hash.h"
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define PLAYLIST_BYTES 8192u
int lunaPsxPlaylistFilename(const char *path) {
    size_t n=path?strlen(path):0;
    return n>4 && !strcasecmp(path+n-4,".m3u");
}
static void digestText(const unsigned char *digest, unsigned count, char *out) {
    for(unsigned i=0;i<count;++i)snprintf(out+2*i,3,"%02x",digest[i]);
}
// A playlist cannot name another device. Resolve dot components before applying
// PSXCore's path policy, never allowing a traversal above the volume root.
static const char *resolve(const char *base,const char *line,char out[256]) {
    char joined[768];
    const char *slash=strrchr(base,'/');
    if(!slash || strchr(line,':'))return "Disc paths must stay on the playlist's drive.";
    int absolute=line[0]=='/' || line[0]=='\\';
    int n=snprintf(joined,sizeof(joined),"%.*s%s",absolute?0:(int)(slash-base+1),base,line);
    if(n<0 || (size_t)n>=sizeof(joined))return "A disc path is too long.";
    for(char *p=joined;*p;++p)if(*p=='\\')*p='/';
    size_t used=0;out[0]=0;
    for(char *p=joined;*p;) {
        while(*p=='/')++p;
        if(!*p)break;
        char *end=strchr(p,'/');size_t length=end?(size_t)(end-p):strlen(p);
        if(length==1 && p[0]=='.') {p+=length;continue;}
        if(length==2 && !memcmp(p,"..",2)) {
            char *previous=strrchr(out,'/');if(!previous)return "A disc path leaves the drive root.";
            *previous=0;used=(size_t)(previous-out);p+=length;continue;
        }
        if(used+1+length>=256)return "A disc path is too long.";
        out[used++]='/';memcpy(out+used,p,length);used+=length;out[used]=0;p+=length;
    }
    return np_file_path_valid(out,256,0)?NULL:"The playlist contains an unsupported disc path.";
}
const char *lunaPsxPlaylistParse(const void *text,size_t bytes,const char *path,TargetDiscSet *out) {
    if(!text || !out || !path || !np_file_path_valid(path,256,1) || !lunaPsxPlaylistFilename(path))
        return "The playlist path is invalid.";
    memset(out,0,sizeof(*out));
    if(!bytes || bytes>PLAYLIST_BYTES || memchr(text,0,bytes))return "The playlist is empty, too large or contains NUL bytes.";
    const unsigned char *data=text;size_t p=bytes>=3 && !memcmp(data,"\xef\xbb\xbf",3)?3:0;
    while(p<bytes) {
        size_t start=p;while(p<bytes && data[p]!='\n' && data[p]!='\r')++p;
        size_t end=p;while(p<bytes && (data[p]=='\n' || data[p]=='\r'))++p;
        while(start<end && (data[start]==' ' || data[start]=='\t'))++start;
        while(end>start && (data[end-1]==' ' || data[end-1]=='\t'))--end;
        if(start==end)continue;
        size_t length=end-start;
        if(data[start]=='#') {
            static const char marker[]="#LUNA-ID ";
            if(length>=sizeof(marker)-1 && !memcmp(data+start,marker,sizeof(marker)-1)) {
                if(out->key[0] || length!=sizeof(marker)-1+32)return "The playlist has an invalid LUNA identity.";
                for(unsigned i=0;i<32;++i) {
                    unsigned char c=data[start+sizeof(marker)-1+i];
                    if(c>='A' && c<='F')c+=32;
                    if(!((c>='0' && c<='9') || (c>='a' && c<='f')))return "The playlist has an invalid LUNA identity.";
                    out->key[i]=(char)c;
                }
            }
            continue;
        }
        if(out->count==LUNA_DISC_LIMIT)return "PSXCore supports at most four discs in a set.";
        if(length>=512)return "A playlist line is too long.";
        char line[512];memcpy(line,data+start,length);line[length]=0;
        const char *error=resolve(path,line,out->paths[out->count]);if(error)return error;
        for(unsigned i=0;i<out->count;++i)if(!strcasecmp(out->paths[i],out->paths[out->count]))
            return "The playlist contains the same disc path twice.";
        ++out->count;
    }
    if(out->count<2)return "A disc set needs two to four VCD images.";
    if(!out->key[0]) {
        char identity[256];size_t n=strlen(path);
        for(size_t i=0;i<=n;++i)identity[i]=path[i]>='A' && path[i]<='Z'?path[i]+32:path[i];
        unsigned char hash[32];np_boot_sha256(identity,n+1,hash);digestText(hash,16,out->key);
    }
    return NULL;
}
const char *lunaPsxPlaylistRead(const char *path,TargetDiscSet *out) {
    if(!path || strlen(path)<7)return "The playlist path is invalid.";
    FILE *file=fopen(path,"rb");if(!file)return "Cannot open the disc playlist.";
    unsigned char *text=malloc(PLAYLIST_BYTES+1);if(!text){fclose(file);return "Not enough memory for the playlist.";}
    size_t n=fread(text,1,PLAYLIST_BYTES+1,file);int bad=ferror(file),closed=fclose(file);
    const char *error=bad || closed?"Cannot read the disc playlist.":lunaPsxPlaylistParse(text,n,path+6,out);
    free(text);return error;
}
static int memberPath(const Target *target,unsigned disc,char out[264]) {
    return target && target->discs && disc<target->discs->count &&
        snprintf(out,264,"%.6s%s",target->fullPath,target->discs->paths[disc])<264;
}
const char *lunaPsxGameIdentity(const Target *target,char out[256]) {
    if(!target || !target->fullPath || strlen(target->fullPath)<7)return "The game path is invalid.";
    if(target->discs) {
        if(strlen(target->discs->key)!=32)return "The disc set has no valid identity. Rescan the library.";
        snprintf(out,256,"/LUNA/PS1SETS/%s.m3u",target->discs->key);
    } else {
        if(!np_file_path_valid(target->fullPath+6,256,0))return "The game path is invalid.";
        snprintf(out,256,"%s",target->fullPath+6);
    }
    return NULL;
}
int lunaPsxGroupTargets(TargetList *targets) {
    // Parse all candidates first, then resolve conflicts before hiding members.
    for(Target *t=targets->first;t;t=t->next)if(t->discs) {
        const char *error=lunaPsxPlaylistRead(t->fullPath,t->discs);
        if(error)snprintf(t->discs->error,sizeof(t->discs->error),"%s",error);
        for(unsigned i=0;!error && i<t->discs->count;++i) {
            char path[264];memberPath(t,i,path);
            error=lunaPsxDiscInfo(path,t->discs->serials[i]);
            if(error)snprintf(t->discs->error,sizeof(t->discs->error),"Disc %u is missing, unreadable or invalid (%s).",i+1,error);
        }
        free(t->id);
        t->id=strdup(t->discs->serials[0][0]?t->discs->serials[0]:"PSXSET");
        if(!t->id)return -ENOMEM;
    }
    for(Target *a=targets->first;a;a=a->next)if(a->discs && a->discs->key[0])
        for(Target *b=a->next;b;b=b->next)if(b->discs && b->discs->key[0]) {
            int conflict=!strcmp(a->discs->key,b->discs->key);
            for(unsigned i=0;i<a->discs->count && !conflict;++i)
                for(unsigned j=0;j<b->discs->count;++j)
                    if(!strcasecmp(a->discs->paths[i],b->discs->paths[j]))conflict=1;
            if(conflict) {
                snprintf(a->discs->error,sizeof(a->discs->error),"Two playlists claim the same discs or LUNA identity. Keep one set.");
                snprintf(b->discs->error,sizeof(b->discs->error),"%s",a->discs->error);
            }
        }
    for(Target *t=targets->first;t;) {
        int hide=0;
        if(t->platform==TARGET_PS1 && !t->discs)
            for(Target *g=targets->first;g && !hide;g=g->next)if(g->discs && !g->discs->error[0])
                for(unsigned i=0;i<g->discs->count;++i)
                    if(!strcasecmp(t->fullPath+6,g->discs->paths[i]))hide=1;
        t=hide?freeTarget(targets,t):t->next;
    }
    return 0;
}
static int sameFile(const struct stat *a,const struct stat *b) {
    return a->st_size==b->st_size && a->st_mtime==b->st_mtime;
}
const char *lunaPsxDiscSetMeasure(const Target *target,unsigned initial,NpFileDiscSet *out,
    int (*progress)(uint64_t,uint64_t)) {
    if(!target || !target->discs || !out)return "No disc set was selected.";
    memset(out,0,sizeof(*out));
    if(target->discs->error[0])return target->discs->error;
    TargetDiscSet current;const char *error=lunaPsxPlaylistRead(target->fullPath,&current);
    if(error)return error;
    if(current.count!=target->discs->count || strcmp(current.key,target->discs->key) ||
       memcmp(current.paths,target->discs->paths,sizeof(current.paths)))return "The disc set changed. Rescan the library.";
    if(initial>=current.count)return "The starting disc is unavailable. Choose a starting disc again.";
    struct stat before[LUNA_DISC_LIMIT];uint64_t total=0,done=0;
    char path[264];
    for(unsigned i=0;i<current.count;++i) {
        memberPath(target,i,path);
        if(stat(path,&before[i]) || !S_ISREG(before[i].st_mode) || before[i].st_size<=0 ||
           (uint64_t)before[i].st_size>INT32_MAX)return "A disc is missing, unreadable or too large.";
        total+=(uint64_t)before[i].st_size;
    }
    unsigned char *buffer=malloc(65536),hashes[LUNA_DISC_LIMIT][32];
    if(!buffer)return "Not enough memory to validate the disc set.";
    NpFileDiscSet result={0};result.magic=NP_DISC_SET_MAGIC;result.version=1;
    result.size=sizeof(result);result.count=current.count;result.initial=initial;
    for(unsigned i=0;i<current.count && !error;++i) {
        NpFileDiscEntry *entry=&result.entries[i];memberPath(target,i,path);
        memcpy(entry->path,current.paths[i],sizeof(entry->path));entry->image_bytes=before[i].st_size;
        FILE *file=fopen(path,"rb");if(!file){error="Cannot open a disc in the set.";break;}
        unsigned char header[NP_FILE_HEADER_HASH_BYTES];NpVcdInfo info;
        if(fread(header,1,sizeof(header),file)!=sizeof(header))error="Cannot read a disc header.";
        if(!error)error=np_vcd_check(header,sizeof(header),entry->image_bytes,&info);
        if(!error) {
            entry->sectors=info.sectors;entry->tracks=info.tracks;
            np_boot_sha256(header,sizeof(header),entry->header_sha256);
            np_boot_sha256(header,1024,entry->toc_sha256);
            error=np_file_disc_entry_check(entry,header,sizeof(header));
        }
        if(!error && fseek(file,0,SEEK_SET))error="Cannot seek a disc in the set.";
        NpSha256 sha;np_sha256_init(&sha);uint64_t remaining=entry->image_bytes;
        while(!error && remaining) {
            if(progress && progress(done,total)){error="Disc validation cancelled.";break;}
            size_t take=remaining>65536?65536:(size_t)remaining;
            if(fread(buffer,1,take,file)!=take){error="Cannot read a complete disc image.";break;}
            np_sha256_update(&sha,buffer,take);done+=take;remaining-=take;
        }
        if(!error && (fgetc(file)!=EOF || ferror(file)))error="A disc changed during validation.";
        if(fclose(file) && !error)error="Cannot close a disc image.";
        struct stat after;
        if(!error && (stat(path,&after) || !sameFile(&before[i],&after)))error="A disc changed during validation.";
        if(!error) {
            np_sha256_final(&sha,hashes[i]);
            for(unsigned j=0;j<i;++j)if(!memcmp(hashes[j],hashes[i],32))error="Two entries contain the same disc image. Correct the disc set.";
        }
    }
    // Recheck all files and the playlist after the full scan, not just each file
    // immediately after reading it. Runtime independently rechecks bounded data.
    for(unsigned i=0;!error && i<current.count;++i) {
        struct stat after;memberPath(target,i,path);
        if(stat(path,&after) || !sameFile(&before[i],&after))error="A disc changed during validation.";
    }
    TargetDiscSet after;
    if(!error && (lunaPsxPlaylistRead(target->fullPath,&after) || after.count!=current.count ||
        strcmp(after.key,current.key) || memcmp(after.paths,current.paths,sizeof(current.paths))))error="The playlist changed during validation.";
    free(buffer);
    if(!error && progress && progress(total,total))error="Disc validation cancelled.";
    for(unsigned i=0;!error && i<current.count;++i) {
        char root[8];snprintf(root,sizeof(root),"%.6s",target->fullPath);
        error=lunaPsxDiscIdentityCheck(root,current.key,current.paths[i],result.entries[i].image_bytes,hashes[i]);
    }
    if(!error)*out=result;
    return error;
}
// Only explicit (Disc N)/(Disk N) markers are inferred. Edition/region text is
// retained in the grouping key; missing/duplicate positions are never filled in.
static unsigned discName(const char *name,char key[256]) {
    if(strlen(name)>=256)return 0;
    strcpy(key,name);unsigned position=0;char *marker=NULL;
    for(char *p=key;*p;++p)if((*p=='(' || *p=='[') &&
        (!strncasecmp(p+1,"disc ",5) || !strncasecmp(p+1,"disk ",5))) {
        if(position || p[6]<'1' || p[6]>'4' || p[7]!=(*p=='('?')':']'))return 0;
        position=(unsigned)(p[6]-'0');marker=p;
    }
    if(marker)memmove(marker,marker+8,strlen(marker+8)+1);
    return position;
}
const char *lunaPsxDiscSuggest(const Target *target,TargetDiscSet *out) {
    memset(out,0,sizeof(*out));
    if(target->discs){*out=*target->discs;out->error[0]=0;return NULL;}
    const char *name=strrchr(target->fullPath,'/');char key[256];
    if(!name || !discName(name+1,key))return "No unambiguous filename suggestion. Add the discs manually.";
    char directory[264];snprintf(directory,sizeof(directory),"%.*s",(int)(name-target->fullPath),target->fullPath);
    DIR *dir=opendir(directory);if(!dir)return "Cannot inspect nearby discs.";
    struct dirent *entry;int bad=0;
    for(;;) {
        errno=0;entry=readdir(dir);if(!entry){if(errno)bad=1;break;}
        char candidate[256];unsigned n=discName(entry->d_name,candidate);
        if(!n || strcasecmp(key,candidate))continue;
        if(out->paths[n-1][0]){bad=1;break;}
        int length=snprintf(out->paths[n-1],256,"%s/%s",directory+6,entry->d_name);
        if(length<0 || length>=256 || !np_file_path_valid(out->paths[n-1],256,0)){bad=1;break;}
        if(out->count<n)out->count=n;
    }
    if(closedir(dir))bad=1;
    for(unsigned i=0;i<out->count;++i)if(!out->paths[i][0])bad=1;
    if(bad || out->count<2){memset(out,0,sizeof(*out));return "Disc names are incomplete or ambiguous. Add the discs manually.";}
    return NULL;
}
