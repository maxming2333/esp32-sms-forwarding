Import("env")  # noqa
import os
if os.environ.get('CI'):
    pass
else:
    import subprocess
    if "compiledb" not in COMMAND_LINE_TARGETS:  # avoids infinite recursion
        subprocess.run(['pio', 'run', '-t', 'compiledb'])