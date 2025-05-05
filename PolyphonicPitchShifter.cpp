#include "PolyphonicPitchShifter.h"
#include "AudioUnitSDK/AUPlugInDispatch.h"
#include <cmath>

// Component registration
AUDIOCOMPONENT_ENTRY(ausdk::AUBaseFactory, PolyphonicPitchShifter)

// Static creator function
OSStatus PolyphonicPitchShifter::CreateEffectInstance(AudioComponentInstance inInstance) {
    return new PolyphonicPitchShifter(inInstance);
}

// Constructor
PolyphonicPitchShifter::PolyphonicPitchShifter(AudioComponentInstance component)
    : ausdk::AUEffectBase(component),
      mPitchShift(kDefaultPitchShift),
      mMix(kDefaultMix),
      mFormant(kDefaultFormant),
      mLatencyMode(kDefaultLatency),
      mNeedsReset(true),
      mIsInitialized(false),
      mLatency(0.0),
      mSampleRate(44100.0),
      mMaxFramesToProcess(4096)
{
    // Initialize parameters
    SetParameter(kParam_PitchShift, kAudioUnitScope_Global, 0, kDefaultPitchShift, 0);
    SetParameter(kParam_Mix, kAudioUnitScope_Global, 0, kDefaultMix, 0);
    SetParameter(kParam_Formant, kAudioUnitScope_Global, 0, kDefaultFormant, 0);
    SetParameter(kParam_Latency, kAudioUnitScope_Global, 0, kDefaultLatency, 0);
}

// Initialize the audio unit
OSStatus PolyphonicPitchShifter::Initialize() {
    OSStatus result = ausdk::AUEffectBase::Initialize();
    if (result != noErr) {
        return result;
    }
    
    // Get audio format info
    const AudioStreamBasicDescription& inputFormat = GetInput(0)->GetStreamFormat();
    mMaxFramesToProcess = GetMaxFramesPerSlice();
    mSampleRate = inputFormat.mSampleRate;
    
    // Initialize RubberBand
    initializeRubberBand();
    
    // Allocate buffers
    mInputBufferL.resize(mMaxFramesToProcess);
    mInputBufferR.resize(mMaxFramesToProcess);
    mOutputBufferL.resize(mMaxFramesToProcess);
    mOutputBufferR.resize(mMaxFramesToProcess);
    
    // Setup buffer pointers
    mInputBuffers.resize(2);
    mOutputBuffers.resize(2);
    mInputBuffers[0] = mInputBufferL.data();
    mInputBuffers[1] = mInputBufferR.data();
    mOutputBuffers[0] = mOutputBufferL.data();
    mOutputBuffers[1] = mOutputBufferR.data();
    
    mIsInitialized = true;
    return noErr;
}

// Cleanup resources
void PolyphonicPitchShifter::Cleanup() {
    // Clean up RubberBand and buffers
    mStretcher.reset();
    mInputBufferL.clear();
    mInputBufferR.clear();
    mOutputBufferL.clear();
    mOutputBufferR.clear();
    mInputBuffers.clear();
    mOutputBuffers.clear();
    mIsInitialized = false;
}

// Reset the processor
OSStatus PolyphonicPitchShifter::Reset(AudioUnitScope inScope, AudioUnitElement inElement) {
    OSStatus result = ausdk::AUEffectBase::Reset(inScope, inElement);
    if (result != noErr) {
        return result;
    }
    
    if (mStretcher) {
        resetRubberBand();
    }
    
    return noErr;
}

// Initialize RubberBand stretcher
void PolyphonicPitchShifter::initializeRubberBand() {
    const AudioStreamBasicDescription& inputFormat = GetInput(0)->GetStreamFormat();
    int numChannels = inputFormat.mChannelsPerFrame;
    
    // Create RubberBand stretcher with realtime options
    mStretcher = std::make_unique<RubberBand::RubberBandStretcher>(
        inputFormat.mSampleRate,
        numChannels,
        RubberBand::RubberBandStretcher::OptionProcessRealTime |
        RubberBand::RubberBandStretcher::OptionPitchHighQuality
    );
    
    // Set initial parameters
    updateRubberBandParameters();
    mStretcher->setMaxProcessSize(mMaxFramesToProcess);
}

// Reset RubberBand stretcher
void PolyphonicPitchShifter::resetRubberBand() {
    std::lock_guard<std::mutex> lock(mProcessMutex);
    if (mStretcher) {
        mStretcher->reset();
        mNeedsReset = false;
    }
}

// Update RubberBand parameters from current settings
void PolyphonicPitchShifter::updateRubberBandParameters() {
    if (!mStretcher) return;
    
    std::lock_guard<std::mutex> lock(mProcessMutex);
    
    // Set pitch shift
    double pitchScale = pow(2.0, mPitchShift / 12.0);
    mStretcher->setPitchScale(pitchScale);
    
    // Set transients mode - using OptionTransientsCrisp 
    // (OptionTransientsPreserve doesn't exist in this version of RubberBand)
    RubberBand::RubberBandStretcher::Options options = mStretcher->getOptions();
    options = (options & ~RubberBand::RubberBandStretcher::OptionTransientsMask) | 
              RubberBand::RubberBandStretcher::OptionTransientsCrisp;
    
    // Set formant preservation
    if (mFormant > 50.0f) {
        options |= RubberBand::RubberBandStretcher::OptionFormantPreserved;
    } else {
        options &= ~RubberBand::RubberBandStretcher::OptionFormantPreserved;
    }
    
    // Update options
    mStretcher->setOptions(options);
    
    // Calculate and set latency
    mLatency = mLatencyMode * 0.05; // Simplified latency calculation
}

// Process audio
OSStatus PolyphonicPitchShifter::ProcessBufferLists(
    AudioUnitRenderActionFlags& ioActionFlags,
    const AudioBufferList& inBuffer,
    AudioBufferList& outBuffer,
    UInt32 inFramesToProcess
) {
    if (!mIsInitialized || !mStretcher) {
        return kAudioUnitErr_Uninitialized;
    }
    
    std::lock_guard<std::mutex> lock(mProcessMutex);
    
    // Check if we need to update parameters
    if (mNeedsReset) {
        resetRubberBand();
        updateRubberBandParameters();
    }
    
    // Get channel counts
    const UInt32 numInputChannels = inBuffer.mNumberBuffers;
    const UInt32 numOutputChannels = outBuffer.mNumberBuffers;
    const UInt32 numChannels = std::min(numInputChannels, numOutputChannels);
    const UInt32 numChannelsToProcess = std::min(numChannels, 2u); // Max 2 channels for now
    
    // Copy input data to our processing buffers
    for (UInt32 channel = 0; channel < numChannelsToProcess; ++channel) {
        const float* inputData = static_cast<const float*>(inBuffer.mBuffers[channel].mData);
        float* inputBuffer = (channel == 0) ? mInputBufferL.data() : mInputBufferR.data();
        
        // Copy input data
        memcpy(inputBuffer, inputData, inFramesToProcess * sizeof(float));
    }
    
    // Process with RubberBand
    mStretcher->process(mInputBuffers.data(), inFramesToProcess, false);
    
    // Get available output
    const UInt32 available = mStretcher->available();
    if (available > 0) {
        // Retrieve output from RubberBand
        mStretcher->retrieve(mOutputBuffers.data(), available);
        
        // Copy output and apply mix
        for (UInt32 channel = 0; channel < numChannelsToProcess; ++channel) {
            const float* inputData = static_cast<const float*>(inBuffer.mBuffers[channel].mData);
            float* outputData = static_cast<float*>(outBuffer.mBuffers[channel].mData);
            const float* processedData = (channel == 0) ? mOutputBufferL.data() : mOutputBufferR.data();
            
            const float wet = mMix / 100.0f;
            const float dry = 1.0f - wet;
            
            // Apply wet/dry mix
            for (UInt32 frame = 0; frame < available && frame < inFramesToProcess; ++frame) {
                outputData[frame] = wet * processedData[frame] + dry * inputData[frame];
            }
            
            // Fill any remaining frames with input
            for (UInt32 frame = available; frame < inFramesToProcess; ++frame) {
                outputData[frame] = inputData[frame];
            }
        }
        
        // Copy processed output to any remaining channels
        for (UInt32 channel = numChannelsToProcess; channel < numOutputChannels; ++channel) {
            if (channel < numInputChannels) {
                // Copy from input
                const float* inputData = static_cast<const float*>(inBuffer.mBuffers[channel].mData);
                float* outputData = static_cast<float*>(outBuffer.mBuffers[channel].mData);
                memcpy(outputData, inputData, inFramesToProcess * sizeof(float));
            } else {
                // Silence
                float* outputData = static_cast<float*>(outBuffer.mBuffers[channel].mData);
                memset(outputData, 0, inFramesToProcess * sizeof(float));
            }
        }
    } else {
        // No output available, pass through input
        for (UInt32 channel = 0; channel < numOutputChannels; ++channel) {
            float* outputData = static_cast<float*>(outBuffer.mBuffers[channel].mData);
            
            if (channel < numInputChannels) {
                const float* inputData = static_cast<const float*>(inBuffer.mBuffers[channel].mData);
                memcpy(outputData, inputData, inFramesToProcess * sizeof(float));
            } else {
                memset(outputData, 0, inFramesToProcess * sizeof(float));
            }
        }
    }
    
    return noErr;
}

// Get property info
OSStatus PolyphonicPitchShifter::GetPropertyInfo(AudioUnitPropertyID inID,
                                               AudioUnitScope inScope,
                                               AudioUnitElement inElement,
                                               UInt32& outDataSize,
                                               bool& outWritable) {
    return ausdk::AUEffectBase::GetPropertyInfo(inID, inScope, inElement, outDataSize, outWritable);
}

// Get property
OSStatus PolyphonicPitchShifter::GetProperty(AudioUnitPropertyID inID,
                                           AudioUnitScope inScope,
                                           AudioUnitElement inElement,
                                           void* outData) {
    return ausdk::AUEffectBase::GetProperty(inID, inScope, inElement, outData);
}

// Get parameter info
OSStatus PolyphonicPitchShifter::GetParameterInfo(AudioUnitScope inScope,
                                                AudioUnitParameterID inID,
                                                AudioUnitParameterInfo& outParameterInfo) {
    // Make sure scope is global
    if (inScope != kAudioUnitScope_Global) {
        return kAudioUnitErr_InvalidScope;
    }
    
    // Initialize the parameter info struct
    AudioUnitParameterInfo& info = outParameterInfo;
    info.flags = kAudioUnitParameterFlag_IsWritable | kAudioUnitParameterFlag_IsReadable;
    
    switch (inID) {
        case kParam_PitchShift:
            info.name = CFSTR("Pitch Shift");
            info.unitName = CFSTR("semitones");
            info.minValue = kMinPitchShift;
            info.maxValue = kMaxPitchShift;
            info.defaultValue = kDefaultPitchShift;
            info.unit = kAudioUnitParameterUnit_RelativeSemiTones;
            break;
            
        case kParam_Mix:
            info.name = CFSTR("Mix");
            info.unitName = CFSTR("%");
            info.minValue = kMinMix;
            info.maxValue = kMaxMix;
            info.defaultValue = kDefaultMix;
            info.unit = kAudioUnitParameterUnit_Percent;
            break;
            
        case kParam_Formant:
            info.name = CFSTR("Formant Preservation");
            info.unitName = CFSTR("%");
            info.minValue = kMinFormant;
            info.maxValue = kMaxFormant;
            info.defaultValue = kDefaultFormant;
            info.unit = kAudioUnitParameterUnit_Percent;
            break;
            
        case kParam_Latency:
            info.name = CFSTR("Low Latency Mode");
            info.unitName = CFSTR("");
            info.minValue = kMinLatency;
            info.maxValue = kMaxLatency;
            info.defaultValue = kDefaultLatency;
            info.unit = kAudioUnitParameterUnit_Boolean;
            break;
            
        default:
            return kAudioUnitErr_InvalidParameter;
    }
    
    return noErr;
}

// Set parameter
OSStatus PolyphonicPitchShifter::SetParameter(AudioUnitParameterID inID,
                                            AudioUnitScope inScope,
                                            AudioUnitElement inElement,
                                            AudioUnitParameterValue inValue,
                                            UInt32 inBufferOffsetInFrames) {
    // Update our parameter values and flag for reconfiguration
    if (inScope == kAudioUnitScope_Global) {
        switch (inID) {
            case kParam_PitchShift:
                mPitchShift = inValue;
                mNeedsReset = true;
                break;
                
            case kParam_Mix:
                mMix = inValue;
                break;
                
            case kParam_Formant:
                mFormant = inValue;
                mNeedsReset = true;
                break;
                
            case kParam_Latency:
                mLatencyMode = inValue;
                mNeedsReset = true;
                break;
                
            default:
                return kAudioUnitErr_InvalidParameter;
        }
    }
    
    // Call the base class implementation
    return ausdk::AUEffectBase::SetParameter(inID, inScope, inElement, inValue, inBufferOffsetInFrames);
}