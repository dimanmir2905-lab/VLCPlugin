#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <cstring>
#include <cmath>
#include <string>
#include "MinHook.h"
#include "patches/splash_d3d.hpp"
#include "utils/window.hpp"
#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")

// === ИНКЛУДЫ IMGUI (ТОЛЬКО ДЛЯ ЧАТА) ===
#include <imgui.h>
#include <imgui_impl_dx9.h>
#include <imgui_impl_win32.h>
#include "../utils/samp_chat.hpp"

namespace Patches::SplashD3D {
    namespace {
        using PresentFn = HRESULT(WINAPI*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
        using ResetFn = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);

        struct VertexColor { float x, y, z, rhw; D3DCOLOR color; };
        struct VertexTex { float x, y, z, rhw; D3DCOLOR color; float u, v; };
        struct GradientVertex { float x, y, z, rhw; D3DCOLOR color; };
        struct GlyphInfo { wchar_t ch; float u1, v1, u2, v2; float w, h; bool valid; };

        class AtlasFont {
        public:
            AtlasFont() : m_device(nullptr), m_texture(nullptr), m_ready(false), m_lineHeight(18.0f), m_glyphCount(0) {
                for (int i = 0; i < 256; ++i) m_glyphs[i].valid = false;
            }
            ~AtlasFont() { Release(); }
            void Release() {
                if (m_texture) { m_texture->Release(); m_texture = nullptr; }
                m_device = nullptr; m_ready = false; m_lineHeight = 18.0f; m_glyphCount = 0;
            }
            bool IsReady() const { return m_ready && m_texture != nullptr; }

            HRESULT Initialize(IDirect3DDevice9* device, const wchar_t* faceName, int fontPx, bool bold) {
                if (IsReady()) return S_OK;
                if (!device || !faceName || !*faceName) return E_INVALIDARG;
                m_device = device;

                static const wchar_t kCharset[] = L" 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~"
                    L"\x0410\x0411\x0412\x0413\x0414\x0415\x0401\x0416\x0417\x0418\x0419\x041A\x041B\x041C\x041D\x041E\x041F\x0420\x0421\x0422\x0423\x0424\x0425\x0426\x0427\x0428\x0429\x042A\x042B\x042C\x042D\x042E\x042F"
                    L"\x0430\x0431\x0432\x0433\x0434\x0435\x0451\x0436\x0437\x0438\x0439\x043A\x043B\x043C\x043D\x043E\x043F\x0440\x0441\x0442\x0443\x0444\x0445\x0446\x0447\x0448\x0449\x044A\x044B\x044C\x044D\x044E\x044F";

                constexpr int TEX_W = 1024, TEX_H = 1024, CELL_PAD = 2;
                HDC screenDC = GetDC(nullptr);
                if (!screenDC) return E_FAIL;
                HDC memDC = CreateCompatibleDC(screenDC);
                if (!memDC) { ReleaseDC(nullptr, screenDC); return E_FAIL; }

                BITMAPINFO bmi{};
                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = TEX_W; bmi.bmiHeader.biHeight = -TEX_H;
                bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32; bmi.bmiHeader.biCompression = BI_RGB;

                void* dibBits = nullptr;
                HBITMAP dib = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &dibBits, nullptr, 0);
                if (!dib || !dibBits) { if (dib) DeleteObject(dib); DeleteDC(memDC); ReleaseDC(nullptr, screenDC); return E_FAIL; }

                HGDIOBJ oldBmp = SelectObject(memDC, dib);
                HFONT font = CreateFontW(-fontPx, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FF_DONTCARE, faceName);
                if (!font) { SelectObject(memDC, oldBmp); DeleteObject(dib); DeleteDC(memDC); ReleaseDC(nullptr, screenDC); return E_FAIL; }
                HGDIOBJ oldFont = SelectObject(memDC, font);

                RECT rcFull{ 0, 0, TEX_W, TEX_H };
                HBRUSH blackBrush = CreateSolidBrush(RGB(0, 0, 0));
                FillRect(memDC, &rcFull, blackBrush); DeleteObject(blackBrush);
                SetBkMode(memDC, OPAQUE); SetBkColor(memDC, RGB(0, 0, 0)); SetTextColor(memDC, RGB(255, 255, 255));

                TEXTMETRICW tm{}; GetTextMetricsW(memDC, &tm);
                m_lineHeight = static_cast<float>(tm.tmHeight);
                int penX = CELL_PAD, penY = CELL_PAD, rowH = tm.tmHeight + CELL_PAD * 2;
                m_glyphCount = 0;

                const int charsetLen = static_cast<int>(wcslen(kCharset));
                for (int i = 0; i < charsetLen; ++i) {
                    wchar_t ch = kCharset[i]; if (!ch) continue;
                    SIZE sz{}; wchar_t str[2] = { ch, 0 };
                    if (!GetTextExtentPoint32W(memDC, str, 1, &sz)) continue;
                    int gw = sz.cx, gh = tm.tmHeight;
                    if (gw <= 0) { gw = tm.tmAveCharWidth / 2; if (gw <= 0) gw = 4; }
                    int boxW = gw + CELL_PAD * 2, boxH = gh + CELL_PAD * 2;
                    if (penX + boxW >= TEX_W) { penX = CELL_PAD; penY += rowH; rowH = boxH; }
                    if (penY + boxH >= TEX_H) break;
                    if (boxH > rowH) rowH = boxH;
                    TextOutW(memDC, penX + CELL_PAD, penY + CELL_PAD, str, 1);

                    if (m_glyphCount < 256) {
                        GlyphInfo& g = m_glyphs[m_glyphCount++];
                        g.ch = ch;
                        g.u1 = static_cast<float>(penX + CELL_PAD) / TEX_W; g.v1 = static_cast<float>(penY + CELL_PAD) / TEX_H;
                        g.u2 = static_cast<float>(penX + CELL_PAD + gw) / TEX_W; g.v2 = static_cast<float>(penY + CELL_PAD + gh) / TEX_H;
                        g.w = static_cast<float>(gw); g.h = static_cast<float>(gh); g.valid = true;
                    }
                    penX += boxW;
                }

                HRESULT hr = m_device->CreateTexture(TEX_W, TEX_H, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &m_texture, nullptr);
                if (FAILED(hr) || !m_texture) {
                    SelectObject(memDC, oldFont); DeleteObject(font); SelectObject(memDC, oldBmp); DeleteObject(dib);
                    DeleteDC(memDC); ReleaseDC(nullptr, screenDC); return FAILED(hr) ? hr : E_FAIL;
                }

                D3DLOCKED_RECT lr{};
                hr = m_texture->LockRect(0, &lr, nullptr, 0);
                if (FAILED(hr)) {
                    SelectObject(memDC, oldFont); DeleteObject(font); SelectObject(memDC, oldBmp); DeleteObject(dib);
                    DeleteDC(memDC); ReleaseDC(nullptr, screenDC); return hr;
                }

                const unsigned char* src = static_cast<const unsigned char*>(dibBits);
                for (int y = 0; y < TEX_H; ++y) {
                    unsigned int* dstRow = reinterpret_cast<unsigned int*>(static_cast<unsigned char*>(lr.pBits) + y * lr.Pitch);
                    const unsigned int* srcRow = reinterpret_cast<const unsigned int*>(src + y * TEX_W * 4);
                    for (int x = 0; x < TEX_W; ++x) {
                        unsigned int c = srcRow[x];
                        unsigned int intensity = c & 0xFF;
                        dstRow[x] = (intensity << 24) | 0x00FFFFFF;
                    }
                }
                m_texture->UnlockRect(0);
                SelectObject(memDC, oldFont); DeleteObject(font);
                SelectObject(memDC, oldBmp); DeleteObject(dib); DeleteDC(memDC); ReleaseDC(nullptr, screenDC);
                m_ready = true;
                return S_OK;
            }

            float MeasureText(const wchar_t* text, float scale = 1.0f) const {
                if (!text || !*text) return 0.0f;
                float width = 0.0f;
                for (const wchar_t* p = text; *p; ++p) {
                    const GlyphInfo* g = FindGlyph(*p);
                    width += (g && g->valid) ? (g->w * scale) : (m_lineHeight * 0.4f * scale);
                }
                return width;
            }

            void DrawTextLine(IDirect3DDevice9* device, float x, float y, D3DCOLOR color, const wchar_t* text, float scale = 1.0f) {
                if (!device || !IsReady() || !text || !*text) return;
                device->SetTexture(0, m_texture);
                device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
                float penX = x;
                for (const wchar_t* p = text; *p; ++p) {
                    const GlyphInfo* g = FindGlyph(*p);
                    if (!g || !g->valid) { penX += (m_lineHeight * 0.4f) * scale; continue; }
                    const float gw = g->w * scale, gh = g->h * scale;
                    VertexTex verts[6] = {
                        { penX, y, 0.0f, 1.0f, color, g->u1, g->v1 }, { penX + gw, y, 0.0f, 1.0f, color, g->u2, g->v1 }, { penX, y + gh, 0.0f, 1.0f, color, g->u1, g->v2 },
                        { penX + gw, y, 0.0f, 1.0f, color, g->u2, g->v1 }, { penX + gw, y + gh, 0.0f, 1.0f, color, g->u2, g->v2 }, { penX, y + gh, 0.0f, 1.0f, color, g->u1, g->v2 }
                    };
                    device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, verts, sizeof(VertexTex));
                    penX += gw;
                }
            }

            void DrawTextOutlined(IDirect3DDevice9* device, float x, float y, D3DCOLOR color, D3DCOLOR outlineColor, const wchar_t* text, float scale = 1.0f) {
                if (!device || !text || !*text) return;
                const float o = 1.0f * scale;
                DrawTextLine(device, x - o, y, outlineColor, text, scale);
                DrawTextLine(device, x + o, y, outlineColor, text, scale);
                DrawTextLine(device, x, y - o, outlineColor, text, scale);
                DrawTextLine(device, x, y + o, outlineColor, text, scale);
                DrawTextLine(device, x - o, y - o, outlineColor, text, scale);
                DrawTextLine(device, x + o, y - o, outlineColor, text, scale);
                DrawTextLine(device, x - o, y + o, outlineColor, text, scale);
                DrawTextLine(device, x + o, y + o, outlineColor, text, scale);
                DrawTextLine(device, x, y, color, text, scale);
            }

        private:
            const GlyphInfo* FindGlyph(wchar_t ch) const {
                for (int i = 0; i < m_glyphCount; ++i) if (m_glyphs[i].valid && m_glyphs[i].ch == ch) return &m_glyphs[i];
                return nullptr;
            }
            IDirect3DDevice9* m_device; IDirect3DTexture9* m_texture;
            bool m_ready; float m_lineHeight; GlyphInfo m_glyphs[256]; int m_glyphCount;
        };

        enum class ConnectionFinishMode { None, Success, Error };

        HMODULE g_d3d9 = nullptr;
        FARPROC g_direct3DCreate9 = nullptr;
        PresentFn g_originalPresent = nullptr;
        ResetFn g_originalReset = nullptr;
        DWORD g_startTick = 0;
        float g_barAnim = 0.0f;
        bool g_hideStarted = false;
        DWORD g_hideStartTick = 0;
        float g_overlayAlpha = 1.0f;
        static unsigned int& g_gameState = *reinterpret_cast<unsigned int*>(0xC8D4C0);

        IDirect3DTexture9* g_backgroundTex = nullptr;
        bool g_bgLoadAttempted = false;
        char g_bgPath[MAX_PATH]{};
        AtlasFont g_font;
        bool g_fontInitAttempted = false;

        char g_connectionStatus[256] = "Подключение к серверу...";
        bool g_connectionOverlayActive = false;
        bool g_connectionCompleted = false;
        ConnectionFinishMode g_finishMode = ConnectionFinishMode::None;
        DWORD g_finishTick = 0;

        const wchar_t* kBootTitle = L"\x0417\x0410\x041F\x0423\x0421\x041A \x0418\x0413\x0420\x041E\x0412\x041E\x0419 \x0421\x0415\x0421\x0421\x0418\x0418";
        const wchar_t* kLogoText = L"VIALIENCE RP";
        const wchar_t* kVersionText = L"v1.0.0 | 0.3.7-R3";
        const wchar_t* kQuitHint = L"\x041F\x043E\x0434\x0441\x043A\x0430\x0437\x043A\x0430: /(q)uit \x0434\x043B\x044F \x0432\x044B\x0445\x043E\x0434\x0430 \x0438\x0437 \x0438\x0433\x0440\x044B";

        void BuildModuleDir(char* outDir, size_t outDirSize) {
            outDir[0] = '\0';
            char modulePath[MAX_PATH]{};
            if (GetModuleFileNameA(GetModuleHandleA(nullptr), modulePath, MAX_PATH) == 0) return;
            strncpy_s(outDir, outDirSize, modulePath, _TRUNCATE);
            char* slash = strrchr(outDir, '\\');
            if (slash) *(slash + 1) = '\0';
        }

        void BuildAssetPaths() {
            char dir[MAX_PATH]{};
            BuildModuleDir(dir, sizeof(dir));
            if (dir[0]) { strcpy_s(g_bgPath, dir); strcat_s(g_bgPath, "VialenceLoadScreen.png"); }
            else { strcpy_s(g_bgPath, "VialenceLoadScreen.png"); }
        }

        void Cp1251ToWideLocal(const char* src, wchar_t* dst, size_t dstCount) {
            if (!dst || dstCount == 0 || !src) return;
            dst[0] = 0;
            MultiByteToWideChar(1251, 0, src, -1, dst, static_cast<int>(dstCount));
        }

        LRESULT CALLBACK DummyWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
            return DefWindowProcA(hwnd, msg, wParam, lParam);
        }

        HWND CreateDummyWindow() {
            WNDCLASSEXA wc{};
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = DummyWndProc;
            wc.hInstance = GetModuleHandleA(nullptr);
            wc.lpszClassName = "VialenceDummyWindow";
            RegisterClassExA(&wc);
            HWND hwnd = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, "VialenceDummyWindow", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, wc.hInstance, nullptr);
            if (hwnd) {
                ShowWindow(hwnd, SW_HIDE);
                SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 1, 1, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_HIDEWINDOW | SWP_NOOWNERZORDER | SWP_NOZORDER);
            }
            return hwnd;
        }

        bool UpdateOverlayVisibility() {
            if (g_startTick == 0) g_startTick = GetTickCount();
            const DWORD now = GetTickCount();

            if (g_gameState != 9) {
                g_hideStarted = false;
                g_hideStartTick = 0;
                g_overlayAlpha = 1.0f;
                g_connectionOverlayActive = false;
                g_connectionCompleted = false;
                g_finishMode = ConnectionFinishMode::None;
                g_finishTick = 0;
                return true;
            }

            if (!g_connectionCompleted && !g_connectionOverlayActive && g_finishMode == ConnectionFinishMode::None) {
                g_connectionOverlayActive = true;
                g_hideStarted = false;
                g_hideStartTick = 0;
                g_overlayAlpha = 1.0f;
                if (g_connectionStatus[0] == '\0')
                    strcpy_s(g_connectionStatus, "Подключение к серверу...");
            }

            if (g_connectionOverlayActive) {
                if (g_finishMode == ConnectionFinishMode::None) {
                    g_overlayAlpha = 1.0f;
                    g_hideStarted = false;
                    g_hideStartTick = 0;
                    return true;
                }

                DWORD holdDuration = (g_finishMode == ConnectionFinishMode::Success) ? 1100 : 2200;
                if ((now - g_finishTick) < holdDuration) {
                    g_overlayAlpha = 1.0f;
                    return true;
                }

                if (!g_hideStarted) {
                    g_hideStarted = true;
                    g_hideStartTick = now;
                }

                const DWORD fadeDuration = 700;
                const DWORD fadeElapsed = now - g_hideStartTick;
                if (fadeElapsed >= fadeDuration) {
                    g_overlayAlpha = 0.0f;
                    g_connectionOverlayActive = false;
                    g_connectionCompleted = true;
                    g_finishMode = ConnectionFinishMode::None;
                    return false;
                }

                float t = static_cast<float>(fadeElapsed) / static_cast<float>(fadeDuration);
                t = 1.0f - ((1.0f - t) * (1.0f - t) * (1.0f - t));
                g_overlayAlpha = 1.0f - t;
                return true;
            }

            g_overlayAlpha = 0.0f;
            return false;
        }

        HRESULT LoadTextureFromFile(IDirect3DDevice9* device, const char* path, IDirect3DTexture9** outTex) {
            if (!device || !outTex) return E_INVALIDARG;

            if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
                OutputDebugStringA(("[VLC] Texture file not found: " + std::string(path) + "\n").c_str());
                return E_FAIL;
            }

            HRESULT hr = D3DXCreateTextureFromFileExA(
                device, path,
                D3DX_DEFAULT_NONPOW2, D3DX_DEFAULT_NONPOW2,
                D3DX_DEFAULT, 0,
                D3DFMT_A8R8G8B8,
                D3DPOOL_MANAGED,
                D3DX_DEFAULT, D3DX_DEFAULT, 0,
                nullptr, nullptr,
                outTex
            );

            if (FAILED(hr) || !*outTex) {
                OutputDebugStringA(("[VLC] Failed to load texture: " + std::string(path) + "\n").c_str());
                return hr;
            }

            return S_OK;
        }

        D3DCOLOR ApplyAlpha(D3DCOLOR color, float alphaMul) {
            if (alphaMul <= 0.0f) return D3DCOLOR_ARGB(0, 0, 0, 0);
            if (alphaMul >= 1.0f) return color;
            const unsigned int a = (color >> 24) & 0xFF, r = (color >> 16) & 0xFF, g = (color >> 8) & 0xFF, b = color & 0xFF;
            return D3DCOLOR_ARGB(static_cast<unsigned int>(a * alphaMul), r, g, b);
        }

        D3DCOLOR LerpColor(D3DCOLOR c1, D3DCOLOR c2, float t) {
            if (t <= 0.0f) return c1;
            if (t >= 1.0f) return c2;
            const unsigned int a1 = (c1 >> 24) & 0xFF, r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
            const unsigned int a2 = (c2 >> 24) & 0xFF, r2 = (c2 >> 16) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = c2 & 0xFF;
            return D3DCOLOR_ARGB(
                static_cast<unsigned int>(a1 + (a2 - a1) * t),
                static_cast<unsigned int>(r1 + (r2 - r1) * t),
                static_cast<unsigned int>(g1 + (g2 - g1) * t),
                static_cast<unsigned int>(b1 + (b2 - b1) * t)
            );
        }

        void EnsureBackgroundTexture(IDirect3DDevice9* device) {
            if (g_backgroundTex || g_bgLoadAttempted) return;
            g_bgLoadAttempted = true;
            BuildAssetPaths();
            LoadTextureFromFile(device, g_bgPath, &g_backgroundTex);
        }

        void EnsureAtlasFont(IDirect3DDevice9* device) {
            if (g_font.IsReady() || g_fontInitAttempted) return;
            g_fontInitAttempted = true;
            g_font.Initialize(device, L"Verdana", 18, true);
        }

        void DrawFilledRect(IDirect3DDevice9* device, float x, float y, float w, float h, D3DCOLOR color) {
            VertexColor verts[4] = { {x, y, 0.0f, 1.0f, color}, {x + w, y, 0.0f, 1.0f, color}, {x, y + h, 0.0f, 1.0f, color}, {x + w, y + h, 0.0f, 1.0f, color} };
            device->SetTexture(0, nullptr);
            device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
            device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, verts, sizeof(VertexColor));
        }

        void DrawGradientRect(IDirect3DDevice9* device, float x, float y, float w, float h, D3DCOLOR colorLeft, D3DCOLOR colorRight) {
            GradientVertex verts[4] = {
                { x, y, 0.0f, 1.0f, colorLeft },
                { x + w, y, 0.0f, 1.0f, colorRight },
                { x, y + h, 0.0f, 1.0f, colorLeft },
                { x + w, y + h, 0.0f, 1.0f, colorRight }
            };
            device->SetTexture(0, nullptr);
            device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
            device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, verts, sizeof(GradientVertex));
        }

        void DrawTexturedQuad(IDirect3DDevice9* device, IDirect3DTexture9* tex, float x, float y, float w, float h, D3DCOLOR color) {
            if (!device || !tex) return;
            VertexTex verts[4] = { {x, y, 0.0f, 1.0f, color, 0.0f, 0.0f}, {x + w, y, 0.0f, 1.0f, color, 1.0f, 0.0f}, {x, y + h, 0.0f, 1.0f, color, 0.0f, 1.0f}, {x + w, y + h, 0.0f, 1.0f, color, 1.0f, 1.0f} };
            device->SetTexture(0, tex);
            device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
            device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, verts, sizeof(VertexTex));
        }

        void DrawFullscreenTexture(IDirect3DDevice9* device, IDirect3DTexture9* tex, float screenW, float screenH, D3DCOLOR color) {
            DrawTexturedQuad(device, tex, 0.0f, 0.0f, screenW, screenH, color);
        }

        void PrepareOverlayRenderState(IDirect3DDevice9* device) {
            device->SetPixelShader(nullptr); device->SetVertexShader(nullptr);
            device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
            device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
            device->SetRenderState(D3DRS_LIGHTING, FALSE); device->SetRenderState(D3DRS_ZENABLE, FALSE);
            device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE); device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
            device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE); device->SetRenderState(D3DRS_FOGENABLE, FALSE);
            device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
            device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
            device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
            device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
            device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
            device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
            device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
            device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
            device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
            device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
            device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
            device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
            device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        }

        void DrawAnimatedLine(IDirect3DDevice9* device, float x, float y, float w) {
            DrawFilledRect(device, x, y, w, 2.0f, ApplyAlpha(D3DCOLOR_ARGB(145, 70, 70, 70), g_overlayAlpha));

            static float animPos = 0.0f;
            static float animDir = 1.0f;
            static DWORD lastMove = GetTickCount();

            DWORD now = GetTickCount();
            if (now - lastMove > 50) {
                animPos += animDir * 2.0f;
                if (animPos > w - 100.0f) animDir = -1.0f;
                if (animPos < 0.0f) animDir = 1.0f;
                lastMove = now;
            }

            DrawFilledRect(device, x + animPos - 4.0f, y - 2.0f, 108.0f, 6.0f, ApplyAlpha(D3DCOLOR_ARGB(80, 26, 132, 245), g_overlayAlpha));
            DrawGradientRect(device, x + animPos, y - 1.0f, 100.0f, 4.0f,
                ApplyAlpha(D3DCOLOR_ARGB(255, 26, 132, 245), g_overlayAlpha),
                ApplyAlpha(D3DCOLOR_ARGB(255, 255, 255, 255), g_overlayAlpha));
        }

        void DrawBootPanel(IDirect3DDevice9* device, float screenW, float screenH) {
            if (!g_font.IsReady()) return;

            const DWORD elapsed = GetTickCount() - g_startTick;
            float fadeIn = static_cast<float>(elapsed) / 500.0f;
            if (fadeIn > 1.0f) fadeIn = 1.0f;
            float currentAlpha = g_overlayAlpha * fadeIn;

            const float panelW = 418.0f, panelH = 66.0f;
            const float x = screenW - panelW - 42.0f, y = screenH - panelH - 34.0f;
            const float badgeX = x + panelW - 144.0f, badgeY = y + 9.0f;

            DrawFilledRect(device, badgeX, badgeY, 152.0f, 25.0f, ApplyAlpha(D3DCOLOR_ARGB(225, 26, 132, 245), currentAlpha));

            const float barX = x + 14.0f, barY = y + panelH - 10.0f, barW = panelW - 28.0f;
            DrawAnimatedLine(device, barX, barY, barW);

            const float sessionScale = 0.88f, connectScale = 0.84f;
            const D3DCOLOR whiteText = ApplyAlpha(D3DCOLOR_ARGB(255, 245, 248, 255), currentAlpha);
            const D3DCOLOR outlineText = ApplyAlpha(D3DCOLOR_ARGB(220, 0, 0, 0), currentAlpha);
            g_font.DrawTextOutlined(device, x + 18.0f, y + 14.0f, whiteText, outlineText, kBootTitle, sessionScale);

            const wchar_t* connectWord = L"\x0417\x0410\x0413\x0420\x0423\x0417\x041A\x0410";
            const float wordW = g_font.MeasureText(connectWord, connectScale);
            const float dotW = g_font.MeasureText(L".", connectScale);
            const float totalW = wordW + dotW * 3.0f;
            const float connectTextX = badgeX + (152.0f - totalW) * 0.5f - 1.0f;

            const D3DCOLOR connectMainColor = ApplyAlpha(D3DCOLOR_ARGB(255, 255, 255, 255), currentAlpha);
            const D3DCOLOR connectOutlineColor = ApplyAlpha(D3DCOLOR_ARGB(200, 0, 54, 110), currentAlpha);
            g_font.DrawTextOutlined(device, connectTextX, badgeY + 3.5f, connectMainColor, connectOutlineColor, connectWord, connectScale);

            const DWORD tick = (g_startTick == 0) ? 0 : GetTickCount();
            auto DotAlpha = [&](int index) -> int {
                float local = (tick / 450.0f) - static_cast<float>(index);
                while (local < 0.0f) local += 3.0f;
                while (local >= 3.0f) local -= 3.0f;
                if (local >= 0.0f && local < 1.0f) {
                    float k = local < 0.5f ? local / 0.5f : (1.0f - local) / 0.5f;
                    return 70 + static_cast<int>((255 - 70) * k);
                }
                return 70;
                };

            const float dotsBaseX = connectTextX + wordW;
            for (int i = 0; i < 3; ++i) {
                const int alpha = DotAlpha(i);
                g_font.DrawTextOutlined(device, dotsBaseX + i * dotW, badgeY + 3.5f,
                    ApplyAlpha(D3DCOLOR_ARGB(alpha, 255, 255, 255), currentAlpha),
                    ApplyAlpha(D3DCOLOR_ARGB((alpha * 200) / 255, 0, 54, 110), currentAlpha), L".", connectScale);
            }
        }

        void DrawConnectionOverlay(IDirect3DDevice9* device, float screenW, float screenH) {
            if (!g_font.IsReady()) return;

            const DWORD elapsed = GetTickCount() - g_startTick;
            float fadeIn = static_cast<float>(elapsed) / 500.0f;
            if (fadeIn > 1.0f) fadeIn = 1.0f;
            float currentAlpha = g_overlayAlpha * fadeIn;

            wchar_t statusWide[256]{};
            Cp1251ToWideLocal(g_connectionStatus, statusWide, 256);

            float statusScale = 1.02f;
            if (strlen(g_connectionStatus) > 28) statusScale = 0.94f;
            if (strlen(g_connectionStatus) > 40) statusScale = 0.86f;

            D3DCOLOR statusColor = ApplyAlpha(D3DCOLOR_ARGB(255, 245, 248, 255), currentAlpha);
            const D3DCOLOR outlineColor = ApplyAlpha(D3DCOLOR_ARGB(230, 0, 0, 0), currentAlpha);
            const D3DCOLOR hintColor = ApplyAlpha(D3DCOLOR_ARGB(220, 210, 220, 235), currentAlpha);
            const D3DCOLOR hintOutline = ApplyAlpha(D3DCOLOR_ARGB(170, 0, 0, 0), currentAlpha);

            if (g_finishMode == ConnectionFinishMode::Success)
                statusColor = ApplyAlpha(D3DCOLOR_ARGB(255, 210, 255, 220), currentAlpha);
            else if (g_finishMode == ConnectionFinishMode::Error)
                statusColor = ApplyAlpha(D3DCOLOR_ARGB(255, 255, 220, 220), currentAlpha);

            const float panelW = 418.0f, panelH = 66.0f;
            const float x = screenW - panelW - 42.0f, y = screenH - panelH - 34.0f;

            const float textX = x + 18.0f;
            const float statusY = y + 14.0f;
            g_font.DrawTextOutlined(device, textX, statusY, statusColor, outlineColor, statusWide, statusScale);

            if (g_finishMode != ConnectionFinishMode::Success) {
                g_font.DrawTextOutlined(device, textX, statusY + 20.0f, hintColor, hintOutline, kQuitHint, 0.64f);
            }

            DrawAnimatedLine(device, x + 14.0f, y + panelH - 10.0f, panelW - 28.0f);
        }

        void DrawLoadScreenOverlay(IDirect3DDevice9* device) {
            D3DVIEWPORT9 vp{};
            if (FAILED(device->GetViewport(&vp))) return;
            const float screenW = static_cast<float>(vp.Width), screenH = static_cast<float>(vp.Height);

            EnsureBackgroundTexture(device);
            EnsureAtlasFont(device);

            if (g_backgroundTex) {
                DrawFullscreenTexture(device, g_backgroundTex, screenW, screenH, ApplyAlpha(D3DCOLOR_ARGB(255, 255, 255, 255), g_overlayAlpha));
            }
            else {
                DrawFilledRect(device, 0.0f, 0.0f, screenW, screenH, ApplyAlpha(D3DCOLOR_ARGB(255, 0, 0, 0), g_overlayAlpha));
            }

            if (g_gameState != 9) DrawBootPanel(device, screenW, screenH);
            else if (g_connectionOverlayActive) DrawConnectionOverlay(device, screenW, screenH);
        }

        void DrawOverlaySafe(IDirect3DDevice9* device) {
            if (!device) return;
            const bool overlayVisible = UpdateOverlayVisibility();
            IDirect3DStateBlock9* stateBlock = nullptr;
            if (SUCCEEDED(device->CreateStateBlock(D3DSBT_ALL, &stateBlock)) && stateBlock) stateBlock->Capture();
            PrepareOverlayRenderState(device);
            if (overlayVisible) DrawLoadScreenOverlay(device);
            if (stateBlock) { stateBlock->Apply(); stateBlock->Release(); }
        }

        // ==========================================
        // ПРЯМАЯ ИНИЦИАЛИЗАЦИЯ IMGUI (БЕЗ ImGuiMenu)
        // ==========================================
        static bool g_imguiInitialized = false;

        void InitializeImGui(IDirect3DDevice9* device, HWND hwnd) {
            if (g_imguiInitialized) return;

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

            ImFontConfig font_cfg;
            font_cfg.OversampleH = 1;
            font_cfg.OversampleV = 1;
            font_cfg.PixelSnapH = true;
            io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", 16.0f, &font_cfg, io.Fonts->GetGlyphRangesCyrillic());

            ImGui::StyleColorsDark();

            ImGui_ImplWin32_Init(hwnd);
            ImGui_ImplDX9_Init(device);

            g_imguiInitialized = true;
            OutputDebugStringA("[VLC] ImGui успешно инициализирован для чата\n");
        }

        HRESULT WINAPI HookedPresent(IDirect3DDevice9* device, const RECT* srcRect, const RECT* dstRect, HWND dstWindowOverride, const RGNDATA* dirtyRegion) {
            // 1. Отрисовка сплэш-скрина
            DrawOverlaySafe(device);

            // 2. Инициализация ImGui (только один раз)
            if (!g_imguiInitialized) {
                HWND targetHwnd = Utils::GetGameHwnd();
                if (!targetHwnd) targetHwnd = GetActiveWindow();

                if (targetHwnd && device) {
                    InitializeImGui(device, targetHwnd);
                }
            }

            // 3. Рендер UI элементов (ТОЛЬКО ЧАТ)
            if (g_imguiInitialized && ImGui::GetCurrentContext()) {
                // Начало кадра ImGui
                ImGui_ImplDX9_NewFrame();
                ImGui_ImplWin32_NewFrame();
                ImGui::NewFrame();

                // Обновляем и рендерим кастомный чат
                Utils::SampChat::Update(ImGui::GetIO().DeltaTime);
                Utils::SampChat::Render();

                // Конец кадра ImGui и рендер
                ImGui::Render();
                ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            }

            // 4. Вызов оригинальной функции Present
            return g_originalPresent(device, srcRect, dstRect, dstWindowOverride, dirtyRegion);
        }

        HRESULT WINAPI HookedReset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* pp) {
            // Сбрасываем ресурсы ImGui
            if (g_imguiInitialized) {
                ImGui_ImplDX9_InvalidateDeviceObjects();
            }

            g_font.Release();
            g_fontInitAttempted = false;

            if (g_backgroundTex) {
                g_backgroundTex->Release();
                g_backgroundTex = nullptr;
            }
            g_bgLoadAttempted = false;

            // Вызываем оригинальный Reset
            HRESULT hr = g_originalReset(device, pp);

            // Восстанавливаем ресурсы после успешного Reset
            if (SUCCEEDED(hr) && g_imguiInitialized) {
                ImGui_ImplDX9_CreateDeviceObjects();
            }

            return hr;
        }

        bool InstallPresentHook() {
            uintptr_t gtaBase = reinterpret_cast<uintptr_t>(GetModuleHandleA("gta_sa.exe"));
            if (gtaBase) {
                DWORD oldProtect = 0;
                void* patchAddr = reinterpret_cast<void*>(gtaBase + 0x590480);
                if (VirtualProtect(patchAddr, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                    memset(patchAddr, 0x90, 5);
                    VirtualProtect(patchAddr, 5, oldProtect, &oldProtect);
                    FlushInstructionCache(GetCurrentProcess(), patchAddr, 5);
                }
            }

            g_d3d9 = GetModuleHandleA("d3d9.dll");
            if (!g_d3d9) g_d3d9 = LoadLibraryA("d3d9.dll");
            if (!g_d3d9) return false;

            g_direct3DCreate9 = GetProcAddress(g_d3d9, "Direct3DCreate9");
            if (!g_direct3DCreate9) return false;

            HWND hwnd = CreateDummyWindow();
            if (!hwnd) return false;

            auto Direct3DCreate9Fn = reinterpret_cast<IDirect3D9 * (WINAPI*)(UINT)>(g_direct3DCreate9);
            IDirect3D9* d3d = Direct3DCreate9Fn(D3D_SDK_VERSION);
            if (!d3d) { DestroyWindow(hwnd); return false; }

            D3DPRESENT_PARAMETERS pp{};
            pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = hwnd;
            pp.BackBufferWidth = 1; pp.BackBufferHeight = 1; pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

            IDirect3DDevice9* device = nullptr;
            HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device);
            if (FAILED(hr) || !device) { d3d->Release(); DestroyWindow(hwnd); return false; }

            void** vtable = *reinterpret_cast<void***>(device);
            void* resetAddr = vtable[16];
            void* presentAddr = vtable[17];

            if (MH_CreateHook(presentAddr, &HookedPresent, reinterpret_cast<void**>(&g_originalPresent)) != MH_OK ||
                MH_CreateHook(resetAddr, &HookedReset, reinterpret_cast<void**>(&g_originalReset)) != MH_OK ||
                MH_EnableHook(presentAddr) != MH_OK ||
                MH_EnableHook(resetAddr) != MH_OK) {
                MH_DisableHook(MH_ALL_HOOKS);
                device->Release(); d3d->Release(); DestroyWindow(hwnd);
                return false;
            }

            device->Release(); d3d->Release(); DestroyWindow(hwnd);
            return true;
        }
    }

    void SetConnectionStatus(const char* text) {
        if (!text || !*text) return;
        if (g_finishMode != ConnectionFinishMode::None || g_connectionCompleted) return;
        strncpy_s(g_connectionStatus, text, _TRUNCATE);
        g_connectionOverlayActive = true; g_finishTick = 0; g_hideStarted = false; g_hideStartTick = 0; g_overlayAlpha = 1.0f;
    }

    void SetConnectionSuccess(const char* text) {
        if (text && *text) strncpy_s(g_connectionStatus, text, _TRUNCATE);
        g_connectionOverlayActive = true; g_connectionCompleted = false; g_finishMode = ConnectionFinishMode::Success;
        g_finishTick = GetTickCount(); g_hideStarted = false; g_hideStartTick = 0; g_overlayAlpha = 1.0f;
    }

    void SetConnectionError(const char* text) {
        if (text && *text) strncpy_s(g_connectionStatus, text, _TRUNCATE);
        g_connectionOverlayActive = true; g_connectionCompleted = false; g_finishMode = ConnectionFinishMode::Error;
        g_finishTick = GetTickCount(); g_hideStarted = false; g_hideStartTick = 0; g_overlayAlpha = 1.0f;
    }

    void Initialize() {
        MH_Initialize();
        InstallPresentHook();
    }

    void Shutdown() {
        // Корректное завершение работы ImGui
        if (g_imguiInitialized) {
            ImGui_ImplDX9_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            g_imguiInitialized = false;
        }

        g_font.Release();
        if (g_backgroundTex) {
            g_backgroundTex->Release();
            g_backgroundTex = nullptr;
        }

        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
    }
}