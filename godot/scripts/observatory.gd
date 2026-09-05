extends Node3D
## Application boundary: one native world, explicit day commands, immutable views.

const CHECKPOINT: String = "user://observer.wsc"
@onready var terrain: ObserverTerrain = $Terrain
@onready var camera: ObserverCamera = $ObserverCamera
@onready var hud: ObserverHUD = $HUD
var bridge: Object
var paused: bool = false
var days_per_second: float = 16.0
# Preserve exact daily core steps while bounding work between rendered frames.
const MAX_DAYS_PER_FRAME: int = 32
const STEP_BUDGET_USEC: int = 8000
var _elapsed: float = 0.0
var _remaining_days: int = 0
var _inspection_elapsed: float = 0.0
var _frame: Dictionary = {}
var _selected: Dictionary = {}
var _pinned: bool = false
var _sample_x_m: float = 0.0
var _sample_y_m: float = 0.0


func _ready() -> void:
	camera.terrain = terrain
	camera.overview_requested.connect(_overview)
	camera.pause_requested.connect(_toggle_pause)
	hud.pause_requested.connect(_toggle_pause)
	hud.step_requested.connect(_single_step)
	hud.fast_forward_requested.connect(_fast_forward)
	hud.speed_changed.connect(func(speed: float) -> void: days_per_second = speed; _elapsed = 0.0)
	hud.layer_changed.connect(_set_layer)
	hud.grid_changed.connect(terrain.set_grid)
	hud.seed_requested.connect(_new_world)
	hud.save_requested.connect(_save)
	hud.load_requested.connect(_load)
	hud.overview_requested.connect(_overview)
	hud.focus_requested.connect(_focus)
	hud.clear_requested.connect(_unpin)
	if not ClassDB.class_exists(&"WorldSimBridge"):
		hud.show_error("WorldSim native bridge is not available.\n\nBuild the C++ GDExtension from the repository root:\n\ncmake --preset godot\ncmake --build --preset godot\n\nThen reopen godot/project.godot. See docs/GODOT.md for prerequisites.\n\nThe observer needs the real simulation to display a world.")
		set_process(false)
		return
	bridge = ClassDB.instantiate(&"WorldSimBridge")
	var checkpoint_path := ""
	var seed_value: int = 42
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with("--checkpoint="):
			checkpoint_path = argument.trim_prefix("--checkpoint=")
		elif argument.begins_with("--seed="):
			var seed_text := argument.trim_prefix("--seed=")
			if seed_text.is_valid_int():
				seed_value = seed_text.to_int()
		elif argument == "--paused":
			paused = true
	if not checkpoint_path.is_empty():
		if not checkpoint_path.is_absolute_path():
			hud.show_error("--checkpoint must use an absolute filesystem path.\n\nExample: godot --path godot -- --checkpoint=/absolute/path/world.wsc")
			return
		if not bool(bridge.call("load_world", checkpoint_path)):
			hud.show_error("Could not load checkpoint:\n%s\n\n%s\n\nChoose New world or Load to continue." % [checkpoint_path, str(bridge.call("get_last_error"))])
			return
	else:
		if not bool(bridge.call("create_world", seed_value)):
			hud.show_error("Could not create world:\n%s" % str(bridge.call("get_last_error")))
			return
	_refresh_world()


func _ready_world() -> bool:
	return bridge != null and bool(bridge.call("is_ready")) and not _frame.is_empty()


func _refresh_world() -> void:
	_frame = bridge.call("get_frame")
	var data: Dictionary = bridge.call("get_terrain", 128)
	if _frame.is_empty() or data.is_empty():
		hud.show_error("Could not read world for observation:\n%s" % str(bridge.call("get_last_error")))
		return
	terrain.load_terrain(data, _frame)
	_selected = {}
	_pinned = false
	_elapsed = 0.0
	_remaining_days = 0
	hud.reset_history()
	hud.show_progress(0)
	hud.clear_error()
	hud.show_sample({}, false)
	hud.show_frame(_frame, paused)
	_overview()


func _advance(refresh: bool = true) -> bool:
	if not _ready_world():
		return false
	if not bool(bridge.call("advance_day")):
		paused = true
		_remaining_days = 0
		hud.show_progress(0)
		hud.set_paused(true)
		hud.show_message(str(bridge.call("get_last_error")), true)
		return false
	if refresh:
		_refresh_frame()
	return true


func _refresh_frame() -> void:
	_frame = bridge.call("get_frame")
	terrain.update_frame(_frame)
	hud.show_frame(_frame, paused)
	if _pinned:
		_read_sample(_sample_x_m, _sample_y_m)


func _new_world(seed_value: int) -> void:
	if bridge == null:
		return
	if not bool(bridge.call("create_world", seed_value)):
		hud.show_message(str(bridge.call("get_last_error")), true)
		return
	_refresh_world()
	hud.show_message("Created world with seed %s." % seed_value)


func _save() -> void:
	if not _ready_world():
		return
	var path := ProjectSettings.globalize_path(CHECKPOINT)
	if bool(bridge.call("save_world", path)):
		hud.show_message("Saved day %s to observer.wsc." % _frame.get("day", 0))
	else:
		hud.show_message(str(bridge.call("get_last_error")), true)


func _load() -> void:
	if bridge == null:
		return
	var path := ProjectSettings.globalize_path(CHECKPOINT)
	if bool(bridge.call("load_world", path)):
		_refresh_world()
		hud.show_message("Loaded observer.wsc.")
	else:
		hud.show_message(str(bridge.call("get_last_error")), true)


func _toggle_pause() -> void:
	if not _ready_world():
		return
	paused = true if _remaining_days > 0 else not paused
	_elapsed = 0.0
	_remaining_days = 0
	hud.show_progress(0)
	hud.set_paused(paused)


func _single_step() -> void:
	if not _ready_world():
		return
	paused = true
	_elapsed = 0.0
	_remaining_days = 0
	hud.show_progress(0)
	_advance()


func _fast_forward(days: int) -> void:
	if not _ready_world() or days < 1 or days > 365:
		return
	paused = true
	_elapsed = 0.0
	_remaining_days = days
	hud.set_paused(true)
	hud.show_progress(_remaining_days)


func _set_layer(index: int) -> void:
	terrain.set_layer(index)
	hud.set_map_layer(index)


func _overview() -> void:
	if terrain.has_terrain():
		camera.show_overview(terrain.world_size)


func _focus() -> void:
	if not _selected.is_empty():
		camera.focus_point(terrain.local_point(_sample_x_m, _sample_y_m))


func _unpin() -> void:
	_pinned = false
	_selected = {}
	terrain.select_cell({})
	hud.show_sample({}, false)


func _read_sample(x_m: float, y_m: float) -> void:
	_selected = bridge.call("sample_point", x_m, y_m)
	if _selected.is_empty():
		return
	var settlements: Array[Dictionary] = []
	for settlement: Dictionary in _frame.get("settlements", []):
		if int(floor(float(settlement.get("x_m", 0.0)) / ObserverTerrain.CELL_METERS)) == int(_selected.get("cell_x", 0)) and int(floor(float(settlement.get("y_m", 0.0)) / ObserverTerrain.CELL_METERS)) == int(_selected.get("cell_y", 0)):
			settlements.append(settlement)
	_selected["observer_settlements"] = settlements
	_sample_x_m = x_m
	_sample_y_m = y_m
	terrain.select_cell(_selected)
	hud.show_sample(_selected, _pinned)


func _inspect(mouse_position: Vector2) -> void:
	var hit: Variant = terrain.pick(camera.project_ray_origin(mouse_position), camera.project_ray_normal(mouse_position))
	if hit is Vector3:
		_read_sample(terrain.world_x_m(hit.x), terrain.world_y_m(hit.z))
	elif not _pinned:
		_selected = {}
		terrain.select_cell({})
		hud.show_sample({}, false)


func _unhandled_input(event: InputEvent) -> void:
	if not _ready_world():
		return
	if event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_LEFT and event.pressed:
		_inspect(event.position)
		_pinned = not _selected.is_empty()
		hud.show_sample(_selected, _pinned)


func _process(delta: float) -> void:
	if not _ready_world():
		return
	# A stall must not create unlimited catch-up work. Retain fractional days,
	# cap the backlog, and render once per batch instead of once per native day.
	if not paused and is_finite(days_per_second) and days_per_second > 0.0:
		_elapsed = minf(_elapsed + maxf(delta, 0.0) * days_per_second, float(MAX_DAYS_PER_FRAME))
	var requested := _remaining_days if _remaining_days > 0 else (int(_elapsed) if not paused else 0)
	var advanced := 0
	var started := Time.get_ticks_usec()
	while advanced < mini(requested, MAX_DAYS_PER_FRAME):
		if not _advance(false):
			break
		advanced += 1
		if _remaining_days > 0:
			_remaining_days -= 1
		else:
			_elapsed -= 1.0
		if Time.get_ticks_usec() - started >= STEP_BUDGET_USEC:
			break
	if advanced > 0:
		_refresh_frame()
		hud.show_progress(_remaining_days)
	_inspection_elapsed += delta
	if _inspection_elapsed >= 0.12:
		_inspection_elapsed = 0.0
		hud.show_coordinates(camera.position.x / ObserverTerrain.HORIZONTAL_SCALE + terrain.origin_x_m, camera.position.z / ObserverTerrain.HORIZONTAL_SCALE + terrain.origin_y_m, camera.position.y / ObserverTerrain.VERTICAL_SCALE, camera.move_speed)
		if not _pinned and Input.mouse_mode != Input.MOUSE_MODE_CAPTURED and get_viewport().gui_get_hovered_control() == null:
			_inspect(get_viewport().get_mouse_position())
