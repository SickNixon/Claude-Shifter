#include "PolyphonicPitchShifter.h"
#include "AudioUnitSDK/AUPlugInDispatch.h"

// Component registration
AUDIOCOMPONENT_ENTRY(ausdk::AUBaseFactory, PolyphonicPitchShifter)

// Static creator function
OSStatus PolyphonicPitchShifter::CreateEffectInstance(AudioComponentInstance inInstance) {
    return new PolyphonicPitchShifter(inInstance);
}

// Constructor
PolyphonicPitchShifter::PolyphonicPitchShifter(AudioComponentInstance inComponentInstance)
    : ausdk::AUEffectBase(inComponentInstance),
      mPitchShift(kDefaultPitchShift),
      mMix(kDefaultMix),
      mFormant(kDefaultFormant),
      mLatency(kDefaultLatency),
      mNeedReconfiguration(true)
{
    // Set default parameter values
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
    UInt32 maxFrames = GetMaxFramesPerSlice();
    
    // Create the RubberBand stretcher
    mStretcher = std::make_unique<RubberBand::RubberBandStretcher>(
        inputFormat.mSampleRate,
        inputFormat.mChannelsPerFrame,
        RubberBand::RubberBandStretcher::OptionProcessRealTime |
        RubberBand::RubberBandStretcher::OptionPitchHighQuality
    );
    
    // Configure initial state
    ReconfigureStretcher();
    mStretcher->setMaxProcessSize(maxFrames);
    
    return noErr;
}

// Destructor
PolyphonicPitchShifter::~PolyphonicPitchShifter() {
    Cleanup();
}

// Cleanup resources
void PolyphonicPitchShifter::Cleanup() {
    mStretcher.reset();
}

// Reset the processor
OSStatus PolyphonicPitchShifter::Reset(AudioUnitScope inScope, AudioUnitElement inElement) {
    OSStatus result = ausdk::AUEffectBase::Reset(inScope, inElement);
    if (result != noErr) {
        return result;
    }
    
    if (mStretcher) {
        mStretcher->reset();
    }
    
    return noErr;
}

// Update stretcher configuration based on parameters
void PolyphonicPitchShifter::ReconfigureStretcher() {
    if (!mStretcher) {
        return;
    }
    
    // Set pitch shift
    mStretcher->setPitchScale(pow(2.0, mPitchShift / 12.0));
    
    // Set transients mode - using OptionTransientsCrisp instead of OptionTransientsPreserve
    mStretcher->setTransientsOption(RubberBand::RubberBandStretcher::OptionTransientsCrisp);
    
    // Set formant preservation
    RubberBand::RubberBandStretcher::Options options = mStretcher->getOptions();
    if (mFormant > 50.0f) {
        options |= RubberBand::RubberBandStretcher::OptionFormantPreserved;
    } else {
        options &= ~RubberBand::RubberBandStretcher::OptionFormantPreserved;
    }
    mStretcher->setOptions(options);
    
    // Set latency mode
    if (mLatency > 50.0f) {
        mStretcher->setTimeRatio(1.0);
    } else {
        mStretcher->setTimeRatio(1.0);  // Same setting, but could be adjusted for different latency modes
    }
    
    mNeedReconfiguration = false;
}

// Process audio
OSStatus PolyphonicPitchShifter::ProcessBufferLists(
    AudioUnitRenderActionFlags& ioActionFlags,
    const AudioBufferList& inBuffer,
    AudioBufferList& outBuffer,
    UInt32 inFramesToProcess
) {
    // Check if we need to reconfigure the stretcher
    if (mNeedReconfiguration) {
        ReconfigureStretcher();
    }
    
    // Check for formant preservation changes
    int currentOptions = mStretcher->getOptions();
    bool formantPreserved = (currentOptions & RubberBand::RubberBandStretcher::OptionFormantPreserved) != 0;
    
    if ((mFormant > 50.0f && !formantPreserved) ||
        (mFormant <= 50.0f && formantPreserved)) {
        ReconfigureStretcher();
    }
    
    // Get channel counts
    const UInt32 numInputChannels = inBuffer.mNumberBuffers;
    const UInt32 numOutputChannels = outBuffer.mNumberBuffers;
    
    // Process the input through RubberBand
    for (UInt32 channel = 0; channel < numInputChannels; ++channel) {
        float* inputData = static_cast<float*>(inBuffer.mBuffers[channel].mData);
        mStretcher->process(&inputData, inFramesToProcess, false);
    }
    
    // Retrieve available output
    const UInt32 available = mStretcher->available();
    if (available > 0) {
        // Create temporary output buffers for RubberBand
        std::vector<float*> outputPtrs(numOutputChannels);
        for (UInt32 channel = 0; channel < numOutputChannels; ++channel) {
            outputPtrs[channel] = static_cast<float*>(outBuffer.mBuffers[channel].mData);
        }
        
        // Retrieve output from RubberBand
        mStretcher->retrieve(outputPtrs.data(), available);
        
        // Apply wet/dry mix if not 100% wet
        if (mMix < 100.0f) {
            const float wet = mMix / 100.0f;
            const float dry = 1.0f - wet;
            
            for (UInt32 channel = 0; channel < numInputChannels && channel < numOutputChannels; ++channel) {
                float* inputData = static_cast<float*>(inBuffer.mBuffers[channel].mData);
                float* outputData = static_cast<float*>(outBuffer.mBuffers[channel].mData);
                
                for (UInt32 frame = 0; frame < inFramesToProcess && frame < available; ++frame) {
                    outputData[frame] = wet * outputData[frame] + dry * inputData[frame];
                }
            }
        }
    } else {
        // No output available yet, pass through the input
        for (UInt32 channel = 0; channel < numInputChannels && channel < numOutputChannels; ++channel) {
            float* inputData = static_cast<float*>(inBuffer.mBuffers[channel].mData);
            float* outputData = static_cast<float*>(outBuffer.mBuffers[channel].mData);
            
            memcpy(outputData, inputData, inFramesToProcess * sizeof(float));
        }
    }
    
    return noErr;
}

// Get parameter value strings
OSStatus PolyphonicPitchShifter::GetParameterValueStrings(AudioUnitScope inScope,
                                                        AudioUnitParameterID inParameterID,
                                                        CFArrayRef* outStrings) {
    // We don't use parameter strings
    return kAudioUnitErr_InvalidProperty;
}

// Get parameter info
OSStatus PolyphonicPitchShifter::GetParameterInfo(AudioUnitScope inScope,
                                                AudioUnitParameterID inParameterID,
                                                AudioUnitParameterInfo& outParameterInfo) {
    // Make sure scope is global
    if (inScope != kAudioUnitScope_Global) {
        return kAudioUnitErr_InvalidScope;
    }
    
    // Initialize the parameter info struct
    AudioUnitParameterInfo& info = outParameterInfo;
    info.flags = kAudioUnitParameterFlag_IsWritable | kAudioUnitParameterFlag_IsReadable;
    
    switch (inParameterID) {
        case kParam_PitchShift:
            info.name = CFSTR("Pitch Shift");
            info.unitName = CFSTR("semitones");
            info.minValue = -24.0f;
            info.maxValue = 24.0f;
            info.defaultValue = kDefaultPitchShift;
            info.unit = kAudioUnitParameterUnit_RelativeSemiTones;
            break;
            
        case kParam_Mix:
            info.name = CFSTR("Mix");
            info.unitName = CFSTR("%");
            info.minValue = 0.0f;
            info.maxValue = 100.0f;
            info.defaultValue = kDefaultMix;
            info.unit = kAudioUnitParameterUnit_Percent;
            break;
            
        case kParam_Formant:
            info.name = CFSTR("Formant Preservation");
            info.unitName = CFSTR("%");
            info.minValue = 0.0f;
            info.maxValue = 100.0f;
            info.defaultValue = kDefaultFormant;
            info.unit = kAudioUnitParameterUnit_Percent;
            break;
            
        case kParam_Latency:
            info.name = CFSTR("Low Latency Mode");
            info.unitName = CFSTR("%");
            info.minValue = 0.0f;
            info.maxValue = 100.0f;
            info.defaultValue = kDefaultLatency;
            info.unit = kAudioUnitParameterUnit_Percent;
            break;
            
        default:
            return kAudioUnitErr_InvalidParameter;
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

// Set parameter
OSStatus PolyphonicPitchShifter::SetParameter(AudioUnitParameterID inID,
                                           AudioUnitScope inScope,
                                           AudioUnitElement inElement,
                                           AudioUnitParameterValue inValue,
                                           UInt32 inBufferOffsetInFrames) {
    // Update our parameter values and flag for reconfiguration
    switch (inID) {
        case kParam_PitchShift:
            mPitchShift = inValue;
            mNeedReconfiguration = true;
            break;
            
        case kParam_Mix:
            mMix = inValue;
            break;
            
        case kParam_Formant:
            mFormant = inValue;
            mNeedReconfiguration = true;
            break;
            
        case kParam_Latency:
            mLatency = inValue;
            mNeedReconfiguration = true;
            break;
            
        default:
            return kAudioUnitErr_InvalidParameter;
    }
    
    // Call the base class implementation
    return ausdk::AUEffectBase::SetParameter(inID, inScope, inElement, inValue, inBufferOffsetInFrames);
}