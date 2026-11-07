extends Node

var ticks = []

func _on_ticked(n):
	ticks.append(n)

func _ready():
	var m = get_node("../Mover")
	print("GD_PROPS ", m.speed, " ", m.label)
	print("GD_PING ", m.ping(5), " ", m.ping(2))
	m.speed = 50
	print("GD_SET ", m.speed, " ", typeof(m.speed) == TYPE_FLOAT)
	print("GD_TIMER ", m.get_node_or_null("MadeByHemlock") != null)
	m.explode()
	print("GD_ALIVE")

	var c = Node.new()
	c.name = "Counter"
	c.set_script(load("res://counter.hml"))
	add_child(c)
	c.connect("ticked", _on_ticked)
	print("SIGNAL ", c.has_signal("ticked"))
	print("BUMP ", c.bump(), " ", c.bump(10), " ", ticks)
	print("NOTES ", c.notes_seen())
	print("MATH ", c.math())
	print("RNG ", c.rng_roll())
	c.bad_set()

	print("BROKEN ", load("res://broken.hml").can_instantiate())
	print("UNDEFINED ", load("res://undefined_name.hml").can_instantiate())
	var n = Node.new()
	n.set_script(load("res://throws_top.hml"))
	print("THROWS_TOP ", n.get_script() == null)
	n.free()

	var ext = HemlockScript.new()
	ext.source_code = "const EXTENDS = \"Node2D\";\nfn hi() { return 1; }\n"
	var r = RefCounted.new()
	print("EXT_RELOAD ", ext.reload() == OK)
	r.set_script(ext)
	print("EXT_REJECTED ", r.get_script() == null)

	var s = c.get_script()
	s.source_code = s.source_code.replace("count = count + by;", "count = count + by * 100;")
	print("HOT_RELOAD ", s.reload(true) == OK, " ", c.count, " ", c.bump())

func _process(_delta):
	if Engine.get_process_frames() == 6:
		var m = get_node("../Mover")
		print("MOVED ", m.position.x > 0.0)
		get_tree().quit()
