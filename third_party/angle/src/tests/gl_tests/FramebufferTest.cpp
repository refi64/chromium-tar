//
// Copyright 2015 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Framebuffer tests:
//   Various tests related for Frambuffers.
//

#include "platform/FeaturesD3D.h"
#include "test_utils/ANGLETest.h"
#include "test_utils/gl_raii.h"

using namespace angle;

namespace
{

void ExpectFramebufferCompleteOrUnsupported(GLenum binding)
{
    GLenum status = glCheckFramebufferStatus(binding);
    EXPECT_TRUE(status == GL_FRAMEBUFFER_COMPLETE || status == GL_FRAMEBUFFER_UNSUPPORTED);
}

}  // anonymous namespace

class FramebufferFormatsTest : public ANGLETest
{
  protected:
    FramebufferFormatsTest() : mFramebuffer(0), mTexture(0), mRenderbuffer(0), mProgram(0)
    {
        setWindowWidth(128);
        setWindowHeight(128);
        setConfigRedBits(8);
        setConfigGreenBits(8);
        setConfigBlueBits(8);
        setConfigAlphaBits(8);
    }

    void checkBitCount(GLuint fbo, GLenum channel, GLint minBits)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);

        GLint bits = 0;
        glGetIntegerv(channel, &bits);

        if (minBits == 0)
        {
            EXPECT_EQ(minBits, bits);
        }
        else
        {
            EXPECT_GE(bits, minBits);
        }
    }

    void testBitCounts(GLuint fbo,
                       GLint minRedBits,
                       GLint minGreenBits,
                       GLint minBlueBits,
                       GLint minAlphaBits,
                       GLint minDepthBits,
                       GLint minStencilBits)
    {
        checkBitCount(fbo, GL_RED_BITS, minRedBits);
        checkBitCount(fbo, GL_GREEN_BITS, minGreenBits);
        checkBitCount(fbo, GL_BLUE_BITS, minBlueBits);
        checkBitCount(fbo, GL_ALPHA_BITS, minAlphaBits);
        checkBitCount(fbo, GL_DEPTH_BITS, minDepthBits);
        checkBitCount(fbo, GL_STENCIL_BITS, minStencilBits);
    }

    void testTextureFormat(GLenum internalFormat,
                           GLint minRedBits,
                           GLint minGreenBits,
                           GLint minBlueBits,
                           GLint minAlphaBits)
    {
        glGenTextures(1, &mTexture);
        glBindTexture(GL_TEXTURE_2D, mTexture);

        if (getClientMajorVersion() >= 3)
        {
            glTexStorage2D(GL_TEXTURE_2D, 1, internalFormat, 1, 1);
        }
        else
        {
            glTexStorage2DEXT(GL_TEXTURE_2D, 1, internalFormat, 1, 1);
        }

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTexture, 0);

        testBitCounts(mFramebuffer, minRedBits, minGreenBits, minBlueBits, minAlphaBits, 0, 0);
    }

    void testRenderbufferMultisampleFormat(int minESVersion,
                                           GLenum attachmentType,
                                           GLenum internalFormat)
    {
        int clientVersion = getClientMajorVersion();
        if (clientVersion < minESVersion)
        {
            return;
        }

        // Check that multisample is supported with at least two samples (minimum required is 1)
        bool supports2Samples = false;

        if (clientVersion == 2)
        {
            if (IsGLExtensionEnabled("ANGLE_framebuffer_multisample"))
            {
                int maxSamples;
                glGetIntegerv(GL_MAX_SAMPLES_ANGLE, &maxSamples);
                supports2Samples = maxSamples >= 2;
            }
        }
        else
        {
            assert(clientVersion >= 3);
            int maxSamples;
            glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
            supports2Samples = maxSamples >= 2;
        }

        if (!supports2Samples)
        {
            return;
        }

        glGenRenderbuffers(1, &mRenderbuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, mRenderbuffer);

        EXPECT_GL_NO_ERROR();
        glRenderbufferStorageMultisampleANGLE(GL_RENDERBUFFER, 2, internalFormat, 128, 128);
        EXPECT_GL_NO_ERROR();
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachmentType, GL_RENDERBUFFER, mRenderbuffer);
        EXPECT_GL_NO_ERROR();
    }

    void testZeroHeightRenderbuffer()
    {
        glGenRenderbuffers(1, &mRenderbuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, mRenderbuffer);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 1, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
                                  mRenderbuffer);
        EXPECT_GL_NO_ERROR();
    }

    void testSetUp() override
    {
        glGenFramebuffers(1, &mFramebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer);
    }

    void testTearDown() override
    {
        if (mTexture != 0)
        {
            glDeleteTextures(1, &mTexture);
            mTexture = 0;
        }

        if (mRenderbuffer != 0)
        {
            glDeleteRenderbuffers(1, &mRenderbuffer);
            mRenderbuffer = 0;
        }

        if (mFramebuffer != 0)
        {
            glDeleteFramebuffers(1, &mFramebuffer);
            mFramebuffer = 0;
        }

        if (mProgram != 0)
        {
            glDeleteProgram(mProgram);
            mProgram = 0;
        }
    }

    GLuint mFramebuffer;
    GLuint mTexture;
    GLuint mRenderbuffer;
    GLuint mProgram;
};

TEST_P(FramebufferFormatsTest, RGBA4)
{
    ANGLE_SKIP_TEST_IF(getClientMajorVersion() < 3 &&
                       !IsGLExtensionEnabled("GL_EXT_texture_storage"));

    testTextureFormat(GL_RGBA4, 4, 4, 4, 4);
}

TEST_P(FramebufferFormatsTest, RGB565)
{
    ANGLE_SKIP_TEST_IF(getClientMajorVersion() < 3 &&
                       !IsGLExtensionEnabled("GL_EXT_texture_storage"));

    testTextureFormat(GL_RGB565, 5, 6, 5, 0);
}

TEST_P(FramebufferFormatsTest, RGB8)
{
    ANGLE_SKIP_TEST_IF(getClientMajorVersion() < 3 &&
                       (!IsGLExtensionEnabled("GL_OES_rgb8_rgba8") ||
                        !IsGLExtensionEnabled("GL_EXT_texture_storage")));

    testTextureFormat(GL_RGB8_OES, 8, 8, 8, 0);
}

TEST_P(FramebufferFormatsTest, BGRA8)
{
    ANGLE_SKIP_TEST_IF(
        !IsGLExtensionEnabled("GL_EXT_texture_format_BGRA8888") ||
        (getClientMajorVersion() < 3 && !IsGLExtensionEnabled("GL_EXT_texture_storage")));

    testTextureFormat(GL_BGRA8_EXT, 8, 8, 8, 8);
}

TEST_P(FramebufferFormatsTest, RGBA8)
{
    ANGLE_SKIP_TEST_IF(getClientMajorVersion() < 3 &&
                       (!IsGLExtensionEnabled("GL_OES_rgb8_rgba8") ||
                        !IsGLExtensionEnabled("GL_EXT_texture_storage")));

    testTextureFormat(GL_RGBA8_OES, 8, 8, 8, 8);
}

TEST_P(FramebufferFormatsTest, RenderbufferMultisample_DEPTH16)
{
    testRenderbufferMultisampleFormat(2, GL_DEPTH_ATTACHMENT, GL_DEPTH_COMPONENT16);
}

TEST_P(FramebufferFormatsTest, RenderbufferMultisample_DEPTH24)
{
    testRenderbufferMultisampleFormat(3, GL_DEPTH_ATTACHMENT, GL_DEPTH_COMPONENT24);
}

TEST_P(FramebufferFormatsTest, RenderbufferMultisample_DEPTH32F)
{
    ANGLE_SKIP_TEST_IF(getClientMajorVersion() < 3);

    testRenderbufferMultisampleFormat(3, GL_DEPTH_ATTACHMENT, GL_DEPTH_COMPONENT32F);
}

TEST_P(FramebufferFormatsTest, RenderbufferMultisample_DEPTH24_STENCIL8)
{
    testRenderbufferMultisampleFormat(3, GL_DEPTH_STENCIL_ATTACHMENT, GL_DEPTH24_STENCIL8);
}

TEST_P(FramebufferFormatsTest, RenderbufferMultisample_DEPTH32F_STENCIL8)
{
    ANGLE_SKIP_TEST_IF(getClientMajorVersion() < 3);

    testRenderbufferMultisampleFormat(3, GL_DEPTH_STENCIL_ATTACHMENT, GL_DEPTH32F_STENCIL8);
}

TEST_P(FramebufferFormatsTest, RenderbufferMultisample_STENCIL_INDEX8)
{
    // TODO(geofflang): Figure out how to support GLSTENCIL_INDEX8 on desktop GL
    ANGLE_SKIP_TEST_IF(IsDesktopOpenGL());

    testRenderbufferMultisampleFormat(2, GL_STENCIL_ATTACHMENT, GL_STENCIL_INDEX8);
}

// Test that binding an incomplete cube map is rejected by ANGLE.
TEST_P(FramebufferFormatsTest, IncompleteCubeMap)
{
    // http://anglebug.com/3145
    ANGLE_SKIP_TEST_IF(IsFuchsia() && IsIntel() && IsVulkan());

    // First make a complete CubeMap.
    glGenTextures(1, &mTexture);
    glBindTexture(GL_TEXTURE_CUBE_MAP, mTexture);
    glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
    glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_Y, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
    glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_Z, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
    glTexImage2D(GL_TEXTURE_CUBE_MAP_NEGATIVE_X, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
    glTexImage2D(GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
    glTexImage2D(GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X,
                           mTexture, 0);

    // Verify the framebuffer is complete.
    ASSERT_GL_FRAMEBUFFER_COMPLETE(GL_FRAMEBUFFER);

    // Make the CubeMap cube-incomplete.
    glTexImage2D(GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);

    // Verify the framebuffer is incomplete.
    ASSERT_GLENUM_EQ(GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT,
                     glCheckFramebufferStatus(GL_FRAMEBUFFER));

    ASSERT_GL_NO_ERROR();

    // Verify drawing with the incomplete framebuffer produces a GL error
    mProgram = CompileProgram(essl1_shaders::vs::Simple(), essl1_shaders::fs::Red());
    ASSERT_NE(0u, mProgram);
    drawQuad(mProgram, essl1_shaders::PositionAttrib(), 0.5f);
    ASSERT_GL_ERROR(GL_INVALID_FRAMEBUFFER_OPERATION);
}

// Test that a renderbuffer with zero height but nonzero width is handled without crashes/asserts.
TEST_P(FramebufferFormatsTest, ZeroHeightRenderbuffer)
{
    ANGLE_SKIP_TEST_IF(getClientMajorVersion() < 3);

    testZeroHeightRenderbuffer();
}

// Test to cover a bug where the read framebuffer affects the completeness of the draw framebuffer.
TEST_P(FramebufferFormatsTest, ReadDrawCompleteness)
{
    ANGLE_SKIP_TEST_IF(getClientMajorVersion() < 3);

    GLTexture incompleteTexture;
    glBindTexture(GL_TEXTURE_2D, incompleteTexture);

    GLFramebuffer incompleteFBO;
    glBindFramebuffer(GL_FRAMEBUFFER, incompleteFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, incompleteTexture,
                           0);
    EXPECT_GLENUM_EQ(GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT,
                     glCheckFramebufferStatus(GL_FRAMEBUFFER));

    GLTexture completeTexture;
    glBindTexture(GL_TEXTURE_2D, completeTexture);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, getWindowWidth(), getWindowHeight());

    GLFramebuffer completeFBO;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, completeFBO);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           completeTexture, 0);

    EXPECT_GLENUM_EQ(GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT,
                     glCheckFramebufferStatus(GL_READ_FRAMEBUFFER));
    EXPECT_GLENUM_EQ(GL_FRAMEBUFFER_COMPLETE, glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER));

    ASSERT_GL_NO_ERROR();

    // Simple draw program.
    ANGLE_GL_PROGRAM(program, essl1_shaders::vs::Simple(), essl1_shaders::fs::Red());

    drawQuad(program, essl1_shaders::PositionAttrib(), 0.5f, 1.0f, true);
    EXPECT_GL_NO_ERROR();

    glBindFramebuffer(GL_READ_FRAMEBUFFER, completeFBO);
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::red);
}

// Test that a renderbuffer with RGB565 format works as expected. This test is intended for some
// back-end having no support for native RGB565 renderbuffer and thus having to emulate using RGBA
// format.
TEST_P(FramebufferFormatsTest, RGB565Renderbuffer)
{
    GLRenderbuffer rbo;
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGB565, 1, 1);

    GLFramebuffer completeFBO;
    glBindFramebuffer(GL_FRAMEBUFFER, completeFBO);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbo);

    EXPECT_GLENUM_EQ(GL_FRAMEBUFFER_COMPLETE, glCheckFramebufferStatus(GL_FRAMEBUFFER));

    ASSERT_GL_NO_ERROR();

    glClearColor(1, 0, 0, 0.5f);
    glClear(GL_COLOR_BUFFER_BIT);
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::red);
}

class FramebufferTest_ES3 : public ANGLETest
{};

// Covers invalidating an incomplete framebuffer. This should be a no-op, but should not error.
TEST_P(FramebufferTest_ES3, InvalidateIncomplete)
{
    GLFramebuffer framebuffer;
    GLRenderbuffer renderbuffer;

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, renderbuffer);
    EXPECT_GLENUM_EQ(GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT,
                     glCheckFramebufferStatus(GL_FRAMEBUFFER));

    std::vector<GLenum> attachments;
    attachments.push_back(GL_COLOR_ATTACHMENT0);

    glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, attachments.data());
    EXPECT_GL_NO_ERROR();
}

// Test that the framebuffer state tracking robustly handles a depth-only attachment being set
// as a depth-stencil attachment. It is equivalent to detaching the depth-stencil attachment.
TEST_P(FramebufferTest_ES3, DepthOnlyAsDepthStencil)
{
    GLFramebuffer framebuffer;
    GLRenderbuffer renderbuffer;

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, 4, 4);

    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER,
                              renderbuffer);
    EXPECT_GLENUM_NE(GL_FRAMEBUFFER_COMPLETE, glCheckFramebufferStatus(GL_FRAMEBUFFER));
}

// Test that the framebuffer correctly returns that it is not complete if invalid texture mip levels
// are bound
TEST_P(FramebufferTest_ES3, TextureAttachmentMipLevels)
{
    GLFramebuffer framebuffer;
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

    GLTexture texture;
    glBindTexture(GL_TEXTURE_2D, texture);

    // Create a complete mip chain in mips 1 to 3
    glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexImage2D(GL_TEXTURE_2D, 2, GL_RGBA8, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexImage2D(GL_TEXTURE_2D, 3, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    // Create another complete mip chain in mips 4 to 5
    glTexImage2D(GL_TEXTURE_2D, 4, GL_RGBA8, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexImage2D(GL_TEXTURE_2D, 5, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    // Create a non-complete mip chain in mip 6
    glTexImage2D(GL_TEXTURE_2D, 6, GL_RGBA8, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    // Incomplete, mipLevel != baseLevel and texture is not mip complete
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 1);
    EXPECT_GLENUM_EQ(GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT,
                     glCheckFramebufferStatus(GL_FRAMEBUFFER));

    // Complete, mipLevel == baseLevel
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 1);
    ExpectFramebufferCompleteOrUnsupported(GL_FRAMEBUFFER);

    // Complete, mipLevel != baseLevel but texture is now mip complete
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 2);
    ExpectFramebufferCompleteOrUnsupported(GL_FRAMEBUFFER);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 3);
    ExpectFramebufferCompleteOrUnsupported(GL_FRAMEBUFFER);

    // Incomplete, attached level below the base level
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 2);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 1);
    EXPECT_GLENUM_EQ(GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT,
                     glCheckFramebufferStatus(GL_FRAMEBUFFER));

    // Incomplete, attached level is beyond effective max level
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 4);
    EXPECT_GLENUM_EQ(GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT,
                     glCheckFramebufferStatus(GL_FRAMEBUFFER));

    // Complete, mipLevel == baseLevel
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 4);
    ExpectFramebufferCompleteOrUnsupported(GL_FRAMEBUFFER);

    // Complete, mipLevel != baseLevel but texture is now mip complete
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 5);
    ExpectFramebufferCompleteOrUnsupported(GL_FRAMEBUFFER);

    // Complete, mipLevel == baseLevel
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 6);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 6);
    ExpectFramebufferCompleteOrUnsupported(GL_FRAMEBUFFER);
}

TEST_P(FramebufferTest_ES3, TextureAttachmentMipLevelsReadBack)
{
#if defined(ADDRESS_SANITIZER)
    // http://anglebug.com/4737
    ANGLE_SKIP_TEST_IF(IsOSX());
#endif

    GLFramebuffer framebuffer;
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

    GLTexture texture;
    glBindTexture(GL_TEXTURE_2D, texture);

    const std::array<GLColor, 2 * 2> mip0Data = {GLColor::red, GLColor::red, GLColor::red,
                                                 GLColor::red};
    const std::array<GLColor, 1 * 1> mip1Data = {GLColor::green};

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, mip0Data.data());
    glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, mip1Data.data());

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 1);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 1);
    EXPECT_GL_FRAMEBUFFER_COMPLETE(GL_FRAMEBUFFER);

    glClearColor(0, 0, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::blue);
}

// TextureAttachmentMipLevelsReadBackWithDraw is a copy of TextureAttachmentMipLevelsReadBack except
// for adding a draw after the last clear. The draw forces ANGLE's Vulkan backend to use the
// framebuffer that is level 1 of the texture which will trigger the mismatch use of the GL level
// and Vulkan level in referring to that rendertarget.
TEST_P(FramebufferTest_ES3, TextureAttachmentMipLevelsReadBackWithDraw)
{
#if defined(ADDRESS_SANITIZER)
    // http://anglebug.com/4737
    ANGLE_SKIP_TEST_IF(IsOSX());
#endif

    ANGLE_GL_PROGRAM(greenProgram, essl1_shaders::vs::Simple(), essl1_shaders::fs::Green());

    GLFramebuffer framebuffer;
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

    GLTexture texture;
    glBindTexture(GL_TEXTURE_2D, texture);

    const std::array<GLColor, 2 * 2> mip0Data = {GLColor::red, GLColor::red, GLColor::red,
                                                 GLColor::red};
    const std::array<GLColor, 1 * 1> mip1Data = {GLColor::green};

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, mip0Data.data());
    glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, mip1Data.data());

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 1);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 1);
    EXPECT_GL_FRAMEBUFFER_COMPLETE(GL_FRAMEBUFFER);

    glClearColor(0, 0, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // This draw triggers the use of the framebuffer
    glUseProgram(greenProgram);
    drawQuad(greenProgram.get(), std::string(essl1_shaders::PositionAttrib()), 0.0f);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);
}

// Test that passing an attachment COLOR_ATTACHMENTm where m is equal to MAX_COLOR_ATTACHMENTS
// generates an INVALID_OPERATION.
// OpenGL ES Version 3.0.5 (November 3, 2016), 4.4.2.4 Attaching Texture Images to a Framebuffer, p.
// 208
TEST_P(FramebufferTest_ES3, ColorAttachmentIndexOutOfBounds)
{
    GLFramebuffer framebuffer;
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer.get());

    GLint maxColorAttachments = 0;
    glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &maxColorAttachments);
    GLenum attachment = static_cast<GLenum>(maxColorAttachments + GL_COLOR_ATTACHMENT0);

    GLTexture texture;
    glBindTexture(GL_TEXTURE_2D, texture.get());
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, 1, 1);
    glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, texture.get(), 0);
    EXPECT_GL_ERROR(GL_INVALID_OPERATION);
}

// Check that depth-only attachments report the correct number of samples.
TEST_P(FramebufferTest_ES3, MultisampleDepthOnly)
{
    GLRenderbuffer renderbuffer;
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, 2, GL_DEPTH_COMPONENT24, 32, 32);

    GLFramebuffer framebuffer;
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, renderbuffer);
    ASSERT_GL_FRAMEBUFFER_COMPLETE(GL_FRAMEBUFFER);
    EXPECT_GL_NO_ERROR();

    GLint samples = 0;
    glGetIntegerv(GL_SAMPLES, &samples);
    EXPECT_GL_NO_ERROR();
    EXPECT_GE(samples, 2);
}

// Check that we only compare width and height of attachments, not depth.
TEST_P(FramebufferTest_ES3, AttachmentWith3DLayers)
{
    GLTexture texA;
    glBindTexture(GL_TEXTURE_2D, texA);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    GLTexture texB;
    glBindTexture(GL_TEXTURE_3D, texB);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, 4, 4, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    GLFramebuffer framebuffer;
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texA, 0);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, texB, 0, 0);
    ASSERT_GL_FRAMEBUFFER_COMPLETE(GL_FRAMEBUFFER);
    EXPECT_GL_NO_ERROR();
}

// Test that clearing the stencil buffer when the framebuffer only has a color attachment does not
// crash.
TEST_P(FramebufferTest_ES3, ClearNonexistentStencil)
{
    GLRenderbuffer rbo;
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 1, 1);

    GLFramebuffer fbo;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferRenderbuffer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbo);

    GLint clearValue = 0;
    glClearBufferiv(GL_STENCIL, 0, &clearValue);

    // There's no error specified for clearing nonexistent buffers, it's simply a no-op.
    EXPECT_GL_NO_ERROR();
}

// Test that clearing the depth buffer when the framebuffer only has a color attachment does not
// crash.
TEST_P(FramebufferTest_ES3, ClearNonexistentDepth)
{
    GLRenderbuffer rbo;
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 1, 1);

    GLFramebuffer fbo;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferRenderbuffer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbo);

    GLfloat clearValue = 0.0f;
    glClearBufferfv(GL_DEPTH, 0, &clearValue);

    // There's no error specified for clearing nonexistent buffers, it's simply a no-op.
    EXPECT_GL_NO_ERROR();
}

// Test that clearing a nonexistent color attachment does not crash.
TEST_P(FramebufferTest_ES3, ClearNonexistentColor)
{
    GLRenderbuffer rbo;
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 1, 1);

    GLFramebuffer fbo;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferRenderbuffer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbo);

    std::vector<GLfloat> clearValue = {{0.0f, 1.0f, 0.0f, 1.0f}};
    glClearBufferfv(GL_COLOR, 1, clearValue.data());

    // There's no error specified for clearing nonexistent buffers, it's simply a no-op.
    EXPECT_GL_NO_ERROR();
}

// Test that clearing the depth and stencil buffers when the framebuffer only has a color attachment
// does not crash.
TEST_P(FramebufferTest_ES3, ClearNonexistentDepthStencil)
{
    GLRenderbuffer rbo;
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 1, 1);

    GLFramebuffer fbo;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferRenderbuffer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbo);

    glClearBufferfi(GL_DEPTH_STENCIL, 0, 0.0f, 0);

    // There's no error specified for clearing nonexistent buffers, it's simply a no-op.
    EXPECT_GL_NO_ERROR();
}

// Test that clearing a color attachment that has been deleted doesn't crash.
TEST_P(FramebufferTest_ES3, ClearDeletedAttachment)
{
    // An INVALID_FRAMEBUFFER_OPERATION error was seen in this test on Mac, not sure where it might
    // be originating from. http://anglebug.com/2834
    ANGLE_SKIP_TEST_IF(IsOSX() && IsOpenGL());

    GLFramebuffer fbo;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    // There used to be a bug where some draw buffer state used to remain set even after the
    // attachment was detached via deletion. That's why we create, attach and delete this RBO here.
    GLuint rbo = 0u;
    glGenRenderbuffers(1, &rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbo);
    glDeleteRenderbuffers(1, &rbo);

    // There needs to be at least one color attachment to prevent early out from the clear calls.
    GLRenderbuffer rbo2;
    glBindRenderbuffer(GL_RENDERBUFFER, rbo2);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 1, 1);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_RENDERBUFFER, rbo2);

    ASSERT_GL_NO_ERROR();

    // There's no error specified for clearing nonexistent buffers, it's simply a no-op, so we
    // expect no GL errors below.
    std::array<GLfloat, 4> floatClearValue = {0.0f, 0.0f, 0.0f, 0.0f};
    glClearBufferfv(GL_COLOR, 0, floatClearValue.data());
    EXPECT_GL_NO_ERROR();
    std::array<GLuint, 4> uintClearValue = {0u, 0u, 0u, 0u};
    glClearBufferuiv(GL_COLOR, 0, uintClearValue.data());
    EXPECT_GL_NO_ERROR();
    std::array<GLint, 4> intClearValue = {0, 0, 0, 0};
    glClearBufferiv(GL_COLOR, 0, intClearValue.data());
    EXPECT_GL_NO_ERROR();
}

// Test that resizing the color attachment is handled correctly.
TEST_P(FramebufferTest_ES3, ResizeColorAttachmentSmallToLarge)
{
    GLFramebuffer fbo;
    GLTexture smallTexture;
    GLTexture largeTexture;

    ANGLE_GL_PROGRAM(greenProgram, essl1_shaders::vs::Simple(), essl1_shaders::fs::Green());
    ANGLE_GL_PROGRAM(blueProgram, essl1_shaders::vs::Simple(), essl1_shaders::fs::Blue());

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    // Bind the small texture
    glBindTexture(GL_TEXTURE_2D, smallTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, getWindowWidth() / 2, getWindowHeight() / 2, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, smallTexture, 0);
    ASSERT_GL_FRAMEBUFFER_COMPLETE(GL_FRAMEBUFFER);

    // Draw to FBO backed by the small texture
    glUseProgram(greenProgram);
    drawQuad(greenProgram.get(), std::string(essl1_shaders::PositionAttrib()), 0.0f);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);
    EXPECT_PIXEL_COLOR_EQ((getWindowWidth() / 2) - 1, (getWindowHeight() / 2) - 1, GLColor::green);

    // Change the attachment to the larger texture that fills the window
    glBindTexture(GL_TEXTURE_2D, largeTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, getWindowWidth(), getWindowHeight(), 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, largeTexture, 0);
    ASSERT_GL_FRAMEBUFFER_COMPLETE(GL_FRAMEBUFFER);

    // Draw to FBO backed by the large texture
    glUseProgram(blueProgram);
    drawQuad(blueProgram.get(), std::string(essl1_shaders::PositionAttrib()), 0.0f);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::blue);
    EXPECT_PIXEL_COLOR_EQ(getWindowWidth() - 1, getWindowHeight() - 1, GLColor::blue);
}

// Test that resizing the color attachment is handled correctly.
TEST_P(FramebufferTest_ES3, ResizeColorAttachmentLargeToSmall)
{
    GLFramebuffer fbo;
    GLTexture smallTexture;
    GLTexture largeTexture;

    ANGLE_GL_PROGRAM(greenProgram, essl1_shaders::vs::Simple(), essl1_shaders::fs::Green());
    ANGLE_GL_PROGRAM(blueProgram, essl1_shaders::vs::Simple(), essl1_shaders::fs::Blue());

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    // Bind the large texture
    glBindTexture(GL_TEXTURE_2D, largeTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, getWindowWidth(), getWindowHeight(), 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, largeTexture, 0);
    ASSERT_GL_FRAMEBUFFER_COMPLETE(GL_FRAMEBUFFER);

    // Draw to FBO backed by the large texture
    glUseProgram(blueProgram);
    drawQuad(blueProgram.get(), std::string(essl1_shaders::PositionAttrib()), 0.0f);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::blue);
    EXPECT_PIXEL_COLOR_EQ(getWindowWidth() - 1, getWindowHeight() - 1, GLColor::blue);

    // Change the attachment to the smaller texture
    glBindTexture(GL_TEXTURE_2D, smallTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, getWindowWidth() / 2, getWindowHeight() / 2, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, smallTexture, 0);
    ASSERT_GL_FRAMEBUFFER_COMPLETE(GL_FRAMEBUFFER);

    // Draw to FBO backed by the small texture
    glUseProgram(greenProgram);
    drawQuad(greenProgram.get(), std::string(essl1_shaders::PositionAttrib()), 0.0f);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);
    EXPECT_PIXEL_COLOR_EQ((getWindowWidth() / 2) - 1, (getWindowHeight() / 2) - 1, GLColor::green);
}

// Test that resizing the texture is handled correctly.
TEST_P(FramebufferTest_ES3, ResizeTextureLargeToSmall)
{
    GLFramebuffer fbo;
    GLTexture texture;

    ANGLE_GL_PROGRAM(greenProgram, essl1_shaders::vs::Simple(), essl1_shaders::fs::Green());
    ANGLE_GL_PROGRAM(blueProgram, essl1_shaders::vs::Simple(), essl1_shaders::fs::Blue());

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    // Allocate a large texture
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, getWindowWidth(), getWindowHeight(), 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    ASSERT_GL_FRAMEBUFFER_COMPLETE(GL_FRAMEBUFFER);

    // Draw to FBO backed by the large texture
    glUseProgram(blueProgram);
    drawQuad(blueProgram.get(), std::string(essl1_shaders::PositionAttrib()), 0.0f);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::blue);
    EXPECT_PIXEL_COLOR_EQ(getWindowWidth() - 1, getWindowHeight() - 1, GLColor::blue);

    // Shrink the texture
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, getWindowWidth() / 2, getWindowHeight() / 2, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    ASSERT_GL_FRAMEBUFFER_COMPLETE(GL_FRAMEBUFFER);

    // Draw to FBO backed by the small texture
    glUseProgram(greenProgram);
    drawQuad(greenProgram.get(), std::string(essl1_shaders::PositionAttrib()), 0.0f);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);
    EXPECT_PIXEL_COLOR_EQ((getWindowWidth() / 2) - 1, (getWindowHeight() / 2) - 1, GLColor::green);
}

// Test that resizing the texture is handled correctly.
TEST_P(FramebufferTest_ES3, ResizeTextureSmallToLarge)
{
    GLFramebuffer fbo;
    GLTexture texture;

    ANGLE_GL_PROGRAM(greenProgram, essl1_shaders::vs::Simple(), essl1_shaders::fs::Green());
    ANGLE_GL_PROGRAM(blueProgram, essl1_shaders::vs::Simple(), essl1_shaders::fs::Blue());

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    // Allocate a small texture
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, getWindowWidth() / 2, getWindowHeight() / 2, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    ASSERT_GL_FRAMEBUFFER_COMPLETE(GL_FRAMEBUFFER);

    // Draw to FBO backed by the large texture
    glUseProgram(blueProgram);
    drawQuad(blueProgram.get(), std::string(essl1_shaders::PositionAttrib()), 0.0f);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::blue);
    EXPECT_PIXEL_COLOR_EQ((getWindowWidth() / 2) - 1, (getWindowHeight() / 2) - 1, GLColor::blue);

    // Grow the texture
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, getWindowWidth(), getWindowHeight(), 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    ASSERT_GL_FRAMEBUFFER_COMPLETE(GL_FRAMEBUFFER);

    // Draw to FBO backed by the small texture
    glUseProgram(greenProgram);
    drawQuad(greenProgram.get(), std::string(essl1_shaders::PositionAttrib()), 0.0f);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);
    EXPECT_PIXEL_COLOR_EQ(getWindowWidth() - 1, getWindowHeight() - 1, GLColor::green);
}

// Test that fewer outputs than framebuffer attachments doesn't crash.  This causes a Vulkan
// validation warning, but should not be fatal.
TEST_P(FramebufferTest_ES3, FewerShaderOutputsThanAttachments)
{
    constexpr char kFS[] = R"(#version 300 es
precision highp float;

layout(location = 0) out vec4 color0;
layout(location = 1) out vec4 color1;
layout(location = 2) out vec4 color2;

void main()
{
    color0 = vec4(1.0, 0.0, 0.0, 1.0);
    color1 = vec4(0.0, 1.0, 0.0, 1.0);
    color2 = vec4(0.0, 0.0, 1.0, 1.0);
}
)";

    ANGLE_GL_PROGRAM(program, essl3_shaders::vs::Simple(), kFS);

    constexpr GLint kDrawBufferCount = 4;

    GLint maxDrawBuffers;
    glGetIntegerv(GL_MAX_DRAW_BUFFERS, &maxDrawBuffers);
    ASSERT_GE(maxDrawBuffers, kDrawBufferCount);

    GLTexture textures[kDrawBufferCount];

    for (GLint texIndex = 0; texIndex < kDrawBufferCount; ++texIndex)
    {
        glBindTexture(GL_TEXTURE_2D, textures[texIndex]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, getWindowWidth(), getWindowHeight(), 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, nullptr);
    }

    GLenum allBufs[kDrawBufferCount] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1,
                                        GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3};

    GLFramebuffer fbo;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);

    // Enable all draw buffers.
    for (GLint texIndex = 0; texIndex < kDrawBufferCount; ++texIndex)
    {
        glBindTexture(GL_TEXTURE_2D, textures[texIndex]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + texIndex, GL_TEXTURE_2D,
                               textures[texIndex], 0);
    }
    glDrawBuffers(kDrawBufferCount, allBufs);

    // Draw with simple program.
    drawQuad(program, essl3_shaders::PositionAttrib(), 0.5f, 1.0f, true);
    ASSERT_GL_NO_ERROR();
}

class FramebufferTest_ES31 : public ANGLETest
{
  protected:
    void validateSamplePass(GLuint &query, GLuint &passedCount, GLint width, GLint height)
    {
        glUniform2i(0, width - 1, height - 1);
        glBeginQuery(GL_ANY_SAMPLES_PASSED, query);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glEndQuery(GL_ANY_SAMPLES_PASSED);
        glGetQueryObjectuiv(query, GL_QUERY_RESULT, &passedCount);
        EXPECT_GT(static_cast<GLint>(passedCount), 0);

        glUniform2i(0, width - 1, height);
        glBeginQuery(GL_ANY_SAMPLES_PASSED, query);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glEndQuery(GL_ANY_SAMPLES_PASSED);
        glGetQueryObjectuiv(query, GL_QUERY_RESULT, &passedCount);
        EXPECT_EQ(static_cast<GLint>(passedCount), 0);

        glUniform2i(0, width, height - 1);
        glBeginQuery(GL_ANY_SAMPLES_PASSED, query);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glEndQuery(GL_ANY_SAMPLES_PASSED);
        glGetQueryObjectuiv(query, GL_QUERY_RESULT, &passedCount);
        EXPECT_EQ(static_cast<GLint>(passedCount), 0);
    }
};

// Test that without attachment, if either the value of FRAMEBUFFER_DEFAULT_WIDTH or
// FRAMEBUFFER_DEFAULT_HEIGHT parameters is zero, the framebuffer is incomplete.
TEST_P(FramebufferTest_ES31, IncompleteMissingAttachmentDefaultParam)
{
    GLFramebuffer mFramebuffer;
    glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer.get());

    glFramebufferParameteri(GL_FRAMEBUFFER, GL_FRAMEBUFFER_DEFAULT_WIDTH, 1);
    glFramebufferParameteri(GL_FRAMEBUFFER, GL_FRAMEBUFFER_DEFAULT_HEIGHT, 1);
    EXPECT_GLENUM_EQ(GL_FRAMEBUFFER_COMPLETE, glCheckFramebufferStatus(GL_FRAMEBUFFER));

    glFramebufferParameteri(GL_FRAMEBUFFER, GL_FRAMEBUFFER_DEFAULT_WIDTH, 0);
    glFramebufferParameteri(GL_FRAMEBUFFER, GL_FRAMEBUFFER_DEFAULT_HEIGHT, 0);
    EXPECT_GLENUM_EQ(GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT,
                     glCheckFramebufferStatus(GL_FRAMEBUFFER));

    glFramebufferParameteri(GL_FRAMEBUFFER, GL_FRAMEBUFFER_DEFAULT_WIDTH, 1);
    glFramebufferParameteri(GL_FRAMEBUFFER, GL_FRAMEBUFFER_DEFAULT_HEIGHT, 0);
    EXPECT_GLENUM_EQ(GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT,
                     glCheckFramebufferStatus(GL_FRAMEBUFFER));

    glFramebufferParameteri(GL_FRAMEBUFFER, GL_FRAMEBUFFER_DEFAULT_WIDTH, 0);
    glFramebufferParameteri(GL_FRAMEBUFFER, GL_FRAMEBUFFER_DEFAULT_HEIGHT, 1);
    EXPECT_GLENUM_EQ(GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT,
                     glCheckFramebufferStatus(GL_FRAMEBUFFER));

    ASSERT_GL_NO_ERROR();
}

// Test that the sample count of a mix of texture and renderbuffer should be same.
TEST_P(FramebufferTest_ES31, IncompleteMultisampleSampleCountMix)
{
    GLFramebuffer mFramebuffer;
    glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer.get());

    // Lookup the supported number of sample counts (rely on fact that ANGLE uses the same set of
    // sample counts for textures and renderbuffers)
    GLint numSampleCounts = 0;
    std::vector<GLint> sampleCounts;
    GLsizei queryBufferSize = 1;
    glGetInternalformativ(GL_TEXTURE_2D_MULTISAMPLE, GL_RGBA8, GL_NUM_SAMPLE_COUNTS,
                          queryBufferSize, &numSampleCounts);
    ANGLE_SKIP_TEST_IF((numSampleCounts < 2));
    sampleCounts.resize(numSampleCounts);
    queryBufferSize = numSampleCounts;
    glGetInternalformativ(GL_TEXTURE_2D_MULTISAMPLE, GL_RGBA8, GL_SAMPLES, queryBufferSize,
                          sampleCounts.data());

    GLTexture mTexture;
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, mTexture.get());
    glTexStorage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, sampleCounts[0], GL_RGBA8, 1, 1, true);

    GLRenderbuffer mRenderbuffer;
    glBindRenderbuffer(GL_RENDERBUFFER, mRenderbuffer.get());
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, sampleCounts[1], GL_RGBA8, 1, 1);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE,
                           mTexture.get(), 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_RENDERBUFFER,
                              mRenderbuffer.get());
    EXPECT_GLENUM_EQ(GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE,
                     glCheckFramebufferStatus(GL_FRAMEBUFFER));

    ASSERT_GL_NO_ERROR();
}

// Test that the sample count of texture attachments should be same.
TEST_P(FramebufferTest_ES31, IncompleteMultisampleSampleCountTex)
{
    GLFramebuffer mFramebuffer;
    glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer.get());

    // Lookup the supported number of sample counts
    GLint numSampleCounts = 0;
    std::vector<GLint> sampleCounts;
    GLsizei queryBufferSize = 1;
    glGetInternalformativ(GL_TEXTURE_2D_MULTISAMPLE, GL_RGBA8, GL_NUM_SAMPLE_COUNTS,
                          queryBufferSize, &numSampleCounts);
    ANGLE_SKIP_TEST_IF((numSampleCounts < 2));
    sampleCounts.resize(numSampleCounts);
    queryBufferSize = numSampleCounts;
    glGetInternalformativ(GL_TEXTURE_2D_MULTISAMPLE, GL_RGBA8, GL_SAMPLES, queryBufferSize,
                          sampleCounts.data());

    GLTexture mTextures[2];
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, mTextures[0].get());
    glTexStorage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, sampleCounts[0], GL_RGBA8, 1, 1, true);
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, mTextures[1].get());
    glTexStorage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, sampleCounts[1], GL_RGBA8, 1, 1, true);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE,
                           mTextures[0].get(), 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D_MULTISAMPLE,
                           mTextures[1].get(), 0);
    EXPECT_GLENUM_EQ(GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE,
                     glCheckFramebufferStatus(GL_FRAMEBUFFER));

    ASSERT_GL_NO_ERROR();
}

// Test that if the attached images are a mix of renderbuffers and textures, the value of
// TEXTURE_FIXED_SAMPLE_LOCATIONS must be TRUE for all attached textures.
TEST_P(FramebufferTest_ES31, IncompleteMultisampleFixedSampleLocationsMix)
{
    GLFramebuffer mFramebuffer;
    glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer.get());

    GLTexture mTexture;
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, mTexture.get());
    glTexStorage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, 1, GL_RGBA8, 1, 1, false);

    GLRenderbuffer mRenderbuffer;
    glBindRenderbuffer(GL_RENDERBUFFER, mRenderbuffer.get());
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, 1, GL_RGBA8, 1, 1);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE,
                           mTexture.get(), 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_RENDERBUFFER,
                              mRenderbuffer.get());
    EXPECT_GLENUM_EQ(GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE,
                     glCheckFramebufferStatus(GL_FRAMEBUFFER));

    ASSERT_GL_NO_ERROR();
}

// Test that the value of TEXTURE_FIXED_SAMPLE_LOCATIONS is the same for all attached textures.
TEST_P(FramebufferTest_ES31, IncompleteMultisampleFixedSampleLocationsTex)
{
    GLFramebuffer mFramebuffer;
    glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer.get());

    GLTexture mTextures[2];
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, mTextures[0].get());
    glTexStorage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, 1, GL_RGBA8, 1, 1, false);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE,
                           mTextures[0].get(), 0);
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, mTextures[1].get());
    glTexStorage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, 1, GL_RGB8, 1, 1, true);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D_MULTISAMPLE,
                           mTextures[1].get(), 0);
    EXPECT_GLENUM_EQ(GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE,
                     glCheckFramebufferStatus(GL_FRAMEBUFFER));

    ASSERT_GL_NO_ERROR();
}

// Test resolving a multisampled texture with blit
TEST_P(FramebufferTest_ES31, MultisampleResolveWithBlit)
{
    constexpr int kSize = 16;
    glViewport(0, 0, kSize, kSize);

    GLFramebuffer msaaFBO;
    glBindFramebuffer(GL_FRAMEBUFFER, msaaFBO.get());

    GLTexture texture;
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, texture.get());
    glTexStorage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, 4, GL_RGBA8, kSize, kSize, false);
    ASSERT_GL_NO_ERROR();
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE,
                           texture.get(), 0);
    ASSERT_GL_FRAMEBUFFER_COMPLETE(GL_FRAMEBUFFER);

    ANGLE_GL_PROGRAM(gradientProgram, essl31_shaders::vs::Passthrough(),
                     essl31_shaders::fs::RedGreenGradient());
    drawQuad(gradientProgram, essl31_shaders::PositionAttrib(), 0.5f, 1.0f, true);
    ASSERT_GL_NO_ERROR();

    // Create another FBO to resolve the multisample buffer into.
    GLTexture resolveTexture;
    GLFramebuffer resolveFBO;
    glBindTexture(GL_TEXTURE_2D, resolveTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, resolveFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolveTexture, 0);
    ASSERT_GL_FRAMEBUFFER_COMPLETE(GL_FRAMEBUFFER);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, msaaFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFBO);
    glBlitFramebuffer(0, 0, kSize, kSize, 0, 0, kSize, kSize, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    ASSERT_GL_NO_ERROR();

    glBindFramebuffer(GL_READ_FRAMEBUFFER, resolveFBO);
    EXPECT_PIXEL_NEAR(0, 0, 0, 0, 0, 255, 1.0);                      // Black
    EXPECT_PIXEL_NEAR(kSize - 1, 1, 239, 0, 0, 255, 1.0);            // Red
    EXPECT_PIXEL_NEAR(0, kSize - 1, 0, 239, 0, 255, 1.0);            // Green
    EXPECT_PIXEL_NEAR(kSize - 1, kSize - 1, 239, 239, 0, 255, 1.0);  // Yellow
}

// Test resolving a multisampled texture with blit after drawing to mulitiple FBOs.
TEST_P(FramebufferTest_ES31, MultisampleResolveWithBlitMultipleFBOs)
{
    // FBO 1 -> multisample draw (red)
    // FBO 2 -> multisample draw (green)
    // Bind FBO 1 as read
    // Bind FBO 3 as draw
    // Resolve

    constexpr int kSize = 16;
    glViewport(0, 0, kSize, kSize);

    GLFramebuffer msaaFBORed;
    glBindFramebuffer(GL_FRAMEBUFFER, msaaFBORed.get());

    GLTexture textureRed;
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, textureRed.get());
    glTexStorage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, 4, GL_RGBA8, kSize, kSize, false);
    ASSERT_GL_NO_ERROR();
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE,
                           textureRed.get(), 0);
    ASSERT_GL_FRAMEBUFFER_COMPLETE(GL_FRAMEBUFFER);

    ANGLE_GL_PROGRAM(redProgram, essl31_shaders::vs::Simple(), essl31_shaders::fs::Red());
    drawQuad(redProgram, essl31_shaders::PositionAttrib(), 0.5f, 1.0f, true);
    ASSERT_GL_NO_ERROR();

    GLFramebuffer msaaFBOGreen;
    glBindFramebuffer(GL_FRAMEBUFFER, msaaFBOGreen.get());

    GLTexture textureGreen;
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, textureGreen.get());
    glTexStorage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, 4, GL_RGBA8, kSize, kSize, false);
    ASSERT_GL_NO_ERROR();
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE,
                           textureGreen.get(), 0);
    ASSERT_GL_FRAMEBUFFER_COMPLETE(GL_FRAMEBUFFER);

    ANGLE_GL_PROGRAM(greenProgram, essl31_shaders::vs::Simple(), essl31_shaders::fs::Green());
    drawQuad(greenProgram, essl31_shaders::PositionAttrib(), 0.5f, 1.0f, true);
    ASSERT_GL_NO_ERROR();

    // Create another FBO to resolve the multisample buffer into.
    GLTexture resolveTexture;
    GLFramebuffer resolveFBO;
    glBindTexture(GL_TEXTURE_2D, resolveTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, resolveFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolveTexture, 0);
    ASSERT_GL_FRAMEBUFFER_COMPLETE(GL_FRAMEBUFFER);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, msaaFBORed);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFBO);
    glBlitFramebuffer(0, 0, kSize, kSize, 0, 0, kSize, kSize, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    ASSERT_GL_NO_ERROR();

    glBindFramebuffer(GL_READ_FRAMEBUFFER, resolveFBO);
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::red);
}

// Test resolving a multisampled texture with blit after drawing to mulitiple FBOs.
TEST_P(FramebufferTest_ES31, MultisampleResolveWithBlitMultipleResolves)
{
    // Draw multisampled in FBO 1
    // Bind FBO 1 as read
    // Bind FBO 2 as draw
    // Resolve
    // Bind FBO 3 as draw
    // Resolve

    constexpr int kSize = 16;
    glViewport(0, 0, kSize, kSize);

    GLFramebuffer msaaFBORed;
    glBindFramebuffer(GL_FRAMEBUFFER, msaaFBORed.get());

    GLTexture textureRed;
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, textureRed.get());
    glTexStorage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, 4, GL_RGBA8, kSize, kSize, false);
    ASSERT_GL_NO_ERROR();
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE,
                           textureRed.get(), 0);
    ASSERT_GL_FRAMEBUFFER_COMPLETE(GL_FRAMEBUFFER);

    ANGLE_GL_PROGRAM(redProgram, essl31_shaders::vs::Simple(), essl31_shaders::fs::Red());
    drawQuad(redProgram, essl31_shaders::PositionAttrib(), 0.5f, 1.0f, true);
    ASSERT_GL_NO_ERROR();

    // Create another FBO to resolve the multisample buffer into.
    GLTexture resolveTexture1;
    GLFramebuffer resolveFBO1;
    glBindTexture(GL_TEXTURE_2D, resolveTexture1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, resolveFBO1);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolveTexture1, 0);
    ASSERT_GL_FRAMEBUFFER_COMPLETE(GL_FRAMEBUFFER);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, msaaFBORed);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFBO1);
    glBlitFramebuffer(0, 0, kSize, kSize, 0, 0, kSize, kSize, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    ASSERT_GL_NO_ERROR();

    glBindFramebuffer(GL_READ_FRAMEBUFFER, resolveFBO1);
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::red);

    // Create another FBO to resolve the multisample buffer into.
    GLTexture resolveTexture2;
    GLFramebuffer resolveFBO2;
    glBindTexture(GL_TEXTURE_2D, resolveTexture2);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, resolveFBO2);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolveTexture2, 0);
    ASSERT_GL_FRAMEBUFFER_COMPLETE(GL_FRAMEBUFFER);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, msaaFBORed);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFBO2);
    glBlitFramebuffer(0, 0, kSize, kSize, 0, 0, kSize, kSize, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    ASSERT_GL_NO_ERROR();

    glBindFramebuffer(GL_READ_FRAMEBUFFER, resolveFBO2);
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::red);
}

// Test resolving a multisampled texture into a mipmaped texture with blit
TEST_P(FramebufferTest_ES31, MultisampleResolveIntoMipMapWithBlit)
{
    // FBO 1 is attached to a 64x64 texture
    // FBO 2 attached to level 1 of a 128x128 texture

    constexpr int kSize = 64;
    glViewport(0, 0, kSize, kSize);

    // Create the textures early and call glGenerateMipmap() so it doesn't break the render pass
    // between the drawQuad() and glBlitFramebuffer(), so we can test the resolve with subpass path
    // in the Vulkan back end.
    GLTexture texture;
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, texture.get());
    glTexStorage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, 4, GL_RGBA8, kSize, kSize, false);
    ASSERT_GL_NO_ERROR();

    GLTexture resolveTexture;
    glBindTexture(GL_TEXTURE_2D, resolveTexture);
    glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 1);
    ASSERT_GL_NO_ERROR();
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GLFramebuffer msaaFBO;
    glBindFramebuffer(GL_FRAMEBUFFER, msaaFBO.get());
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE,
                           texture.get(), 0);
    ASSERT_GL_FRAMEBUFFER_COMPLETE(GL_FRAMEBUFFER);

    ANGLE_GL_PROGRAM(gradientProgram, essl31_shaders::vs::Passthrough(),
                     essl31_shaders::fs::RedGreenGradient());
    drawQuad(gradientProgram, essl31_shaders::PositionAttrib(), 0.5f, 1.0f, true);
    ASSERT_GL_NO_ERROR();

    // Create another FBO to resolve the multisample buffer into.
    GLFramebuffer resolveFBO;
    glBindFramebuffer(GL_FRAMEBUFFER, resolveFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolveTexture, 1);
    ASSERT_GL_FRAMEBUFFER_COMPLETE(GL_FRAMEBUFFER);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, msaaFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFBO);
    glBlitFramebuffer(0, 0, kSize, kSize, 0, 0, kSize, kSize, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    ASSERT_GL_NO_ERROR();

    glBindFramebuffer(GL_READ_FRAMEBUFFER, resolveFBO);
    EXPECT_PIXEL_NEAR(0, 0, 0, 0, 0, 255, 1.0);                      // Black
    EXPECT_PIXEL_NEAR(kSize - 1, 1, 251, 0, 0, 255, 1.0);            // Red
    EXPECT_PIXEL_NEAR(0, kSize - 1, 0, 251, 0, 255, 1.0);            // Green
    EXPECT_PIXEL_NEAR(kSize - 1, kSize - 1, 251, 251, 0, 255, 1.0);  // Yellow
}

// Test resolving a multisampled texture with blit after drawing to mulitiple FBOs.
TEST_P(FramebufferTest_ES31, MultipleTextureMultisampleResolveWithBlitMultipleResolves)
{
    // Attach two MSAA textures to FBO1
    // Set read buffer 0
    // Resolve into FBO2
    // Set read buffer 1
    // Resolve into FBO3

    ANGLE_SKIP_TEST_IF(!EnsureGLExtensionEnabled("GL_EXT_draw_buffers"));

    constexpr int kSize = 16;
    glViewport(0, 0, kSize, kSize);

    GLFramebuffer msaaFBO;
    glBindFramebuffer(GL_FRAMEBUFFER, msaaFBO.get());

    GLTexture msaaTextureRed;
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, msaaTextureRed.get());
    glTexStorage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, 4, GL_RGBA8, kSize, kSize, false);
    ASSERT_GL_NO_ERROR();
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE,
                           msaaTextureRed.get(), 0);
    ASSERT_GL_FRAMEBUFFER_COMPLETE(GL_FRAMEBUFFER);

    GLTexture msaaTextureGreen;
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, msaaTextureGreen.get());
    glTexStorage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, 4, GL_RGBA8, kSize, kSize, false);
    ASSERT_GL_NO_ERROR();
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D_MULTISAMPLE,
                           msaaTextureGreen.get(), 0);
    ASSERT_GL_FRAMEBUFFER_COMPLETE(GL_FRAMEBUFFER);

    // Setup program to render red into attachment 0 and green into attachment 1.
    const char *fs = R"(#extension GL_EXT_draw_buffers : enable
precision highp float;
void main()
{
    gl_FragData[0] = vec4(1.0, 0.0, 0.0, 1.0);  // attachment 0: red
    gl_FragData[1] = vec4(0.0, 1.0, 0.0, 1.0);  // attachment 1: green
})";
    ANGLE_GL_PROGRAM(program, essl1_shaders::vs::Simple(), fs);
    glUseProgram(program);
    constexpr GLenum kDrawBuffers[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, kDrawBuffers);

    drawQuad(program, essl1_shaders::PositionAttrib(), 0.5f);
    ASSERT_GL_NO_ERROR();

    // Create another FBO to resolve the multisample buffer into.
    GLTexture resolveTexture1;
    GLFramebuffer resolveFBO1;
    glBindTexture(GL_TEXTURE_2D, resolveTexture1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, resolveFBO1);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolveTexture1, 0);
    ASSERT_GL_FRAMEBUFFER_COMPLETE(GL_FRAMEBUFFER);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, msaaFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFBO1);
    glReadBuffer(GL_COLOR_ATTACHMENT0);  // Red
    glBlitFramebuffer(0, 0, kSize, kSize, 0, 0, kSize, kSize, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    ASSERT_GL_NO_ERROR();

    glBindFramebuffer(GL_READ_FRAMEBUFFER, resolveFBO1);
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::red);

    // Create another FBO to resolve the multisample buffer into.
    GLTexture resolveTexture2;
    GLFramebuffer resolveFBO2;
    glBindTexture(GL_TEXTURE_2D, resolveTexture2);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, resolveFBO2);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolveTexture2, 0);
    ASSERT_GL_FRAMEBUFFER_COMPLETE(GL_FRAMEBUFFER);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, msaaFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFBO2);
    glReadBuffer(GL_COLOR_ATTACHMENT1);  // Green
    glBlitFramebuffer(0, 0, kSize, kSize, 0, 0, kSize, kSize, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    ASSERT_GL_NO_ERROR();

    glBindFramebuffer(GL_READ_FRAMEBUFFER, resolveFBO2);
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);
}

// If there are no attachments, rendering will be limited to a rectangle having a lower left of
// (0, 0) and an upper right of(width, height), where width and height are the framebuffer
// object's default width and height.
TEST_P(FramebufferTest_ES31, RenderingLimitToDefaultFBOSizeWithNoAttachments)
{
    // anglebug.com/2253
    ANGLE_SKIP_TEST_IF(IsLinux() && IsAMD() && IsDesktopOpenGL());
    // Occlusion query reports fragments outside the render area are still rendered
    ANGLE_SKIP_TEST_IF(IsAndroid() || (IsWindows() && (IsIntel() || IsAMD())));

    constexpr char kVS1[] = R"(#version 310 es
in layout(location = 0) highp vec2 a_position;
void main()
{
    gl_Position = vec4(a_position, 0.0, 1.0);
})";

    constexpr char kFS1[] = R"(#version 310 es
uniform layout(location = 0) highp ivec2 u_expectedSize;
out layout(location = 3) mediump vec4 f_color;
void main()
{
    if (ivec2(gl_FragCoord.xy) != u_expectedSize) discard;
    f_color = vec4(1.0, 0.5, 0.25, 1.0);
})";

    constexpr char kVS2[] = R"(#version 310 es
in layout(location = 0) highp vec2 a_position;
void main()
{
    gl_Position = vec4(a_position, 0.0, 1.0);
})";

    constexpr char kFS2[] = R"(#version 310 es
uniform layout(location = 0) highp ivec2 u_expectedSize;
out layout(location = 2) mediump vec4 f_color;
void main()
{
    if (ivec2(gl_FragCoord.xy) != u_expectedSize) discard;
    f_color = vec4(1.0, 0.5, 0.25, 1.0);
})";

    ANGLE_GL_PROGRAM(program1, kVS1, kFS1);
    ANGLE_GL_PROGRAM(program2, kVS2, kFS2);

    glUseProgram(program1);

    GLFramebuffer mFramebuffer;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mFramebuffer);
    GLuint defaultWidth  = 1;
    GLuint defaultHeight = 1;

    glFramebufferParameteri(GL_FRAMEBUFFER, GL_FRAMEBUFFER_DEFAULT_WIDTH, defaultWidth);
    glFramebufferParameteri(GL_FRAMEBUFFER, GL_FRAMEBUFFER_DEFAULT_HEIGHT, defaultHeight);
    EXPECT_GLENUM_EQ(GL_FRAMEBUFFER_COMPLETE, glCheckFramebufferStatus(GL_FRAMEBUFFER));

    const float data[] = {
        1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, -1.0f,
    };

    GLuint vertexArray  = 0;
    GLuint vertexBuffer = 0;
    GLuint query        = 0;
    GLuint passedCount  = 0;

    glGenQueries(1, &query);
    glGenVertexArrays(1, &vertexArray);
    glBindVertexArray(vertexArray);

    glGenBuffers(1, &vertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(data), data, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, false, 0, 0);
    EXPECT_GLENUM_EQ(GL_FRAMEBUFFER_COMPLETE, glCheckFramebufferStatus(GL_FRAMEBUFFER));

    validateSamplePass(query, passedCount, defaultWidth, defaultHeight);

    glUseProgram(program2);
    validateSamplePass(query, passedCount, defaultWidth, defaultHeight);

    glUseProgram(program1);
    // If fbo has attachments, the rendering size should be the same as its attachment.
    GLTexture mTexture;
    GLuint width  = 2;
    GLuint height = 2;
    glBindTexture(GL_TEXTURE_2D, mTexture.get());
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, width, height);

    const GLenum bufs[] = {GL_NONE, GL_NONE, GL_NONE, GL_COLOR_ATTACHMENT3};

    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, mTexture.get(),
                           0);
    EXPECT_GLENUM_EQ(GL_FRAMEBUFFER_COMPLETE, glCheckFramebufferStatus(GL_FRAMEBUFFER));
    glDrawBuffers(4, bufs);

    validateSamplePass(query, passedCount, width, height);

    // If fbo's attachment has been removed, the rendering size should be the same as framebuffer
    // default size.
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, 0, 0, 0);
    EXPECT_GLENUM_EQ(GL_FRAMEBUFFER_COMPLETE, glCheckFramebufferStatus(GL_FRAMEBUFFER));

    validateSamplePass(query, passedCount, defaultWidth, defaultHeight);

    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glDeleteBuffers(1, &vertexBuffer);
    glDeleteVertexArrays(1, &vertexArray);

    ASSERT_GL_NO_ERROR();
}

class AddDummyTextureNoRenderTargetTest : public ANGLETest
{
  public:
    AddDummyTextureNoRenderTargetTest()
    {
        setWindowWidth(512);
        setWindowHeight(512);
        setConfigRedBits(8);
        setConfigGreenBits(8);
        setConfigBlueBits(8);
        setConfigAlphaBits(8);
    }

    void overrideWorkaroundsD3D(FeaturesD3D *features) override
    {
        features->overrideFeatures({"add_dummy_texture_no_render_target"}, true);
    }
};

// Test to verify workaround succeeds when no program outputs exist http://anglebug.com/2283
TEST_P(AddDummyTextureNoRenderTargetTest, NoProgramOutputWorkaround)
{
    constexpr char kVS[] = "void main() {}";
    constexpr char kFS[] = "void main() {}";

    ANGLE_GL_PROGRAM(drawProgram, kVS, kFS);

    glUseProgram(drawProgram);

    glDrawArrays(GL_TRIANGLES, 0, 6);

    ASSERT_GL_NO_ERROR();
}

// Covers a bug in ANGLE's Vulkan back-end framebuffer cache which ignored depth/stencil after
// calls to DrawBuffers.
TEST_P(FramebufferTest_ES3, AttachmentStateChange)
{
    constexpr GLuint kSize = 2;

    ANGLE_GL_PROGRAM(program, essl1_shaders::vs::Simple(), essl1_shaders::fs::Green());

    GLTexture colorTexture;
    glBindTexture(GL_TEXTURE_2D, colorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    GLFramebuffer fbo;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0);

    ASSERT_GL_NO_ERROR();
    ASSERT_GL_FRAMEBUFFER_COMPLETE(GL_FRAMEBUFFER);

    // First draw without a depth buffer.
    drawQuad(program, essl1_shaders::PositionAttrib(), 0.5f);

    GLRenderbuffer depthBuffer;
    glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, kSize, kSize);

    // Bind just a renderbuffer and draw.
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthBuffer);

    ASSERT_GL_NO_ERROR();
    ASSERT_GL_FRAMEBUFFER_COMPLETE(GL_FRAMEBUFFER);

    glDrawBuffers(0, nullptr);
    drawQuad(program, essl1_shaders::PositionAttrib(), 0.5f);

    // Re-enable color buffer and draw one final time. This previously triggered a crash.
    GLenum drawBuffs = {GL_COLOR_ATTACHMENT0};
    glDrawBuffers(1, &drawBuffs);

    drawQuad(program, essl1_shaders::PositionAttrib(), 0.5f);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);
}

// Tests that we can support a feedback loop between a depth textures and the depth buffer.
// Does not totally mirror the case used in Manhattan. The Manhattan case seems to handle
// "clear" specially instead of rendering to depth in the same RP.
TEST_P(FramebufferTest_ES3, DepthFeedbackLoopSupported)
{
    // Feedback loops not supported on D3D11 and may not ever be.
    ANGLE_SKIP_TEST_IF(IsD3D11());

    // Also this particular test doesn't work on Android despite similar support in Manhattan.
    ANGLE_SKIP_TEST_IF(IsAndroid() && IsOpenGLES());

    constexpr GLuint kSize = 2;
    glViewport(0, 0, kSize, kSize);

    constexpr char kFS[] = R"(precision mediump float;
varying vec2 v_texCoord;
uniform sampler2D depth;
void main()
{
    if (abs(texture2D(depth, v_texCoord).x - 0.5) < 0.1)
    {
        gl_FragColor = vec4(0, 1, 0, 1);
    }
    else
    {
        gl_FragColor = vec4(1, 0, 0, 1);
    }
})";

    ANGLE_GL_PROGRAM(program, essl1_shaders::vs::Texture2D(), kFS);

    GLFramebuffer framebuffer;
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

    GLTexture colorTexture;
    glBindTexture(GL_TEXTURE_2D, colorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0);

    GLTexture depthTexture;
    glBindTexture(GL_TEXTURE_2D, depthTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, kSize, kSize, 0, GL_DEPTH_COMPONENT,
                 GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTexture, 0);

    ASSERT_GL_NO_ERROR();
    ASSERT_GL_FRAMEBUFFER_COMPLETE(GL_FRAMEBUFFER);

    // Clear depth to 0.5.
    glClearDepthf(0.5f);
    glClear(GL_DEPTH_BUFFER_BIT);

    // Disable the depth mask. Although this does not remove the feedback loop as defined by the
    // spec it mimics what gfxbench does in its rendering tests.
    glDepthMask(false);

    // Verify we can sample the depth texture and get 0.5.
    drawQuad(program, essl1_shaders::PositionAttrib(), 0.5);

    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);
}

// Covers a bug in ANGLE's Vulkan back-end. Our VkFramebuffer cache would in some cases forget to
// check the draw states when computing a cache key.
TEST_P(FramebufferTest_ES3, DisabledAttachmentRedefinition)
{
    constexpr GLuint kSize = 2;

    // Make a Framebuffer with two attachments with one enabled and one disabled.
    GLTexture texA, texB;
    glBindTexture(GL_TEXTURE_2D, texA);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, texB);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    GLFramebuffer fbo;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texA, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, texB, 0);

    // Mask out the second texture.
    constexpr GLenum kOneDrawBuf = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &kOneDrawBuf);

    ASSERT_GL_NO_ERROR();
    ASSERT_GL_FRAMEBUFFER_COMPLETE(GL_FRAMEBUFFER);

    // Set up a very simple shader.
    ANGLE_GL_PROGRAM(program, essl1_shaders::vs::Simple(), essl1_shaders::fs::Green());
    glViewport(0, 0, kSize, kSize);

    // Draw
    drawQuad(program, essl1_shaders::PositionAttrib(), 0.5f, 1.0f, true);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);

    // Update the masked out attachment and draw again.
    std::vector<GLColor> redPixels(kSize * kSize, GLColor::red);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kSize, kSize, GL_RGBA, GL_UNSIGNED_BYTE,
                    redPixels.data());

    // Draw
    drawQuad(program, essl1_shaders::PositionAttrib(), 0.5f, 1.0f, true);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);

    glReadBuffer(GL_COLOR_ATTACHMENT1);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::red);
}

class FramebufferTest : public ANGLETest
{};

template <typename T>
void FillTexture2D(GLuint texture,
                   GLsizei width,
                   GLsizei height,
                   const T &onePixelData,
                   GLint level,
                   GLint internalFormat,
                   GLenum format,
                   GLenum type)
{
    std::vector<T> allPixelsData(width * height, onePixelData);

    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, level, internalFormat, width, height, 0, format, type,
                 allPixelsData.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

// Multi-context uses of textures should not cause rendering feedback loops.
TEST_P(FramebufferTest, MultiContextNoRenderingFeedbackLoops)
{
    constexpr char kTextureVS[] =
        R"(attribute vec4 a_position;
varying vec2 v_texCoord;
void main() {
    gl_Position = a_position;
    v_texCoord = (a_position.xy * 0.5) + 0.5;
})";

    constexpr char kTextureFS[] =
        R"(precision mediump float;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
void main() {
    gl_FragColor = texture2D(u_texture, v_texCoord).rgba;
})";

    ANGLE_GL_PROGRAM(textureProgram, kTextureVS, kTextureFS);

    glUseProgram(textureProgram.get());
    GLint uniformLoc = glGetUniformLocation(textureProgram.get(), "u_texture");
    ASSERT_NE(-1, uniformLoc);
    glUniform1i(uniformLoc, 0);

    GLTexture texture;
    FillTexture2D(texture.get(), 1, 1, GLColor::red, 0, GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE);
    glBindTexture(GL_TEXTURE_2D, texture.get());
    // Note that _texture_ is still bound to GL_TEXTURE_2D in this context at this point.

    EGLWindow *window          = getEGLWindow();
    EGLDisplay display         = window->getDisplay();
    EGLConfig config           = window->getConfig();
    EGLSurface surface         = window->getSurface();
    EGLint contextAttributes[] = {
        EGL_CONTEXT_MAJOR_VERSION_KHR,
        GetParam().majorVersion,
        EGL_CONTEXT_MINOR_VERSION_KHR,
        GetParam().minorVersion,
        EGL_NONE,
    };
    EGLContext context1 = eglGetCurrentContext();
    // Create context2, sharing resources with context1.
    EGLContext context2 = eglCreateContext(display, config, context1, contextAttributes);
    ASSERT_NE(context2, EGL_NO_CONTEXT);
    eglMakeCurrent(display, surface, surface, context2);

    constexpr char kVS[] =
        R"(attribute vec4 a_position;
void main() {
    gl_Position = a_position;
})";

    constexpr char kFS[] =
        R"(precision mediump float;
void main() {
    gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);
})";

    ANGLE_GL_PROGRAM(program, kVS, kFS);
    glUseProgram(program.get());

    ASSERT_GL_NO_ERROR();

    // Render to the texture in context2.
    GLFramebuffer framebuffer;
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer.get());
    // Texture is still a valid name in context2.
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture.get(), 0);
    ASSERT_GL_FRAMEBUFFER_COMPLETE(GL_FRAMEBUFFER);
    // There is no rendering feedback loop at this point.

    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    ASSERT_GL_NO_ERROR();

    // If draw is no-op'ed, texture will not be filled appropriately.
    drawQuad(program.get(), "a_position", 0.5f, 1.0f, true);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);

    // Make context1 current again.
    eglMakeCurrent(display, surface, surface, context1);

    // Render texture to screen.
    drawQuad(textureProgram.get(), "a_position", 0.5f, 1.0f, true);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);

    eglDestroyContext(display, context2);
}

// Ensure cube-incomplete attachments cause incomplete Framebuffers.
TEST_P(FramebufferTest, IncompleteCubeMap)
{
    constexpr GLuint kSize = 2;

    GLTexture srcTex;
    glBindTexture(GL_TEXTURE_CUBE_MAP, srcTex);
    glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, GL_RGBA, kSize, kSize, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);

    GLFramebuffer fbo;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X,
                           srcTex, 0);

    ASSERT_GL_NO_ERROR();
    ASSERT_GLENUM_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER),
                     GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT);
}

// Test FBOs with different sizes are drawn correctly
TEST_P(FramebufferTest, BindAndDrawDifferentSizedFBOs)
{
    // 1. Create FBO 1 with dimensions 16x16
    // 2. Draw red into FBO 1 (note, FramebufferVk::syncState is called)
    // 3. Create FBO 2 with dimensions 8x8
    // 4. Draw green into FBO 2 (note, FramebufferVk::syncState is called)
    // 5. Bind FBO 1 (note, it's not dirty)
    // 6. Draw blue into FBO 1
    // 7. Verify FBO 1 is entirely blue

    GLFramebuffer smallFbo;
    GLFramebuffer largeFbo;
    GLTexture smallTexture;
    GLTexture largeTexture;
    constexpr GLsizei kLargeWidth  = 16;
    constexpr GLsizei kLargeHeight = 16;
    constexpr GLsizei kSmallWidth  = 8;
    constexpr GLsizei kSmallHeight = 8;

    ANGLE_GL_PROGRAM(redProgram, essl1_shaders::vs::Simple(), essl1_shaders::fs::Red());
    ANGLE_GL_PROGRAM(greenProgram, essl1_shaders::vs::Simple(), essl1_shaders::fs::Green());
    ANGLE_GL_PROGRAM(blueProgram, essl1_shaders::vs::Simple(), essl1_shaders::fs::Blue());

    // 1. Create FBO 1 with dimensions 16x16
    glBindFramebuffer(GL_FRAMEBUFFER, largeFbo);
    glBindTexture(GL_TEXTURE_2D, largeTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kLargeWidth, kLargeHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, largeTexture, 0);
    ASSERT_GL_FRAMEBUFFER_COMPLETE(GL_FRAMEBUFFER);

    // 2. Draw red into FBO 1 (note, FramebufferVk::syncState is called)
    glUseProgram(redProgram);
    drawQuad(redProgram.get(), std::string(essl1_shaders::PositionAttrib()), 0.0f);
    ASSERT_GL_NO_ERROR();

    // 3. Create FBO 2 with dimensions 8x8
    glBindFramebuffer(GL_FRAMEBUFFER, smallFbo);
    glBindTexture(GL_TEXTURE_2D, smallTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kSmallWidth, kSmallHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, smallTexture, 0);
    ASSERT_GL_FRAMEBUFFER_COMPLETE(GL_FRAMEBUFFER);

    // 4. Draw green into FBO 2 (note, FramebufferVk::syncState is called)
    glUseProgram(greenProgram);
    drawQuad(greenProgram.get(), std::string(essl1_shaders::PositionAttrib()), 0.0f);
    ASSERT_GL_NO_ERROR();

    // 5. Bind FBO 1 (note, it's not dirty)
    glBindFramebuffer(GL_FRAMEBUFFER, largeFbo);

    // 6. Draw blue into FBO 1
    glUseProgram(blueProgram);
    drawQuad(blueProgram.get(), std::string(essl1_shaders::PositionAttrib()), 0.0f);
    ASSERT_GL_NO_ERROR();

    // 7. Verify FBO 1 is entirely blue
    EXPECT_PIXEL_RECT_EQ(0, 0, kLargeWidth, kLargeHeight, GLColor::blue);
}

ANGLE_INSTANTIATE_TEST_ES2(AddDummyTextureNoRenderTargetTest);
ANGLE_INSTANTIATE_TEST_ES2(FramebufferTest);
ANGLE_INSTANTIATE_TEST_ES2_AND_ES3(FramebufferFormatsTest);
ANGLE_INSTANTIATE_TEST_ES3(FramebufferTest_ES3);
ANGLE_INSTANTIATE_TEST_ES31(FramebufferTest_ES31);
