# Imports source assets into the formats games load: the asset database, which finds the sources and their .meta
# files, and an importer per type of source. Only the tools link it; games load imported assets with AssetManager.
#
# Like spot_tools, it is built against the engine's headers only: whatever links it also links spot::spot or
# spot::shared, listed after it, so a program never holds two copies of the engine.
add_library(spot_import STATIC
    asset_database.cpp
    asset_database.h
    asset_meta.cpp
    asset_meta.h
    importers/animator_controller_importer.cpp
    importers/font_importer.cpp
    importers/importer.cpp
    importers/importer.h
    importers/material_importer.cpp
    importers/model_importer.cpp
    importers/prefab_importer.cpp
    importers/scene_importer.cpp
    importers/script_importer.cpp
    importers/sound_importer.cpp
    importers/texture_compression.cpp
    importers/texture_compression.h
    importers/texture_importer.cpp
    importers/sprite_atlas_importer.cpp
    ../spot_import.h
)
add_library(spot::import ALIAS spot_import)

target_include_directories(spot_import PUBLIC $<TARGET_PROPERTY:spot_objects,INTERFACE_INCLUDE_DIRECTORIES>)
target_compile_features(spot_import PUBLIC cxx_std_20)
target_link_libraries(spot_import PRIVATE nlohmann_json::nlohmann_json)
# stb_dxt compresses textures into blocks.
target_include_directories(spot_import SYSTEM PRIVATE ${stb_SOURCE_DIR})

spot_set_compile_options(spot_import)
