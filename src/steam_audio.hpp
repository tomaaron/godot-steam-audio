#ifndef STEAM_AUDIO_H
#define STEAM_AUDIO_H

#include "godot_cpp/variant/transform3d.hpp"
#include <phonon.h>
#include <godot_cpp/classes/audio_stream_player3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <shared_mutex>

using namespace godot;

VARIANT_ENUM_CAST(IPLAirAbsorptionModelType);

class SteamAudio {
public:
	typedef enum {
		log_debug,
		log_info,
		log_warn,
		log_error
	} GodotSteamAudioLogLevel;

	static void log(GodotSteamAudioLogLevel lvl, const String &str);
};

struct GlobalSteamAudioState {
	IPLScene scene;
	IPLAudioSettings audio_cfg;
	IPLContext ctx;
	IPLHRTF hrtf;
	IPLAmbisonicsEncodeEffect ambi_enc_effect;
	IPLAmbisonicsDecodeEffect ambi_dec_effect;
	IPLSimulationSettings sim_cfg;
	IPLSimulator sim;
	IPLCoordinateSpace3 listener_coords;
	std::mutex refl_ir_lock;
};

struct SteamAudioSource {
	AudioStreamPlayer3D *player = nullptr;
	IPLSource src;
};

struct SteamAudioSourceConfig {
	float occ_radius;
	int occ_samples;
	int transm_rays;
	float min_attn_dist;
	int ambisonics_order;
	float max_refl_dist;
	bool is_dist_attn_on;
	bool is_air_absorp_on;
	float air_absorption_low;
	float air_absorption_mid;
	float air_absorption_high;
	IPLAirAbsorptionModelType air_absorption_model_type;
	bool is_ambisonics_on;
	bool is_occlusion_on;
	bool is_reflection_on;
};

struct SteamAudioEffects {
	IPLDirectEffect direct;
	IPLReflectionEffect refl;
	// Dedicated effect instance for PARAMETRIC reflection output (reverb)
	IPLReflectionEffect refl_param;
	IPLAmbisonicsDecodeEffect dec;
	IPLAmbisonicsDecodeEffect refl_dec;
	IPLAmbisonicsEncodeEffect enc;
};

struct LocalSteamAudioBuffers {
	IPLAudioBuffer in;
	IPLAudioBuffer direct;
	IPLAudioBuffer mono;
	IPLAudioBuffer refl_ambi;
	IPLAudioBuffer refl_out;
	IPLAudioBuffer ambi;
	IPLAudioBuffer out;
};

struct LocalSteamAudioState {
	SteamAudioSource src;
	Vector3 dir_to_listener;
	IPLDirectEffectParams direct_outputs{ {} };
	IPLReflectionEffectParams refl_outputs{ {} };
	LocalSteamAudioBuffers bufs;
	SteamAudioEffects fx;
	SteamAudioSourceConfig cfg;
	// Smoothed gain used to scale the contribution of listener-centric baked REVERB
	// for this source. Driven from direct-simulation transmission (0..1).
	float reverb_send_gain = 1.0f;
	// Per-band smoothed gains (match IPL_NUM_BANDS) for frequency-aware scaling.
	float reverb_send_band_gain[IPL_NUM_BANDS] = { 1.0f };
	// True when reflections are coming from global baked reverb (needs transmission attenuation).
	// False for realtime reflections (no attenuation needed - already ray-traced).
	bool using_global_reverb = false;
	std::shared_mutex mux;
};

inline int ambisonic_channels_from(int order) {
	return (order + 1) * (order + 1);
}

// NOTE:
// We must convert Godot math types to Steam Audio (IPL) types explicitly.
// - Godot uses Vector3 with forward = -Z. Steam Audio expects plain float triplets
//   (IPLVector3) with no implicit handedness conversion. The numeric values are the
//   same, so this helper exists purely to avoid repeating boilerplate and to keep
//   call sites clear.
inline IPLVector3 ipl_vec3_from(Vector3 v) { return IPLVector3{ v.x, v.y, v.z }; }

// NOTE:
// Steam Audio needs an explicit coordinate frame (origin, right, up, ahead).
// Godot's basis columns are: X = right, Y = up, Z = forward (but forward is -Z in Godot).
// Therefore, we must negate the Z column to produce the "ahead" vector that Steam Audio
// expects. This small transform is required for correct spatialization and is not
// redundant with any Steam Audio API. Removing or skipping this will flip directions.
inline IPLCoordinateSpace3 ipl_coords_from(Transform3D trf) {
	auto orig = trf.origin;
	auto right = trf.get_basis().get_column(0);
	auto up = trf.get_basis().get_column(1);
	auto fwd = -trf.get_basis().get_column(2);

	IPLCoordinateSpace3 coords;
	coords.origin = ipl_vec3_from(orig);
	coords.right = ipl_vec3_from(right);
	coords.up = ipl_vec3_from(up);
	coords.ahead = ipl_vec3_from(fwd);

	return coords;
}

// Build an IPLMatrix4x4 from a Godot Transform3D.
// Context and layout notes:
// - IPLMatrix4x4 is documented as "row-major order" in phonon.h, but this refers to memory
//   layout (elements[row][col]). The mathematical convention is column-major (basis vectors
//   are columns), confirmed by Steam Audio passing matrices to Embree as COLUMN_MAJOR.
// - Godot's Transform3D conceptually has basis vectors as columns, but stores them transposed
//   internally (basis.rows[]) for performance (see godot-cpp basis.hpp:169).
// - Therefore, we can directly copy Godot's internal rows as IPL matrix columns (which are
//   stored row-major in memory but represent a column-major transform mathematically).
inline IPLMatrix4x4 ipl_matrix_from(const Transform3D &xf) {
	// Godot's basis.rows[i] is actually the i-th basis column vector stored for performance.
	// We copy these directly as columns in the IPL matrix.
	const Basis &b = xf.basis;
	const Vector3 &t = xf.origin;

	IPLMatrix4x4 M{};
	// Column 0: X-axis (right)
	M.elements[0][0] = b.rows[0][0];
	M.elements[1][0] = b.rows[0][1];
	M.elements[2][0] = b.rows[0][2];
	M.elements[3][0] = 0.f;
	// Column 1: Y-axis (up)
	M.elements[0][1] = b.rows[1][0];
	M.elements[1][1] = b.rows[1][1];
	M.elements[2][1] = b.rows[1][2];
	M.elements[3][1] = 0.f;
	// Column 2: Z-axis (forward)
	M.elements[0][2] = b.rows[2][0];
	M.elements[1][2] = b.rows[2][1];
	M.elements[2][2] = b.rows[2][2];
	M.elements[3][2] = 0.f;
	// Column 3: Translation
	M.elements[0][3] = t.x;
	M.elements[1][3] = t.y;
	M.elements[2][3] = t.z;
	M.elements[3][3] = 1.f;
	return M;
}

inline void handleErr(IPLerror err) {
	switch (err) {
		case IPL_STATUS_SUCCESS:
			return;
		case IPL_STATUS_FAILURE:
			SteamAudio::log(SteamAudio::log_error, "Unspecified error in init");
			return;
		case IPL_STATUS_OUTOFMEMORY:
			SteamAudio::log(SteamAudio::log_error, "Out of memory in init");
			return;
		case IPL_STATUS_INITIALIZATION:
			SteamAudio::log(SteamAudio::log_error, "Failed to handle external dependency in init");
			return;
	}
}

inline void log_callback(IPLLogLevel level, const char *message) {
	SteamAudio::GodotSteamAudioLogLevel godot_log_level;
	switch (level) {
		case IPL_LOGLEVEL_INFO:
			godot_log_level = SteamAudio::GodotSteamAudioLogLevel::log_info;
			break;
		case IPL_LOGLEVEL_WARNING:
			godot_log_level = SteamAudio::GodotSteamAudioLogLevel::log_warn;
			break;
		case IPL_LOGLEVEL_ERROR:
			godot_log_level = SteamAudio::GodotSteamAudioLogLevel::log_error;
			break;
		case IPL_LOGLEVEL_DEBUG:
			godot_log_level = SteamAudio::GodotSteamAudioLogLevel::log_debug;
			break;
	}
	SteamAudio::log(godot_log_level, String(message).strip_edges());
}

#endif
