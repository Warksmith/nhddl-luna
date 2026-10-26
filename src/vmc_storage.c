// LUNA card management. Reopened byte comparisons verify all copied data.
#include "vmc_storage.h"
#include "vmc_create.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <timer.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

const char *const lunaVmcRecoverySuffixes[9] = {"", ".luna-new", ".luna-old", ".luna-recover", ".prev", ".bak", ".new", ".recover", ".recovery-old"};
static int statCard(const char *path, iox_stat_t *st) { return fileXioGetStat(path, st); }
static int absentCard(const char *path, int result) {
  if (!strncmp(path, "mass", 4) && path[4] >= '0' && path[4] <= '9' && path[5] == ':')
    return result == -4 || result == -5;
  return result == -ENOENT;
}
static int pathFree(const char *path) {
  iox_stat_t st; return absentCard(path, statCard(path, &st));
}
static int cardPath(char out[LUNA_VMC_PATH], const char *path, const char *suffix) {
  int n = snprintf(out, LUNA_VMC_PATH, "%s%s", path, suffix);
  return n > 0 && n < LUNA_VMC_PATH;
}
static int ioAll(int fd, void *buffer, unsigned size, int writing) {
  unsigned done = 0;
  while (done < size) {
    int n = writing ? fileXioWrite(fd, (char *)buffer + done, size - done) :
                      fileXioRead(fd, (char *)buffer + done, size - done);
    if (n <= 0 || (unsigned)n > size - done) return -1;
    done += n;
  }
  return 0;
}
static uint16_t card16(const unsigned char *p) { return p[0] | ((uint16_t)p[1] << 8); }
static uint32_t card32(const unsigned char *p) { return card16(p) | ((uint32_t)card16(p+2) << 16); }
const char *lunaVmcInspect(const char *path, int platform, LunaVmcInfo *info) {
  iox_stat_t st; unsigned char header[128];
  if (!path || statCard(path, &st) < 0 || !FIO_S_ISREG(st.mode) || st.hisize)
    return "The card is missing or cannot be read.";
  int fd = fileXioOpen(path, FIO_O_RDONLY, 0);
  if (fd < 0) return "Cannot open the memory card.";
  int bad = ioAll(fd, header, sizeof(header), 0);
  if (fileXioClose(fd) < 0) bad = 1;
  if (bad) return "Cannot read the memory card header.";
  if (platform == LUNA_VMC_PS1) {
    unsigned char sum = 0; for (unsigned i=0; i<127; ++i) sum ^= header[i];
    if (st.size != 131072 || header[0] != 'M' || header[1] != 'C' || sum != header[127])
      return "Choose a valid raw 128 KB PlayStation card.";
  } else if (platform == LUNA_VMC_PS2) {
    uint16_t page = card16(header+40), cluster = card16(header+42), block = card16(header+44);
    uint64_t bytes = (uint64_t)page * cluster * card32(header+48);
    if (memcmp(header, "Sony PS2 Memory Card Format ", 28) || page != 512 || cluster != 2 ||
        block < 16 || block > 64 || (block & (block-1)) || bytes < 8*1024*1024 ||
        bytes > 64*1024*1024 || (bytes & (bytes-1)) || bytes != st.size || (bytes/512)%block)
      return "Choose a valid raw 8, 16, 32 or 64 MB PlayStation 2 card.";
  } else return "Unknown memory card format.";
  if (info) info->bytes = st.size;
  return NULL;
}
const char *lunaVmcReady(const char *path, int platform) {
  char candidate[LUNA_VMC_PATH];
  for (unsigned i=1; i<4; ++i) {
    if (!cardPath(candidate, path, lunaVmcRecoverySuffixes[i])) return "The card path is too long.";
    if (!pathFree(candidate)) return "This card needs recovery before it can be used.";
  }
  if (platform == LUNA_VMC_PS1) {
    const char *suffix[]={".bak", ".new", ".recover", ".recovery-old"};
    for (unsigned i=0;i<4;++i) {
      if (!cardPath(candidate,path,suffix[i]) || !pathFree(candidate)) return "This PS1 card needs recovery.";
    }
    if(!cardPath(candidate,path,".prev"))return "The card path is too long.";
    if(!pathFree(candidate) && lunaVmcInspect(candidate,platform,NULL))return "The previous PS1 save needs recovery.";
    if(strlen(path)>=sizeof(candidate))return "The card path is too long.";
    snprintf(candidate,sizeof(candidate),"%s",path);
    char *slash=strrchr(candidate,'/');
    if(slash) {
      *slash=0;char journal[LUNA_VMC_PATH];
      if(!cardPath(journal,candidate,"/cards.pending") || !pathFree(journal) ||
         !cardPath(journal,candidate,"/cards.pending.new") || !pathFree(journal))return "This PS1 card pair needs recovery.";
    }
  }
  return lunaVmcInspect(path, platform, NULL);
}
/* Exact raw copy, bounded memory, no overwrite. Reopen both files and compare. */
static const char *copyCardBytes(const char *source, const char *destination, LunaVmcProgress progress) {
  iox_stat_t before, after;
  if (!pathFree(destination)) return "The destination exists or cannot be checked.";
  if (statCard(source,&before)<0 || !FIO_S_ISREG(before.mode) || before.hisize || before.size>64*1024*1024)
    return "Cannot read this card candidate.";
  unsigned char *buffers=malloc(32768*2);
  if (!buffers) return "Not enough memory to copy the card.";
  int in=fileXioOpen(source,FIO_O_RDONLY,0), out=-1, bad=0;
  if (in>=0) out=fileXioOpen(destination,FIO_O_WRONLY|FIO_O_CREAT|FIO_O_EXCL,0666);
  if (in<0 || out<0) bad=1;
  for (unsigned done=0; !bad && done<before.size;) {
    unsigned take=before.size-done; if(take>32768)take=32768;
    bad=ioAll(in,buffers,take,0) || ioAll(out,buffers,take,1);done+=take;
    if(progress)progress((unsigned)((uint64_t)done*50/(before.size?before.size:1)));
  }
  unsigned char extra;
  if(!bad && fileXioRead(in,&extra,1)!=0)bad=1;
  if(in>=0 && fileXioClose(in)<0)bad=1;
  if(out>=0 && fileXioClose(out)<0)bad=1;
  if(!bad && (statCard(destination,&after)<0 || after.hisize || after.size!=before.size))bad=1;
  in=out=-1;
  if(!bad) {in=fileXioOpen(source,FIO_O_RDONLY,0);out=fileXioOpen(destination,FIO_O_RDONLY,0);if(in<0 || out<0)bad=1;}
  for(unsigned done=0;!bad && done<before.size;) {
    unsigned take=before.size-done;if(take>32768)take=32768;
    bad=ioAll(in,buffers,take,0) || ioAll(out,buffers+32768,take,0) || memcmp(buffers,buffers+32768,take);done+=take;
    if(progress)progress(50+(unsigned)((uint64_t)done*50/(before.size?before.size:1)));
  }
  if(!bad && (fileXioRead(in,&extra,1)!=0 || fileXioRead(out,&extra,1)!=0))bad=1;
  if(in>=0 && fileXioClose(in)<0)bad=1;
  if(out>=0 && fileXioClose(out)<0)bad=1;
  free(buffers);
  return bad?"The copy failed verification. Original and staged files were preserved.":NULL;
}
const char *lunaVmcCopy(const char *source,const char *destination,int platform,LunaVmcProgress progress) {
  const char *error=lunaVmcReady(source,platform);if(error)return error;
  char stage[LUNA_VMC_PATH],candidate[LUNA_VMC_PATH];
  if(!strcasecmp(source,destination) || !pathFree(destination))return "Choose a new destination filename.";
  for(unsigned i=1;i<4;++i)if(!cardPath(candidate,destination,lunaVmcRecoverySuffixes[i]) || !pathFree(candidate))
    return "The destination needs recovery.";
  if(!cardPath(stage,destination,".luna-new"))return "The destination path is too long.";
  error=copyCardBytes(source,stage,progress);if(!error)error=lunaVmcInspect(stage,platform,NULL);
  if(error)return error;
  if(!pathFree(destination) || fileXioRename(stage,destination)<0)return "Cannot publish the verified card. Recover its staged copy.";
  return lunaVmcReady(destination,platform);
}
static const char *snapshotCard(const char *path,char out[LUNA_VMC_PATH],LunaVmcProgress progress) {
  static unsigned sequence;
  int n=snprintf(out,LUNA_VMC_PATH,"%s.backup-%llu-%u",path,(unsigned long long)GetTimerSystemTime(),++sequence);
  if(n<0 || n>=LUNA_VMC_PATH)return "The backup path is too long.";
  return copyCardBytes(path,out,progress);
}
const char *lunaVmcBackup(const char *path,int platform,char out[LUNA_VMC_PATH],LunaVmcProgress progress) {
  const char *error=lunaVmcReady(path,platform);return error?error:snapshotCard(path,out,progress);
}
const char *lunaVmcReplace(const char *path,const char *source,int platform,LunaVmcProgress progress) {
  const char *error=lunaVmcReady(path,platform);if(!error)error=lunaVmcReady(source,platform);if(error)return error;
  if(!strcasecmp(path,source))return "Choose a different source card.";
  char backup[LUNA_VMC_PATH],stage[LUNA_VMC_PATH],old[LUNA_VMC_PATH];
  if(!cardPath(stage,path,".luna-new") || !cardPath(old,path,".luna-old"))return "The card path is too long.";
  error=snapshotCard(path,backup,progress);if(error)return error;
  error=copyCardBytes(source,stage,progress);if(!error)error=lunaVmcInspect(stage,platform,NULL);if(error)return error;
  if(fileXioRename(path,old)<0 || fileXioRename(stage,path)<0)return "Replacement was interrupted. Open Recover before using this card.";
  error=lunaVmcInspect(path,platform,NULL);if(error)return error;
  return fileXioRemove(old)<0?"The replacement needs recovery cleanup.":NULL;
}
static const char *formatCard(const char *stage,int platform,LunaVmcProgress progress);
const char *lunaVmcRecover(const char *path,const char *suffix,int platform,LunaVmcProgress progress) {
  unsigned count=platform==LUNA_VMC_PS1?9:4,choice=count;
  for(unsigned i=0;i<count;++i)if(!strcmp(suffix,lunaVmcRecoverySuffixes[i]))choice=i;
  int blank=!strcmp(suffix,".blank");
  if(choice==count && !blank)return "Choose a recovery candidate.";
  char source[LUNA_VMC_PATH],candidate[LUNA_VMC_PATH],backup[LUNA_VMC_PATH],stage[LUNA_VMC_PATH];
  if(!cardPath(source,path,suffix) || !cardPath(stage,path,".luna-recover"))return "The card path is too long.";
  const char *error=NULL;
  if(blank) {
    if(!pathFree(path))return "An existing card cannot be replaced with a blank recovery card.";
    for(unsigned i=1;i<count;++i) {
      if(!cardPath(candidate,path,lunaVmcRecoverySuffixes[i]))return "The card path is too long.";
      if(!pathFree(candidate) && !lunaVmcInspect(candidate,platform,NULL))return "A valid saved card exists. Choose that recovery candidate.";
    }
  } else {error=lunaVmcInspect(source,platform,NULL);if(error)return error;}
  for(unsigned i=0;i<count;++i) {
    if(!cardPath(candidate,path,lunaVmcRecoverySuffixes[i]))return "The card path is too long.";
    if(pathFree(candidate))continue;
    error=snapshotCard(candidate,backup,progress);if(error)return error;
  }
  if(choice!=3 || blank) {
    if(!pathFree(stage) && fileXioRemove(stage)<0)return "Cannot clear the backed-up recovery stage.";
    error=blank?formatCard(stage,platform,progress):copyCardBytes(source,stage,progress);if(error)return error;
  }
  error=lunaVmcInspect(stage,platform,NULL);if(error)return error;
  if(!pathFree(path) && fileXioRemove(path)<0)return "Cannot replace the backed-up current card.";
  if(fileXioRename(stage,path)<0)return "Recovery was interrupted. The verified candidate remains available.";
  error=lunaVmcInspect(path,platform,NULL);if(error)return error;
  for(unsigned i=1;i<count;++i) {
    cardPath(candidate,path,lunaVmcRecoverySuffixes[i]);
    if(!pathFree(candidate) && fileXioRemove(candidate)<0)return "Recovery cleanup was interrupted.";
  }
  return lunaVmcReady(path,platform);
}
static void createProgress(int percent,void *context) { LunaVmcProgress *fn=context;if(*fn)(*fn)(percent); }
static const char *formatCard(const char *stage,int platform,LunaVmcProgress progress) {
  if(platform==LUNA_VMC_PS2) {
    if(lunaCreateVMC8(stage,createProgress,&progress))return "Could not create the 8 MB card.";
  } else if(platform==LUNA_VMC_PS1) {
    int fd=fileXioOpen(stage,FIO_O_WRONLY|FIO_O_CREAT|FIO_O_EXCL,0666);if(fd<0)return "Cannot create the card.";
    int bad=0;unsigned char frame[128];
    for(unsigned i=0;i<1024 && !bad;++i) {
      memset(frame,0,sizeof(frame));
      if(!i || i==63){frame[0]='M';frame[1]='C';}
      else if(i<=15){frame[0]=0xa0;frame[8]=frame[9]=0xff;}
      else if(i<=35){memset(frame,0xff,4);frame[8]=frame[9]=0xff;}
      for(unsigned j=0;j<127;++j)frame[127]^=frame[j];
      bad=ioAll(fd,frame,sizeof(frame),1);
      if(progress && !(i%64))progress(i*100/1024);
    }
    if(fileXioClose(fd)<0)bad=1;
    if(bad)return "Card creation was interrupted. Recover its staged copy.";
  } else return "Unknown memory card format.";
  return lunaVmcInspect(stage,platform,NULL);
}
const char *lunaVmcCreate(const char *path,int platform,LunaVmcProgress progress) {
  char stage[LUNA_VMC_PATH],candidate[LUNA_VMC_PATH];
  if(!pathFree(path))return "A card already exists or cannot be checked.";
  unsigned count=platform==LUNA_VMC_PS1?9:4;
  for(unsigned i=1;i<count;++i)if(!cardPath(candidate,path,lunaVmcRecoverySuffixes[i]) || !pathFree(candidate))
    return "This card needs recovery before creation.";
  if(!cardPath(stage,path,".luna-new"))return "The card path is too long.";
  const char *error=formatCard(stage,platform,progress);if(error)return error;
  if(!pathFree(path) || fileXioRename(stage,path)<0)return "Cannot publish the new card. Recover its staged copy.";
  if(progress)progress(100);
  return NULL;
}
