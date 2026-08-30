// model_view.c - load and render a REAL Star Fox Adventures 3D model from the ISO.
//
// MODELS.bin/MODELS.tab format (from the decomp + on-disc inspection): .tab is a
// big-endian u32 per model, offset = e & 0xffffff into MODELS.bin. Each record is a
// small FACEFEED wrapper followed by a ZLB (zlib) block that decompresses to a
// ModelFileHeader + data. Header offsets are model-relative once decompressed:
// vertices@0x28 (stride 6, s16 xyz), displayLists@0xD0 (0x1C-byte entries
// {dlistOff@0, dlistSize@4}), counts vertexCount@0xE4 / displayListCount@0xF5 /
// jointCount@0xF3.
//
// Rather than replay SFA's render-instruction bitstream (which programs the GX vertex
// descriptor), we auto-detect each display list's per-vertex stride and POS index size
// by validating indices against vertexCount - enough to recover the triangle geometry.
// Positions are shaded by face normal; textures and the faithful interpreter come later.
//
//   model_view --iso <path> [--dir animtest] [--index N] [--gfx d3d11|d3d12|vk] [--capture out.bmp]

#include "port/gx_shim.h"
#include "port/dvd_shim.h"
#include "port/stfx_inflate.h"
#include "port/renderer/rhi.h"
#include "port/plat_window.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#if defined(_WIN32)
#include <windows.h>
static void sleep_ms(int ms) { Sleep((DWORD)ms); }
#endif

static unsigned be16(const unsigned char* p){ return (p[0]<<8)|p[1]; }
static int      s16be(const unsigned char* p){ int v=(p[0]<<8)|p[1]; return v>=0x8000?v-0x10000:v; }
static unsigned be32(const unsigned char* p){ return (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3]; }

static RhiBackend parse_backend(const char* s){
    if(!s) return RHI_BACKEND_D3D11;
    if(!strcmp(s,"vk")||!strcmp(s,"vulkan")) return RHI_BACKEND_VULKAN;
    if(!strcmp(s,"d3d12")) return RHI_BACKEND_D3D12;
    return RHI_BACKEND_D3D11;
}

static int findZLB(const unsigned char* p, int maxScan){
    for (int i=0; i+4<=maxScan; ++i)
        if (p[i]=='Z'&&p[i+1]=='L'&&p[i+2]=='B'&&p[i+3]==0) return i;
    return -1;
}
// Decompress the model's ZLB block at bin+off into out. Returns decompressed size (0 on fail).
static size_t decompressModel(const unsigned char* bin, int binSize, unsigned off, unsigned char* out, size_t cap){
    if (off + 0x40 > (unsigned)binSize) return 0;
    int z = findZLB(bin+off, 0x40);
    if (z < 0) return 0;
    const unsigned char* zlb = bin + off + z;
    unsigned usize = be32(zlb+8), csize = be32(zlb+0xc);
    if (usize == 0 || usize > cap) return 0;
    if (off + z + 0x10 + csize > (unsigned)binSize) return 0;
    size_t got = 0;
    // Inflate as much as fits (cap may be a small header probe): success = expected size,
    // or "output full" (1) having produced at least the requested cap.
    int rc = stfx_inflate_zlib(out, cap, zlb+0x10, csize, &got);
    if (rc != 0 && got < cap) return 0;
    if (cap >= usize && got != usize) return 0;
    return got;
}

typedef struct { unsigned off; int vc,dc,jc; unsigned vtxOff,dlOff,usize; } Model;

// Decompress a TEX1 record (id) and return its GC image + dims/format. TEX1 blocks
// are ZLB directly (no FACEFEED wrapper). Fills a caller-provided persistent buffer.
static int loadTex1(unsigned char* tab,int tabSize,unsigned char* bin,int binSize,int id,
                    unsigned char* out,size_t outCap,int* ow,int* oh,int* ofmt,unsigned* oimgOff){
    if(!tab||!bin||id<0||(id*4+4)>tabSize) return 0;
    unsigned e=be32(tab+id*4); int mips=(e>>24)&0x3f; unsigned off=(e&0xffffff)<<1;
    if(mips<1||off==0||off+0x10>(unsigned)binSize) return 0;
    const unsigned char* r=bin+off;
    if(memcmp(r,"ZLB",3)!=0) return 0;
    unsigned usize=be32(r+8),csize=be32(r+0xc);
    if(usize<0x60||usize>outCap||off+0x10+csize>(unsigned)binSize) return 0;
    size_t got=0;
    if(stfx_inflate_zlib(out,outCap,r+0x10,csize,&got)!=0||got!=usize) return 0;
    int w=(int)be16(out+0xA),h=(int)be16(out+0xC),fmt=out[0x16];
    unsigned imgOff=0x60+(unsigned)(int)be32(out+0x50);
    if(w<4||w>1024||h<4||h>1024||imgOff>=usize) return 0;
    *ow=w; *oh=h; *ofmt=fmt; *oimgOff=imgOff;
    return 1;
}

// Geometry emit context (set up per model before decoding display lists).
static const unsigned char* gVtx; static const unsigned char* gTcs;
static float gCtr[3], gScale;
static RhiTexVertex*  gTex;  static int gNTex;  static int gTexCap;
static RhiColorVertex* gCol; static int gNCol;  static int gColCap;

static void getPos(int idx, float o[3]){
    const unsigned char* q=gVtx+idx*6;
    o[0]=((float)s16be(q)-gCtr[0])*gScale;
    o[1]=((float)s16be(q+2)-gCtr[1])*gScale;
    o[2]=((float)s16be(q+4)-gCtr[2])*gScale;
}
static void emitTri(int a,int b,int c,int ta,int tb,int tc,int hasTex){
    int pi[3]={a,b,c}, ti[3]={ta,tb,tc}; float p[3][3];
    for(int k=0;k<3;++k) getPos(pi[k],p[k]);
    if(hasTex){
        if(gNTex+3>gTexCap) return;
        for(int k=0;k<3;++k){
            float u=(float)s16be(gTcs+ti[k]*4)/128.0f, v=(float)s16be(gTcs+ti[k]*4+2)/128.0f;
            gTex[gNTex].x=p[k][0]; gTex[gNTex].y=p[k][1]; gTex[gNTex].z=p[k][2];
            gTex[gNTex].rgba=0xFFFFFFFFu; gTex[gNTex].u=u; gTex[gNTex].v=v; ++gNTex;
        }
    } else {
        if(gNCol+3>gColCap) return;
        float ux=p[1][0]-p[0][0],uy=p[1][1]-p[0][1],uz=p[1][2]-p[0][2];
        float vx=p[2][0]-p[0][0],vy=p[2][1]-p[0][1],vz=p[2][2]-p[0][2];
        float nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx; float nl=sqrtf(nx*nx+ny*ny+nz*nz); if(nl<1e-6f)nl=1;
        float diff=0.30f+0.70f*fabsf((nx*0.4f+ny*0.5f+nz*0.75f)/nl);
        unsigned g=(unsigned)(diff*215)+20; unsigned rgba=g|(g<<8)|(g<<16)|(255u<<24);
        for(int k=0;k<3;++k){ gCol[gNCol].x=p[k][0]; gCol[gNCol].y=p[k][1]; gCol[gNCol].z=p[k][2]; gCol[gNCol].rgba=rgba; ++gNCol; }
    }
}

int main(int argc, char** argv) {
    const char* iso=NULL; const char* dir="animtest"; const char* gfx="d3d11";
    const char* capturePath=NULL; int wantIndex=-1;
    for (int i=1;i<argc;++i){
        if(!strcmp(argv[i],"--iso")&&i+1<argc) iso=argv[++i];
        else if(!strcmp(argv[i],"--dir")&&i+1<argc) dir=argv[++i];
        else if(!strcmp(argv[i],"--index")&&i+1<argc) wantIndex=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--gfx")&&i+1<argc) gfx=argv[++i];
        else if(!strcmp(argv[i],"--capture")&&i+1<argc) capturePath=argv[++i];
    }
    if(!iso){ fprintf(stderr,"usage: model_view --iso <path> [--dir animtest] [--index N]\n"); return 2; }

    dvd_shim_init(iso);
    char tabPath[256], binPath[256];
    snprintf(tabPath,sizeof(tabPath),"%s/MODELS.tab",dir);
    snprintf(binPath,sizeof(binPath),"%s/MODELS.bin",dir);
    int tabSize=0, binSize=0;
    unsigned char* tab=(unsigned char*)loadFileByPath(tabPath,&tabSize,0);
    unsigned char* bin=(unsigned char*)loadFileByPath(binPath,&binSize,0);
    if(!tab||!bin){ fprintf(stderr,"[model] cannot read %s/%s\n",tabPath,binPath); return 3; }
    printf("[model] %s: tab=%d (%d entries) bin=%d\n",dir,tabSize,tabSize/4,binSize);

    // TEX1 archive (for the model's own textures).
    char t1tabP[256], t1binP[256];
    snprintf(t1tabP,sizeof(t1tabP),"%s/TEX1.tab",dir);
    snprintf(t1binP,sizeof(t1binP),"%s/TEX1.bin",dir);
    int t1tabSize=0,t1binSize=0;
    unsigned char* t1tab=(unsigned char*)loadFileByPath(t1tabP,&t1tabSize,0);
    unsigned char* t1bin=(unsigned char*)loadFileByPath(t1binP,&t1binSize,0);

    // Scan: each distinct tab offset -> a ZLB-compressed model. Probe headers.
    static unsigned char probe[1024];
    int nT=tabSize/4;
    Model* models=(Model*)malloc(sizeof(Model)*nT);
    int nModels=0; unsigned last=0xffffffff;
    for (int i=0;i<nT;++i){
        unsigned e=be32(tab+i*4); unsigned off=e&0xffffff;
        if(off==0||off==last||off>=(unsigned)binSize) continue;
        last=off;
        if(off+0x40>(unsigned)binSize) continue;
        int z=findZLB(bin+off,0x40); if(z<0) continue;
        const unsigned char* zlb=bin+off+z;
        unsigned usize=be32(zlb+8), csize=be32(zlb+0xc);
        if(usize<0x100 || off+z+0x10+csize>(unsigned)binSize) continue;
        size_t got=0;
        stfx_inflate_zlib(probe, sizeof(probe), zlb+0x10, csize, &got); // header probe
        if(got<0x100) continue;
        int vc=be16(probe+0xE4), dc=probe[0xF5], jc=probe[0xF3];
        unsigned vtxOff=be32(probe+0x28), dlOff=be32(probe+0xD0);
        if(vc<3||vc>60000||dc<1||dc>250) continue;
        if(vtxOff==0||vtxOff+(unsigned)vc*6>usize) continue;
        if(dlOff==0||dlOff+(unsigned)dc*0x1c>usize) continue;
        models[nModels].off=off; models[nModels].vc=vc; models[nModels].dc=dc; models[nModels].jc=jc;
        models[nModels].vtxOff=vtxOff; models[nModels].dlOff=dlOff; models[nModels].usize=usize;
        ++nModels;
    }
    printf("[model] %d valid models. First few:\n", nModels);
    for (int i=0;i<nModels && i<16;++i)
        printf("   [%2d] verts=%d dls=%d joints=%d usize=%u\n", i, models[i].vc, models[i].dc, models[i].jc, models[i].usize);
    if (nModels==0){ fprintf(stderr,"[model] no valid models found\n"); return 4; }

    int pick=-1;
    if (wantIndex>=0 && wantIndex<nModels) pick=wantIndex;
    else { long best=-1; for(int i=0;i<nModels;++i){ if(models[i].jc>1) continue; if(models[i].vc>best){best=models[i].vc;pick=i;} } }
    if (pick<0) pick=0;
    Model M=models[pick];
    printf("[model] showing [%d] verts=%d dls=%d joints=%d\n", pick, M.vc, M.dc, M.jc);

    // Full-decompress the chosen model.
    unsigned char* m=(unsigned char*)malloc(M.usize);
    if(!m || decompressModel(bin,binSize,M.off,m,M.usize)!=M.usize){ fprintf(stderr,"[model] decompress failed\n"); return 5; }
    const unsigned char* vtx=m+M.vtxOff;
    unsigned tcOff=be32(m+0x34); const unsigned char* tcs=m+tcOff; int tcCount=(int)be16(m+0xEA);
    int tcValid=(tcOff>0 && tcOff+(unsigned)tcCount*4<=M.usize && tcCount>0);

    // Load the model's first texture from TEX1.
    static unsigned char texBuf[4*1024*1024];
    int texW=0,texH=0,texFmt=0; unsigned texImgOff=0; int haveTex=0;
    {
        unsigned tidOff=be32(m+0x20); int texCount=m[0xF2];
        if (tidOff>0 && tidOff+4<=M.usize && texCount>0){
            int texId=(int)be32(m+tidOff);
            if (loadTex1(t1tab,t1tabSize,t1bin,t1binSize,texId,texBuf,sizeof texBuf,&texW,&texH,&texFmt,&texImgOff)){
                haveTex=1; printf("[model] texture id=%d %dx%d fmt=%d\n", texId, texW, texH, texFmt);
            }
        }
    }

    // Bounding box for auto-normalization.
    float minb[3]={1e9f,1e9f,1e9f}, maxb[3]={-1e9f,-1e9f,-1e9f};
    for (int i=0;i<M.vc;++i) for(int c=0;c<3;++c){ float v=(float)s16be(vtx+i*6+c*2); if(v<minb[c])minb[c]=v; if(v>maxb[c])maxb[c]=v; }
    float ext=1e-6f;
    for (int c=0;c<3;++c){ gCtr[c]=(minb[c]+maxb[c])*0.5f; float e=maxb[c]-minb[c]; if(e>ext)ext=e; }
    gScale=1.6f/ext; gVtx=vtx; gTcs=tcs;

    static RhiTexVertex  texTris[400000]; static RhiColorVertex colTris[400000];
    gTex=texTris; gTexCap=400000; gNTex=0; gCol=colTris; gColCap=400000; gNCol=0;

    for (int d=0; d<M.dc; ++d){
        const unsigned char* de=m+M.dlOff+d*0x1c;
        unsigned dlOff=be32(de+0); int dlSize=(int)be16(de+4);
        if(dlOff==0 || dlOff+dlSize>M.usize) continue;
        const unsigned char* dl=m+dlOff;
        // auto-detect POS (index at offset 0)
        int posSz=0, stride=0;
        for (int ps=2; ps>=1 && stride==0; --ps){
            for (int st=ps; st<=48; ++st){
                int p=0, ok=1, saw=0;
                while (p<dlSize){ unsigned op=dl[p]; if(op==0){++p;continue;} if((op&0x80)==0){ok=0;break;}
                    if(p+3>dlSize){ok=0;break;} int cnt=be16(dl+p+1); p+=3;
                    if(cnt==0||p+cnt*st>dlSize){ok=0;break;}
                    for(int v=0;v<cnt;++v){ int idx=(ps==2)?be16(dl+p+v*st):dl[p+v*st]; if(idx>=M.vc){ok=0;break;} }
                    if(!ok)break; p+=cnt*st; saw=1; }
                if(ok&&saw){ posSz=ps; stride=st; break; }
            }
        }
        if(stride==0) continue;
        // auto-detect TEX0 (last attribute whose indices are all < tcCount)
        int texOff=-1, texSz=0;
        if (haveTex && tcValid){
            for (int ts=2; ts>=1 && texOff<0; --ts){
                for (int to=stride-ts; to>=posSz; --to){
                    int p=0, ok=1;
                    while (p<dlSize){ unsigned op=dl[p]; if(op==0){++p;continue;} if((op&0x80)==0){ok=0;break;}
                        int cnt=be16(dl+p+1); p+=3;
                        for(int v=0;v<cnt;++v){ int idx=(ts==2)?be16(dl+p+v*stride+to):dl[p+v*stride+to]; if(idx>=tcCount){ok=0;break;} }
                        if(!ok)break; p+=cnt*stride; }
                    if(ok){ texOff=to; texSz=ts; break; }
                }
            }
        }
        int dlHasTex = (texOff>=0);
        int p=0;
        while (p<dlSize){
            unsigned op=dl[p]; if(op==0){++p;continue;} if((op&0x80)==0) break;
            int prim=op&0xf8; int cnt=be16(dl+p+1); p+=3;
            int n=cnt>2048?2048:cnt; static int pidx[2048], tidx[2048];
            for(int v=0;v<n;++v){
                pidx[v]=(posSz==2)?be16(dl+p+v*stride):dl[p+v*stride];
                tidx[v]=dlHasTex?((texSz==2)?be16(dl+p+v*stride+texOff):dl[p+v*stride+texOff]):0;
            }
            p+=cnt*stride;
            #define TRI(a,b,c) emitTri(pidx[a],pidx[b],pidx[c], tidx[a],tidx[b],tidx[c], dlHasTex)
            if(prim==0x90){ for(int v=0;v+3<=n;v+=3) TRI(v,v+1,v+2); }
            else if(prim==0x98){ for(int v=2;v<n;++v){ if(v&1) TRI(v-1,v-2,v); else TRI(v-2,v-1,v); } }
            else if(prim==0xa0){ for(int v=2;v<n;++v) TRI(0,v-1,v); }
            else if(prim==0x80){ for(int v=0;v+4<=n;v+=4){ TRI(v,v+1,v+2); TRI(v,v+2,v+3); } }
            #undef TRI
        }
    }
    printf("[model] built %d textured + %d flat triangles\n", gNTex/3, gNCol/3);
    if (gNTex==0 && gNCol==0){ fprintf(stderr,"[model] no geometry decoded\n"); return 6; }

    const int W=1280,H=720;
    char title[160];
    snprintf(title,sizeof(title),"Stairfax Temperatures - %s MODEL [%d] %d tris [%s]", dir, pick, (gNTex+gNCol)/3, gfx);
    PlatWindow* win=plat_window_create(title,W,H);
    RhiCreateInfo ci={0}; ci.backend=parse_backend(gfx); ci.windowHandle=plat_window_native_handle(win);
    ci.width=W; ci.height=H; ci.vsync=true; ci.appName="Stairfax Temperatures";
    RhiInstance* rhi=rhi_create(&ci);
    if(!rhi){ fprintf(stderr,"[model] rhi_create failed\n"); return 7; }
    RhiSwapchain* sc=rhi_swapchainCreate(rhi,ci.windowHandle,W,H,ci.vsync);

    gx_shim_setRhi(rhi, sc);
    GXInit_host();
    GXTexObj modelTex;
    if (haveTex) GXInitTexObj(&modelTex, texBuf+texImgOff, (uint16_t)texW, (uint16_t)texH, texFmt, 0,0,0);

    int captured=0, frames=100000, captureFrame=40;
    for (int f=0; f<frames; ++f){
        if(!plat_window_pump(win)) break;
        rhi_beginFrame(rhi);
        rhi_clear(rhi, 0.10f,0.11f,0.13f, 1.0f);
        float a=(float)f*0.02f, cS=cosf(a), sS=sinf(a);
        float ys=1.0f/tanf(0.9f), xs=ys/((float)W/H), zn=0.05f, zf=50.0f, dist=3.0f;
        float mv[3][4]={{cS,0,sS,0},{0,1,0,0},{-sS,0,cS,dist}};
        float proj[4][4]={{xs,0,0,0},{0,ys,0,0},{0,0,zf/(zf-zn),-zn*zf/(zf-zn)},{0,0,1,0}};
        float P[4][4]; for(int r=0;r<3;++r)for(int c=0;c<4;++c)P[r][c]=mv[r][c]; P[3][0]=P[3][1]=P[3][2]=0; P[3][3]=1;
        float mvp[16]; for(int r=0;r<4;++r)for(int c=0;c<4;++c){ float s=0; for(int k=0;k<4;++k)s+=proj[r][k]*P[k][c]; mvp[r*4+c]=s; }
        rhi_setColorTransform(rhi, mvp);
        if (haveTex && gNTex>0){ GXLoadTexObj(&modelTex, GX_TEXMAP0); rhi_drawTextured(rhi, texTris, (uint32_t)gNTex); }
        if (gNCol>0) rhi_drawColored(rhi, colTris, (uint32_t)gNCol);
        rhi_endFrame(rhi);
        rhi_present(rhi, sc);
        if (capturePath && !captured && f>=captureFrame){ sleep_ms(60); plat_window_capture_bmp(win,capturePath); printf("[model] captured -> %s\n",capturePath); captured=1; frames=f+3; }
    }

    free(tab); free(bin); free(models); free(m);
    rhi_swapchainDestroy(rhi,sc); rhi_destroy(rhi); plat_window_destroy(win); dvd_shim_shutdown();
    return 0;
}
