#include "baked_reflections.hpp"
#include "server.hpp"

#include <functional>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "geometry.hpp"

#include "player.hpp"
#include "steam_audio.hpp"

using namespace godot;

// Note: Matrix helpers centralized in steam_audio.hpp (ipl_matrix_row_major_from /
// ipl_matrix_transposed_from) to avoid duplication and confusion about layout.

void SteamAudioBakedReflections::_ready() {
	SteamAudioServer::get_singleton()->set_baked_reflections(this);
}

void SteamAudioBakedReflections::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_EXIT_TREE:
			// Clear baked reflections when this node is removed from the scene tree
			if (SteamAudioServer::get_singleton()) {
				SteamAudioServer::get_singleton()->clear_baked_reflections(this);
			}
			break;
	}
}

void SteamAudioBakedReflections::_bind_methods() {
	// Bind methods
	ClassDB::bind_method(D_METHOD("set_baked_data", "data"), &SteamAudioBakedReflections::set_baked_data);
	ClassDB::bind_method(D_METHOD("get_baked_data"), &SteamAudioBakedReflections::get_baked_data);
	ClassDB::bind_method(D_METHOD("start_bake"), &SteamAudioBakedReflections::start_bake);
	ClassDB::bind_method(D_METHOD("clear"), &SteamAudioBakedReflections::clear);
	ClassDB::bind_method(D_METHOD("get_is_baking"), &SteamAudioBakedReflections::get_is_baking);
	ClassDB::bind_method(D_METHOD("get_bake_progress"), &SteamAudioBakedReflections::get_bake_progress);
	// finalize_bake is called via call_deferred from a background thread; bind it so Godot can resolve the method.
	ClassDB::bind_method(D_METHOD("finalize_bake"), &SteamAudioBakedReflections::finalize_bake);
	// Editor gizmo helper: return last generated probe positions (local space).
	ClassDB::bind_method(D_METHOD("get_probe_positions"), &SteamAudioBakedReflections::get_probe_positions);
	ClassDB::bind_method(D_METHOD("set_num_rays", "num_rays"), &SteamAudioBakedReflections::set_num_rays);
	ClassDB::bind_method(D_METHOD("get_num_rays"), &SteamAudioBakedReflections::get_num_rays);
	ClassDB::bind_method(D_METHOD("set_num_bounces", "num_bounces"), &SteamAudioBakedReflections::set_num_bounces);
	ClassDB::bind_method(D_METHOD("get_num_bounces"), &SteamAudioBakedReflections::get_num_bounces);
	ClassDB::bind_method(D_METHOD("set_duration", "duration"), &SteamAudioBakedReflections::set_duration);
	ClassDB::bind_method(D_METHOD("get_duration"), &SteamAudioBakedReflections::get_duration);
	ClassDB::bind_method(D_METHOD("set_num_diffuse_samples", "num_diffuse_samples"), &SteamAudioBakedReflections::set_num_diffuse_samples);
	ClassDB::bind_method(D_METHOD("get_num_diffuse_samples"), &SteamAudioBakedReflections::get_num_diffuse_samples);
	ClassDB::bind_method(D_METHOD("set_irradiance_min_distance", "irradiance_min_distance"), &SteamAudioBakedReflections::set_irradiance_min_distance);
	ClassDB::bind_method(D_METHOD("get_irradiance_min_distance"), &SteamAudioBakedReflections::get_irradiance_min_distance);
	ClassDB::bind_method(D_METHOD("set_bake_parametric", "bake_parametric"), &SteamAudioBakedReflections::set_bake_parametric);
	ClassDB::bind_method(D_METHOD("get_bake_parametric"), &SteamAudioBakedReflections::get_bake_parametric);
	ClassDB::bind_method(D_METHOD("set_bake_convolution", "bake_convolution"), &SteamAudioBakedReflections::set_bake_convolution);
	ClassDB::bind_method(D_METHOD("get_bake_convolution"), &SteamAudioBakedReflections::get_bake_convolution);
	// center/radius bindings removed
	ClassDB::bind_method(D_METHOD("set_probe_spacing", "probe_spacing"), &SteamAudioBakedReflections::set_probe_spacing);
	ClassDB::bind_method(D_METHOD("get_probe_spacing"), &SteamAudioBakedReflections::get_probe_spacing);
	ClassDB::bind_method(D_METHOD("set_probe_height", "probe_height"), &SteamAudioBakedReflections::set_probe_height);
	ClassDB::bind_method(D_METHOD("get_probe_height"), &SteamAudioBakedReflections::get_probe_height);
	ClassDB::bind_method(D_METHOD("set_probe_generation_extents", "probe_generation_extents"), &SteamAudioBakedReflections::set_probe_generation_extents);
	ClassDB::bind_method(D_METHOD("get_probe_generation_extents"), &SteamAudioBakedReflections::get_probe_generation_extents);

	ClassDB::bind_method(D_METHOD("set_include_scene_probes", "include_scene_probes"), &SteamAudioBakedReflections::set_include_scene_probes);
	ClassDB::bind_method(D_METHOD("get_include_scene_probes"), &SteamAudioBakedReflections::get_include_scene_probes);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "include_scene_probes"), "set_include_scene_probes", "get_include_scene_probes");

	// Add properties
	ADD_GROUP("Reflection Bake", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "num_rays"), "set_num_rays", "get_num_rays");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "num_bounces"), "set_num_bounces", "get_num_bounces");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "duration"), "set_duration", "get_duration");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "num_diffuse_samples"), "set_num_diffuse_samples", "get_num_diffuse_samples");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "irradiance_min_distance"), "set_irradiance_min_distance", "get_irradiance_min_distance");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bake_parametric"), "set_bake_parametric", "get_bake_parametric");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bake_convolution"), "set_bake_convolution", "get_bake_convolution");

	ADD_GROUP("Scene", "");
	/* center and radius removed: probe generation uses node transform + extents */

	ADD_GROUP("Probes", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "probe_spacing"), "set_probe_spacing", "get_probe_spacing");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "probe_height"), "set_probe_height", "get_probe_height");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "probe_generation_extents"), "set_probe_generation_extents", "get_probe_generation_extents");

	ADD_GROUP("Bake Data", "");
	// Expose the baked data resource so GDScript can assign/read it.
	// Use the actual registered class name for the resource type hint.
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "baked_data", PROPERTY_HINT_RESOURCE_TYPE, "SteamAudioBakedReflectionData"), "set_baked_data", "get_baked_data");

	// Options for STATICSOURCE baking
	ClassDB::bind_method(D_METHOD("set_bake_static_sources", "bake_static_sources"), &SteamAudioBakedReflections::set_bake_static_sources);
	ClassDB::bind_method(D_METHOD("get_bake_static_sources"), &SteamAudioBakedReflections::get_bake_static_sources);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bake_static_sources"), "set_bake_static_sources", "get_bake_static_sources");

	ClassDB::bind_method(D_METHOD("set_static_source_radius", "radius"), &SteamAudioBakedReflections::set_static_source_radius);
	ClassDB::bind_method(D_METHOD("get_static_source_radius"), &SteamAudioBakedReflections::get_static_source_radius);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "static_source_radius", PROPERTY_HINT_RANGE, "0,10000,1"), "set_static_source_radius", "get_static_source_radius");
}

SteamAudioBakedReflections::SteamAudioBakedReflections() {
	probe_batch = nullptr;
	is_baking = false;
	bake_progress = 0.0f;

	// Initialize properties from ReflectionBakeConfig
	num_rays = 32768;
	num_bounces = 64;
	duration = 2.0f;
	num_diffuse_samples = 1024;
	irradiance_min_distance = 1.0f;
	bake_parametric = true;
	bake_convolution = true;
	bake_static_sources = true; // default on for parity with Unity/UE workflows
	static_source_radius = 1000.0f; // match listener default

	// Sensible defaults for scene influence and probe generation to avoid empty bakes.
	// scene center/radius removed — use node transform and `probe_generation_extents` instead

	probe_spacing = 4.0f;
	probe_height = 1.5f;
	// Default extents (X,Z size and Y thickness) for uniform floor generation.
	probe_generation_extents = Vector3(20.0f, 1.0f, 20.0f);
	include_scene_probes = true; // sensible default: include manual probes
}

SteamAudioBakedReflections::~SteamAudioBakedReflections() {
	SteamAudio::log(SteamAudio::log_info, "SteamAudioBakedReflections destructor called for " + String::num_int64(reinterpret_cast<int64_t>(this)));

	// Notify server to clear the reference before destruction
	if (SteamAudioServer::get_singleton()) {
		SteamAudio::log(SteamAudio::log_info, "Notifying server to clear baked reflections...");
		SteamAudioServer::get_singleton()->clear_baked_reflections(this);
	} else {
		SteamAudio::log(SteamAudio::log_warn, "Server singleton is null, cannot clear baked reflections!");
	}

	// Ensure background bake thread is joined before destruction.
	if (bake_thread.joinable()) {
		bake_thread.join();
	}
	if (probe_batch) {
		iplProbeBatchRelease(&probe_batch);
	}

	SteamAudio::log(SteamAudio::log_info, "SteamAudioBakedReflections destructor completed.");
}

void SteamAudioBakedReflections::generate_probes() {
	// Recreate probe batch if it already exists.
	if (probe_batch) {
		iplProbeBatchRelease(&probe_batch);
		probe_batch = nullptr;
	}
	// Build transform from this node's global transform (Node3D).
	Transform3D xf = get_global_transform();
	// Apply user-configured probe_generation_extents as a scale on the local unit cube volume,
	// mirroring Unity's behavior where the GameObject's scale defines the generation volume.
	// This keeps existing extents property functional even if the node's scale is left at 1.
	xf.basis.scale(probe_generation_extents);
	// Build the transform matrix for Steam Audio's probe generation.
	// IPLMatrix4x4 uses column-major convention (basis vectors as columns) despite row-major
	// memory layout. Unity applies Z-flip transforms and copies directly; Unreal transposes
	// from its row-major FMatrix. Godot stores basis transposed internally, so we use the
	// corrected conversion that properly handles this.
	IPLMatrix4x4 xf_matrix = ipl_matrix_from(xf);

	IPLProbeGenerationParams probe_params{};
	probe_params.type = IPL_PROBEGENERATIONTYPE_UNIFORMFLOOR; // default strategy; can be exposed later
	probe_params.spacing = probe_spacing;
	probe_params.height = probe_height;
	probe_params.transform = xf_matrix;

	// Debug dump of transform and probe params to help diagnose 0-probe cases.
	{
		Vector3 pos = xf.origin;
		Vector3 c0 = xf.basis.get_column(0);
		Vector3 c1 = xf.basis.get_column(1);
		Vector3 c2 = xf.basis.get_column(2);
		SteamAudio::log(SteamAudio::log_debug, "Probe gen transform pos: " + String(pos));
		SteamAudio::log(SteamAudio::log_debug, "Basis columns X: " + String(c0) + " Y: " + String(c1) + " Z: " + String(c2));
		SteamAudio::log(SteamAudio::log_debug, "Probe params type=UNIFORMFLOOR spacing=" + String::num_real(probe_spacing) + " height=" + String::num_real(probe_height));
		// Print matrix rows (column-major transform in row-major storage)
		SteamAudio::log(SteamAudio::log_debug, "IPLMatrix4x4 (column-major transform):");
		for (int r = 0; r < 4; ++r) {
			SteamAudio::log(SteamAudio::log_debug, "  [" + String::num_real(xf_matrix.elements[r][0]) + ", " + String::num_real(xf_matrix.elements[r][1]) + ", " + String::num_real(xf_matrix.elements[r][2]) + ", " + String::num_real(xf_matrix.elements[r][3]) + "]");
		}
	}

	// Ensure global state is initialized before accessing context/scene.
	GlobalSteamAudioState *gs = SteamAudioServer::get_singleton()->get_global_state(true);
	if (!gs) {
		UtilityFunctions::push_error("[Steam Audio] Global state is not initialized; cannot generate probes.");
		return;
	}
	IPLContext context = gs->ctx;
	IPLScene scene = gs->scene;

	// If we're in the editor (most baking is initiated there), ensure the Steam Audio scene actually
	// has geometry by forcing all SteamAudioGeometry nodes to (re)create and register their meshes.
	// At runtime, SteamAudioGeometry registers itself, but in the editor it early-outs; this ensures
	// geometry is present for probe generation without requiring a separate export step.
	if (Engine::get_singleton() && Engine::get_singleton()->is_editor_hint()) {
		int forced_geo = 0;
		SceneTree *tree = get_tree();
		if (tree && tree->get_root()) {
			// Recursive traversal to find SteamAudioGeometry nodes.
			std::function<void(Node *)> visit = [&](Node *n) {
				if (!n)
					return;
				if (auto *geo = Object::cast_to<SteamAudioGeometry>(n)) {
					geo->recalculate(); // creates + registers geometry into the Steam Audio scene
					++forced_geo;
				}
				const int child_count = n->get_child_count();
				for (int i = 0; i < child_count; ++i) {
					visit(n->get_child(i));
				}
			};
			visit(tree->get_root());
		}
		SteamAudio::log(SteamAudio::log_debug, "Forced (re)registration of SteamAudioGeometry nodes: " + String::num_int64(forced_geo));
	}
	// Ensure the scene is committed before generating probes, matching Unity flow.
	if (scene) {
		iplSceneCommit(scene);
	}

	// Dump scene geometry state for debugging
	SteamAudioServer::get_singleton()->debug_dump_scene_geometry();

	IPLProbeArray probe_array = nullptr;
	iplProbeArrayCreate(context, &probe_array);
	iplProbeArrayGenerateProbes(probe_array, scene, &probe_params);
	// Log probe array count before committing to batch
	int arr_count = 0;
	if (probe_array) {
		arr_count = iplProbeArrayGetNumProbes(probe_array);
	}
	SteamAudio::log(SteamAudio::log_debug, "ProbeArray count: " + String::num_int64(arr_count));

	// Fallback: if UniformFloor yields 0 probes, try Centroid generation in same volume.
	// Note: This SDK does not expose IPL_PROBEGENERATIONTYPE_UNIFORM; available types are
	// CENTROID and UNIFORMFLOOR. We therefore fall back to CENTROID when floor placement fails.
	if (arr_count == 0 && probe_params.type == IPL_PROBEGENERATIONTYPE_UNIFORMFLOOR) {
		// Match expected diagnostic phrasing exactly for easier troubleshooting.
		UtilityFunctions::push_warning("Uniformfloor generated 0 probes: retrying with Centroid strategy");
		iplProbeArrayRelease(&probe_array);
		probe_array = nullptr;
		iplProbeArrayCreate(context, &probe_array);
		IPLProbeGenerationParams fallback_params = probe_params;
		fallback_params.type = IPL_PROBEGENERATIONTYPE_CENTROID;
		iplProbeArrayGenerateProbes(probe_array, scene, &fallback_params);
		if (probe_array) {
			arr_count = iplProbeArrayGetNumProbes(probe_array);
		} else {
			arr_count = 0;
		}
		SteamAudio::log(SteamAudio::log_debug, "ProbeArray count after Centroid fallback: " + String::num_int64(arr_count));
		// If fallback still yields 0, abort early with a clear message and avoid creating an empty batch
		// unless the user has manually placed scene probes and opted to include them.
		if (arr_count == 0 && !include_scene_probes) {
			UtilityFunctions::push_warning("[Steam Audio] Centroid generated 0 probes as well; aborting probe generation.");
			last_probe_positions_local.clear();
			last_probe_count = 0;
			if (probe_array) {
				iplProbeArrayRelease(&probe_array);
				probe_array = nullptr;
			}
			return;
		}
		if (arr_count == 0 && include_scene_probes) {
			SteamAudio::log(SteamAudio::log_info, "No generated probes found but manual scene probes are enabled; continuing to include manual probes.");
		}
	}

	// Cache probe positions in local space for gizmo before moving into batch
	last_probe_positions_local.clear();
	if (probe_array && arr_count > 0) {
		// Convert from world to local using inverse global transform
		Transform3D inv = get_global_transform().affine_inverse();
		last_probe_positions_local.reserve(arr_count);
		for (int i = 0; i < arr_count; ++i) {
			IPLSphere s = iplProbeArrayGetProbe(probe_array, i);
			Vector3 world_pos(s.center.x, s.center.y, s.center.z);
			Vector3 local_pos = inv.xform(world_pos);
			last_probe_positions_local.push_back(local_pos);
		}
	}

	iplProbeBatchCreate(context, &probe_batch);
	if (probe_array) {
		iplProbeBatchAddProbeArray(probe_batch, probe_array);
	}

	// If configured, include manually placed SteamAudioProbe nodes as explicit probes
	if (include_scene_probes) {
		SceneTree *tree = get_tree();
		Window *root_window = tree ? tree->get_root() : nullptr;
		Node *root = root_window ? Object::cast_to<Node>(root_window) : nullptr;
		int manual_found = 0;
		if (root) {
			std::function<void(Node *)> visit = [&](Node *n) {
				if (!n)
					return;
				if (auto *sap = Object::cast_to<SteamAudioProbe>(n)) {
					// Convert probe global position to IPLSphere and add
					Vector3 p = sap->get_global_position();
					IPLSphere sph{};
					sph.center = IPLVector3{ p.x, p.y, p.z };
					sph.radius = sap->get_radius();
					iplProbeBatchAddProbe(probe_batch, sph);
					// Also cache local position for gizmo
					Transform3D inv = get_global_transform().affine_inverse();
					Vector3 local_pos = inv.xform(p);
					last_probe_positions_local.push_back(local_pos);
					++manual_found;
					SteamAudio::log(SteamAudio::log_debug, "Manual probe found at: " + String(p) + " radius=" + String::num_real(sap->get_radius()));
				}
				const int cc = n->get_child_count();
				for (int i = 0; i < cc; ++i)
					visit(n->get_child(i));
			};
			visit(root);
			SteamAudio::log(SteamAudio::log_debug, "Manual probes collected: " + String::num_int64(manual_found));
		}
	}

	iplProbeBatchCommit(probe_batch);

	// Debug: report number of probes generated to help diagnose empty/degenerate bakes.
	int num_probes = iplProbeBatchGetNumProbes(probe_batch);
	last_probe_count = num_probes;
	SteamAudio::log(SteamAudio::log_info, "Generated probes: " + String::num_int64(num_probes));
	if (num_probes <= 1) {
		UtilityFunctions::push_warning("[Steam Audio] Probe generation produced insufficient probes (" + String::num_int64(num_probes) + "). Aborting bake.\n"
																																		 "Tips: (1) Increase probe_generation_extents or decrease probe_spacing, (2) ensure geometry is registered and the Steam Audio scene is committed, (3) place the baker over walkable areas.");
		// Prevent downstream bake from starting by clearing the batch and cached gizmo data.
		last_probe_positions_local.clear();
		last_probe_count = 0;
		iplProbeBatchRelease(&probe_batch);
		probe_batch = nullptr;
	}

	// Release temporary probe array
	if (probe_array) {
		iplProbeArrayRelease(&probe_array);
	}

	// Notify editor to update gizmo visualization
	if (Engine::get_singleton() && Engine::get_singleton()->is_editor_hint()) {
		update_gizmos();
	}
}

PackedVector3Array SteamAudioBakedReflections::get_probe_positions() {
	PackedVector3Array arr;
	if (last_probe_positions_local.empty()) {
		return arr;
	}
	arr.resize(static_cast<int>(last_probe_positions_local.size()));
	for (int i = 0; i < (int)last_probe_positions_local.size(); ++i) {
		arr.set(i, last_probe_positions_local[i]);
	}
	return arr;
}

void SteamAudioBakedReflections::set_baked_data(const Ref<SteamAudioBakedReflectionData> &p_data) {
	baked_data = p_data;
}

Ref<SteamAudioBakedReflectionData> SteamAudioBakedReflections::get_baked_data() const {
	return baked_data;
}

IPLReflectionsBakeParams SteamAudioBakedReflections::get_bake_params() const {
	IPLBakedDataIdentifier identifier{};
	identifier.type = IPL_BAKEDDATATYPE_REFLECTIONS;
	// Use REVERB as the default baked reflections variation. This matches common usage
	// and is robust for runtime lookup by the simulator. Other variations can be exposed later.
	identifier.variation = IPL_BAKEDDATAVARIATION_REVERB;
	// For REVERB, endpointInfluence is not used.
	identifier.endpointInfluence.center = IPLVector3{ 0, 0, 0 };
	identifier.endpointInfluence.radius = 0.0f;

	IPLReflectionsBakeParams params{};
	// Ensure global scene is available.
	GlobalSteamAudioState *gs = SteamAudioServer::get_singleton()->get_global_state(true);
	if (!gs) {
		UtilityFunctions::push_error("[Steam Audio] Global state is not initialized; cannot build bake params.");
		return IPLReflectionsBakeParams{};
	}
	params.scene = gs->scene;
	params.probeBatch = probe_batch; // provide probes to bake into

	params.sceneType = IPL_SCENETYPE_DEFAULT;
	params.identifier = identifier;
	params.savedDuration = duration;
	// Also set simulatedDuration; some SDK versions require this to generate data.
	params.simulatedDuration = duration;
	params.order = 2;
	params.numThreads = 8;

	params.numRays = num_rays;
	params.numBounces = num_bounces;
	params.numDiffuseSamples = num_diffuse_samples;
	params.irradianceMinDistance = irradiance_min_distance;
	if (bake_parametric) {
		params.bakeFlags = static_cast<IPLReflectionsBakeFlags>(
				static_cast<int>(params.bakeFlags) | IPL_REFLECTIONSBAKEFLAGS_BAKEPARAMETRIC);
	}
	if (bake_convolution) {
		params.bakeFlags = static_cast<IPLReflectionsBakeFlags>(
				static_cast<int>(params.bakeFlags) | IPL_REFLECTIONSBAKEFLAGS_BAKECONVOLUTION);
	}
	return params;
}

void SteamAudioBakedReflections::start_bake() {
	if (is_baking) {
		return;
	}

	is_baking = true;
	bake_progress = 0.0f;

	// Clear any previously stored baked bytes to avoid mistakenly finalizing
	// with stale data from an earlier bake (can happen across editor play/stop).
	{
		std::lock_guard<std::mutex> lock(baked_bytes_mutex);
		baked_bytes_buffer.clear();
	}

	// IMPORTANT: Generate probes on the main thread because this touches the scene tree (find_children).
	generate_probes();

	// Guard: don't start a bake if no probes were generated or probe_batch is invalid.
	int probe_count = 0;
	if (probe_batch) {
		probe_count = iplProbeBatchGetNumProbes(probe_batch);
	}
	if (!probe_batch || probe_count <= 1) {
		UtilityFunctions::push_warning("[Steam Audio] Aborting reflections bake: no probes found in the generation volume.\n"
									   "Ensure: (1) your Steam Audio scene/geometry is exported and committed, (2) this node's transform/scale encloses walkable areas, and (3) probe_spacing/height are reasonable.\n"
									   "Hint: You currently have " +
				String::num_int64(probe_count) + " probe(s); at least 2 are required.");
		// Reset baking state to indicate failure without hanging the UI.
		is_baking = false;
		bake_progress = 0.0f;
		return;
	}

	// Collect static source positions on the main thread if enabled.
	static_source_positions.clear();
	if (bake_static_sources) {
		SceneTree *tree = get_tree();
		Window *root_window = tree ? tree->get_root() : nullptr;
		Node *root = root_window ? Object::cast_to<Node>(root_window) : nullptr;
		int found = 0;
		if (root) {
			std::function<void(Node *)> dfs = [&](Node *n) {
				if (!n)
					return;
				if (auto *sap = Object::cast_to<SteamAudioPlayer>(n)) {
					if (sap->is_baked_static_source()) {
						Vector3 p = sap->get_global_position();
						static_source_positions.push_back(IPLVector3{ p.x, p.y, p.z });
						++found;
						// Log each collected static source position for diagnostics.
						SteamAudio::log(SteamAudio::log_debug, "Static source found at " + String(p));
					}
				}
				const int cc = n->get_child_count();
				for (int i = 0; i < cc; ++i)
					dfs(n->get_child(i));
			};
			dfs(root);
		}
		SteamAudio::log(SteamAudio::log_info, "Static sources to bake (STATICSOURCE): " + String::num_int64(found));
	}

	// Run the bake on a background thread to avoid freezing the main thread.
	if (bake_thread.joinable()) {
		bake_thread.join();
	}
	bake_thread = std::thread([this]() { this->bake_task(); });
}

void SteamAudioBakedReflections::bake_progress_callback(float progress, void *user_data) {
	SteamAudioBakedReflections *manager = static_cast<SteamAudioBakedReflections *>(user_data);
	manager->bake_progress = progress;
}

bool SteamAudioBakedReflections::get_is_baking() const { return is_baking.load(); }
float SteamAudioBakedReflections::get_bake_progress() const { return bake_progress.load(); }
void SteamAudioBakedReflections::set_num_rays(int p_num_rays) { num_rays = p_num_rays; }
int SteamAudioBakedReflections::get_num_rays() const { return num_rays; }
void SteamAudioBakedReflections::set_num_bounces(int p_num_bounces) { num_bounces = p_num_bounces; }
int SteamAudioBakedReflections::get_num_bounces() const { return num_bounces; }
void SteamAudioBakedReflections::set_duration(float p_duration) { duration = p_duration; }
float SteamAudioBakedReflections::get_duration() const { return duration; }
void SteamAudioBakedReflections::set_num_diffuse_samples(int p_num_diffuse_samples) { num_diffuse_samples = p_num_diffuse_samples; }
int SteamAudioBakedReflections::get_num_diffuse_samples() const { return num_diffuse_samples; }
void SteamAudioBakedReflections::set_irradiance_min_distance(float p_irradiance_min_distance) { irradiance_min_distance = p_irradiance_min_distance; }
float SteamAudioBakedReflections::get_irradiance_min_distance() const { return irradiance_min_distance; }
void SteamAudioBakedReflections::set_bake_parametric(bool p_bake_parametric) { bake_parametric = p_bake_parametric; }
bool SteamAudioBakedReflections::get_bake_parametric() const { return bake_parametric; }
void SteamAudioBakedReflections::set_bake_convolution(bool p_bake_convolution) { bake_convolution = p_bake_convolution; }
bool SteamAudioBakedReflections::get_bake_convolution() const { return bake_convolution; }
// center/radius accessors removed
void SteamAudioBakedReflections::set_probe_spacing(float p_probe_spacing) { probe_spacing = p_probe_spacing; }
float SteamAudioBakedReflections::get_probe_spacing() const { return probe_spacing; }
void SteamAudioBakedReflections::set_probe_height(float p_probe_height) { probe_height = p_probe_height; }
float SteamAudioBakedReflections::get_probe_height() const { return probe_height; }
void SteamAudioBakedReflections::set_probe_generation_extents(Vector3 p_probe_generation_extents) {
	probe_generation_extents = p_probe_generation_extents;
	// Notify editor to update gizmo when extents change
	if (Engine::get_singleton() && Engine::get_singleton()->is_editor_hint()) {
		update_gizmos();
	}
}
Vector3 SteamAudioBakedReflections::get_probe_generation_extents() const { return probe_generation_extents; }

void SteamAudioBakedReflections::set_include_scene_probes(bool p_include) { include_scene_probes = p_include; }
bool SteamAudioBakedReflections::get_include_scene_probes() const { return include_scene_probes; }

PackedStringArray SteamAudioBakedReflections::_get_configuration_warnings() const {
	PackedStringArray res;
	return res;
}

void SteamAudioBakedReflections::bake_task() {
	// Heavy work: run baker and serialize result. Probes must already be generated on the main thread.
	if (!probe_batch) {
		// Probes missing; abort safely and clear progress so UI doesn't display
		// a stale 100% from a previous bake.
		bake_progress = 0.0f;
		is_baking = false;
		return;
	}
	// Diagnostic: log that bake task started and some context info to help
	// diagnose Windows-only instant-complete issues.
	SteamAudio::log(SteamAudio::log_debug, "bake_task starting on thread");
#if defined(_WIN32) || defined(_WIN64)
	SteamAudio::log(SteamAudio::log_debug, "Platform: Windows");
#else
	SteamAudio::log(SteamAudio::log_debug, "Platform: Non-Windows");
#endif
	// Ensure global state is initialized and valid.
	GlobalSteamAudioState *gs = SteamAudioServer::get_singleton()->get_global_state(true);
	if (!gs) {
		UtilityFunctions::push_error("[Steam Audio] Global state is not initialized; cannot run bake.");
		bake_progress = 0.0f;
		is_baking = false;
		return;
	}
	IPLContext context = gs->ctx;
	// Bake global REVERB first.
	IPLReflectionsBakeParams params = get_bake_params();
	iplReflectionsBakerBake(context, &params, &SteamAudioBakedReflections::bake_progress_callback, this);

	// We'll commit after all passes (REVERB + STATICSOURCE) before querying sizes.

	// Then optionally bake STATICSOURCE per collected static source.
	IPLReflectionsBakeParams sparams = params; // copy shared settings up-front
	int static_passes = 0;
	if (!static_source_positions.empty()) {
		sparams.identifier.variation = IPL_BAKEDDATAVARIATION_STATICSOURCE;
		for (const IPLVector3 &pos : static_source_positions) {
			sparams.identifier.endpointInfluence.center = pos;
			sparams.identifier.endpointInfluence.radius = static_source_radius;
			iplReflectionsBakerBake(context, &sparams, &SteamAudioBakedReflections::bake_progress_callback, this);

			// Check this specific layer's size
			IPLsize pass_size = iplProbeBatchGetDataSize(probe_batch, &sparams.identifier);
			SteamAudio::log(SteamAudio::log_info, "STATICSOURCE data for source at (" + String::num_real(pos.x) + ", " + String::num_real(pos.y) + ", " + String::num_real(pos.z) + "): " + String::num_int64((int64_t)pass_size) + " bytes");

			++static_passes;
		}
		SteamAudio::log(SteamAudio::log_info, "STATICSOURCE bake passes: " + String::num_int64((int64_t)static_passes));
	} else if (bake_static_sources) {
		SteamAudio::log(SteamAudio::log_info, "STATICSOURCE bake enabled but no static sources were found in the scene.");
	}

	// IMPORTANT: Commit the probe batch after writing baked data so size queries reflect the new data.
	iplProbeBatchCommit(probe_batch);

	// Diagnostic: layer sizes after commit
	IPLsize reverb_size = iplProbeBatchGetDataSize(probe_batch, &params.identifier);
	if (reverb_size <= 0) {
		UtilityFunctions::push_warning("[Steam Audio] REVERB bake produced empty data layer.");
	}

	// Create a serialized object to save the probe batch into
	IPLSerializedObjectSettings so_settings{};
	IPLSerializedObject serialized_object = nullptr;
	IPLerror so_err = iplSerializedObjectCreate(context, &so_settings, &serialized_object);
	if (so_err != IPL_STATUS_SUCCESS) {
		// We are in a background thread; avoid heavy Godot calls. Use push_error sparingly.
		UtilityFunctions::push_error("[Steam Audio] Failed to create serialized object for baked reflections.");
		bake_progress = 0.0f;
		is_baking = false;
		return;
	}
	iplProbeBatchSave(probe_batch, serialized_object);
	// Copy bytes into a temporary buffer to finalize on the main thread.
	{
		std::lock_guard<std::mutex> lock(baked_bytes_mutex);
		const IPLbyte *data_ptr = reinterpret_cast<const IPLbyte *>(iplSerializedObjectGetData(serialized_object));
		IPLsize data_size = iplSerializedObjectGetSize(serialized_object);
		baked_bytes_buffer.resize(static_cast<size_t>(data_size));
		if (data_ptr && data_size > 0) {
			std::memcpy(baked_bytes_buffer.data(), data_ptr, static_cast<size_t>(data_size));
		}
	}
	iplSerializedObjectRelease(&serialized_object);

	// Defer finalization on the main thread to interact with Godot resources safely.
	call_deferred("finalize_bake");
}

// New getters/setters
void SteamAudioBakedReflections::set_bake_static_sources(bool p_bake_static_sources) { bake_static_sources = p_bake_static_sources; }
bool SteamAudioBakedReflections::get_bake_static_sources() const { return bake_static_sources; }
void SteamAudioBakedReflections::set_static_source_radius(float p_radius) { static_source_radius = p_radius; }
float SteamAudioBakedReflections::get_static_source_radius() const { return static_source_radius; }

void SteamAudioBakedReflections::finalize_bake() {
	// Recreate a serialized object from bytes and push into the Resource.
	std::vector<uint8_t> local_copy;
	{
		std::lock_guard<std::mutex> lock(baked_bytes_mutex);
		local_copy = baked_bytes_buffer;
	}
	if (local_copy.empty()) {
		UtilityFunctions::push_warning("[Steam Audio] Finalize called but no baked bytes were produced; aborting finalize.");
		bake_progress = 0.0f;
		is_baking = false;
		return;
	}
	// Ensure global state is initialized for finalization.
	GlobalSteamAudioState *gs = SteamAudioServer::get_singleton()->get_global_state(true);
	if (!gs) {
		UtilityFunctions::push_error("[Steam Audio] Global state is not initialized; cannot finalize bake.");
		bake_progress = 0.0f;
		is_baking = false;
		return;
	}
	IPLContext context = gs->ctx;
	IPLSerializedObject serialized_object = nullptr;
	IPLSerializedObjectSettings settings{};
	// IPLSerializedObjectSettings::data expects a non-const IPLbyte*.
	// local_copy.data() yields a non-const uint8_t* here, so cast accordingly.
	settings.data = local_copy.empty() ? nullptr : reinterpret_cast<IPLbyte *>(local_copy.data());
	settings.size = static_cast<IPLsize>(local_copy.size());
	IPLerror so_err = iplSerializedObjectCreate(context, &settings, &serialized_object);
	if (so_err != IPL_STATUS_SUCCESS) {
		UtilityFunctions::push_error("[Steam Audio] Failed to finalize baked reflections serialized object.");
		bake_progress = 0.0f;
		is_baking = false;
		return;
	}
	if (!baked_data.is_valid()) {
		baked_data.instantiate();
	}
	baked_data->set_data(serialized_object);
	iplSerializedObjectRelease(&serialized_object);

	// Inform the user and runtime about the result, and register the baked probe batch with the simulator.
	{
		const int64_t bytes = static_cast<int64_t>(local_copy.size());
		const String res_path = baked_data->get_path();
		SteamAudio::log(SteamAudio::log_info, "Baked reflections saved " + String::num_int64(bytes) + " bytes to resource: " + res_path);
	}

	// Ensure the simulator uses the freshly baked data (adds/updates the probe batch in the sim).
	if (SteamAudioServer::get_singleton()) {
		SteamAudioServer::get_singleton()->set_baked_reflections(this);
	}

	bake_progress = 1.0f;
	is_baking = false;
}

void SteamAudioBakedReflections::clear() {
	// If a bake is running, wait for it to finish to avoid races.
	if (bake_thread.joinable()) {
		bake_thread.join();
	}

	// Release any existing probe batch so future bakes regenerate from scratch.
	if (probe_batch) {
		iplProbeBatchRelease(&probe_batch);
		probe_batch = nullptr;
	}

	// Reset cached state used by the editor gizmo and bake pipeline.
	last_probe_positions_local.clear();
	last_probe_count = 0;

	// Clear transient buffers used to shuttle baked bytes between threads.
	{
		std::lock_guard<std::mutex> lock(baked_bytes_mutex);
		baked_bytes_buffer.clear();
		baked_bytes_buffer.shrink_to_fit();
	}
	static_source_positions.clear();

	// Reset flags and progress.
	is_baking = false;
	bake_progress = 0.0f;

	SteamAudio::log(SteamAudio::log_info, "Baked reflections state cleared. Next bake will regenerate probes and data.");

	// Ensure simulator is aware that there is no active batch from this baker until we bake again.
	if (SteamAudioServer::get_singleton()) {
		SteamAudioServer::get_singleton()->set_baked_reflections(this);
	}
}
