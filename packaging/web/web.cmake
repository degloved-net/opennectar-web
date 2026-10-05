set(PIKMIN_WEB_ASSET_DIR "" CACHE PATH "extracted game data for a private web package")
if(NOT NECTAR_WEB_TOOLS_DIR)
    set(NECTAR_WEB_TOOLS_DIR "${CMAKE_CURRENT_LIST_DIR}")
endif()
if(NOT NECTAR_WEB_GAME)
    set(NECTAR_WEB_GAME "pikmin1")
endif()
target_sources(${NECTAR_GAME} PRIVATE "${NECTAR_WEB_TOOLS_DIR}/runtime/web/pc_web_runtime.cpp")
target_include_directories(${NECTAR_GAME} PRIVATE "${NECTAR_WEB_TOOLS_DIR}/runtime")

option(PIKMIN_WEB_THREADS "Use browser pthreads (requires COOP and COEP headers)" ON)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

foreach(target IN LISTS NECTAR_GAME NECTAR_LEGACY)
    target_compile_options(${target} PRIVATE "SHELL:-sUSE_SDL=2")
    if(PIKMIN_WEB_THREADS)
        target_compile_options(${target} PRIVATE -pthread)
    endif()
endforeach()

if(PIKMIN_WEB_THREADS)
    target_link_options(${NECTAR_GAME} PRIVATE -pthread
        "SHELL:-sDEFAULT_PTHREAD_STACK_SIZE=2097152"
        "SHELL:-sPTHREAD_POOL_SIZE=8")
    set(web_threaded true)
else()
    target_sources(${NECTAR_GAME} PRIVATE "${NECTAR_WEB_TOOLS_DIR}/runtime/web/pc_web_threads.cpp")
    target_link_options(${NECTAR_GAME} PRIVATE "SHELL:-sASYNCIFY=1")
    set(web_threaded false)
endif()

set_property(SOURCE pc_port/mods/pc_hd_model_convert.cpp APPEND PROPERTY COMPILE_OPTIONS -fexceptions)

target_link_options(${NECTAR_GAME} PRIVATE
    -fexceptions
    "SHELL:-sUSE_SDL=2"
    "SHELL:-sMIN_WEBGL_VERSION=2"
    "SHELL:-sMAX_WEBGL_VERSION=2"
    "SHELL:-sFULL_ES3=1"
    "SHELL:-sALLOW_MEMORY_GROWTH=1"
    "SHELL:-sINITIAL_MEMORY=536870912"
    "SHELL:-sMAXIMUM_MEMORY=2147483648"
    "SHELL:-sSTACK_SIZE=8388608"
    "SHELL:-sFORCE_FILESYSTEM=1"
    "SHELL:-sEXPORTED_RUNTIME_METHODS=FS,FS_createPath,FS_createDataFile,addRunDependency,removeRunDependency,ccall"
    "SHELL:-lidbfs.js"
    "SHELL:--pre-js \"${NECTAR_WEB_TOOLS_DIR}/pre.js\""
    "SHELL:--shell-file \"${NECTAR_WEB_TOOLS_DIR}/shell.html\"")

set_target_properties(${NECTAR_GAME} PROPERTIES SUFFIX ".html")
set_property(TARGET ${NECTAR_GAME} APPEND PROPERTY LINK_DEPENDS
    "${NECTAR_WEB_TOOLS_DIR}/pre.js"
    "${NECTAR_WEB_TOOLS_DIR}/shell.html")

foreach(name loader packs pack-worker actions discs disc-files disc-store disc-worker)
    configure_file("${NECTAR_WEB_TOOLS_DIR}/${name}.js" "${CMAKE_BINARY_DIR}/bin/nectar-${name}.js" COPYONLY)
endforeach()
add_custom_target(nectar_web_disc ALL
    COMMAND sh "${NECTAR_WEB_TOOLS_DIR}/build_disc.sh" "${CMAKE_BINARY_DIR}/bin"
    WORKING_DIRECTORY "${NECTAR_WEB_TOOLS_DIR}/../.."
    VERBATIM)
foreach(name nectar.css serve.py)
    configure_file("${NECTAR_WEB_TOOLS_DIR}/${name}" "${CMAKE_BINARY_DIR}/bin/${name}" COPYONLY)
endforeach()
file(REMOVE "${CMAKE_BINARY_DIR}/bin/nectar-core.js")
configure_file("${NECTAR_WEB_TOOLS_DIR}/../../games/pikmin1/packaging/icon/open_nectar.png" "${CMAKE_BINARY_DIR}/bin/nectar-icon.png" COPYONLY)

configure_file("${NECTAR_WEB_TOOLS_DIR}/play.md" "${CMAKE_BINARY_DIR}/bin/readme.md" COPYONLY)

execute_process(COMMAND git rev-parse --short=12 HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE web_revision OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT web_revision)
    set(web_revision "source-archive")
endif()
if(NECTAR_WEB_DISC_KEY)
    set(web_disc_key "${NECTAR_WEB_DISC_KEY}")
elseif(PIKMIN_GAME_VERSION STREQUAL "VERSION_GPIP01_00")
    set(web_disc_key "GPIP01:0")
elseif(PIKMIN_GAME_VERSION STREQUAL "VERSION_GPIE01_01")
    set(web_disc_key "GPIE01:1")
else()
    message(FATAL_ERROR "the web importer supports usa rev. 1 and europe")
endif()
file(WRITE "${CMAKE_BINARY_DIR}/bin/nectar-build.json"
    "{\"version\":3,\"revision\":\"${web_revision}\",\"assetMode\":\"separate\",\"textureStorage\":\"opfs\",\"threaded\":${web_threaded},\"discKey\":\"${web_disc_key}\",\"runtimes\":{\"${web_disc_key}\":\"${NECTAR_OUTPUT_NAME}.js\"}}\n")
execute_process(COMMAND "${Python3_EXECUTABLE}" -c
    "import json,pathlib,sys; p=pathlib.Path(sys.argv[1]); data=json.loads(p.read_text()); data.update(game=sys.argv[2], upstream=json.loads(pathlib.Path(sys.argv[3]).read_text())['commit']); p.write_text(json.dumps(data)+'\\n')"
    "${CMAKE_BINARY_DIR}/bin/nectar-build.json" "${NECTAR_WEB_GAME}" "${NECTAR_WEB_TOOLS_DIR}/upstream.json"
    COMMAND_ERROR_IS_FATAL ANY)

if(PIKMIN_WEB_ASSET_DIR)
    if(NOT EXISTS "${PIKMIN_WEB_ASSET_DIR}/parms/gamePrms.bin")
        message(FATAL_ERROR "the web asset directory must contain parms/gamePrms.bin")
    endif()
    add_custom_target(nectar_web_assets ALL
        COMMAND "${Python3_EXECUTABLE}" "${NECTAR_WEB_TOOLS_DIR}/package_assets.py"
            "${PIKMIN_WEB_ASSET_DIR}" "${CMAKE_BINARY_DIR}/bin"
            --packager "${EMSCRIPTEN_ROOT_PATH}/tools/file_packager.py"
        VERBATIM)
else()
    file(REMOVE
        "${CMAKE_BINARY_DIR}/bin/nectar.data"
        "${CMAKE_BINARY_DIR}/bin/nectar-assets.js"
        "${CMAKE_BINARY_DIR}/bin/nectar-assets.js.metadata"
        "${CMAKE_BINARY_DIR}/bin/nectar-assets.json")
endif()
