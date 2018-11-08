//
// Copyright (c) 2002-2013 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

#include "compiler/translator/TranslatorESSL.h"

#include "compiler/translator/BuiltInFunctionEmulatorGLSL.h"
#include "compiler/translator/EmulatePrecision.h"
#include "compiler/translator/RecordConstantPrecision.h"
#include "compiler/translator/OutputESSL.h"
#include "angle_gl.h"

namespace sh
{

TranslatorESSL::TranslatorESSL(sh::GLenum type, ShShaderSpec spec)
    : TCompiler(type, spec, SH_ESSL_OUTPUT)
{
}

void TranslatorESSL::initBuiltInFunctionEmulator(BuiltInFunctionEmulator *emu,
                                                 ShCompileOptions compileOptions)
{
    if (compileOptions & SH_EMULATE_ATAN2_FLOAT_FUNCTION)
    {
        InitBuiltInAtanFunctionEmulatorForGLSLWorkarounds(emu);
    }
}

void TranslatorESSL::translate(TIntermBlock *root,
                               ShCompileOptions compileOptions,
                               PerformanceDiagnostics * /*perfDiagnostics*/)
{
    TInfoSinkBase &sink = getInfoSink().obj;

    int shaderVer = getShaderVersion();
    if (shaderVer >= 300) 
    {
        compileOptions &= ~SH_ENFORCE_OUTPUT_TO_ESSL3;
    } else if (shaderVer < 300 && ((compileOptions & SH_ENFORCE_OUTPUT_TO_ESSL3) || isMultiviewExtensionNeeded())) 
    {
        shaderVer = 300;
        compileOptions |= SH_ENFORCE_OUTPUT_TO_ESSL3;
    }
    if (shaderVer > 100)
    {
        sink << "#version " << shaderVer << " es\n";
    }

    // Write built-in extension behaviors.
    writeExtensionBehavior(compileOptions);

    // Write pragmas after extensions because some drivers consider pragmas
    // like non-preprocessor tokens.
    writePragma(compileOptions);

    bool precisionEmulation =
        getResources().WEBGL_debug_shader_precision && getPragma().debugShaderPrecision;

    if (precisionEmulation)
    {
        EmulatePrecision emulatePrecision(&getSymbolTable());
        root->traverse(&emulatePrecision);
        emulatePrecision.updateTree();
        emulatePrecision.writeEmulationHelpers(sink, shaderVer, SH_ESSL_OUTPUT);
    }

    RecordConstantPrecision(root, &getSymbolTable());

    // Write emulated built-in functions if needed.
    if (!getBuiltInFunctionEmulator().isOutputEmpty())
    {
        sink << "// BEGIN: Generated code for built-in function emulation\n\n";
        if (getShaderType() == GL_FRAGMENT_SHADER)
        {
            sink << "#if defined(GL_FRAGMENT_PRECISION_HIGH)\n"
                 << "#define emu_precision highp\n"
                 << "#else\n"
                 << "#define emu_precision mediump\n"
                 << "#endif\n\n";
        }
        else
        {
            sink << "#define emu_precision highp\n";
        }

        getBuiltInFunctionEmulator().outputEmulatedFunctions(sink);
        sink << "// END: Generated code for built-in function emulation\n\n";
    }

    // Write array bounds clamping emulation if needed.
    getArrayBoundsClamper().OutputClampingFunctionDefinition(sink);

    if (getShaderType() == GL_COMPUTE_SHADER && isComputeShaderLocalSizeDeclared())
    {
        const sh::WorkGroupSize &localSize = getComputeShaderLocalSize();
        sink << "layout (local_size_x=" << localSize[0] << ", local_size_y=" << localSize[1]
             << ", local_size_z=" << localSize[2] << ") in;\n";
    }

    if (getShaderType() == GL_GEOMETRY_SHADER_EXT)
    {
        WriteGeometryShaderLayoutQualifiers(
            sink, getGeometryShaderInputPrimitiveType(), getGeometryShaderInvocations(),
            getGeometryShaderOutputPrimitiveType(), getGeometryShaderMaxVertices());
    }

    // Declare gl_FragColor and glFragData as webgl_FragColor and webgl_FragData
    // if it's core profile shaders and they are used.
    if (getShaderType() == GL_FRAGMENT_SHADER && (compileOptions & SH_ENFORCE_OUTPUT_TO_ESSL3))
    {
        bool hasGLFragColor = false;
        bool hasGLFragData  = false;

        for (const auto &outputVar : outputVariables)
        {
            if (outputVar.name == "gl_FragColor")
            {
                ASSERT(!hasGLFragColor);
                hasGLFragColor = true;
                continue;
            }
            else if (outputVar.name == "gl_FragData")
            {
                ASSERT(!hasGLFragData);
                hasGLFragData = true;
                continue;
            }
        }
        ASSERT(!(hasGLFragColor && hasGLFragData));

        TPrecision defPrecision = getSymbolTable().getDefaultPrecision(EbtFloat);
        if (defPrecision == EbpUndefined)
        {
            defPrecision = EbpMedium;
        }
        const char *precStr = sh::getPrecisionString(defPrecision);
        if (hasGLFragColor)
        {
            sink << "out " << precStr << " vec4 webgl_FragColor;\n";
        }
        if (hasGLFragData) //???
        {
            sink << "out " << precStr << " vec4 webgl_FragData[gl_MaxDrawBuffers];\n";
        }
    }

    // Write translated shader.
    TOutputESSL outputESSL(sink, getArrayIndexClampingStrategy(), getHashFunction(), getNameMap(),
                           &getSymbolTable(), getShaderType(), shaderVer, precisionEmulation,
                           compileOptions);

    root->traverse(&outputESSL);
}

bool TranslatorESSL::shouldFlattenPragmaStdglInvariantAll()
{
    // If following the spec to the letter, we should not flatten this pragma.
    // However, the spec's wording means that the pragma applies only to outputs.
    // This contradicts the spirit of using the pragma,
    // because if the pragma is used in a vertex shader,
    // the only way to be able to link it to a fragment shader
    // is to manually qualify each of fragment shader's inputs as invariant.
    // Which defeats the purpose of this pragma - temporarily make all varyings
    // invariant for debugging.
    // Thus, we should be non-conformant to spec's letter here and flatten.
    return true;
}

void TranslatorESSL::writeExtensionBehavior(ShCompileOptions compileOptions)
{
    TInfoSinkBase &sink                   = getInfoSink().obj;
    const TExtensionBehavior &extBehavior = getExtensionBehavior();
    const bool isMultiviewExtEmulated =
        (compileOptions & (SH_INITIALIZE_BUILTINS_FOR_INSTANCED_MULTIVIEW |
                           SH_SELECT_VIEW_IN_NV_GLSL_VERTEX_SHADER)) != 0u;
    for (TExtensionBehavior::const_iterator iter = extBehavior.begin(); iter != extBehavior.end();
         ++iter)
    {
        if (iter->second != EBhUndefined)
        {
            const bool isMultiview = (iter->first == TExtension::OVR_multiview);
            if (getResources().NV_shader_framebuffer_fetch &&
                iter->first == TExtension::EXT_shader_framebuffer_fetch)
            {
                sink << "#extension GL_NV_shader_framebuffer_fetch : "
                     << GetBehaviorString(iter->second) << "\n";
            }
            else if (getResources().NV_draw_buffers && iter->first == TExtension::EXT_draw_buffers)
            {
                sink << "#extension GL_NV_draw_buffers : " << GetBehaviorString(iter->second)
                     << "\n";
            }
            else if (isMultiview)
            {
                if (isMultiviewExtEmulated && getShaderType() == GL_VERTEX_SHADER &&
                    (compileOptions & SH_SELECT_VIEW_IN_NV_GLSL_VERTEX_SHADER) != 0u)
                {
                    // Emit the NV_viewport_array2 extension in a vertex shader if the
                    // SH_SELECT_VIEW_IN_NV_GLSL_VERTEX_SHADER option is set and the
                    // OVR_multiview(2) extension is requested.
                    sink << "#extension GL_NV_viewport_array2 : require\n";
                }
                //!AB begin
                else if (getShaderType() == GL_VERTEX_SHADER)
                {
                    sink << "#extension GL_OVR_multiview2 : " << GetBehaviorString(iter->second)
                         << "\n";

                    // Vertex shaders allows the num_views layout qualifier.
                    // If this qualifier is not declared, the behavior is as if it had been set to 1.
                    int numViews = getNumViews();
                    if (compileOptions & SH_ENFORCE_OUTPUT_TO_ESSL3)
                    {
                        numViews = 2; //? How can we specify numViews in WebGL 1?
                    }
                    if (numViews >= 2)
                    {
                        sink << "layout(num_views=" << numViews << ") in;"
                             << "\n"; 
                    }
                }
                //!AB end
            }
            else if (iter->first == TExtension::EXT_geometry_shader)
            {
                sink << "#ifdef GL_EXT_geometry_shader\n"
                     << "#extension GL_EXT_geometry_shader : " << GetBehaviorString(iter->second)
                     << "\n"
                     << "#elif defined GL_OES_geometry_shader\n"
                     << "#extension GL_OES_geometry_shader : " << GetBehaviorString(iter->second)
                     << "\n";
                if (iter->second == EBhRequire)
                {
                    sink << "#else\n"
                         << "#error \"No geometry shader extensions available.\" // Only generate "
                            "this if the extension is \"required\"\n";
                }
                sink << "#endif\n";
            }
            else
            {
                sink << "#extension " << GetExtensionNameString(iter->first) << " : "
                     << GetBehaviorString(iter->second) << "\n";
            }
        }
    }
}

bool TranslatorESSL::isMultiviewExtensionNeeded() const {
    const TExtensionBehavior &extBehavior = getExtensionBehavior();
    for (TExtensionBehavior::const_iterator iter = extBehavior.begin(); iter != extBehavior.end();
         ++iter)
    {
        if (iter->second != EBhUndefined)
        {
            const bool isMultiview = (iter->first == TExtension::OVR_multiview);
            if (isMultiview) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace sh
