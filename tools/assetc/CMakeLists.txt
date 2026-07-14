add_executable(assetc main.cpp)
target_link_libraries(assetc PRIVATE engine_assets engine_asset_tools)
engine_apply_target_defaults(assetc "Tools")
