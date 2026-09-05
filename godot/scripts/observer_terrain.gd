class_name ObserverTerrain
extends Node3D
## Rendering only: terrain samples and ecological aggregates remain owned by C++.
## Coordinates are local km horizontally, with an explicitly labelled 8x relief.

const HORIZONTAL_SCALE: float = 0.001
const VERTICAL_SCALE: float = 0.008
const CELL_METERS: float = 8192.0
const LAYER_NAMES: Array[String] = ["Landscape", "Plant biomass", "Soil moisture", "Temperature", "Elevation", "River discharge", "Surface water", "Snow water", "Precipitation"]

var world_size := Vector2.ZERO
var origin_x_m: float = 0.0
var origin_y_m: float = 0.0
var sea_level_m: float = 0.0
var layer: int = 0
var show_grid: bool = false
var _resolution: int = 0
var _heights := PackedFloat32Array()
var _frame: Dictionary = {}
var _arrays: Array = []
var _surface := MeshInstance3D.new()
var _water := MeshInstance3D.new()
var _skirt := MeshInstance3D.new()
var _rivers := MeshInstance3D.new()
var _grid := MeshInstance3D.new()
var _selection := MeshInstance3D.new()
var _vegetation := MultiMeshInstance3D.new()
var _settlements := MultiMeshInstance3D.new()


func _ready() -> void:
	for item: Node3D in [_surface, _water, _skirt, _rivers, _grid, _selection, _vegetation, _settlements]:
		add_child(item)
	_surface.material_override = _material(Color.WHITE, false, true)
	_rivers.material_override = _material(Color.WHITE, true, true)
	_grid.material_override = _material(Color(0.7, 0.9, 0.86, 0.23), true)
	_selection.material_override = _material(Color("f3d595"), true)
	_vegetation.material_override = _material(Color.WHITE, false, true)
	_vegetation.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_settlements.material_override = _material(Color("f1cb85"), true)


func has_terrain() -> bool:
	return _resolution > 0 and not _heights.is_empty()


func load_terrain(data: Dictionary, frame: Dictionary) -> void:
	_resolution = int(data.get("resolution", 0))
	_heights = data.get("heights", PackedFloat32Array())
	origin_x_m = float(data.get("origin_x_m", 0.0))
	origin_y_m = float(data.get("origin_y_m", 0.0))
	world_size = Vector2(float(data.get("width_m", 0.0)), float(data.get("height_m", 0.0))) * HORIZONTAL_SCALE
	sea_level_m = float(data.get("sea_level_m", 0.0))
	_frame = frame
	_selection.mesh = null
	if not has_terrain() or _heights.size() != (_resolution + 1) * (_resolution + 1):
		return
	_build_surface()
	_build_water_and_skirt()
	_build_grid()
	update_frame(frame)


func update_frame(frame: Dictionary) -> void:
	_frame = frame
	if not has_terrain():
		return
	_recolor()
	_build_rivers()
	_build_vegetation()
	_build_settlements()


func set_layer(index: int) -> void:
	layer = clampi(index, 0, LAYER_NAMES.size() - 1)
	if has_terrain():
		_recolor()
	_vegetation.visible = layer == 0 or layer == 1


func set_grid(enabled: bool) -> void:
	show_grid = enabled
	_grid.visible = enabled


func world_x_m(local_x: float) -> float:
	return local_x / HORIZONTAL_SCALE + origin_x_m


func world_y_m(local_z: float) -> float:
	return local_z / HORIZONTAL_SCALE + origin_y_m


func local_point(x_m: float, y_m: float) -> Vector3:
	var x := (x_m - origin_x_m) * HORIZONTAL_SCALE
	var z := (y_m - origin_y_m) * HORIZONTAL_SCALE
	return Vector3(x, height_at(x, z), z)


func height_at(x: float, z: float) -> float:
	if not has_terrain():
		return sea_level_m * VERTICAL_SCALE
	var u := clampf(x / world_size.x * _resolution, 0.0, float(_resolution))
	var v := clampf(z / world_size.y * _resolution, 0.0, float(_resolution))
	var ix := mini(int(u), _resolution - 1)
	var iz := mini(int(v), _resolution - 1)
	var stride := _resolution + 1
	var a := lerpf(_heights[iz * stride + ix], _heights[iz * stride + ix + 1], u - ix)
	var b := lerpf(_heights[(iz + 1) * stride + ix], _heights[(iz + 1) * stride + ix + 1], u - ix)
	return maxf(lerpf(a, b, v - iz), sea_level_m) * VERTICAL_SCALE


func pick(ray_origin: Vector3, direction: Vector3) -> Variant:
	if not has_terrain():
		return null
	var step := maxf(world_size.x, world_size.y) / 300.0
	var previous_t := 0.0
	for i in range(1800):
		var t := i * step
		var point := ray_origin + direction * t
		if point.x < 0.0 or point.z < 0.0 or point.x >= world_size.x or point.z >= world_size.y:
			previous_t = t
			continue
		if point.y <= height_at(point.x, point.z):
			var low := previous_t
			var high := t
			for refinement in range(12):
				var mid := (low + high) * 0.5
				var sample := ray_origin + direction * mid
				if sample.y > height_at(sample.x, sample.z):
					low = mid
				else:
					high = mid
			var hit := ray_origin + direction * high
			hit.x = clampf(hit.x, 0.001, world_size.x - 0.001)
			hit.z = clampf(hit.z, 0.001, world_size.y - 0.001)
			return Vector3(hit.x, height_at(hit.x, hit.z), hit.z)
		previous_t = t
	return null


func select_cell(cell: Dictionary) -> void:
	if cell.is_empty():
		_selection.mesh = null
		return
	var x0 := maxf(0.0, (float(cell.get("cell_x", 0)) * CELL_METERS - origin_x_m) * HORIZONTAL_SCALE)
	var z0 := maxf(0.0, (float(cell.get("cell_y", 0)) * CELL_METERS - origin_y_m) * HORIZONTAL_SCALE)
	var x1 := minf(world_size.x, (float(cell.get("cell_x", 0)) * CELL_METERS + CELL_METERS - origin_x_m) * HORIZONTAL_SCALE)
	var z1 := minf(world_size.y, (float(cell.get("cell_y", 0)) * CELL_METERS + CELL_METERS - origin_y_m) * HORIZONTAL_SCALE)
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	_ribbon(st, Vector2(x0, z0), Vector2(x1, z0), 0.08, Color.WHITE, 20)
	_ribbon(st, Vector2(x1, z0), Vector2(x1, z1), 0.08, Color.WHITE, 20)
	_ribbon(st, Vector2(x1, z1), Vector2(x0, z1), 0.08, Color.WHITE, 20)
	_ribbon(st, Vector2(x0, z1), Vector2(x0, z0), 0.08, Color.WHITE, 20)
	_selection.mesh = st.commit()


func _material(color: Color, unshaded: bool = false, vertex_color: bool = false) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.albedo_color = color
	material.vertex_color_use_as_albedo = vertex_color
	material.vertex_color_is_srgb = true
	material.roughness = 0.93
	material.cull_mode = BaseMaterial3D.CULL_DISABLED
	if unshaded:
		material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	if color.a < 1.0:
		material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	return material


func _build_surface() -> void:
	var vertices := PackedVector3Array()
	var normals := PackedVector3Array()
	var indices := PackedInt32Array()
	var stride := _resolution + 1
	var dx := world_size.x / _resolution
	var dz := world_size.y / _resolution
	for z in range(stride):
		for x in range(stride):
			vertices.append(Vector3(x * dx, _heights[z * stride + x] * VERTICAL_SCALE, z * dz))
			var left := _heights[z * stride + maxi(0, x - 1)]
			var right := _heights[z * stride + mini(_resolution, x + 1)]
			var up := _heights[maxi(0, z - 1) * stride + x]
			var down := _heights[mini(_resolution, z + 1) * stride + x]
			normals.append(Vector3(-(right - left) * VERTICAL_SCALE / (2.0 * dx), 1.0, -(down - up) * VERTICAL_SCALE / (2.0 * dz)).normalized())
	for z in range(_resolution):
		for x in range(_resolution):
			var a := z * stride + x
			indices.append_array(PackedInt32Array([a, a + 1, a + stride, a + 1, a + stride + 1, a + stride]))
	_arrays.resize(Mesh.ARRAY_MAX)
	_arrays[Mesh.ARRAY_VERTEX] = vertices
	_arrays[Mesh.ARRAY_NORMAL] = normals
	_arrays[Mesh.ARRAY_INDEX] = indices


func _cell_at(x: float, z: float) -> Dictionary:
	var gx := int(floor((x / HORIZONTAL_SCALE + origin_x_m) / CELL_METERS)) - int(_frame.get("min_cell_x", 0))
	var gz := int(floor((z / HORIZONTAL_SCALE + origin_y_m) / CELL_METERS)) - int(_frame.get("min_cell_y", 0))
	var width := int(_frame.get("grid_width", 0))
	var height := int(_frame.get("grid_height", 0))
	var cells: Array = _frame.get("cells", [])
	if width == 0 or height == 0 or cells.is_empty():
		return {}
	return cells[clampi(gz, 0, height - 1) * width + clampi(gx, 0, width - 1)]


func _recolor() -> void:
	var colors := PackedColorArray()
	var vertices: PackedVector3Array = _arrays[Mesh.ARRAY_VERTEX]
	for vertex: Vector3 in vertices:
		colors.append(_color_for(_cell_at(vertex.x, vertex.z), vertex.y / VERTICAL_SCALE))
	_arrays[Mesh.ARRAY_COLOR] = colors
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, _arrays)
	_surface.mesh = mesh


func _color_for(cell: Dictionary, elevation: float) -> Color:
	if elevation < sea_level_m:
		return Color("28505a")
	var plants := float(cell.get("grass_carbon", 0.0)) + float(cell.get("shrub_carbon", 0.0)) + float(cell.get("tree_carbon", 0.0))
	match layer:
		1:
			return Color("c0a478").lerp(Color("3c966f"), clampf(plants / 3.0, 0.0, 1.0)).lerp(Color("164d49"), clampf((plants - 3.0) / 8.0, 0.0, 1.0))
		2:
			return Color("b98456").lerp(Color("8ccbb4"), clampf(float(cell.get("soil_saturation", 0.0)), 0.0, 1.0))
		3:
			var temperature := float(cell.get("temperature_c", 0.0))
			return Color("528ec3").lerp(Color("e1dbb0"), clampf((temperature + 20.0) / 35.0, 0.0, 1.0)).lerp(Color("bc6147"), clampf((temperature - 15.0) / 25.0, 0.0, 1.0))
		4:
			return Color("4f927a").lerp(Color("bba983"), clampf((elevation - sea_level_m) / 1100.0, 0.0, 1.0)).lerp(Color("e2e7de"), clampf((elevation - sea_level_m - 1100.0) / 1400.0, 0.0, 1.0))
		5:
			return Color("303f42").lerp(Color("5dcfe4"), _discharge_scale(float(cell.get("channel_discharge_m3_s", 0.0))))
		6:
			return Color("b98456").lerp(Color("4fbcd7"), clampf(log(1.0 + float(cell.get("surface_water_mm", 0.0))) / log(101.0), 0.0, 1.0))
		7:
			return Color("516b61").lerp(Color("e8f3fa"), clampf(float(cell.get("snow_water_mm", 0.0)) / 200.0, 0.0, 1.0))
		8:
			return Color("bfa477").lerp(Color("5e92d1"), clampf(float(cell.get("precipitation_mm", 0.0)) / 20.0, 0.0, 1.0))
	var color := Color("a6b280").lerp(Color("527b57"), clampf(plants / 5.0, 0.0, 1.0))
	color = color.lerp(Color("a0a38e"), clampf((elevation - sea_level_m - 800.0) / 1600.0, 0.0, 0.8))
	color = color.lerp(Color("e1e8df"), clampf(float(cell.get("snow_water_mm", 0.0)) / 160.0, 0.0, 0.85))
	if elevation - sea_level_m < 22.0:
		color = color.lerp(Color("c5c2a0"), 0.6)
	return color


func _build_water_and_skirt() -> void:
	var plane := PlaneMesh.new()
	plane.size = world_size
	_water.mesh = plane
	_water.position = Vector3(world_size.x / 2.0, sea_level_m * VERTICAL_SCALE + 0.025, world_size.y / 2.0)
	var water_material := _material(Color("327585"))
	water_material.metallic = 0.15
	water_material.roughness = 0.28
	_water.material_override = water_material
	var boundary: Array[Vector3] = []
	var stride := _resolution + 1
	var vertices: PackedVector3Array = _arrays[Mesh.ARRAY_VERTEX]
	for i in range(stride):
		boundary.append(vertices[i])
	for i in range(1, stride):
		boundary.append(vertices[i * stride + _resolution])
	for i in range(_resolution - 1, -1, -1):
		boundary.append(vertices[_resolution * stride + i])
	for i in range(_resolution - 1, 0, -1):
		boundary.append(vertices[i * stride])
	var lowest := sea_level_m
	for height: float in _heights:
		lowest = minf(lowest, height)
	var bottom := minf(lowest * VERTICAL_SCALE - 1.0, sea_level_m * VERTICAL_SCALE - 3.0)
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	for i in range(boundary.size()):
		var a: Vector3 = boundary[i]
		var b: Vector3 = boundary[(i + 1) % boundary.size()]
		a.y = maxf(a.y, sea_level_m * VERTICAL_SCALE)
		b.y = maxf(b.y, sea_level_m * VERTICAL_SCALE)
		for point: Vector3 in [a, b, Vector3(a.x, bottom, a.z), b, Vector3(b.x, bottom, b.z), Vector3(a.x, bottom, a.z)]:
			st.add_vertex(point)
	st.generate_normals()
	_skirt.mesh = st.commit()
	_skirt.material_override = _material(Color("273d3e"))


func _ribbon(st: SurfaceTool, start: Vector2, end: Vector2, width: float, color: Color, segments: int = 12) -> void:
	var direction := (end - start).normalized()
	var perpendicular := Vector2(-direction.y, direction.x) * width
	for i in range(segments):
		var a := start.lerp(end, float(i) / segments)
		var b := start.lerp(end, float(i + 1) / segments)
		for point: Vector2 in [a - perpendicular, a + perpendicular, b - perpendicular, a + perpendicular, b + perpendicular, b - perpendicular]:
			st.set_color(color)
			st.add_vertex(Vector3(point.x, height_at(point.x, point.y) + 0.07, point.y))


func _discharge_scale(discharge: float) -> float:
	# Fixed logarithmic legend, not auto-normalization: comparable across days.
	return clampf(log(1.0 + maxf(discharge, 0.0)) / log(1001.0), 0.0, 1.0)


func _build_rivers() -> void:
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var count := 0
	for cell: Dictionary in _frame.get("cells", []):
		var storage := float(cell.get("channel_storage_m3", 0.0))
		var discharge := float(cell.get("channel_discharge_m3_s", 0.0))
		if bool(cell.get("ocean", false)) or not bool(cell.get("has_downstream", false)):
			continue
		# Static drainage potential is not evidence of present water.
		if storage <= 1.0 and discharge <= 0.0001:
			continue
		var start := Vector2((float(cell.get("x_m", 0.0)) - origin_x_m) * HORIZONTAL_SCALE, (float(cell.get("y_m", 0.0)) - origin_y_m) * HORIZONTAL_SCALE)
		var end := Vector2((float(cell.get("downstream_x_m", 0.0)) - origin_x_m) * HORIZONTAL_SCALE, (float(cell.get("downstream_y_m", 0.0)) - origin_y_m) * HORIZONTAL_SCALE)
		start = start.clamp(Vector2.ZERO, world_size)
		end = end.clamp(Vector2.ZERO, world_size)
		var amount := _discharge_scale(discharge)
		_ribbon(st, start, end, 0.035 + amount * 0.30, Color("91b7b7").lerp(Color("5dcfe4"), amount))
		# Direction glyphs describe the daily routed flow; no invented velocity.
		if discharge > 0.0001 and start.distance_to(end) > 0.1:
			var direction := (end - start).normalized()
			var side := Vector2(-direction.y, direction.x)
			var tip := start.lerp(end, 0.6)
			var arrow_size := minf(0.6, start.distance_to(end) * 0.15)
			for sign_value: float in [-1.0, 1.0]:
				_ribbon(st, tip - direction * arrow_size + side * sign_value * arrow_size * 0.5, tip, 0.055, Color("c7f2f6"))
		count += 1
	_rivers.mesh = st.commit() if count > 0 else null


func _build_grid() -> void:
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var cells_x := int(_frame.get("grid_width", 0))
	var cells_y := int(_frame.get("grid_height", 0))
	for i in range(cells_x + 1):
		var x := clampf(((int(_frame.get("min_cell_x", 0)) + i) * CELL_METERS - origin_x_m) * HORIZONTAL_SCALE, 0.0, world_size.x)
		_ribbon(st, Vector2(x, 0.0), Vector2(x, world_size.y), 0.018, Color.WHITE, 128)
	for i in range(cells_y + 1):
		var z := clampf(((int(_frame.get("min_cell_y", 0)) + i) * CELL_METERS - origin_y_m) * HORIZONTAL_SCALE, 0.0, world_size.y)
		_ribbon(st, Vector2(0.0, z), Vector2(world_size.x, z), 0.018, Color.WHITE, 128)
	_grid.mesh = st.commit()
	_grid.visible = show_grid


func _build_vegetation() -> void:
	# Each stand is a cartographic glyph for aggregate tree carbon, never an agent.
	var transforms: Array[Transform3D] = []
	var colors: Array[Color] = []
	var world_seed := str(_frame.get("seed", "42")).hash()
	for cell: Dictionary in _frame.get("cells", []):
		var tree_density := float(cell.get("tree_carbon", 0.0))
		if bool(cell.get("ocean", false)) or tree_density <= 0.02:
			continue
		var rng := RandomNumberGenerator.new()
		rng.seed = world_seed + int(cell.get("cell_x", 0)) * 73856093 + int(cell.get("cell_y", 0)) * 19349663
		var center := Vector2((float(cell.get("x_m", 0.0)) - origin_x_m) * HORIZONTAL_SCALE, (float(cell.get("y_m", 0.0)) - origin_y_m) * HORIZONTAL_SCALE)
		var density_scale := clampf(tree_density / 5.0, 0.05, 1.0)
		for glyph in range(5):
			var point := center + Vector2(rng.randf_range(-2.6, 2.6), rng.randf_range(-2.6, 2.6))
			if point.x <= 0.0 or point.y <= 0.0 or point.x >= world_size.x or point.y >= world_size.y:
				continue
			var y := height_at(point.x, point.y)
			if y <= sea_level_m * VERTICAL_SCALE + 0.1:
				continue
			var size := 0.35 + sqrt(density_scale) * 0.65
			var basis := Basis.IDENTITY.scaled(Vector3(size, 0.3 + density_scale * 1.5, size))
			transforms.append(Transform3D(basis, Vector3(point.x, y + (0.3 + density_scale * 1.5) * 0.5, point.y)))
			colors.append(Color("27574d").lerp(Color("497254"), rng.randf()))
	var cone := CylinderMesh.new()
	cone.top_radius = 0.0
	cone.bottom_radius = 0.55
	cone.height = 1.0
	cone.radial_segments = 5
	var multimesh := MultiMesh.new()
	multimesh.transform_format = MultiMesh.TRANSFORM_3D
	multimesh.use_colors = true
	multimesh.mesh = cone
	multimesh.instance_count = transforms.size()
	for i in range(transforms.size()):
		multimesh.set_instance_transform(i, transforms[i])
		multimesh.set_instance_color(i, colors[i])
	_vegetation.multimesh = multimesh
	_vegetation.visible = layer == 0 or layer == 1


func _build_settlements() -> void:
	# A marker denotes an actual settlement's regional center, not building geometry.
	var settlements: Array = _frame.get("settlements", [])
	var marker := SphereMesh.new()
	marker.radius = 0.45
	marker.height = 0.9
	marker.radial_segments = 8
	marker.rings = 4
	var multimesh := MultiMesh.new()
	multimesh.transform_format = MultiMesh.TRANSFORM_3D
	multimesh.mesh = marker
	multimesh.instance_count = settlements.size()
	for i in range(settlements.size()):
		var settlement: Dictionary = settlements[i]
		var point := local_point(float(settlement.get("x_m", 0.0)), float(settlement.get("y_m", 0.0)))
		point.y += 0.9
		multimesh.set_instance_transform(i, Transform3D(Basis.IDENTITY, point))
	_settlements.multimesh = multimesh
