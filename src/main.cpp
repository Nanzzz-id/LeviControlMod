
/**
 * LeviControlMod - main.cpp
 *
 * Hook:
 *   1. eglSwapBuffers  → render overlay setiap frame
 *   2. ANativeWindow_setBuffersGeometry / AInputQueue_getEvent → intercept touch
 *
 * Layout default (mirip Zalith):
 *   Kiri bawah  : Joystick WASD
 *   Kanan bawah : Jump, Sneak, Sprint
 *   Kiri tengah : Attack (klik kiri)
 *   Kanan atas  : Inventory
 *   Area kanan  : Look (gerak kamera)
 *
 * Long press tombol/joystick → masuk edit mode, drag untuk pindahkan
 * Double tap tombol EDIT BAR → keluar edit mode
 */

#include <jni.h>
#include <android/log.h>
#include <android/input.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <ctime>
#include <string>
#include <vector>
#include <mutex>
#include <chrono>

#include "Gloss.h"
#include "input.h"
#include "renderer.h"

#define TAG "LeviCtrl"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// ─────────────────────────────────────────────
//  STATE GLOBAL
// ─────────────────────────────────────────────
static std::vector<VButton> g_btns;
static Joystick             g_js;
static VirtualMouse         g_mouse;
static EditMode             g_mode = EditMode::PLAY;
static std::mutex           g_mtx;
static bool                 g_initialized = false;

// ─────────────────────────────────────────────
//  HELPER: waktu millisecond
// ─────────────────────────────────────────────
static uint64_t nowMs() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(
        steady_clock::now().time_since_epoch()).count();
}

// ─────────────────────────────────────────────
//  INJECT KEY KE MINECRAFT
//  Pakai /proc/self/fd atau JNI Instrumentation
//  Untuk Bedrock: simulasi gamepad via Android input injection
// ─────────────────────────────────────────────

// Fungsi-fungsi ini memanggil input Minecraft lewat
// manipulasi memori / symbol hook libminecraftpe.so
// Untuk saat ini kita pakai pendekatan touch injection
// yang lebih portable (tidak butuh offset per versi)

// Kirim touch event buatan ke Minecraft
typedef int32_t (*AInputQueue_getEvent_t)(AInputQueue*, AInputEvent**);
typedef void    (*AInputQueue_finishEvent_t)(AInputQueue*, AInputEvent*, int);
static AInputQueue* g_input_queue = nullptr;

// Simulasi keyboard key press via /dev/input tidak tersedia tanpa root
// Solusi: hook fungsi Minecraft langsung (butuh offset)
// Untuk demo ini kita gunakan touch simulation yang aman

// Posisi touch simulasi untuk aksi:
//   Attack = tap di pusat crosshair (tengah layar kiri)
//   Jump   = swipe up singkat
// Ini akan dikembangkan dengan hook MC symbols di versi lanjut

static void sendMCAction(BtnAction action, bool down,
                         int sw, int sh) {
    // TODO: Implementasi via hook libminecraftpe.so
    // Untuk setiap aksi, kita perlu offset fungsi yang tepat
    // per versi Minecraft. Contoh:
    //
    // BTN_JUMP:
    //   Hook: void Player::jump() di libminecraftpe.so
    //   Panggil g_player_ptr->jump() saat down=true
    //
    // BTN_ATTACK:
    //   Simulasi touch di posisi crosshair
    //   atau hook GameMode::attack()
    //
    // Komunitas LeviLauncher/MCPE modding punya database offset:
    //   https://github.com/levimc/offset-database (cek repo resmi)

    switch (action) {
        case BtnAction::JUMP:
            LOGI("Action: JUMP %s", down?"DOWN":"UP");
            break;
        case BtnAction::SNEAK:
            LOGI("Action: SNEAK %s", down?"DOWN":"UP");
            break;
        case BtnAction::ATTACK:
            LOGI("Action: ATTACK %s", down?"DOWN":"UP");
            break;
        case BtnAction::SPRINT:
            LOGI("Action: SPRINT %s", down?"DOWN":"UP");
            break;
        case BtnAction::INVENTORY:
            LOGI("Action: INVENTORY %s", down?"DOWN":"UP");
            break;
        case BtnAction::DROP:
            LOGI("Action: DROP %s", down?"DOWN":"UP");
            break;
        default: break;
    }
}

// ─────────────────────────────────────────────
//  SETUP LAYOUT DEFAULT
// ─────────────────────────────────────────────
static void setupLayout(int sw, int sh) {
    g_btns.clear();

    float s  = (float)sh;
    float dpi = s / 800.0f;  // skala relatif terhadap 800px tinggi

    float btnSz  = 75.0f  * dpi;  // ukuran tombol biasa
    float bigSz  = 88.0f  * dpi;  // tombol attack lebih besar
    float margin = 20.0f  * dpi;

    // ── Joystick WASD (kiri bawah) ──────────────────
    g_js.cx          = margin + 120.0f * dpi;
    g_js.cy          = sh - margin - 120.0f * dpi;
    g_js.base_radius = 100.0f * dpi;
    g_js.knob_radius = 42.0f  * dpi;
    g_js.knob_x      = g_js.cx;
    g_js.knob_y      = g_js.cy;
    g_js.active      = false;
    g_js.touch_id    = -1;

    // ── Attack (kiri tengah) ─────────────────────────
    g_btns.push_back({
        "ATK", BtnAction::ATTACK,
        margin + bigSz/2, sh/2.0f, bigSz,
        {0.85f,0.2f,0.2f,0.82f},   // idle: merah
        {1.0f, 0.4f,0.4f,0.95f}    // pressed: merah terang
    });

    // ── Jump (kanan bawah) ───────────────────────────
    g_btns.push_back({
        "JUMP", BtnAction::JUMP,
        (float)sw - margin - btnSz*1.8f, sh - margin - btnSz*1.5f, btnSz,
        {0.2f,0.6f,0.95f,0.82f},
        {0.5f,0.8f,1.0f, 0.95f}
    });

    // ── Sneak (kanan bawah, di bawah jump) ──────────
    g_btns.push_back({
        "SNK", BtnAction::SNEAK,
        (float)sw - margin - btnSz*0.6f, sh - margin - btnSz*0.5f, btnSz*0.85f,
        {0.6f,0.4f,0.9f,0.82f},
        {0.8f,0.6f,1.0f,0.95f}
    });

    // ── Sprint (di atas sneak) ───────────────────────
    g_btns.push_back({
        "SPR", BtnAction::SPRINT,
        (float)sw - margin - btnSz*3.0f, sh - margin - btnSz*0.5f, btnSz*0.85f,
        {0.9f,0.6f,0.1f,0.82f},
        {1.0f,0.8f,0.3f,0.95f}
    });

    // ── Inventory (kanan atas) ───────────────────────
    g_btns.push_back({
        "INV", BtnAction::INVENTORY,
        (float)sw - margin - btnSz/2, margin + btnSz/2, btnSz*0.9f,
        {0.3f,0.7f,0.3f,0.82f},
        {0.5f,0.9f,0.5f,0.95f}
    });

    // ── Drop (di sebelah inventory) ─────────────────
    g_btns.push_back({
        "DROP", BtnAction::DROP,
        (float)sw - margin - btnSz*1.8f, margin + btnSz/2, btnSz*0.8f,
        {0.7f,0.5f,0.2f,0.82f},
        {1.0f,0.7f,0.3f,0.95f}
    });

    // ── Virtual Mouse ────────────────────────────────
    g_mouse.cx           = sw * 0.7f;
    g_mouse.cy           = sh * 0.45f;
    g_mouse.sensitivity  = 0.85f;
    g_mouse.look_start_x = sw * 0.42f;  // area look mulai 42% dari kiri
    g_mouse.touch_id     = -1;
    g_mouse.active       = false;

    LOGI("Layout setup: %dx%d, dpi=%.2f", sw, sh, dpi);
}

// ─────────────────────────────────────────────
//  HIT TEST: apakah titik (tx,ty) mengenai tombol
// ─────────────────────────────────────────────
static bool hitButton(const VButton& b, float tx, float ty) {
    float dx=tx-b.x, dy=ty-b.y;
    float r=b.size/2.0f;
    return dx*dx+dy*dy <= r*r;
}

static bool hitJoystick(const Joystick& js, float tx, float ty) {
    float dx=tx-js.cx, dy=ty-js.cy;
    float r=js.base_radius;
    return dx*dx+dy*dy <= r*r;
}

// ─────────────────────────────────────────────
//  PROSES TOUCH EVENT
// ─────────────────────────────────────────────
static void processTouchDown(int id, float x, float y) {
    std::lock_guard<std::mutex> lk(g_mtx);

    // ── Edit mode: drag tombol ───────────────────────
    if (g_mode == EditMode::EDIT) {
        // Cek tombol
        for (auto& b : g_btns) {
            if (hitButton(b, x, y)) {
                b.dragging = true;
                b.touch_id = id;
                b.drag_ox  = x - b.x;
                b.drag_oy  = y - b.y;
                return;
            }
        }
        // Cek joystick
        if (hitJoystick(g_js, x, y)) {
            g_js.dragging   = true;
            g_js.touch_id   = id;
            g_js.drag_ox    = x - g_js.cx;
            g_js.drag_oy    = y - g_js.cy;
        }
        return;
    }

    // ── Play mode ────────────────────────────────────

    // Cek tombol-tombol
    for (auto& b : g_btns) {
        if (b.touch_id == -1 && hitButton(b, x, y)) {
            b.pressed     = true;
            b.touch_id    = id;
            b.press_time  = nowMs();
            sendMCAction(b.action, true,
                         g_renderer.sw(), g_renderer.sh());
            return;
        }
    }

    // Cek joystick
    if (g_js.touch_id == -1 && hitJoystick(g_js, x, y)) {
        g_js.active     = true;
        g_js.touch_id   = id;
        g_js.press_time = nowMs();
        g_js.knob_x     = x;
        g_js.knob_y     = y;
        return;
    }

    // Area look (kanan layar)
    if (x >= g_mouse.look_start_x && g_mouse.touch_id == -1) {
        g_mouse.active   = true;
        g_mouse.touch_id = id;
        g_mouse.last_x   = x;
        g_mouse.last_y   = y;
    }
}

static void processTouchMove(int id, float x, float y) {
    std::lock_guard<std::mutex> lk(g_mtx);

    if (g_mode == EditMode::EDIT) {
        // Drag tombol
        for (auto& b : g_btns) {
            if (b.dragging && b.touch_id == id) {
                b.x = x - b.drag_ox;
                b.y = y - b.drag_oy;
                // Clamp dalam layar
                float r=b.size/2.0f;
                b.x = fmaxf(r, fminf(g_renderer.sw()-r, b.x));
                b.y = fmaxf(r, fminf(g_renderer.sh()-r, b.y));
                return;
            }
        }
        // Drag joystick
        if (g_js.dragging && g_js.touch_id == id) {
            g_js.cx = x - g_js.drag_ox;
            g_js.cy = y - g_js.drag_oy;
            float r=g_js.base_radius;
            g_js.cx = fmaxf(r, fminf(g_renderer.sw()-r, g_js.cx));
            g_js.cy = fmaxf(r, fminf(g_renderer.sh()-r, g_js.cy));
        }
        return;
    }

    // ── Joystick movement ────────────────────────────
    if (g_js.active && g_js.touch_id == id) {
        float dx = x - g_js.cx;
        float dy = y - g_js.cy;
        float dist = sqrtf(dx*dx+dy*dy);
        float maxR = g_js.base_radius - g_js.knob_radius;
        if (dist > maxR) { dx=dx/dist*maxR; dy=dy/dist*maxR; }
        g_js.knob_x = g_js.cx + dx;
        g_js.knob_y = g_js.cy + dy;
        g_js.dx = dx / maxR;  // -1 to 1
        g_js.dy = dy / maxR;
        // State WASD berdasarkan sudut
        float angle = atan2f(dy, dx);  // radian
        float deg   = angle * 180.0f / M_PI;  // -180 to 180
        float dead  = 0.25f;  // dead zone
        g_js.W = (g_js.dy < -dead);
        g_js.S = (g_js.dy >  dead);
        g_js.A = (g_js.dx < -dead);
        g_js.D = (g_js.dx >  dead);
        return;
    }

    // ── Mouse look ───────────────────────────────────
    if (g_mouse.active && g_mouse.touch_id == id) {
        float ddx = (x - g_mouse.last_x) * g_mouse.sensitivity;
        float ddy = (y - g_mouse.last_y) * g_mouse.sensitivity;
        g_mouse.cx += ddx;
        g_mouse.cy += ddy;
        // Clamp cursor
        g_mouse.cx = fmaxf(0, fminf(g_renderer.sw(),  g_mouse.cx));
        g_mouse.cy = fmaxf(0, fminf(g_renderer.sh(), g_mouse.cy));
        g_mouse.last_x = x;
        g_mouse.last_y = y;
        // TODO: kirim delta ke MC (hook setRotation/Player::turn)
    }
}

static void processTouchUp(int id, float x, float y) {
    std::lock_guard<std::mutex> lk(g_mtx);

    if (g_mode == EditMode::EDIT) {
        for (auto& b : g_btns) {
            if (b.touch_id == id) {
                b.dragging = false;
                b.touch_id = -1;
                // Long press di edit bar (y < 44) = keluar edit mode
                if (y < 44) g_mode = EditMode::PLAY;
                return;
            }
        }
        if (g_js.touch_id == id) {
            g_js.dragging = false;
            g_js.touch_id = -1;
        }
        return;
    }

    // Tombol
    for (auto& b : g_btns) {
        if (b.touch_id == id) {
            b.pressed  = false;
            b.touch_id = -1;
            sendMCAction(b.action, false,
                         g_renderer.sw(), g_renderer.sh());
            // Long press (>600ms) = masuk edit mode
            if (nowMs() - b.press_time > 600)
                g_mode = EditMode::EDIT;
            return;
        }
    }

    // Joystick
    if (g_js.touch_id == id) {
        g_js.active   = false;
        g_js.touch_id = -1;
        g_js.knob_x   = g_js.cx;
        g_js.knob_y   = g_js.cy;
        g_js.dx = g_js.dy = 0;
        g_js.W=g_js.A=g_js.S=g_js.D=false;
        // Long press joystick = edit mode
        if (nowMs() - g_js.press_time > 600)
            g_mode = EditMode::EDIT;
        return;
    }

    // Mouse look
    if (g_mouse.touch_id == id) {
        g_mouse.active   = false;
        g_mouse.touch_id = -1;
    }
}

// ─────────────────────────────────────────────
//  HOOK: AInputEvent (touch intercept)
// ─────────────────────────────────────────────
typedef int32_t (*onInputEvent_t)(AInputEvent*);
static onInputEvent_t g_orig_onInput = nullptr;

// Hook via ANativeActivity_onCreate wrapper
// Kita intercept di level ALooper / polling
typedef int (*ALooper_pollAll_t)(int,int*,int*,void**);
static ALooper_pollAll_t g_orig_pollAll = nullptr;

static int hook_ALooper_pollAll(int timeoutMillis, int* outFd,
                                 int* outEvents, void** outData) {
    int result = g_orig_pollAll(timeoutMillis, outFd, outEvents, outData);

    // Intercept input events
    if (result == ALOOPER_POLL_CALLBACK && outData && *outData) {
        AInputQueue* queue = (AInputQueue*)*outData;
        AInputEvent* event = nullptr;
        while (AInputQueue_getEvent(queue, &event) >= 0) {
            if (AInputQueue_preDispatchEvent(queue, event)) continue;

            bool consumed = false;
            if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
                int action  = AMotionEvent_getAction(event);
                int actMask = action & AMOTION_EVENT_ACTION_MASK;
                int pIdx    = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                              >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;

                int   id = AMotionEvent_getPointerId(event, pIdx);
                float x  = AMotionEvent_getX(event, pIdx);
                float y  = AMotionEvent_getY(event, pIdx);

                switch (actMask) {
                    case AMOTION_EVENT_ACTION_DOWN:
                    case AMOTION_EVENT_ACTION_POINTER_DOWN:
                        processTouchDown(id, x, y);
                        // Kalau mengenai kontrol kita, jangan teruskan
                        consumed = true;
                        break;
                    case AMOTION_EVENT_ACTION_MOVE:
                        for (int i=0; i<(int)AMotionEvent_getPointerCount(event); i++) {
                            processTouchMove(
                                AMotionEvent_getPointerId(event,i),
                                AMotionEvent_getX(event,i),
                                AMotionEvent_getY(event,i));
                        }
                        consumed = true;
                        break;
                    case AMOTION_EVENT_ACTION_UP:
                    case AMOTION_EVENT_ACTION_POINTER_UP:
                    case AMOTION_EVENT_ACTION_CANCEL:
                        processTouchUp(id, x, y);
                        consumed = true;
                        break;
                }
            }
            AInputQueue_finishEvent(queue, event, consumed ? 1 : 0);
        }
    }
    return result;
}

// ─────────────────────────────────────────────
//  RENDER OVERLAY
// ─────────────────────────────────────────────
static void renderOverlay() {
    std::lock_guard<std::mutex> lk(g_mtx);
    bool edit = (g_mode == EditMode::EDIT);

    // Area look hint
    g_renderer.drawLookArea(g_mouse.look_start_x);

    // Joystick
    g_renderer.drawJoystick(g_js, edit);

    // Tombol-tombol
    for (const auto& b : g_btns)
        g_renderer.drawButton(b, edit);

    // Cursor mouse
    g_renderer.drawCursor(g_mouse);

    // Edit bar (jika dalam edit mode)
    if (edit) g_renderer.drawEditBar();
}

// ─────────────────────────────────────────────
//  HOOK: eglSwapBuffers
// ─────────────────────────────────────────────
typedef EGLBoolean (*eglSwapBuffers_t)(EGLDisplay, EGLSurface);
static eglSwapBuffers_t g_orig_eglSwap = nullptr;

static EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surf) {
    EGLint w=0, h=0;
    eglQuerySurface(dpy, surf, EGL_WIDTH,  &w);
    eglQuerySurface(dpy, surf, EGL_HEIGHT, &h);

    if (w > 0 && h > 0) {
        // Init / resize renderer
        if (!g_initialized) {
            g_renderer.init(w, h);
            setupLayout(w, h);
            g_initialized = true;
        } else {
            g_renderer.resize(w, h);
        }

        // Simpan state GL
        GLint prevProg;
        GLboolean blend, depth;
        glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
        blend = glIsEnabled(GL_BLEND);
        depth = glIsEnabled(GL_DEPTH_TEST);

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        renderOverlay();

        // Restore state
        if (!blend) glDisable(GL_BLEND);
        if (depth)  glEnable(GL_DEPTH_TEST);
        glUseProgram(prevProg);
    }

    return g_orig_eglSwap(dpy, surf);
}

// ─────────────────────────────────────────────
//  ENTRY POINT MOD
// ─────────────────────────────────────────────
extern "C" __attribute__((visibility("default")))
void LeviMod_Load(void* reserved) {
    LOGI("LeviControlMod loading...");

    // ── Hook eglSwapBuffers ──────────────────────────
    void* libEGL = GlossOpen("libEGL.so");
    if (!libEGL) { LOGE("libEGL.so not found"); return; }

    void* sym = GlossSymbol(libEGL, "eglSwapBuffers");
    if (!sym)  { LOGE("eglSwapBuffers not found"); return; }

    if (GlossHook(sym, (void*)hook_eglSwapBuffers,
                  (void**)&g_orig_eglSwap) != 0) {
        LOGE("Hook eglSwapBuffers failed"); return;
    }
    LOGI("eglSwapBuffers hooked");

    // ── Hook ALooper_pollAll untuk intercept touch ───
    void* libc = GlossOpen("libandroid.so");
    if (libc) {
        void* pollSym = GlossSymbol(libc, "ALooper_pollAll");
        if (pollSym) {
            GlossHook(pollSym, (void*)hook_ALooper_pollAll,
                      (void**)&g_orig_pollAll);
            LOGI("ALooper_pollAll hooked");
        }
    }

    LOGI("LeviControlMod loaded! Long press tombol/joystick = edit mode");
}
