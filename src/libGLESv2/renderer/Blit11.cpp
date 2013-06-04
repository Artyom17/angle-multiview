#include "precompiled.h"
//
// Copyright (c) 2013 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// Blit11.cpp: Texture copy utility class.

#include "libGLESv2/main.h"
#include "libGLESv2/formatutils.h"
#include "libGLESv2/renderer/Blit11.h"
#include "libGLESv2/renderer/Renderer11.h"
#include "libGLESv2/renderer/renderer11_utils.h"
#include "libGLESv2/renderer/formatutils11.h"

#include "libGLESv2/renderer/shaders/compiled/passthrough2d11vs.h"
#include "libGLESv2/renderer/shaders/compiled/passthroughrgba2d11ps.h"
#include "libGLESv2/renderer/shaders/compiled/passthroughrgba2dui11ps.h"
#include "libGLESv2/renderer/shaders/compiled/passthroughrgba2di11ps.h"
#include "libGLESv2/renderer/shaders/compiled/passthroughrgb2d11ps.h"
#include "libGLESv2/renderer/shaders/compiled/passthroughrgb2dui11ps.h"
#include "libGLESv2/renderer/shaders/compiled/passthroughrgb2di11ps.h"
#include "libGLESv2/renderer/shaders/compiled/passthroughrg2d11ps.h"
#include "libGLESv2/renderer/shaders/compiled/passthroughrg2dui11ps.h"
#include "libGLESv2/renderer/shaders/compiled/passthroughrg2di11ps.h"
#include "libGLESv2/renderer/shaders/compiled/passthroughr2d11ps.h"
#include "libGLESv2/renderer/shaders/compiled/passthroughr2dui11ps.h"
#include "libGLESv2/renderer/shaders/compiled/passthroughr2di11ps.h"
#include "libGLESv2/renderer/shaders/compiled/passthroughlum2d11ps.h"
#include "libGLESv2/renderer/shaders/compiled/passthroughlumalpha2d11ps.h"

#include "libGLESv2/renderer/shaders/compiled/passthrough3d11vs.h"
#include "libGLESv2/renderer/shaders/compiled/passthrough3d11gs.h"
#include "libGLESv2/renderer/shaders/compiled/passthroughrgba3d11ps.h"
#include "libGLESv2/renderer/shaders/compiled/passthroughrgba3dui11ps.h"
#include "libGLESv2/renderer/shaders/compiled/passthroughrgba3di11ps.h"
#include "libGLESv2/renderer/shaders/compiled/passthroughrgb3d11ps.h"
#include "libGLESv2/renderer/shaders/compiled/passthroughrgb3dui11ps.h"
#include "libGLESv2/renderer/shaders/compiled/passthroughrgb3di11ps.h"
#include "libGLESv2/renderer/shaders/compiled/passthroughrg3d11ps.h"
#include "libGLESv2/renderer/shaders/compiled/passthroughrg3dui11ps.h"
#include "libGLESv2/renderer/shaders/compiled/passthroughrg3di11ps.h"
#include "libGLESv2/renderer/shaders/compiled/passthroughr3d11ps.h"
#include "libGLESv2/renderer/shaders/compiled/passthroughr3dui11ps.h"
#include "libGLESv2/renderer/shaders/compiled/passthroughr3di11ps.h"
#include "libGLESv2/renderer/shaders/compiled/passthroughlum3d11ps.h"
#include "libGLESv2/renderer/shaders/compiled/passthroughlumalpha3d11ps.h"

namespace rx
{

Blit11::Blit11(rx::Renderer11 *renderer)
    : mRenderer(renderer), mShaderMap(compareBlitParameters), mVertexBuffer(NULL),
      mPointSampler(NULL), mLinearSampler(NULL), mQuad2DIL(NULL), mQuad2DVS(NULL),
      mQuad3DIL(NULL), mQuad3DVS(NULL), mQuad3DGS(NULL)
{
    HRESULT result;
    ID3D11Device *device = mRenderer->getDevice();

    D3D11_BUFFER_DESC vbDesc;
    vbDesc.ByteWidth = std::max(sizeof(d3d11::PositionLayerTexCoord3DVertex) * 6 * renderer->getMaxTextureDepth(),
                                sizeof(d3d11::PositionTexCoordVertex) * 4);
    vbDesc.Usage = D3D11_USAGE_DYNAMIC;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    vbDesc.MiscFlags = 0;
    vbDesc.StructureByteStride = 0;

    result = device->CreateBuffer(&vbDesc, NULL, &mVertexBuffer);
    ASSERT(SUCCEEDED(result));
    d3d11::SetDebugName(mVertexBuffer, "Blit11 vertex buffer");

    D3D11_SAMPLER_DESC pointSamplerDesc;
    pointSamplerDesc.Filter = D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR;
    pointSamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    pointSamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    pointSamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    pointSamplerDesc.MipLODBias = 0.0f;
    pointSamplerDesc.MaxAnisotropy = 0;
    pointSamplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    pointSamplerDesc.BorderColor[0] = 0.0f;
    pointSamplerDesc.BorderColor[1] = 0.0f;
    pointSamplerDesc.BorderColor[2] = 0.0f;
    pointSamplerDesc.BorderColor[3] = 0.0f;
    pointSamplerDesc.MinLOD = 0.0f;
    pointSamplerDesc.MaxLOD = 0.0f;

    result = device->CreateSamplerState(&pointSamplerDesc, &mPointSampler);
    ASSERT(SUCCEEDED(result));
    d3d11::SetDebugName(mPointSampler, "Blit11 point sampler");

    D3D11_SAMPLER_DESC linearSamplerDesc;
    linearSamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    linearSamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    linearSamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    linearSamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    linearSamplerDesc.MipLODBias = 0.0f;
    linearSamplerDesc.MaxAnisotropy = 0;
    linearSamplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    linearSamplerDesc.BorderColor[0] = 0.0f;
    linearSamplerDesc.BorderColor[1] = 0.0f;
    linearSamplerDesc.BorderColor[2] = 0.0f;
    linearSamplerDesc.BorderColor[3] = 0.0f;
    linearSamplerDesc.MinLOD = 0.0f;
    linearSamplerDesc.MaxLOD = 0.0f;

    result = device->CreateSamplerState(&linearSamplerDesc, &mLinearSampler);
    ASSERT(SUCCEEDED(result));
    d3d11::SetDebugName(mLinearSampler, "Blit11 linear sampler");

    D3D11_INPUT_ELEMENT_DESC quad2DLayout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    result = device->CreateInputLayout(quad2DLayout, ArraySize(quad2DLayout), g_VS_Passthrough2D, ArraySize(g_VS_Passthrough2D), &mQuad2DIL);
    ASSERT(SUCCEEDED(result));
    d3d11::SetDebugName(mQuad2DIL, "Blit11 2D input layout");

    result = device->CreateVertexShader(g_VS_Passthrough2D, ArraySize(g_VS_Passthrough2D), NULL, &mQuad2DVS);
    ASSERT(SUCCEEDED(result));
    d3d11::SetDebugName(mQuad2DVS, "Blit11 2D vertex shader");

    D3D11_INPUT_ELEMENT_DESC quad3DLayout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,    0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "LAYER",    0, DXGI_FORMAT_R32_UINT,        0,  8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    result = device->CreateInputLayout(quad3DLayout, ArraySize(quad3DLayout), g_VS_Passthrough3D, ArraySize(g_VS_Passthrough3D), &mQuad3DIL);
    ASSERT(SUCCEEDED(result));
    d3d11::SetDebugName(mQuad3DIL, "Blit11 3D input layout");

    result = device->CreateVertexShader(g_VS_Passthrough3D, ArraySize(g_VS_Passthrough3D), NULL, &mQuad3DVS);
    ASSERT(SUCCEEDED(result));
    d3d11::SetDebugName(mQuad3DVS, "Blit11 3D vertex shader");

    result = device->CreateGeometryShader(g_GS_Passthrough3D, ArraySize(g_GS_Passthrough3D), NULL, &mQuad3DGS);
    ASSERT(SUCCEEDED(result));
    d3d11::SetDebugName(mQuad3DGS, "Renderer11 copy 3D texture geometry shader");

    buildShaderMap();
}

Blit11::~Blit11()
{
    SafeRelease(mVertexBuffer);
    SafeRelease(mPointSampler);
    SafeRelease(mLinearSampler);

    SafeRelease(mQuad2DIL);
    SafeRelease(mQuad2DVS);

    SafeRelease(mQuad3DIL);
    SafeRelease(mQuad3DVS);
    SafeRelease(mQuad3DGS);

    clearShaderMap();
}

bool Blit11::copyTexture(ID3D11ShaderResourceView *source, const gl::Box &sourceArea, const gl::Extents &sourceSize,
                         ID3D11RenderTargetView *dest, const gl::Box &destArea, const gl::Extents &destSize,
                         GLenum destFormat, GLenum filter)
{
    if(sourceArea.x < 0 || sourceArea.x + sourceArea.width  > sourceSize.width  ||
       sourceArea.y < 0 || sourceArea.y + sourceArea.height > sourceSize.height ||
       sourceArea.z < 0 || sourceArea.z + sourceArea.depth  > sourceSize.depth  ||
       destArea.x   < 0 || destArea.x   + destArea.width    > destSize.width    ||
       destArea.y   < 0 || destArea.y   + destArea.height   > destSize.height   ||
       destArea.z   < 0 || destArea.z   + destArea.depth    > destSize.depth    )
    {
        return false;
    }

    HRESULT result;
    ID3D11DeviceContext *deviceContext = mRenderer->getDeviceContext();

    // Determine if the source format is a signed integer format, the destFormat will already
    // be GL_XXXX_INTEGER but it does not tell us if it is signed or unsigned.
    D3D11_SHADER_RESOURCE_VIEW_DESC sourceSRVDesc;
    source->GetDesc(&sourceSRVDesc);
    GLint sourceInternalFormat = d3d11_gl::GetInternalFormat(sourceSRVDesc.Format);

    BlitParameters parameters = { 0 };
    parameters.mDestinationFormat = destFormat;
    parameters.mSignedInteger = gl::IsSignedIntegerFormat(sourceInternalFormat, mRenderer->getCurrentClientVersion());
    parameters.m3DBlit = sourceArea.depth > 1;

    BlitShaderMap::const_iterator i = mShaderMap.find(parameters);
    if (i == mShaderMap.end())
    {
        UNREACHABLE();
        return false;
    }

    const BlitShader& shader = i->second;

    // Set vertices
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    result = deviceContext->Map(mVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (FAILED(result))
    {
        ERR("Failed to map vertex buffer for texture copy, HRESULT: 0x%X.", result);
        return false;
    }

    UINT stride = 0;
    UINT startIdx = 0;
    UINT drawCount = 0;
    D3D11_PRIMITIVE_TOPOLOGY topology;

    shader.mVertexWriteFunction(sourceArea, sourceSize, destArea, destSize, mappedResource.pData,
                                &stride, &drawCount, &topology);

    deviceContext->Unmap(mVertexBuffer, 0);

    // Apply vertex buffer
    deviceContext->IASetVertexBuffers(0, 1, &mVertexBuffer, &stride, &startIdx);

    // Apply state
    deviceContext->OMSetBlendState(NULL, NULL, 0xFFFFFFF);
    deviceContext->OMSetDepthStencilState(NULL, 0xFFFFFFFF);
    deviceContext->RSSetState(NULL);

    // Apply shaders
    deviceContext->IASetInputLayout(shader.mInputLayout);
    deviceContext->IASetPrimitiveTopology(topology);
    deviceContext->VSSetShader(shader.mVertexShader, NULL, 0);

    deviceContext->PSSetShader(shader.mPixelShader, NULL, 0);
    deviceContext->GSSetShader(shader.mGeometryShader, NULL, 0);

    // Unset the currently bound shader resource to avoid conflicts
    ID3D11ShaderResourceView *const nullSRV = NULL;
    deviceContext->PSSetShaderResources(0, 1, &nullSRV);

    // Apply render target
    mRenderer->setOneTimeRenderTarget(dest);

    // Set the viewport
    D3D11_VIEWPORT viewport;
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width = destSize.width;
    viewport.Height = destSize.height;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    deviceContext->RSSetViewports(1, &viewport);

    // Apply textures
    deviceContext->PSSetShaderResources(0, 1, &source);

    // Apply samplers
    ID3D11SamplerState *sampler = NULL;
    switch (filter)
    {
      case GL_NEAREST: sampler = mPointSampler;  break;
      case GL_LINEAR:  sampler = mLinearSampler; break;
      default:         UNREACHABLE(); return false;
    }
    deviceContext->PSSetSamplers(0, 1, &sampler);

    // Draw the quad
    deviceContext->Draw(drawCount, 0);

    // Unbind textures and render targets and vertex buffer
    deviceContext->PSSetShaderResources(0, 1, &nullSRV);

    mRenderer->unapplyRenderTargets();

    UINT zero = 0;
    ID3D11Buffer *const nullBuffer = NULL;
    deviceContext->IASetVertexBuffers(0, 1, &nullBuffer, &zero, &zero);

    mRenderer->markAllStateDirty();

    return true;
}

bool Blit11::compareBlitParameters(const Blit11::BlitParameters &a, const Blit11::BlitParameters &b)
{
    return memcmp(&a, &b, sizeof(Blit11::BlitParameters)) < 0;
}

template <unsigned int N>
static ID3D11PixelShader *compilePS(ID3D11Device *device, const BYTE (&byteCode)[N], const char *name)
{
    ID3D11PixelShader *ps = NULL;
    HRESULT result = device->CreatePixelShader(byteCode, N, NULL, &ps);
    ASSERT(SUCCEEDED(result));
    d3d11::SetDebugName(ps, name);
    return ps;
}

inline static void generateVertexCoords(const gl::Box &sourceArea, const gl::Extents &sourceSize,
                                        const gl::Box &destArea, const gl::Extents &destSize,
                                        float *x1, float *y1, float *x2, float *y2,
                                        float *u1, float *v1, float *u2, float *v2)
{
    *x1 = (destArea.x / float(destSize.width)) * 2.0f - 1.0f;
    *y1 = ((destSize.height - destArea.y - destArea.height) / float(destSize.height)) * 2.0f - 1.0f;
    *x2 = ((destArea.x + destArea.width) / float(destSize.width)) * 2.0f - 1.0f;
    *y2 = ((destSize.height - destArea.y) / float(destSize.height)) * 2.0f - 1.0f;

    *u1 = sourceArea.x / float(sourceSize.width);
    *v1 = sourceArea.y / float(sourceSize.height);
    *u2 = (sourceArea.x + sourceArea.width) / float(sourceSize.width);
    *v2 = (sourceArea.y + sourceArea.height) / float(sourceSize.height);
}

static void write2DVertices(const gl::Box &sourceArea, const gl::Extents &sourceSize,
                            const gl::Box &destArea, const gl::Extents &destSize,
                            void *outVertices, unsigned int *outStride, unsigned int *outVertexCount,
                            D3D11_PRIMITIVE_TOPOLOGY *outTopology)
{
    float x1, y1, x2, y2, u1, v1, u2, v2;
    generateVertexCoords(sourceArea, sourceSize, destArea, destSize, &x1, &y1, &x2, &y2, &u1, &v1, &u2, &v2);

    d3d11::PositionTexCoordVertex *vertices = static_cast<d3d11::PositionTexCoordVertex*>(outVertices);

    d3d11::SetPositionTexCoordVertex(&vertices[0], x1, y1, u1, v2);
    d3d11::SetPositionTexCoordVertex(&vertices[1], x1, y2, u1, v1);
    d3d11::SetPositionTexCoordVertex(&vertices[2], x2, y1, u2, v2);
    d3d11::SetPositionTexCoordVertex(&vertices[3], x2, y2, u2, v1);

    *outStride = sizeof(d3d11::PositionTexCoordVertex);
    *outVertexCount = 4;
    *outTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
}

static void write3DVertices(const gl::Box &sourceArea, const gl::Extents &sourceSize,
                            const gl::Box &destArea, const gl::Extents &destSize,
                            void *outVertices, unsigned int *outStride, unsigned int *outVertexCount,
                            D3D11_PRIMITIVE_TOPOLOGY *outTopology)
{
    float x1, y1, x2, y2, u1, v1, u2, v2;
    generateVertexCoords(sourceArea, sourceSize, destArea, destSize, &x1, &y1, &x2, &y2, &u1, &v1, &u2, &v2);

    d3d11::PositionLayerTexCoord3DVertex *vertices = static_cast<d3d11::PositionLayerTexCoord3DVertex*>(outVertices);

    for (int i = 0; i < destSize.depth; i++)
    {
        float readDepth = ((i * 2) + 0.5f) / (sourceSize.depth - 1);

        d3d11::SetPositionLayerTexCoord3DVertex(&vertices[i * 6 + 0], x1, y1, i, u1, v2, readDepth);
        d3d11::SetPositionLayerTexCoord3DVertex(&vertices[i * 6 + 1], x1, y2, i, u1, v1, readDepth);
        d3d11::SetPositionLayerTexCoord3DVertex(&vertices[i * 6 + 2], x2, y1, i, u2, v2, readDepth);

        d3d11::SetPositionLayerTexCoord3DVertex(&vertices[i * 6 + 3], x1, y2, i, u1, v1, readDepth);
        d3d11::SetPositionLayerTexCoord3DVertex(&vertices[i * 6 + 4], x2, y2, i, u2, v1, readDepth);
        d3d11::SetPositionLayerTexCoord3DVertex(&vertices[i * 6 + 5], x2, y1, i, u2, v2, readDepth);
    }

    *outStride = sizeof(d3d11::PositionLayerTexCoord3DVertex);
    *outVertexCount = destSize.depth * 6;
    *outTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
}

void Blit11::add2DShaderToMap(GLenum destFormat, bool signedInteger, ID3D11PixelShader *ps)
{
    BlitParameters params = { 0 };
    params.mDestinationFormat = destFormat;
    params.mSignedInteger = signedInteger;
    params.m3DBlit = false;

    ASSERT(mShaderMap.find(params) == mShaderMap.end());
    ASSERT(ps);

    BlitShader shader;
    shader.mVertexWriteFunction = write2DVertices;
    shader.mInputLayout = mQuad2DIL;
    shader.mVertexShader = mQuad2DVS;
    shader.mGeometryShader = NULL;
    shader.mPixelShader = ps;

    mShaderMap[params] = shader;
}

void Blit11::add3DShaderToMap(GLenum destFormat, bool signedInteger, ID3D11PixelShader *ps)
{
    BlitParameters params = { 0 };
    params.mDestinationFormat = destFormat;
    params.mSignedInteger = signedInteger;
    params.m3DBlit = true;

    ASSERT(mShaderMap.find(params) == mShaderMap.end());
    ASSERT(ps);

    BlitShader shader;
    shader.mVertexWriteFunction = write3DVertices;
    shader.mInputLayout = mQuad3DIL;
    shader.mVertexShader = mQuad3DVS;
    shader.mGeometryShader = mQuad3DGS;
    shader.mPixelShader = ps;

    mShaderMap[params] = shader;
}

void Blit11::buildShaderMap()
{
    ID3D11Device *device = mRenderer->getDevice();

    add2DShaderToMap(GL_RGBA,            false, compilePS(device, g_PS_PassthroughRGBA2D,     "Blit11 2D RGBA pixel shader"           ));
    add2DShaderToMap(GL_RGBA_INTEGER,    false, compilePS(device, g_PS_PassthroughRGBA2DUI,   "Blit11 2D RGBA UI pixel shader"        ));
    add2DShaderToMap(GL_RGBA_INTEGER,    true,  compilePS(device, g_PS_PassthroughRGBA2DI,    "Blit11 2D RGBA I pixel shader"         ));
    add2DShaderToMap(GL_BGRA_EXT,        false, compilePS(device, g_PS_PassthroughRGBA2D,     "Blit11 2D BGRA pixel shader"           ));
    add2DShaderToMap(GL_RGB,             false, compilePS(device, g_PS_PassthroughRGB2D,      "Blit11 2D RGB pixel shader"            ));
    add2DShaderToMap(GL_RGB_INTEGER,     false, compilePS(device, g_PS_PassthroughRGB2DUI,    "Blit11 2D RGB UI pixel shader"         ));
    add2DShaderToMap(GL_RGB_INTEGER,     true,  compilePS(device, g_PS_PassthroughRGB2DI,     "Blit11 2D RGB I pixel shader"          ));
    add2DShaderToMap(GL_RG,              false, compilePS(device, g_PS_PassthroughRG2D,       "Blit11 2D RG pixel shader"             ));
    add2DShaderToMap(GL_RG_INTEGER,      false, compilePS(device, g_PS_PassthroughRG2DUI,     "Blit11 2D RG UI pixel shader"          ));
    add2DShaderToMap(GL_RG_INTEGER,      true,  compilePS(device, g_PS_PassthroughRG2DI,      "Blit11 2D RG I pixel shader"           ));
    add2DShaderToMap(GL_RED,             false, compilePS(device, g_PS_PassthroughR2D,        "Blit11 2D R pixel shader"              ));
    add2DShaderToMap(GL_RED_INTEGER,     false, compilePS(device, g_PS_PassthroughR2DUI,      "Blit11 2D R UI pixel shader"           ));
    add2DShaderToMap(GL_RED_INTEGER,     true,  compilePS(device, g_PS_PassthroughR2DI,       "Blit11 2D R I pixel shader"            ));
    add2DShaderToMap(GL_ALPHA,           false, compilePS(device, g_PS_PassthroughRGBA2D,     "Blit11 2D alpha pixel shader"          ));
    add2DShaderToMap(GL_LUMINANCE,       false, compilePS(device, g_PS_PassthroughLum2D,      "Blit11 2D lum pixel shader"            ));
    add2DShaderToMap(GL_LUMINANCE_ALPHA, false, compilePS(device, g_PS_PassthroughLumAlpha2D, "Blit11 2D luminance alpha pixel shader"));

    add3DShaderToMap(GL_RGBA,            false, compilePS(device, g_PS_PassthroughRGBA3D,     "Blit11 3D RGBA pixel shader"           ));
    add3DShaderToMap(GL_RGBA_INTEGER,    false, compilePS(device, g_PS_PassthroughRGBA3DUI,   "Blit11 3D UI RGBA pixel shader"        ));
    add3DShaderToMap(GL_RGBA_INTEGER,    true,  compilePS(device, g_PS_PassthroughRGBA3DI,    "Blit11 3D I RGBA pixel shader"         ));
    add3DShaderToMap(GL_BGRA_EXT,        false, compilePS(device, g_PS_PassthroughRGBA3D,     "Blit11 3D BGRA pixel shader"           ));
    add3DShaderToMap(GL_RGB,             false, compilePS(device, g_PS_PassthroughRGB3D,      "Blit11 3D RGB pixel shader"            ));
    add3DShaderToMap(GL_RGB_INTEGER,     false, compilePS(device, g_PS_PassthroughRGB3DUI,    "Blit11 3D RGB UI pixel shader"         ));
    add3DShaderToMap(GL_RGB_INTEGER,     true,  compilePS(device, g_PS_PassthroughRGB3DI,     "Blit11 3D RGB I pixel shader"          ));
    add3DShaderToMap(GL_RG,              false, compilePS(device, g_PS_PassthroughRG3D,       "Blit11 3D RG pixel shader"             ));
    add3DShaderToMap(GL_RG_INTEGER,      false, compilePS(device, g_PS_PassthroughRG3DUI,     "Blit11 3D RG UI pixel shader"          ));
    add3DShaderToMap(GL_RG_INTEGER,      true,  compilePS(device, g_PS_PassthroughRG3DI,      "Blit11 3D RG I pixel shader"           ));
    add3DShaderToMap(GL_RED,             false, compilePS(device, g_PS_PassthroughR3D,        "Blit11 3D R pixel shader"              ));
    add3DShaderToMap(GL_RED_INTEGER,     false, compilePS(device, g_PS_PassthroughR3DUI,      "Blit11 3D R UI pixel shader"           ));
    add3DShaderToMap(GL_RED_INTEGER,     true,  compilePS(device, g_PS_PassthroughR3DI,       "Blit11 3D R I pixel shader"            ));
    add3DShaderToMap(GL_ALPHA,           false, compilePS(device, g_PS_PassthroughRGBA3D,     "Blit11 3D alpha pixel shader"          ));
    add3DShaderToMap(GL_LUMINANCE,       false, compilePS(device, g_PS_PassthroughLum3D,      "Blit11 3D luminance pixel shader"      ));
    add3DShaderToMap(GL_LUMINANCE_ALPHA, false, compilePS(device, g_PS_PassthroughLumAlpha3D, "Blit11 3D luminance alpha pixel shader"));
}

void Blit11::clearShaderMap()
{
    for (BlitShaderMap::iterator i = mShaderMap.begin(); i != mShaderMap.end(); ++i)
    {
        BlitShader &shader = i->second;
        SafeRelease(shader.mPixelShader);
    }
    mShaderMap.clear();
}

}
