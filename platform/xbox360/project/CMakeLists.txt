# dkr360: Golden Balloon (Diddy Kong Racing PC port, github.com/akratch/goldenballoon) for the Xbox 360 with xsdk.
#
# Own build description of Golden Balloon's engine (its CMakeLists.txt is not run): no launcher app, no WebGPU or
# OpenGL, no online play, no custom-character text shaping. The renderer is src/gfx_xenos.cpp (the Xenos backend of the
# Star Fox / Smash / Zelda ports, reshaped for Golden Balloon's fast3d API).
#
#   cmake -S platform/xbox360/project -B <build> -G Ninja -DCMAKE_TOOLCHAIN_FILE=platform/xbox360/cmake/dkr-xsdk.cmake \
#         -DGB_DIR=<goldenballoon, patched>
cmake_minimum_required(VERSION 3.24)
project(DKR360 VERSION 1.0.0 LANGUAGES C CXX)

if(NOT XSDK)
	message(FATAL_ERROR "Configure with -DCMAKE_TOOLCHAIN_FILE=<dkr360>/platform/xbox360/cmake/dkr-xsdk.cmake")
endif()
set(GB_DIR "" CACHE PATH "Golden Balloon's checkout, with dkr360's patch applied")
if(NOT IS_DIRECTORY "${GB_DIR}/game/src")
	message(FATAL_ERROR "missing ${GB_DIR}/game/src")
endif()
set(GB "${GB_DIR}")
set(X360 "${DKR_XSDK_PLATFORM_DIR}")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(GEN "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GEN}")

add_library(dkr_sysroot INTERFACE)
target_include_directories(dkr_sysroot INTERFACE "${XSDK_SYSROOT}/include" "${XSDK_SYSROOT}/include/SDL2")
target_link_libraries(dkr_sysroot INTERFACE "${XSDK_SYSROOT}/lib/libSDL2.a" "${XSDK_SYSROOT}/lib/libSDL3X.a")

# --- the game (the vendored decompilation) ------------------------------------------------------------------------
file(GLOB GAME_SOURCES "${GB}/game/src/*.c" "${GB}/game/src/hasm/*.c" "${GB}/game/src/hasm_native/*.c")

# --- the port layer, as Golden Balloon's CMakeLists.txt lists it, less the desktop-only parts ------------------------
set(PLATFORM_NAMES
	main_pc display_config camera_obstruction camera_obstruction_resolver camera_obstruction_transform
	camera_obstruction_query camera_target_visibility config_ini text_state_file magic_codes_state
	magic_codes_state_file taj_mod_state taj_mod_state_file mod_manifest mod_source mod_registry sprite_model_mesh
	sprite_models mod_texture_key mod_texture_store rice_crc mod_music modern_character_asset modern_character_install
	modern_character_registry modern_character_pose modern_character_render modern_character_draw_store
	modern_character_identity modern_character_text modern_character_runtime modern_character_gpu_timing
	modern_character_capture_projection modern_character_surface_intersection workshop_preview_runtime
	modern_character_donor modern_character_gameplay_profile enhancement_registry enh_speedometer enh_draw_distance
	enh_ai_difficulty save_state asset_subentry custom_track_section custom_track_decode custom_track_model
	custom_track_header custom_track_model_payload custom_track_texture dev_command video_config video_config_runtime
	port_env a11y_model a11y_race a11y_speech_worker a11y_speech_null user_paths fs_utf8 pacing_policy steering_compat
	hud_layout sim_sched host_frame_driver input_tick_queue pad_router party/party_protocol party/remote_pad
	controller_mapping viewport_route_cache audio_service_clock audio_queue_controller audio_ring audio_sink_evidence
	audio_volume present_sched sim_hash gameplay_event_trace gpu_diagnostics input_consumption_trace
	input_latency_census host_sched_probe session/session_core session/session_bridge online/lobby_core
	online/compatibility_identity online/lobby_view_model online/lobby_fake_adapter net/match_launch_descriptor
	online/match_launch_builder net/match_manifest net/match_transport net/match_input_runtime net/match_input_packet
	net/match_input_bundle net/match_input_repair net/match_peer_graph net/match_peer_forward net/match_peer_transcript
	net/match_preflight net/net_input net/net_local_input net/net_roster net/net_roster_runtime net/online_race_results
	net/local_listener_mix net/net_impairment net/net_failure_ring net/net_clock rollback/rollback_snapshot
	rollback/rollback_ring rollback/rollback_events rollback/rollback_audio rollback/rollback_driver
	rollback/rollback_engine_registry rollback/rollback_game_authority rollback/rollback_game_runtime
	presentation_snapshot presentation_snapshot_walk platform_sdl_min app_overlay_hooks stubs_dkr pi_dma rom_io rom_id
	rom_validation sha256 save_codec virtual_pak ghost_bank segment_consts gu_port asset_swap gfx_ptr gfx_config_shim
	hasm_stubs_temp math_util_native mixer audi_port_dkr audio_compat audio_bank audio_sequence audio_envmix audio_fx
	audio_filters audio_synth audio_seqplayer audio_cspplayer audio_event_queue audio_fx_transfer mdkr_adventure
	mdkr_challenge mdkr_taj adventure_party/adventure_party_state adventure_party/adventure_party_policy
	adventure_party/adventure_party_trace adventure_party/adventure_party_spawn adventure_party/adventure_party_runtime
	platform_stdio)
set(PLATFORM_SOURCES "")
foreach(n IN LISTS PLATFORM_NAMES)
	list(APPEND PLATFORM_SOURCES "${GB}/platform/${n}.c")
endforeach()

# The renderer front-end (F3DDKR HLE) and its helpers; Xenos instead of OpenGL/WebGPU.
file(GLOB FAST3D_SOURCES "${GB}/platform/fast3d/*.c")
list(FILTER FAST3D_SOURCES EXCLUDE REGEX "/(screenshot_series|gfx_opengl|gfx_webgpu[a-z_]*|gfx_character_text)\\.c$")

set(LIB_SOURCES "${GB}/lib/stb/stb_image_impl.c" "${GB}/lib/miniz/miniz.c" "${GB}/lib/dr_libs/dr_wav_impl.c"
	"${GB}/lib/imgui/imgui.cpp" "${GB}/lib/imgui/imgui_draw.cpp" "${GB}/lib/imgui/imgui_tables.cpp"
	"${GB}/lib/imgui/imgui_widgets.cpp")
set_source_files_properties(${LIB_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")

# Xenos shaders (the Star Fox port's, built in 256 variants of fast3d.frag)
set(XENOS_SHADER_DIR "${X360}/src/xenos")
file(READ "${XENOS_SHADER_DIR}/fast3d.frag" XENOS_FAST3D_FRAG)
string(REGEX REPLACE "^#version 450\n" "" XENOS_FAST3D_BODY "${XENOS_FAST3D_FRAG}")
set(XENOS_SHADERS "${XENOS_SHADER_DIR}/fast3d.vert" "${XENOS_SHADER_DIR}/ui.vert" "${XENOS_SHADER_DIR}/ui.frag")
set(XENOS_VARIANT_LIST "")
foreach(variant RANGE 0 255)
	string(APPEND XENOS_VARIANT_LIST "XENOS_VARIANT(${variant})\n")
	set(defines "")
	foreach(bit_name IN ITEMS "1;TP0" "2;TP1" "4;CYC2" "8;KILL" "16;NOISE" "32;T0" "64;T1" "128;MASK0")
		list(GET bit_name 0 bit)
		list(GET bit_name 1 name)
		math(EXPR has "${variant} & ${bit}")
		if(has)
			string(APPEND defines "#define ${name} 1\n")
		endif()
	endforeach()
	set(out "${GEN}/xenos/fast3d_v${variant}.frag")
	set(content "#version 450\n${defines}${XENOS_FAST3D_BODY}")
	set(old "")
	if(EXISTS "${out}")
		file(READ "${out}" old)
	endif()
	if(NOT old STREQUAL content)
		file(WRITE "${out}" "${content}")
	endif()
	list(APPEND XENOS_SHADERS "${out}")
endforeach()
set(old "")
if(EXISTS "${GEN}/fast3d_variants.inc")
	file(READ "${GEN}/fast3d_variants.inc" old)
endif()
if(NOT old STREQUAL XENOS_VARIANT_LIST)
	file(WRITE "${GEN}/fast3d_variants.inc" "${XENOS_VARIANT_LIST}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${XENOS_SHADER_DIR}/fast3d.frag")
set(XENOS_GLSLANG "")
if(EXISTS "${XSDK_GLSLANG}")
	set(XENOS_GLSLANG --glslang "${XSDK_GLSLANG}")
endif()
add_custom_command(OUTPUT "${GEN}/dkr360_shaders.h"
	COMMAND python3 "${XSDK_ROOT}/tools/build_shaders.py" ${XENOS_GLSLANG} --output "${GEN}/dkr360_shaders.h"
		${XENOS_SHADERS}
	DEPENDS ${XENOS_SHADERS} "${XSDK_ROOT}/tools/build_shaders.py"
	COMMENT "Compiling the Xenos shaders"
	VERBATIM)

set(X360_SOURCES
	"${X360}/src/gfx_xenos.cpp" "${X360}/src/xsdk_glue.c" "${X360}/src/xsdk_posix_stubs.c"
	"${X360}/src/dkr360_main.c" "${X360}/src/dkr360_stubs.c" "${X360}/src/dkr360_menu.cpp" "${GEN}/dkr360_shaders.h")

add_executable(dkr360 ${GAME_SOURCES} ${PLATFORM_SOURCES} ${FAST3D_SOURCES} ${LIB_SOURCES} ${X360_SOURCES})
target_include_directories(dkr360 PRIVATE
	"${GEN}" "${GB}/game" "${GB}/game/src" "${GB}/game/include" "${GB}/game/libultra" "${GB}/game/libultra/src/audio"
	"${GB}/platform" "${GB}/platform/fast3d" "${GB}/platform/fast3d_shim" "${GB}/lib/stb" "${GB}/lib/miniz"
	"${GB}/lib/dr_libs" "${GB}/lib/imgui" "${X360}/src")
target_compile_options(dkr360 PRIVATE
	$<$<COMPILE_LANGUAGE:C>:-iquote${GB}/game/include/PR> $<$<COMPILE_LANGUAGE:C>:-iquote${GB}/game/include/sys>
	-fno-strict-aliasing -fcommon -fwrapv -w
	$<$<COMPILE_LANGUAGE:C>:-Werror=implicit-function-declaration>
	$<$<COMPILE_LANGUAGE:C>:-Werror=int-to-pointer-cast> $<$<COMPILE_LANGUAGE:C>:-Werror=pointer-to-int-cast>
	-Werror=return-type)
target_compile_definitions(dkr360 PRIVATE
	VERSION_us_v80 _LANGUAGE_C MODERN_CC NON_MATCHING=1 AVOID_UB=1 NATIVE_PORT=1 F3DDKR_GBI _FINALROM SDL_MAIN_HANDLED IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS
	MDKR_ADVENTURE_PARTY_OMIT_NOT=1 MDKR_VERSION_STRING="1.8.0-x360" DKR360=1)
target_link_libraries(dkr360 PRIVATE dkr_sysroot)
target_link_options(dkr360 PRIVATE
	-Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=memalign -Wl,--wrap=pthread_create
	-Wl,--wrap=xsdk_log_open)
xsdk_add_xex(dkr360 NAME "Diddy Kong Racing")
