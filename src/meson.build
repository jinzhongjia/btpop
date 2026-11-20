resources = gnome.compile_resources('bp-resources',
  'ui/btpop.gresource.xml',
  source_dir: 'ui',
  c_name: 'bp',
)

# gtk4-layer-shell 需要在 libwayland-client 之前链接，因此排在最前面
executable('btpop',
  files(
    'main.c',
    'backend/bp-agent.c',
    'backend/bp-bluez.c',
    'ui/bp-app.c',
    'ui/bp-page-auth.c',
    'ui/bp-page-device.c',
    'ui/bp-page-main.c',
    'ui/bp-page-settings.c',
    'ui/bp-widgets.c',
  ),
  resources,
  dependencies: [layer, gtk, bpcore_dep],
  include_directories: include_directories('backend', 'ui'),
  install: true,
)
