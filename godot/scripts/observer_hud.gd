class_name ObserverHUD
extends CanvasLayer
## UI consumes snapshots and emits commands; it never owns simulation state.

signal pause_requested
signal step_requested
signal fast_forward_requested(days: int)
signal speed_changed(days_per_second: float)
signal layer_changed(index: int)
signal grid_changed(enabled: bool)
signal seed_requested(seed_value: int)
signal save_requested
signal load_requested
signal overview_requested
signal focus_requested
signal clear_requested

const INK := Color("e8eee8")
const MUTED := Color("a1b6b8")
const ACCENT := Color("9ed5bd")
const HistoryPlot = preload("res://scripts/observer_history.gd")
var _root: Control
var _day: Label
var _date: Label
var _state: Label
var _seed: Label
var _pause: Button
var _inspector: RichTextLabel
var _cell_title: Label
var _totals: Label
var _legend: Label
var _coordinates: Label
var _message: Label
var _seed_input: LineEdit
var _error_panel: PanelContainer
var _error_text: Label
var _gradient: TextureRect
var _focus: Button
var _clear: Button
var _progress: Label
var _history: ObserverHistory
var _plant_change: Label
var _is_paused: bool = false
var _advancing: bool = false
var _status_timeout: float = 0.0


func _ready() -> void:
	_root = Control.new()
	_root.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_root)
	_root.theme = _make_theme()
	_build_header()
	_build_sidebar()
	_build_footer()
	_build_error()
	set_map_layer(0)
	show_sample({}, false)


func _make_theme() -> Theme:
	var theme := Theme.new()
	theme.default_font_size = 14
	theme.set_color("font_color", "Label", INK)
	theme.set_color("default_color", "RichTextLabel", MUTED)
	theme.set_color("font_color", "Button", INK)
	theme.set_color("font_hover_color", "Button", Color.WHITE)
	theme.set_color("font_color", "CheckBox", MUTED)
	theme.set_color("font_color", "LineEdit", INK)
	for control_name in ["Button", "OptionButton", "LineEdit"]:
		theme.set_stylebox("normal", control_name, _box(Color("21353d"), 7, 8, Color("3a5157")))
		theme.set_stylebox("hover", control_name, _box(Color("304c52"), 7, 8, ACCENT))
		theme.set_stylebox("pressed", control_name, _box(Color("3e625f"), 7, 8, ACCENT))
		theme.set_stylebox("focus", control_name, _box(Color(0, 0, 0, 0), 7, 8, ACCENT))
	theme.set_stylebox("panel", "PopupMenu", _box(Color("182d35"), 8, 8))
	theme.set_color("font_color", "PopupMenu", INK)
	theme.set_color("font_hover_color", "PopupMenu", Color.WHITE)
	theme.set_constant("separation", "VBoxContainer", 9)
	theme.set_constant("separation", "HBoxContainer", 8)
	return theme


func _box(color: Color, radius: int = 12, padding: int = 18, border: Color = Color.TRANSPARENT) -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = color
	style.set_corner_radius_all(radius)
	style.content_margin_left = padding
	style.content_margin_right = padding
	style.content_margin_top = padding
	style.content_margin_bottom = padding
	style.set_border_width_all(1 if border.a > 0.0 else 0)
	style.border_color = border
	return style


func _label(text: String, size: int = 14, color: Color = INK) -> Label:
	var label := Label.new()
	label.text = text
	label.add_theme_font_size_override("font_size", size)
	label.add_theme_color_override("font_color", color)
	label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	return label


func _button(text: String, action: Callable, tooltip: String = "") -> Button:
	var button := Button.new()
	button.text = text
	button.tooltip_text = tooltip
	button.focus_mode = Control.FOCUS_NONE
	button.pressed.connect(action)
	return button


func _build_header() -> void:
	var panel := PanelContainer.new()
	panel.position = Vector2(24, 24)
	panel.custom_minimum_size = Vector2(314, 0)
	panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
	panel.add_theme_stylebox_override("panel", _box(Color(0.055, 0.105, 0.13, 0.94), 12, 20, Color("365258")))
	_root.add_child(panel)
	var column := VBoxContainer.new()
	column.mouse_filter = Control.MOUSE_FILTER_IGNORE
	panel.add_child(column)
	column.add_child(_label("W O R L D S I M     /     0 1", 12, ACCENT))
	column.add_child(_label("Observatory", 34))
	column.add_child(_label("TERRAIN  /  WATER  /  ECOLOGY", 11, MUTED))
	_state = _label("CONNECTING TO WORLD", 11, ACCENT)
	column.add_child(_state)


func _section(parent: VBoxContainer, text: String) -> void:
	var separator := HSeparator.new()
	separator.modulate = Color("40575c")
	parent.add_child(separator)
	parent.add_child(_label(text, 11, ACCENT))


func _build_sidebar() -> void:
	var panel := PanelContainer.new()
	panel.set_anchors_and_offsets_preset(Control.PRESET_RIGHT_WIDE)
	panel.offset_left = -354
	panel.offset_right = -18
	panel.offset_top = 18
	panel.offset_bottom = -18
	panel.add_theme_stylebox_override("panel", _box(Color(0.04, 0.08, 0.10, 0.97), 14, 18, Color("344e53")))
	_root.add_child(panel)
	var scroll := ScrollContainer.new()
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	panel.add_child(scroll)
	var column := VBoxContainer.new()
	column.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	column.add_theme_constant_override("separation", 5)
	scroll.add_child(column)
	column.add_child(_label("SIMULATION CLOCK", 11, ACCENT))
	var clock_row := HBoxContainer.new()
	column.add_child(clock_row)
	_day = _label("Day —", 32)
	_day.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	clock_row.add_child(_day)
	_date = _label("Year —", 13, MUTED)
	clock_row.add_child(_date)
	_seed = _label("Seed — · waiting for world", 12, MUTED)
	column.add_child(_seed)
	var playback := HBoxContainer.new()
	column.add_child(playback)
	_pause = _button("Pause", func() -> void: pause_requested.emit(), "Space · pause or resume")
	_pause.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	playback.add_child(_pause)
	playback.add_child(_button("+1 day", func() -> void: step_requested.emit(), "Pause and advance one exact simulation day"))
	var speed := OptionButton.new()
	speed.focus_mode = Control.FOCUS_NONE
	for value in ["1 d/s", "16 d/s", "64 d/s", "365 d/s"]:
		speed.add_item(value)
	speed.select(1)
	speed.tooltip_text = "Target simulation days per real second. Actual speed is limited by the time needed to compute a day."
	speed.item_selected.connect(func(index: int) -> void: speed_changed.emit([1.0, 16.0, 64.0, 365.0][index]))
	playback.add_child(speed)
	column.add_child(_label("Speed target · actual rate depends on hardware", 11, MUTED))
	var advance_row := HBoxContainer.new()
	column.add_child(advance_row)
	for days: int in [30, 365]:
		var advance_button := _button("+%d days" % days, func() -> void: fast_forward_requested.emit(days), "Queue exact simulation days; the observer remains responsive. Pause cancels the remaining queue.")
		advance_button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		advance_row.add_child(advance_button)
	_progress = _label("", 12, ACCENT)
	_progress.visible = false
	column.add_child(_progress)
	_section(column, "OBSERVED CHANGE")
	_plant_change = _label("Awaiting observation history", 12, MUTED)
	_plant_change.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(_plant_change)
	_history = HistoryPlot.new()
	column.add_child(_history)
	_section(column, "OBSERVATION LAYER")
	var layers := OptionButton.new()
	layers.focus_mode = Control.FOCUS_NONE
	for layer_name: String in ObserverTerrain.LAYER_NAMES:
		layers.add_item(layer_name)
	layers.item_selected.connect(func(index: int) -> void: layer_changed.emit(index))
	column.add_child(layers)
	_gradient = TextureRect.new()
	_gradient.custom_minimum_size = Vector2(0, 8)
	_gradient.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	column.add_child(_gradient)
	_legend = _label("", 12, MUTED)
	_legend.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(_legend)
	var grid := CheckBox.new()
	grid.text = "Show L0 grid · 8.192 km"
	grid.focus_mode = Control.FOCUS_NONE
	grid.toggled.connect(func(enabled: bool) -> void: grid_changed.emit(enabled))
	column.add_child(grid)
	_section(column, "CELL INSPECTOR")
	_cell_title = _label("Point at the landscape", 16)
	column.add_child(_cell_title)
	_inspector = RichTextLabel.new()
	_inspector.bbcode_enabled = true
	_inspector.fit_content = true
	_inspector.scroll_active = false
	_inspector.custom_minimum_size = Vector2(0, 170)
	_inspector.add_theme_font_size_override("normal_font_size", 13)
	column.add_child(_inspector)
	var inspect_buttons := HBoxContainer.new()
	column.add_child(inspect_buttons)
	_focus = _button("Fly to cell", func() -> void: focus_requested.emit())
	_focus.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	inspect_buttons.add_child(_focus)
	_clear = _button("Unpin", func() -> void: clear_requested.emit())
	inspect_buttons.add_child(_clear)
	_section(column, "WORLD TOTALS")
	_totals = _label("Awaiting world state…", 13, MUTED)
	column.add_child(_totals)
	_section(column, "WORLD CHECKPOINT")
	var seed_row := HBoxContainer.new()
	column.add_child(seed_row)
	_seed_input = LineEdit.new()
	_seed_input.text = "42"
	_seed_input.placeholder_text = "World seed"
	_seed_input.tooltip_text = "Signed 64-bit integer seed; New world replaces the current session"
	_seed_input.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	seed_row.add_child(_seed_input)
	seed_row.add_child(_button("New world", _request_seed))
	var saves := HBoxContainer.new()
	column.add_child(saves)
	for button: Button in [_button("Save", func() -> void: save_requested.emit()), _button("Load", func() -> void: load_requested.emit()), _button("Overview", func() -> void: overview_requested.emit(), "F / Home")]:
		button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		saves.add_child(button)
	var checkpoint_label := _label("user://observer.wsc · quick checkpoint", 11, MUTED)
	checkpoint_label.mouse_filter = Control.MOUSE_FILTER_PASS
	checkpoint_label.tooltip_text = ProjectSettings.globalize_path("user://observer.wsc")
	column.add_child(checkpoint_label)
	_message = _label("", 12, ACCENT)
	_message.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(_message)


func _build_footer() -> void:
	var panel := PanelContainer.new()
	panel.set_anchors_and_offsets_preset(Control.PRESET_BOTTOM_LEFT)
	panel.offset_left = 24
	panel.offset_right = 677
	panel.offset_top = -108
	panel.offset_bottom = -24
	panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
	panel.add_theme_stylebox_override("panel", _box(Color(0.04, 0.085, 0.105, 0.94), 10, 14, Color("365258")))
	_root.add_child(panel)
	var column := VBoxContainer.new()
	column.mouse_filter = Control.MOUSE_FILTER_IGNORE
	column.add_theme_constant_override("separation", 5)
	panel.add_child(column)
	_coordinates = _label("FREE OBSERVER   ·   terrain relief ×8   ·   horizontal units km", 12, ACCENT)
	column.add_child(_coordinates)
	column.add_child(_label("W A S D  move     Q / E  descend / ascend     RMB  look     Shift  faster", 12))
	column.add_child(_label("Click  pin cell     Wheel  flight speed     F  overview     Space  pause", 12, MUTED))


func _build_error() -> void:
	_error_panel = PanelContainer.new()
	_error_panel.set_anchors_and_offsets_preset(Control.PRESET_CENTER)
	_error_panel.offset_left = -380
	_error_panel.offset_right = 380
	_error_panel.offset_top = -170
	_error_panel.offset_bottom = 170
	_error_panel.add_theme_stylebox_override("panel", _box(Color("132730"), 14, 28, Color("b9a775")))
	_root.add_child(_error_panel)
	_error_text = _label("", 17)
	_error_text.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_error_panel.add_child(_error_text)
	_error_panel.hide()


func _request_seed() -> void:
	var value := _seed_input.text.strip_edges()
	if not value.is_valid_int():
		show_message("Seed must be a signed 64-bit integer.", true)
		return
	_seed_input.release_focus()
	seed_requested.emit(value.to_int())


func show_frame(frame: Dictionary, paused: bool) -> void:
	var day := int(frame.get("day", 0))
	_day.text = "Day %s" % day
	_date.text = "Year %d\nDay %d / 365" % [day / 365 + 1, day % 365 + 1]
	_seed.text = "Seed %s · %.0f × %.0f km" % [str(frame.get("seed", "—")), float(frame.get("width_m", 0.0)) / 1000.0, float(frame.get("height_m", 0.0)) / 1000.0]
	var totals: Dictionary = frame.get("totals", {})
	_history.record(day, float(totals.get("plant_carbon_kg", 0.0)) / 1000.0, float(totals.get("terminal_outflow_m3_s", 0.0)))
	_plant_change.text = _history.plant_change_text()
	_totals.text = "Plants        %s t C\nHerbivores    %s t C\nCarnivores    %s t C\nChannel water %s m³" % [_number(float(totals.get("plant_carbon_kg", 0.0)) / 1000.0), _number(float(totals.get("herbivore_carbon_kg", 0.0)) / 1000.0), _number(float(totals.get("carnivore_carbon_kg", 0.0)) / 1000.0), _number(float(totals.get("channel_storage_m3", 0.0)))]
	_totals.text += "\nSoil water    %s m³\nGroundwater   %s m³\nSurface water %s m³\nSnow water    %s m³" % [_number(float(totals.get("soil_water_m3", 0.0))), _number(float(totals.get("groundwater_m3", 0.0))), _number(float(totals.get("surface_water_m3", 0.0))), _number(float(totals.get("snow_water_m3", 0.0)))]
	_totals.text += "\nOutlet flow   %s m³/s\nMax reach Q   %s m³/s\n(flows: last completed day)" % [_number(float(totals.get("terminal_outflow_m3_s", 0.0))), _number(float(totals.get("max_channel_discharge_m3_s", 0.0)))]
	var settlements: Array = frame.get("settlements", [])
	if not settlements.is_empty():
		var population := 0.0
		for settlement: Dictionary in settlements:
			population += float(settlement.get("population", 0.0))
		_totals.text += "\nSettlements %d · population %s" % [settlements.size(), _number(population)]
	set_paused(paused)


func reset_history() -> void:
	_history.reset_history()
	_plant_change.text = "Awaiting observation history"
	show_progress(0)


func show_progress(remaining: int) -> void:
	_advancing = remaining > 0
	_progress.visible = remaining > 0
	_progress.text = "Advancing · %d days remaining" % remaining
	set_paused(_is_paused)


func set_paused(paused: bool) -> void:
	_is_paused = paused
	if _advancing:
		_pause.text = "Cancel"
		_pause.tooltip_text = "Space · cancel remaining queued days and stay paused"
		_state.text = "ADVANCING  ·  OBSERVING"
		_state.add_theme_color_override("font_color", ACCENT)
		return
	_pause.text = "Resume" if paused else "Pause"
	_pause.tooltip_text = "Space · pause or resume"
	_state.text = "PAUSED  ·  OBSERVATION" if paused else "LIVE WORLD  ·  OBSERVING"
	_state.add_theme_color_override("font_color", Color("e5cb91") if paused else ACCENT)


func set_map_layer(index: int) -> void:
	var gradients: Array[PackedColorArray] = [
		PackedColorArray([Color("a6b280"), Color("527b57"), Color("a0a38e"), Color("e1e8df")]),
		PackedColorArray([Color("c0a478"), Color("3c966f"), Color("164d49")]),
		PackedColorArray([Color("b98456"), Color("8ccbb4")]),
		PackedColorArray([Color("528ec3"), Color("e1dbb0"), Color("bc6147")]),
		PackedColorArray([Color("4f927a"), Color("bba983"), Color("e2e7de")]),
		PackedColorArray([Color("303f42"), Color("5dcfe4")]),
		PackedColorArray([Color("b98456"), Color("4fbcd7")]),
		PackedColorArray([Color("516b61"), Color("e8f3fa")]),
		PackedColorArray([Color("bfa477"), Color("5e92d1")]),
	]
	var descriptions: Array[String] = [
		"Live plant cover · terrain · snow\nStand symbols = L0 tree biomass, not individuals.\nWet reaches: width follows log(1 + Q); arrows show drainage direction, not velocity.",
		"Plant carbon: 0 → 3 → 11+ kg C/m²\nGrass + shrub + tree biomass, L0 aggregates.",
		"Soil saturation: 0% dry → 100% saturated\nL0 equivalent; inspector samples refined water.",
		"Daily air temperature: −20 → 15 → 40 °C\nCurrent L0 weather, not a climate average.",
		"Height above sea datum: 0 → 1100 → 2500 m\nReal terrain samples; vertical scale ×8.",
		"River Q: 0 → 1000+ m³/s · log(1 + Q)\nMean outflow over last completed day.\nReach widths are schematic, not physical river widths.",
		"Surface water: 0 → 100+ mm · log(1 + mm)\nEquivalent depth across an L0 cell; not local flood depth.",
		"Snow water equivalent: 0 → 200+ mm\nWater stored as snow, not snowpack depth.",
		"Daily precipitation: 0 → 20+ mm/d\nRain + snow water equivalent, L0 weather.",
	]
	index = clampi(index, 0, gradients.size() - 1)
	var gradient := Gradient.new()
	gradient.colors = gradients[index]
	var offsets := PackedFloat32Array()
	for i in range(gradients[index].size()):
		offsets.append(float(i) / (gradients[index].size() - 1))
	gradient.offsets = offsets
	var texture := GradientTexture1D.new()
	texture.gradient = gradient
	_gradient.texture = texture
	_legend.text = descriptions[index]


func show_sample(sample: Dictionary, pinned: bool) -> void:
	_focus.disabled = sample.is_empty()
	_clear.disabled = not pinned
	if sample.is_empty():
		_cell_title.text = "Point at the landscape"
		_inspector.text = "Hover over the world to read its state.\nClick to pin a cell and follow it over time.\n\nThe camera observes without creating local patches.\n\nAnimal guilds are carbon biomass pools; there are no individual animal agents."
		return
	_cell_title.text = "L0  [%d, %d]  %s" % [int(sample.get("cell_x", 0)), int(sample.get("cell_y", 0)), "· PINNED" if pinned else "· HOVER"]
	var water_scale := "L1 refined" if bool(sample.get("water_refined", false)) else "L0 coarse"
	_inspector.text = "[color=#e8eee8]%.2f, %.2f km   ·   %.0f m elevation[/color]\n" % [float(sample.get("x_m", 0.0)) / 1000.0, float(sample.get("y_m", 0.0)) / 1000.0, float(sample.get("elevation_m", 0.0))]
	_inspector.text += "%s · water %s\n" % ["Ocean" if bool(sample.get("ocean", false)) else "Land", water_scale]
	_inspector.text += "Temperature  [color=#e8eee8]%.1f °C[/color]\nPrecipitation  [color=#e8eee8]%.1f mm/d[/color] (rain + snow)\n" % [float(sample.get("temperature_c", 0.0)), float(sample.get("precipitation_mm", 0.0))]
	_inspector.text += "Soil  [color=#e8eee8]%.1f mm · %.0f%%[/color]\n" % [float(sample.get("soil_water_mm", 0.0)), float(sample.get("soil_saturation", 0.0)) * 100.0]
	_inspector.text += "Groundwater  %.1f mm\nSurface  %.1f mm   Snow water  %.1f mm\n" % [float(sample.get("groundwater_mm", 0.0)), float(sample.get("surface_water_mm", 0.0)), float(sample.get("snow_water_mm", 0.0))]
	_inspector.text += "Channel storage  [color=#9ed5bd]%s m³[/color]\n" % _number(float(sample.get("channel_storage_m3", 0.0)))
	_inspector.text += "Channel Q  [color=#9ed5bd]%s m³/s[/color]\n(mean over last completed day)\n" % _number(float(sample.get("channel_discharge_m3_s", 0.0)))
	_inspector.text += "Routing residence  %.2f d (heuristic)\nReach length  %.2f km\n" % [float(sample.get("channel_residence_days", 0.0)), float(sample.get("channel_reach_length_m", 0.0)) / 1000.0]
	_inspector.text += "[color=#9ed5bd]CARBON DENSITY · kg C/m² · L0[/color]\nGrass %.3f   Shrub %.3f   Tree %.3f\nHerbivore %.4f   Carnivore %.5f" % [float(sample.get("grass_carbon", 0.0)), float(sample.get("shrub_carbon", 0.0)), float(sample.get("tree_carbon", 0.0)), float(sample.get("herbivore_carbon", 0.0)), float(sample.get("carnivore_carbon", 0.0))]
	if bool(sample.get("local_materialized", false)):
		_inspector.text += "\nLocal live cover %.3f · disturbance %.3f" % [float(sample.get("local_vegetation_biomass", 0.0)), float(sample.get("local_disturbance", 0.0))]
	var settlements: Array = sample.get("observer_settlements", [])
	if not settlements.is_empty():
		var population := 0.0
		for settlement: Dictionary in settlements:
			population += float(settlement.get("population", 0.0))
		_inspector.text += "\n[color=#f1cb85]Amber markers: %d settlement(s) in L0\nPopulation %s[/color]" % [settlements.size(), _number(population)]


func show_coordinates(x_m: float, y_m: float, altitude: float, flight_speed: float) -> void:
	_coordinates.text = "CAM  %.1f, %.1f km    ALT  %.0f m*    SPEED  %.1f km/s    *relief ×8" % [x_m / 1000.0, y_m / 1000.0, altitude, flight_speed]


func show_message(message: String, error: bool = false) -> void:
	_message.text = message
	_message.add_theme_color_override("font_color", Color("efb590") if error else ACCENT)
	_status_timeout = 8.0


func show_error(message: String) -> void:
	_error_text.text = message
	_error_panel.show()
	_state.text = "WORLD UNAVAILABLE"
	_pause.disabled = true


func clear_error() -> void:
	_error_panel.hide()
	_pause.disabled = false


func _number(value: float) -> String:
	if absf(value) >= 1.0e9:
		return "%.2f B" % (value / 1.0e9)
	if absf(value) >= 1.0e6:
		return "%.2f M" % (value / 1.0e6)
	if absf(value) >= 1.0e3:
		return "%.1f k" % (value / 1.0e3)
	return "%.1f" % value


func _process(delta: float) -> void:
	if _status_timeout > 0.0:
		_status_timeout -= delta
		if _status_timeout <= 0.0:
			_message.text = ""
