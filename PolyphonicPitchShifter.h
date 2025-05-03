#include "AudioUnitSDK/AUEffectBase.h" // Added missing closing quote
#include <rubberband/RubberBandStretcher.h>
#include <mutex>
#include <vector>
#include <memory>

#if AU_DEBUG_DISPATCHER
	#include "AUDebugDispatcher.h"
#endif

#ifndef __PolyphonicPitchShifter_h__
#define __PolyphonicPitchShifter_h__

// Custom parameters for our plugin
enum {
    kParam_PitchShift = 0,  // -12 to +12 semitones
    kParam_Mix,            // Dry/wet mix (0-100%)
    kParam_Formant,        // Formant preservation (0-100%)
    kParam_Latency,        // Latency mode (0=lowest possible, 1=higher quality)
    kNumberOfParameters
};

// Parameter ranges
const float kMinPitchShift = -12.0f;
const float kMaxPitchShift = 12.0f;
const float kDefaultPitchShift = 0.0f;

const float kMinMix = 0.0f;
const float kMaxMix = 100.0f;
const float kDefaultMix = 100.0f;

const float kMinFormant = 0.0f;
const float kMaxFormant = 100.0f;
const float kDefaultFormant = 0.0f;

const float kMinLatency = 0.0f;
const float kMaxLatency = 1.0f;
const float kDefaultLatency = 0.0f;

class PolyphonicPitchShifter : public AUEffectBase
{
public:
    PolyphonicPitchShifter(AudioUnit component);
    virtual ~PolyphonicPitchShifter() {}

    // Required AUEffectBase overrides
    virtual OSStatus Initialize();
    virtual void Cleanup();
    virtual OSStatus Reset(AudioUnitScope inScope, AudioUnitElement inElement);
    virtual OSStatus GetPropertyInfo(AudioUnitPropertyID inID,
                                     AudioUnitScope inScope,
                                     AudioUnitElement inElement,
                                     UInt32& outDataSize,
                                     Boolean& outWritable);
    virtual OSStatus GetProperty(AudioUnitPropertyID inID,
                                 AudioUnitScope inScope,
                                 AudioUnitElement inElement,
                                 void* outData);
    virtual OSStatus SetProperty(AudioUnitPropertyID inID,
                                 AudioUnitScope inScope,
                                 AudioUnitElement inElement,
                                 const void* inData,
                                 UInt32 inDataSize);
    virtual OSStatus GetParameterInfo(AudioUnitParameterID inID,
                                      AudioUnitParameterInfo& outParameterInfo);
    virtual OSStatus GetParameter(AudioUnitParameterID inID,
                                  AudioUnitScope inScope,
                                  AudioUnitElement inElement,
                                  Float32& outValue);
    virtual OSStatus SetParameter(AudioUnitParameterID inID,
                                        AudioUnitScope inScope,
                                        AudioUnitElement inElement,
                                        Float32 inValue,
                                        UInt32 inBufferOffsetInFrames);

    // Required factory method
    static OSStatus CreateEffectInstance(ComponentInstanceRecord* inInstance);

    // For Cocoa UI?
    virtual bool SupportsTail() { return true; }
    virtual Float64 GetTailTime() { return 0.5; }
    virtual Float64 GetLatency() { return mLatency; }

private:
    // RubberBand implementation
    void initializeRubberBand();
    void resetRubberBand();
    void updateRubberBandParameters();

    // Parameters
    float mPitchShift;
    float mMix;
    float mFormant;
    float mLatencyMode;

    // RubberBand stretcher
    std::unique_ptr<RubberBand::RubberBandStretcher> mStretcher;

    // Processing buffers
    std::vector<float> mInputBufferL;
    std::vector<float> mInputBufferR;
    std::vector<float> mOutputBufferL;
    std::vector<float> mOutputBufferR;

    std::vector<float*> mInputBuffers;
    std::vector<float*> mOutputBuffers;

    // States
    bool mNeedsReset;
    bool mIsInitialized;
    double mLatency;
    double mSampleRate;
    UInt32 mMaxFramesToProcess;
    std::mutex mProcessMutex;
};

#endif /* __PolyphonicPitchShifter_h__ */