# 核心库：不依赖 GTK，也不连接 D-Bus，单元测试直接链接它
core_deps = [glib, gio]

libbpcore = static_library('bpcore',
  files('bp-config.c', 'bp-info.c', 'bp-model.c', 'bp-rfkill.c'),
  dependencies: core_deps,
  include_directories: root_inc,
)

bpcore_dep = declare_dependency(
  link_with: libbpcore,
  include_directories: [root_inc, include_directories('.')],
  dependencies: core_deps,
)
