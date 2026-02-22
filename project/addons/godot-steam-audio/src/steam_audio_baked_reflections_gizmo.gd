@tool
extends EditorNode3DGizmoPlugin

const GIZMO_COLOR := Color(0.3, 0.7, 1.0, 0.6)
const PROBE_COLOR := Color(1.0, 0.8, 0.2, 0.8)
const BOX_COLOR := Color(0.3, 0.7, 1.0, 0.3)
const PROBE_RADIUS := 0.15


func _init() -> void:
	create_material("box_material", BOX_COLOR)
	create_material("probe_material", PROBE_COLOR)
	create_handle_material("handles")


func _get_gizmo_name() -> String:
	return "SteamAudioBakedReflections"


func _has_gizmo(node: Node3D) -> bool:
	return node.get_class() == "SteamAudioBakedReflections"


func _redraw(gizmo: EditorNode3DGizmo) -> void:
	gizmo.clear()
	
	var node := gizmo.get_node_3d()
	if not node:
		return
	
	# Get the probe generation extents from the node
	var extents: Vector3 = node.get("probe_generation_extents")
	if not extents:
		return
	
	# Draw the bounding box showing the generation volume
	_draw_box(gizmo, extents)
	
	# Draw the generated probe positions
	_draw_probes(gizmo, node)


func _draw_box(gizmo: EditorNode3DGizmo, extents: Vector3) -> void:
	var lines := PackedVector3Array()
	
	# Create box vertices at half-extents (box goes from -extents/2 to +extents/2)
	var half_ext := extents * 0.5
	
	# Bottom face (Y = -half_ext.y)
	lines.append(Vector3(-half_ext.x, -half_ext.y, -half_ext.z))
	lines.append(Vector3(half_ext.x, -half_ext.y, -half_ext.z))
	
	lines.append(Vector3(half_ext.x, -half_ext.y, -half_ext.z))
	lines.append(Vector3(half_ext.x, -half_ext.y, half_ext.z))
	
	lines.append(Vector3(half_ext.x, -half_ext.y, half_ext.z))
	lines.append(Vector3(-half_ext.x, -half_ext.y, half_ext.z))
	
	lines.append(Vector3(-half_ext.x, -half_ext.y, half_ext.z))
	lines.append(Vector3(-half_ext.x, -half_ext.y, -half_ext.z))
	
	# Top face (Y = half_ext.y)
	lines.append(Vector3(-half_ext.x, half_ext.y, -half_ext.z))
	lines.append(Vector3(half_ext.x, half_ext.y, -half_ext.z))
	
	lines.append(Vector3(half_ext.x, half_ext.y, -half_ext.z))
	lines.append(Vector3(half_ext.x, half_ext.y, half_ext.z))
	
	lines.append(Vector3(half_ext.x, half_ext.y, half_ext.z))
	lines.append(Vector3(-half_ext.x, half_ext.y, half_ext.z))
	
	lines.append(Vector3(-half_ext.x, half_ext.y, half_ext.z))
	lines.append(Vector3(-half_ext.x, half_ext.y, -half_ext.z))
	
	# Vertical edges connecting bottom to top
	lines.append(Vector3(-half_ext.x, -half_ext.y, -half_ext.z))
	lines.append(Vector3(-half_ext.x, half_ext.y, -half_ext.z))
	
	lines.append(Vector3(half_ext.x, -half_ext.y, -half_ext.z))
	lines.append(Vector3(half_ext.x, half_ext.y, -half_ext.z))
	
	lines.append(Vector3(half_ext.x, -half_ext.y, half_ext.z))
	lines.append(Vector3(half_ext.x, half_ext.y, half_ext.z))
	
	lines.append(Vector3(-half_ext.x, -half_ext.y, half_ext.z))
	lines.append(Vector3(-half_ext.x, half_ext.y, half_ext.z))
	
	var material := get_material("box_material", gizmo)
	gizmo.add_lines(lines, material, false)


func _draw_probes(gizmo: EditorNode3DGizmo, node: Node) -> void:
	# Get probe positions from the node (already in local space)
	var probe_positions: PackedVector3Array = node.call("get_probe_positions")
	
	if probe_positions.is_empty():
		return
	
	var probe_material := get_material("probe_material", gizmo)
	
	# Draw each probe as a small sphere
	for pos in probe_positions:
		_draw_probe_sphere(gizmo, pos, PROBE_RADIUS, probe_material)


func _draw_probe_sphere(gizmo: EditorNode3DGizmo, center: Vector3, radius: float, material: Material) -> void:
	# Create a simple cross/diamond shape to represent each probe
	var lines := PackedVector3Array()
	
	# X axis
	lines.append(center + Vector3(-radius, 0, 0))
	lines.append(center + Vector3(radius, 0, 0))
	
	# Y axis
	lines.append(center + Vector3(0, -radius, 0))
	lines.append(center + Vector3(0, radius, 0))
	
	# Z axis
	lines.append(center + Vector3(0, 0, -radius))
	lines.append(center + Vector3(0, 0, radius))
	
	# Diagonal crosses for better visibility
	lines.append(center + Vector3(-radius * 0.7, -radius * 0.7, 0))
	lines.append(center + Vector3(radius * 0.7, radius * 0.7, 0))
	
	lines.append(center + Vector3(-radius * 0.7, radius * 0.7, 0))
	lines.append(center + Vector3(radius * 0.7, -radius * 0.7, 0))
	
	gizmo.add_lines(lines, material, false)
