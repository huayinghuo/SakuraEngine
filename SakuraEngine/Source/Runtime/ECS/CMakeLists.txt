project(ECS)

Module(
    NAME ECS
    TYPE Library
    SRC_PATH  
        #Default as Source
    DEPS
    DEPS_PUBLIC 
        RuntimeCore 
        DotsCodebase DotsDatabase
    INCLUDES_PUBLIC 
        #Default as RuntimeCore
    LINKS
    LINKS_PUBLIC
)