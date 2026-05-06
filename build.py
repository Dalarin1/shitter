import os
import time

st = time.time()
cpp_files = []
print(os.system('pwd'))
folders = ["include", "src"]
for folder in folders:
    for entry in os.scandir(folder):
        if entry.is_file() and entry.name.endswith(".cpp"):
                cpp_files.append(entry.path)

compiler = "g++"
executable = "main.exe"
inludes = "-I include"

libraries = []
# windows
if os.name == 'nt':
    libraries = [
        "-lws2_32",
        "-lsecur32",
        "-lshlwapi",
        "-lcrypt32",
        "-l:libspdlog.a",
        "-lbcrypt",
        "-lmswsock",
        "-lssl",
        "-lcrypto",
    ]
    
# posix / java
else :
    libraries = [
        "-lcrypto",
        "-lssl"
    ]    
    executable = executable[:-4]
flags = [
    "-std=c++20",
    "-O2",
    "-Wall",
    "-Wextra",
    "-g",
    "-L lib",
    '-pthread'
    # "-DSPDLOG_COMPILED_LIB",
]

command = f"{compiler} {' '.join(cpp_files)} {inludes} {' '.join(libraries)} {' '.join(flags)} -o {executable}"
print(command)

os.system(command)

print(time.time() - st)