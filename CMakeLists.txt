cmake_minimum_required(VERSION 3.16)
project(CabControlPlatformIO NONE)

# Default PlatformIO environment from platformio.ini
set(PIO_ENV "nanorp2040connect" CACHE STRING "PlatformIO environment name")
set(PIO_EXECUTABLE "" CACHE FILEPATH "Path to PlatformIO executable (optional)")

# Locate PlatformIO CLI as either 'pio' or 'platformio'.
if(PIO_EXECUTABLE)
    set(PIO_COMMAND ${PIO_EXECUTABLE})
else()
    find_program(PIO_FOUND_EXECUTABLE
        NAMES pio platformio
        HINTS
            "$ENV{HOME}/.platformio/penv/bin"
    )

    if(PIO_FOUND_EXECUTABLE)
        set(PIO_COMMAND ${PIO_FOUND_EXECUTABLE})
    else()
        find_package(Python3 COMPONENTS Interpreter)
        if(Python3_Interpreter_FOUND)
            # Fallback for environments where PlatformIO is installed as a Python module.
            set(PIO_COMMAND ${Python3_EXECUTABLE} -m platformio)
        else()
            message(FATAL_ERROR "Could not find PlatformIO CLI ('pio'/'platformio') or a usable Python3 interpreter for 'python3 -m platformio'.")
        endif()
    endif()
endif()

add_custom_target(pio_compile
    COMMAND ${PIO_COMMAND} run -e ${PIO_ENV}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Compiling PlatformIO environment '${PIO_ENV}'"
    VERBATIM
)

add_custom_target(pio_clean
    COMMAND ${PIO_COMMAND} run -t clean -e ${PIO_ENV}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Cleaning PlatformIO environment '${PIO_ENV}'"
    VERBATIM
)

add_custom_target(pio_flash
    COMMAND ${PIO_COMMAND} run -e ${PIO_ENV} -t upload
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Compiling and flashing PlatformIO environment '${PIO_ENV}'"
    VERBATIM
)
