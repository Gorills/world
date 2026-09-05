extends SceneTree

var _failures: int = 0
var _checks: int = 0


func _check(condition: bool, message: String) -> void:
	_checks += 1
	if not condition:
		_failures += 1
		push_error(message)


func _compare_subset(actual: Dictionary, expected: Dictionary, context: String) -> void:
	for key: String in expected:
		_check(actual.has(key), "%s has %s" % [context, key])
		if not actual.has(key):
			continue
		var wanted: Variant = expected[key]
		if wanted is float or wanted is int:
			var tolerance: float = maxf(0.000001, absf(float(wanted)) * 0.0000001)
			_check(absf(float(actual[key]) - float(wanted)) <= tolerance,
				"%s.%s matches native core: %s versus %s" % [context, key, actual[key], wanted])
		else:
			_check(actual[key] == wanted, "%s.%s matches native core" % [context, key])


func _initialize() -> void:
	if not ClassDB.class_exists("WorldSimBridge"):
		push_error("WorldSimBridge is missing: build and import the GDExtension first")
		quit(1)
		return
	var fixture_path: String = ""
	var work_dir: String = OS.get_environment("TMPDIR")
	if work_dir.is_empty():
		work_dir = "/tmp" if OS.get_name() != "Windows" else OS.get_environment("TEMP")
	for argument: String in OS.get_cmdline_user_args():
		if argument.begins_with("--fixture="):
			fixture_path = argument.trim_prefix("--fixture=")
		elif argument.begins_with("--work-dir="):
			work_dir = argument.trim_prefix("--work-dir=")
	var checkpoint: String = work_dir.path_join("worldsim_bridge_%d.wsc" % OS.get_process_id())
	var observed_checkpoint: String = checkpoint + ".observed"
	var corrupt_checkpoint: String = checkpoint + ".corrupt"
	var bridge: Variant = ClassDB.instantiate("WorldSimBridge")
	_check(not bridge.is_ready(), "bridge begins without a world")
	_check(not bridge.advance_day(), "advance without world is rejected")
	_check(not bridge.get_last_error().is_empty(), "rejected command reports native error")
	_check(bridge.get_frame().is_empty(), "uninitialized frame is empty")
	_check(not bridge.create_world(-1), "negative seed is rejected")
	_check(bridge.create_world(), "default world can be created")
	_check(bridge.is_ready(), "successful creation publishes world")
	var frame: Dictionary = bridge.get_frame()
	_check(frame.day == 0 and frame.seed == "42", "default world begins at day zero with seed 42")
	_check(frame.materialized_patches == 0 and frame.refined_tiles == 0,
		"observer starts without sparse history")
	_check(frame.settlements.is_empty(), "observer does not invent settlements")
	_check(frame.cells.size() == frame.grid_width * frame.grid_height, "complete row-major L0 frame")
	for index: int in range(frame.cells.size()):
		var cell: Dictionary = frame.cells[index]
		_check(cell.cell_x == frame.min_cell_x + index % frame.grid_width and
			cell.cell_y == frame.min_cell_y + index / frame.grid_width, "frame coordinate order")
	_check(bridge.save_world(checkpoint), "native compound checkpoint can be saved")
	var original_bytes: PackedByteArray = FileAccess.get_file_as_bytes(checkpoint)
	_check(not original_bytes.is_empty(), "checkpoint is nonempty")
	var terrain: Dictionary = bridge.get_terrain(16)
	_check(terrain.resolution == 16 and terrain.heights.size() == 17 * 17, "terrain includes both mesh edges")
	var heights: PackedFloat32Array = terrain.heights
	for y: int in range(17):
		for x: int in range(17):
			# The endpoint lies on the exclusive world edge. The bridge samples
			# nextafter(edge, origin); a millimetre inset agrees at float precision.
			var x_m: float = terrain.origin_x_m + minf(terrain.width_m - 0.001, terrain.width_m * x / 16.0)
			var y_m: float = terrain.origin_y_m + minf(terrain.height_m - 0.001, terrain.height_m * y / 16.0)
			var point: Dictionary = bridge.sample_point(x_m, y_m)
			_check(absf(heights[y * 17 + x] - point.elevation_m) < 0.001, "terrain samples native elevation")
	_check(bridge.get_terrain(0).is_empty(), "zero terrain resolution is rejected")
	_check(bridge.get_terrain(257).is_empty(), "excessive terrain allocation is rejected")
	_check(bridge.sample_point(NAN, 0.0).is_empty(), "NaN sample is rejected")
	_check(bridge.sample_point(0.0, INF).is_empty(), "infinite sample is rejected")
	_check(bridge.sample_point(-1.0, 0.0).is_empty(), "outside sample is rejected")
	_check(bridge.sample_point(frame.width_m, 0.0).is_empty(), "exclusive far edge is rejected")
	_check(not bridge.save_world("relative.wsc"), "relative checkpoint path is rejected")
	_check(not bridge.load_world(checkpoint + ".missing"), "missing checkpoint is rejected")
	_check(not bridge.create_world(-123), "invalid replacement world is rejected")
	var detached: Dictionary = bridge.get_frame()
	detached.cells[0].soil_water_mm = -1000.0
	_check(bridge.get_frame().cells[0].soil_water_mm >= 0.0, "snapshot edits cannot mutate native state")
	_check(bridge.save_world(observed_checkpoint), "observed world can be serialized")
	_check(FileAccess.get_file_as_bytes(observed_checkpoint) == original_bytes,
		"reads, failed commands and detached edits leave checkpoint byte-identical")
	_check(bridge.advance_day(), "one daily command succeeds")
	var day_one: Dictionary = bridge.get_frame()
	_check(day_one.day == 1, "advance_day moves exactly one day")
	_check(day_one.totals != frame.totals, "daily command evolves the actual ecosystem")
	_check(day_one.materialized_patches == 0 and day_one.refined_tiles == 0,
		"stepping does not create observer history")
	_check(day_one.totals.terminal_outflow_m3_s == 0.0,
		"first-day runoff is stored, not reported as same-day river flow")
	_check(bridge.load_world(checkpoint), "compound checkpoint restores world")
	_check(bridge.get_frame() == frame, "load restores full visible frame exactly")
	_check(bridge.advance_day(), "restored world advances")
	_check(bridge.get_frame() == day_one, "restored evolution is deterministic")
	var bad_file: FileAccess = FileAccess.open(corrupt_checkpoint, FileAccess.WRITE)
	_check(bad_file != null, "corrupt checkpoint fixture can be written")
	if bad_file != null:
		bad_file.store_string("invalid checkpoint")
		bad_file.close()
		_check(not bridge.load_world(corrupt_checkpoint), "malformed checkpoint is rejected")
		_check(not bridge.get_last_error().is_empty(), "malformed checkpoint error survives for UI")
		_check(bridge.get_frame() == day_one, "failed load preserves current world")
	if not fixture_path.is_empty():
		_test_native_fixture(bridge, fixture_path, observed_checkpoint)
	for path: String in [checkpoint, observed_checkpoint, corrupt_checkpoint]:
		if FileAccess.file_exists(path):
			_check(DirAccess.remove_absolute(path) == OK, "temporary checkpoint removed")
	bridge = null
	print("WorldSimBridge: %d checks, %d failures" % [_checks, _failures])
	quit(0 if _failures == 0 else 1)


func _test_native_fixture(bridge: Variant, fixture_path: String, observed_checkpoint: String) -> void:
	_check(bridge.load_world(fixture_path), "loads independently generated refined checkpoint")
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(fixture_path + ".json"))
	_check(parsed is Dictionary, "native fixture truth JSON is valid")
	if not parsed is Dictionary:
		return
	var truth: Dictionary = parsed
	var frame: Dictionary = bridge.get_frame()
	_check(frame.day == truth.day, "fixture day restored")
	var totals: Dictionary = frame.totals
	var water_storage: float = totals.soil_water_m3 + totals.surface_water_m3 + totals.snow_water_m3 + totals.groundwater_m3 + totals.channel_storage_m3
	_check(absf(water_storage - float(truth.water_storage_m3)) < maxf(0.001, float(truth.water_storage_m3) * 1.0e-7),
		"observer sums actual coarse/refined stores with clipped areas, matching core water budget")
	_check(absf(float(totals.terminal_outflow_m3_s) - float(truth.terminal_outflow_m3_s)) < maxf(0.000001, float(truth.terminal_outflow_m3_s) * 1.0e-7),
		"outlet discharge matches completed core day without double-counting internal reaches")
	_compare_subset(frame, truth.bounds, "fixture bounds")
	_check(frame.materialized_patches == truth.counts.local_patches and
		frame.refined_tiles == truth.counts.refined_tiles and
		frame.settlements.size() == truth.counts.settlements, "fixture sparse owners restored")
	_check(frame.cells.size() == truth.frames.size(), "fixture has complete L0 snapshot")
	for index: int in range(mini(frame.cells.size(), truth.frames.size())):
		_compare_subset(frame.cells[index], truth.frames[index], "fixture cell %d" % index)
	for index: int in range(mini(frame.settlements.size(), truth.settlements.size())):
		_compare_subset(frame.settlements[index], truth.settlements[index], "fixture settlement")
	var point: Dictionary = bridge.sample_point(truth.point.x_m, truth.point.y_m)
	_compare_subset(point, truth.point, "fixture point")
	_check(point.water_resolution_m == 1024, "point observes refined regional water")
	_check(point.soil_water_mm > 0.0 and point.local_disturbance > 0.0,
		"fixture distinguishes actual refined state and local history from coarse zeros")
	_check(not bridge.get_terrain(32).is_empty(), "terrain supports negative origin and partial edges")
	_check(bridge.save_world(observed_checkpoint), "loaded fixture can be saved")
	_check(FileAccess.get_file_as_bytes(observed_checkpoint) == FileAccess.get_file_as_bytes(fixture_path),
		"observing loaded refined state preserves canonical checkpoint bytes")
	_check(bridge.advance_day(), "loaded refined world advances full simulation")
	_check(bridge.get_frame().day == truth.day + 1, "loaded refined world has one global clock")
	var before_oversized: Dictionary = bridge.get_frame()
	_check(not bridge.load_world(fixture_path + ".oversized.wsc"), "oversized but valid core checkpoint is rejected")
	_check("4096" in bridge.get_last_error(), "oversized checkpoint reports the observer limit")
	_check(bridge.get_frame() == before_oversized, "allocation limit preserves the active world")
