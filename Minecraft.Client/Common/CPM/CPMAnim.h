#pragma once
// CPM - Custom Player Models
//
// Port of com.tom.cpm.shared.animation and com.tom.cpm.shared.parts.anim.
//
// What is ported: the ANIMATION_NEW part, pose-triggered animations, all seven
// interpolator types, and the four float3 cube drivers (position, rotation,
// colour, scale) plus the per-cube visibility driver.
//
// What is NOT ported: gestures, named/parameter/staged triggers, layer control
// and animated textures. Those need either a UI to drive them or server-synced
// parameters, neither of which exists in this port. Their blocks are parsed and
// discarded so the rest of the animation data still loads.

#include <vector>
#include "CPMIO.h"

class CPMModelDefinition;
class CPMRenderedCube;

// com.tom.cpm.shared.animation.VanillaPose ordinals. Only the ones this port
// can actually detect from LCE player state are named; the ordinal is the wire
// value so the gaps matter.
enum CPMPose
{
	CPM_POSE_CUSTOM = 0,
	CPM_POSE_STANDING,
	CPM_POSE_WALKING,
	CPM_POSE_RUNNING,
	CPM_POSE_SNEAKING,
	CPM_POSE_SWIMMING,
	CPM_POSE_FALLING,
	CPM_POSE_SLEEPING,
	CPM_POSE_RIDING,
	CPM_POSE_FLYING,
	CPM_POSE_DYING,
	CPM_POSE_SKULL_RENDER,
	CPM_POSE_GLOBAL,
	CPM_POSE_CREATIVE_FLYING,
	CPM_POSE_JUMPING = 17,
	CPM_POSE_SNEAK_WALK = 18
};

// com.tom.cpm.shared.animation.interpolator.InterpolatorType ordinals
enum CPMInterpType
{
	CPM_INT_POLY_LOOP = 0,
	CPM_INT_POLY_SINGLE,
	CPM_INT_LINEAR_LOOP,
	CPM_INT_LINEAR_SINGLE,
	CPM_INT_NO_INTERPOLATE,
	CPM_INT_TRIG_LOOP,
	CPM_INT_TRIG_SINGLE,
	CPM_INT_COUNT
};

// One keyframe track. Holds the frame values already passed through the
// channel's interpolator setup (rotation channels get angle unwrapping) and,
// for the spline types, the precomputed cubic coefficients.
class CPMTrack
{
public:
	int type;
	std::vector<float> values;

	// Spline state: knots run from knot0 upwards in unit steps.
	float knot0;
	std::vector<float> sa, sb, sc, sd;   // per-interval cubic coefficients

	CPMTrack();

	// `rotation` selects the RotationInterpolator setup used by ROT_* channels.
	void init(int interpType, const std::vector<float> &raw, bool rotation);
	float apply(float step) const;   // step is normalised time in [0,1)
	bool empty() const { return values.empty(); }
};

// The 13 channels a cube contributes, in the fixed order CUBES_TO_CHANNELS
// creates them.
enum CPMChannelKind
{
	CPM_CH_POS_X = 0, CPM_CH_POS_Y, CPM_CH_POS_Z,
	CPM_CH_ROT_X, CPM_CH_ROT_Y, CPM_CH_ROT_Z,
	CPM_CH_COLOR_R, CPM_CH_COLOR_G, CPM_CH_COLOR_B,
	CPM_CH_SCALE_X, CPM_CH_SCALE_Y, CPM_CH_SCALE_Z,
	CPM_CH_VISIBLE,
	CPM_CH_COUNT
};

class CPMCubeChannels
{
public:
	int cubeId;
	bool additive;
	bool used[CPM_CH_COUNT];
	CPMTrack tracks[CPM_CH_COUNT];

	// Resolved at apply time.
	CPMRenderedCube *cube;

	CPMCubeChannels();
};

class CPMAnimation
{
public:
	int triggerId;
	int priority;
	int duration;
	std::vector<CPMCubeChannels> cubes;

	// Filled in from the animation's trigger.
	int pose;              // CPMPose, or -1 if this port cannot drive it
	bool looping;

	CPMAnimation();

	// Runs every driver at normalised time `step` in [0,1).
	void apply(float step) const;
};

class CPMAnimationSet
{
public:
	std::vector<CPMAnimation> animations;

	// Parses an ANIMATION_NEW part payload. Never fails hard: anything it does
	// not understand is skipped, leaving the animations it did understand.
	void load(CPMIn &in);

	// Binds cube ids to nodes. Must run after the cube tree is built.
	void bind(CPMModelDefinition *def);

	// Applies every animation whose trigger matches `pose`, plus GLOBAL ones,
	// lowest priority first. `timeMillis` is a free-running clock.
	void applyForPose(int pose, long long timeMillis) const;

	bool empty() const { return animations.empty(); }
};
