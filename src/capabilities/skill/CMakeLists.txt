# 技能加载器
bengear_module(bengear_skill
    skill.cpp
    zip_extract.cpp
)
target_link_libraries(bengear_skill PUBLIC bengear_base bengear_json bengear_net bengear_compress)
