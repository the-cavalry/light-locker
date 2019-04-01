project('light-locker', 'c', version: '1.8.0', meson_version: '>=0.44.0')

gnome = import('gnome')
i18n = import('i18n')

glib_required_version = '2.25.6'
glib_dep = dependency('glib-2.0', version: '>='+glib_required_version)
gio_dep = dependency('gio-2.0', version: '>='+glib_required_version)
gobject_dep = dependency('gobject-2.0', version: '>='+glib_required_version)
  gtk_dep = dependency('gtk+-3.0', version: '>=3.0')
dbus_glib_dep = dependency('dbus-glib-1', version: '>=0.30')
x11_dep = dependency('x11', version: '>=1.0')
xext_dep = dependency('xext', required: false)
xscrnsaver_dep = dependency('xscrnsaver', required: false)
x_org_dep = [
  x11_dep,
  xext_dep,
  xscrnsaver_dep
]

dbus_dep = dependency('dbus-1')
dbus_version_parts = dbus_dep.version().split('.')
dbus_version_major = dbus_version_parts[0]
dbus_version_minor = dbus_version_parts[1]
dbus_version_micro = dbus_version_parts[2]

add_project_arguments(
  '-DDBUS_VERSION_MAJOR=@0@'.format(dbus_version_major),
  '-DDBUS_VERSION_MINOR=@0@'.format(dbus_version_minor),
  '-DDBUS_VERSION_MICRO=@0@'.format(dbus_version_micro),
  language: 'c',
)

c_compiler = meson.get_compiler('c')
# Check for the MIT-SCREEN-SAVER server extension
if get_option('mit-ext')
  if not c_compiler.has_header ('X11/extensions/scrnsaver.h', dependencies: x_org_dep)
    error('MIT-SCREEN-SAVER X11 extension not found, disable it using -Dmit-ext=false')
  endif

  if not c_compiler.has_function('XScreenSaverRegister', dependencies: x_org_dep)
    error('MIT-SCREEN-SAVER X11 extension not found, disable it using -Dmit-ext=false')
  endif

  add_project_arguments('-DHAVE_MIT_SAVER_EXTENSION=1', language: 'c')
endif

# Check for the DPMS server extension
if get_option('dpms-ext')
  if not c_compiler.has_header ('X11/extensions/dpms.h', dependencies: x_org_dep)
    error('DPMS X11 extension not found, disable it using -Ddpms-ext=false')
  endif

  if not c_compiler.has_function('DPMSForceLevel', dependencies: x_org_dep)
    error('DPMS X11 extension not found, disable it using -Ddpms-ext=false')
  endif

  add_project_arguments('-DHAVE_DPMS_EXTENSION=1', language: 'c')
endif

# Check for the XF86VMODE server extension (for gamma fading)
if get_option('xf86gamma-ext')
  x_org_dep += dependency('xxf86vm')

  if not c_compiler.has_header ('X11/extensions/xf86vmode.h', dependencies: x_org_dep)
    error('XF86VMODE Gamma not available, disable it using -Dxf86gamma-ext=false')
  endif

  if not c_compiler.has_function('XF86VidModeSetGamma', dependencies: x_org_dep)
    error('XF86VMODE Gamma not available, disable it using -Dxf86gamma-ext=false')
  endif

  if c_compiler.has_function('XF86VidModeSetGammaRamp', dependencies: x_org_dep)
    add_project_arguments('-DHAVE_XF86VMODE_GAMMA_RAMP=1', language: 'c')
  endif

  if c_compiler.has_function('XF86MiscSetGrabKeysState', dependencies: x_org_dep)
    add_project_arguments('-DHAVE_XF86MISCSETGRABKEYSSTATE=1', language: 'c')
  endif

  add_project_arguments('-DHAVE_XF86VMODE_GAMMA=1', language: 'c')
endif

# systemd
libsystemd = []
if get_option('systemd')
  libsystemd_dep = dependency('libsystemd')
  add_project_arguments('-DWITH_SYSTEMD=1', language: 'c')
endif

# UPower
if get_option('upower')
  add_project_arguments('-DWITH_UPOWER=1', language: 'c')
endif

if get_option('late-locking')
  add_project_arguments('-DWITH_LATE_LOCKING=TRUE', language: 'c')
endif

if get_option('lock-on-suspend')
  add_project_arguments('-DWITH_LOCK_ON_SUSPEND=TRUE', language: 'c')
endif

if get_option('lock-on-lid')
  add_project_arguments('-DWITH_LOCK_ON_LID=TRUE', language: 'c')
endif

if get_option('gsettings')
  add_project_arguments('-DWITH_SETTINGS_BACKEND=GSETTINGS', language: 'c')
endif

conf = configuration_data()
conf.set_quoted('GETTEXT_PACKAGE', 'light-locker')
conf.set_quoted('LOCALEDIR', join_paths(get_option('prefix'), get_option('localedir')))
conf.set_quoted('VERSION', meson.project_version())

config_header = configure_file(
  output: 'config.h',
  configuration: conf,
)

config_dep = declare_dependency(
  sources: config_header,
  include_directories: include_directories('.')
)

subdir('data')
subdir('po')
subdir('src')
