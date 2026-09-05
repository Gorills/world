class_name ObserverCamera
extends Camera3D
## A detached observer. Moving the camera never changes simulation allocation/state.

signal overview_requested
signal pause_requested

@export var move_speed: float = 22.0
@export var look_sensitivity: float = 0.0025
var terrain: ObserverTerrain
var _extent: float = 128.0


func show_overview(world_size: Vector2) -> void:
	_extent = maxf(world_size.x, world_size.y)
	move_speed = _extent * 0.18
	var center := Vector3(world_size.x * 0.5, 2.0, world_size.y * 0.5)
	position = center + Vector3(_extent * 0.70, _extent * 1.17, _extent * 1.28)
	# Aim slightly right of the world center to leave room for the inspector.
	look_at(center + Vector3(_extent * 0.18, -_extent * 0.02, -_extent * 0.10))
	far = maxf(3000.0, _extent * 8.0)
	Input.mouse_mode = Input.MOUSE_MODE_VISIBLE


func focus_point(point: Vector3) -> void:
	position = point + Vector3(7.0, 10.0, 12.0)
	look_at(point)


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_RIGHT:
			Input.mouse_mode = Input.MOUSE_MODE_CAPTURED if event.pressed else Input.MOUSE_MODE_VISIBLE
		elif event.pressed and event.button_index == MOUSE_BUTTON_WHEEL_UP:
			move_speed = clampf(move_speed * 1.25, 0.4, _extent * 2.0)
		elif event.pressed and event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			move_speed = clampf(move_speed / 1.25, 0.4, _extent * 2.0)
	elif event is InputEventMouseMotion and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		rotation.y -= event.relative.x * look_sensitivity
		rotation.x = clampf(rotation.x - event.relative.y * look_sensitivity, -1.53, 1.45)
	elif event is InputEventKey and event.pressed and not event.echo:
		match event.physical_keycode:
			KEY_ESCAPE:
				Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
			KEY_F, KEY_HOME:
				overview_requested.emit()
			KEY_SPACE:
				pause_requested.emit()


func _process(delta: float) -> void:
	# Text fields and other controls own keyboard input while focused.
	if get_viewport().gui_get_focus_owner() != null:
		return
	var movement := Vector3.ZERO
	if Input.is_physical_key_pressed(KEY_W):
		movement.z -= 1.0
	if Input.is_physical_key_pressed(KEY_S):
		movement.z += 1.0
	if Input.is_physical_key_pressed(KEY_A):
		movement.x -= 1.0
	if Input.is_physical_key_pressed(KEY_D):
		movement.x += 1.0
	var vertical := float(Input.is_physical_key_pressed(KEY_E)) - float(Input.is_physical_key_pressed(KEY_Q))
	var velocity := global_basis * movement + Vector3.UP * vertical
	var boost := 3.0 if Input.is_physical_key_pressed(KEY_SHIFT) else 1.0
	if velocity.length_squared() > 0.0:
		position += velocity.normalized() * move_speed * boost * delta
		if terrain != null and terrain.has_terrain():
			position.y = maxf(position.y, terrain.height_at(position.x, position.z) + 0.35)
