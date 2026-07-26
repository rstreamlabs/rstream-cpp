import shlex


def _join(arguments):
    return shlex.join(arguments) if arguments else ""


def normalize_compiler_environment(environment):
    for compiler_key, flags_key in (("CC", "CFLAGS"), ("CXX", "CXXFLAGS")):
        command = shlex.split(environment.get(compiler_key, ""))
        if not command:
            continue
        compiler_flags = command[1:]
        configured_flags = shlex.split(environment.get(flags_key, ""))
        environment[compiler_key] = command[0]
        environment[flags_key] = _join(compiler_flags + configured_flags)


def compiler_executables(environment):
    executables = {}
    if environment.get("CC"):
        executables["c"] = environment["CC"]
    if environment.get("CXX"):
        executables["cpp"] = environment["CXX"]
    return executables
