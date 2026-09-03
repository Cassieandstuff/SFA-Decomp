// game_model_swap.c - byte-swap a decompressed ModelFileHeader from big-endian (disc)
// to host order, so the recompiled ObjModel_RelocateModelData (which reads the header's
// offset fields natively and adds the base pointer) produces valid pointers instead of
// wild ones. Uses the real struct so fields swap by name (no hand-counted offsets).
// Leaf geometry (vertices/DLs/joints) is NOT swapped here - that's the render follow-up;
// this makes the LOAD + relocation succeed. See port/byteswap.h, [[port-model]].

#include "main/model.h"
#include "port/byteswap.h"

void bswapModelFileHeader(void* p) {
    ModelFileHeader* h = (ModelFileHeader*)p;
    BE16(h->flags); BE16(h->modelId); BE32(h->dataSize);
    // relocated pointer/offset fields (RelocateModelData reads these as u32 + adds base)
    BE32(h->unk18); BE32(h->unk1C); BE32(h->textureIds);
    BE32(h->vertices); BE32(h->normals); BE32(h->colors); BE32(h->texCoords);
    BE32(h->renderOps); BE32(h->jointData); BE32(h->jointBlendData);
    BEF32(h->vertexAnimPivot[0]); BEF32(h->vertexAnimPivot[1]); BEF32(h->vertexAnimPivot[2]);
    BEF32(h->vertexAnimScaleDivisor);
    BE32(h->extraJointDefs); BE32(h->hitVolumes);
    BE32(h->collisionTriangles); BE32(h->collisionBlocks);
    BE32(h->animationModelPtrs); BE32(h->animationDataSection); BE32(h->animationHeaderBuffer);
    for (int i = 0; i < 8; ++i) BE16(h->animGroupBaseIndices[i]);
    BE32(h->animationDataFileOffset); BE16(h->headerSize);
    BE16(h->vertexAnimCount);
    BE32(h->vertexAnimEntriesRaw); BE32(h->vertexAnimEntries); BE32(h->vertexAnimBase);
    BE16(h->blendAnimCount);
    BE32(h->blendAnimEntriesRaw); BE32(h->blendAnimEntries); BE32(h->blendAnimBase);
    BE32(h->displayLists); BE32(h->instrs); BE16(h->instrsBitLenWords);
    BE32(h->morphTargetPtrs);
    BE16(h->cullDistance); BE16(h->shaderFlags);
    BE16(h->vertexCount); BE16(h->normalCount); BE16(h->colorCount); BE16(h->texCoordCount);
    BE16(h->animationCount); BE16(h->collisionBlockCount);
    // 0xF2..0xFB are u8 counts (textureCount/jointCount/...); no swap.
}

// Swap the leaf tables ObjModel_ResolveRenderOpTextures reads natively during load: the
// renderOps (Shader, 0x44 each) and the textureIds int array. Call AFTER the header swap
// but BEFORE relocation - the header's renderOps/textureIds fields are still byte OFFSETS.
static void swapShader(Shader* s) {
    BE32(s->reg1Texture); BE32(s->reg2Texture); BE32(s->textureId); BE32(s->unk1C);
    BE32(s->layers[0].textureIndex); BE32(s->layers[1].textureIndex);
    BE32(s->auxTextureIndex); BE32(s->indTextureId); BE32(s->flags);
}
void bswapModelRenderOps(void* m) {
    ModelFileHeader* h = (ModelFileHeader*)m;
    unsigned roOff = *(unsigned*)&h->renderOps;   // still an offset (pre-relocation)
    if (roOff) {
        Shader* ops = (Shader*)((unsigned char*)m + roOff);
        for (int j = 0; j < h->renderOpCount; ++j) swapShader(&ops[j]);
    }
    unsigned tidOff = *(unsigned*)&h->textureIds;
    if (tidOff) {
        int* tids = (int*)((unsigned char*)m + tidOff);
        for (int i = 0; i < h->textureCount; ++i) beFix32(&tids[i]);
    }
    // DL table entries: de+0 is a u32 offset (BE leaf) that RelocateModelData reads
    // natively and adds the base to - swap so it relocates to a valid pointer.
    unsigned dlOff = *(unsigned*)&h->displayLists;
    if (dlOff) {
        unsigned char* dt = (unsigned char*)m + dlOff;
        int total = h->displayListCount + h->shadowDisplayListCount;
        for (int i = 0; i < total; ++i) beFix32(dt + i * 0x1c);
    }
    // morphTargetPtrs entries (u32 offsets, also relocated per-entry).
    unsigned mtOff = *(unsigned*)&h->morphTargetPtrs;
    if (mtOff) {
        unsigned char* mt = (unsigned char*)m + mtOff;
        for (int i = 0; i < h->morphTargetCount; ++i) beFix32(mt + i * 4);
    }
}
