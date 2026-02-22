@tool
extends EditorPlugin

const BakedReflectionsGizmo = preload("res://addons/godot-steam-audio/src/steam_audio_baked_reflections_gizmo.gd")

var _button: Button
var _progress_dialog: AcceptDialog
var _progress_bar: ProgressBar
var _cancel_button: Button
var _status_label: Label
var _is_processing: bool = false
var _should_cancel: bool = false
var _gizmo_plugin: EditorNode3DGizmoPlugin


func _enter_tree() -> void:
	_create_button()
	_create_progress_dialog()
	
	# Register the gizmo plugin
	_gizmo_plugin = BakedReflectionsGizmo.new()
	add_node_3d_gizmo_plugin(_gizmo_plugin)

	# Connect to selection changes
	var selection := EditorInterface.get_selection()
	selection.selection_changed.connect(_on_selection_changed)

	# Initial visibility check
	_update_button_visibility()


func _exit_tree() -> void:
	var selection := EditorInterface.get_selection()
	if selection.selection_changed.is_connected(_on_selection_changed):
		selection.selection_changed.disconnect(_on_selection_changed)
	
	# Unregister the gizmo plugin
	if _gizmo_plugin:
		remove_node_3d_gizmo_plugin(_gizmo_plugin)
		_gizmo_plugin = null

	if _button:
		remove_control_from_container(EditorPlugin.CONTAINER_SPATIAL_EDITOR_MENU, _button)
		_button.queue_free()
	if _progress_dialog:
		_progress_dialog.queue_free()


func _create_button() -> void:
	_button = Button.new()
	_button.text = "Bake Reflections"
	_button.pressed.connect(_on_bake_button_pressed)
	_button.visible = false  # Hidden by default until correct node selected
	add_control_to_container(EditorPlugin.CONTAINER_SPATIAL_EDITOR_MENU, _button)


func _create_progress_dialog() -> void:
	_progress_dialog = AcceptDialog.new()
	_progress_dialog.title = "Baking Reflections"
	_progress_dialog.size = Vector2i(400, 150)
	_progress_dialog.exclusive = true
	_progress_dialog.unresizable = true
	# Hide the default OK button
	_progress_dialog.get_ok_button().hide()
	# Prevent closing via X button while processing
	_progress_dialog.close_requested.connect(_on_dialog_close_requested)

	var vbox := VBoxContainer.new()
	vbox.set_anchors_preset(Control.PRESET_FULL_RECT)
	vbox.set_offsets_preset(Control.PRESET_FULL_RECT, Control.PRESET_MODE_MINSIZE, 8)
	_progress_dialog.add_child(vbox)

	_status_label = Label.new()
	_status_label.text = "Initializing..."
	_status_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	vbox.add_child(_status_label)

	_progress_bar = ProgressBar.new()
	_progress_bar.min_value = 0.0
	_progress_bar.max_value = 100.0
	_progress_bar.value = 0.0
	_progress_bar.show_percentage = true
	_progress_bar.custom_minimum_size = Vector2(380, 30)
	vbox.add_child(_progress_bar)

	var spacer := Control.new()
	spacer.custom_minimum_size = Vector2(0, 10)
	vbox.add_child(spacer)

	var button_container := HBoxContainer.new()
	button_container.alignment = BoxContainer.ALIGNMENT_CENTER
	vbox.add_child(button_container)

	_cancel_button = Button.new()
	_cancel_button.text = "Cancel"
	_cancel_button.custom_minimum_size = Vector2(100, 30)
	_cancel_button.pressed.connect(_on_cancel_pressed)
	button_container.add_child(_cancel_button)

	EditorInterface.get_base_control().add_child(_progress_dialog)


func _on_selection_changed() -> void:
	_update_button_visibility()


func _update_button_visibility() -> void:
	if not _button:
		return

	_button.visible = _get_selected_baked_reflections_node() != null


func _get_selected_baked_reflections_node() -> Node:
	var selection := EditorInterface.get_selection()
	var selected_nodes := selection.get_selected_nodes()

	for node in selected_nodes:
		# Check by class name string (works even if class isn't directly available)
		if node.get_class() == "SteamAudioBakedReflections":
			return node

	return null


func _on_bake_button_pressed() -> void:
	if _is_processing:
		return

	var target_node := _get_selected_baked_reflections_node()
	if not target_node:
		return

	_is_processing = true
	_should_cancel = false
	_progress_bar.value = 0.0
	_status_label.text = "Starting bake process..."
	_cancel_button.text = "Cancel"
	_cancel_button.disabled = false

	_progress_dialog.popup_centered()

	# Start the bake process with the selected node
	_do_bake_process(target_node)


func _do_bake_process(target_node: Node) -> void:
	target_node.start_bake()

	while target_node.get_is_baking():
		if _should_cancel:
			# TODO: Add cancel method if available on baker
			_status_label.text = "Cancelled!"
			_progress_bar.value = 0.0
			_is_processing = false
			_cancel_button.text = "Close"
			_cancel_button.disabled = false
			return

		var progress := int(target_node.get_bake_progress() * 100.0)
		_progress_bar.value = progress
		_status_label.text = "Baking reflections: %.1f%%" % progress

		# Yield to allow UI updates
		await get_tree().process_frame

	# Completed
	_progress_bar.value = 100.0
	_status_label.text = "Bake complete!"
	_is_processing = false
	_cancel_button.text = "Close"


func _on_cancel_pressed() -> void:
	if _is_processing:
		_should_cancel = true
		_cancel_button.disabled = true
		_status_label.text = "Cancelling..."
	else:
		_progress_dialog.hide()


func _on_dialog_close_requested() -> void:
	if not _is_processing:
		_progress_dialog.hide()
	# If processing, ignore the close request (modal stays open)
