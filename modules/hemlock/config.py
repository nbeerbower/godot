import os


def _hemlock_dir(env):
    path = env.get("hemlock_dir", "") or os.environ.get("HEMLOCK_DIR", "")
    return os.path.abspath(os.path.expanduser(path)) if path else ""


def can_build(env, platform):
    # Hemlock's interpreter is POSIX-first (pthreads, dlopen, libffi).
    if platform not in ("linuxbsd", "macos"):
        return False
    path = _hemlock_dir(env)
    if not path:
        print("hemlock: module disabled (set hemlock_dir=/path/to/hemlock or HEMLOCK_DIR to enable).")
        return False
    if not os.path.isfile(os.path.join(path, "include", "hemlock_embed.h")):
        print(f"hemlock: module disabled ({path} has no include/hemlock_embed.h).")
        return False
    return True


def get_opts(platform):
    from SCons.Variables import PathVariable

    return [
        PathVariable(
            "hemlock_dir",
            "Path to a Hemlock checkout (github.com/hemlang/hemlock) to embed; the module is disabled when empty",
            "",
            PathVariable.PathAccept,
        ),
    ]


def configure(env):
    pass


def get_doc_classes():
    return [
        "HemlockScript",
    ]


def get_doc_path():
    return "doc_classes"
