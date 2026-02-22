#ifndef STEAM_AUDIO_SERVER_H
#define STEAM_AUDIO_SERVER_H

#include "baked_reflections.hpp"
#include "godot_cpp/classes/object.hpp"
#include "godot_cpp/classes/ref.hpp"
#include "godot_cpp/classes/thread.hpp"
#include "listener.hpp"
#include "steam_audio.hpp"
#include <atomic>
#include <condition_variable>
#include <mutex>

using namespace godot;

class SteamAudioServer : public Object {
	GDCLASS(SteamAudioServer, Object)

private:
	static SteamAudioServer *self;
	GlobalSteamAudioState global_state{};
	std::vector<LocalSteamAudioState *> local_states;

	std::atomic<bool> is_global_state_init;
	std::atomic<bool> is_refl_thread_processing;
	std::atomic<bool> is_running;
	std::atomic<bool> local_states_have_changed;
	std::atomic<bool> scene_dirty;
	std::atomic<int> ticks_after_clear{ 0 }; // Skip fetching outputs briefly after clearing baked reflections
	std::mutex init_mux;
	std::mutex refl_mux;
	std::condition_variable cv;

	// meshes to add to the global state scene after it's initialized.
	std::vector<IPLStaticMesh> static_meshes_to_add;
	// meshes that have already been added to the scene (tracked for debugging)
	std::vector<IPLStaticMesh> registered_static_meshes;
	std::vector<IPLStaticMesh> dynamic_meshes_to_add;

	// TODO: allow for multiple
	SteamAudioListener *listener = nullptr;

	SteamAudioBakedReflections *baked_reflections = nullptr;

	// Handle to the currently loaded baked probe batch (if any).
	IPLProbeBatch loaded_probe_batch = nullptr;

	// Diagnostics: size of baked layers in the currently loaded probe batch.
	IPLsize loaded_reverb_bytes = 0;
	IPLsize loaded_static_bytes = 0;

	// Debug logging controls for reflections usage reporting.
	bool debug_log_refl_usage = true;
	int debug_log_frame_counter = 0;
	int debug_log_interval_frames = 60;

	// One-time log guard to avoid spamming when set_baked_reflections is called
	// before the global state has been initialized.
	std::atomic<bool> logged_baked_refl_deferral{ false };

	// Dedicated source used to query baked REVERB (listener-centric)
	IPLSource reverb_source = nullptr;
	// Cached reverb outputs retrieved from reverb_source each tick
	IPLReflectionEffectParams reverb_outputs_cache{ {} };

	void init_scene(IPLSceneSettings *scene_cfg);
	void start_refl_sim();
	void run_refl_sim();
	Ref<Thread> refl_thread; // Hold as Ref to match Godot's RefCounted lifecycle
	std::atomic<bool> refl_thread_started{ false };
	// Runtime toggle to disable the reflections worker thread for diagnostics.
	bool enable_refl_worker = true;

	bool is_tick_ready() const;
	bool is_state_active(LocalSteamAudioState *ls) const;
	bool is_within_refl_range(LocalSteamAudioState *ls) const;
	void commit_scene_if_idle();
	void process_direct_simulation();
	void fetch_reflection_outputs();
	void prepare_reflection_inputs();
	void prepare_reverb_inputs();
	void configure_reflection_shared_inputs();
	void wake_reflection_worker();
	void mark_scene_dirty();

protected:
	static void _bind_methods();

public:
	SteamAudioServer();
	~SteamAudioServer();

	static SteamAudioServer *get_singleton();
	GlobalSteamAudioState *get_global_state(bool should_init = true);

	void add_listener(SteamAudioListener *listener);
	void add_local_state(LocalSteamAudioState *ls);
	void remove_local_state(LocalSteamAudioState *ls);
	void add_static_mesh(IPLStaticMesh mesh);
	void remove_static_mesh(IPLStaticMesh mesh);
	void add_dynamic_mesh(IPLInstancedMesh mesh);
	void remove_dynamic_mesh(IPLInstancedMesh mesh);
	void set_baked_reflections(SteamAudioBakedReflections *baked_reflections);
	void clear_baked_reflections(SteamAudioBakedReflections *node);
	void notify_scene_dirty();

	void tick();

	// Debug helpers
	void debug_dump_scene_geometry();
};

#endif // STEAM_AUDIO_SERVER_H
