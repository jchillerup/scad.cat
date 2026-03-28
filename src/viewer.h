#pragma once
#include "imgui.h"
#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

// FBO-based 3D mesh viewer with orbit camera and ImGuizmo view cube.
// Call Init() after GL context is created, Shutdown() before it's destroyed.
// LoadSTL() accepts binary STL. Draw() must be called inside an ImGui window.
class MeshViewer {
public:
    void Init(const char* glsl_version);
    void Shutdown();
    bool LoadSTL(const char* path);
    bool LoadSTLFromMemory(const uint8_t* data, size_t len);
    void Draw(); // call inside ImGui::Begin / ImGui::End

private:
    void ResizeFBO(int w, int h);
    bool UploadMesh(std::vector<float>& buf,
                    const glm::vec3& bb_min, const glm::vec3& bb_max, int n_tri);

    const char* glsl_version_ = nullptr;

    // Shader
    uint32_t program_  = 0;
    int u_mvp_         = -1;
    int u_model_       = -1;
    int u_light_dir_   = -1;
    int u_color_       = -1;

    // Mesh
    uint32_t vao_      = 0;
    uint32_t vbo_      = 0;
    int      tri_count_= 0;
    float    scene_radius_ = 1.0f;

    // FBO
    uint32_t fbo_       = 0;
    uint32_t color_tex_ = 0;
    uint32_t depth_rbo_ = 0;
    int      fbo_w_     = 0;
    int      fbo_h_     = 0;

    // View matrix (column-major float[16], GLM / OpenGL convention)
    float view_[16] = {};
};
