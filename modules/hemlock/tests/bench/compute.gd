extends Node2D
var acc = 0.0
func _process(delta):
	var s = 0.0
	for i in 20:
		s = s + i * delta
	acc = acc + s
