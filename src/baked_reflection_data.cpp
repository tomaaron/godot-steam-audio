#include "baked_reflection_data.hpp"
#include "server.hpp"

#include <cstring>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

void SteamAudioBakedReflectionData::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_serialized_data", "data"), &SteamAudioBakedReflectionData::set_serialized_data);
	ClassDB::bind_method(D_METHOD("get_serialized_data"), &SteamAudioBakedReflectionData::get_serialized_data);

	ADD_PROPERTY(PropertyInfo(Variant::PACKED_BYTE_ARRAY, "serialized_data"), "set_serialized_data", "get_serialized_data");
}

SteamAudioBakedReflectionData::SteamAudioBakedReflectionData() {
}

SteamAudioBakedReflectionData::~SteamAudioBakedReflectionData() {
}

void SteamAudioBakedReflectionData::set_data(IPLSerializedObject serialized_object) {
	auto data = static_cast<const IPLbyte *>(iplSerializedObjectGetData(serialized_object));
	const IPLsize size = iplSerializedObjectGetSize(serialized_object);
	serialized_data.resize(size);
	if (size > 0 && data != nullptr) {
		uint8_t *dst = serialized_data.ptrw();
		// Copy byte-for-byte from Steam Audio serialized object into our PackedByteArray
		std::memcpy(dst, data, static_cast<size_t>(size));
	}

	Error result = ResourceSaver::get_singleton()->save(this, get_path(), ResourceSaver::FLAG_COMPRESS);
	if (result != OK) {
		UtilityFunctions::push_error("Failed to save the baked reflection data resource. Error code: " + String::num_int64(result));
	}
}

IPLSerializedObject SteamAudioBakedReflectionData::get_data() {
	// If no bytes are stored, return null to signal absence of baked data.
	if (serialized_data.is_empty()) {
		UtilityFunctions::push_warning("[Steam Audio] Baked reflections resource has no data (0 bytes).");
		return nullptr;
	}
	// Guard against use before SteamAudioServer global state is initialized.
	SteamAudioServer *srv = SteamAudioServer::get_singleton();
	if (!srv) {
		UtilityFunctions::push_error("[Steam Audio] SteamAudioServer singleton is null when requesting baked data.");
		return nullptr;
	}
	GlobalSteamAudioState *gs = srv->get_global_state(false);
	if (!gs || !gs->ctx) {
		UtilityFunctions::push_warning("[Steam Audio] Global state/context not initialized; returning null serialized object for baked reflections.");
		return nullptr;
	}

	IPLSerializedObject serialized_object{};
	IPLSerializedObjectSettings settings = { serialized_data.ptrw(), static_cast<IPLsize>(serialized_data.size()) };
	IPLerror err = iplSerializedObjectCreate(gs->ctx, &settings, &serialized_object);
	if (err != IPL_STATUS_SUCCESS) {
		UtilityFunctions::push_error("[Steam Audio] Failed to create serialized object from baked reflection resource. Status: " + String::num_int64(err));
		return nullptr;
	}
	return serialized_object;
}

void SteamAudioBakedReflectionData::set_serialized_data(const PackedByteArray &p_data) {
	serialized_data = p_data;
}

PackedByteArray SteamAudioBakedReflectionData::get_serialized_data() const {
	return serialized_data;
}
