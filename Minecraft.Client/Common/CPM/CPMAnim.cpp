#include "stdafx.h"
#include "CPMAnim.h"
#include "CPMModel.h"
#include <math.h>
#include <map>

#ifndef CPM_PI
#define CPM_PI 3.14159265358979323846
#endif

// com.tom.cpm.shared.parts.anim.TagType ordinals
enum CPMTagType
{
	CPM_TAG_END = 0,
	CPM_TAG_NEW_ANIM,
	CPM_TAG_NEW_TRIGGER,
	CPM_TAG_INIT_BUILTIN_TRIGGER,
	CPM_TAG_INIT_NAMED_TRIGGER,
	CPM_TAG_INIT_PARAMETER_TRIGGER,
	CPM_TAG_INIT_STAGED_TRIGGER,
	CPM_TAG_CONSTANT_FRAME_TIME_FLOAT,
	CPM_TAG_CONSTANT_FRAME_TIME_BOOLEAN,
	CPM_TAG_CUBES_TO_CHANNELS,
	CPM_TAG_CONTROL_INFO,
	CPM_TAG_GESTURE_BUTTON,
	CPM_TAG_PARAMETERS,
	CPM_TAG_INIT_STAGED_ANIM,
	CPM_TAG_COUNT
};

// SerializedTrigger flags
#define CPM_TRIG_LOOPING (1 << 1)

namespace
{
	int cpmFloor(double d)
	{
		int i = (int)d;
		return d < (double)i ? i - 1 : i;
	}

	float cpmLerp(float partial, float prev, float cur)
	{
		return prev + partial * (cur - prev);
	}

	float cpmTrigInt(float partial, float prev, float cur)
	{
		float v = (float)sin(partial * CPM_PI / 2);
		return prev + v * (cur - prev);
	}

	// com.tom.cpm.shared.util.RotationInterpolator. Stateful across a track's
	// frames: it unwraps successive angles so a track crossing 0/2pi does not
	// spin the long way round.
	class RotationUnwrap
	{
		double fullRot;
		bool hasPrev;
		double prevVal;
		double mul;

	public:
		RotationUnwrap() : fullRot(2 * CPM_PI), hasPrev(false), prevVal(0), mul(0) {}

		double apply(double value)
		{
			if (!hasPrev)
			{
				hasPrev = true;
				prevVal = value;
				return value;
			}
			double v1 = fabs(value - prevVal);
			double v2 = fabs(fullRot - prevVal + value);
			double v3 = fabs(fullRot + prevVal - value);
			prevVal = value;
			if (v1 < v2 && v1 < v3)
			{
				return value + mul;
			}
			else if (v1 > v2 && v2 < v3)
			{
				mul += fullRot;
				return value + mul;
			}
			else if (v1 > v3 && v2 > v3)
			{
				mul -= fullRot;
				return value + mul;
			}
			return value;
		}
	};

	bool isLoopType(int t)
	{
		return t == CPM_INT_POLY_LOOP || t == CPM_INT_LINEAR_LOOP || t == CPM_INT_TRIG_LOOP;
	}

	bool isSplineType(int t)
	{
		return t == CPM_INT_POLY_LOOP || t == CPM_INT_POLY_SINGLE;
	}
}

//////////////////////////////////////////////////////////////////////////
// CPMTrack
//////////////////////////////////////////////////////////////////////////

CPMTrack::CPMTrack() : type(CPM_INT_LINEAR_SINGLE), knot0(0)
{
}

// Natural cubic spline, matching the Apache commons-math3 SplineInterpolator
// that CPM vendors. Knots are consecutive integers, so every h is 1, but the
// solve is kept in the general form to stay faithful to the reference.
static void cpmBuildSpline(const std::vector<double> &x, const std::vector<double> &y,
                           std::vector<float> &oa, std::vector<float> &ob,
                           std::vector<float> &oc, std::vector<float> &od)
{
	int n = (int)x.size() - 1;
	if (n < 2) return;

	std::vector<double> h(n), mu(n), z(n + 1);
	for (int i = 0; i < n; i++) h[i] = x[i + 1] - x[i];

	mu[0] = 0.0;
	z[0] = 0.0;
	for (int i = 1; i < n; i++)
	{
		double g = 2.0 * (x[i + 1] - x[i - 1]) - h[i - 1] * mu[i - 1];
		mu[i] = h[i] / g;
		z[i] = (3.0 * (y[i + 1] * h[i - 1] - y[i] * (x[i + 1] - x[i - 1]) + y[i - 1] * h[i]) /
		        (h[i - 1] * h[i]) - h[i - 1] * z[i - 1]) / g;
	}

	std::vector<double> b(n), c(n + 1), d(n);
	z[n] = 0.0;
	c[n] = 0.0;
	for (int j = n - 1; j >= 0; j--)
	{
		c[j] = z[j] - mu[j] * c[j + 1];
		b[j] = (y[j + 1] - y[j]) / h[j] - h[j] * (c[j + 1] + 2.0 * c[j]) / 3.0;
		d[j] = (c[j + 1] - c[j]) / (3.0 * h[j]);
	}

	oa.resize(n); ob.resize(n); oc.resize(n); od.resize(n);
	for (int i = 0; i < n; i++)
	{
		oa[i] = (float)y[i];
		ob[i] = (float)b[i];
		oc[i] = (float)c[i];
		od[i] = (float)d[i];
	}
}

void CPMTrack::init(int interpType, const std::vector<float> &raw, bool rotation)
{
	type = interpType;
	values.clear();
	sa.clear(); sb.clear(); sc.clear(); sd.clear();
	if (raw.empty()) return;

	int frames = (int)raw.size();
	RotationUnwrap unwrap;

	if (isSplineType(type))
	{
		// The spline variants pad the knot range so the curve is well defined
		// at and beyond the endpoints; the padding differs between loop and
		// single, exactly as in PolynomialSpline(Loop)Interpolator.
		std::vector<double> xs, ys;
		if (type == CPM_INT_POLY_LOOP)
		{
			int cnt = frames + 5;
			knot0 = -2.0f;
			// The loop variant primes the unwrapper with two extra samples
			// before reading the real frames.
			if (rotation)
			{
				unwrap.apply(raw[0]);
				unwrap.apply(raw[frames - 1]);
			}
			xs.resize(cnt); ys.resize(cnt);
			for (int j = 0; j < cnt; j++)
			{
				xs[j] = j - 2;
				float v = raw[((j + frames - 2) % frames + frames) % frames];
				ys[j] = rotation ? unwrap.apply(v) : v;
			}
		}
		else
		{
			int cnt = frames + 2;
			knot0 = -1.0f;
			xs.resize(cnt); ys.resize(cnt);
			for (int j = 0; j < cnt; j++)
			{
				xs[j] = j - 1;
				int idx = j - 1;
				if (idx < 0) idx = 0;
				if (idx > frames - 1) idx = frames - 1;
				float v = raw[idx];
				ys[j] = rotation ? unwrap.apply(v) : v;
			}
		}
		cpmBuildSpline(xs, ys, sa, sb, sc, sd);
		values = raw;   // kept so empty() and the frame count stay meaningful
		return;
	}

	// Non-spline types just map each frame through the setup operator.
	values.resize(frames);
	if (isLoopType(type) && rotation)
	{
		unwrap.apply(raw[(frames - 2 + frames) % frames]);
		unwrap.apply(raw[(frames - 1 + frames) % frames]);
	}
	for (int i = 0; i < frames; i++)
		values[i] = rotation ? (float)unwrap.apply(raw[i]) : raw[i];
}

float CPMTrack::apply(float step) const
{
	int frames = (int)values.size();
	if (frames == 0) return 0.0f;

	// ConstantTimeFloatDriver scales normalised time by the frame count before
	// handing it to the interpolator, so the interpolators all work in frames.
	float operand = step * frames;

	if (isSplineType(type))
	{
		if (sa.empty()) return values[0];
		int i = cpmFloor(operand - knot0);
		if (i < 0) i = 0;
		if (i >= (int)sa.size()) i = (int)sa.size() - 1;
		float dx = operand - (knot0 + i);
		return sa[i] + dx * (sb[i] + dx * (sc[i] + dx * sd[i]));
	}

	if (type == CPM_INT_NO_INTERPOLATE)
	{
		int i = (int)operand;
		if (i < 0) i = -i;
		return values[i % frames];
	}

	if (isLoopType(type))
	{
		int frm = cpmFloor(operand);
		if (frm < 0) frm = -frm;
		float partial = (float)(operand - floor(operand));
		float a = values[frm % frames];
		float b = values[(frm + 1) % frames];
		return type == CPM_INT_TRIG_LOOP ? cpmTrigInt(partial, a, b) : cpmLerp(partial, a, b);
	}

	// Single (non-looping) linear / trigonometric
	double v = operand / frames * (frames - 1);
	int frm = cpmFloor(v);
	if (frm < 0) frm = -frm;
	float partial = (float)(v - floor(v));
	int i0 = frm < frames - 1 ? frm : frames - 1;
	int i1 = (frm + 1) < (frames - 1) ? (frm + 1) : (frames - 1);
	float a = values[i0];
	float b = values[i1];
	return type == CPM_INT_TRIG_SINGLE ? cpmTrigInt(partial, a, b) : cpmLerp(partial, a, b);
}

//////////////////////////////////////////////////////////////////////////
// CPMCubeChannels / CPMAnimation
//////////////////////////////////////////////////////////////////////////

CPMCubeChannels::CPMCubeChannels() : cubeId(0), additive(false), cube(NULL)
{
	for (int i = 0; i < CPM_CH_COUNT; i++) used[i] = false;
}

CPMAnimation::CPMAnimation() :
	triggerId(0), priority(0), duration(1), pose(-1), looping(true)
{
}

void CPMAnimation::apply(float step) const
{
	for (size_t i = 0; i < cubes.size(); i++)
	{
		const CPMCubeChannels &cc = cubes[i];
		CPMRenderedCube *c = cc.cube;
		if (c == NULL) continue;

		// Float3Driver only fires when at least one of its three components has
		// keyframes; a component without them contributes the cube's current
		// value (or 0 when additive), which is what getX/getY/getZ return.
		if (cc.used[CPM_CH_POS_X] || cc.used[CPM_CH_POS_Y] || cc.used[CPM_CH_POS_Z])
		{
			float x = cc.used[CPM_CH_POS_X] ? cc.tracks[CPM_CH_POS_X].apply(step) : (cc.additive ? 0 : c->pos.x);
			float y = cc.used[CPM_CH_POS_Y] ? cc.tracks[CPM_CH_POS_Y].apply(step) : (cc.additive ? 0 : c->pos.y);
			float z = cc.used[CPM_CH_POS_Z] ? cc.tracks[CPM_CH_POS_Z].apply(step) : (cc.additive ? 0 : c->pos.z);
			if (cc.additive) { c->pos.x += x; c->pos.y += y; c->pos.z += z; }
			else { c->pos.x = x; c->pos.y = y; c->pos.z = z; }
		}

		if (cc.used[CPM_CH_ROT_X] || cc.used[CPM_CH_ROT_Y] || cc.used[CPM_CH_ROT_Z])
		{
			float x = cc.used[CPM_CH_ROT_X] ? cc.tracks[CPM_CH_ROT_X].apply(step) : (cc.additive ? 0 : c->rotation.x);
			float y = cc.used[CPM_CH_ROT_Y] ? cc.tracks[CPM_CH_ROT_Y].apply(step) : (cc.additive ? 0 : c->rotation.y);
			float z = cc.used[CPM_CH_ROT_Z] ? cc.tracks[CPM_CH_ROT_Z].apply(step) : (cc.additive ? 0 : c->rotation.z);
			if (cc.additive) { c->rotation.x += x; c->rotation.y += y; c->rotation.z += z; }
			else { c->rotation.x = x; c->rotation.y = y; c->rotation.z = z; }
		}

		if (cc.used[CPM_CH_COLOR_R] || cc.used[CPM_CH_COLOR_G] || cc.used[CPM_CH_COLOR_B])
		{
			// setColor only takes effect on flat-coloured cubes.
			if (c->cube.texSize == 0)
			{
				int cur = c->color;
				float r = cc.used[CPM_CH_COLOR_R] ? cc.tracks[CPM_CH_COLOR_R].apply(step) : (float)((cur >> 16) & 0xff);
				float g = cc.used[CPM_CH_COLOR_G] ? cc.tracks[CPM_CH_COLOR_G].apply(step) : (float)((cur >> 8) & 0xff);
				float b = cc.used[CPM_CH_COLOR_B] ? cc.tracks[CPM_CH_COLOR_B].apply(step) : (float)(cur & 0xff);
				int ri = (int)r, gi = (int)g, bi = (int)b;
				if (ri < 0) ri = 0; if (ri > 255) ri = 255;
				if (gi < 0) gi = 0; if (gi > 255) gi = 255;
				if (bi < 0) bi = 0; if (bi > 255) bi = 255;
				c->color = (ri << 16) | (gi << 8) | bi;
			}
		}

		if (cc.used[CPM_CH_SCALE_X] || cc.used[CPM_CH_SCALE_Y] || cc.used[CPM_CH_SCALE_Z])
		{
			float x = cc.used[CPM_CH_SCALE_X] ? cc.tracks[CPM_CH_SCALE_X].apply(step) : (cc.additive ? 0 : c->renderScale.x);
			float y = cc.used[CPM_CH_SCALE_Y] ? cc.tracks[CPM_CH_SCALE_Y].apply(step) : (cc.additive ? 0 : c->renderScale.y);
			float z = cc.used[CPM_CH_SCALE_Z] ? cc.tracks[CPM_CH_SCALE_Z].apply(step) : (cc.additive ? 0 : c->renderScale.z);
			// A zero component means "leave this axis alone", per setRenderScale.
			if (cc.additive)
			{
				if (x != 0) c->renderScale.x *= x;
				if (y != 0) c->renderScale.y *= y;
				if (z != 0) c->renderScale.z *= z;
			}
			else
			{
				if (x != 0) c->renderScale.x = x;
				if (y != 0) c->renderScale.y = y;
				if (z != 0) c->renderScale.z = z;
			}
		}

		if (cc.used[CPM_CH_VISIBLE])
			c->display = cc.tracks[CPM_CH_VISIBLE].apply(step) > 0.5f;
	}
}

//////////////////////////////////////////////////////////////////////////
// CPMAnimationSet
//////////////////////////////////////////////////////////////////////////

namespace
{
	// Mirrors AnimLoaderState: triggers and animations are appended in stream
	// order, and INIT_*/CUBES_TO_CHANNELS/CONSTANT_* configure the most
	// recently created one.
	struct TriggerInfo
	{
		int pose;          // CPMPose, or -1 when this port cannot drive it
		bool looping;
		TriggerInfo() : pose(-1), looping(true) {}
	};

	// Maps a flat channel index back to the cube slot and channel kind that
	// CUBES_TO_CHANNELS implicitly created.
	struct ChannelRef
	{
		int cubeSlot;
		int kind;
	};
}

void CPMAnimationSet::load(CPMIn &in)
{
	std::vector<TriggerInfo> triggers;
	int curTrigger = -1;
	int curAnim = -1;

	// Per-animation channel index -> (cube slot, kind)
	std::vector<std::vector<ChannelRef> > animChannels;

	for (int guard = 0; guard < 65536; guard++)
	{
		int tag = in.readByte();
		if (in.fail()) return;

		std::vector<unsigned char> block;
		if (!in.readNextBlock(block)) return;

		if (tag == CPM_TAG_END) break;
		if (tag < 0 || tag >= CPM_TAG_COUNT) continue;   // newer tag, skip

		CPMIn b(block.empty() ? NULL : &block[0], (int)block.size());

		switch (tag)
		{
		case CPM_TAG_NEW_TRIGGER:
			triggers.push_back(TriggerInfo());
			curTrigger = (int)triggers.size() - 1;
			break;

		case CPM_TAG_INIT_BUILTIN_TRIGGER:
		{
			if (curTrigger < 0) break;
			int poseOrdinal = b.readByte();
			triggers[curTrigger].pose = poseOrdinal;
			triggers[curTrigger].looping = true;   // builtin pose triggers loop
			break;
		}

		case CPM_TAG_INIT_NAMED_TRIGGER:
		case CPM_TAG_INIT_PARAMETER_TRIGGER:
		case CPM_TAG_INIT_STAGED_TRIGGER:
			// Gesture, parameter and staged triggers need input or server state
			// this port does not have. The trigger stays with pose == -1 so its
			// animations are parsed but never played.
			break;

		case CPM_TAG_NEW_ANIM:
		{
			CPMAnimation a;
			a.triggerId = b.readVarInt();
			a.priority = b.readSignedVarInt();
			a.duration = b.readVarInt();
			if (a.duration <= 0) a.duration = 1;
			animations.push_back(a);
			animChannels.push_back(std::vector<ChannelRef>());
			curAnim = (int)animations.size() - 1;
			break;
		}

		case CPM_TAG_CUBES_TO_CHANNELS:
		{
			if (curAnim < 0) break;
			int cubeCount = b.readVarInt();
			int intChCount = b.readVarInt();
			int flags = b.read();
			if (b.fail() || cubeCount < 0 || cubeCount > 8192) break;
			bool additive = (flags & 1) != 0;

			CPMAnimation &an = animations[curAnim];
			std::vector<ChannelRef> &refs = animChannels[curAnim];

			for (int i = 0; i < cubeCount; i++)
			{
				int id = b.readVarInt();
				if (b.fail()) break;

				CPMCubeChannels cc;
				cc.cubeId = id;
				cc.additive = additive;
				int slot = (int)an.cubes.size();
				an.cubes.push_back(cc);

				// Channel order is fixed by the four Float3Driver.make calls
				// followed by the visibility channel.
				if (intChCount == 12)
				{
					for (int k = CPM_CH_POS_X; k <= CPM_CH_SCALE_Z; k++)
					{
						ChannelRef r;
						r.cubeSlot = slot;
						r.kind = k;
						refs.push_back(r);
					}
				}
				ChannelRef vr;
				vr.cubeSlot = slot;
				vr.kind = CPM_CH_VISIBLE;
				refs.push_back(vr);
			}
			break;
		}

		case CPM_TAG_CONSTANT_FRAME_TIME_FLOAT:
		{
			if (curAnim < 0) break;
			int intType = b.readByte();
			int frames = b.readVarInt();
			int compCount = b.readVarInt();
			if (b.fail() || frames < 0 || frames > 4096 || compCount < 0) break;

			CPMAnimation &an = animations[curAnim];
			std::vector<ChannelRef> &refs = animChannels[curAnim];

			for (int i = 0; i < compCount; i++)
			{
				int channelId = b.readVarInt();
				std::vector<float> f(frames);
				for (int j = 0; j < frames; j++) f[j] = b.readVarFloat();
				if (b.fail()) break;
				if (channelId < 0 || channelId >= (int)refs.size()) continue;

				ChannelRef &r = refs[channelId];
				CPMCubeChannels &cc = an.cubes[r.cubeSlot];
				bool rot = (r.kind >= CPM_CH_ROT_X && r.kind <= CPM_CH_ROT_Z);
				cc.tracks[r.kind].init(intType, f, rot);
				cc.used[r.kind] = frames > 0;
			}
			break;
		}

		case CPM_TAG_CONSTANT_FRAME_TIME_BOOLEAN:
		{
			if (curAnim < 0) break;
			int frames = b.readVarInt();
			int compCount = b.readVarInt();
			if (b.fail() || frames < 0 || frames > 4096 || compCount < 0) break;

			CPMAnimation &an = animations[curAnim];
			std::vector<ChannelRef> &refs = animChannels[curAnim];

			int byteCount = (frames + 7) / 8;
			for (int i = 0; i < compCount; i++)
			{
				int channelId = b.readVarInt();
				std::vector<unsigned char> bits(byteCount ? byteCount : 1);
				if (byteCount) b.readFully(&bits[0], byteCount);
				if (b.fail()) break;
				if (channelId < 0 || channelId >= (int)refs.size()) continue;

				std::vector<float> f(frames);
				for (int j = 0; j < frames; j++)
					f[j] = (bits[j / 8] & (1 << (j % 8))) != 0 ? 1.0f : 0.0f;

				ChannelRef &r = refs[channelId];
				CPMCubeChannels &cc = an.cubes[r.cubeSlot];
				// Boolean tracks step rather than interpolate.
				cc.tracks[r.kind].init(CPM_INT_NO_INTERPOLATE, f, false);
				cc.used[r.kind] = frames > 0;
			}
			break;
		}

		default:
			// CONTROL_INFO, GESTURE_BUTTON, PARAMETERS, INIT_STAGED_ANIM
			break;
		}
	}

	// Resolve each animation's trigger into a pose.
	for (size_t i = 0; i < animations.size(); i++)
	{
		int t = animations[i].triggerId;
		if (t >= 0 && t < (int)triggers.size())
		{
			animations[i].pose = triggers[t].pose;
			animations[i].looping = triggers[t].looping;
		}
	}
}

void CPMAnimationSet::bind(CPMModelDefinition *def)
{
	if (def == NULL) return;
	std::map<int, CPMRenderedCube *> byId;
	for (size_t i = 0; i < def->allCubes.size(); i++)
		byId[def->allCubes[i]->cube.id] = def->allCubes[i];

	for (size_t i = 0; i < animations.size(); i++)
	{
		for (size_t j = 0; j < animations[i].cubes.size(); j++)
		{
			CPMCubeChannels &cc = animations[i].cubes[j];
			std::map<int, CPMRenderedCube *>::iterator it = byId.find(cc.cubeId);
			cc.cube = (it == byId.end()) ? NULL : it->second;
		}
	}
}

void CPMAnimationSet::applyForPose(int pose, long long timeMillis) const
{
	// AnimationHandler sorts by priority ascending, so a higher priority
	// animation runs last and wins on any channel it writes.
	std::multimap<int, const CPMAnimation *> ordered;
	for (size_t i = 0; i < animations.size(); i++)
	{
		const CPMAnimation &a = animations[i];
		if (a.pose < 0) continue;
		if (a.pose != pose && a.pose != CPM_POSE_GLOBAL) continue;
		ordered.insert(std::make_pair(a.priority, &a));
	}

	std::multimap<int, const CPMAnimation *>::const_iterator it;
	for (it = ordered.begin(); it != ordered.end(); ++it)
	{
		const CPMAnimation *a = it->second;
		float step = (float)(timeMillis % a->duration) / (float)a->duration;
		a->apply(step);
	}
}
