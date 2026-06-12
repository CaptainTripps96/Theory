add_subdirectory(core)

if(TSQ_BUILD_APP)
    add_subdirectory(engine)
    add_subdirectory(ui)
    add_subdirectory(app)
endif()
