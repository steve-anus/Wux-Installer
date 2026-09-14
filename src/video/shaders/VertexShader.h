/****************************************************************************
 * Copyright (C) 2015 Dimok
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ****************************************************************************/
#ifndef VERTEX_SHADER_H
#define VERTEX_SHADER_H

#include <string.h>
#include "Shader.h"

class VertexShader : public Shader
{
public:
    VertexShader(u32 numAttr)
        : attributesCount( numAttr )
        , attributes( new GX2AttribStream[attributesCount] )
        , vertexShader( (GX2VertexShader*) memalign(0x40, sizeof(GX2VertexShader)) )
    {
        if(vertexShader)
        {
            memset(vertexShader, 0, sizeof(GX2VertexShader));
            vertexShader->mode = GX2_SHADER_MODE_UNIFORM_REGISTER;
        }
    }

    virtual ~VertexShader() {
        delete [] attributes;

        if(vertexShader)
        {
            if(vertexShader->program)
                free(vertexShader->program);

            for(u32 i = 0; i < vertexShader->uniformBlockCount; i++)
                free((void*)vertexShader->uniformBlocks[i].name);

            if(vertexShader->uniformBlocks)
                free((void*)vertexShader->uniformBlocks);

            for(u32 i = 0; i < vertexShader->uniformVarCount; i++)
                free((void*)vertexShader->uniformVars[i].name);

            if(vertexShader->uniformVars)
                free((void*)vertexShader->uniformVars);

            if(vertexShader->initialValues)
                free((void*)vertexShader->initialValues);

            for(u32 i = 0; i < vertexShader->samplerVarCount; i++)
                free((void*)vertexShader->samplerVars[i].name);

            if(vertexShader->samplerVars)
                free((void*)vertexShader->samplerVars);

            for(u32 i = 0; i < vertexShader->attribVarCount; i++)
                free((void*)vertexShader->attribVars[i].name);

            if(vertexShader->attribVars)
                free((void*)vertexShader->attribVars);

            if(vertexShader->loopVars)
                free((void*)vertexShader->loopVars);

            free(vertexShader);
        }
    }

    void setProgram(const u32 * program, const u32 & programSize, const u32 * regs, const u32 & regsSize)
    {
        if(!vertexShader)
            return;

        //! this must be moved into an area where the graphic engine has access to and must be aligned to 0x100
        if(vertexShader->program)
            free(vertexShader->program);

        vertexShader->size = programSize;
        vertexShader->program = (u8*) memalign(GX2_SHADER_ALIGNMENT, vertexShader->size);
        if(vertexShader->program)
        {
            memcpy(vertexShader->program, program, vertexShader->size);
            GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, vertexShader->program, vertexShader->size);
        }

        //! never copy past the register block, whatever the caller measured
        u32 copySize = (regsSize > sizeof(vertexShader->regs)) ? (u32) sizeof(vertexShader->regs) : regsSize;
        memcpy(&vertexShader->regs, regs, copySize);
    }

    void addUniformVar(const GX2UniformVar & var)
    {
        if(!vertexShader)
            return;

        u32 idx = vertexShader->uniformVarCount;

        GX2UniformVar* newVar = (GX2UniformVar*) malloc((vertexShader->uniformVarCount + 1) * sizeof(GX2UniformVar));
        if(newVar)
        {
            if(vertexShader->uniformVarCount > 0)
            {
                memcpy(newVar, vertexShader->uniformVars, vertexShader->uniformVarCount * sizeof(GX2UniformVar));
                free(vertexShader->uniformVars);
            }
            vertexShader->uniformVars = newVar;

            memcpy(vertexShader->uniformVars + idx, &var, sizeof(GX2UniformVar));
            vertexShader->uniformVars[idx].name = (char*) malloc(strlen(var.name) + 1);
            strcpy((char*)vertexShader->uniformVars[idx].name, var.name);

            vertexShader->uniformVarCount++;
        }
    }

    void addAttribVar(const GX2AttribVar & var)
    {
        if(!vertexShader)
            return;

        u32 idx = vertexShader->attribVarCount;

        GX2AttribVar* newVar = (GX2AttribVar*) malloc((vertexShader->attribVarCount + 1) * sizeof(GX2AttribVar));
        if(newVar)
        {
            if(vertexShader->attribVarCount > 0)
            {
                memcpy(newVar, vertexShader->attribVars, vertexShader->attribVarCount * sizeof(GX2AttribVar));
                free(vertexShader->attribVars);
            }
            vertexShader->attribVars = newVar;

            memcpy(vertexShader->attribVars + idx, &var, sizeof(GX2AttribVar));
            vertexShader->attribVars[idx].name = (char*) malloc(strlen(var.name) + 1);
            strcpy((char*)vertexShader->attribVars[idx].name, var.name);

            vertexShader->attribVarCount++;
        }
    }

    static inline void setAttributeBuffer(u32 bufferIdx, u32 bufferSize, u32 stride, const void * buffer) {
        GX2SetAttribBuffer(bufferIdx, bufferSize, stride, (void*)buffer);
    }

    GX2VertexShader *getVertexShader() const {
        return vertexShader;
    }

    void setShader(void) const {
        GX2SetVertexShader(vertexShader);
    }

    GX2AttribStream * getAttributeBuffer(u32 idx = 0) const {
        if(idx >= attributesCount) {
            return NULL;
        }
        return &attributes[idx];
    }
    u32 getAttributesCount() const {
        return attributesCount;
    }

    //! GX2 addresses uniforms in 4-byte words and uploads whole 16-byte constant
    //! slots, so the count is a word count and a multiple of 4. On hardware a
    //! count that is not a multiple of 4 is dropped silently: no error, no log,
    //! just geometry drawn with a stale transform. Every uniform in these shaders
    //! owns one slot, so the count is fixed here rather than repeated per call.
    static void setUniform4(u32 location, const void * reg) {
        if((location & 3) != 0) {
            log_printf("VertexShader::setUniform4: uniform slot %u is not 16-byte aligned\n", location);
            return;
        }
        GX2SetVertexUniformReg(location, 4, reg);
    }
protected:
    u32 attributesCount;
    GX2AttribStream *attributes;
    GX2VertexShader *vertexShader;
};

#endif // VERTEX_SHADER_H
