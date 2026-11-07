extends Node2D
# Same string-keyed get/set path the Hemlock wrapper uses
func _process(delta):
	var p = get("position")
	set("position", Vector2(p.x + 10.0 * delta, p.y))
