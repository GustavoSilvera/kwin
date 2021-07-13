add_subdirectory(plugin)
kpackage_install_package(package expo scripts kwin)

# Copy the desktop file to the services dir in order to let system settings know that
# this script has a config module.
install(FILES package/metadata.desktop DESTINATION ${KDE_INSTALL_KSERVICES5DIR} RENAME kwin-script-expo.desktop)
