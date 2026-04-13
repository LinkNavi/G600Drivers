project('g600d', ['c', 'cpp'],
  version: '0.1.0',
  default_options: [
    'c_std=c11',
    'cpp_std=c++17',
    'warning_level=2',
  ]
)

src_main = files('daemon/main.c')
src_led_test = files('tests/led_test.c')

qt6 = import('qt6')
qt6_dep = dependency('qt6', modules: ['Core', 'Quick', 'Qml', 'Gui', 'Widgets'])

src_gui_cpp = files('gui/main.cpp', 'gui/ConfigManager.cpp')

gui_moc = qt6.compile_moc(
  headers: ['gui/ConfigManager.h'],
)

gui_qrc = qt6.compile_resources(
  sources: ['gui/resources.qrc'],
)

executable('g600d', src_main,
  install: true,
  dependencies: dependency('threads'),
  install_dir: get_option('bindir'),
)

executable('led_test', src_led_test)

executable('g600-config', [src_gui_cpp, gui_moc, gui_qrc],
  install: true,
  dependencies: qt6_dep,
  install_dir: get_option('bindir'),
)
