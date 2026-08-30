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

    // Bounding box for auto-normalization.
    float minb[3]={1e9f,1e9f,1e9f}, maxb[3]={-1e9f,-1e9f,-1e9f};
    for (int i=0;i<M.vc;++i) for(int c=0;c<3;++c){ float v=(float)s16be(vtx+i*6+c*2); if(v<minb[c])minb[c]=v; if(v>maxb[c])maxb[c]=v; }
    float ctr[3], ext=1e-6f;
    for (int c=0;c<3;++c){ ctr[c]=(minb[c]+maxb[c])*0.5f; float e=maxb[c]-minb[c]; if(e>ext)ext=e; }
    float scale=1.6f/ext;

    // Build a triangle list (positions) from the display lists.
    static RhiColorVertex tris[400000];
    int nTris=0;
    for (int d=0; d<M.dc && nTris<400000-6; ++d){
        const unsigned char* de=m+M.dlOff+d*0x1c;
        unsigned dlOff=be32(de+0); int dlSize=(int)be16(de+4);
        if(dlOff==0 || dlOff+dlSize>M.usize) continue;
        const unsigned char* dl=m+dlOff;
        // auto-detect (posIdxSize, stride)
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
        int p=0;
        while (p<dlSize && nTris<400000-6){
            unsigned op=dl[p]; if(op==0){++p;continue;} if((op&0x80)==0) break;
            int prim=op&0xf8; int cnt=be16(dl+p+1); p+=3;
            int n=cnt>2048?2048:cnt; static int idxb[2048];
            for(int v=0;v<n;++v) idxb[v]=(posSz==2)?be16(dl+p+v*stride):dl[p+v*stride];
            p+=cnt*stride;
            #define EMIT3(a,b,c) do{ int ii[3]={a,b,c}; float pv[3][3]; \
                for(int k=0;k<3;++k){ const unsigned char* q=vtx+ii[k]*6; \
                    pv[k][0]=((float)s16be(q)-ctr[0])*scale; pv[k][1]=((float)s16be(q+2)-ctr[1])*scale; pv[k][2]=((float)s16be(q+4)-ctr[2])*scale; } \
                float ux=pv[1][0]-pv[0][0],uy=pv[1][1]-pv[0][1],uz=pv[1][2]-pv[0][2]; \
                float vx=pv[2][0]-pv[0][0],vy=pv[2][1]-pv[0][1],vz=pv[2][2]-pv[0][2]; \
                float nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx; float nl=sqrtf(nx*nx+ny*ny+nz*nz); if(nl<1e-6f)nl=1; \
                float diff=0.30f+0.70f*fabsf((nx*0.4f+ny*0.5f+nz*0.75f)/nl); \
                unsigned g=(unsigned)(diff*215)+20; unsigned rgba=g|(g<<8)|(g<<16)|(255u<<24); \
                for(int k=0;k<3;++k){ tris[nTris].x=pv[k][0]; tris[nTris].y=pv[k][1]; tris[nTris].z=pv[k][2]; tris[nTris].rgba=rgba; ++nTris; } }while(0)
            if(prim==0x90){ for(int v=0;v+3<=n&&nTris<400000-6;v+=3) EMIT3(idxb[v],idxb[v+1],idxb[v+2]); }
            else if(prim==0x98){ for(int v=2;v<n&&nTris<400000-6;++v){ if(v&1) EMIT3(idxb[v-1],idxb[v-2],idxb[v]); else EMIT3(idxb[v-2],idxb[v-1],idxb[v]); } }
            else if(prim==0xa0){ for(int v=2;v<n&&nTris<400000-6;++v) EMIT3(idxb[0],idxb[v-1],idxb[v]); }
            else if(prim==0x80){ for(int v=0;v+4<=n&&nTris<400000-6;v+=4){ EMIT3(idxb[v],idxb[v+1],idxb[v+2]); EMIT3(idxb[v],idxb[v+2],idxb[v+3]); } }
            #undef EMIT3
        }
    }
    printf("[model] built %d triangles\n", nTris/3);
    if (nTris==0){ fprintf(stderr,"[model] no geometry decoded\n"); return 6; }

    const int W=1280,H=720;
    char title[160];
    snprintf(title,sizeof(title),"Stairfax Temperatures - %s MODEL [%d] %d tris [%s]", dir, pick, nTris/3, gfx);
    PlatWindow* win=plat_window_create(title,W,H);
    RhiCreateInfo ci={0}; ci.backend=parse_backend(gfx); ci.windowHandle=plat_window_native_handle(win);
    ci.width=W; ci.height=H; ci.vsync=true; ci.appName="Stairfax Temperatures";
    RhiInstance* rhi=rhi_create(&ci);
    if(!rhi){ fprintf(stderr,"[model] rhi_create failed\n"); return 7; }
    RhiSwapchain* sc=rhi_swapchainCreate(rhi,ci.windowHandle,W,H,ci.vsync);

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
        rhi_drawColored(rhi, tris, (uint32_t)nTris);
        rhi_endFrame(rhi);
        rhi_present(rhi, sc);
        if (capturePath && !captured && f>=captureFrame){ sleep_ms(60); plat_window_capture_bmp(win,capturePath); printf("[model] captured -> %s\n",capturePath); captured=1; frames=f+3; }
    }

    free(tab); free(bin); free(models); free(m);
    rhi_swapchainDestroy(rhi,sc); rhi_destroy(rhi); plat_window_destroy(win); dvd_shim_shutdown();
    return 0;
}
