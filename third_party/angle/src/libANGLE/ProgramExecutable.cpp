//
// Copyright 2020 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// ProgramExecutable.cpp: Collects the interfaces common to both Programs and
// ProgramPipelines in order to execute/draw with either.

#include "libANGLE/ProgramExecutable.h"

#include "libANGLE/Context.h"
#include "libANGLE/Program.h"
#include "libANGLE/ProgramPipeline.h"
#include "libANGLE/Shader.h"

namespace gl
{
namespace
{
bool IncludeSameArrayElement(const std::set<std::string> &nameSet, const std::string &name)
{
    std::vector<unsigned int> subscripts;
    std::string baseName = ParseResourceName(name, &subscripts);
    for (const std::string &nameInSet : nameSet)
    {
        std::vector<unsigned int> arrayIndices;
        std::string arrayName = ParseResourceName(nameInSet, &arrayIndices);
        if (baseName == arrayName &&
            (subscripts.empty() || arrayIndices.empty() || subscripts == arrayIndices))
        {
            return true;
        }
    }
    return false;
}

// Find the matching varying or field by name.
const sh::ShaderVariable *FindOutputVaryingOrField(const ProgramMergedVaryings &varyings,
                                                   ShaderType stage,
                                                   const std::string &name)
{
    const sh::ShaderVariable *var = nullptr;
    for (const ProgramVaryingRef &ref : varyings)
    {
        if (ref.frontShaderStage != stage)
        {
            continue;
        }

        const sh::ShaderVariable *varying = ref.get(stage);
        if (varying->name == name)
        {
            var = varying;
            break;
        }
        GLuint fieldIndex = 0;
        var               = varying->findField(name, &fieldIndex);
        if (var != nullptr)
        {
            break;
        }
    }
    return var;
}
}  // anonymous namespace

ProgramExecutable::ProgramExecutable()
    : mMaxActiveAttribLocation(0),
      mAttributesTypeMask(0),
      mAttributesMask(0),
      mActiveSamplersMask(0),
      mActiveSamplerRefCounts{},
      mActiveImagesMask(0),
      mCanDrawWith(false),
      mYUVOutput(false),
      mTransformFeedbackBufferMode(GL_INTERLEAVED_ATTRIBS),
      mDefaultUniformRange(0, 0),
      mSamplerUniformRange(0, 0),
      mImageUniformRange(0, 0),
      mPipelineHasGraphicsUniformBuffers(false),
      mPipelineHasComputeUniformBuffers(false),
      mPipelineHasGraphicsStorageBuffers(false),
      mPipelineHasComputeStorageBuffers(false),
      mPipelineHasGraphicsAtomicCounterBuffers(false),
      mPipelineHasComputeAtomicCounterBuffers(false),
      mPipelineHasGraphicsDefaultUniforms(false),
      mPipelineHasComputeDefaultUniforms(false),
      mPipelineHasGraphicsTextures(false),
      mPipelineHasComputeTextures(false),
      mPipelineHasGraphicsImages(false),
      mPipelineHasComputeImages(false),
      mIsCompute(false),
      // [GL_EXT_geometry_shader] Table 20.22
      mGeometryShaderInputPrimitiveType(PrimitiveMode::Triangles),
      mGeometryShaderOutputPrimitiveType(PrimitiveMode::TriangleStrip),
      mGeometryShaderInvocations(1),
      mGeometryShaderMaxVertices(0)
{
    reset();
}

ProgramExecutable::ProgramExecutable(const ProgramExecutable &other)
    : mLinkedGraphicsShaderStages(other.mLinkedGraphicsShaderStages),
      mLinkedComputeShaderStages(other.mLinkedComputeShaderStages),
      mActiveAttribLocationsMask(other.mActiveAttribLocationsMask),
      mMaxActiveAttribLocation(other.mMaxActiveAttribLocation),
      mAttributesTypeMask(other.mAttributesTypeMask),
      mAttributesMask(other.mAttributesMask),
      mActiveSamplersMask(other.mActiveSamplersMask),
      mActiveSamplerRefCounts(other.mActiveSamplerRefCounts),
      mActiveSamplerTypes(other.mActiveSamplerTypes),
      mActiveSamplerYUV(other.mActiveSamplerYUV),
      mActiveSamplerFormats(other.mActiveSamplerFormats),
      mActiveSamplerShaderBits(other.mActiveSamplerShaderBits),
      mActiveImagesMask(other.mActiveImagesMask),
      mActiveImageShaderBits(other.mActiveImageShaderBits),
      mCanDrawWith(other.mCanDrawWith),
      mOutputVariables(other.mOutputVariables),
      mOutputLocations(other.mOutputLocations),
      mYUVOutput(other.mYUVOutput),
      mProgramInputs(other.mProgramInputs),
      mLinkedTransformFeedbackVaryings(other.mLinkedTransformFeedbackVaryings),
      mTransformFeedbackStrides(other.mTransformFeedbackStrides),
      mTransformFeedbackBufferMode(other.mTransformFeedbackBufferMode),
      mUniforms(other.mUniforms),
      mDefaultUniformRange(other.mDefaultUniformRange),
      mSamplerUniformRange(other.mSamplerUniformRange),
      mUniformBlocks(other.mUniformBlocks),
      mAtomicCounterBuffers(other.mAtomicCounterBuffers),
      mImageUniformRange(other.mImageUniformRange),
      mComputeShaderStorageBlocks(other.mComputeShaderStorageBlocks),
      mGraphicsShaderStorageBlocks(other.mGraphicsShaderStorageBlocks),
      mPipelineHasGraphicsUniformBuffers(other.mPipelineHasGraphicsUniformBuffers),
      mPipelineHasComputeUniformBuffers(other.mPipelineHasComputeUniformBuffers),
      mPipelineHasGraphicsStorageBuffers(other.mPipelineHasGraphicsStorageBuffers),
      mPipelineHasComputeStorageBuffers(other.mPipelineHasComputeStorageBuffers),
      mPipelineHasGraphicsAtomicCounterBuffers(other.mPipelineHasGraphicsAtomicCounterBuffers),
      mPipelineHasComputeAtomicCounterBuffers(other.mPipelineHasComputeAtomicCounterBuffers),
      mPipelineHasGraphicsDefaultUniforms(other.mPipelineHasGraphicsDefaultUniforms),
      mPipelineHasComputeDefaultUniforms(other.mPipelineHasComputeDefaultUniforms),
      mPipelineHasGraphicsTextures(other.mPipelineHasGraphicsTextures),
      mPipelineHasComputeTextures(other.mPipelineHasComputeTextures),
      mPipelineHasGraphicsImages(other.mPipelineHasGraphicsImages),
      mPipelineHasComputeImages(other.mPipelineHasComputeImages),
      mIsCompute(other.mIsCompute)
{
    reset();
}

ProgramExecutable::~ProgramExecutable() = default;

void ProgramExecutable::reset()
{
    resetInfoLog();
    mActiveAttribLocationsMask.reset();
    mAttributesTypeMask.reset();
    mAttributesMask.reset();
    mMaxActiveAttribLocation = 0;

    mActiveSamplersMask.reset();
    mActiveSamplerRefCounts = {};
    mActiveSamplerTypes.fill(TextureType::InvalidEnum);
    mActiveSamplerYUV.reset();
    mActiveSamplerFormats.fill(SamplerFormat::InvalidEnum);

    mActiveImagesMask.reset();

    mProgramInputs.clear();
    mLinkedTransformFeedbackVaryings.clear();
    mUniforms.clear();
    mUniformBlocks.clear();
    mComputeShaderStorageBlocks.clear();
    mGraphicsShaderStorageBlocks.clear();
    mAtomicCounterBuffers.clear();
    mOutputVariables.clear();
    mOutputLocations.clear();
    mYUVOutput = false;
    mSamplerBindings.clear();
    mComputeImageBindings.clear();
    mGraphicsImageBindings.clear();

    mPipelineHasGraphicsUniformBuffers       = false;
    mPipelineHasComputeUniformBuffers        = false;
    mPipelineHasGraphicsStorageBuffers       = false;
    mPipelineHasComputeStorageBuffers        = false;
    mPipelineHasGraphicsAtomicCounterBuffers = false;
    mPipelineHasComputeAtomicCounterBuffers  = false;
    mPipelineHasGraphicsDefaultUniforms      = false;
    mPipelineHasComputeDefaultUniforms       = false;
    mPipelineHasGraphicsTextures             = false;
    mPipelineHasComputeTextures              = false;

    mGeometryShaderInputPrimitiveType  = PrimitiveMode::Triangles;
    mGeometryShaderOutputPrimitiveType = PrimitiveMode::TriangleStrip;
    mGeometryShaderInvocations         = 1;
    mGeometryShaderMaxVertices         = 0;
}

void ProgramExecutable::load(gl::BinaryInputStream *stream)
{
    static_assert(MAX_VERTEX_ATTRIBS * 2 <= sizeof(uint32_t) * 8,
                  "Too many vertex attribs for mask: All bits of mAttributesTypeMask types and "
                  "mask fit into 32 bits each");
    mAttributesTypeMask        = gl::ComponentTypeMask(stream->readInt<uint32_t>());
    mAttributesMask            = gl::AttributesMask(stream->readInt<uint32_t>());
    mActiveAttribLocationsMask = gl::AttributesMask(stream->readInt<uint32_t>());
    mMaxActiveAttribLocation   = stream->readInt<unsigned int>();

    mLinkedGraphicsShaderStages = ShaderBitSet(stream->readInt<uint8_t>());
    mLinkedComputeShaderStages  = ShaderBitSet(stream->readInt<uint8_t>());
    mIsCompute                  = stream->readBool();

    mPipelineHasGraphicsUniformBuffers       = stream->readBool();
    mPipelineHasComputeUniformBuffers        = stream->readBool();
    mPipelineHasGraphicsStorageBuffers       = stream->readBool();
    mPipelineHasComputeStorageBuffers        = stream->readBool();
    mPipelineHasGraphicsAtomicCounterBuffers = stream->readBool();
    mPipelineHasComputeAtomicCounterBuffers  = stream->readBool();
    mPipelineHasGraphicsDefaultUniforms      = stream->readBool();
    mPipelineHasComputeDefaultUniforms       = stream->readBool();
    mPipelineHasGraphicsTextures             = stream->readBool();
    mPipelineHasComputeTextures              = stream->readBool();

    mGeometryShaderInputPrimitiveType  = stream->readEnum<PrimitiveMode>();
    mGeometryShaderOutputPrimitiveType = stream->readEnum<PrimitiveMode>();
    mGeometryShaderInvocations         = stream->readInt<int>();
    mGeometryShaderMaxVertices         = stream->readInt<int>();
}

void ProgramExecutable::save(gl::BinaryOutputStream *stream) const
{
    static_assert(MAX_VERTEX_ATTRIBS * 2 <= sizeof(uint32_t) * 8,
                  "All bits of mAttributesTypeMask types and mask fit into 32 bits each");
    stream->writeInt(static_cast<uint32_t>(mAttributesTypeMask.to_ulong()));
    stream->writeInt(static_cast<uint32_t>(mAttributesMask.to_ulong()));
    stream->writeInt(static_cast<uint32_t>(mActiveAttribLocationsMask.to_ulong()));
    stream->writeInt(mMaxActiveAttribLocation);

    stream->writeInt(mLinkedGraphicsShaderStages.bits());
    stream->writeInt(mLinkedComputeShaderStages.bits());
    stream->writeBool(mIsCompute);

    stream->writeBool(mPipelineHasGraphicsUniformBuffers);
    stream->writeBool(mPipelineHasComputeUniformBuffers);
    stream->writeBool(mPipelineHasGraphicsStorageBuffers);
    stream->writeBool(mPipelineHasComputeStorageBuffers);
    stream->writeBool(mPipelineHasGraphicsAtomicCounterBuffers);
    stream->writeBool(mPipelineHasComputeAtomicCounterBuffers);
    stream->writeBool(mPipelineHasGraphicsDefaultUniforms);
    stream->writeBool(mPipelineHasComputeDefaultUniforms);
    stream->writeBool(mPipelineHasGraphicsTextures);
    stream->writeBool(mPipelineHasComputeTextures);

    ASSERT(mGeometryShaderInvocations >= 1 && mGeometryShaderMaxVertices >= 0);
    stream->writeEnum(mGeometryShaderInputPrimitiveType);
    stream->writeEnum(mGeometryShaderOutputPrimitiveType);
    stream->writeInt(mGeometryShaderInvocations);
    stream->writeInt(mGeometryShaderMaxVertices);
}

int ProgramExecutable::getInfoLogLength() const
{
    return static_cast<int>(mInfoLog.getLength());
}

void ProgramExecutable::getInfoLog(GLsizei bufSize, GLsizei *length, char *infoLog) const
{
    return mInfoLog.getLog(bufSize, length, infoLog);
}

std::string ProgramExecutable::getInfoLogString() const
{
    return mInfoLog.str();
}

bool ProgramExecutable::isAttribLocationActive(size_t attribLocation) const
{
    // TODO(timvp): http://anglebug.com/3570: Enable this assert here somehow.
    //    ASSERT(!mLinkingState);
    ASSERT(attribLocation < mActiveAttribLocationsMask.size());
    return mActiveAttribLocationsMask[attribLocation];
}

AttributesMask ProgramExecutable::getAttributesMask() const
{
    // TODO(timvp): http://anglebug.com/3570: Enable this assert here somehow.
    //    ASSERT(!mLinkingState);
    return mAttributesMask;
}

bool ProgramExecutable::hasDefaultUniforms() const
{
    return !getDefaultUniformRange().empty() ||
           (isCompute() ? mPipelineHasComputeDefaultUniforms : mPipelineHasGraphicsDefaultUniforms);
}

bool ProgramExecutable::hasTextures() const
{
    return !getSamplerBindings().empty() ||
           (isCompute() ? mPipelineHasComputeTextures : mPipelineHasGraphicsTextures);
}

// TODO: http://anglebug.com/3570: Remove mHas*UniformBuffers once PPO's have valid data in
// mUniformBlocks
bool ProgramExecutable::hasUniformBuffers() const
{
    return !getUniformBlocks().empty() ||
           (isCompute() ? mPipelineHasComputeUniformBuffers : mPipelineHasGraphicsUniformBuffers);
}

bool ProgramExecutable::hasStorageBuffers() const
{
    return (isCompute() ? hasComputeStorageBuffers() : hasGraphicsStorageBuffers());
}

bool ProgramExecutable::hasGraphicsStorageBuffers() const
{
    return !mGraphicsShaderStorageBlocks.empty() || mPipelineHasGraphicsStorageBuffers;
}

bool ProgramExecutable::hasComputeStorageBuffers() const
{
    return !mComputeShaderStorageBlocks.empty() || mPipelineHasComputeStorageBuffers;
}

bool ProgramExecutable::hasAtomicCounterBuffers() const
{
    return !getAtomicCounterBuffers().empty() ||
           (isCompute() ? mPipelineHasComputeAtomicCounterBuffers
                        : mPipelineHasGraphicsAtomicCounterBuffers);
}

bool ProgramExecutable::hasImages() const
{
    return (isCompute() ? hasComputeImages() : hasGraphicsImages());
}

bool ProgramExecutable::hasGraphicsImages() const
{
    return !mGraphicsImageBindings.empty() || mPipelineHasGraphicsImages;
}

bool ProgramExecutable::hasComputeImages() const
{
    return !mComputeImageBindings.empty() || mPipelineHasComputeImages;
}

GLuint ProgramExecutable::getUniformIndexFromImageIndex(GLuint imageIndex) const
{
    ASSERT(imageIndex < mImageUniformRange.length());
    return imageIndex + mImageUniformRange.low();
}

void ProgramExecutable::updateActiveSamplers(const ProgramState &programState)
{
    const std::vector<SamplerBinding> &samplerBindings = programState.getSamplerBindings();

    for (uint32_t samplerIndex = 0; samplerIndex < samplerBindings.size(); ++samplerIndex)
    {
        const SamplerBinding &samplerBinding = samplerBindings[samplerIndex];
        uint32_t uniformIndex = programState.getUniformIndexFromSamplerIndex(samplerIndex);
        const gl::LinkedUniform &samplerUniform = programState.getUniforms()[uniformIndex];

        for (GLint textureUnit : samplerBinding.boundTextureUnits)
        {
            if (++mActiveSamplerRefCounts[textureUnit] == 1)
            {
                mActiveSamplerTypes[textureUnit]   = samplerBinding.textureType;
                mActiveSamplerYUV[textureUnit]     = IsSamplerYUVType(samplerBinding.samplerType);
                mActiveSamplerFormats[textureUnit] = samplerBinding.format;
                mActiveSamplerShaderBits[textureUnit] = samplerUniform.activeShaders();
            }
            else
            {
                if (mActiveSamplerTypes[textureUnit] != samplerBinding.textureType)
                {
                    mActiveSamplerTypes[textureUnit] = TextureType::InvalidEnum;
                }
                if (mActiveSamplerYUV.test(textureUnit) !=
                    IsSamplerYUVType(samplerBinding.samplerType))
                {
                    mActiveSamplerYUV[textureUnit] = false;
                }
                if (mActiveSamplerFormats[textureUnit] != samplerBinding.format)
                {
                    mActiveSamplerFormats[textureUnit] = SamplerFormat::InvalidEnum;
                }
            }
            mActiveSamplersMask.set(textureUnit);
        }
    }
}

void ProgramExecutable::updateActiveImages(const ProgramExecutable &executable)
{
    const std::vector<ImageBinding> *imageBindings = getImageBindings();
    for (uint32_t imageIndex = 0; imageIndex < imageBindings->size(); ++imageIndex)
    {
        const gl::ImageBinding &imageBinding = imageBindings->at(imageIndex);

        uint32_t uniformIndex = executable.getUniformIndexFromImageIndex(imageIndex);
        const gl::LinkedUniform &imageUniform = executable.getUniforms()[uniformIndex];
        const ShaderBitSet shaderBits         = imageUniform.activeShaders();
        for (GLint imageUnit : imageBinding.boundImageUnits)
        {
            mActiveImagesMask.set(imageUnit);
            if (isCompute())
            {
                mActiveImageShaderBits[imageUnit].set(gl::ShaderType::Compute);
            }
            else
            {
                mActiveImageShaderBits[imageUnit] = shaderBits;
            }
        }
    }
}

void ProgramExecutable::setSamplerUniformTextureTypeAndFormat(
    size_t textureUnitIndex,
    std::vector<SamplerBinding> &samplerBindings)
{
    bool foundBinding         = false;
    TextureType foundType     = TextureType::InvalidEnum;
    bool foundYUV             = false;
    SamplerFormat foundFormat = SamplerFormat::InvalidEnum;

    for (const SamplerBinding &binding : samplerBindings)
    {
        // A conflict exists if samplers of different types are sourced by the same texture unit.
        // We need to check all bound textures to detect this error case.
        for (GLuint textureUnit : binding.boundTextureUnits)
        {
            if (textureUnit == textureUnitIndex)
            {
                if (!foundBinding)
                {
                    foundBinding = true;
                    foundType    = binding.textureType;
                    foundYUV     = IsSamplerYUVType(binding.samplerType);
                    foundFormat  = binding.format;
                }
                else
                {
                    if (foundType != binding.textureType)
                    {
                        foundType = TextureType::InvalidEnum;
                    }
                    if (foundYUV != IsSamplerYUVType(binding.samplerType))
                    {
                        foundYUV = false;
                    }
                    if (foundFormat != binding.format)
                    {
                        foundFormat = SamplerFormat::InvalidEnum;
                    }
                }
            }
        }
    }

    mActiveSamplerTypes[textureUnitIndex]   = foundType;
    mActiveSamplerYUV[textureUnitIndex]     = foundYUV;
    mActiveSamplerFormats[textureUnitIndex] = foundFormat;
}

void ProgramExecutable::updateCanDrawWith()
{
    mCanDrawWith =
        (hasLinkedShaderStage(ShaderType::Vertex) && hasLinkedShaderStage(ShaderType::Fragment));
}

void ProgramExecutable::saveLinkedStateInfo(const ProgramState &state)
{
    for (ShaderType shaderType : getLinkedShaderStages())
    {
        Shader *shader = state.getAttachedShader(shaderType);
        ASSERT(shader);
        mLinkedOutputVaryings[shaderType] = shader->getOutputVaryings();
        mLinkedInputVaryings[shaderType]  = shader->getInputVaryings();
        mLinkedShaderVersions[shaderType] = shader->getShaderVersion();
    }
}

bool ProgramExecutable::isYUVOutput() const
{
    return !isCompute() && mYUVOutput;
}

ShaderType ProgramExecutable::getLinkedTransformFeedbackStage() const
{
    if (mLinkedGraphicsShaderStages[ShaderType::Geometry])
    {
        return ShaderType::Geometry;
    }
    if (mLinkedGraphicsShaderStages[ShaderType::TessEvaluation])
    {
        return ShaderType::TessEvaluation;
    }
    return ShaderType::Vertex;
}

bool ProgramExecutable::linkMergedVaryings(
    const Context *context,
    const HasAttachedShaders &programOrPipeline,
    const ProgramMergedVaryings &mergedVaryings,
    const std::vector<std::string> &transformFeedbackVaryingNames,
    bool isSeparable,
    ProgramVaryingPacking *varyingPacking)
{
    ShaderType tfStage = programOrPipeline.getAttachedShader(ShaderType::Geometry)
                             ? ShaderType::Geometry
                             : ShaderType::Vertex;

    if (!linkValidateTransformFeedback(context, mergedVaryings, tfStage,
                                       transformFeedbackVaryingNames))
    {
        return false;
    }

    // Map the varyings to the register file
    // In WebGL, we use a slightly different handling for packing variables.
    gl::PackMode packMode = PackMode::ANGLE_RELAXED;
    if (context->getLimitations().noFlexibleVaryingPacking)
    {
        // D3D9 pack mode is strictly more strict than WebGL, so takes priority.
        packMode = PackMode::ANGLE_NON_CONFORMANT_D3D9;
    }
    else if (context->getExtensions().webglCompatibility)
    {
        packMode = PackMode::WEBGL_STRICT;
    }

    // Build active shader stage map.
    ShaderBitSet attachedShadersMask;
    for (ShaderType shaderType : kAllGraphicsShaderTypes)
    {
        if (programOrPipeline.getAttachedShader(shaderType))
        {
            attachedShadersMask[shaderType] = true;
        }
    }

    if (!varyingPacking->collectAndPackUserVaryings(mInfoLog, context->getCaps(), packMode,
                                                    attachedShadersMask, mergedVaryings,
                                                    transformFeedbackVaryingNames, isSeparable))
    {
        return false;
    }

    gatherTransformFeedbackVaryings(mergedVaryings, tfStage, transformFeedbackVaryingNames);
    updateTransformFeedbackStrides();

    return true;
}

bool ProgramExecutable::linkValidateTransformFeedback(
    const Context *context,
    const ProgramMergedVaryings &varyings,
    ShaderType stage,
    const std::vector<std::string> &transformFeedbackVaryingNames)
{
    const Version &version = context->getClientVersion();

    // Validate the tf names regardless of the actual program varyings.
    std::set<std::string> uniqueNames;
    for (const std::string &tfVaryingName : transformFeedbackVaryingNames)
    {
        if (version < Version(3, 1) && tfVaryingName.find('[') != std::string::npos)
        {
            mInfoLog << "Capture of array elements is undefined and not supported.";
            return false;
        }
        if (version >= Version(3, 1))
        {
            if (IncludeSameArrayElement(uniqueNames, tfVaryingName))
            {
                mInfoLog << "Two transform feedback varyings include the same array element ("
                         << tfVaryingName << ").";
                return false;
            }
        }
        else
        {
            if (uniqueNames.count(tfVaryingName) > 0)
            {
                mInfoLog << "Two transform feedback varyings specify the same output variable ("
                         << tfVaryingName << ").";
                return false;
            }
        }
        uniqueNames.insert(tfVaryingName);
    }

    // Validate against program varyings.
    size_t totalComponents = 0;
    for (const std::string &tfVaryingName : transformFeedbackVaryingNames)
    {
        std::vector<unsigned int> subscripts;
        std::string baseName = ParseResourceName(tfVaryingName, &subscripts);

        const sh::ShaderVariable *var = FindOutputVaryingOrField(varyings, stage, baseName);
        if (var == nullptr)
        {
            mInfoLog << "Transform feedback varying " << tfVaryingName
                     << " does not exist in the vertex shader.";
            return false;
        }

        // Validate the matching variable.
        if (var->isStruct())
        {
            mInfoLog << "Struct cannot be captured directly (" << baseName << ").";
            return false;
        }

        size_t elementCount   = 0;
        size_t componentCount = 0;

        if (var->isArray())
        {
            if (version < Version(3, 1))
            {
                mInfoLog << "Capture of arrays is undefined and not supported.";
                return false;
            }

            // GLSL ES 3.10 section 4.3.6: A vertex output can't be an array of arrays.
            ASSERT(!var->isArrayOfArrays());

            if (!subscripts.empty() && subscripts[0] >= var->getOutermostArraySize())
            {
                mInfoLog << "Cannot capture outbound array element '" << tfVaryingName << "'.";
                return false;
            }
            elementCount = (subscripts.empty() ? var->getOutermostArraySize() : 1);
        }
        else
        {
            if (!subscripts.empty())
            {
                mInfoLog << "Varying '" << baseName
                         << "' is not an array to be captured by element.";
                return false;
            }
            elementCount = 1;
        }

        const Caps &caps = context->getCaps();

        // TODO(jmadill): Investigate implementation limits on D3D11
        componentCount = VariableComponentCount(var->type) * elementCount;
        if (mTransformFeedbackBufferMode == GL_SEPARATE_ATTRIBS &&
            componentCount > static_cast<GLuint>(caps.maxTransformFeedbackSeparateComponents))
        {
            mInfoLog << "Transform feedback varying " << tfVaryingName << " components ("
                     << componentCount << ") exceed the maximum separate components ("
                     << caps.maxTransformFeedbackSeparateComponents << ").";
            return false;
        }

        totalComponents += componentCount;
        if (mTransformFeedbackBufferMode == GL_INTERLEAVED_ATTRIBS &&
            totalComponents > static_cast<GLuint>(caps.maxTransformFeedbackInterleavedComponents))
        {
            mInfoLog << "Transform feedback varying total components (" << totalComponents
                     << ") exceed the maximum interleaved components ("
                     << caps.maxTransformFeedbackInterleavedComponents << ").";
            return false;
        }
    }
    return true;
}

void ProgramExecutable::gatherTransformFeedbackVaryings(
    const ProgramMergedVaryings &varyings,
    ShaderType stage,
    const std::vector<std::string> &transformFeedbackVaryingNames)
{
    // Gather the linked varyings that are used for transform feedback, they should all exist.
    mLinkedTransformFeedbackVaryings.clear();
    for (const std::string &tfVaryingName : transformFeedbackVaryingNames)
    {
        std::vector<unsigned int> subscripts;
        std::string baseName = ParseResourceName(tfVaryingName, &subscripts);
        size_t subscript     = GL_INVALID_INDEX;
        if (!subscripts.empty())
        {
            subscript = subscripts.back();
        }
        for (const ProgramVaryingRef &ref : varyings)
        {
            if (ref.frontShaderStage != stage)
            {
                continue;
            }

            const sh::ShaderVariable *varying = ref.get(stage);
            if (baseName == varying->name)
            {
                mLinkedTransformFeedbackVaryings.emplace_back(*varying,
                                                              static_cast<GLuint>(subscript));
                break;
            }
            else if (varying->isStruct())
            {
                GLuint fieldIndex = 0;
                const auto *field = varying->findField(tfVaryingName, &fieldIndex);
                if (field != nullptr)
                {
                    mLinkedTransformFeedbackVaryings.emplace_back(*field, *varying);
                    break;
                }
            }
        }
    }
}

void ProgramExecutable::updateTransformFeedbackStrides()
{
    if (mTransformFeedbackBufferMode == GL_INTERLEAVED_ATTRIBS)
    {
        mTransformFeedbackStrides.resize(1);
        size_t totalSize = 0;
        for (const TransformFeedbackVarying &varying : mLinkedTransformFeedbackVaryings)
        {
            totalSize += varying.size() * VariableExternalSize(varying.type);
        }
        mTransformFeedbackStrides[0] = static_cast<GLsizei>(totalSize);
    }
    else
    {
        mTransformFeedbackStrides.resize(mLinkedTransformFeedbackVaryings.size());
        for (size_t i = 0; i < mLinkedTransformFeedbackVaryings.size(); i++)
        {
            TransformFeedbackVarying &varying = mLinkedTransformFeedbackVaryings[i];
            mTransformFeedbackStrides[i] =
                static_cast<GLsizei>(varying.size() * VariableExternalSize(varying.type));
        }
    }
}

}  // namespace gl
