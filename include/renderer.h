#pragma once
#include <GLES3/gl3.h>
#include <cstdint>
#include "input.h"

// ─────────────────────────────────────────────────
//  OpenGL ES Renderer
//  Menggambar: lingkaran, kotak, teks bitmap
// ─────────────────────────────────────────────────

class Renderer {
public:
    bool init(int screen_w, int screen_h);
    void resize(int w, int h);
    void shutdown();

    // Primitif
    void drawRect(float x, float y, float w, float h, const Color& c);
    void drawCircle(float cx, float cy, float r, const Color& c, int segments=32);
    void drawRing(float cx, float cy, float r, float thickness, const Color& c, int segments=32);
    void drawText(const char* text, float x, float y, float scale, const Color& c);
    float textWidth(const char* text, float scale);

    // Komponen UI
    void drawButton(const VButton& btn, bool edit_mode);
    void drawJoystick(const Joystick& js, bool edit_mode);
    void drawCursor(const VirtualMouse& mouse);
    void drawEditBar();          // toolbar edit di atas layar
    void drawLookArea(float x);  // garis batas area look

    int sw() const { return m_sw; }
    int sh() const { return m_sh; }

private:
    GLuint compileShader(GLenum type, const char* src);
    GLuint createProgram(const char* vert, const char* frag);
    void   buildFontTexture();
    void   uploadQuad(float* verts6x4);  // 6 vertex, masing2 x,y,u,v

    GLuint m_prog_solid = 0;
    GLuint m_prog_text  = 0;
    GLuint m_vao = 0, m_vbo = 0;
    GLuint m_font_tex = 0;
    int    m_sw = 0, m_sh = 0;
    bool   m_ready = false;

    // Font 8x8 bitmap ASCII 32-127
    static const uint8_t FONT_DATA[96][8];
};

extern Renderer g_renderer;

