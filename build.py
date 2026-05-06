import os

cpp_files = []
folders = ["include", "src"]
for folder in folders:
    for entry in os.scandir(folder):
        if entry.is_file() and entry.name.endswith(".cpp"):
                cpp_files.append(entry.path)

compiler = "g++"
executable = "main.exe"
inludes = "-Iinclude"
libraries = [
    "-lws2_32",
    "-lsecur32",
    "-lshlwapi",
    "-lWinhttp",
    "-lcrypt32",
    "-l:libspdlog.a",
    "-lbcrypt",
    "-lmswsock",
    "-lssl",
    "-lcrypto",
]
flags = [
    "-std=c++20",
    "-O2",
    "-Os",
    "-Wall",
    "-Wextra",
    "-g",
    "-Llib",
    "-DSPDLOG_COMPILED_LIB",
]

command = f"{compiler} {' '.join(cpp_files)} {inludes} {' '.join(libraries)} {' '.join(flags)} -o {executable}"

print(command)
os.system(command)
