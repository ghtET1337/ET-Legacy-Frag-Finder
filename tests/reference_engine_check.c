// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic fixture generator and independent ET: Legacy message-decoder checker.
// Build against the official engine msg.c/huffman.c; see fixtures/README.md.
#include "q_shared.h"
#include "qcommon.h"
#include <stdarg.h>
#include <stdint.h>

cvar_t shownetStorage; cvar_t *cl_shownet = &shownetStorage;
void QDECL Com_Printf(const char *fmt, ...) { (void)fmt; }
void QDECL Com_Error(int level, const char *fmt, ...) {
    va_list args; va_start(args,fmt); vfprintf(stderr,fmt,args); va_end(args); fputc('\n',stderr); exit(2);
}
void Q_strncpyz_f(char *dest, const char *src, int n, const char *func, const char *file, int line) { if (n) { strncpy(dest,src,n-1);dest[n-1]=0; } }
void Q_SafeNetString(char* s, size_t len, qboolean strip) { while (*s) { if (*s == '%') *s = '.'; ++s; } }
static void rawLong(FILE* f,int v) { unsigned u=(unsigned)v;for(int i=0;i<4;i++)fputc((u>>(i*8))&255,f); }
static int readLong(FILE* f,int* value) { unsigned u=0; for(int i=0;i<4;i++){int c=fgetc(f);if(c==EOF)return 0;u|=(unsigned)c<<(i*8);}*value=(int)u;return 1; }
static void packet(FILE* f,int seq,msg_t* m) {rawLong(f,seq);rawLong(f,m->cursize);fwrite(m->data,1,m->cursize,f);}
static void cs(msg_t* m,int index,const char* value){MSG_WriteByte(m,svc_configstring);MSG_WriteShort(m,index);MSG_WriteBigString(m,value);}
static void command(msg_t*m,int seq,const char*value){MSG_WriteByte(m,svc_serverCommand);MSG_WriteLong(m,seq);MSG_WriteString(m,value);}
static entityState_t baseline[1024];
static playerState_t players[102];
static entityState_t ents[102][3];
static void generate(const char* path) {
 FILE*f=fopen(path,"wb");if(!f)exit(2);byte data[MAX_MSGLEN];msg_t m;entityState_t zero={0};
 baseline[100].number=100;baseline[100].modelindex=3;baseline[100].pos.trBase[0]=12.5;
 MSG_Init(&m,data,sizeof(data));MSG_Bitstream(&m);MSG_WriteLong(&m,17);MSG_WriteByte(&m,svc_gamestate);MSG_WriteLong(&m,50);
 cs(&m,0,"\\mapname\\oasis\\gamename\\legacy\\mod_version\\2.84\\protocol\\84");
 cs(&m,1,"\\sv_serverid\\100000\\sv_pure\\0");cs(&m,11,"100000");cs(&m,21,"\\gamestate\\0");
 cs(&m,689,"\\n\\^1Tester\\t\\1");cs(&m,690,"\\n\\Enemy\\t\\2");
 MSG_WriteByte(&m,svc_baseline);MSG_WriteDeltaEntity(&m,&zero,&baseline[100],qtrue);
 MSG_WriteByte(&m,svc_EOF);MSG_WriteLong(&m,0);MSG_WriteLong(&m,123456789);MSG_WriteByte(&m,svc_EOF);packet(f,100,&m);
 int cmdseq=50;
 for(int i=0;i<=100;i++){
  playerState_t *p=&players[i];p->commandTime=100000+i*100;p->clientNum=0;p->pm_time=-123;p->weaponTime=-300;p->viewheight=-12;
  p->origin[0]=12.25f+i*0.25f;p->origin[1]=(float)(-i);p->velocity[2]=0.125f;p->viewangles[1]=i*3.25f;p->weapon=3;p->eFlags=3;
  p->stats[0]=100-i;p->stats[15]=-17;p->persistant[3]=i;p->holdable[15]=7;p->powerups[15]=111111+i;
  for(int g=0;g<4;g++){p->ammo[g*16+15]=300-i;p->ammoclip[g*16+3]=30-i%30;}
  ents[i][0]=baseline[100];ents[i][0].pos.trBase[0]+=i*0.5f;ents[i][0].apos.trBase[1]=-i*1.125f;
  ents[i][1].number=200;ents[i][1].eType=130;ents[i][1].otherEntityNum=1;ents[i][1].otherEntityNum2=0;ents[i][1].weapon=3;ents[i][1].eventParm=9;ents[i][1].time=i;
  ents[i][2].number=300;ents[i][2].modelindex=7;ents[i][2].pos.trBase[2]=31.75f;
  int base=(i>=3 && i%10!=0)?i-3:-1;
  MSG_Init(&m,data,sizeof(data));MSG_Bitstream(&m);MSG_WriteLong(&m,17);
  if(i==5)command(&m,++cmdseq,"cs 690 \"\\n\\NewEnemy\\t\\2\"");
  if(i==20)command(&m,++cmdseq,"bcs0 700 \"first-\"");
  if(i==21)command(&m,++cmdseq,"bcs1 700 \"middle-\"");
  if(i==23)command(&m,++cmdseq,"bcs2 700 \"last\"");
  if(i==30)command(&m,++cmdseq,"print \"100 percent ready\"");
  if(i==40)command(&m,++cmdseq,"cs 690 \"\\n\\FinalEnemy\\t\\2\"");
  if(i==41)command(&m,cmdseq,"cs 690 \"\\n\\FinalEnemy\\t\\2\"");
  MSG_WriteByte(&m,svc_snapshot);MSG_WriteLong(&m,100000+i*100);MSG_WriteByte(&m,base<0?0:i-base);MSG_WriteByte(&m,4);
  MSG_WriteByte(&m,4);MSG_WriteByte(&m,0);MSG_WriteByte(&m,255);MSG_WriteByte(&m,85);MSG_WriteByte(&m,i);
  MSG_WriteDeltaPlayerstate(&m,base<0?NULL:&players[base],p);
  // entity 200 exists for a single snapshot at 25, 45, 65. Entity 300
  // appears at 15, disappears at 50, then returns at 70.
  for(int e=0;e<3;e++){
   int number=100+e*100;
   int present=e==0 || (e==1?(i==25||i==45||i==65):(i>=15&&i<50)||i>=70);
   int oldpresent=base>=0 && (e==0 || (e==1?(base==25||base==45||base==65):(base>=15&&base<50)||base>=70));
   if(present)MSG_WriteDeltaEntity(&m,oldpresent?&ents[base][e]:&baseline[number],&ents[i][e],!oldpresent);
   else if(oldpresent)MSG_WriteDeltaEntity(&m,&ents[base][e],NULL,qtrue);
  }
  MSG_WriteBits(&m,1023,10);MSG_WriteByte(&m,svc_EOF);packet(f,101+i,&m);
 }
 rawLong(f,-1);rawLong(f,-1);fclose(f);
}
typedef struct {int valid,seq,time,flags,n;byte area[32];playerState_t player;entityState_t entities[1024];} frame;
static frame frames[32];
static uint64_t hash(const void* data,size_t size){const byte*p=data;uint64_t h=1469598103934665603ULL;for(size_t i=0;i<size;i++){h^=p[i];h*=1099511628211ULL;}return h;}
static void inspect(const char*path){
 FILE*f=fopen(path,"rb");if(!f)exit(2);byte data[MAX_MSGLEN];msg_t m;int seq,len;int cmdseq=-1;int first=1;
 memset(baseline,0,sizeof(baseline));
 while(readLong(f,&seq)&&readLong(f,&len)){
  if(len==-1)break;if(len<0||len>MAX_MSGLEN)exit(3);if(fread(data,1,len,f)!=(size_t)len)exit(3);
  MSG_Init(&m,data,sizeof(data));m.cursize=len;MSG_BeginReading(&m);MSG_ReadLong(&m);
  for(;;){int cmd=MSG_ReadByte(&m);if(cmd==svc_EOF)break;if(m.readcount>len)exit(3);
   if(cmd==svc_nop)continue;
   if(cmd==svc_gamestate){cmdseq=MSG_ReadLong(&m);for(;;){int k=MSG_ReadByte(&m);if(k==svc_EOF)break;
    if(k==svc_configstring){int index=MSG_ReadShort(&m);printf("CS %d %s\n",index,MSG_ReadBigString(&m));}
    else if(k==svc_baseline){entityState_t zero={0};int n=MSG_ReadBits(&m,10);MSG_ReadDeltaEntity(&m,&zero,&baseline[n],n);}
    else exit(3);
   }int client=MSG_ReadLong(&m),checksum=MSG_ReadLong(&m);printf("GAME %d %d\n",client,checksum);}
   else if(cmd==svc_serverCommand){int n=MSG_ReadLong(&m);char*txt=MSG_ReadString(&m);if(n>cmdseq){printf("CMD %s\n",txt);cmdseq=n;}}
   else if(cmd==svc_snapshot){static frame next;memset(&next,0,sizeof(next));next.seq=seq;next.time=MSG_ReadLong(&m);int delta=MSG_ReadByte(&m);next.flags=MSG_ReadByte(&m);
    frame*old=NULL;if(delta){old=&frames[(seq-delta)&31];if(!old->valid||old->seq!=seq-delta){fprintf(stderr,"MISSING DELTA\n");exit(3);}}
    if(first&&delta){fprintf(stderr,"FIRST SNAPSHOT IS DELTA\n");exit(3);}first=0;
    int area=MSG_ReadByte(&m);if(area>32)exit(3);for(int a=0;a<area;a++)next.area[a]=MSG_ReadByte(&m);
    MSG_ReadDeltaPlayerstate(&m,old?&old->player:NULL,&next.player);
    int index=0;for(;;){int n=MSG_ReadBits(&m,10);if(n==1023)break;
      while(old&&index<old->n&&old->entities[index].number<n)next.entities[next.n++]=old->entities[index++];
      entityState_t e;entityState_t*base=&baseline[n];if(old&&index<old->n&&old->entities[index].number==n)base=&old->entities[index++];
      MSG_ReadDeltaEntity(&m,base,&e,n);if(e.number!=1023)next.entities[next.n++]=e;
    }
    while(old&&index<old->n)next.entities[next.n++]=old->entities[index++];
    if(m.readcount>len)exit(3);next.valid=1;frames[seq&31]=next;
    // Hash every decoded state byte, not just kills or visible UI fields.
    printf("SNAP %d %d %llu %llu %d %llu\n",next.time,next.flags,(unsigned long long)hash(next.area,32),
     (unsigned long long)hash(&next.player,sizeof(next.player)),next.n,(unsigned long long)hash(next.entities,next.n*sizeof(entityState_t)));
   }else{fprintf(stderr,"BAD OPCODE %d\n",cmd);exit(3);}
  }
 }
 fclose(f);
}
int main(int argc,char**argv){if(argc!=3)return 1;if(!strcmp(argv[1],"generate"))generate(argv[2]);else inspect(argv[2]);return 0;}
