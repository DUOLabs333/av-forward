conn=import_build("../asio_c")

class main(BuildBase):
    SRC_FILES=["main.cpp"]

    INCLUDE_PATHS=[get_dep_path("boost"), conn.library, get_dep_path("glaze")]

    STATIC_LIBS=[conn.library]

    OUTPUT_TYPE=EXE

    OUTPUT_NAME="av-forward"
    
