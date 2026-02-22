#ifndef GODOT_STEAM_AUDIO_REFLECTION_BAKER_HPP
#define GODOT_STEAM_AUDIO_REFLECTION_BAKER_HPP

#include "baked_reflection_data.hpp"
#include "probe.hpp"

#include <phonon.h>
#include <atomic>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <mutex>
#include <thread>
#include <vector>

using namespace godot;

class SteamAudioBakedReflections : public Node3D {
	GDCLASS(SteamAudioBakedReflections, Node3D)

private:
	IPLProbeBatch probe_batch;
	std::atomic_bool is_baking;
	std::atomic<float> bake_progress;
	std::thread bake_thread;
	std::vector<uint8_t> baked_bytes_buffer;
	std::mutex baked_bytes_mutex;
	std::atomic<int> last_probe_count{ 0 };
	std::atomic<long long> last_baked_layer_size{ 0 };
	// Cached probe positions in this node's LOCAL space (used by editor gizmo).
	std::vector<Vector3> last_probe_positions_local;

	// config
	int num_rays;
	int num_bounces;
	float duration;
	int num_diffuse_samples;
	float irradiance_min_distance;
	bool bake_parametric;
	bool bake_convolution;
	// When true, also bake per-source STATICSOURCE data for sources marked static.
	bool bake_static_sources;
	// Radius used for STATICSOURCE endpoint influence during both baking and playback lookup.
	// This is the single authoritative value - the server reads this at runtime for baked data queries.
	float static_source_radius;

	// scene: (center/radius removed; probe generation uses node transform + extents)

	// probes
	float probe_spacing;
	float probe_height;
	Vector3 probe_generation_extents;
	// When true, include manually placed `SteamAudioProbe` nodes in the scene
	// as explicit probes in the generated probe batch.
	bool include_scene_probes;

	// Cached source positions collected on main thread for STATICSOURCE baking.
	std::vector<IPLVector3> static_source_positions;

protected:
	static void _bind_methods();

public:
	void _ready() override;
	void _notification(int p_what);

	Ref<SteamAudioBakedReflectionData> baked_data;

	SteamAudioBakedReflections();
	~SteamAudioBakedReflections();

	void generate_probes();

	// Returns probe positions in this node's local space, for gizmo drawing.
	PackedVector3Array get_probe_positions();

	void set_baked_data(const Ref<SteamAudioBakedReflectionData> &p_data);
	Ref<SteamAudioBakedReflectionData> get_baked_data() const;

	void start_bake();
	bool get_is_baking() const;
	float get_bake_progress() const;

	void set_num_rays(int p_num_rays);
	int get_num_rays() const;

	void set_num_bounces(int p_num_bounces);
	int get_num_bounces() const;

	void set_duration(float p_duration);
	float get_duration() const;

	void set_num_diffuse_samples(int p_num_diffuse_samples);
	int get_num_diffuse_samples() const;

	void set_irradiance_min_distance(float p_irradiance_min_distance);
	float get_irradiance_min_distance() const;

	void set_bake_parametric(bool p_bake_parametric);
	bool get_bake_parametric() const;

	void set_bake_convolution(bool p_bake_convolution);
	bool get_bake_convolution() const;

	void set_bake_static_sources(bool p_bake_static_sources);
	bool get_bake_static_sources() const;

	void set_static_source_radius(float p_radius);
	float get_static_source_radius() const;

	// center/radius removed

	void set_probe_spacing(float p_probe_spacing);
	float get_probe_spacing() const;

	void set_probe_height(float p_probe_height);
	float get_probe_height() const;

	void set_probe_generation_extents(Vector3 p_probe_generation_extents);
	Vector3 get_probe_generation_extents() const;

	void set_include_scene_probes(bool p_include);
	bool get_include_scene_probes() const;

	IPLReflectionsBakeParams get_bake_params() const;

	PackedStringArray _get_configuration_warnings() const override;

private:
	static void bake_progress_callback(float progress, void *user_data);
	void bake_task();
	void finalize_bake();

public:
	// Clears any in-memory baking state (probe batch, cached probe gizmo positions,
	// in-flight bake thread progress buffer). Does NOT modify the baked_data resource.
	// Use this before re-baking if you suspect stale probe batches or progress state.
	void clear();
};

#endif // GODOT_STEAM_AUDIO_REFLECTION_BAKER_HPP
