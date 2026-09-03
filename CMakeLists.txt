cmake_minimum_required(VERSION 3.10)
project(xGPU_unit_test LANGUAGES C CXX)
set(TARGET_PROJECT "xGPU_unit_test")
set(COMPONENT_REGISTRY "")

if(POLICY CMP0169)
  # Set CMP0169 to OLD to allow FetchContent_Populate without auto-executing root CMakeLists
  cmake_policy(SET CMP0169 OLD)
endif()

#
# Fetch Common.cmake from xcmake_tools
#
include(FetchContent)
FetchContent_Declare(
  xcmake_tools
  GIT_REPOSITORY https://github.com/LIONant-depot/xcmake_tools.git
  GIT_TAG main
  GIT_SHALLOW TRUE
  SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/xcmake_tools"
)
FetchContent_MakeAvailable(xcmake_tools)
include(${CMAKE_BINARY_DIR}/_deps/xcmake_tools/Common.cmake)

#
# Get and build the core example project
#
# Install.bat only copies check_changes.bat into Cache/Plugins after every plugin has
# cloned, symlinked, and built successfully -- gate on that (not just the folder existing)
# so a prior partial/failed install gets retried instead of being silently skipped, which
# previously surfaced as an unrelated "can't find assimp-vc143-mt.dll" error much further down.
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/example.lionprj/Cache/Plugins/check_changes.bat")

    FetchContent_Declare(
      xresource_pipeline_example
      GIT_REPOSITORY https://github.com/LIONant-depot/example.lionprj.git
      GIT_TAG main
      GIT_SHALLOW TRUE
      SOURCE_DIR "${CMAKE_SOURCE_DIR}/example.lionprj"
    )
    FetchContent_Populate(xresource_pipeline_example)
    
    # Define the working directory
    set(XEXAMPLE_PLUGIN_BUILD_DIR "${CMAKE_SOURCE_DIR}/example.lionprj")
    
    # Change to the build directory and execute the batch script
    message(STATUS "Running Install.bat in ${XEXAMPLE_PLUGIN_BUILD_DIR}...")
    execute_process(
        COMMAND "${XEXAMPLE_PLUGIN_BUILD_DIR}/Install.bat" "return"
        WORKING_DIRECTORY "${XEXAMPLE_PLUGIN_BUILD_DIR}"
        RESULT_VARIABLE result
    )
    if(result)
        message(FATAL_ERROR "Failed to execute Install.bat: ${result}")
    endif()    
endif()

#
# Create a worm hole to unify all the dependencies
#
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/dependencies")
  file(CREATE_LINK
    "${CMAKE_SOURCE_DIR}/example.lionprj/Cache/dependencies"
    "${CMAKE_SOURCE_DIR}/dependencies"
    SYMBOLIC
    RESULT result
  )
  if(result)
    message(FATAL_ERROR "Failed to create symlink: ${result}")
  endif()
endif()

#
# Create a worm hole to unify all the dependencies
#
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/plugins")
  file(CREATE_LINK
    "${CMAKE_SOURCE_DIR}/example.lionprj/Cache/plugins"
    "${CMAKE_SOURCE_DIR}/plugins"
    SYMBOLIC
    RESULT result
  )
  if(result)
    message(FATAL_ERROR "Failed to create symlink: ${result}")
  endif()
endif()


#
# Get all the dependencies
#
FetchAndPopulate("https://github.com/ocornut/imgui.git" "docking")
FetchAndPopulate("https://github.com/LIONant-depot/xresource_mgr.git" "main")
FetchAndPopulate("https://github.com/LIONant-depot/xcontainer.git" "main")
FetchAndPopulate("https://github.com/LIONant-depot/xdelegate.git" "main")
FetchAndPopulate("https://github.com/LIONant-depot/xprim_geom.git" "main")
FetchAndPopulate("https://github.com/LIONant-depot/xproperty.git" "master")
FetchAndPopulate("https://github.com/LIONant-depot/xstrtool.git" "main")
FetchAndPopulate("https://github.com/LIONant-depot/xbmp_tools.git" "main")
FetchAndPopulate("https://github.com/LIONant-depot/xserializer.git" "main")
FetchAndPopulate("https://github.com/LIONant-depot/xscheduler.git" "main")
FetchAndPopulate("https://github.com/thedmd/imgui-node-editor.git" "master")

#
# Add ImGui files directly
#
DefineInterfaceComponent(imgui "dependencies"
  "imconfig.h"
  "imgui.cpp"
  "imgui.h"
  "imgui_demo.cpp"
  "imgui_draw.cpp"
  "imgui_internal.h"
  "imgui_tables.cpp"
  "imgui_widgets.cpp"
  "imstb_rectpack.h"
  "imstb_textedit.h"
  "imstb_truetype.h"
  "LICENSE.txt"
)

#
# Add ImGui Node Editor files directly
#
DefineInterfaceComponent(imgui-node-editor "dependencies"
  "crude_json.cpp"
  "crude_json.h"
  "imgui_bezier_math.h"
  "imgui_bezier_math.inl"
  "imgui_canvas.cpp"
  "imgui_canvas.h"
  "imgui_extra_math.h"
  "imgui_extra_math.inl"
  "imgui_node_editor.cpp"
  "imgui_node_editor.h"
  "imgui_node_editor_api.cpp"
  "imgui_node_editor_internal.h"
  "imgui_node_editor_internal.inl"
  "LICENSE"
)

#
# Copy assimp dll
#
# Define source and destination paths
set(ASSIMP_DEST "${CMAKE_CURRENT_SOURCE_DIR}/Build/xGPUExamples.vs2022")
if(NOT EXISTS "${ASSIMP_DEST}/assimp-vc143-mt.dll")
    set(ASSIMP_SRC "${CMAKE_SOURCE_DIR}/example.lionprj/Cache/Plugins/xgeom_static.plugin/dependencies/assimp/BINARIES/Win32/bin/Release/assimp-vc143-mt.dll")

    # Copy the DLL file
    message(STATUS "Copying assimp-vc143-mt.dll...")
    file(COPY "${ASSIMP_SRC}" DESTINATION "${ASSIMP_DEST}" NO_SOURCE_PERMISSIONS)
    if(NOT EXISTS "${ASSIMP_DEST}/assimp-vc143-mt.dll")
        message(FATAL_ERROR "Failed to copy assimp-vc143-mt.dll")
    endif()
endif()

#
# Get shaderC
#
# Download Shaderc zip if it doesn't exist
set(SHADERC_ZIP "${CMAKE_SOURCE_DIR}/dependencies/Shaderc.zip")
set(SHADERC_EXTRACT_DIR "${CMAKE_SOURCE_DIR}/dependencies")
set(SHADERC_INSTALL_DIR "${SHADERC_EXTRACT_DIR}/shaderc")
# Gate on the fully-extracted install dir, not the zip: a failed/interrupted download can
# leave a stale or 0-byte zip behind, which would otherwise block every future retry.
if(NOT EXISTS "${SHADERC_INSTALL_DIR}")

    # Define paths
    set(SHADERC_URL "https://storage.googleapis.com/shaderc/artifacts/prod/graphics_shader_compiler/shaderc/windows-vs2022-amd64-release/continuous/36/20260731-122731/install.zip")

    message(STATUS "Downloading Shaderc...")
    file(DOWNLOAD "${SHADERC_URL}" "${SHADERC_ZIP}" STATUS download_status)
    list(GET download_status 0 status_code)
    if(status_code)
        message(FATAL_ERROR "Failed to download Shaderc: ${download_status}")
    endif()

    # Unzip the Shaderc archive
    message(STATUS "Unzipping Shaderc...")
    file(ARCHIVE_EXTRACT INPUT "${SHADERC_ZIP}" DESTINATION "${SHADERC_EXTRACT_DIR}")
    file(RENAME "${SHADERC_EXTRACT_DIR}/install" "${SHADERC_INSTALL_DIR}")
    if(NOT EXISTS "${SHADERC_INSTALL_DIR}")
       message(FATAL_ERROR "Failed to extract Shaderc zip")
    endif()
endif()

#
# Extract all the animated assets
#
set(DESTINATION_ASSETS "${CMAKE_SOURCE_DIR}/dependencies/Assets/Animated")
# Each zip extracts to a top-level folder matching its own name -- gate per-archive rather
# than on the shared parent folder, so a failure partway through (e.g. zip 2 of 6) doesn't
# leave the rest silently un-extracted on the next run.
set(ANIMATED_ASSET_ARCHIVES catwalk ImperialWalker Sonic Starwars supersoldier walking-while-listening)
foreach(asset_name ${ANIMATED_ASSET_ARCHIVES})
  if(NOT EXISTS "${DESTINATION_ASSETS}/${asset_name}")
    file(ARCHIVE_EXTRACT INPUT "${CMAKE_SOURCE_DIR}/Assets/Animated/${asset_name}.zip" DESTINATION "${DESTINATION_ASSETS}")
  endif()
endforeach()

#####################
#
# Shaders 
#
####################

# Define glslc path (adjust if needed)
set(GLSLC "${CMAKE_SOURCE_DIR}/dependencies/Shaderc/bin/glslc.exe")

# Create output directory at configure time
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/sprv_shaders_h")

# List fragment shader sources 
set(FRAG_SHADER_SOURCES 
  "bin/draw_frag.glsl" 
  "source/Examples/E10_TextureResourcePipeline/e10_2d_frag.glsl"
  "source/Examples/E10_TextureResourcePipeline/e10_2d_cube_frag.glsl"
  "source/Examples/E10_TextureResourcePipeline/e10_3d_cube_frag.glsl"
  "source/Examples/E10_TextureResourcePipeline/e10_3d_frag.glsl"
  "source/Examples/E11_NormalMaps/E11_frag_btn.glsl"  
  "source/Examples/E12_Lighting/E12_frag_lighting.glsl"
  "source/Examples/E13_ParallaxOcclusionMapping/E13_POM_frag.glsl"
  "source/Examples/E14_PhysicalBasedRendering/E14_PBR_frag.glsl"
  "source/Examples/E15_Shadowmap/E15_ShadowmapGeneration_frag.glsl"
  "source/Examples/E15_Shadowmap/E15_ShadowmapLighting_frag.glsl"
  "source/Examples/E17_FullSkin/E17_skingeom_frag.glsl"
  "plugins/xgeom_static.plugin/source/runtime/GeomStaticShadowMapCreation_frag.glsl"
  "plugins/xgeom_static.plugin/source/runtime/GeomStaticBasicShader_frag.glsl"
  "plugins/xgeom_skin.plugin/source/runtime/GeomSkinShadowMapCreation_frag.glsl"
  "plugins/xgeom_skin.plugin/source/runtime/GeomSkinBasicShader_frag.glsl"
  "source/Examples/E21_StaticGeomEditor/E21_GridShader_frag.glsl"
  "source/Examples/E21_StaticGeomEditor/E21_WireFrame_frag.glsl"
  "source/Examples/E21_StaticGeomEditor/E21_DebugNormalRender_frag.glsl"
  "source/Examples/E23_SkeletonEditor/E23_Pick_frag.glsl"
  "source/Examples/E23_SkeletonEditor/E23_WedgeFill_frag.glsl"
  "source/Examples/E23_SkeletonEditor/E23_ShadowGeneration_frag.glsl"
  "source/Examples/E27_NodeOS/E27_normal_frag.glsl"
  "source/Examples/E28_FontResourcePipeline/E28_msdf_frag.glsl"
  "source/Examples/E28_FontResourcePipeline/E28_wire_frag.glsl"

  )

# List vertex shader sources 
set(VERT_SHADER_SOURCES 
  "bin/draw_vert.glsl" 
  "bin/imgui_vert.glsl" 
  "source/Examples/E10_TextureResourcePipeline/e10_2d_cube_vert.glsl"
  "source/Examples/E10_TextureResourcePipeline/e10_2d_vert.glsl"
  "source/Examples/E10_TextureResourcePipeline/e10_3d_cube_vert.glsl"
  "source/Examples/E10_TextureResourcePipeline/e10_3d_vert.glsl"
  "source/Examples/E11_NormalMaps/E11_vert_btn.glsl"
  "source/Examples/E12_Lighting/E12_vert_lighting.glsl"
  "source/Examples/E13_ParallaxOcclusionMapping/E13_POM_vert.glsl"
  "source/Examples/E14_PhysicalBasedRendering/E14_PBR_vert.glsl"
  "source/Examples/E15_Shadowmap/E15_ShadowmapGeneration_vert.glsl"
  "source/Examples/E15_Shadowmap/E15_ShadowmapLighting_vert.glsl"
  "source/Examples/E16_Animation/E16_skingeom_vert.glsl"
  "source/Examples/E17_FullSkin/E17_skingeom_vert.glsl"
  "source/Examples/E18_QuantizeSkinVertex/E18_quantize_skingeom_vert.glsl"
  "plugins/xgeom_static.plugin/source/runtime/GeomStaticShadowMapCreation_vert.glsl"
  "plugins/xgeom_static.plugin/source/runtime/GeomStaticBasicShader_vert.glsl"
  "plugins/xgeom_skin.plugin/source/runtime/GeomSkinShadowMapCreation_vert.glsl"
  "plugins/xgeom_skin.plugin/source/runtime/GeomSkinBasicShader_vert.glsl"
  "source/Examples/E21_StaticGeomEditor/E21_GridShader_vert.glsl"
  "source/Examples/E21_StaticGeomEditor/E21_WireFrame_vert.glsl"
  "source/Examples/E21_StaticGeomEditor/E21_DebugNormalRender_vert.glsl"
  "source/Examples/E23_SkeletonEditor/E23_Pick_vert.glsl"
  "source/Examples/E23_SkeletonEditor/E23_WedgeFill_vert.glsl"
  "source/Examples/E23_SkeletonEditor/E23_ShadowGeneration_vert.glsl"
  "source/Examples/E27_NodeOS/E27_normal_vert.glsl"
  "source/Examples/E28_FontResourcePipeline/E28_msdf_vert.glsl"
  "source/Examples/E28_FontResourcePipeline/E28_wire_vert.glsl"
  )

# List geometry shader sources
set(GEOM_SHADER_SOURCES 
  "source/Examples/E21_StaticGeomEditor/E21_WireFrame_geom.glsl"
  "source/Examples/E21_StaticGeomEditor/E21_DebugNormalRender_geom.glsl"
  )

# Generate headers
set(GENERATED_HEADERS)
foreach(SHADER ${FRAG_SHADER_SOURCES})
    get_filename_component(FILENAME ${SHADER} NAME_WE)
    set(OUTPUT_FILE "${CMAKE_BINARY_DIR}/sprv_shaders_h/${FILENAME}.h")
    add_custom_command(
        OUTPUT ${OUTPUT_FILE}
        COMMAND ${GLSLC} -fshader-stage=frag "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER}" -mfmt=num -o ${OUTPUT_FILE}
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER}"
        COMMENT "Compiling fragment shader ${SHADER}"
    )
    list(APPEND GENERATED_HEADERS ${OUTPUT_FILE})
endforeach()

foreach(SHADER ${VERT_SHADER_SOURCES})
    get_filename_component(FILENAME ${SHADER} NAME_WE)
    set(OUTPUT_FILE "${CMAKE_BINARY_DIR}/sprv_shaders_h/${FILENAME}.h")
    add_custom_command(
        OUTPUT ${OUTPUT_FILE}
        COMMAND ${GLSLC} -fshader-stage=vert "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER}" -mfmt=num -o ${OUTPUT_FILE}
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER}"
        COMMENT "Compiling vertex shader ${SHADER}"
    )
    list(APPEND GENERATED_HEADERS ${OUTPUT_FILE})
endforeach()

foreach(SHADER ${GEOM_SHADER_SOURCES})
    get_filename_component(FILENAME ${SHADER} NAME_WE)
    set(OUTPUT_FILE "${CMAKE_BINARY_DIR}/sprv_shaders_h/${FILENAME}.h")
    add_custom_command(
        OUTPUT ${OUTPUT_FILE}
        COMMAND ${GLSLC} -fshader-stage=geom "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER}" -mfmt=num -o ${OUTPUT_FILE}
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER}"
        COMMENT "Compiling geometry shader ${SHADER}"
    )
    list(APPEND GENERATED_HEADERS ${OUTPUT_FILE})
endforeach()

# Include the output directory for headers
set(Vulkan_INCLUDE_DIR "${VK_SDK_PATH}/Include")
set(Vulkan_LIBRARY "${VK_SDK_PATH}/Lib/vulkan-1.lib")

include_directories("${CMAKE_BINARY_DIR}/sprv_shaders_h"
		    ${Vulkan_INCLUDE_DIR} 	)

# Create a single collection for all shaders
set(SHADER_SOURCES ${FRAG_SHADER_SOURCES} ${VERT_SHADER_SOURCES} ${GEOM_SHADER_SOURCES})

# Treat shaders as header files so that we can add them as part of the sources
set_source_files_properties(${SHADER_SOURCES} PROPERTIES HEADER_FILE_ONLY TRUE)

#####################
#
# Cpp
#
####################

# Define the main executable
add_executable(${TARGET_PROJECT}
  "source/Examples/main.cpp"
  "source/Examples/Examples.h"
  "source/Examples/E01_Initialize/E01_Initialize.cpp"
  "source/Examples/E02_UsingUtilities/E02_UsingTools.cpp"
  "source/Examples/E03_ImguiBreatch/E03_ImguiBreach.cpp"
  "source/Examples/E04_Properties/E04_Properties.cpp"
  "source/Examples/E05_Textures/E05_BitmapInspector.h"
  "source/Examples/E05_Textures/E05_Textures.cpp"
  "source/Examples/E06_AssimLoader/E06_AssimLoader.cpp"
  "source/Examples/E06_AssimLoader/E06_LoadTextureHelper.h"
  "source/Examples/E07_PrimitiveGeoms/E07_PrimitiveGeoms.cpp"
  "source/Examples/E08_AsyncTextureLoading/E08_AsyncTextureLoading.cpp"
  "source/Examples/E09_Raw3DViewer/E09_Raw3DViewer.cpp"
  "source/Examples/E10_TextureResourcePipeline/E10_TextureResourcePipeline.cpp"
  "source/Examples/E10_TextureResourcePipeline/E10_Resources.h"
  "source/Examples/E10_TextureResourcePipeline/E10_PluginMgr.h"
  "source/Examples/E10_TextureResourcePipeline/E10_AssetMgr.h"
  "source/Examples/E10_TextureResourcePipeline/E10_AssetBrowser.h"
  "source/Examples/E10_TextureResourcePipeline/E10_asset_browser_virtual_tree_tab.h"
  "source/Examples/E10_TextureResourcePipeline/E10_asset_browser_search_tab.h"
  "source/Examples/E10_TextureResourcePipeline/E10_asset_browser_compiler_tab.h"
  "source/Examples/E11_NormalMaps/E11_NormalMaps.cpp"
  "source/Examples/E12_Lighting/E12_Ligting.cpp"
  "source/Examples/E13_ParallaxOcclusionMapping/E13_POM.cpp"
  "source/Examples/E14_PhysicalBasedRendering/E14_PBR.cpp"
  "source/Examples/E15_Shadowmap/E15_Shadowmap.cpp"
  "source/Examples/E16_Animation/E16_AnimationExample.cpp"
  "source/Examples/E16_Animation/E16_AnimCharacter.h"
  "source/Examples/E17_FullSkin/E17_FullSkinExample.cpp"
  "source/Examples/E18_QuantizeSkinVertex/E18_QuantizeSkinVertex.cpp"
  "source/Examples/E19_MaterialEditor/E19_Material_Editor.cpp"
  "source/Examples/E19_MaterialEditor/E19_Node_Core.h"
  "source/Examples/E19_MaterialEditor/E19_Node_Core.h"
  "source/Examples/E19_MaterialEditor/E19_TextEditor.h"
  "source/Examples/E19_MaterialEditor/E19_TextEditor.cpp"
  "example.lionprj/Cache/Plugins/xmaterial.plugin/source/Graph/xmaterial_graph.cpp"
  "example.lionprj/Cache/Plugins/xmaterial.plugin/source/Graph/xmaterial_graph.h"
  "example.lionprj/Cache/Plugins/xmaterial.plugin/source/Graph/xmaterial_graph_node.cpp"
  "example.lionprj/Cache/Plugins/xmaterial.plugin/source/Graph/xmaterial_graph_node.h"
  "example.lionprj/Cache/Plugins/xmaterial.plugin/source/Graph/xmaterial_graph_node_guid.h"
  "source/Examples/E20_MaterialInstanceEditor/E20_Material_Instance_Editor.cpp"
  "source/Examples/E21_StaticGeomEditor/E21_StaticGeom_Editor.cpp"
  "source/Examples/E22_FramebufferTarget/E22_FramebufferTarget.cpp"
  "source/Examples/E23_SkeletonEditor/E23_Skeleton_Editor.cpp"
  "source/Examples/E24_AnimPackageEditor/E24_AnimPackage_Editor.cpp"
  "source/Examples/E25_SkinGeomEditor/E25_SkinGeom_Editor.cpp"
  "source/Examples/E26_RTCS_Editor/E26_RTCS_Editor.cpp"
  "source/Examples/E27_NodeOS/E27_NodeOS_Editor.cpp"
  "source/Examples/E27_NodeOS/Editor/NodeOS_Common.h"
  "source/Examples/E27_NodeOS/Editor/NodeOS_Types.h"
  "source/Examples/E27_NodeOS/Editor/NodeOS_PropertySerialize.h"
  "source/Examples/E27_NodeOS/Editor/NodeOS_SaveLoad.h"
  "source/Examples/E27_NodeOS/Editor/NodeOS_Interpreter.h"
  "source/Examples/E27_NodeOS/Editor/NodeOS_Codegen.h"
  "source/Examples/E27_NodeOS/Editor/NodeOS_CommandBuilders.h"
  "source/Examples/E27_NodeOS/Editor/NodeOS_CanvasSupport.h"
  "source/Examples/E27_NodeOS/Editor/NodeOS_UI_Panels.h"
  "source/Examples/E27_NodeOS/Editor/NodeOS_UI_Canvas.h"
  "source/Examples/E27_NodeOS/Editor/NodeOS_UI_CommandConsole.h"
  "source/Examples/E27_NodeOS/Editor/NodeOS_CommandContext.h"
  "source/Examples/E27_NodeOS/Editor/NodeOS_Commands_Edit.h"
  "source/Examples/E27_NodeOS/Editor/NodeOS_Commands_Query.h"
  "source/Examples/E27_NodeOS/SDK/xnode_os_plugin_api.h"
  "source/Examples/E27_NodeOS/SDK/xnode_os_host_interface.h"
  "source/Examples/E27_NodeOS/SDK/xnode_os_shared_types.h"
  "source/Examples/E28_FontResourcePipeline/E28_FontResourcePipeline.cpp"
  "example.lionprj/Cache/Plugins/xrtcs.plugin/source/Graph/xrtcs_identity.h"
  "example.lionprj/Cache/Plugins/xrtcs.plugin/source/Graph/xrtcs_template.h"
  "example.lionprj/Cache/Plugins/xrtcs.plugin/source/Graph/xrtcs_resolution.h"
  "example.lionprj/Cache/Plugins/xrtcs.plugin/source/Graph/xrtcs_semantic.h"
  "example.lionprj/Cache/Plugins/xrtcs.plugin/source/Graph/xrtcs_document.h"
  "example.lionprj/Cache/Plugins/xrtcs.plugin/source/Graph/xrtcs_operations.h"
  "example.lionprj/Cache/Plugins/xrtcs.plugin/source/Graph/xrtcs_demo_data.h"


  "source/tools/editors/xgpu_editor_viewport.h"
  "source/tools/editors/xgpu_editor_anim_pose.h"
  "source/tools/editors/xgpu_editor_resource_picker.h"
  "source/tools/xgpu_view.h"
  "source/tools/xgpu_view_inline.h"
  "source/tools/xgpu_xcore_bitmap_helpers.h"
  "source/tools/xgpu_imgui_breach.h"
  "source/tools/xgpu_imgui_breach.cpp"
  "source/tools/xgpu_imgui_timeline.h"
  "source/tools/xgpu_basis_universal_texture_loader.h"
  "source/tools/Import3D/import3d.h"
  "source/tools/Import3D/import3d_animation.h"
  "source/tools/Import3D/import3d_geom.h"
  "source/tools/Import3D/import3d_skeleton.h"
  "dependencies/xproperty/source/examples/imgui/xPropertyImGuiInspector.h"
  "dependencies/xproperty/source/examples/imgui/my_properties.h"
  "dependencies/xproperty/source/examples/imgui/my_property_ui.h"
  "dependencies/xproperty/source/examples/imgui/xPropertyImGuiExample.h"  

  "Readme.md"

  ${GENERATED_HEADERS}
  ${SHADER_SOURCES}
)

# Organize the shades under the folder shaders (At least the default folder)
source_group("Shaders" FILES ${SHADER_SOURCES} ${GENERATED_HEADERS} )

# Add support for assimp paths...
include_directories(${TARGET_PROJECT} PRIVATE "example.lionprj/Cache/Plugins/xgeom_static.plugin/dependencies/assimp/include"
                                              "example.lionprj/Cache/Plugins/xgeom_static.plugin/dependencies/assimp/BINARIES/Win32/include"
                                              "example.lionprj/Cache") 
target_link_directories(${TARGET_PROJECT} PRIVATE "example.lionprj/Cache/Plugins/xgeom_static.plugin")


source_group("Tools" FILES
  "source/tools/xgpu_view.h"
  "source/tools/xgpu_view_inline.h"
  "source/tools/xgpu_xcore_bitmap_helpers.h"
  "source/tools/xgpu_imgui_breach.h"
  "source/tools/xgpu_imgui_breach.cpp"
  "source/tools/xgpu_imgui_timeline.h"
  "source/tools/xgpu_basis_universal_texture_loader.h"
)

source_group("Tools/Import3D" FILES
  "source/tools/Import3D/import3d.h"
  "source/tools/Import3D/import3d_animation.h"
  "source/tools/Import3D/import3d_geom.h"
  "source/tools/Import3D/import3d_skeleton.h"
)

# Organize source files in IDE
source_group("Examples/E01_Initialize" FILES
  "source/Examples/E01_Initialize/E01_Initialize.cpp"
)

source_group("Examples/E02_UsingUtilities" FILES
  "source/Examples/E02_UsingUtilities/E02_UsingTools.cpp"
)

source_group("Examples/E03_ImguiBreatch" FILES
   "source/Examples/E03_ImguiBreatch/E03_ImguiBreach.cpp"
)

source_group("Examples/E04_Properties" FILES
   "source/Examples/E04_Properties/E04_Properties.cpp"
)

source_group("Examples/E04_Properties/xproperty" FILES
  "dependencies/xproperty/source/examples/imgui/xPropertyImGuiInspector.h"
  "dependencies/xproperty/source/examples/imgui/my_properties.h"
  "dependencies/xproperty/source/examples/imgui/my_property_ui.h"
  "dependencies/xproperty/source/examples/imgui/xPropertyImGuiExample.h"  
)

source_group("Examples/E05_Textures" FILES
   "source/Examples/E05_Textures/E05_BitmapInspector.h"
   "source/Examples/E05_Textures/E05_Textures.cpp"
)

source_group("Examples/E06_AssimLoader" FILES
   "source/Examples/E06_AssimLoader/E06_AssimLoader.cpp"
   "source/Examples/E06_AssimLoader/E06_LoadTextureHelper.h"
)

source_group("Examples/E07_PrimitiveGeoms" FILES
   "source/Examples/E07_PrimitiveGeoms/E07_PrimitiveGeoms.cpp"
)

# Include the xgeom_static.plugin so it works
include_directories( "${CMAKE_SOURCE_DIR}/dependencies/example.lionprj/Cache/Plugins/xgeom_static.plugin" )

# Include the xgeom_skin.plugin so it works
include_directories( "${CMAKE_SOURCE_DIR}/dependencies/example.lionprj/Cache/Plugins/xgeom_skin.plugin" )

source_group("Examples/E08_AsyncTextureLoading" FILES
   "source/Examples/E08_AsyncTextureLoading/E08_AsyncTextureLoading.cpp"
)

source_group("Examples/E09_Raw3DViewer" FILES
   "source/Examples/E09_Raw3DViewer/E09_Raw3DViewer.cpp"
)

source_group("Examples/E10_TextureResourcePipeline" FILES
   "source/Examples/E10_TextureResourcePipeline/E10_TextureResourcePipeline.cpp"
   "source/Examples/E10_TextureResourcePipeline/E10_Resources.h"
   "source/Examples/E10_TextureResourcePipeline/E10_PluginMgr.h"
   "source/Examples/E10_TextureResourcePipeline/E10_AssetMgr.h"
)

source_group("Examples/E10_TextureResourcePipeline/AssetBrowser" FILES
   "source/Examples/E10_TextureResourcePipeline/E10_AssetBrowser.h"
   "source/Examples/E10_TextureResourcePipeline/E10_asset_browser_virtual_tree_tab.h"
   "source/Examples/E10_TextureResourcePipeline/E10_asset_browser_search_tab.h"
   "source/Examples/E10_TextureResourcePipeline/E10_asset_browser_compiler_tab.h"
)

# Include the xtexture.plugin so it works
include_directories( "${CMAKE_SOURCE_DIR}/dependencies/example.lionprj/Cache/Plugins/xtexture.plugin" )

source_group("Examples/E10_TextureResourcePipeline/Shaders" FILES
  "${CMAKE_BINARY_DIR}/sprv_shaders_h/e10_2d_cube_frag.h"
  "${CMAKE_BINARY_DIR}/sprv_shaders_h/e10_2d_cube_vert.h"
  "${CMAKE_BINARY_DIR}/sprv_shaders_h/e10_2d_frag.h"
  "${CMAKE_BINARY_DIR}/sprv_shaders_h/e10_2d_vert.h"
  "${CMAKE_BINARY_DIR}/sprv_shaders_h/e10_3d_cube_frag.h"
  "${CMAKE_BINARY_DIR}/sprv_shaders_h/e10_3d_cube_vert.h"
  "${CMAKE_BINARY_DIR}/sprv_shaders_h/e10_3d_frag.h"
  "${CMAKE_BINARY_DIR}/sprv_shaders_h/e10_3d_vert.h"
  "source/Examples/E10_TextureResourcePipeline/e10_2d_cube_frag.glsl"
  "source/Examples/E10_TextureResourcePipeline/e10_2d_cube_vert.glsl"
  "source/Examples/E10_TextureResourcePipeline/e10_2d_frag.glsl"
  "source/Examples/E10_TextureResourcePipeline/e10_2d_vert.glsl"
  "source/Examples/E10_TextureResourcePipeline/e10_3d_cube_frag.glsl"
  "source/Examples/E10_TextureResourcePipeline/e10_3d_cube_vert.glsl"
  "source/Examples/E10_TextureResourcePipeline/e10_3d_frag.glsl"
  "source/Examples/E10_TextureResourcePipeline/e10_3d_vert.glsl"
)

source_group("Examples/E11_NormalMaps" FILES
   "source/Examples/E11_NormalMaps/E11_NormalMaps.cpp"
)

source_group("Examples/E11_NormalMaps/Shaders" FILES
  "source/Examples/E11_NormalMaps/E11_frag_btn.glsl"  
  "source/Examples/E11_NormalMaps/E11_vert_btn.glsl"
  "${CMAKE_BINARY_DIR}/sprv_shaders_h/E11_frag_btn.h"
  "${CMAKE_BINARY_DIR}/sprv_shaders_h/E11_vert_btn.h"
)

source_group("Examples/E12_Lighting" FILES
   "source/Examples/E12_Lighting/E12_Ligting.cpp"
)

source_group("Examples/E12_Lighting/Shaders" FILES
  "source/Examples/E12_Lighting/E12_frag_lighting.glsl"  
  "source/Examples/E12_Lighting/E12_vert_lighting.glsl"
  "${CMAKE_BINARY_DIR}/sprv_shaders_h/E12_frag_lighting.h"
  "${CMAKE_BINARY_DIR}/sprv_shaders_h/E12_vert_lighting.h"
)

source_group("Examples/E13_ParallaxOcclusionMapping" FILES
   "source/Examples/E13_ParallaxOcclusionMapping/E13_POM.cpp"
)

source_group("Examples/E13_ParallaxOcclusionMapping/Shaders" FILES
  "source/Examples/E13_ParallaxOcclusionMapping/E13_POM_frag.glsl"
  "source/Examples/E13_ParallaxOcclusionMapping/E13_POM_vert.glsl"
  "${CMAKE_BINARY_DIR}/sprv_shaders_h/E13_POM_frag.h"
  "${CMAKE_BINARY_DIR}/sprv_shaders_h/E13_POM_vert.h"
)

source_group("Examples/E14_PhysicalBasedRendering" FILES
   "source/Examples/E14_PhysicalBasedRendering/E14_PBR.cpp"
)

source_group("Examples/E14_PhysicalBasedRendering/Shaders" FILES
  "source/Examples/E14_PhysicalBasedRendering/E14_PBR_frag.glsl"
  "source/Examples/E14_PhysicalBasedRendering/E14_PBR_vert.glsl"
  "${CMAKE_BINARY_DIR}/sprv_shaders_h/E14_PBR_frag.h"
  "${CMAKE_BINARY_DIR}/sprv_shaders_h/E14_PBR_vert.h"
)

source_group("Examples/E15_Shadowmap" FILES
   "source/Examples/E15_Shadowmap/E15_Shadowmap.cpp"
)

source_group("Examples/E15_Shadowmap/Shaders" FILES
  "source/Examples/E15_Shadowmap/E15_ShadowmapGeneration_vert.glsl"
  "source/Examples/E15_Shadowmap/E15_ShadowmapLighting_vert.glsl"
  "source/Examples/E15_Shadowmap/E15_ShadowmapGeneration_frag.glsl"
  "source/Examples/E15_Shadowmap/E15_ShadowmapLighting_frag.glsl"
  "${CMAKE_BINARY_DIR}/sprv_shaders_h/E15_ShadowmapGeneration_vert.h"
  "${CMAKE_BINARY_DIR}/sprv_shaders_h/E15_ShadowmapLighting_vert.h"
  "${CMAKE_BINARY_DIR}/sprv_shaders_h/E15_ShadowmapGeneration_frag.h"
  "${CMAKE_BINARY_DIR}/sprv_shaders_h/E15_ShadowmapLighting_frag.h"
)

source_group("Examples/E16_Animation" FILES
   "source/Examples/E16_Animation/E16_AnimationExample.cpp"
   "source/Examples/E16_Animation/E16_AnimCharacter.h"
)

source_group("Examples/E16_Animation/Shaders" FILES
  "source/Examples/E16_Animation/E16_skingeom_vert.glsl"
  "${CMAKE_BINARY_DIR}/sprv_shaders_h/E16_skingeom_vert.h"
)

source_group("Examples/E17_FullSkin" FILES
   "source/Examples/E17_FullSkin/E17_FullSkinExample.cpp"
)

source_group("Examples/E17_FullSkin/Shaders" FILES
  "source/Examples/E17_FullSkin/E17_skingeom_frag.glsl"
  "source/Examples/E17_FullSkin/E17_skingeom_vert.glsl"
  "${CMAKE_BINARY_DIR}/sprv_shaders_h/E17_skingeom_frag.h"
  "${CMAKE_BINARY_DIR}/sprv_shaders_h/E17_skingeom_vert.h"
)

source_group("Examples/E18_QuantizeSkinVertex" FILES
   "source/Examples/E18_QuantizeSkinVertex/E18_QuantizeSkinVertex.cpp"
)

source_group("Examples/E18_QuantizeSkinVertex/Shaders" FILES
  "source/Examples/E18_QuantizeSkinVertex/E18_quantize_skingeom_vert.glsl"
  "${CMAKE_BINARY_DIR}/sprv_shaders_h/E18_quantize_skingeom_vert.h"
)

source_group("Examples/E19_MaterialEditor" FILES
   "source/Examples/E19_MaterialEditor/E19_Material_Editor.cpp"
   "source/Examples/E19_MaterialEditor/E19_Node_Core.h"
   "source/Examples/E19_MaterialEditor/E19_TextEditor.h"
   "source/Examples/E19_MaterialEditor/E19_TextEditor.cpp"
)

# Include the material.plugin so it works
include_directories( "${CMAKE_SOURCE_DIR}/dependencies/example.lionprj/Cache/Plugins/xmaterial.plugin" )

# We also need to predefine EDITOR 
target_compile_definitions(${TARGET_PROJECT} PRIVATE EDITOR)

source_group("Examples/E19_MaterialEditor/graph" FILES
  "example.lionprj/Cache/Plugins/xmaterial.plugin/source/Graph/xmaterial_graph.cpp"
  "example.lionprj/Cache/Plugins/xmaterial.plugin/source/Graph/xmaterial_graph.h"
  "example.lionprj/Cache/Plugins/xmaterial.plugin/source/Graph/xmaterial_graph_node.cpp"
  "example.lionprj/Cache/Plugins/xmaterial.plugin/source/Graph/xmaterial_graph_node.h"
  "example.lionprj/Cache/Plugins/xmaterial.plugin/source/Graph/xmaterial_graph_node_guid.h"
)

source_group("Examples/E20_MaterialInstanceEditor" FILES
   "source/Examples/E20_MaterialInstanceEditor/E20_Material_Instance_Editor.cpp"
)

source_group("Examples/E21_StaticGeomEditor" FILES
   "source/Examples/E21_StaticGeomEditor/E21_StaticGeom_Editor.cpp"
)

source_group("Examples/E21_StaticGeomEditor/Shaders" FILES
  "plugins/xgeom_static.plugin/source/runtime/GeomStaticShadowMapCreation_vert.glsl"
  "plugins/xgeom_static.plugin/source/runtime/GeomStaticShadowMapCreation_frag.glsl"
  "plugins/xgeom_static.plugin/source/runtime/GeomStaticBasicShader_vert.glsl"
  "plugins/xgeom_static.plugin/source/runtime/GeomStaticBasicShader_frag.glsl"
  "source/Examples/E21_StaticGeomEditor/E21_GridShader_frag.glsl"
  "source/Examples/E21_StaticGeomEditor/E21_GridShader_vert.glsl"
  "source/Examples/E21_StaticGeomEditor/E21_WireFrame_frag.glsl"
  "source/Examples/E21_StaticGeomEditor/E21_WireFrame_vert.glsl"
  "source/Examples/E21_StaticGeomEditor/E21_WireFrame_geom.glsl"
  "source/Examples/E21_StaticGeomEditor/E21_DebugNormalRender_vert.glsl"
  "source/Examples/E21_StaticGeomEditor/E21_DebugNormalRender_geom.glsl"
  "source/Examples/E21_StaticGeomEditor/E21_DebugNormalRender_frag.glsl"
)

source_group("Examples/E21_StaticGeomEditor/Shaders/Headers" FILES
  "${CMAKE_BINARY_DIR}/sprv_shaders_h/GeomStaticShadowMapCreation_vert.h"
  "${CMAKE_BINARY_DIR}/sprv_shaders_h/GeomStaticShadowMapCreation_frag.h"
  "${CMAKE_BINARY_DIR}/sprv_shaders_h/GeomStaticBasicShader_vert.h"
  "${CMAKE_BINARY_DIR}/sprv_shaders_h/GeomStaticBasicShader_frag.h"
  "${CMAKE_BINARY_DIR}/sprv_shaders_h/E21_GridShader_vert.h"
  "${CMAKE_BINARY_DIR}/sprv_shaders_h/E21_GridShader_frag.h"
  "${CMAKE_BINARY_DIR}/sprv_shaders_h/E21_WireFrame_frag.h"
  "${CMAKE_BINARY_DIR}/sprv_shaders_h/E21_WireFrame_vert.h"
  "${CMAKE_BINARY_DIR}/sprv_shaders_h/E21_WireFrame_geom.h"
  "${CMAKE_BINARY_DIR}/sprv_shaders_h/E21_DebugNormalRender_vert.h"
  "${CMAKE_BINARY_DIR}/sprv_shaders_h/E21_DebugNormalRender_geom.h"
  "${CMAKE_BINARY_DIR}/sprv_shaders_h/E21_DebugNormalRender_frag.h"
)


source_group("Examples/E22_FramebufferTarget" FILES
   "source/Examples/E22_FramebufferTarget/E22_FramebufferTarget.cpp"
)

source_group("Examples/E23_SkeletonEditor" FILES
   "source/Examples/E23_SkeletonEditor/E23_Skeleton_Editor.cpp"
   "source/Examples/E23_SkeletonEditor/E23_Pick_frag.glsl"
   "source/Examples/E23_SkeletonEditor/E23_Pick_vert.glsl"
   "source/Examples/E23_SkeletonEditor/E23_WedgeFill_frag.glsl"
   "source/Examples/E23_SkeletonEditor/E23_WedgeFill_vert.glsl"
   "source/Examples/E23_SkeletonEditor/E23_ShadowGeneration_frag.glsl"
   "source/Examples/E23_SkeletonEditor/E23_ShadowGeneration_vert.glsl"
)

source_group("Examples/E24_AnimPackageEditor" FILES
   "source/Examples/E24_AnimPackageEditor/E24_AnimPackage_Editor.cpp"
)

# Include the xrtcs.plugin so it works - not a resource-pipeline plugin (see its README.md), so
# just its Graph/ headers, compiled directly into this target like xmaterial_graph's above.
include_directories( "${CMAKE_SOURCE_DIR}/example.lionprj/Cache/Plugins/xrtcs.plugin" )

source_group("Examples/E26_RTCS_Editor" FILES
   "source/Examples/E26_RTCS_Editor/E26_RTCS_Editor.cpp"
)

source_group("Examples/E26_RTCS_Editor/graph" FILES
  "example.lionprj/Cache/Plugins/xrtcs.plugin/source/Graph/xrtcs_identity.h"
  "example.lionprj/Cache/Plugins/xrtcs.plugin/source/Graph/xrtcs_template.h"
  "example.lionprj/Cache/Plugins/xrtcs.plugin/source/Graph/xrtcs_resolution.h"
  "example.lionprj/Cache/Plugins/xrtcs.plugin/source/Graph/xrtcs_semantic.h"
  "example.lionprj/Cache/Plugins/xrtcs.plugin/source/Graph/xrtcs_document.h"
  "example.lionprj/Cache/Plugins/xrtcs.plugin/source/Graph/xrtcs_operations.h"
  "example.lionprj/Cache/Plugins/xrtcs.plugin/source/Graph/xrtcs_demo_data.h"
)

# xundo_system.h (used by E27_NodeOS_Editor.cpp for its command/undo layer) does a bare
# #include "xcmdline_parser.h" rather than a repo-root-relative path, so xcmdline's own source
# folder needs to be on the include path directly - nothing else currently adds it.
include_directories( "${CMAKE_SOURCE_DIR}/dependencies/xcmdline/source" )

# NodeOS's plugin ABI headers only - the plugin .cpp files themselves
# (source/Examples/E27_NodeOS/Plugins/**) are deliberately NOT part of this list: the whole point
# is that they are compiled live, by the running E27 host shelling out to the local MSVC toolchain,
# never by this project's own build.
source_group("Examples/E27_NodeOS" FILES
   "source/Examples/E27_NodeOS/E27_NodeOS_Editor.cpp"
)
# The 13 headers E27_NodeOS_Editor.cpp was split into (plus the shared NodeOS_Common.h include
# header) - grouped into their own Solution Explorer folder so they're easy to find/browse
# separately from the now-much-shorter .cpp that just #includes them in dependency order.
source_group("Examples/E27_NodeOS/Editor" FILES
  "source/Examples/E27_NodeOS/Editor/NodeOS_Common.h"
  "source/Examples/E27_NodeOS/Editor/NodeOS_Types.h"
  "source/Examples/E27_NodeOS/Editor/NodeOS_PropertySerialize.h"
  "source/Examples/E27_NodeOS/Editor/NodeOS_SaveLoad.h"
  "source/Examples/E27_NodeOS/Editor/NodeOS_Interpreter.h"
  "source/Examples/E27_NodeOS/Editor/NodeOS_Codegen.h"
  "source/Examples/E27_NodeOS/Editor/NodeOS_CommandBuilders.h"
  "source/Examples/E27_NodeOS/Editor/NodeOS_CanvasSupport.h"
  "source/Examples/E27_NodeOS/Editor/NodeOS_UI_Panels.h"
  "source/Examples/E27_NodeOS/Editor/NodeOS_UI_Canvas.h"
  "source/Examples/E27_NodeOS/Editor/NodeOS_UI_CommandConsole.h"
  "source/Examples/E27_NodeOS/Editor/NodeOS_CommandContext.h"
  "source/Examples/E27_NodeOS/Editor/NodeOS_Commands_Edit.h"
  "source/Examples/E27_NodeOS/Editor/NodeOS_Commands_Query.h"
)
source_group("Examples/E27_NodeOS/SDK" FILES
  "source/Examples/E27_NodeOS/SDK/xnode_os_plugin_api.h"
  "source/Examples/E27_NodeOS/SDK/xnode_os_host_interface.h"
  "source/Examples/E27_NodeOS/SDK/xnode_os_shared_types.h"
)

source_group("Examples/E28_FontResourcePipeline" FILES
  "source/Examples/E28_FontResourcePipeline/E28_FontResourcePipeline.cpp"
)


source_group("" FILES
  "source/Examples/main.cpp"
  "source/Examples/Examples.h"
  "Readme.md"
)

# ADD SUPPORT FOR OPEN_MP
find_package(OpenMP REQUIRED)
target_link_libraries(${TARGET_PROJECT} PRIVATE OpenMP::OpenMP_CXX)

# The hardened xproperty header instantiates noticeably more templates per translation unit,
# which pushes some examples (E10_TextureResourcePipeline) past MSVC's COFF section limit.
if(MSVC)
  target_compile_options(${TARGET_PROJECT} PRIVATE /bigobj)
endif()



# NodeOSCLI - a tiny, standalone client for E27_NodeOS's Command Console named pipe (see the
# top comment in NodeOSCLI.cpp). Deliberately its own add_executable, not folded into
# ${TARGET_PROJECT}: it has zero dependency on Vulkan/ImGui/xproperty/etc., just Win32 + iostream,
# so it stays a genuinely separate, fast-building CLI tool.
add_executable(NodeOSCLI "source/Examples/E27_NodeOS/NodeOSCLI.cpp")
set_target_properties(NodeOSCLI PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

# Add dependency subdirectory
add_subdirectory("build/dependency" "${CMAKE_CURRENT_BINARY_DIR}/xGPUExamples")

# Process components
ProcessComponents()
