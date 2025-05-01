#include <AudioUnit/AudioUnit.r>

#include "PolyphonicPitchShifterVersion.h"

// Note these are all AU plugin related resources

// Audio Unit Description
#define kAudioUnitResID_PolyphonicPitchShifter     1000

// Component description
#define RES_ID			kAudioUnitResID_PolyphonicPitchShifter
#define COMP_TYPE		kAudioUnitType_Effect
#define COMP_SUBTYPE	'ppsh'  // Custom subtype for our pitch shifter
#define COMP_MANUF		'AirW'  // Manufacturer code for AirWindows style

#define VERSION			kPolyphonicPitchShifterVersion
#define NAME			"AirwindowsClone: PolyphonicPitchShifter"
#define DESCRIPTION		"Polyphonic Pitch Shifter AudioUnit Plugin"
#define ENTRY_POINT		"PolyphonicPitchShifterEntry"

#include "AUResources.r"