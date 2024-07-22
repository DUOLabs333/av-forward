conn=import_build("../asio_c")

import subprocess, os
class process(BuildBase):
    def build(cls):
        subprocess.run([os.path.join(".","bootstrap.sh"), f"--prefix={os.path.join(os.getcwd(),'external')}", "--with-libraries=filesystem"], cwd=get_dep_path("boost"))

        subprocess.run([os.path.join(".","b2"), "install", "link=static", "--layout=tagged", "--includedir=."], cwd=get_dep_path("boost"))

class main(BuildBase):
    SRC_FILES=["main.cpp"]

    INCLUDE_PATHS=[get_dep_path("boost"), conn.library, get_dep_path("glaze", "include")]

    STATIC_LIBS=[conn.library, "external/lib/*"]

    OUTPUT_TYPE=EXE

    OUTPUT_NAME="av-forward"
    