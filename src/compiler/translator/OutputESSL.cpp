//
// Copyright (c) 2002-2013 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

#include "compiler/translator/OutputESSL.h"

#include "angle_gl.h"
#include "compiler/translator/Symbol.h"

namespace sh
{

TOutputESSL::TOutputESSL(TInfoSinkBase &objSink,
                         ShArrayIndexClampingStrategy clampingStrategy,
                         ShHashFunction64 hashFunction,
                         NameMap &nameMap,
                         TSymbolTable *symbolTable,
                         sh::GLenum shaderType,
                         int shaderVersion,
                         bool forceHighp,
                         ShCompileOptions compileOptions)
    : TOutputGLSLBase(objSink,
                      clampingStrategy,
                      hashFunction,
                      nameMap,
                      symbolTable,
                      shaderType,
                      shaderVersion,
                      SH_ESSL_OUTPUT,
                      compileOptions),
      mForceHighp(forceHighp)
{
}

bool TOutputESSL::writeVariablePrecision(TPrecision precision)
{
    if (precision == EbpUndefined)
        return false;

    TInfoSinkBase &out = objSink();
    if (mForceHighp)
        out << getPrecisionString(EbpHigh);
    else
        out << getPrecisionString(precision);
    return true;
}

void TOutputESSL::visitSymbol(TIntermSymbol *node)
{
    TInfoSinkBase &out = objSink();

    bool wasTreated = false;
    if (node->variable().symbolType() != SymbolType::Empty && (getCompileOptions() & SH_ENFORCE_OUTPUT_TO_ESSL3))
    {
        const ImmutableString &name = node->getName();
        
        if (getShaderType() == GL_FRAGMENT_SHADER) 
        {
            if (name == "gl_FragColor")
            {
                out << "webgl_FragColor";
                wasTreated = true;
            }
            else if (name == "gl_FragData")
            {
                out << "webgl_FragData";
                wasTreated = true;
            }
            else if (name == "sample")
            {
                out << "webgl_sample";
                wasTreated = true;
            }
        }
        else if (getShaderType() == GL_VERTEX_SHADER) 
        {
            if (name == "gl_ViewID_OVR")
            {
                // gl_ViewID_OVR is unsigned in ESSL3, however, in WebGL1 it is just int.
                out << "int(gl_ViewID_OVR)";
                wasTreated = true;
            }
        }
    }

    if (!wasTreated)
    {
        TOutputGLSLBase::visitSymbol(node);
    }
}

ImmutableString TOutputESSL::translateTextureFunction(const ImmutableString &name)
{
    static const char *simpleRename[] = {"texture2DLodEXT",     "texture2DLod",
                                         "texture2DProjLodEXT", "texture2DProjLod",
                                         "textureCubeLodEXT",   "textureCubeLod",
                                         "texture2DGradEXT",    "texture2DGradARB",
                                         "texture2DProjGradEXT","texture2DProjGradARB",
                                         "textureCubeGradEXT",  "textureCubeGradARB",
                                         nullptr,                nullptr};
    static const char *legacyToCoreRename[] = {
        "texture2D",     "texture", 
        "texture2DProj", "textureProj", 
        "texture2DLod",  "textureLod",
        "texture2DProjLod", "textureProjLod", 
        "texture2DRect", "texture", 
        "textureCube", "texture",
        "textureCubeLod", "textureLod",
        // Extensions
        "texture2DLodEXT", "textureLod", 
        "texture2DProjLodEXT", "textureProjLod",
        "textureCubeLodEXT", "textureLod", 
        "texture2DGradEXT", "textureGrad",
        "texture2DProjGradEXT", "textureProjGrad", 
        "textureCubeGradEXT", "textureGrad", 
        nullptr, nullptr};
    const char **mapping =
        (getCompileOptions() & SH_ENFORCE_OUTPUT_TO_ESSL3) ? legacyToCoreRename : simpleRename;

    for (int i = 0; mapping[i] != nullptr; i += 2)
    {
        if (name == mapping[i])
        {
            return ImmutableString(mapping[i + 1]);
        }
    }

    return name;
}

}  // namespace sh
