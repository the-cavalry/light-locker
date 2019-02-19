gschema_config = configuration_data()
if get_option('lock-on-suspend')
  gschema_config.set('LOCK_ON_SUSPEND_DEFAULT', 'true')
else
  gschema_config.set('LOCK_ON_SUSPEND_DEFAULT', 'false')
endif

if get_option('late-locking')
  gschema_config.set('LATE_LOCKING_DEFAULT', 'true')
else
  gschema_config.set('LATE_LOCKING_DEFAULT', 'false')
endif

if get_option('lock-on-lid')
  gschema_config.set('LOCK_ON_LID_DEFAULT', 'true')
else
  gschema_config.set('LOCK_ON_LID_DEFAULT', 'false')
endif

gschema_in = configure_file(
  input: 'apps.light-locker.gschema.xml.in.in',
  output: 'apps.light-locker.gschema.xml.in',
  configuration: gschema_config,
)

custom_target(
  'apps.light-locker.gschema.xml',
  input: gschema_in,
  output: 'apps.light-locker.gschema.xml',
  command: [
    find_program('intltool-merge'),
    '--schemas-style',
    join_paths(meson.source_root(), 'po'),
    '@INPUT@',
    '@OUTPUT@',
  ],
  install: true,
  install_dir: join_paths(get_option('datadir'), 'glib-2.0', 'schemas'),
)

install_man(
  'light-locker.1',
  'light-locker-command.1',
)
