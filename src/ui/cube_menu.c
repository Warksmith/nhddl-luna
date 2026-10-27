/* Settings menu with authored crystal rendering and selection effects. */
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "ui/cube_menu.h"
#define CUBE_PI 3.14159265359f
#define CUBE_COUNT 8
#define CUBE_ITEMS 6
typedef struct { float x,y,z; } CubeVec;
typedef struct {
  float entry,exit,time,moveAge,rowFrom,row;
  float light[8],lightFrom[8],lightTarget[8],lightAge[8],lightDuration[8];
  CubeVec cameraTarget;
  uint32_t lastMs;
  int selected,closing,entering;
  int itemCount;
  float submenuAge;
  int submenuOpen,submenuDirection;
} CubeMenu;
enum { CUBE_SUBMENU_MS = 1440 };
static float cubeClamp(float v) { return fmaxf(0,fminf(1,v)); }
static float cubeSmooth(float t) {t=cubeClamp(t);return t*t*(3-2*t);}
static float cubeSin(float degrees) { return sinf(degrees*CUBE_PI/180); }
static CubeVec cubePosition(int i,float seconds) {
  float t=seconds*30,x=0,y=0,z=0;
  /* Static transcription of all eight switch cases. This is not an orbit
   * approximation: each crystal has its own original positional equation. */
  switch(i) {
    case 0:x=5.5f;y=3.5f;z=-5.4f+1.5f*cubeSin(30+.1f*t);break;
    case 1:x=5.5f+cubeSin(135+.2f*t);y=5;z=6.5f+cubeSin(60+.2f*t);break;
    case 2:x=1+cubeSin(-.1f*t);y=3;z=2+cubeSin(90-.1f*t);break;
    case 3:x=9+2*cubeSin(.1f*t-130);y=1;z=18+2*cubeSin(30-.08f*t);break;
    case 4:x=2*cubeSin(.1f*t)-1;y=-5.5f;z=13+3*cubeSin(.1f*t+90);break;
    case 5:x=11+2*cubeSin(.1f*t+180);y=-7-cubeSin(-.1f*t-30);z=6+3*cubeSin(.1f*t+270);break;
    case 6:x=4+cubeSin(.1f*t);y=0;z=8+14*cubeSin(.08f*t-50);break;
    case 7:x=cubeSin(.1f*t-30);y=0;z=8+10*cubeSin(.09f*t);break;
  }
  return (CubeVec){-25*x,25*y,25*z};
}
static CubeVec cubeRotation(int i,float seconds) {
  float t=seconds*30,x=0,y=0,z=0;
  switch(i) {
    case 0:x=.05f*t;y=-.15f*t;z=.1f*t;break;
    case 1:x=.05f*t;y=-.1f*t;z=45+.1f*t;break;
    case 2:x=45-.1f*t;y=.15f*t;z=.1f*t;break;
    case 3:x=.1f*t;y=45+.15f*t;z=-.05f*t;break;
    case 4:x=30+.1f*t;y=-.05f*t;z=.1f*t;break;
    case 5:x=.1f*t;y=.142f*t;z=.05f*t;break;
    case 6:x=.12f*t;y=.15f*t;z=.1f*t;break;
    case 7:x=.1f*t;y=45+.1f*t;z=45+.1f*t;break;
  }
  return (CubeVec){x*CUBE_PI/180,y*CUBE_PI/180,z*CUBE_PI/180};
}
static void cubeInit(CubeMenu *m,uint32_t now,int selected) {
  memset(m,0,sizeof(*m));m->lastMs=now;m->selected=selected;
  m->itemCount=CUBE_ITEMS;
  m->row=m->rowFrom=selected;m->moveAge=1000;
  for(int i=0;i<8;i++) {m->lightTarget[i]=i==selected;m->lightDuration[i]=150;}
}
static void cubeMove(CubeMenu *m,int dir) {
  if(m->closing)return;
  m->selected=(m->selected+dir+m->itemCount)%m->itemCount;
  m->rowFrom=m->row;m->moveAge=0;
  for(int i=0;i<8;i++) {
    m->lightFrom[i]=m->light[i];m->lightTarget[i]=i==m->selected%CUBE_COUNT;
    m->lightAge[i]=0;m->lightDuration[i]=i==m->selected?150:200;
  }
}
static void cubeClose(CubeMenu *m,int entering) {
  m->closing=1;m->entering=entering;m->exit=0;
  CubeVec p=cubePosition(m->selected%CUBE_COUNT,m->time);
  /* camera.bnx: lateral offset 96, vertical -4, near destination z+260.
   * Camera/root handedness is expressed in our projection coordinates. */
  m->cameraTarget=(CubeVec){-p.x-96,p.y-4,500+p.z-260};
}
static void cubeStep(CubeMenu *m,uint32_t now) {
  uint32_t dt=now-m->lastMs;m->lastMs=now;
  if(dt>250)return;
  m->entry=fminf(500,m->entry+dt);
  if(m->closing)m->exit+=dt;
  else if(!m->submenuOpen)m->time+=dt*.001f;
  if(m->submenuDirection) {
    m->submenuAge=fmaxf(0,fminf(CUBE_SUBMENU_MS,
        m->submenuAge+(float)dt*m->submenuDirection));
    if(m->submenuAge==0||m->submenuAge==CUBE_SUBMENU_MS) {
      m->submenuDirection=0;
      if(m->submenuAge==0)m->submenuOpen=0;
    }
  }
  m->moveAge=fminf(150,m->moveAge+dt);
  float u=cubeClamp(m->moveAge/150);
  m->row=m->rowFrom+(m->selected-m->rowFrom)*u;
  for(int i=0;i<8;i++) {
    m->lightAge[i]+=dt;
    float a=cubeClamp(m->lightAge[i]/m->lightDuration[i]);
    m->light[i]=m->lightFrom[i]+(m->lightTarget[i]-m->lightFrom[i])*a;
  }
}
#include "ui/view_internal.h"
#include "ui/pad.h"
#include "ui/language.h"
#include <libpad.h>
#include <dmaKit.h>
#include <unistd.h>
#include "cube_draw.inc"
int uiCubeSettingsLoop(int *selected, const char *status,
                       const LunaCubeSubmenu submenus[4]) {
  static CubeMenu menu;
  static const int actions[]={LUNA_CUBE_VMC,LUNA_CUBE_STORAGE,LUNA_CUBE_LANGUAGE,
                              LUNA_CUBE_DEFAULTS,LUNA_CUBE_EXIT,LUNA_CUBE_SHUTDOWN};
  int initial=*selected;
  if(initial<0||initial>=CUBE_ITEMS)initial=0;
  cubeInit(&menu,uiNowMs(),initial);
  cubeMaterialInit();
  int previous=pollInput(),destination=LUNA_CUBE_BACK,confirmShutdown=0;
  const LunaCubeSubmenu *submenu=NULL;
  for(;;) {
    int wasSubmenuOpen=menu.submenuOpen;
    cubeStep(&menu,uiNowMs());
    if(wasSubmenuOpen&&!menu.submenuOpen) {
      if(submenu->end)submenu->end(submenu->context);
      submenu=NULL;
    }
    cubeDraw(&menu,status,confirmShutdown,NULL,NULL,submenu);
    int held=pollInput(),input=held&~previous;previous=held;
    if(menu.submenuOpen) {
      if(menu.submenuDirection)continue;
      int result=submenu->input(submenu->context,input);
      /* Resample after actions that may open a blocking dialog. Idle frames
       * keep their original sample so a new press cannot be swallowed. */
      if(input)previous=pollInput();
      if(result==LUNA_CUBE_SUBMENU_BACK)menu.submenuDirection=-1;
      else if(result==LUNA_CUBE_SUBMENU_DONE) {
        if(submenu->end)submenu->end(submenu->context);
        cubeMaterialClose();return actions[menu.selected];
      }
      continue;
    }
    if(menu.closing) {
      if(menu.exit>=(menu.entering?1440:500)) {cubeMaterialClose();return destination;}
      continue;
    }
    if(confirmShutdown) {
      if(input&(PAD_CIRCLE|PAD_TRIANGLE|PAD_START))confirmShutdown=0;
      else if(input&PAD_CROSS) {destination=LUNA_CUBE_SHUTDOWN;cubeClose(&menu,1);}
    } else if(input&(PAD_CIRCLE|PAD_TRIANGLE|PAD_START)) {
      destination=LUNA_CUBE_BACK;cubeClose(&menu,0);
    } else if(input&(PAD_UP|PAD_LEFT|PAD_L1))cubeMove(&menu,-1);
    else if(input&(PAD_DOWN|PAD_RIGHT|PAD_R1))cubeMove(&menu,1);
    else if(input&PAD_CROSS) {
      if(actions[menu.selected]==LUNA_CUBE_SHUTDOWN)confirmShutdown=1;
      else if(menu.selected<4&&submenus&&submenus[menu.selected].draw&&
              submenus[menu.selected].input) {
        submenu=&submenus[menu.selected];
        if(submenu->begin)submenu->begin(submenu->context);
        menu.submenuOpen=1;menu.submenuDirection=1;menu.submenuAge=0;
      }
      else {destination=actions[menu.selected];cubeClose(&menu,1);}
    }
    *selected=menu.selected;
  }
}

int uiCubePageLoop(const LunaCubePage *page,int *selected) {
  static CubeMenu menu;
  if(!page||page->count<=0||!page->label)return -1;
  int initial=*selected;
  if(initial<0||initial>=page->count)initial=0;
  cubeInit(&menu,uiNowMs(),initial);menu.itemCount=page->count;
  for(int i=0;i<CUBE_COUNT;i++)menu.lightTarget[i]=i==initial%CUBE_COUNT;
  cubeMaterialInit();
  int previous=pollInput(),result=-1;
  for(;;) {
    cubeStep(&menu,uiNowMs());cubeDraw(&menu,page->status,0,page,NULL,NULL);
    int held=pollInput(),input=held&~previous;previous=held;
    if(menu.closing) {
      if(menu.exit>=(menu.entering?1440:500)) {cubeMaterialClose();return result;}
      continue;
    }
    if(input&(PAD_CIRCLE|PAD_TRIANGLE|PAD_START))cubeClose(&menu,0);
    else if(input&(PAD_UP|PAD_L1))cubeMove(&menu,-1);
    else if(input&(PAD_DOWN|PAD_R1))cubeMove(&menu,1);
    else if(page->adjust&&(input&(PAD_LEFT|PAD_RIGHT|PAD_CROSS)))
      page->adjust(page->context,menu.selected,input&PAD_LEFT?-1:1);
    else if(input&PAD_LEFT)cubeMove(&menu,-1);
    else if(input&PAD_RIGHT)cubeMove(&menu,1);
    else if(input&PAD_CROSS) {result=menu.selected;cubeClose(&menu,1);}
    *selected=menu.selected;
  }
}

void uiCubeWorkspaceLoop(const LunaCubeWorkspace *workspace) {
  static CubeMenu menu;
  cubeInit(&menu,uiNowMs(),0);cubeMaterialInit();
  int previous=pollInput();
  for(;;) {
    int selection=workspace->selection(workspace->context);
    if(!menu.closing&&selection>=0&&selection<CUBE_COUNT&&selection!=menu.selected) {
      menu.selected=selection;
      for(int i=0;i<CUBE_COUNT;i++) {
        menu.lightFrom[i]=menu.light[i];menu.lightTarget[i]=i==selection;
        menu.lightAge[i]=0;menu.lightDuration[i]=i==selection?150:200;
      }
    }
    cubeStep(&menu,uiNowMs());cubeDraw(&menu,NULL,0,NULL,workspace,NULL);
    int held=pollInput(),input=held&~previous;previous=held;
    if(menu.closing) {
      if(menu.exit>=500) {cubeMaterialClose();return;}
    } else if(workspace->input(workspace->context,input))cubeClose(&menu,0);
  }
}
