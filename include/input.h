
#pragma once
#include <string>
#include <cstdint>

// ─────────────────────────────────
//  WARNA RGBA
// ─────────────────────────────────
struct Color {
    float r, g, b, a;
    Color(float r=1,float g=1,float b=1,float a=1): r(r),g(g),b(b),a(a){}
};

// Warna preset
namespace Colors {
    static Color WHITE   (1.0f, 1.0f, 1.0f, 1.0f);
    static Color BLACK   (0.0f, 0.0f, 0.0f, 1.0f);
    static Color RED     (0.9f, 0.2f, 0.2f, 1.0f);
    static Color GREEN   (0.2f, 0.8f, 0.2f, 1.0f);
    static Color BLUE    (0.2f, 0.4f, 0.9f, 1.0f);
    static Color YELLOW  (1.0f, 0.9f, 0.1f, 1.0f);
    static Color GRAY    (0.5f, 0.5f, 0.5f, 0.85f);
    static Color DARK    (0.1f, 0.1f, 0.1f, 0.75f);
    static Color ORANGE  (1.0f, 0.5f, 0.1f, 1.0f);
}

// ─────────────────────────────────
//  JENIS AKSI TOMBOL
// ─────────────────────────────────
enum class BtnAction {
    ATTACK,      // klik kiri = serang/hancur block
    USE,         // klik kanan = pakai/tempatkan block
    JUMP,        // spasi
    SNEAK,       // shift
    SPRINT,      // ctrl / double-tap W
    INVENTORY,   // E
    DROP,        // Q
    CHAT,        // T
    PERSPECTIVE, // F5
    NONE,
};

// ─────────────────────────────────
//  TOMBOL VIRTUAL
// ─────────────────────────────────
struct VButton {
    std::string label;
    BtnAction   action;
    float       x, y;        // posisi pusat (pixel)
    float       size;        // diameter
    bool        pressed   = false;
    bool        visible   = true;
    bool        dragging  = false;   // sedang didrag posisinya
    int         touch_id  = -1;      // pointer ID yg menekan
    float       drag_ox   = 0;       // offset drag
    float       drag_oy   = 0;
    uint64_t    press_time = 0;      // untuk deteksi long press
    Color       color_idle;
    Color       color_pressed;
    Color       color_label;

    VButton() {}
    VButton(const std::string& lbl, BtnAction act,
            float x, float y, float sz,
            Color ci, Color cp, Color cl = Colors::WHITE)
        : label(lbl), action(act), x(x), y(y), size(sz),
          color_idle(ci), color_pressed(cp), color_label(cl) {}
};

// ─────────────────────────────────
//  JOYSTICK WASD
// ─────────────────────────────────
struct Joystick {
    float cx, cy;        // posisi pusat joystick
    float base_radius;   // radius lingkaran luar
    float knob_radius;   // radius bulatan dalam

    float knob_x, knob_y;  // posisi knob saat ini
    float dx, dy;           // arah (-1 to 1)

    bool  active     = false;
    int   touch_id   = -1;
    bool  dragging   = false; // drag posisi joystick
    float drag_ox    = 0, drag_oy = 0;
    uint64_t press_time = 0;

    // State WASD
    bool W = false, A = false, S = false, D = false;
};

// ─────────────────────────────────
//  VIRTUAL MOUSE CURSOR
// ─────────────────────────────────
struct VirtualMouse {
    float cx, cy;         // posisi cursor saat ini
    float sensitivity;    // sensitivitas look

    // Area look: separuh kanan layar
    float look_start_x;   // batas kiri area look
    int   touch_id = -1;
    float last_x, last_y;
    bool  active = false;

    bool  left_down  = false;  // mouse kiri ditekan
    bool  right_down = false;  // mouse kanan ditekan
};

// ─────────────────────────────────
//  MODE EDIT LAYOUT
// ─────────────────────────────────
enum class EditMode {
    PLAY,   // mode main normal
    EDIT,   // drag tombol untuk pindah posisi
};
