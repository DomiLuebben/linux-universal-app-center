set(CMAKE_AUTOMOC ON)

add_library(liblut STATIC
    liblut.cpp
    protocol/events.cpp
    progress/Weights.cpp
    progress/Eta.cpp
    progress/ProgressModel.cpp
    backend/Validation.cpp
    backend/Backend.cpp
    backend/replay/ReplayBackend.cpp
    backend/dnf5/Dnf5Backend.cpp
    backend/alpm/AlpmBackend.cpp
    backend/apt/StatusFdParser.cpp
    backend/apt/AptBackend.cpp
    detect/DistroDetect.cpp
    history/HistoryDb.cpp
)

target_include_directories(liblut PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(liblut PUBLIC
    Qt6::Core
    Qt6::DBus
    SQLite3::SQLite3
)

if(ALPM_FOUND)
    target_compile_definitions(liblut PUBLIC HAVE_ALPM=1)
    target_include_directories(liblut PUBLIC ${ALPM_INCLUDE_DIRS})
    target_link_libraries(liblut PUBLIC ${ALPM_LIBRARIES})

    add_executable(lut-alpm-worker
        backend/alpm/worker_main.cpp
    )
    target_link_libraries(lut-alpm-worker PRIVATE
        liblut
        Qt6::Core
        ${ALPM_LIBRARIES}
    )
    target_include_directories(lut-alpm-worker PRIVATE
        ${ALPM_INCLUDE_DIRS}
    )
endif()
