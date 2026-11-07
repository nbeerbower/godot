extends Node2D
func _process(delta):
	var p = position
	position = Vector2(p.x + 10.0 * delta, p.y)
