class_name ObserverHistory
extends Control
## Observation samples only; horizontal spacing uses elapsed simulation days.

const MAX_SAMPLES: int = 256
const PLANT_COLOR := Color("9ed5bd")
const FLOW_COLOR := Color("73b8e5")
const MUTED := Color("a1b6b8")
var _days: Array[int] = []
var _values: Array[Vector2] = []
var _initial_plants: float = 0.0
var _initial_day: int = 0


func _ready() -> void:
	custom_minimum_size = Vector2(0, 250)
	mouse_filter = Control.MOUSE_FILTER_PASS
	tooltip_text = "Up to 256 observed snapshots. X = simulation day (proportional spacing); Y = labelled automatic range. Plants are total tonnes of carbon. Outlet flow is the sum at terminal reaches, averaged over the last completed day."
	resized.connect(queue_redraw)


func reset_history() -> void:
	_days.clear()
	_values.clear()
	_initial_plants = 0.0
	_initial_day = 0
	queue_redraw()


func record(day: int, plant_tonnes: float, terminal_outflow: float) -> void:
	if not is_finite(plant_tonnes) or not is_finite(terminal_outflow):
		return
	if not _days.is_empty():
		if day == _days.back():
			return
		if day < _days.back():
			reset_history()
	if _days.is_empty():
		_initial_plants = plant_tonnes
		_initial_day = day
	_days.append(day)
	_values.append(Vector2(plant_tonnes, terminal_outflow))
	if _days.size() > MAX_SAMPLES:
		_days.pop_front()
		_values.pop_front()
	queue_redraw()


func plant_change_text() -> String:
	if _values.is_empty():
		return "Awaiting observation history"
	if _initial_plants <= 0.0:
		return "Plant change: n/a (initial stock was zero)"
	var change: float = (_values.back().x / _initial_plants - 1.0) * 100.0
	return "Plants %+.2f%% since day %d" % [change, _initial_day]


func _draw() -> void:
	_draw_series(0, 15.0, "Plants · t C · auto range", PLANT_COLOR)
	_draw_series(1, 140.0, "Outlet flow · m³/s · previous day", FLOW_COLOR)


func _draw_series(component: int, top: float, title: String, color: Color) -> void:
	var font := ThemeDB.fallback_font
	draw_string(font, Vector2(0, top), title, HORIZONTAL_ALIGNMENT_LEFT, size.x, 12, color)
	var plot := Rect2(66, top + 12, maxf(size.x - 74, 1.0), 70)
	draw_rect(plot, Color("12272f"))
	if _values.is_empty():
		draw_string(font, plot.position + Vector2(8, 40), "Awaiting snapshots", HORIZONTAL_ALIGNMENT_LEFT, plot.size.x, 11, MUTED)
		return
	var low := float(_values[0][component])
	var high := low
	for sample: Vector2 in _values:
		low = minf(low, sample[component])
		high = maxf(high, sample[component])
	if high <= low:
		var padding := maxf(absf(high) * 0.01, 1.0)
		low = maxf(0.0, low - padding)
		high += padding
	var span := high - low
	var scale := 1.0
	var suffix := ""
	if high >= 1.0e9:
		scale = 1.0e9
		suffix = "B"
	elif high >= 1.0e6:
		scale = 1.0e6
		suffix = "M"
	elif high >= 1.0e3:
		scale = 1.0e3
		suffix = "k"
	var precision := clampi(ceili(-log(span / scale) / log(10.0)) + 1, 1, 6)
	draw_string(font, Vector2(0, plot.position.y + 10), String.num(high / scale, precision) + suffix, HORIZONTAL_ALIGNMENT_RIGHT, 59, 10, MUTED)
	draw_string(font, Vector2(0, plot.end.y), String.num(low / scale, precision) + suffix, HORIZONTAL_ALIGNMENT_RIGHT, 59, 10, MUTED)
	draw_line(plot.position, Vector2(plot.position.x, plot.end.y), Color("40575c"))
	draw_line(Vector2(plot.position.x, plot.end.y), plot.end, Color("40575c"))
	var first_day: int = _days.front()
	var day_span := maxi(_days.back() - first_day, 1)
	var points := PackedVector2Array()
	for i in range(_days.size()):
		points.append(Vector2(plot.position.x + float(_days[i] - first_day) / day_span * plot.size.x, plot.end.y - (_values[i][component] - low) / span * plot.size.y))
	if points.size() > 1:
		draw_polyline(points, color, 1.8, true)
	draw_circle(points[points.size() - 1], 2.5, color)
	draw_string(font, Vector2(plot.position.x, plot.end.y + 15), "d %d" % first_day, HORIZONTAL_ALIGNMENT_LEFT, plot.size.x, 10, MUTED)
	if _days.size() > 1:
		draw_string(font, Vector2(plot.position.x, plot.end.y + 15), "d %d" % _days.back(), HORIZONTAL_ALIGNMENT_RIGHT, plot.size.x, 10, MUTED)
