extends Node

# Usage: godot --headless --path . -- <script> <nodes> <rounds>
# Calls _process(delta) on every node `rounds` times through Object::call
# (the same callp path the engine's process notification takes), timing
# with the wall clock. Reports ns per _process call; best of 5 batches.

func _ready():
	var args = OS.get_cmdline_user_args()
	var which = args[0]
	var n = int(args[1]) if args.size() > 1 else 1000
	var rounds = int(args[2]) if args.size() > 2 else 200
	var scr = load("res://" + which)
	var nodes = []
	for i in n:
		var node = Node2D.new()
		node.set_script(scr)
		add_child(node)
		node.set_process(false)
		nodes.append(node)

	var delta = 1.0 / 60.0
	for warm in 20:
		for node in nodes:
			node._process(delta)

	var best = INF
	for batch in 5:
		var t0 = Time.get_ticks_usec()
		for r in rounds:
			for node in nodes:
				node._process(delta)
		var ns = float(Time.get_ticks_usec() - t0) * 1000.0 / (rounds * n)
		best = min(best, ns)
	print("RESULT %-18s %8.0f ns/call" % [which, best])
	get_tree().quit()
