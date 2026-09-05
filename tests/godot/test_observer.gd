extends SceneTree
## Real scene/input/render-state regression. Optional --screenshot=/absolute/path.png
## captures this project's viewport only when run with a graphical display.

var _failures: int = 0


func _check(condition: bool, message: String) -> void:
	if not condition:
		_failures += 1
		push_error(message)


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	var packed: PackedScene = load("res://scenes/observatory.tscn")
	var scene: Node = packed.instantiate()
	root.add_child(scene)
	scene.paused = true
	scene.set_process(false)
	await process_frame
	_check(scene._ready_world(), "main scene connects to the real native world")
	if not scene._ready_world():
		quit(1)
		return
	_check(int(scene._frame.day) == 0, "new observer starts at day zero")
	_check(scene.terrain.has_terrain(), "terrain exists")
	var vertices: PackedVector3Array = scene.terrain._arrays[Mesh.ARRAY_VERTEX]
	var normals: PackedVector3Array = scene.terrain._arrays[Mesh.ARRAY_NORMAL]
	_check(vertices.size() == 129 * 129, "terrain has bounded, complete mesh samples")
	for vertex: Vector3 in vertices:
		_check(vertex.is_finite(), "terrain vertex is finite")
	for normal: Vector3 in normals:
		_check(normal.is_finite() and normal.length_squared() > 0.9, "terrain normal is valid")
	var landscape: PackedColorArray = scene.terrain._arrays[Mesh.ARRAY_COLOR]
	for layer: int in range(ObserverTerrain.LAYER_NAMES.size()):
		scene.hud.layer_changed.emit(layer)
		_check(scene.terrain.layer == layer, "UI changes visible layer")
		if layer != 0:
			_check(scene.terrain._arrays[Mesh.ARRAY_COLOR] != landscape, "diagnostic layer changes mesh colors")
	scene.hud.layer_changed.emit(0)
	scene.hud.grid_changed.emit(true)
	_check(scene.terrain._grid.visible, "UI enables resolution grid")
	scene.hud.grid_changed.emit(false)
	scene._process(2.0)
	_check(int(scene._frame.day) == 0, "paused scene never advances with elapsed time")
	scene.hud.step_requested.emit()
	_check(scene.paused and int(scene._frame.day) == 1, "single-step emits exactly one native day and pauses")
	scene._read_sample(65536.0, 65536.0)
	_check(not scene._selected.is_empty(), "point inspection reads native state")
	scene._pinned = true
	scene.hud.speed_changed.emit(10.0)
	scene.hud.pause_requested.emit()
	scene._process(0.11)
	_check(int(scene._frame.day) == 2 and not scene._selected.is_empty(), "running time refreshes pinned state")
	scene.hud.pause_requested.emit()
	var before_camera: Vector3 = scene.camera.position
	scene.hud.focus_requested.emit()
	_check(scene.camera.position != before_camera, "fly-to moves the observer camera")
	_check(int(scene.bridge.get_frame().day) == 2, "camera movement leaves simulation time alone")
	# A static river-potential flag must never paint a dry channel as flowing.
	var dry_frame: Dictionary = scene._frame.duplicate(true)
	for cell: Dictionary in dry_frame.cells:
		cell.river = true
		cell.channel_storage_m3 = 0.0
		cell.channel_discharge_m3_s = 0.0
	scene.terrain.update_frame(dry_frame)
	_check(scene.terrain._rivers.mesh == null, "dry drainage has no blue river geometry")
	scene.terrain.update_frame(scene._frame)
	var start_frame: Dictionary = scene._frame.duplicate(true)
	var reference: Variant = ClassDB.instantiate("WorldSimBridge")
	_check(reference.create_world(42), "independent daily reference starts")
	for day: int in range(367):
		_check(reference.advance_day(), "independent daily reference advances")
	scene.hud.fast_forward_requested.emit(365)
	var batches := 0
	while scene._remaining_days > 0 and batches < 365:
		var before_day := int(scene._frame.day)
		scene._process(0.0)
		_check(int(scene._frame.day) - before_day >= 1 and int(scene._frame.day) - before_day <= scene.MAX_DAYS_PER_FRAME,
			"fast-forward bounds native work per frame")
		batches += 1
	_check(scene.paused and int(scene._frame.day) == 367 and scene._remaining_days == 0,
		"year command completes exactly 365 days and stays paused")
	_check(scene._frame == reference.get_frame(), "batched observation equals 367 separate native days exactly")
	_check(scene._frame.totals.plant_carbon_kg != start_frame.totals.plant_carbon_kg,
		"a displayed year changes live plant biomass")
	_check(scene._frame.totals.channel_storage_m3 != start_frame.totals.channel_storage_m3 and
		scene._frame.totals.max_channel_discharge_m3_s > 0.0, "a displayed year evolves river water and flow")
	_check(scene.terrain._rivers.mesh != null, "wet channels produce visible river geometry")
	var history: Variant = scene.hud._history
	var history_count: int = history._days.size()
	scene.hud.show_frame(scene._frame, scene.paused)
	_check(history._days.size() == history_count, "repeated inspection does not duplicate history days")
	_check(history._days.size() <= 256 and history._days.front() >= 0 and history._days.back() == 367,
		"history follows model time across bounded fast-forward batches")
	scene.hud.fast_forward_requested.emit(30)
	scene.hud.pause_requested.emit()
	scene._process(1.0)
	_check(scene._remaining_days == 0 and scene.paused and int(scene._frame.day) == 367,
		"cancel stops queued days and leaves the world paused")
	scene.hud.fast_forward_requested.emit(30)
	scene.hud.step_requested.emit()
	_check(scene._remaining_days == 0 and scene.paused and int(scene._frame.day) == 368,
		"single-step cancels queued fast-forward and advances once")
	# Preserve fractional time instead of dropping the 0.05 s overshoot each step.
	scene.hud.speed_changed.emit(10.0)
	scene.hud.pause_requested.emit()
	scene._process(0.15)
	scene._process(0.15)
	_check(int(scene._frame.day) == 371, "fractional elapsed time survives across frames")
	var before_stall := int(scene._frame.day)
	scene._process(3600.0)
	_check(int(scene._frame.day) - before_stall <= scene.MAX_DAYS_PER_FRAME, "long stalls cannot create an unbounded catch-up loop")
	scene.hud.pause_requested.emit()
	reference = null
	scene.hud.overview_requested.emit()
	scene._read_sample(65536.0, 65536.0)
	_check(scene.terrain.pick(scene.camera.position, (scene.terrain.local_point(65536.0, 65536.0) - scene.camera.position).normalized()) is Vector3,
		"camera ray can select the rendered world")
	await process_frame
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with("--screenshot="):
			await RenderingServer.frame_post_draw
			var screenshot: Image = root.get_texture().get_image()
			_check(screenshot.save_png(argument.trim_prefix("--screenshot=")) == OK, "viewport screenshot saved")
	scene.hud.seed_requested.emit(123)
	_check(str(scene._frame.seed) == "123" and int(scene._frame.day) == 0, "UI new-world command resets state")
	_check(history._days.size() == 1 and history._days[0] == 0,
		"a new world resets history instead of connecting unrelated worlds")
	scene.hud.clear_requested.emit()
	_check(scene._selected.is_empty() and not scene._pinned, "clear selection resets inspector")
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with("--fixture="):
			_check(scene.bridge.load_world(argument.trim_prefix("--fixture=")), "scene loads real refined checkpoint")
			scene._refresh_world()
			_check(scene.terrain._settlements.multimesh.instance_count == 1, "saved settlement has one visible marker")
			var settlement: Dictionary = scene._frame.settlements[0]
			scene._read_sample(float(settlement.x_m), float(settlement.y_m))
			_check(scene._selected.observer_settlements.size() == 1, "inspector identifies saved settlement")
			_check(scene.terrain.origin_x_m < 0.0, "scene retains negative world origin")
	scene.queue_free()
	await process_frame
	print("WorldSim observer scene: %d failures" % _failures)
	quit(0 if _failures == 0 else 1)
