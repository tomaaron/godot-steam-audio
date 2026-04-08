#include "server.hpp"
#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/project_settings.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/core/memory.hpp"
#include "godot_cpp/variant/callable_method_pointer.hpp"
#include "phonon.h"
#include "player.hpp"
#include "server_init.hpp"
#include "steam_audio.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <functional>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <thread>

void SteamAudioServer::tick() {
	// Advance debug frame counter for throttled logging.
	debug_log_frame_counter++;
	if (!is_tick_ready()) {
		return;
	}

	SteamAudio::log(SteamAudio::log_debug, "tick");

	commit_scene_if_idle();

	global_state.listener_coords =
			ipl_coords_from(listener->get_global_transform());

	process_direct_simulation();

	if (is_refl_thread_processing.load()) {
		SteamAudio::log(SteamAudio::log_debug, "tick: done, skipping reflections");
		return;
	}

	fetch_reflection_outputs();
	prepare_reflection_inputs();
	prepare_reverb_inputs();
	configure_reflection_shared_inputs();
	wake_reflection_worker();

	SteamAudio::log(SteamAudio::log_debug, "tick: done");
}

bool SteamAudioServer::is_tick_ready() const {
	if (Engine::get_singleton()->is_editor_hint()) {
		return false;
	}
	if (!is_global_state_init.load()) {
		return false;
	}
	std::lock_guard<std::mutex> tick_lock(self->tick_mux);
	if (listener == nullptr || !listener->is_inside_tree()) {
		return false;
	}
	return true;
}

bool SteamAudioServer::is_state_active(LocalSteamAudioState *ls) const {
	if (ls == nullptr) {
		return false;
	}
	if (ls->src.player == nullptr) {
		UtilityFunctions::push_warning(
				"local state has empty player, not updating simulation state");
		return false;
	}
	if (!ls->src.player->is_playing()) {
		return false;
	}
	return true;
}

bool SteamAudioServer::is_within_refl_range(LocalSteamAudioState *ls) const {
	return ls->src.player->get_global_position().distance_to(
				   listener->get_global_position()) <= ls->cfg.max_refl_dist;
}

void SteamAudioServer::commit_scene_if_idle() {
	if (!scene_dirty.load()) {
		return;
	}
	if (is_refl_thread_processing.load()) {
		SteamAudio::log(SteamAudio::log_debug,
				"tick: commit deferred, reflections busy");
		return;
	}
	iplSceneCommit(global_state.scene);
	scene_dirty.store(false);
	SteamAudio::log(SteamAudio::log_debug, "tick: committed scene");
}

void SteamAudioServer::process_direct_simulation() {
	for (auto ls : local_states) {
		if (!is_state_active(ls)) {
			continue;
		}

		Vector3 src_pos = ls->src.player->get_global_position();
		ls->dir_to_listener = src_pos - listener->get_global_position();

		IPLDistanceAttenuationModel attn_model{};
		attn_model.type = IPL_DISTANCEATTENUATIONTYPE_INVERSEDISTANCE;
		attn_model.minDistance = ls->cfg.min_attn_dist;

		IPLAirAbsorptionModel absorp_model{};
		absorp_model.type = ls->cfg.air_absorption_model_type;
		absorp_model.coefficients[0] = ls->cfg.air_absorption_low;
		absorp_model.coefficients[1] = ls->cfg.air_absorption_mid;
		absorp_model.coefficients[2] = ls->cfg.air_absorption_high;

		IPLCoordinateSpace3 src_coords;
		src_coords.ahead = IPLVector3{};
		src_coords.up = IPLVector3{};
		src_coords.right = IPLVector3{};
		src_coords.origin = ipl_vec3_from(src_pos);

		IPLSimulationInputs inputs{};
		inputs.flags = IPL_SIMULATIONFLAGS_DIRECT;
		inputs.distanceAttenuationModel = attn_model;
		inputs.airAbsorptionModel = absorp_model;
		inputs.source = src_coords;
		inputs.occlusionType = IPL_OCCLUSIONTYPE_VOLUMETRIC;
		inputs.occlusionRadius = ls->cfg.occ_radius;
		inputs.numOcclusionSamples = ls->cfg.occ_samples;
		inputs.numTransmissionRays = ls->cfg.transm_rays;

		if (ls->cfg.is_air_absorp_on) {
			inputs.directFlags = static_cast<IPLDirectSimulationFlags>(
					inputs.directFlags |
					IPL_DIRECTSIMULATIONFLAGS_AIRABSORPTION);
		}

		if (ls->cfg.is_dist_attn_on) {
			inputs.directFlags = static_cast<IPLDirectSimulationFlags>(
					inputs.directFlags |
					IPL_DIRECTSIMULATIONFLAGS_DISTANCEATTENUATION);
		}
		if (ls->cfg.is_occlusion_on) {
			inputs.directFlags = static_cast<IPLDirectSimulationFlags>(
					inputs.directFlags |
					IPL_DIRECTSIMULATIONFLAGS_OCCLUSION |
					IPL_DIRECTSIMULATIONFLAGS_TRANSMISSION);
		}

		SteamAudio::log(SteamAudio::log_debug, "tick: setting inputs");
		iplSourceSetInputs(ls->src.src, IPL_SIMULATIONFLAGS_DIRECT, &inputs);
	}
	SteamAudio::log(SteamAudio::log_debug, "tick: direct inputs set");

	IPLSimulationSharedInputs shared_inputs{};
	shared_inputs.listener = global_state.listener_coords;
	iplSimulatorSetSharedInputs(global_state.sim,
			IPL_SIMULATIONFLAGS_DIRECT, &shared_inputs);
	iplSimulatorRunDirect(global_state.sim);

	SteamAudio::log(SteamAudio::log_debug, "tick: direct sim complete");

	for (auto ls : local_states) {
		if (!is_state_active(ls)) {
			continue;
		}

		IPLSimulationOutputs outputs{};
		iplSourceGetOutputs(ls->src.src, IPL_SIMULATIONFLAGS_DIRECT, &outputs);
		ls->direct_outputs = outputs.direct;
	}
}

void SteamAudioServer::fetch_reflection_outputs() {
	global_state.refl_ir_lock.lock();

	const bool have_baked = (loaded_probe_batch != nullptr && loaded_reverb_bytes > 0);

	// After clearing baked reflections, skip fetching outputs briefly to let
	// Steam Audio's background thread run a simulation cycle with realtime settings.
	int ticks_remaining = ticks_after_clear.load();
	if (ticks_remaining > 0) {
		ticks_after_clear.store(ticks_remaining - 1);
		for (auto ls : local_states) {
			if (ls != nullptr) {
				ls->refl_outputs.type = IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
				ls->refl_outputs.ir = nullptr;
				ls->refl_outputs.irSize = 0;
			}
		}
		reverb_outputs_cache.type = IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
		reverb_outputs_cache.ir = nullptr;
		reverb_outputs_cache.irSize = 0;
		global_state.refl_ir_lock.unlock();
		return;
	}

	for (auto ls : local_states) {
		if (!is_state_active(ls)) {
			continue;
		}
		if (!is_within_refl_range(ls)) {
			continue;
		}

		// Fetch outputs from Steam Audio
		IPLSimulationOutputs outputs;
		iplSourceGetOutputs(ls->src.src, IPL_SIMULATIONFLAGS_REFLECTIONS, &outputs);
		ls->refl_outputs = outputs.reflections;
	}

	if (reverb_source != nullptr && have_baked) {
		IPLSimulationOutputs ro{};
		iplSourceGetOutputs(reverb_source, IPL_SIMULATIONFLAGS_REFLECTIONS, &ro);
		reverb_outputs_cache = ro.reflections;
	} else {
		// No baked data or no reverb source - clear the cache
		reverb_outputs_cache.type = IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
		reverb_outputs_cache.ir = nullptr;
		reverb_outputs_cache.irSize = 0;
	}
	global_state.refl_ir_lock.unlock();
}

void SteamAudioServer::prepare_reflection_inputs() {
	for (auto ls : local_states) {
		if (!is_state_active(ls)) {
			continue;
		}
		if (!is_within_refl_range(ls)) {
			continue;
		}

		auto player = dynamic_cast<SteamAudioPlayer *>(ls->src.player);
		if (player == nullptr || !player->is_reflection_on()) {
			continue;
		}

		Vector3 src_pos = ls->src.player->get_global_position();
		IPLCoordinateSpace3 src_coords;
		src_coords.ahead = IPLVector3{};
		src_coords.up = IPLVector3{};
		src_coords.right = IPLVector3{};
		src_coords.origin = ipl_vec3_from(src_pos);

		IPLSimulationInputs inputs{};
		inputs.flags = IPL_SIMULATIONFLAGS_REFLECTIONS;
		inputs.source = src_coords;

		int refl_mode = listener->get_reflection_mode();

		IPLBakedDataIdentifier baked_id{};
		baked_id.type = IPL_BAKEDDATATYPE_REFLECTIONS;
		baked_id.variation = IPL_BAKEDDATAVARIATION_REVERB;
		baked_id.endpointInfluence.center = IPLVector3{ 0, 0, 0 };
		baked_id.endpointInfluence.radius = 0.0f;

		const bool have_reverb = (loaded_probe_batch != nullptr && loaded_reverb_bytes > 0);
		const bool have_static = (loaded_probe_batch != nullptr);
		const bool is_static_src = (player && player->is_baked_static_source());

		bool use_global_reverb = false;
		if (refl_mode == 1) {
			inputs.baked = IPL_FALSE;
		} else if (refl_mode == 2) {
			if (have_reverb && reverb_source != nullptr) {
				use_global_reverb = true;
				inputs.baked = IPL_FALSE;
			} else if (have_static && is_static_src) {
				baked_id.variation = IPL_BAKEDDATAVARIATION_STATICSOURCE;
				baked_id.endpointInfluence.center = ipl_vec3_from(src_pos);
				baked_id.endpointInfluence.radius = baked_reflections ? baked_reflections->get_static_source_radius() : 1000.0f;
				inputs.baked = IPL_TRUE;
				inputs.bakedDataIdentifier = baked_id;
			} else {
				inputs.baked = IPL_FALSE;
			}
		} else {
			if (have_static && is_static_src) {
				baked_id.variation = IPL_BAKEDDATAVARIATION_STATICSOURCE;
				baked_id.endpointInfluence.center = ipl_vec3_from(src_pos);
				baked_id.endpointInfluence.radius = baked_reflections ? baked_reflections->get_static_source_radius() : 1000.0f;
				inputs.baked = IPL_TRUE;
				inputs.bakedDataIdentifier = baked_id;
			} else if (have_reverb && reverb_source != nullptr) {
				use_global_reverb = true;
				inputs.baked = IPL_FALSE;
			} else {
				inputs.baked = IPL_FALSE;
			}
		}

		// Track whether this source is using global baked reverb for stream.cpp
		ls->using_global_reverb = use_global_reverb;

		if (use_global_reverb) {
			// Populate per-band reverbScale from the direct sim transmission[] where available.
			// IPL defines IPL_NUM_BANDS bands; use per-band smoothing to avoid artifacts.
			const float rate_per_sec = 6.0f; // smoothing speed (tune as needed)
			const float dt = 1.0f / 60.0f; // approximate tick interval; replace with real delta if available
			const float gate_thresh = 0.001f; // smaller gate to avoid audible abrupt silence
			float overall_transmission = 1.0f;
			bool have_band_trans = false;
			for (int b = 0; b < IPL_NUM_BANDS; ++b) {
				float band_trans = 1.0f;
				// If transmission[] exists on direct_outputs, use it. Otherwise use occlusion scalar.
				band_trans = ls->direct_outputs.transmission[b];
				have_band_trans = true;
				// Smooth each band
				ls->reverb_send_band_gain[b] += (band_trans - ls->reverb_send_band_gain[b]) * std::min(1.0f, rate_per_sec * dt);
				inputs.reverbScale[b] = ls->reverb_send_band_gain[b];
				overall_transmission += ls->reverb_send_band_gain[b];
			}
			if (!have_band_trans) {
				// Fallback to occlusion scalar
				float transmission = 1.0f - ls->direct_outputs.occlusion;
				ls->reverb_send_gain += (transmission - ls->reverb_send_gain) * std::min(1.0f, rate_per_sec * dt);
				if (ls->reverb_send_gain <= gate_thresh) {
					// Zero the reverbScale to effectively gate baked reverb
					for (int b = 0; b < IPL_NUM_BANDS; ++b)
						inputs.reverbScale[b] = 0.0f;
				} else {
					for (int b = 0; b < IPL_NUM_BANDS; ++b)
						inputs.reverbScale[b] = ls->reverb_send_gain;
				}
			} else {
				// If all bands very small, treat as gated.
				overall_transmission /= float(IPL_NUM_BANDS);
				if (overall_transmission <= gate_thresh) {
					for (int b = 0; b < IPL_NUM_BANDS; ++b)
						inputs.reverbScale[b] = 0.0f;
				}
			}
			// Apply cached reverb IR; per-band scaling is provided in inputs.reverbScale.
			ls->refl_outputs = reverb_outputs_cache;
		}

		if (debug_log_refl_usage) {
			if ((debug_log_frame_counter % std::max(1, debug_log_interval_frames)) == 0) {
				String src_name = ls->src.player->get_name();
				const char *mode_str = (refl_mode == 2) ? "BAKED" : (refl_mode == 1 ? "REALTIME" : "AUTO");
				if (inputs.baked == IPL_TRUE) {
					if (baked_id.variation == IPL_BAKEDDATAVARIATION_STATICSOURCE) {
						SteamAudio::log(SteamAudio::log_debug, "(" + String(mode_str) + ") Reflections for source " + src_name + " [static=" + String(is_static_src ? "true" : "false") + "]: BAKED (STATICSOURCE), radius=" + String::num_real(baked_reflections ? baked_reflections->get_static_source_radius() : 1000.0f) + ", layer bytes=" + String::num_int64((int64_t)loaded_static_bytes));
					} else {
						SteamAudio::log(SteamAudio::log_debug, "(" + String(mode_str) + ") Reflections for source " + src_name + " [static=" + String(is_static_src ? "true" : "false") + "]: BAKED (REVERB) — via global reverb source, layer bytes=" + String::num_int64((int64_t)loaded_reverb_bytes));
					}
				} else {
					const char *reason = (use_global_reverb)
							? "global reverb in use"
							: ((loaded_probe_batch == nullptr)
											  ? "no probe batch"
											  : ((loaded_reverb_bytes <= 0 &&
														 loaded_static_bytes <= 0)
																? "no baked layers"
																: (refl_mode == 1
																				  ? "forced realtime"
																				  : (!is_static_src && have_static
																									? "source not marked static"
																									: "fallback"))));
					SteamAudio::log(SteamAudio::log_debug, "(" + String(mode_str) + ") Reflections for source " + src_name + " [static=" + String(is_static_src ? "true" : "false") + "]: REALTIME (" + String(reason) + ")");
				}
			}
		}

		iplSourceSetInputs(ls->src.src, IPL_SIMULATIONFLAGS_REFLECTIONS, &inputs);
	}
}

void SteamAudioServer::prepare_reverb_inputs() {
	if (reverb_source == nullptr) {
		return;
	}

	IPLSimulationInputs reverb_inputs{};
	reverb_inputs.flags = IPL_SIMULATIONFLAGS_REFLECTIONS;
	reverb_inputs.source = global_state.listener_coords;
	if (loaded_probe_batch != nullptr && loaded_reverb_bytes > 0) {
		reverb_inputs.baked = IPL_TRUE;
		IPLBakedDataIdentifier rid{};
		rid.type = IPL_BAKEDDATATYPE_REFLECTIONS;
		rid.variation = IPL_BAKEDDATAVARIATION_REVERB;
		rid.endpointInfluence.center = IPLVector3{ 0, 0, 0 };
		rid.endpointInfluence.radius = 0.0f;
		reverb_inputs.bakedDataIdentifier = rid;
	} else {
		reverb_inputs.baked = IPL_FALSE;
	}
	iplSourceSetInputs(reverb_source, IPL_SIMULATIONFLAGS_REFLECTIONS, &reverb_inputs);
}

void SteamAudioServer::configure_reflection_shared_inputs() {
	IPLSimulationSharedInputs shared_inputs{};
	shared_inputs.listener = global_state.listener_coords;
	shared_inputs.numRays = listener->get_num_refl_rays();
	shared_inputs.numBounces = listener->get_num_refl_bounces();
	shared_inputs.duration = listener->get_refl_duration();
	shared_inputs.order = listener->get_refl_ambisonics_order();
	shared_inputs.irradianceMinDistance = listener->get_irradiance_min_dist();
	iplSimulatorSetSharedInputs(global_state.sim, IPL_SIMULATIONFLAGS_REFLECTIONS,
			&shared_inputs);
}

void SteamAudioServer::wake_reflection_worker() {
	std::unique_lock<std::mutex> lock(refl_mux);
	is_refl_thread_processing.store(true);
	cv.notify_one();
}

void SteamAudioServer::mark_scene_dirty() {
	scene_dirty.store(true);
}

void SteamAudioServer::notify_scene_dirty() {
	mark_scene_dirty();
}

GlobalSteamAudioState *SteamAudioServer::get_global_state(bool should_init) {
	self->init_mux.lock();
	if (self->is_global_state_init.load()) {
		self->init_mux.unlock();
		return &self->global_state;
	}

	if (!should_init) {
		self->init_mux.unlock();
		return nullptr;
	}

	SteamAudio::log(SteamAudio::log_info, "Initializing SteamAudioServer global state");

	global_state.audio_cfg = create_audio_cfg();
	global_state.ctx = create_ctx();

	IPLSceneSettings scene_cfg = create_scene_cfg(global_state.ctx);
	IPLerror err = iplSceneCreate(global_state.ctx, &scene_cfg, &global_state.scene);
	handleErr(err);
	global_state.scene = iplSceneRetain(global_state.scene);
	for (auto m : static_meshes_to_add) {
		iplStaticMeshAdd(m, global_state.scene);
		registered_static_meshes.push_back(m);
	}
	// Commit the scene now so geometry is available for reflection simulation
	if (!static_meshes_to_add.empty()) {
		iplSceneCommit(global_state.scene);
		SteamAudio::log(SteamAudio::log_info, "Committed scene with " + String::num_int64(static_meshes_to_add.size()) + " static meshes.");
	}

	global_state.sim = create_simulator(
			global_state.ctx, global_state.audio_cfg, scene_cfg);
	global_state.hrtf = create_hrtf(global_state.ctx, global_state.audio_cfg);
	global_state.ambi_enc_effect = create_ambisonics_encode_effect(
			global_state.ctx, global_state.audio_cfg);
	global_state.ambi_dec_effect = create_ambisonics_decode_effect(
			global_state.ctx, global_state.audio_cfg, global_state.hrtf);

	iplSimulatorSetScene(global_state.sim, global_state.scene);
	iplSimulatorCommit(global_state.sim);

	is_global_state_init.store(true);
	init_mux.unlock();

	SteamAudio::log(SteamAudio::log_info, "Initialized SteamAudioServer global state");

	start_refl_sim();

	// Create dedicated reverb source (listener-centric) for baked REVERB queries.
	{
		IPLSourceSettings src_cfg{};
		src_cfg.flags = IPL_SIMULATIONFLAGS_REFLECTIONS;
		IPLerror st = iplSourceCreate(global_state.sim, &src_cfg, &reverb_source);
		handleErr(st);
		if (st == IPL_STATUS_SUCCESS && reverb_source != nullptr) {
			iplSourceAdd(reverb_source, global_state.sim);
			iplSimulatorCommit(global_state.sim);
			SteamAudio::log(SteamAudio::log_info, "Created dedicated reverb source for listener-centric baked REVERB.");
		} else {
			UtilityFunctions::push_warning("[Steam Audio] Failed to create dedicated reverb source; baked REVERB will be unavailable.");
		}
	}

	// If a baker node already provided baked data before init, load it now.
	if (baked_reflections && baked_reflections->baked_data.is_valid()) {
		SteamAudio::log(SteamAudio::log_info, "Global state ready; attempting to load baked reflections into simulator.");
		set_baked_reflections(baked_reflections);
	} else {
		// Attempt auto-discovery of a SteamAudioBakedReflections node in the current scene tree.
		SteamAudioBakedReflections *found_baker = nullptr;
		if (Engine::get_singleton()) {
			SceneTree *tree = Engine::get_singleton()->get_main_loop() ? Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop()) : nullptr;
			Window *root_window = tree ? tree->get_root() : nullptr;
			Node *root = root_window ? Object::cast_to<Node>(root_window) : nullptr;
			if (root) {
				// DFS to find first SteamAudioBakedReflections
				std::function<void(Node *)> dfs = [&](Node *n) {
					if (found_baker || !n)
						return;
					if (auto *baker_node = Object::cast_to<SteamAudioBakedReflections>(n)) {
						found_baker = baker_node;
						return;
					}
					const int cc = n->get_child_count();
					for (int i = 0; i < cc; ++i) {
						dfs(n->get_child(i));
						if (found_baker)
							return;
					}
				};
				dfs(root);
			}
		}

		if (found_baker) {
			SteamAudio::log(SteamAudio::log_info, "Found SteamAudioBakedReflections node in scene; attempting to load baked data.");
			set_baked_reflections(found_baker);
		} else {
			SteamAudio::log(SteamAudio::log_info, "Global state ready; no baked reflections resource assigned yet.");
			SteamAudio::log(SteamAudio::log_info, "Hint: Add a SteamAudioBakedReflections node to your scene and assign a SteamAudioBakedReflectionData resource to auto-load bakes on play.");
		}
	}
	return &global_state;
}

void SteamAudioServer::start_refl_sim() {
	if (!refl_thread.is_valid()) {
		refl_thread.instantiate();
		SteamAudio::log(SteamAudio::log_debug, "Created reflections Thread object " + String::num_int64(reinterpret_cast<int64_t>(refl_thread.ptr())));
	}
	if (!refl_thread_started.load() && refl_thread.is_valid()) {
		const bool in_editor = Engine::get_singleton() && Engine::get_singleton()->is_editor_hint();
		SteamAudio::log(SteamAudio::log_debug, "Starting reflections worker (in_editor=" + String(in_editor ? "true" : "false") + ") started_flag=" + String(refl_thread_started.load() ? "true" : "false") + ")");
		refl_thread->start(callable_mp(this, &SteamAudioServer::run_refl_sim));
		refl_thread_started.store(true);
	}
}

void SteamAudioServer::run_refl_sim() {
	SteamAudio::log(SteamAudio::log_debug, "run_refl_sim() ENTER (self=" + String::num_int64(reinterpret_cast<int64_t>(this)) + ")");
	while (this->is_running.load()) {
		{
			std::unique_lock<std::mutex> lock(this->refl_mux);
			cv.wait(lock, [&] { return is_refl_thread_processing.load() || !is_running.load(); });
		}
		// if someone removed a local state, then the reflection sim might crash, so
		// we need it to wait for another tick.
		// XXX: what happens if a local state is removed in the middle of a sim run...?
		if (local_states_have_changed.load()) {
			local_states_have_changed.store(false);
			is_refl_thread_processing.store(false);
			continue;
		}
		SteamAudio::log(SteamAudio::log_debug, "running reflection sim");
		iplSimulatorRunReflections(global_state.sim);
		is_refl_thread_processing.store(false);
	}
	SteamAudio::log(SteamAudio::log_debug, "run_refl_sim() EXIT (is_running=false)");
}

void SteamAudioServer::add_listener(SteamAudioListener *lis) {
	std::lock_guard<std::mutex> lock(self->tick_mux);
	self->listener = lis;
}

void SteamAudioServer::add_local_state(LocalSteamAudioState *ls) {
	std::lock_guard<std::mutex> lock(self->tick_mux);
	self->local_states.push_back(ls);
}

void SteamAudioServer::remove_local_state(LocalSteamAudioState *ls) {
	std::lock_guard<std::mutex> lock(self->tick_mux);
	auto it = std::find(local_states.begin(), local_states.end(), ls);
	if (it == local_states.end()) {
		return;
	}
	local_states.erase(it);
	local_states_have_changed.store(true);
}

void SteamAudioServer::add_static_mesh(IPLStaticMesh mesh) {
	if (is_global_state_init.load()) {
		iplStaticMeshAdd(mesh, global_state.scene);
		registered_static_meshes.push_back(mesh);
		mark_scene_dirty();
	} else {
		static_meshes_to_add.push_back(mesh);
	}
}

void SteamAudioServer::remove_static_mesh(IPLStaticMesh mesh) {
	if (is_global_state_init.load()) {
		iplStaticMeshRemove(mesh, global_state.scene);
		auto it2 = std::find(registered_static_meshes.begin(), registered_static_meshes.end(), mesh);
		if (it2 != registered_static_meshes.end()) {
			registered_static_meshes.erase(it2);
		}
		mark_scene_dirty();
	} else {
		// Probably won't happen?
		auto it = std::find(static_meshes_to_add.begin(), static_meshes_to_add.end(), mesh);
		if (it != static_meshes_to_add.end()) {
			static_meshes_to_add.erase(it);
		}
	}
}

void SteamAudioServer::add_dynamic_mesh(IPLInstancedMesh mesh) {
	if (is_global_state_init.load()) {
		iplInstancedMeshAdd(mesh, global_state.scene);
		mark_scene_dirty();
	} else {
		SteamAudio::log(SteamAudio::log_error, "Adding a dynamic mesh, but SteamAudio is not initialized. Probably crashing soon.");
	}
}

void SteamAudioServer::remove_dynamic_mesh(IPLInstancedMesh mesh) {
	if (!is_global_state_init.load()) {
		return; // We've probably already deleted the scene.
	}

	iplInstancedMeshRemove(mesh, global_state.scene);
	mark_scene_dirty();
}

void SteamAudioServer::clear_baked_reflections(SteamAudioBakedReflections *node) {
	// Only clear if this is the currently active baked reflections node
	if (node != nullptr && this->baked_reflections != node) {
		return;
	}

	this->baked_reflections = nullptr;

	if (!is_global_state_init.load()) {
		return;
	}

	// Wait for reflection sim to be idle
	int spins = 0;
	while (is_refl_thread_processing.load() && spins < 200) {
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
		++spins;
	}

	std::unique_lock<std::mutex> refl_lock(refl_mux);

	// Remove probe batch from simulator
	if (loaded_probe_batch != nullptr && global_state.sim != nullptr) {
		iplSimulatorRemoveProbeBatch(global_state.sim, loaded_probe_batch);
		iplSimulatorCommit(global_state.sim);
		iplProbeBatchRelease(&loaded_probe_batch);
		loaded_probe_batch = nullptr;
		loaded_reverb_bytes = 0;
		loaded_static_bytes = 0;
	}

	global_state.refl_ir_lock.lock();

	// Remove and re-add all sources to force Steam Audio to clear internal cached state
	std::vector<IPLSource> sources_to_readd;
	for (auto ls : local_states) {
		if (ls != nullptr && ls->src.src != nullptr) {
			sources_to_readd.push_back(ls->src.src);
			iplSourceRemove(ls->src.src, global_state.sim);
		}
	}
	if (reverb_source != nullptr) {
		sources_to_readd.push_back(reverb_source);
		iplSourceRemove(reverb_source, global_state.sim);
	}
	iplSimulatorCommit(global_state.sim);

	for (auto src : sources_to_readd) {
		iplSourceAdd(src, global_state.sim);
	}
	iplSimulatorCommit(global_state.sim);

	// Reset all local states and their effects
	for (auto ls : local_states) {
		if (ls == nullptr)
			continue;

		// Clear cached reflection outputs
		ls->refl_outputs.type = IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
		ls->refl_outputs.ir = nullptr;
		ls->refl_outputs.irSize = 0;
		ls->reverb_send_gain = 1.0f;
		for (int b = 0; b < IPL_NUM_BANDS; ++b) {
			ls->reverb_send_band_gain[b] = 1.0f;
		}
		ls->using_global_reverb = false;

		// Reset reflection effects to flush convolution buffers
		if (ls->fx.refl != nullptr) {
			iplReflectionEffectReset(ls->fx.refl);
		}
		if (ls->fx.refl_param != nullptr) {
			iplReflectionEffectReset(ls->fx.refl_param);
		}

		// Zero audio buffers to remove lingering reverb samples
		if (ls->bufs.refl_out.data != nullptr) {
			for (int ch = 0; ch < ls->bufs.refl_out.numChannels; ++ch) {
				if (ls->bufs.refl_out.data[ch] != nullptr) {
					memset(ls->bufs.refl_out.data[ch], 0, ls->bufs.refl_out.numSamples * sizeof(float));
				}
			}
		}
		if (ls->bufs.refl_ambi.data != nullptr) {
			for (int ch = 0; ch < ls->bufs.refl_ambi.numChannels; ++ch) {
				if (ls->bufs.refl_ambi.data[ch] != nullptr) {
					memset(ls->bufs.refl_ambi.data[ch], 0, ls->bufs.refl_ambi.numSamples * sizeof(float));
				}
			}
		}

		// Set source to realtime mode
		if (ls->src.src != nullptr) {
			IPLSimulationInputs inputs{};
			inputs.flags = IPL_SIMULATIONFLAGS_REFLECTIONS;
			Vector3 src_pos = ls->src.player ? ls->src.player->get_global_position() : Vector3();
			IPLCoordinateSpace3 src_coords{};
			src_coords.origin = ipl_vec3_from(src_pos);
			inputs.source = src_coords;
			inputs.baked = IPL_FALSE;
			iplSourceSetInputs(ls->src.src, IPL_SIMULATIONFLAGS_REFLECTIONS, &inputs);
		}
	}

	// Clear cached reverb outputs
	reverb_outputs_cache.type = IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
	reverb_outputs_cache.ir = nullptr;
	reverb_outputs_cache.irSize = 0;

	// Set reverb source to realtime mode
	if (reverb_source != nullptr) {
		IPLSimulationInputs reverb_inputs{};
		reverb_inputs.flags = IPL_SIMULATIONFLAGS_REFLECTIONS;
		reverb_inputs.source = global_state.listener_coords;
		reverb_inputs.baked = IPL_FALSE;
		iplSourceSetInputs(reverb_source, IPL_SIMULATIONFLAGS_REFLECTIONS, &reverb_inputs);
	}

	iplSimulatorCommit(global_state.sim);
	iplSceneCommit(global_state.scene);
	iplSimulatorSetScene(global_state.sim, global_state.scene);
	iplSimulatorCommit(global_state.sim);

	global_state.refl_ir_lock.unlock();

	// Skip fetching outputs briefly to let Steam Audio run a simulation cycle
	ticks_after_clear.store(5);
}

void SteamAudioServer::set_baked_reflections(SteamAudioBakedReflections *baked_reflections) {
	this->baked_reflections = baked_reflections;

	// If nullptr is passed, clear the current batch
	if (baked_reflections == nullptr) {
		if (is_global_state_init.load()) {
			clear_baked_reflections(nullptr);
		}
		return;
	}

	if (!is_global_state_init.load()) {
		// Defer loading baked data until global state is initialized to avoid null dereferences.
		// Log only once at info level to avoid spam during editor reloads.
		bool expected = false;
		if (logged_baked_refl_deferral.compare_exchange_strong(expected, true)) {
			SteamAudio::log(SteamAudio::log_info, "[godot-steam-audio] set_baked_reflections called before global state init; deferring load.");
		}
		return;
	}

	// Try to avoid races with the reflections simulation thread by waiting for it to be idle,
	// then preventing it from starting while we modify probe batches.
	int spins = 0;
	while (is_refl_thread_processing.load() && spins < 200) { // up to ~1s
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
		++spins;
	}
	std::unique_lock<std::mutex> refl_lock(refl_mux);

	// If a probe batch is already loaded, remove it first.
	if (loaded_probe_batch != nullptr && global_state.sim != nullptr) {
		SteamAudio::log(SteamAudio::log_info, "Removing previously loaded baked probe batch from simulator.");
		iplSimulatorRemoveProbeBatch(global_state.sim, loaded_probe_batch);
		iplSimulatorCommit(global_state.sim);
		iplProbeBatchRelease(&loaded_probe_batch);
		loaded_probe_batch = nullptr;
		loaded_reverb_bytes = 0;
		loaded_static_bytes = 0;
	}

	if (!(baked_reflections && baked_reflections->baked_data.is_valid())) {
		UtilityFunctions::push_warning("[Steam Audio] No baked reflections data assigned; simulator will use real-time reflections only.");
		return;
	}

	// Load probe batch from the serialized resource and add it to the simulator.
	IPLSerializedObject serializedObject = baked_reflections->baked_data->get_data();
	if (!serializedObject) {
		UtilityFunctions::push_error("[Steam Audio] Baked reflections resource returned an empty serialized object.");
		return;
	}

	const IPLsize bytes = iplSerializedObjectGetSize(serializedObject);
	SteamAudio::log(SteamAudio::log_info, "Loading baked reflections from resource (" + String::num_int64(bytes) + " bytes).");

	IPLerror status = iplProbeBatchLoad(global_state.ctx, serializedObject, &loaded_probe_batch);
	if (status != IPL_STATUS_SUCCESS || loaded_probe_batch == nullptr) {
		UtilityFunctions::push_error("[Steam Audio] Failed to load probe batch from baked data. Status: " + String::num_int64(status));
		iplSerializedObjectRelease(&serializedObject);
		loaded_probe_batch = nullptr;
		return;
	}

	// We no longer need the serialized object after loading.
	iplSerializedObjectRelease(&serializedObject);

	// Report probe count and attach to simulator.
	const int num_probes = iplProbeBatchGetNumProbes(loaded_probe_batch);
	SteamAudio::log(SteamAudio::log_info, "Baked probe batch loaded. Probes: " + String::num_int64(num_probes));

	// Ensure the probe batch is committed before adding to the simulator, mirroring Unity's flow.
	iplProbeBatchCommit(loaded_probe_batch);

	iplSimulatorAddProbeBatch(global_state.sim, loaded_probe_batch);
	iplSimulatorCommit(global_state.sim);
	SteamAudio::log(SteamAudio::log_info, "Baked reflections probe batch registered with simulator and committed.");

	// Compute and log baked layer sizes for diagnostics (REVERB and STATICSOURCE).
	IPLBakedDataIdentifier reverbId{};
	reverbId.type = IPL_BAKEDDATATYPE_REFLECTIONS;
	reverbId.variation = IPL_BAKEDDATAVARIATION_REVERB;
	reverbId.endpointInfluence.center = IPLVector3{ 0, 0, 0 };
	reverbId.endpointInfluence.radius = 0.0f;
	loaded_reverb_bytes = iplProbeBatchGetDataSize(loaded_probe_batch, &reverbId);
	SteamAudio::log(SteamAudio::log_info, "REVERB layer size in loaded probe batch: " + String::num_int64((int64_t)loaded_reverb_bytes) + " bytes");

	IPLBakedDataIdentifier staticId{};
	staticId.type = IPL_BAKEDDATATYPE_REFLECTIONS;
	staticId.variation = IPL_BAKEDDATAVARIATION_STATICSOURCE;
	staticId.endpointInfluence.center = IPLVector3{ 0, 0, 0 };
	staticId.endpointInfluence.radius = 0.0f;
	loaded_static_bytes = iplProbeBatchGetDataSize(loaded_probe_batch, &staticId);
	SteamAudio::log(SteamAudio::log_info, "STATICSOURCE layer size in loaded probe batch: " + String::num_int64((int64_t)loaded_static_bytes) + " bytes");
}

SteamAudioServer::SteamAudioServer() {
	self = this;
	is_global_state_init.store(false);
	is_refl_thread_processing.store(false);
	is_running.store(true);
	local_states_have_changed.store(false);
	scene_dirty.store(false);
	SteamAudio::log(SteamAudio::log_debug, "SteamAudioServer() constructed (this=" + String::num_int64(reinterpret_cast<int64_t>(this)) + ", in_editor=" + String(Engine::get_singleton() && Engine::get_singleton()->is_editor_hint() ? "true" : "false") + ")");
}

SteamAudioServer::~SteamAudioServer() {
	// Stop the reflections worker thread cleanly.
	is_running.store(false);
	// Wake the worker if it's blocked on the condition variable so it can exit.
	{
		std::unique_lock<std::mutex> lock(refl_mux);
		cv.notify_all();
	}
	if (refl_thread.is_valid()) {
		// Godot requires calling wait_to_finish() on any thread that was ever started,
		// even if it has already finished. Track this explicitly to avoid warnings.
		SteamAudio::log(SteamAudio::log_debug, "~SteamAudioServer: Thread ptr=" + String::num_int64(reinterpret_cast<int64_t>(refl_thread.ptr())) + ", started_flag=" + String(refl_thread_started.load() ? "true" : "false") + ", is_started()=" + String(refl_thread->is_started() ? "true" : "false") + ", is_alive()=" + String(refl_thread->is_alive() ? "true" : "false"));
		if (refl_thread_started.load()) {
			refl_thread->wait_to_finish();
		}
		refl_thread.unref();
	}

	if (!self->is_global_state_init.load()) {
		SteamAudio::log(SteamAudio::log_debug, "~SteamAudioServer: global state not initialized; skipping audio cleanup.");
		return;
	}
	SteamAudio::log(SteamAudio::log_debug, "destroying steam audio server");

	// Clean up dedicated reverb source.
	if (reverb_source != nullptr) {
		iplSourceRemove(reverb_source, global_state.sim);
		iplSimulatorCommit(global_state.sim);
		iplSourceRelease(&reverb_source);
		reverb_source = nullptr;
	}

	// Clean up any loaded baked probe batch from simulator.
	if (loaded_probe_batch != nullptr) {
		SteamAudio::log(SteamAudio::log_info, "Releasing baked probe batch from simulator during shutdown.");
		iplSimulatorRemoveProbeBatch(global_state.sim, loaded_probe_batch);
		iplSimulatorCommit(global_state.sim);
		iplProbeBatchRelease(&loaded_probe_batch);
		loaded_probe_batch = nullptr;
	}

	iplAmbisonicsDecodeEffectRelease(&self->global_state.ambi_dec_effect);
	iplAmbisonicsEncodeEffectRelease(&self->global_state.ambi_enc_effect);
	iplHRTFRelease(&self->global_state.hrtf);
	iplSimulatorRelease(&self->global_state.sim);
	iplSceneRelease(&self->global_state.scene);
	iplContextRelease(&self->global_state.ctx);
}

void SteamAudioServer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("tick"), &SteamAudioServer::tick);
	ClassDB::bind_static_method("SteamAudioServer", D_METHOD("get_singleton"),
			&SteamAudioServer::get_singleton);
}

SteamAudioServer *SteamAudioServer::get_singleton() {
	return self;
}

SteamAudioServer *SteamAudioServer::self = nullptr;

void SteamAudioServer::debug_dump_scene_geometry() {
	SteamAudio::log(SteamAudio::log_debug, "Global init:" + String(is_global_state_init.load() ? "true" : "false"));
	SteamAudio::log(SteamAudio::log_debug, "Context ptr:" + String::num_int64(reinterpret_cast<int64_t>(global_state.ctx)));
	SteamAudio::log(SteamAudio::log_debug, "Scene ptr:" + String::num_int64(reinterpret_cast<int64_t>(global_state.scene)));
	SteamAudio::log(SteamAudio::log_debug, "Simulator ptr:" + String::num_int64(reinterpret_cast<int64_t>(global_state.sim)));
	SteamAudio::log(SteamAudio::log_debug, "Queued static meshes:" + String::num_int64(static_meshes_to_add.size()));
	SteamAudio::log(SteamAudio::log_debug, "Registered static meshes:" + String::num_int64(registered_static_meshes.size()));
	SteamAudio::log(SteamAudio::log_debug, "Queued dynamic meshes:" + String::num_int64(dynamic_meshes_to_add.size()));
}
