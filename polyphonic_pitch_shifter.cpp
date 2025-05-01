#include "PolyphonicPitchShifter.h"
#include <cmath>

// Component entry point
AUDIOCOMPONENT_ENTRY(AUBaseFactory, PolyphonicPitchShifter)

// Factory method
OSStatus PolyphonicPitchShifter::CreateEffectInstance(ComponentInstanceRecord* inInstance) {
    return AUEffectBase::CreateEffectInstance(inInstance);
}

// Constructor
PolyphonicPitchShifter::PolyphonicPitchShifter(AudioUnit component) : AUEffectBase(component) {
    // Initialize parameters with defaults
    mPitchShift = kDefaultPitchShift;
    mMix = kDefaultMix;
    mFormant = kDefaultFormant;
    mLatencyMode = kDefaultLatency;
    
    // Initialize state
    mNeedsReset = true;
    mIsInitialized = false;
    mLatency = 0;
    mSampleRate = 44100.0;  // Default until Initialize is called
    mMaxFramesToProcess = 4096;  // Default size
    
    // Set AU's parameter ranges
    SetParameter(kParam_PitchShift, kAudioUnitScope_Global, 0, kDefaultPitchShift);
    SetParameter(kParam_Mix, kAudioUnitScope_Global, 0, kDefaultMix);
    SetParameter(kParam_Formant, kAudioUnitScope_Global, 0, kDefaultFormant);
    SetParameter(kParam_Latency, kAudioUnitScope_Global, 0, kDefaultLatency);
    
    // Reserve buffers with initial size
    mInputBufferL.resize(mMaxFramesToProcess);
    mInputBufferR.resize(mMaxFramesToProcess);
    mOutputBufferL.resize(mMaxFramesToProcess);
    mOutputBufferR.resize(mMaxFramesToProcess);
    
    mInputBuffers.resize(2);
    mOutputBuffers.resize(2);
    
    mInputBuffers[0] = mInputBufferL.data();
    mInputBuffers[1] = mInputBufferR.data();
    mOutputBuffers[0] = mOutputBufferL.data();
    mOutputBuffers[1] = mOutputBufferR.data();
}

// Destructor
PolyphonicPitchShifter::~PolyphonicPitchShifter() {
    // RubberBand stretcher is automatically cleaned up by unique_ptr
}

// Initialize the AU
OSStatus PolyphonicPitchShifter::Initialize() {
    OSStatus result = AUEffectBase::Initialize();
    if (result != noErr) return result;
    
    // Get info about the audio format we're working with
    mSampleRate = GetSampleRate();
    
    // Initialize the RubberBand stretcher
    initializeRubberBand();
    
    mIsInitialized = true;
    return noErr;
}

// Initialize or reinitialize the RubberBand stretcher
void PolyphonicPitchShifter::initializeRubberBand() {
    // Set up options for real-time processing
    int options = 
        RubberBand::RubberBandStretcher::OptionProcessRealTime | 
        RubberBand::RubberBandStretcher::OptionThreadingNever | 
        RubberBand::RubberBandStretcher::OptionPitchHighQuality;
    
    // Add formant preservation if needed
    if (mFormant > 50.0f) {
        options |= RubberBand::RubberBandStretcher::OptionFormantPreserved;
    } else {
        options |= RubberBand::RubberBandStretcher::OptionFormantShifted;
    }
    
    // Create a new stretcher instance
    int channels = 2; // Stereo processing
    mStretcher.reset(new RubberBand::RubberBandStretcher(
        mSampleRate, 
        channels, 
        options, 
        1.0, // Initial time ratio (no time stretch)
        pow(2.0, mPitchShift / 12.0) // Convert semitones to pitch scale factor
    ));
    
    // Configure for low latency operation - critical for guitar use
    if (mLatencyMode < 0.5f) {
        // Lowest latency mode
        mStretcher->setTransientsOption(RubberBand::RubberBandStretcher::OptionTransientsPreserve);
        mStretcher->setPhaseOption(RubberBand::RubberBandStretcher::OptionPhaseLaminar);
        mStretcher->setDetectorOption(RubberBand::RubberBandStretcher::OptionDetectorCompound);
    } else {
        // Higher quality mode
        mStretcher->setTransientsOption(RubberBand::RubberBandStretcher::OptionTransientsMixed);
        mStretcher->setPhaseOption(RubberBand::RubberBandStretcher::OptionPhaseLaminar);
        mStretcher->setDetectorOption(RubberBand::RubberBandStretcher::OptionDetectorCompound);
    }
    
    // Set pitch shift
    mStretcher->setPitchScale(pow(2.0, mPitchShift / 12.0)); 
    
    // Get and update latency
    mLatency = mStretcher->getLatency();
    
    // Reset the stretcher
    mStretcher->reset();
}

// Reset the stretcher when needed
void PolyphonicPitchShifter::resetRubberBand() {
    if (mStretcher) {
        mStretcher->reset();
    }
    mNeedsReset = false;
}

// Update parameters as they change
void PolyphonicPitchShifter::updateRubberBandParameters() {
    if (mStretcher) {
        // Update pitch scale (convert semitones to ratio)
        mStretcher->setPitchScale(pow(2.0, mPitchShift / 12.0));
        
        // If formant preservation changed significantly, we need to reinitialize
        if ((mFormant > 50.0f && !(mStretcher->getOptions() & RubberBand::RubberBandStretcher::OptionFormantPreserved)) ||
            (mFormant <= 50.0f && (mStretcher->getOptions() & RubberBand::RubberBandStretcher::OptionFormantPreserved))) {
            initializeRubberBand();
        }
    }
}

// Process audio
OSStatus PolyphonicPitchShifter::ProcessBufferLists(
    AudioUnitRenderActionFlags& ioActionFlags,
    const AudioBufferList& inBuffer,
    AudioBufferList& outBuffer,
    UInt32 inFramesToProcess) {
    
    // Lock during processing to prevent parameter changes mid-buffer
    std::lock_guard<std::mutex> lock(mProcessMutex);
    
    // Check if we're initialized
    if (!mIsInitialized) return kAudioUnitErr_Uninitialized;
    
    // Make sure our internal buffers are large enough
    if (inFramesToProcess > mMaxFramesToProcess) {
        mMaxFramesToProcess = inFramesToProcess;
        mInputBufferL.resize(mMaxFramesToProcess);
        mInputBufferR.resize(mMaxFramesToProcess);
        mOutputBufferL.resize(mMaxFramesToProcess * 2); // Extra room for output
        mOutputBufferR.resize(mMaxFramesToProcess * 2);
        
        mInputBuffers[0] = mInputBufferL.data();
        mInputBuffers[1] = mInputBufferR.data();
        mOutputBuffers[0] = mOutputBufferL.data();
        mOutputBuffers[1] = mOutputBufferR.data();
    }
    
    // Reset if needed
    if (mNeedsReset) {
        resetRubberBand();
    }
    
    // Get pointers to input and output audio data
    const float* inL = static_cast<const float*>(inBuffer.mBuffers[0].mData);
    const float* inR = (inBuffer.mNumberBuffers > 1) ? 
                       static_cast<const float*>(inBuffer.mBuffers[1].mData) : inL;
    
    float* outL = static_cast<float*>(outBuffer.mBuffers[0].mData);
    float* outR = (outBuffer.mNumberBuffers > 1) ? 
                  static_cast<float*>(outBuffer.mBuffers[1].mData) : outL;
    
    // If mix is 0, just copy input to output
    if (mMix <= 0.01f) {
        memcpy(outL, inL, inFramesToProcess * sizeof(float));
        if (outR != outL) {
            memcpy(outR, inR, inFramesToProcess * sizeof(float));
        }
        return noErr;
    }
    
    // Copy input to our intermediate buffers
    memcpy(mInputBuffers[0], inL, inFramesToProcess * sizeof(float));
    if (inR != inL) {
        memcpy(mInputBuffers[1], inR, inFramesToProcess * sizeof(float));
    } else {
        memcpy(mInputBuffers[1], inL, inFramesToProcess * sizeof(float));
    }
    
    // Process through RubberBand
    mStretcher->process(mInputBuffers.data(), inFramesToProcess, false);
    
    // Retrieve available output
    size_t available = mStretcher->available();
    if (available > 0) {
        mStretcher->retrieve(mOutputBuffers.data(), available);
        
        // Mix output with input based on mix parameter
        float mixLevel = mMix / 100.0f;
        float dryLevel = 1.0f - mixLevel;
        
        for (UInt32 i = 0; i < inFramesToProcess; ++i) {
            // Apply dry/wet mix - ensure we don't access beyond available processed frames
            if (i < available) {
                outL[i] = inL[i] * dryLevel + mOutputBuffers[0][i] * mixLevel;
                outR[i] = inR[i] * dryLevel + mOutputBuffers[1][i] * mixLevel;
            } else {
                // If we don't have enough processed frames, use dry signal
                outL[i] = inL[i];
                outR[i] = inR[i];
            }
        }
    } else {
        // No processed audio available, output dry signal
        memcpy(outL, inL, inFramesToProcess * sizeof(float));
        if (outR != outL) {
            memcpy(outR, inR, inFramesToProcess * sizeof(float));
        }
    }
    
    return noErr;
}

// Reset the effect
OSStatus PolyphonicPitchShifter::Reset(AudioUnitScope inScope, AudioUnitElement inElement) {
    mNeedsReset = true;
    return AUEffectBase::Reset(inScope, inElement);
}

// Get parameter string values
OSStatus PolyphonicPitchShifter::GetParameterValueStrings(AudioUnitScope inScope,
                                                       AudioUnitParameterID inParameterID,
                                                       CFArrayRef* outStrings) {
    if (inScope == kAudioUnitScope_Global) {
        if (inParameterID == kParam_Latency) {
            // Create array of strings for latency parameter
            CFStringRef strings[] = {
                CFSTR("Low Latency"),
                CFSTR("High Quality")
            };
            *outStrings = CFArrayCreate(NULL, (const void**)strings, 2, &kCFTypeArrayCallBacks);
            return noErr;
        }
    }
    return kAudioUnitErr_InvalidParameter;
}

// Get parameter info
OSStatus PolyphonicPitchShifter::GetParameterInfo(AudioUnitScope inScope,
                                               AudioUnitParameterID inParameterID,
                                               AudioUnitParameterInfo& outParameterInfo) {
    OSStatus result = noErr;
    
    if (inScope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidScope;
    
    outParameterInfo.flags = kAudioUnitParameterFlag_IsWritable 
                          | kAudioUnitParameterFlag_IsReadable;
    
    switch (inParameterID) {
        case kParam_PitchShift:
            AUBase::FillInParameterName(outParameterInfo, CFSTR("Pitch"), false);
            outParameterInfo.unit = kAudioUnitParameterUnit_RelativeSemiTones;
            outParameterInfo.minValue = kMinPitchShift;
            outParameterInfo.maxValue = kMaxPitchShift;
            outParameterInfo.defaultValue = kDefaultPitchShift;
            break;
            
        case kParam_Mix:
            AUBase::FillInParameterName(outParameterInfo, CFSTR("Mix"), false);
            outParameterInfo.unit = kAudioUnitParameterUnit_Percent;
            outParameterInfo.minValue = kMinMix;
            outParameterInfo.maxValue = kMaxMix;
            outParameterInfo.defaultValue = kDefaultMix;
            break;
            
        case kParam_Formant:
            AUBase::FillInParameterName(outParameterInfo, CFSTR("Formant"), false);
            outParameterInfo.unit = kAudioUnitParameterUnit_Percent;
            outParameterInfo.minValue = kMinFormant;
            outParameterInfo.maxValue = kMaxFormant;
            outParameterInfo.defaultValue = kDefaultFormant;
            break;
            
        case kParam_Latency:
            AUBase::FillInParameterName(outParameterInfo, CFSTR("Latency"), false);
            outParameterInfo.unit = kAudioUnitParameterUnit_Indexed;
            outParameterInfo.minValue = kMinLatency;
            outParameterInfo.maxValue = kMaxLatency;
            outParameterInfo.defaultValue = kDefaultLatency;
            break;
            
        default:
            result = kAudioUnitErr_InvalidParameter;
            break;
    }
    
    return result;
}

// Get property info
OSStatus PolyphonicPitchShifter::GetPropertyInfo(AudioUnitPropertyID inID,
                                             AudioUnitScope inScope,
                                             AudioUnitElement inElement,
                                             UInt32& outDataSize,
                                             Boolean& outWritable) {
    return AUEffectBase::GetPropertyInfo(inID, inScope, inElement, outDataSize, outWritable);
}

// Get property
OSStatus PolyphonicPitchShifter::GetProperty(AudioUnitPropertyID inID,
                                         AudioUnitScope inScope,
                                         AudioUnitElement inElement,
                                         void* outData) {
    return AUEffectBase::GetProperty(inID, inScope, inElement, outData);
}

// Set parameter
ComponentResult PolyphonicPitchShifter::SetParameter(AudioUnitParameterID inID,
                                                 AudioUnitScope inScope,
                                                 AudioUnitElement inElement,
                                                 Float32 inValue,
                                                 UInt32 inBufferOffsetInFrames) {
    ComponentResult result = AUEffectBase::SetParameter(inID, inScope, inElement, inValue, inBufferOffsetInFrames);
    
    if (result == noErr) {
        // Store parameter values locally
        switch (inID) {
            case kParam_PitchShift:
                mPitchShift = inValue;
                break;
                
            case kParam_Mix:
                mMix = inValue;
                break;
                
            case kParam_Formant:
                mFormant = inValue;
                break;
                
            case kParam_Latency:
                mLatencyMode = inValue;
                // Re-initialize if latency mode changes
                if (mIsInitialized) {
                    std::lock_guard<std::mutex> lock(mProcessMutex);
                    initializeRubberBand();
                }
                break;
        }
        
        // Update RubberBand parameters if needed
        if (mIsInitialized && (inID == kParam_PitchShift || inID == kParam_Formant)) {
            std::lock_guard<std::mutex> lock(mProcessMutex);
            updateRubberBandParameters();
        }
    }
    
    return result;
}