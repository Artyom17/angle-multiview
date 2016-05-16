//
// Copyright 2015 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// TextureGL.cpp: Implements the class methods for TextureGL.

#include "libANGLE/renderer/gl/TextureGL.h"

#include "common/debug.h"
#include "common/utilities.h"
#include "libANGLE/State.h"
#include "libANGLE/angletypes.h"
#include "libANGLE/formatutils.h"
#include "libANGLE/renderer/gl/BlitGL.h"
#include "libANGLE/renderer/gl/BufferGL.h"
#include "libANGLE/renderer/gl/FramebufferGL.h"
#include "libANGLE/renderer/gl/FunctionsGL.h"
#include "libANGLE/renderer/gl/StateManagerGL.h"
#include "libANGLE/renderer/gl/WorkaroundsGL.h"
#include "libANGLE/renderer/gl/formatutilsgl.h"

namespace rx
{

static bool UseTexImage2D(GLenum textureType)
{
    return textureType == GL_TEXTURE_2D || textureType == GL_TEXTURE_CUBE_MAP;
}

static bool UseTexImage3D(GLenum textureType)
{
    return textureType == GL_TEXTURE_2D_ARRAY || textureType == GL_TEXTURE_3D;
}

static bool CompatibleTextureTarget(GLenum textureType, GLenum textureTarget)
{
    if (textureType != GL_TEXTURE_CUBE_MAP)
    {
        return textureType == textureTarget;
    }
    else
    {
        return gl::IsCubeMapTextureTarget(textureTarget);
    }
}

static bool IsLUMAFormat(GLenum format)
{
    return format == GL_LUMINANCE || format == GL_ALPHA || format == GL_LUMINANCE_ALPHA;
}

static LUMAWorkaroundGL GetLUMAWorkaroundInfo(const gl::InternalFormat &originalFormatInfo,
                                              GLenum destinationFormat)
{
    if (IsLUMAFormat(originalFormatInfo.format))
    {
        const gl::InternalFormat &destinationFormatInfo =
            gl::GetInternalFormatInfo(destinationFormat);
        return LUMAWorkaroundGL(!IsLUMAFormat(destinationFormatInfo.format),
                                destinationFormatInfo.format);
    }
    else
    {
        return LUMAWorkaroundGL(false, GL_NONE);
    }
}

static bool IsDepthStencilFormat(GLenum format)
{
    return format == GL_DEPTH_COMPONENT || format == GL_DEPTH_STENCIL;
}

static bool GetDepthStencilWorkaround(const gl::InternalFormat &originalFormatInfo)
{
    return IsDepthStencilFormat(originalFormatInfo.format);
}

static LevelInfoGL GetLevelInfo(GLenum originalFormat, GLenum destinationFormat)
{
    const gl::InternalFormat &originalFormatInfo = gl::GetInternalFormatInfo(originalFormat);
    return LevelInfoGL(originalFormat, GetDepthStencilWorkaround(originalFormatInfo),
                       GetLUMAWorkaroundInfo(originalFormatInfo, destinationFormat));
}

LUMAWorkaroundGL::LUMAWorkaroundGL() : LUMAWorkaroundGL(false, GL_NONE)
{
}

LUMAWorkaroundGL::LUMAWorkaroundGL(bool enabled_, GLenum workaroundFormat_)
    : enabled(enabled_), workaroundFormat(workaroundFormat_)
{
}

LevelInfoGL::LevelInfoGL() : LevelInfoGL(GL_NONE, false, LUMAWorkaroundGL())
{
}

LevelInfoGL::LevelInfoGL(GLenum sourceFormat_,
                         bool depthStencilWorkaround_,
                         const LUMAWorkaroundGL &lumaWorkaround_)
    : sourceFormat(sourceFormat_),
      depthStencilWorkaround(depthStencilWorkaround_),
      lumaWorkaround(lumaWorkaround_)
{
}

TextureGL::TextureGL(const gl::TextureState &state,
                     const FunctionsGL *functions,
                     const WorkaroundsGL &workarounds,
                     StateManagerGL *stateManager,
                     BlitGL *blitter)
    : TextureImpl(state),
      mFunctions(functions),
      mWorkarounds(workarounds),
      mStateManager(stateManager),
      mBlitter(blitter),
      mLevelInfo(gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS + 1),
      mAppliedTextureState(state.mTarget),
      mTextureID(0)
{
    ASSERT(mFunctions);
    ASSERT(mStateManager);
    ASSERT(mBlitter);

    mFunctions->genTextures(1, &mTextureID);
    mStateManager->bindTexture(mState.mTarget, mTextureID);
}

TextureGL::~TextureGL()
{
    mStateManager->deleteTexture(mTextureID);
    mTextureID = 0;
}

gl::Error TextureGL::setImage(GLenum target, size_t level, GLenum internalFormat, const gl::Extents &size, GLenum format, GLenum type,
                              const gl::PixelUnpackState &unpack, const uint8_t *pixels)
{
    UNUSED_ASSERTION_VARIABLE(&CompatibleTextureTarget); // Reference this function to avoid warnings.
    ASSERT(CompatibleTextureTarget(mState.mTarget, target));

    nativegl::TexImageFormat texImageFormat =
        nativegl::GetTexImageFormat(mFunctions, mWorkarounds, internalFormat, format, type);

    mStateManager->bindTexture(mState.mTarget, mTextureID);
    if (UseTexImage2D(mState.mTarget))
    {
        ASSERT(size.depth == 1);
        mFunctions->texImage2D(target, static_cast<GLint>(level), texImageFormat.internalFormat,
                               size.width, size.height, 0, texImageFormat.format,
                               texImageFormat.type, pixels);
    }
    else if (UseTexImage3D(mState.mTarget))
    {
        mFunctions->texImage3D(target, static_cast<GLint>(level), texImageFormat.internalFormat,
                               size.width, size.height, size.depth, 0, texImageFormat.format,
                               texImageFormat.type, pixels);
    }
    else
    {
        UNREACHABLE();
    }

    mLevelInfo[level] = GetLevelInfo(internalFormat, texImageFormat.internalFormat);

    return gl::Error(GL_NO_ERROR);
}

gl::Error TextureGL::setSubImage(GLenum target, size_t level, const gl::Box &area, GLenum format, GLenum type,
                                 const gl::PixelUnpackState &unpack, const uint8_t *pixels)
{
    ASSERT(CompatibleTextureTarget(mState.mTarget, target));

    nativegl::TexSubImageFormat texSubImageFormat =
        nativegl::GetTexSubImageFormat(mFunctions, mWorkarounds, format, type);

    mStateManager->bindTexture(mState.mTarget, mTextureID);
    if (UseTexImage2D(mState.mTarget))
    {
        ASSERT(area.z == 0 && area.depth == 1);
        mFunctions->texSubImage2D(target, static_cast<GLint>(level), area.x, area.y, area.width,
                                  area.height, texSubImageFormat.format, texSubImageFormat.type,
                                  pixels);
    }
    else if (UseTexImage3D(mState.mTarget))
    {
        mFunctions->texSubImage3D(target, static_cast<GLint>(level), area.x, area.y, area.z,
                                  area.width, area.height, area.depth, texSubImageFormat.format,
                                  texSubImageFormat.type, pixels);
    }
    else
    {
        UNREACHABLE();
    }

    ASSERT(mLevelInfo[level].lumaWorkaround.enabled ==
           GetLevelInfo(format, texSubImageFormat.format).lumaWorkaround.enabled);

    return gl::Error(GL_NO_ERROR);
}

gl::Error TextureGL::setCompressedImage(GLenum target, size_t level, GLenum internalFormat, const gl::Extents &size,
                                        const gl::PixelUnpackState &unpack, size_t imageSize, const uint8_t *pixels)
{
    ASSERT(CompatibleTextureTarget(mState.mTarget, target));

    nativegl::CompressedTexImageFormat compressedTexImageFormat =
        nativegl::GetCompressedTexImageFormat(mFunctions, mWorkarounds, internalFormat);

    mStateManager->bindTexture(mState.mTarget, mTextureID);
    if (UseTexImage2D(mState.mTarget))
    {
        ASSERT(size.depth == 1);
        mFunctions->compressedTexImage2D(target, static_cast<GLint>(level),
                                         compressedTexImageFormat.internalFormat, size.width,
                                         size.height, 0, static_cast<GLsizei>(imageSize), pixels);
    }
    else if (UseTexImage3D(mState.mTarget))
    {
        mFunctions->compressedTexImage3D(
            target, static_cast<GLint>(level), compressedTexImageFormat.internalFormat, size.width,
            size.height, size.depth, 0, static_cast<GLsizei>(imageSize), pixels);
    }
    else
    {
        UNREACHABLE();
    }

    mLevelInfo[level] = GetLevelInfo(internalFormat, compressedTexImageFormat.internalFormat);
    ASSERT(!mLevelInfo[level].lumaWorkaround.enabled);

    return gl::Error(GL_NO_ERROR);
}

gl::Error TextureGL::setCompressedSubImage(GLenum target, size_t level, const gl::Box &area, GLenum format,
                                           const gl::PixelUnpackState &unpack, size_t imageSize, const uint8_t *pixels)
{
    ASSERT(CompatibleTextureTarget(mState.mTarget, target));

    nativegl::CompressedTexSubImageFormat compressedTexSubImageFormat =
        nativegl::GetCompressedSubTexImageFormat(mFunctions, mWorkarounds, format);

    mStateManager->bindTexture(mState.mTarget, mTextureID);
    if (UseTexImage2D(mState.mTarget))
    {
        ASSERT(area.z == 0 && area.depth == 1);
        mFunctions->compressedTexSubImage2D(
            target, static_cast<GLint>(level), area.x, area.y, area.width, area.height,
            compressedTexSubImageFormat.format, static_cast<GLsizei>(imageSize), pixels);
    }
    else if (UseTexImage3D(mState.mTarget))
    {
        mFunctions->compressedTexSubImage3D(target, static_cast<GLint>(level), area.x, area.y,
                                            area.z, area.width, area.height, area.depth,
                                            compressedTexSubImageFormat.format,
                                            static_cast<GLsizei>(imageSize), pixels);
    }
    else
    {
        UNREACHABLE();
    }

    ASSERT(!mLevelInfo[level].lumaWorkaround.enabled &&
           !GetLevelInfo(format, compressedTexSubImageFormat.format).lumaWorkaround.enabled);

    return gl::Error(GL_NO_ERROR);
}

gl::Error TextureGL::copyImage(GLenum target, size_t level, const gl::Rectangle &sourceArea, GLenum internalFormat,
                               const gl::Framebuffer *source)
{
    nativegl::CopyTexImageImageFormat copyTexImageFormat = nativegl::GetCopyTexImageImageFormat(
        mFunctions, mWorkarounds, internalFormat, source->getImplementationColorReadType());

    LevelInfoGL levelInfo = GetLevelInfo(internalFormat, copyTexImageFormat.internalFormat);
    if (levelInfo.lumaWorkaround.enabled)
    {
        gl::Error error = mBlitter->copyImageToLUMAWorkaroundTexture(
            mTextureID, mState.mTarget, target, levelInfo.sourceFormat, level, sourceArea,
            copyTexImageFormat.internalFormat, source);
        if (error.isError())
        {
            return error;
        }
    }
    else
    {
        const FramebufferGL *sourceFramebufferGL = GetImplAs<FramebufferGL>(source);

        mStateManager->bindTexture(mState.mTarget, mTextureID);
        mStateManager->bindFramebuffer(GL_READ_FRAMEBUFFER,
                                       sourceFramebufferGL->getFramebufferID());

        if (UseTexImage2D(mState.mTarget))
        {
            mFunctions->copyTexImage2D(target, static_cast<GLint>(level),
                                       copyTexImageFormat.internalFormat, sourceArea.x,
                                       sourceArea.y, sourceArea.width, sourceArea.height, 0);
        }
        else
        {
            UNREACHABLE();
        }
    }

    mLevelInfo[level] = levelInfo;

    return gl::Error(GL_NO_ERROR);
}

gl::Error TextureGL::copySubImage(GLenum target, size_t level, const gl::Offset &destOffset, const gl::Rectangle &sourceArea,
                                  const gl::Framebuffer *source)
{
    const FramebufferGL *sourceFramebufferGL = GetImplAs<FramebufferGL>(source);

    mStateManager->bindTexture(mState.mTarget, mTextureID);
    mStateManager->bindFramebuffer(GL_READ_FRAMEBUFFER, sourceFramebufferGL->getFramebufferID());

    const LevelInfoGL &levelInfo = mLevelInfo[level];
    if (levelInfo.lumaWorkaround.enabled)
    {
        gl::Error error = mBlitter->copySubImageToLUMAWorkaroundTexture(
            mTextureID, mState.mTarget, target, levelInfo.sourceFormat, level, destOffset,
            sourceArea, source);
        if (error.isError())
        {
            return error;
        }
    }
    else
    {
        if (UseTexImage2D(mState.mTarget))
        {
            ASSERT(destOffset.z == 0);
            mFunctions->copyTexSubImage2D(target, static_cast<GLint>(level), destOffset.x,
                                          destOffset.y, sourceArea.x, sourceArea.y,
                                          sourceArea.width, sourceArea.height);
        }
        else if (UseTexImage3D(mState.mTarget))
        {
            mFunctions->copyTexSubImage3D(target, static_cast<GLint>(level), destOffset.x,
                                          destOffset.y, destOffset.z, sourceArea.x, sourceArea.y,
                                          sourceArea.width, sourceArea.height);
        }
        else
        {
            UNREACHABLE();
        }
    }

    return gl::Error(GL_NO_ERROR);
}

gl::Error TextureGL::setStorage(GLenum target, size_t levels, GLenum internalFormat, const gl::Extents &size)
{
    // TODO: emulate texture storage with TexImage calls if on GL version <4.2 or the
    // ARB_texture_storage extension is not available.

    nativegl::TexStorageFormat texStorageFormat =
        nativegl::GetTexStorageFormat(mFunctions, mWorkarounds, internalFormat);

    mStateManager->bindTexture(mState.mTarget, mTextureID);
    if (UseTexImage2D(mState.mTarget))
    {
        ASSERT(size.depth == 1);
        if (mFunctions->texStorage2D)
        {
            mFunctions->texStorage2D(target, static_cast<GLsizei>(levels),
                                     texStorageFormat.internalFormat, size.width, size.height);
        }
        else
        {
            // Make sure no pixel unpack buffer is bound
            mStateManager->bindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

            const gl::InternalFormat &internalFormatInfo = gl::GetInternalFormatInfo(internalFormat);

            // Internal format must be sized
            ASSERT(internalFormatInfo.pixelBytes != 0);

            for (size_t level = 0; level < levels; level++)
            {
                gl::Extents levelSize(std::max(size.width >> level, 1),
                                      std::max(size.height >> level, 1),
                                      1);

                if (mState.mTarget == GL_TEXTURE_2D)
                {
                    if (internalFormatInfo.compressed)
                    {
                        size_t dataSize = internalFormatInfo.computeBlockSize(GL_UNSIGNED_BYTE, levelSize.width, levelSize.height);
                        mFunctions->compressedTexImage2D(target, static_cast<GLint>(level),
                                                         texStorageFormat.internalFormat,
                                                         levelSize.width, levelSize.height, 0,
                                                         static_cast<GLsizei>(dataSize), nullptr);
                    }
                    else
                    {
                        mFunctions->texImage2D(target, static_cast<GLint>(level),
                                               texStorageFormat.internalFormat, levelSize.width,
                                               levelSize.height, 0, internalFormatInfo.format,
                                               internalFormatInfo.type, nullptr);
                    }
                }
                else if (mState.mTarget == GL_TEXTURE_CUBE_MAP)
                {
                    for (GLenum face = gl::FirstCubeMapTextureTarget; face <= gl::LastCubeMapTextureTarget; face++)
                    {
                        if (internalFormatInfo.compressed)
                        {
                            size_t dataSize = internalFormatInfo.computeBlockSize(GL_UNSIGNED_BYTE, levelSize.width, levelSize.height);
                            mFunctions->compressedTexImage2D(
                                face, static_cast<GLint>(level), texStorageFormat.internalFormat,
                                levelSize.width, levelSize.height, 0,
                                static_cast<GLsizei>(dataSize), nullptr);
                        }
                        else
                        {
                            mFunctions->texImage2D(face, static_cast<GLint>(level),
                                                   texStorageFormat.internalFormat, levelSize.width,
                                                   levelSize.height, 0, internalFormatInfo.format,
                                                   internalFormatInfo.type, nullptr);
                        }
                    }
                }
                else
                {
                    UNREACHABLE();
                }
            }
        }
    }
    else if (UseTexImage3D(mState.mTarget))
    {
        if (mFunctions->texStorage3D)
        {
            mFunctions->texStorage3D(target, static_cast<GLsizei>(levels),
                                     texStorageFormat.internalFormat, size.width, size.height,
                                     size.depth);
        }
        else
        {
            // Make sure no pixel unpack buffer is bound
            mStateManager->bindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

            const gl::InternalFormat &internalFormatInfo = gl::GetInternalFormatInfo(internalFormat);

            // Internal format must be sized
            ASSERT(internalFormatInfo.pixelBytes != 0);

            for (GLsizei i = 0; i < static_cast<GLsizei>(levels); i++)
            {
                gl::Extents levelSize(
                    std::max(size.width >> i, 1), std::max(size.height >> i, 1),
                    mState.mTarget == GL_TEXTURE_3D ? std::max(size.depth >> i, 1) : size.depth);

                if (internalFormatInfo.compressed)
                {
                    GLsizei dataSize = static_cast<GLsizei>(internalFormatInfo.computeBlockSize(
                                           GL_UNSIGNED_BYTE, levelSize.width, levelSize.height)) *
                                       levelSize.depth;
                    mFunctions->compressedTexImage3D(target, i, texStorageFormat.internalFormat,
                                                     levelSize.width, levelSize.height,
                                                     levelSize.depth, 0, dataSize, nullptr);
                }
                else
                {
                    mFunctions->texImage3D(target, i, texStorageFormat.internalFormat,
                                           levelSize.width, levelSize.height, levelSize.depth, 0,
                                           internalFormatInfo.format, internalFormatInfo.type,
                                           nullptr);
                }
            }
        }
    }
    else
    {
        UNREACHABLE();
    }

    LevelInfoGL levelInfo = GetLevelInfo(internalFormat, texStorageFormat.internalFormat);
    for (size_t level = 0; level < mLevelInfo.size(); level++)
    {
        mLevelInfo[level] = levelInfo;
    }

    return gl::Error(GL_NO_ERROR);
}

gl::Error TextureGL::setImageExternal(GLenum target,
                                      egl::Stream *stream,
                                      const egl::Stream::GLTextureDescription &desc)
{
    UNIMPLEMENTED();
    return gl::Error(GL_INVALID_OPERATION);
}

gl::Error TextureGL::generateMipmaps()
{
    mStateManager->bindTexture(mState.mTarget, mTextureID);
    mFunctions->generateMipmap(mState.mTarget);

    for (size_t level = mState.mBaseLevel; level < mLevelInfo.size(); level++)
    {
        mLevelInfo[level] = mLevelInfo[mState.mBaseLevel];
    }

    return gl::Error(GL_NO_ERROR);
}

void TextureGL::bindTexImage(egl::Surface *surface)
{
    ASSERT(mState.mTarget == GL_TEXTURE_2D);

    // Make sure this texture is bound
    mStateManager->bindTexture(mState.mTarget, mTextureID);

    mLevelInfo[0] = LevelInfoGL();
}

void TextureGL::releaseTexImage()
{
    // Not all Surface implementations reset the size of mip 0 when releasing, do it manually
    ASSERT(mState.mTarget == GL_TEXTURE_2D);

    mStateManager->bindTexture(mState.mTarget, mTextureID);
    if (UseTexImage2D(mState.mTarget))
    {
        mFunctions->texImage2D(mState.mTarget, 0, GL_RGBA, 0, 0, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                               nullptr);
    }
    else
    {
        UNREACHABLE();
    }
}

gl::Error TextureGL::setEGLImageTarget(GLenum target, egl::Image *image)
{
    UNIMPLEMENTED();
    return gl::Error(GL_INVALID_OPERATION);
}

template <typename T, typename ApplyTextureFuncType>
static inline void SyncSamplerStateMember(const FunctionsGL *functions,
                                          ApplyTextureFuncType applyTextureFunc,
                                          const gl::SamplerState &newState,
                                          gl::SamplerState &curState,
                                          GLenum textureType,
                                          GLenum name,
                                          T(gl::SamplerState::*samplerMember))
{
    if (curState.*samplerMember != newState.*samplerMember)
    {
        applyTextureFunc();
        curState.*samplerMember = newState.*samplerMember;
        functions->texParameterf(textureType, name, static_cast<GLfloat>(curState.*samplerMember));
    }
}

template <typename T, typename ApplyTextureFuncType>
static inline void SyncTextureStateMember(const FunctionsGL *functions,
                                          ApplyTextureFuncType applyTextureFunc,
                                          const gl::TextureState &newState,
                                          gl::TextureState &curState,
                                          GLenum textureType,
                                          GLenum name,
                                          T(gl::TextureState::*stateMember))
{
    if (curState.*stateMember != newState.*stateMember)
    {
        applyTextureFunc();
        curState.*stateMember = newState.*stateMember;
        functions->texParameterf(textureType, name, static_cast<GLfloat>(curState.*stateMember));
    }
}

template <typename T, typename ApplyTextureFuncType>
static inline void SyncTextureStateSwizzle(const FunctionsGL *functions,
                                           ApplyTextureFuncType applyTextureFunc,
                                           const LevelInfoGL &levelInfo,
                                           const gl::SwizzleState &newState,
                                           gl::SwizzleState &curState,
                                           GLenum textureType,
                                           GLenum name,
                                           T(gl::SwizzleState::*stateMember))
{
    GLenum resultSwizzle = newState.*stateMember;
    if (levelInfo.lumaWorkaround.enabled || levelInfo.depthStencilWorkaround)
    {
        if (levelInfo.lumaWorkaround.enabled)
        {
            UNUSED_ASSERTION_VARIABLE(levelInfo.lumaWorkaround.workaroundFormat);

            switch (newState.*stateMember)
            {
            case GL_RED:
            case GL_GREEN:
            case GL_BLUE:
                if (levelInfo.sourceFormat == GL_LUMINANCE ||
                    levelInfo.sourceFormat == GL_LUMINANCE_ALPHA)
                {
                    // Texture is backed by a RED or RG texture, point all color channels at the red
                    // channel.
                    ASSERT(levelInfo.lumaWorkaround.workaroundFormat == GL_RED ||
                           levelInfo.lumaWorkaround.workaroundFormat == GL_RG);
                    resultSwizzle = GL_RED;
                }
                else if (levelInfo.sourceFormat == GL_ALPHA)
                {
                    // Color channels are not supposed to exist, make them always sample 0.
                    resultSwizzle = GL_ZERO;
                }
                else
                {
                    UNREACHABLE();
                }
                break;

            case GL_ALPHA:
                if (levelInfo.sourceFormat == GL_LUMINANCE)
                {
                    // Alpha channel is not supposed to exist, make it always sample 1.
                    resultSwizzle = GL_ONE;
                }
                else if (levelInfo.sourceFormat == GL_ALPHA)
                {
                    // Texture is backed by a RED texture, point the alpha channel at the red
                    // channel.
                    ASSERT(levelInfo.lumaWorkaround.workaroundFormat == GL_RED);
                    resultSwizzle = GL_RED;
                }
                else if (levelInfo.sourceFormat == GL_LUMINANCE_ALPHA)
                {
                    // Texture is backed by an RG texture, point the alpha channel at the green
                    // channel.
                    ASSERT(levelInfo.lumaWorkaround.workaroundFormat == GL_RG);
                    resultSwizzle = GL_GREEN;
                }
                else
                {
                    UNREACHABLE();
                }
                break;

            case GL_ZERO:
            case GL_ONE:
                // Don't modify the swizzle state when requesting ZERO or ONE.
                resultSwizzle = newState.*stateMember;
                break;

            default:
                UNREACHABLE();
                break;
            }
        }
        else if (levelInfo.depthStencilWorkaround)
        {
            switch (newState.*stateMember)
            {
                case GL_RED:
                    // Don't modify the swizzle state when requesting the red channel.
                    resultSwizzle = newState.*stateMember;
                    break;

                case GL_GREEN:
                case GL_BLUE:
                    // Depth textures should sample 0 from the green and blue channels.
                    resultSwizzle = GL_ZERO;
                    break;

                case GL_ALPHA:
                    // Depth textures should sample 1 from the alpha channel.
                    resultSwizzle = GL_ONE;
                    break;

                case GL_ZERO:
                case GL_ONE:
                    // Don't modify the swizzle state when requesting ZERO or ONE.
                    resultSwizzle = newState.*stateMember;
                    break;

                default:
                    UNREACHABLE();
                    break;
            }
        }
        else
        {
            UNREACHABLE();
        }

    }

    if (curState.*stateMember != resultSwizzle)
    {
        applyTextureFunc();
        curState.*stateMember = resultSwizzle;
        functions->texParameterf(textureType, name, static_cast<GLfloat>(resultSwizzle));
    }
}

void TextureGL::syncState(size_t textureUnit) const
{
    // Callback lamdba to bind this texture only if needed.
    bool textureApplied   = false;
    auto applyTextureFunc = [&]()
    {
        if (!textureApplied)
        {
            mStateManager->activeTexture(textureUnit);
            mStateManager->bindTexture(mState.mTarget, mTextureID);
            textureApplied = true;
        }
    };

    // Sync texture state
    // Apply the effective base level and max level instead of the base level and max level set from
    // the API. This can help with buggy drivers.
    if (mAppliedTextureState.getEffectiveBaseLevel() != mState.getEffectiveBaseLevel())
    {
        applyTextureFunc();
        mFunctions->texParameteri(mState.mTarget, GL_TEXTURE_BASE_LEVEL,
                                  mState.getEffectiveBaseLevel());
    }
    mAppliedTextureState.mBaseLevel = mState.mBaseLevel;
    if (mAppliedTextureState.getEffectiveMaxLevel() != mState.getEffectiveMaxLevel())
    {
        applyTextureFunc();
        mFunctions->texParameteri(mState.mTarget, GL_TEXTURE_MAX_LEVEL,
                                  mState.getEffectiveMaxLevel());
    }
    mAppliedTextureState.mMaxLevel = mState.mMaxLevel;

    // clang-format off
    const LevelInfoGL &levelInfo = mLevelInfo[mState.getEffectiveBaseLevel()];
    SyncTextureStateSwizzle(mFunctions, applyTextureFunc, levelInfo, mState.mSwizzleState, mAppliedTextureState.mSwizzleState, mState.mTarget, GL_TEXTURE_SWIZZLE_R, &gl::SwizzleState::swizzleRed);
    SyncTextureStateSwizzle(mFunctions, applyTextureFunc, levelInfo, mState.mSwizzleState, mAppliedTextureState.mSwizzleState, mState.mTarget, GL_TEXTURE_SWIZZLE_G, &gl::SwizzleState::swizzleGreen);
    SyncTextureStateSwizzle(mFunctions, applyTextureFunc, levelInfo, mState.mSwizzleState, mAppliedTextureState.mSwizzleState, mState.mTarget, GL_TEXTURE_SWIZZLE_B, &gl::SwizzleState::swizzleBlue);
    SyncTextureStateSwizzle(mFunctions, applyTextureFunc, levelInfo, mState.mSwizzleState, mAppliedTextureState.mSwizzleState, mState.mTarget, GL_TEXTURE_SWIZZLE_A, &gl::SwizzleState::swizzleAlpha);

    // Sync sampler state
    SyncSamplerStateMember(mFunctions, applyTextureFunc, mState.mSamplerState, mAppliedTextureState.mSamplerState, mState.mTarget, GL_TEXTURE_MIN_FILTER, &gl::SamplerState::minFilter);
    SyncSamplerStateMember(mFunctions, applyTextureFunc, mState.mSamplerState, mAppliedTextureState.mSamplerState, mState.mTarget, GL_TEXTURE_MAG_FILTER, &gl::SamplerState::magFilter);
    SyncSamplerStateMember(mFunctions, applyTextureFunc, mState.mSamplerState, mAppliedTextureState.mSamplerState, mState.mTarget, GL_TEXTURE_WRAP_S, &gl::SamplerState::wrapS);
    SyncSamplerStateMember(mFunctions, applyTextureFunc, mState.mSamplerState, mAppliedTextureState.mSamplerState, mState.mTarget, GL_TEXTURE_WRAP_T, &gl::SamplerState::wrapT);
    SyncSamplerStateMember(mFunctions, applyTextureFunc, mState.mSamplerState, mAppliedTextureState.mSamplerState, mState.mTarget, GL_TEXTURE_WRAP_R, &gl::SamplerState::wrapR);
    SyncSamplerStateMember(mFunctions, applyTextureFunc, mState.mSamplerState, mAppliedTextureState.mSamplerState, mState.mTarget, GL_TEXTURE_MAX_ANISOTROPY_EXT, &gl::SamplerState::maxAnisotropy);
    SyncSamplerStateMember(mFunctions, applyTextureFunc, mState.mSamplerState, mAppliedTextureState.mSamplerState, mState.mTarget, GL_TEXTURE_MIN_LOD, &gl::SamplerState::minLod);
    SyncSamplerStateMember(mFunctions, applyTextureFunc, mState.mSamplerState, mAppliedTextureState.mSamplerState, mState.mTarget, GL_TEXTURE_MAX_LOD, &gl::SamplerState::maxLod);
    SyncSamplerStateMember(mFunctions, applyTextureFunc, mState.mSamplerState, mAppliedTextureState.mSamplerState, mState.mTarget, GL_TEXTURE_COMPARE_MODE, &gl::SamplerState::compareMode);
    SyncSamplerStateMember(mFunctions, applyTextureFunc, mState.mSamplerState, mAppliedTextureState.mSamplerState, mState.mTarget, GL_TEXTURE_COMPARE_FUNC, &gl::SamplerState::compareFunc);
    // clang-format on
}

GLuint TextureGL::getTextureID() const
{
    return mTextureID;
}

}
