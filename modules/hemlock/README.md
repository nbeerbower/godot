# Hemlock scripting for Godot

This module adds [Hemlock](https://github.com/hemlang/hemlock) as a scripting
language: `.hml` files attach to nodes the same way `.gd` files do, and the
engine calls their `_ready`, `_process`, `_input` and other callbacks. The
module embeds Hemlock's interpreter through its C embedding API
(`include/hemlock_embed.h`, see `docs/advanced/embedding.md` in the Hemlock
repository).

## Building

The module is off unless you point it at a Hemlock checkout:

```bash
scons platform=linuxbsd target=editor hemlock_dir=/path/to/hemlock
```

You can set `HEMLOCK_DIR` in the environment instead of passing `hemlock_dir`.
The build runs `make embed` in that checkout to produce `libhemlock_embed.a`,
and links the system libraries Hemlock was configured with: libffi, zlib,
libcrypto, and libwebsockets when it is installed. Linux and macOS only.

## A script

```hemlock
// res://mover.hml
const EXTENDS = "Node2D";          // the native class it attaches to (default: Object)
const SIGNALS = ["arrived"];       // signals it declares

export let speed: f64 = 120.0;     // `export` puts it in the inspector
let traveled = 0.0;                // per-instance state

fn _ready() {
    godot.print("ready: ", owner.get("name"));
    owner.connect("arrived", owner, "_on_arrived");
}

fn _process(delta) {
    let p = owner.get("position");
    owner.set("position", godot.vec2(p.x + speed * delta, p.y));
    traveled = traveled + speed * delta;
    if (traveled > 500.0) {
        traveled = 0.0;
        owner.emit("arrived", p.x);
    }
}

fn _on_arrived(x) {
    godot.print("arrived at ", x);
}
```

The whole file is the script. Its top level runs once per instance, when the
script is attached to an object. Top-level `let`s are that object's state,
and top-level `fn`s are its methods, which GDScript, signals and `call()` can
all reach. `EXTENDS`, `TOOL` and `SIGNALS` must be literals, because they are
read without running the script. Everything else is plain Hemlock, including
`import`s, whether relative (`./lib/util.hml`) or `@stdlib/...`.

### What a script sees

| Name | What it is |
|------|------------|
| `owner` | the object this instance belongs to |
| `obj.get(name)` / `obj.set(name, value)` | properties of any engine object; an unknown property throws |
| `obj.call(method, ...args)` | any method |
| `obj.get_node(path)`, `obj.emit(signal, ...args)`, `obj.connect(signal, target, method)` | shorthands |
| `obj.is_valid()`, `obj.class_name()`, `obj.is_class(name)`, `obj.free()` | |
| `godot.print(...)` | prints to Godot's output, including the editor's Output panel. Hemlock's own `print` writes to stdout only |
| `godot.singleton(name)` | `Input`, `Engine`, ... |
| `godot.instantiate(class)`, `godot.load(path)` | new objects and resources |
| `godot.keep(obj)`, `godot.release(obj)` | hold a RefCounted object past the current call |
| `godot.vec2/vec2i/vec3/vec3i/vec4/color/rect2/callable/node_path(...)` | build engine values |

### Values

- Numbers, bools, strings, arrays and dictionaries convert structurally.
  Dictionaries become objects, and objects become dictionaries.
- Math types become typed objects: `typeof(owner.get("position"))` is
  `"Vector2"`, with fields `x` and `y`. They convert back by type name, so
  build new ones with `godot.vec2(x, y)` and friends. `Color` has `r g b a`,
  `Rect2` has `position size`, and `Callable` has `object method`.
- Engine objects are handles (ObjectIDs), not pointers. A freed object reads
  as invalid (`is_valid()` is false), and using it throws instead of crashing.
- RefCounted objects are borrowed for the duration of the call that handed
  them over. `godot.instantiate()` and `godot.load()` keep their result for
  you. Anything else you want to hold on to needs `godot.keep(obj)`, and
  every keep needs a matching `godot.release(obj)`. Hemlock does not hide
  refcounts, so you hold exactly what you asked to hold.
- Other engine types (transforms, `RID`, `Signal`, ...) arrive as their
  string form.

### Errors

A Hemlock exception that escapes into the engine is reported as a script
error at the Hemlock line that raised it, followed by the GDScript backtrace
when GDScript made the call:

```
SCRIPT ERROR: boom from hemlock
          at: Hemlock: in explode() (res://mover.hml:39)
          GDScript backtrace (most recent call first):
              [0] _ready (res://checker.gd:11)
```

The engine keeps running. A script that does not compile still loads, so the
editor can show its error, but it cannot be instantiated. A script whose top
level throws is not attached. `panic()` and `exit()` end the process, which
is what they are documented to do.

### Editor

Scripts show up in the "Attach Script" dialog with a default template.
`export let` variables appear in the inspector, with types taken from their
annotation or their literal initializer. The script editor highlights
keywords and reports compile and name errors as you type. Saving a script
reloads it in a running game: instances run the new top level and keep the
current values of the variables that still exist. There is no debugger
support (breakpoints, stepping or stack inspection) and no code completion
yet.

## Limitations

- **Threads**: the interpreter is single-threaded, so the module serializes
  every call into it with one recursive lock. Hemlock `spawn`ed tasks that
  call into the engine are on their own, the same as any engine call from a
  thread.
- **Imported modules load once.** A module is executed the first time a
  script imports it and is then cached for the life of the engine (that is
  how a module's top-level state is shared). Saving the script itself hot
  reloads it, but edits to a module it *imports* take effect after a
  restart, and a module whose top level threw stays failed until then.
- **Exports**: imports are read from disk. That works in the editor and when
  running a project directory, but not yet for modules packed into a `.pck`.
- **`@stdlib`** resolves to `HEMLOCK_STDLIB` if set, then the
  `hemlock/stdlib_path` project setting, then the checkout the engine was
  built against.
- No `class_name`/global classes and no script inheritance. A script extends
  one native class.

## Tests

`tests/smoke/run_smoke.sh [godot-binary]` runs a headless project that
covers lifecycle callbacks, properties, calls in both directions, signals,
error locations, broken scripts, base-type checks and hot reload.
