labwc_sources = files(
  'action.c',
  'buffer.c',
  'debug.c',
  'desktop.c',
  'dnd.c',
  'idle.c',
  'interactive.c',
  'layers.c',
  'magnifier.c',
  'main.c',
  'node.c',
  'osd.c',
  'osd-field.c',
  'output.c',
  'output-state.c',
  'output-virtual.c',
  'overlay.c',
  'placement.c',
  'regions.c',
  'seat.c',
  'server.c',
  'session-lock.c',
  'snap-constraints.c',
  'tearing.c',
  'theme.c',
  'view.c',
  'view-impl-common.c',
  'window-rules.c',
  'workspaces.c',
  'xdg.c',
  'xdg-popup.c',
)

if have_xwayland
  labwc_sources += files(
    'xwayland.c',
    'xwayland-unmanaged.c',
  )
endif


subdir('img')
subdir('common')
subdir('config')
subdir('decorations')
subdir('foreign-toplevel')
subdir('input')
subdir('ssd')
subdir('protocols')
