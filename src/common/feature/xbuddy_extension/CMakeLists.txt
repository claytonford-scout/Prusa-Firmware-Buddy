target_link_libraries(firmware PUBLIC xbuddy_extension)

if(XBUDDY_EXTENSION_VARIANT_STANDARD)
  target_sources(firmware PRIVATE cooling.cpp xbuddy_extension.cpp)
endif()

if(XBUDDY_EXTENSION_VARIANT_IX)
  target_sources(firmware PRIVATE xbuddy_extension.cpp)
endif()
