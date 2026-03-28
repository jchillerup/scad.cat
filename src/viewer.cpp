// GL headers first — GL_GLEXT_PROTOTYPES enables GL3+ function declarations on
// Linux/Mesa without needing GLAD or GLEW.  On WASM everything is in gles3.
#ifndef __EMSCRIPTEN__
#  ifndef GL_GLEXT_PROTOTYPES
#    define GL_GLEXT_PROTOTYPES
#  endif
#  include <GL/gl.h>
#  include <GL/glext.h>
#else
#  include <GLES3/gl3.h>
#endif

#include "viewer.h"
#include "ImGuizmo.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <fstream>

// ---------------------------------------------------------------------------
// Shader helpers
// ---------------------------------------------------------------------------

static GLuint CompileShader(GLenum type, const char* src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512]; glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        fprintf(stderr, "[viewer] shader error: %s\n", log);
        glDeleteShader(s); return 0;
    }
    return s;
}

static GLuint LinkProgram(GLuint vert, GLuint frag)
{
    GLuint p = glCreateProgram();
    glAttachShader(p, vert);
    glAttachShader(p, frag);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512]; glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        fprintf(stderr, "[viewer] link error: %s\n", log);
        glDeleteProgram(p); return 0;
    }
    return p;
}

// ---------------------------------------------------------------------------
// MeshViewer::Init
// ---------------------------------------------------------------------------

void MeshViewer::Init(const char* glsl_version)
{
    glsl_version_ = glsl_version;

    // GLES / WebGL needs precision qualifier in frag shader
    bool es = (strstr(glsl_version, "es") != nullptr);
    const char* prec = es ? "precision mediump float;\n" : "";

    const char* vert_body = R"(
in vec3 aPos;
in vec3 aNormal;
out vec3 vNormal;
out vec3 vFragPos;
uniform mat4 uMVP;
uniform mat4 uModel;
void main() {
    vec4 wp   = uModel * vec4(aPos, 1.0);
    vFragPos  = wp.xyz;
    vNormal   = mat3(uModel) * aNormal;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

    const char* frag_body = R"(
in vec3 vNormal;
in vec3 vFragPos;
out vec4 FragColor;
uniform vec3 uLightDir;
uniform vec3 uColor;
void main() {
    vec3  n    = normalize(vNormal);
    float diff = max(dot(n, uLightDir), 0.0);
    FragColor  = vec4((0.25 + diff * 0.75) * uColor, 1.0);
}
)";

    std::string vs = std::string(glsl_version) + "\n" + vert_body;
    std::string fs = std::string(glsl_version) + "\n" + prec + frag_body;

    GLuint vert = CompileShader(GL_VERTEX_SHADER,   vs.c_str());
    GLuint frag = CompileShader(GL_FRAGMENT_SHADER, fs.c_str());
    program_ = LinkProgram(vert, frag);
    glDeleteShader(vert);
    glDeleteShader(frag);

    u_mvp_      = glGetUniformLocation(program_, "uMVP");
    u_model_    = glGetUniformLocation(program_, "uModel");
    u_light_dir_= glGetUniformLocation(program_, "uLightDir");
    u_color_    = glGetUniformLocation(program_, "uColor");

    // Default view: isometric-ish angle
    glm::mat4 v = glm::lookAt(
        glm::normalize(glm::vec3(1.0f, 0.7f, 1.5f)) * 50.0f,
        glm::vec3(0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));
    memcpy(view_, glm::value_ptr(v), sizeof(view_));
}

// ---------------------------------------------------------------------------
// MeshViewer::Shutdown
// ---------------------------------------------------------------------------

void MeshViewer::Shutdown()
{
    if (vbo_)       { glDeleteBuffers(1, &vbo_);              vbo_       = 0; }
    if (vao_)       { glDeleteVertexArrays(1, &vao_);         vao_       = 0; }
    if (program_)   { glDeleteProgram(program_);              program_   = 0; }
    if (fbo_)       { glDeleteFramebuffers(1, &fbo_);         fbo_       = 0; }
    if (color_tex_) { glDeleteTextures(1, &color_tex_);       color_tex_ = 0; }
    if (depth_rbo_) { glDeleteRenderbuffers(1, &depth_rbo_);  depth_rbo_ = 0; }
}

// ---------------------------------------------------------------------------
// STL parse helper — shared by LoadSTL and LoadSTLFromMemory
// ---------------------------------------------------------------------------

static bool ParseSTL(const uint8_t* data, size_t len,
                     std::vector<float>& out_buf,
                     glm::vec3& out_bb_min, glm::vec3& out_bb_max,
                     int& out_tri_count)
{
    if (len < 84) return false;

    uint32_t n_tri = 0;
    memcpy(&n_tri, data + 80, 4);
    if (n_tri == 0 || 84 + n_tri * 50 > len) return false;

    out_buf.reserve(n_tri * 18);
    out_bb_min = glm::vec3(1e9f);
    out_bb_max = glm::vec3(-1e9f);

    const uint8_t* p = data + 84;
    for (uint32_t i = 0; i < n_tri; ++i) {
        float nx, ny, nz;
        memcpy(&nx, p,      4); memcpy(&ny, p + 4,  4); memcpy(&nz, p + 8,  4);
        p += 12;
        for (int v = 0; v < 3; ++v) {
            float x, y, z;
            memcpy(&x, p, 4); memcpy(&y, p+4, 4); memcpy(&z, p+8, 4);
            p += 12;
            out_buf.push_back(x);  out_buf.push_back(y);  out_buf.push_back(z);
            out_buf.push_back(nx); out_buf.push_back(ny); out_buf.push_back(nz);
            out_bb_min = glm::min(out_bb_min, glm::vec3(x, y, z));
            out_bb_max = glm::max(out_bb_max, glm::vec3(x, y, z));
        }
        p += 2; // attribute byte count
    }
    out_tri_count = (int)n_tri;
    return true;
}

// ---------------------------------------------------------------------------
// MeshViewer — shared GPU upload after parsing
// ---------------------------------------------------------------------------

bool MeshViewer::UploadMesh(std::vector<float>& buf,
                             const glm::vec3& bb_min, const glm::vec3& bb_max,
                             int n_tri)
{
    // Center mesh at origin
    glm::vec3 center = (bb_min + bb_max) * 0.5f;
    for (int i = 0; i < (int)buf.size(); i += 6) {
        buf[i]   -= center.x;
        buf[i+1] -= center.y;
        buf[i+2] -= center.z;
    }
    scene_radius_ = glm::length(bb_max - bb_min) * 0.5f;
    if (scene_radius_ < 0.001f) scene_radius_ = 1.0f;

    tri_count_ = n_tri;

    if (!vao_) glGenVertexArrays(1, &vao_);
    if (!vbo_) glGenBuffers(1, &vbo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, buf.size() * sizeof(float), buf.data(), GL_STATIC_DRAW);

    GLint pos_loc = glGetAttribLocation(program_, "aPos");
    GLint nrm_loc = glGetAttribLocation(program_, "aNormal");
    glEnableVertexAttribArray(pos_loc);
    glVertexAttribPointer(pos_loc, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
    glEnableVertexAttribArray(nrm_loc);
    glVertexAttribPointer(nrm_loc, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));
    glBindVertexArray(0);

    // Frame the model
    float dist = scene_radius_ * 3.0f;
    glm::mat4 v = glm::lookAt(
        glm::normalize(glm::vec3(1.0f, 0.7f, 1.5f)) * dist,
        glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    memcpy(view_, glm::value_ptr(v), sizeof(view_));
    return true;
}

// ---------------------------------------------------------------------------
// MeshViewer::LoadSTL   (binary STL from file)
// ---------------------------------------------------------------------------

bool MeshViewer::LoadSTL(const char* path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) { fprintf(stderr, "[viewer] cannot open %s\n", path); return false; }
    std::vector<uint8_t> data(
        (std::istreambuf_iterator<char>(f)),
        std::istreambuf_iterator<char>());
    return LoadSTLFromMemory(data.data(), data.size());
}

// ---------------------------------------------------------------------------
// MeshViewer::LoadSTLFromMemory   (binary STL from buffer)
// ---------------------------------------------------------------------------

bool MeshViewer::LoadSTLFromMemory(const uint8_t* data, size_t len)
{
    std::vector<float> buf;
    glm::vec3 bb_min, bb_max;
    int n_tri = 0;
    if (!ParseSTL(data, len, buf, bb_min, bb_max, n_tri)) {
        fprintf(stderr, "[viewer] STL parse failed\n");
        return false;
    }
    return UploadMesh(buf, bb_min, bb_max, n_tri);
}

// ---------------------------------------------------------------------------
// MeshViewer::ResizeFBO
// ---------------------------------------------------------------------------

void MeshViewer::ResizeFBO(int w, int h)
{
    if (w == fbo_w_ && h == fbo_h_) return;
    fbo_w_ = w; fbo_h_ = h;

    if (fbo_)       glDeleteFramebuffers(1, &fbo_);
    if (color_tex_) glDeleteTextures(1, &color_tex_);
    if (depth_rbo_) glDeleteRenderbuffers(1, &depth_rbo_);

    glGenTextures(1, &color_tex_);
    glBindTexture(GL_TEXTURE_2D, color_tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glGenRenderbuffers(1, &depth_rbo_);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_rbo_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_tex_, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_rbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ---------------------------------------------------------------------------
// MeshViewer::Draw  —  call inside an ImGui window
// ---------------------------------------------------------------------------

void MeshViewer::Draw()
{
    ImVec2 canvas_pos  = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    int w = (int)canvas_size.x, h = (int)canvas_size.y;
    if (w < 1 || h < 1) return;

    ResizeFBO(w, h);

    // ---- Render mesh to FBO -----------------------------------------------
    GLint prev_fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, w, h);
    glClearColor(0.13f, 0.13f, 0.16f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    if (tri_count_ > 0 && program_ && vao_) {
        glm::mat4 view  = glm::make_mat4(view_);
        glm::mat4 proj  = glm::perspective(glm::radians(45.0f),
                                            (float)w / (float)h,
                                            scene_radius_ * 0.01f,
                                            scene_radius_ * 100.0f);
        glm::mat4 model(1.0f);
        glm::mat4 mvp   = proj * view * model;

        glm::vec3 light = glm::normalize(glm::vec3(0.6f, 1.0f, 0.8f));
        float     col[] = {0.62f, 0.73f, 0.87f};

        glUseProgram(program_);
        glUniformMatrix4fv(u_mvp_,       1, GL_FALSE, glm::value_ptr(mvp));
        glUniformMatrix4fv(u_model_,     1, GL_FALSE, glm::value_ptr(model));
        glUniform3fv(u_light_dir_,       1, glm::value_ptr(light));
        glUniform3fv(u_color_,           1, col);

        glBindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLES, 0, tri_count_ * 3);
        glBindVertexArray(0);
        glUseProgram(0);
    }

    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);

    // ---- Display in ImGui window ------------------------------------------
    // Flip UV vertically: OpenGL FBO origin is bottom-left, ImGui is top-left
    ImGui::Image((ImTextureID)(intptr_t)color_tex_, canvas_size,
                 ImVec2(0, 1), ImVec2(1, 0));

    if (tri_count_ == 0)
        ImGui::GetWindowDrawList()->AddText(
            {canvas_pos.x + 10, canvas_pos.y + 10},
            IM_COL32(180, 180, 180, 255), "No model loaded");

    // ---- Mouse orbit / zoom (only when hovering the image) ----------------
    if (ImGui::IsItemHovered()) {
        ImGuiIO& io = ImGui::GetIO();

        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
            float dx = io.MouseDelta.x;
            float dy = io.MouseDelta.y;

            glm::mat4 view  = glm::make_mat4(view_);
            glm::vec3 cam   = glm::vec3(glm::inverse(view)[3]);
            float     dist  = glm::length(cam);
            if (dist < 1e-5f) dist = 1e-5f;

            // Horizontal: orbit around world Y
            glm::vec3 off = glm::vec3(
                glm::rotate(glm::mat4(1.0f),
                            glm::radians(-dx * 0.4f),
                            glm::vec3(0, 1, 0)) * glm::vec4(cam, 0));

            // Vertical: orbit around camera right axis
            glm::vec3 right = glm::normalize(
                glm::vec3(view[0][0], view[1][0], view[2][0]));
            off = glm::vec3(
                glm::rotate(glm::mat4(1.0f),
                            glm::radians(-dy * 0.4f),
                            right) * glm::vec4(off, 0));

            // Clamp: avoid flipping at poles
            glm::vec3 dir = glm::normalize(off);
            if (std::abs(dir.y) < 0.99f) {
                glm::mat4 nv = glm::lookAt(
                    dir * dist, glm::vec3(0), glm::vec3(0, 1, 0));
                memcpy(view_, glm::value_ptr(nv), sizeof(view_));
            }
        }

        if (io.MouseWheel != 0.0f) {
            glm::mat4 view  = glm::make_mat4(view_);
            glm::vec3 cam   = glm::vec3(glm::inverse(view)[3]);
            float     dist  = glm::length(cam) * std::pow(0.85f, io.MouseWheel);
            dist = glm::clamp(dist, scene_radius_ * 0.1f, scene_radius_ * 50.0f);
            glm::mat4 nv = glm::lookAt(
                glm::normalize(cam) * dist, glm::vec3(0), glm::vec3(0, 1, 0));
            memcpy(view_, glm::value_ptr(nv), sizeof(view_));
        }
    }

    // ---- ImGuizmo view cube (top-right corner of viewport) ----------------
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(canvas_pos.x, canvas_pos.y, (float)w, (float)h);

    float gizmo_sz = 90.0f;
    glm::mat4 cur_view = glm::make_mat4(view_);
    glm::vec3 cam_pos  = glm::vec3(glm::inverse(cur_view)[3]);
    float     cam_dist = glm::length(cam_pos);

    ImGuizmo::ViewManipulate(
        view_,
        cam_dist,
        ImVec2(canvas_pos.x + w - gizmo_sz, canvas_pos.y + 4),
        ImVec2(gizmo_sz, gizmo_sz),
        0x10101090);
}
