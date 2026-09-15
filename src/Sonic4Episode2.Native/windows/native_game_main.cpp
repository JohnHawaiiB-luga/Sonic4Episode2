#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <shellapi.h>

#include "d3d9_device.h"
#include "native_player_scene.h"
#include "native_sonic_audio.h"
#include "native_stage_renderer.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr wchar_t kWindowClassName[] = L"Sonic4Episode2NativeHostWindow";
constexpr wchar_t kWindowTitle[] =
    L"SONIC THE HEDGEHOG 4 Episode II - Decomp WIP (Partial stage inspection)";
constexpr wchar_t kStatusCaption[] =
    L"WASD/arrows: camera | +/-/wheel: zoom | Home: reset | Esc: close | Terrain/gameplay WIP";
constexpr wchar_t kPlayerWindowTitle[] =
    L"SONIC THE HEDGEHOG 4 Episode II - Decomp WIP (Sonic movement)";
constexpr wchar_t kPlayerStatusCaption[] =
    L"A/D or Left/Right: move | J/K: jump | Sonic movement WIP | Esc: close";
constexpr std::uint32_t kInitialWidth = 1280u;
constexpr std::uint32_t kInitialHeight = 720u;
constexpr std::uint32_t kSelfTestWidth = 960u;
constexpr std::uint32_t kSelfTestHeight = 540u;
constexpr int kStatusCaptionHeight = 24;
constexpr float kMinimumDistanceFactor = 0.25f;
constexpr float kMaximumDistanceFactor = 4.0f;

struct Options final {
    std::optional<std::filesystem::path> data_root_argument;
    std::optional<std::filesystem::path> report_path;
    std::optional<float> character_frame;
    std::uint32_t smoke_frames = 0u;
    bool character_inspection = false;
    bool player_scene = false;
    bool smoke_player_walk = false;
    bool smoke_player_dash = false;
    bool hidden = false;
    bool selftest_window = false;
};

struct ReadbackSummary final {
    bool attempted = false;
    bool available = false;
    std::uint64_t dominant_pixels = 0u;
    std::uint64_t nonuniform_pixels = 0u;
    std::uint64_t distinct_pixels = 0u;
    std::uint32_t dominant_rgba = 0u;
    std::string error;
};

struct SelfTestSummary final {
    bool requested = false;
    bool completed = false;
    bool focus_loss_handler_verified = false;
    bool small_resize_verified = false;
    bool restore_resize_verified = false;
    std::uint32_t resize_message_baseline = 0u;
    std::uint32_t focus_gain_baseline = 0u;
    std::uint32_t focus_loss_baseline = 0u;
    std::uint32_t device_resize_baseline = 0u;
    std::uint32_t resize_messages = 0u;
    std::uint32_t focus_gain_messages = 0u;
    std::uint32_t focus_loss_messages = 0u;
    std::uint32_t device_resizes = 0u;
};

struct RunTelemetry final {
    std::uint64_t frames = 0u;
    std::uint32_t width = kInitialWidth;
    std::uint32_t height = kInitialHeight;
    std::uint32_t device_resize_count = 0u;
    ReadbackSummary readback;
    SelfTestSummary selftest;
    std::string player_json;
    std::string audio_json;
    bool player_scene = false;
};

struct RunIdentity final {
    std::filesystem::path executable_path;
    std::string executable_name;
};

struct WindowState final {
    std::array<bool, 256u> pressed{};
    HWND status_caption = nullptr;
    bool close_requested = false;
    bool minimized = false;
    bool focused = false;
    bool resize_pending = false;
    bool reset_required = false;
    std::uint32_t resize_message_count = 0u;
    std::uint32_t focus_gain_count = 0u;
    std::uint32_t focus_loss_count = 0u;
    std::uint32_t pending_width = kInitialWidth;
    std::uint32_t pending_height = kInitialHeight;
    float horizontal_offset = 0.0f;
    float vertical_offset = 0.0f;
    float distance_factor = 1.0f;
};

enum class SelfTestStage : std::uint8_t {
    Disabled,
    WaitForSmallResize,
    WaitForInitialResize,
    Complete,
};

void clear_pressed(WindowState& state) noexcept {
    state.pressed.fill(false);
}

bool is_pressed(const WindowState& state, std::uint32_t virtual_key) noexcept {
    return virtual_key < state.pressed.size() && state.pressed[virtual_key];
}

bool has_foreground_input(HWND window, const WindowState& state) noexcept {
    return state.focused && !state.minimized && GetForegroundWindow() == window;
}

void adjust_distance(WindowState& state, float multiplier) noexcept {
    state.distance_factor = std::clamp(
        state.distance_factor * multiplier,
        kMinimumDistanceFactor,
        kMaximumDistanceFactor);
}

void apply_mouse_wheel(WindowState& state, int wheel_delta) noexcept {
    if (wheel_delta == 0) {
        return;
    }

    const float notches = static_cast<float>(wheel_delta) / static_cast<float>(WHEEL_DELTA);
    adjust_distance(state, std::pow(0.9f, notches));
}

void update_navigation(
    WindowState& state,
    HWND window,
    float delta_seconds,
    float pan_scale = 1.0f) noexcept {
    if (!has_foreground_input(window, state)) {
        clear_pressed(state);
        return;
    }

    const int horizontal = static_cast<int>(is_pressed(state, VK_RIGHT) || is_pressed(state, 'D'))
        - static_cast<int>(is_pressed(state, VK_LEFT) || is_pressed(state, 'A'));
    const int vertical = static_cast<int>(is_pressed(state, VK_UP) || is_pressed(state, 'W'))
        - static_cast<int>(is_pressed(state, VK_DOWN) || is_pressed(state, 'S'));
    const int zoom = static_cast<int>(is_pressed(state, VK_ADD) || is_pressed(state, VK_OEM_PLUS))
        - static_cast<int>(is_pressed(state, VK_SUBTRACT) || is_pressed(state, VK_OEM_MINUS));

    constexpr float pan_units_per_second = 180.0f;
    state.horizontal_offset += static_cast<float>(horizontal) * pan_units_per_second * pan_scale * delta_seconds;
    state.vertical_offset += static_cast<float>(vertical) * pan_units_per_second * pan_scale * delta_seconds;
    if (zoom != 0) {
        adjust_distance(state, std::exp(-0.9f * static_cast<float>(zoom) * delta_seconds));
    }
}

void layout_status_caption(WindowState& state, std::uint32_t width, std::uint32_t height) noexcept {
    if (state.status_caption == nullptr) {
        return;
    }

    const int caption_height = std::min(kStatusCaptionHeight, static_cast<int>(height));
    SetWindowPos(
        state.status_caption,
        nullptr,
        0,
        static_cast<int>(height) - caption_height,
        static_cast<int>(width),
        caption_height,
        SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(l_param);
        state = static_cast<WindowState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    if (state == nullptr) {
        return DefWindowProcW(window, message, w_param, l_param);
    }

    switch (message) {
    case WM_SIZE:
        if (w_param == SIZE_MINIMIZED) {
            state->minimized = true;
            state->resize_pending = false;
            return 0;
        }

        state->pending_width = static_cast<std::uint32_t>(LOWORD(l_param));
        state->pending_height = static_cast<std::uint32_t>(HIWORD(l_param));
        state->minimized = state->pending_width == 0u || state->pending_height == 0u;
        state->resize_pending = !state->minimized;
        ++state->resize_message_count;
        layout_status_caption(*state, state->pending_width, state->pending_height);
        return 0;

    case WM_SETFOCUS:
        state->focused = true;
        ++state->focus_gain_count;
        clear_pressed(*state);
        return 0;

    case WM_KILLFOCUS:
        state->focused = false;
        ++state->focus_loss_count;
        clear_pressed(*state);
        return 0;

    case WM_ACTIVATEAPP:
        if (w_param == FALSE) {
            state->focused = false;
            clear_pressed(*state);
        }
        return 0;

    case WM_KEYDOWN:
        if (w_param == VK_ESCAPE) {
            state->close_requested = true;
            return 0;
        }
        if (w_param == VK_HOME) {
            state->horizontal_offset = 0.0f;
            state->vertical_offset = 0.0f;
            state->distance_factor = 1.0f;
            clear_pressed(*state);
            return 0;
        }
        if (state->focused && w_param < state->pressed.size()) {
            state->pressed[w_param] = true;
        }
        return 0;

    case WM_KEYUP:
        if (w_param < state->pressed.size()) {
            state->pressed[w_param] = false;
        }
        return 0;

    case WM_MOUSEWHEEL:
        if (has_foreground_input(window, *state)) {
            const int wheel_delta = static_cast<int>(static_cast<short>(HIWORD(w_param)));
            apply_mouse_wheel(*state, wheel_delta);
        }
        return 0;

    case WM_CLOSE:
        state->close_requested = true;
        DestroyWindow(window);
        return 0;

    case WM_DESTROY:
        state->close_requested = true;
        PostQuitMessage(0);
        return 0;

    case WM_NCDESTROY:
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        break;

    default:
        break;
    }

    return DefWindowProcW(window, message, w_param, l_param);
}

class RegisteredWindowClass final {
public:
    explicit RegisteredWindowClass(HINSTANCE instance)
        : instance_(instance) {
        icon_ = reinterpret_cast<HICON>(LoadImageW(
            instance_,
            MAKEINTRESOURCEW(101),
            IMAGE_ICON,
            0,
            0,
            LR_DEFAULTSIZE));
        owns_icon_ = icon_ != nullptr;
        if (icon_ == nullptr) {
            icon_ = LoadIconW(nullptr, IDI_APPLICATION);
        }

        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        window_class.lpfnWndProc = window_procedure;
        window_class.hInstance = instance_;
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.hIcon = icon_;
        window_class.hIconSm = icon_;
        window_class.lpszClassName = kWindowClassName;

        if (RegisterClassExW(&window_class) != 0u) {
            registered_ = true;
            return;
        }
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            throw std::runtime_error("RegisterClassExW failed.");
        }
    }

    ~RegisteredWindowClass() {
        if (registered_) {
            UnregisterClassW(kWindowClassName, instance_);
        }
        if (owns_icon_ && icon_ != nullptr) {
            DestroyIcon(icon_);
        }
    }

    RegisteredWindowClass(const RegisteredWindowClass&) = delete;
    RegisteredWindowClass& operator=(const RegisteredWindowClass&) = delete;

private:
    HINSTANCE instance_ = nullptr;
    HICON icon_ = nullptr;
    bool owns_icon_ = false;
    bool registered_ = false;
};

class WindowHandle final {
public:
    explicit WindowHandle(HWND handle)
        : handle_(handle) {
    }

    ~WindowHandle() {
        if (handle_ != nullptr && IsWindow(handle_)) {
            DestroyWindow(handle_);
        }
    }

    WindowHandle(const WindowHandle&) = delete;
    WindowHandle& operator=(const WindowHandle&) = delete;

    HWND get() const noexcept {
        return handle_;
    }

private:
    HWND handle_ = nullptr;
};

std::filesystem::path executable_path() {
    std::vector<wchar_t> buffer(260u);
    for (;;) {
        const DWORD copied = GetModuleFileNameW(
            nullptr,
            buffer.data(),
            static_cast<DWORD>(buffer.size()));
        if (copied == 0u) {
            throw std::runtime_error("GetModuleFileNameW failed.");
        }
        if (static_cast<std::size_t>(copied) < buffer.size() - 1u) {
            return std::filesystem::path(std::wstring(buffer.data(), copied));
        }
        if (buffer.size() >= 32768u) {
            throw std::runtime_error("The current executable path is too long.");
        }
        buffer.resize(buffer.size() * 2u);
    }
}

int checked_character_count(std::size_t size, const char* subject) {
    if (size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::overflow_error(std::string(subject) + " is too large.");
    }
    return static_cast<int>(size);
}

std::string wide_to_utf8(std::wstring_view value) {
    if (value.empty()) {
        return {};
    }

    const int source_size = checked_character_count(value.size(), "UTF-16 text");
    const int result_size = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        source_size,
        nullptr,
        0,
        nullptr,
        nullptr);
    if (result_size <= 0) {
        throw std::runtime_error("UTF-16 to UTF-8 conversion failed.");
    }

    std::string result(static_cast<std::size_t>(result_size), '\0');
    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            value.data(),
            source_size,
            result.data(),
            result_size,
            nullptr,
            nullptr)
        != result_size) {
        throw std::runtime_error("UTF-16 to UTF-8 conversion failed.");
    }
    return result;
}

std::wstring utf8_to_wide(std::string_view value) {
    if (value.empty()) {
        return {};
    }

    const int source_size = checked_character_count(value.size(), "UTF-8 text");
    const int result_size = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        source_size,
        nullptr,
        0);
    if (result_size <= 0) {
        return L"Unexpected native host failure.";
    }

    std::wstring result(static_cast<std::size_t>(result_size), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            source_size,
            result.data(),
            result_size)
        != result_size) {
        return L"Unexpected native host failure.";
    }
    return result;
}

std::wstring trim_text(std::wstring value) {
    if (!value.empty() && value.front() == 0xfeff) {
        value.erase(value.begin());
    }

    constexpr wchar_t whitespace[] = L" \t\r\n";
    const std::size_t first = value.find_first_not_of(whitespace);
    if (first == std::wstring::npos) {
        return {};
    }
    const std::size_t last = value.find_last_not_of(whitespace);
    return value.substr(first, last - first + 1u);
}

std::wstring read_utf8_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("Cannot open game-root.txt.");
    }

    const std::string contents{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    if (input.bad()) {
        throw std::runtime_error("Cannot read game-root.txt.");
    }
    return trim_text(utf8_to_wide(contents));
}

std::uint32_t parse_smoke_frames(std::wstring_view value) {
    if (value.empty()) {
        throw std::invalid_argument("--smoke-frames requires an integer from 1 through 10000.");
    }

    std::uint32_t parsed = 0u;
    for (const wchar_t character : value) {
        if (character < L'0' || character > L'9') {
            throw std::invalid_argument("--smoke-frames requires an integer from 1 through 10000.");
        }
        const std::uint32_t digit = static_cast<std::uint32_t>(character - L'0');
        if (parsed > 1000u || (parsed == 1000u && digit > 0u)) {
            throw std::invalid_argument("--smoke-frames must be between 1 and 10000.");
        }
        parsed = parsed * 10u + digit;
    }
    if (parsed == 0u) {
        throw std::invalid_argument("--smoke-frames must be between 1 and 10000.");
    }
    return parsed;
}

float parse_character_frame(std::wstring_view value) {
    if (value.empty()) {
        throw std::invalid_argument("--character-frame requires a finite frame from 0 through 60.");
    }

    std::wstring text(value);
    wchar_t* end = nullptr;
    errno = 0;
    const float parsed = std::wcstof(text.c_str(), &end);
    if (end != text.c_str() + text.size() || errno == ERANGE || !std::isfinite(parsed) ||
        parsed < 0.0f || parsed > 60.0f) {
        throw std::invalid_argument("--character-frame requires a finite frame from 0 through 60.");
    }
    return parsed;
}

Options parse_options(int argument_count, wchar_t* const* arguments) {
    Options options;
    for (int index = 1; index < argument_count; ++index) {
        const std::wstring_view argument(arguments[index]);
        const auto require_value = [&]() -> std::wstring_view {
            if (index + 1 >= argument_count) {
                throw std::invalid_argument("The option requires a value.");
            }
            ++index;
            return arguments[index];
        };

        if (argument == L"--data-root") {
            if (options.data_root_argument.has_value()) {
                throw std::invalid_argument("--data-root may be supplied only once.");
            }
            const std::wstring_view value = require_value();
            if (value.empty()) {
                throw std::invalid_argument("--data-root may not be empty.");
            }
            options.data_root_argument = std::filesystem::path(value);
        } else if (argument == L"--character-inspection") {
            if (options.character_inspection) {
                throw std::invalid_argument("--character-inspection may be supplied only once.");
            }
            options.character_inspection = true;
        } else if (argument == L"--character-frame") {
            if (options.character_frame.has_value()) {
                throw std::invalid_argument("--character-frame may be supplied only once.");
            }
            options.character_frame = parse_character_frame(require_value());
        } else if (argument == L"--player-scene") {
            if (options.player_scene) {
                throw std::invalid_argument("--player-scene may be supplied only once.");
            }
            options.player_scene = true;
        } else if (argument == L"--smoke-frames") {
            if (options.smoke_frames != 0u) {
                throw std::invalid_argument("--smoke-frames may be supplied only once.");
            }
            options.smoke_frames = parse_smoke_frames(require_value());
        } else if (argument == L"--smoke-player-dash") {
            if (options.smoke_player_dash) throw std::invalid_argument("--smoke-player-dash may be supplied only once.");
            options.smoke_player_dash = true;
        } else if (argument == L"--smoke-player-walk") {
            if (options.smoke_player_walk) {
                throw std::invalid_argument("--smoke-player-walk may be supplied only once.");
            }
            options.smoke_player_walk = true;
        } else if (argument == L"--report") {
            if (options.report_path.has_value()) {
                throw std::invalid_argument("--report may be supplied only once.");
            }
            const std::wstring_view value = require_value();
            if (value.empty()) {
                throw std::invalid_argument("--report may not be empty.");
            }
            options.report_path = std::filesystem::path(value);
        } else if (argument == L"--hidden") {
            if (options.hidden) {
                throw std::invalid_argument("--hidden may be supplied only once.");
            }
            options.hidden = true;
        } else if (argument == L"--selftest-window") {
            if (options.selftest_window) {
                throw std::invalid_argument("--selftest-window may be supplied only once.");
            }
            options.selftest_window = true;
        } else {
            throw std::invalid_argument("Unknown native host option.");
        }
    }

    if (options.hidden && options.smoke_frames == 0u) {
        throw std::invalid_argument("--hidden requires --smoke-frames.");
    }
    if ((options.smoke_player_walk || options.smoke_player_dash) && (!options.hidden || !options.player_scene || options.smoke_frames == 0u)) {
        throw std::invalid_argument("Player smoke input requires --hidden, --player-scene and --smoke-frames.");
    }
    if (options.character_frame.has_value() && !options.character_inspection) {
        throw std::invalid_argument("--character-frame requires --character-inspection.");
    }
    if (options.player_scene && options.character_inspection) {
        throw std::invalid_argument("--player-scene and --character-inspection are separate modes.");
    }
    return options;
}

Options parse_command_line() {
    int argument_count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    if (arguments == nullptr) {
        throw std::runtime_error("CommandLineToArgvW failed.");
    }

    try {
        Options options = parse_options(argument_count, arguments);
        LocalFree(arguments);
        return options;
    } catch (...) {
        LocalFree(arguments);
        throw;
    }
}

bool command_line_requests_hidden() noexcept {
    int argument_count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    if (arguments == nullptr) {
        return false;
    }

    bool hidden_requested = false;
    for (int index = 1; index < argument_count; ++index) {
        if (lstrcmpW(arguments[index], L"--hidden") == 0) {
            hidden_requested = true;
            break;
        }
    }
    LocalFree(arguments);
    return hidden_requested;
}

std::filesystem::path resolve_data_root(const Options& options, const RunIdentity& identity) {
    std::filesystem::path result;
    if (options.data_root_argument.has_value()) {
        result = *options.data_root_argument;
    } else {
        const std::filesystem::path pointer_path = identity.executable_path.parent_path() / L"game-root.txt";
        const std::wstring configured_root = read_utf8_text(pointer_path);
        if (configured_root.empty()) {
            throw std::runtime_error("game-root.txt does not contain an asset root.");
        }
        result = std::filesystem::path(configured_root);
        if (result.is_relative()) {
            result = pointer_path.parent_path() / result;
        }
    }

    std::error_code error;
    if (!std::filesystem::is_directory(result, error)) {
        throw std::runtime_error("The native stage data root is not an accessible directory.");
    }
    return result.lexically_normal();
}

HWND create_window(HINSTANCE instance, WindowState& state) {
    RECT rectangle{};
    rectangle.right = static_cast<LONG>(kInitialWidth);
    rectangle.bottom = static_cast<LONG>(kInitialHeight);
    if (AdjustWindowRectEx(&rectangle, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, FALSE, 0u) == FALSE) {
        throw std::runtime_error("AdjustWindowRectEx failed.");
    }

    const HWND window = CreateWindowExW(
        WS_EX_APPWINDOW,
        kWindowClassName,
        kWindowTitle,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        static_cast<int>(rectangle.right - rectangle.left),
        static_cast<int>(rectangle.bottom - rectangle.top),
        nullptr,
        nullptr,
        instance,
        &state);
    if (window == nullptr) {
        throw std::runtime_error("CreateWindowExW failed.");
    }
    state.status_caption = CreateWindowExW(
        0u,
        L"STATIC",
        kStatusCaption,
        WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE | SS_NOPREFIX,
        0,
        0,
        static_cast<int>(kInitialWidth),
        kStatusCaptionHeight,
        window,
        nullptr,
        instance,
        nullptr);
    if (state.status_caption == nullptr) {
        DestroyWindow(window);
        throw std::runtime_error("CreateWindowExW for the native status caption failed.");
    }
    layout_status_caption(state, kInitialWidth, kInitialHeight);
    return window;
}

void set_client_size(HWND window, std::uint32_t width, std::uint32_t height) {
    RECT rectangle{};
    rectangle.right = static_cast<LONG>(width);
    rectangle.bottom = static_cast<LONG>(height);
    const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE));
    const DWORD extended_style = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE));
    if (AdjustWindowRectEx(&rectangle, style, GetMenu(window) != nullptr ? TRUE : FALSE, extended_style) == FALSE) {
        throw std::runtime_error("AdjustWindowRectEx failed during window self-test.");
    }
    if (SetWindowPos(
            window,
            nullptr,
            0,
            0,
            static_cast<int>(rectangle.right - rectangle.left),
            static_cast<int>(rectangle.bottom - rectangle.top),
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE)
        == FALSE) {
        throw std::runtime_error("SetWindowPos failed during window self-test.");
    }
}

void begin_window_self_test(
    HWND window,
    const WindowState& state,
    const RunTelemetry& telemetry,
    SelfTestStage& stage,
    SelfTestSummary& summary) {
    summary.requested = true;
    summary.resize_message_baseline = state.resize_message_count;
    summary.focus_gain_baseline = state.focus_gain_count;
    summary.focus_loss_baseline = state.focus_loss_count;
    summary.device_resize_baseline = telemetry.device_resize_count;
    set_client_size(window, kSelfTestWidth, kSelfTestHeight);
    stage = SelfTestStage::WaitForSmallResize;
}

void advance_window_self_test(
    HWND window,
    WindowState& state,
    const sonic4ep2::d3d9::D3d9Device& device,
    const RunTelemetry& telemetry,
    SelfTestStage& stage,
    SelfTestSummary& summary) {
    if (stage == SelfTestStage::WaitForSmallResize
        && !state.resize_pending
        && device.width() == kSelfTestWidth
        && device.height() == kSelfTestHeight) {
        summary.small_resize_verified = true;
        const std::uint32_t focus_gain_before = state.focus_gain_count;
        const std::uint32_t focus_loss_before = state.focus_loss_count;
        SendMessageW(window, WM_SETFOCUS, 0u, 0);
        SendMessageW(window, WM_KEYDOWN, static_cast<WPARAM>('D'), 0);
        if (!is_pressed(state, static_cast<std::uint32_t>('D'))) {
            throw std::logic_error("Native window self-test could not register an owned-window key.");
        }
        SendMessageW(window, WM_KILLFOCUS, 0u, 0);
        if (is_pressed(state, static_cast<std::uint32_t>('D'))
            || state.focus_gain_count <= focus_gain_before
            || state.focus_loss_count <= focus_loss_before) {
            throw std::logic_error("Native window self-test did not clear pressed keys on focus loss.");
        }
        summary.focus_loss_handler_verified = true;
        SendMessageW(window, WM_SETFOCUS, 0u, 0);
        set_client_size(window, kInitialWidth, kInitialHeight);
        stage = SelfTestStage::WaitForInitialResize;
    } else if (stage == SelfTestStage::WaitForInitialResize
        && !state.resize_pending
        && device.width() == kInitialWidth
        && device.height() == kInitialHeight) {
        summary.restore_resize_verified = true;
        summary.resize_messages = state.resize_message_count - summary.resize_message_baseline;
        summary.focus_gain_messages = state.focus_gain_count - summary.focus_gain_baseline;
        summary.focus_loss_messages = state.focus_loss_count - summary.focus_loss_baseline;
        summary.device_resizes = telemetry.device_resize_count - summary.device_resize_baseline;
        if (!summary.small_resize_verified
            || !summary.restore_resize_verified
            || !summary.focus_loss_handler_verified
            || summary.resize_messages < 2u
            || summary.focus_gain_messages < 2u
            || summary.focus_loss_messages < 1u
            || summary.device_resizes < 2u) {
            throw std::logic_error("Native window self-test did not complete its focus and resize assertions.");
        }
        summary.completed = true;
        stage = SelfTestStage::Complete;
    }
}

bool dispatch_messages() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0u, 0u, PM_REMOVE) != FALSE) {
        if (message.message == WM_QUIT) {
            return false;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return true;
}

void wait_for_messages(DWORD timeout_milliseconds) noexcept {
    const DWORD result = MsgWaitForMultipleObjectsEx(
        0u,
        nullptr,
        timeout_milliseconds,
        QS_ALLINPUT,
        MWMO_INPUTAVAILABLE);
    if (result == WAIT_FAILED) {
        Sleep(timeout_milliseconds);
    }
}

bool is_recoverable_device_error(const sonic4ep2::d3d9::D3d9Error& error) noexcept {
    return error.result() == static_cast<std::int32_t>(D3DERR_DEVICELOST)
        || error.result() == static_cast<std::int32_t>(D3DERR_DEVICENOTRESET);
}

void request_reset_for_current_client(HWND window, WindowState& state) {
    RECT rectangle{};
    if (GetClientRect(window, &rectangle) == FALSE) {
        throw std::runtime_error("GetClientRect failed while recovering the D3D9 device.");
    }
    if (rectangle.right <= rectangle.left || rectangle.bottom <= rectangle.top) {
        state.minimized = true;
        return;
    }
    state.pending_width = static_cast<std::uint32_t>(rectangle.right - rectangle.left);
    state.pending_height = static_cast<std::uint32_t>(rectangle.bottom - rectangle.top);
    state.resize_pending = true;
    state.reset_required = true;
}

bool apply_pending_resize(
    sonic4ep2::d3d9::D3d9Device& device,
    WindowState& state,
    RunTelemetry& telemetry) {
    if (!state.resize_pending) {
        return true;
    }
    if (!state.reset_required
        && device.width() == state.pending_width
        && device.height() == state.pending_height) {
        state.resize_pending = false;
        return true;
    }

    try {
        device.resize(state.pending_width, state.pending_height);
    } catch (const sonic4ep2::d3d9::D3d9Error& error) {
        if (is_recoverable_device_error(error)) {
            return false;
        }
        throw;
    }
    state.resize_pending = false;
    state.reset_required = false;
    ++telemetry.device_resize_count;
    telemetry.width = device.width();
    telemetry.height = device.height();
    return true;
}

std::uint32_t rgba_at(const sonic4ep2::d3d9::ReadbackImage& image, std::size_t pixel_index) noexcept {
    const std::size_t offset = pixel_index * 4u;
    return (static_cast<std::uint32_t>(image.rgba[offset]) << 24u)
        | (static_cast<std::uint32_t>(image.rgba[offset + 1u]) << 16u)
        | (static_cast<std::uint32_t>(image.rgba[offset + 2u]) << 8u)
        | static_cast<std::uint32_t>(image.rgba[offset + 3u]);
}

ReadbackSummary summarize_readback(const sonic4ep2::d3d9::ReadbackImage& image) {
    if (image.width == 0u || image.height == 0u) {
        throw std::runtime_error("D3D9 readback returned an empty image.");
    }
    const std::size_t width = static_cast<std::size_t>(image.width);
    const std::size_t height = static_cast<std::size_t>(image.height);
    if (width > std::numeric_limits<std::size_t>::max() / height) {
        throw std::overflow_error("D3D9 readback dimensions overflow.");
    }
    const std::size_t pixel_count = width * height;
    if (pixel_count > std::numeric_limits<std::size_t>::max() / 4u
        || image.rgba.size() != pixel_count * 4u) {
        throw std::runtime_error("D3D9 readback dimensions do not match its pixels.");
    }

    std::unordered_map<std::uint32_t, std::uint64_t> colors;
    colors.reserve(pixel_count);
    for (std::size_t pixel = 0u; pixel < pixel_count; ++pixel) {
        ++colors[rgba_at(image, pixel)];
    }

    ReadbackSummary summary;
    summary.attempted = true;
    summary.available = true;
    std::uint64_t dominant_count = 0u;
    for (const auto& entry : colors) {
        if (entry.second > dominant_count) {
            summary.dominant_rgba = entry.first;
            dominant_count = entry.second;
        }
    }
    summary.dominant_pixels = dominant_count;
    summary.nonuniform_pixels = static_cast<std::uint64_t>(pixel_count) - dominant_count;
    summary.distinct_pixels = static_cast<std::uint64_t>(colors.size());
    return summary;
}

void append_json_string(std::ostringstream& output, std::string_view value) {
    constexpr char hexadecimal[] = "0123456789abcdef";
    output.put('"');
    for (const char value_character : value) {
        const unsigned char character = static_cast<unsigned char>(value_character);
        switch (character) {
        case '"':
            output << "\\\"";
            break;
        case '\\':
            output << "\\\\";
            break;
        case '\b':
            output << "\\b";
            break;
        case '\f':
            output << "\\f";
            break;
        case '\n':
            output << "\\n";
            break;
        case '\r':
            output << "\\r";
            break;
        case '\t':
            output << "\\t";
            break;
        default:
            if (character < 0x20u) {
                output << "\\u00" << hexadecimal[character >> 4u] << hexadecimal[character & 0x0fu];
            } else {
                output.put(static_cast<char>(character));
            }
            break;
        }
    }
    output.put('"');
}

std::string renderer_json_object(std::string value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    if (first == std::string::npos || value[first] != '{' || value[last] != '}') {
        throw std::runtime_error("NativeStageRenderer returned a non-object report.");
    }
    return value.substr(first, last - first + 1u);
}

std::string build_success_report(
    const RunIdentity& identity,
    const RunTelemetry& telemetry,
    const sonic4ep2::app::NativeStageRenderer& renderer,
    bool selftest_window) {
    std::ostringstream output;
    output << '{';
    output << "\"frames\":" << telemetry.frames;
    output << ",\"width\":" << telemetry.width;
    output << ",\"height\":" << telemetry.height;
    output << ",\"process_id\":" << GetCurrentProcessId();
    output << ",\"executable_name\":";
    append_json_string(output, identity.executable_name);
    output << ",\"window_title\":";
    append_json_string(output, wide_to_utf8(telemetry.player_scene ? kPlayerWindowTitle : kWindowTitle));
    output << ",\"selftest_window\":" << (selftest_window ? "true" : "false");
    output << ",\"selftest\":{";
    output << "\"requested\":" << (telemetry.selftest.requested ? "true" : "false");
    output << ",\"completed\":" << (telemetry.selftest.completed ? "true" : "false");
    output << ",\"focus_loss_handler_verified\":" << (telemetry.selftest.focus_loss_handler_verified ? "true" : "false");
    output << ",\"os_focus_transfer_verified\":false";
    output << ",\"small_resize_verified\":" << (telemetry.selftest.small_resize_verified ? "true" : "false");
    output << ",\"restore_resize_verified\":" << (telemetry.selftest.restore_resize_verified ? "true" : "false");
    output << ",\"resize_messages\":" << telemetry.selftest.resize_messages;
    output << ",\"focus_gain_messages\":" << telemetry.selftest.focus_gain_messages;
    output << ",\"focus_loss_messages\":" << telemetry.selftest.focus_loss_messages;
    output << ",\"device_resizes\":" << telemetry.selftest.device_resizes;
    output << '}';
    if (telemetry.readback.attempted) {
        output << ",\"readback_available\":" << (telemetry.readback.available ? "true" : "false");
        if (telemetry.readback.available) {
            output << ",\"dominant_rgba\":" << telemetry.readback.dominant_rgba;
            output << ",\"dominant_pixels\":" << telemetry.readback.dominant_pixels;
            output << ",\"nonuniform_pixels\":" << telemetry.readback.nonuniform_pixels;
            output << ",\"distinct_pixels\":" << telemetry.readback.distinct_pixels;
        } else {
            output << ",\"readback_error\":";
            append_json_string(output, telemetry.readback.error);
        }
    }
    output << ",\"renderer\":" << renderer_json_object(renderer.report_json());
    if (!telemetry.player_json.empty()) {
        output << ",\"player\":" << telemetry.player_json;
    }
    if (!telemetry.audio_json.empty()) {
        output << ",\"audio\":" << telemetry.audio_json;
    }
    output << '}';
    return output.str();
}

std::string build_failure_report(
    const RunIdentity& identity,
    const RunTelemetry& telemetry,
    std::string_view error) {
    std::ostringstream output;
    output << '{';
    output << "\"frames\":" << telemetry.frames;
    output << ",\"width\":" << telemetry.width;
    output << ",\"height\":" << telemetry.height;
    output << ",\"process_id\":" << GetCurrentProcessId();
    output << ",\"executable_name\":";
    append_json_string(output, identity.executable_name.empty() ? "Sonic-Decomp.exe" : identity.executable_name);
    output << ",\"window_title\":";
    append_json_string(output, wide_to_utf8(telemetry.player_scene ? kPlayerWindowTitle : kWindowTitle));
    output << ",\"error\":";
    append_json_string(output, error);
    output << '}';
    return output.str();
}

void write_report(const std::filesystem::path& path, std::string_view report) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        throw std::runtime_error("Cannot open the requested report path.");
    }
    output.write(report.data(), checked_character_count(report.size(), "Report"));
    output.put('\n');
    if (!output) {
        throw std::runtime_error("Cannot write the requested report.");
    }
}

void emit_success_report(
    const Options& options,
    const RunIdentity& identity,
    const RunTelemetry& telemetry,
    const sonic4ep2::app::NativeStageRenderer& renderer) {
    if (!options.report_path.has_value() && options.smoke_frames == 0u) {
        return;
    }
    const std::string report = build_success_report(identity, telemetry, renderer, options.selftest_window);
    if (options.report_path.has_value()) {
        write_report(*options.report_path, report);
    }
    if (options.smoke_frames != 0u) {
        std::cout << report << '\n';
    }
}

RunIdentity make_identity() {
    RunIdentity identity;
    identity.executable_path = executable_path();
    identity.executable_name = wide_to_utf8(identity.executable_path.filename().wstring());
    if (identity.executable_name.empty()) {
        throw std::runtime_error("The current executable has no file name.");
    }
    return identity;
}

int run_application(
    HINSTANCE instance,
    const Options& options,
    const RunIdentity& identity,
    const std::filesystem::path& data_root,
    RunTelemetry& telemetry) {
    RegisteredWindowClass window_class(instance);
    WindowState state;
    WindowHandle window(create_window(instance, state));
    telemetry.player_scene = options.player_scene;
    if (options.player_scene) {
        SetWindowTextW(window.get(), kPlayerWindowTitle);
        SetWindowTextW(state.status_caption, kPlayerStatusCaption);
    }
    auto device = sonic4ep2::d3d9::D3d9Device::create(
        sonic4ep2::d3d9::DeviceCreateOptions{
            window.get(),
            0u,
            kInitialWidth,
            kInitialHeight,
            sonic4ep2::d3d9::DepthStencilFormat::D24S8,
            true,
            true});
    telemetry.width = device->width();
    telemetry.height = device->height();
    sonic4ep2::app::NativeStageRenderer renderer(
        *device,
        data_root,
        options.character_inspection,
        options.character_frame,
        options.player_scene);
    std::unique_ptr<sonic4ep2::app::NativePlayerScene> player;
    std::unique_ptr<sonic4ep2::app::NativeSonicAudio> audio;
    if (options.player_scene) {
        player = std::make_unique<sonic4ep2::app::NativePlayerScene>(data_root);
        audio = std::make_unique<sonic4ep2::app::NativeSonicAudio>(
            data_root, options.hidden || options.smoke_frames != 0u || options.selftest_window);
        audio->start_stage_music();
        telemetry.audio_json = audio->report_json();
    }
    double pending_player_time = 0.0;
    bool player_motion_stopped = false;

    ShowWindow(window.get(), options.hidden ? SW_HIDE : SW_SHOWNORMAL);
    if (!options.hidden) {
        UpdateWindow(window.get());
    }

    SelfTestStage selftest_stage = options.selftest_window
        ? SelfTestStage::WaitForSmallResize
        : SelfTestStage::Disabled;
    if (options.selftest_window) {
        begin_window_self_test(window.get(), state, telemetry, selftest_stage, telemetry.selftest);
    }

    auto previous_tick = std::chrono::steady_clock::now();
    const auto diagnostic_deadline = previous_tick + std::chrono::seconds(
        std::max(60u, 30u + options.smoke_frames / 15u));
    for (;;) {
        if ((options.smoke_frames != 0u || (options.selftest_window && !telemetry.selftest.completed))
            && std::chrono::steady_clock::now() >= diagnostic_deadline) {
            throw std::runtime_error("The native window diagnostic exceeded its time limit before completing.");
        }
        if (!dispatch_messages()) {
            break;
        }
        if (audio) audio->update();
        if (state.close_requested) {
            if (audio) audio->set_paused(true);
            if (IsWindow(window.get())) {
                DestroyWindow(window.get());
            }
            previous_tick = std::chrono::steady_clock::now();
            continue;
        }
        if (state.minimized) {
            if (audio) audio->set_paused(true);
            wait_for_messages(50u);
            previous_tick = std::chrono::steady_clock::now();
            continue;
        }

        const sonic4ep2::d3d9::DeviceStatus status = device->cooperative_status();
        if (status == sonic4ep2::d3d9::DeviceStatus::Lost) {
            if (audio) audio->set_paused(true);
            wait_for_messages(50u);
            previous_tick = std::chrono::steady_clock::now();
            continue;
        }
        if (status == sonic4ep2::d3d9::DeviceStatus::NotReset) {
            request_reset_for_current_client(window.get(), state);
            if (state.minimized) {
                if (audio) audio->set_paused(true);
                wait_for_messages(50u);
                previous_tick = std::chrono::steady_clock::now();
                continue;
            }
        }

        if (!apply_pending_resize(*device, state, telemetry)) {
            if (audio) audio->set_paused(true);
            wait_for_messages(50u);
            previous_tick = std::chrono::steady_clock::now();
            continue;
        }

        advance_window_self_test(window.get(), state, *device, telemetry, selftest_stage, telemetry.selftest);
        if (selftest_stage == SelfTestStage::WaitForSmallResize
            || selftest_stage == SelfTestStage::WaitForInitialResize) {
            if (audio) audio->set_paused(true);
            previous_tick = std::chrono::steady_clock::now();
            continue;
        }
        if (options.smoke_frames == 0u && !has_foreground_input(window.get(), state)) {
            if (audio) audio->set_paused(true);
            clear_pressed(state);
            wait_for_messages(50u);
            previous_tick = std::chrono::steady_clock::now();
            continue;
        }

        const auto now = std::chrono::steady_clock::now();
        const float delta_seconds = std::clamp(
            std::chrono::duration<float>(now - previous_tick).count(),
            0.0f,
            0.1f);
        previous_tick = now;
        if (player) {
            audio->set_paused(player_motion_stopped);
            const bool foreground = has_foreground_input(window.get(), state);
            const bool right = options.smoke_player_walk || options.smoke_player_dash
                || (foreground && (is_pressed(state, 'D') || is_pressed(state, VK_RIGHT)));
            const bool left = foreground && (is_pressed(state, 'A') || is_pressed(state, VK_LEFT));
            const auto direction = right ? PlayerWalkDirection::Right
                : (left ? PlayerWalkDirection::Left : PlayerWalkDirection::None);
            const std::int32_t magnitude = right ? 0x7000 : (left ? -0x7000 : 0);
            auto jump_buttons = static_cast<std::uint16_t>(
                (foreground && is_pressed(state, 'J') ? 0x1000u : 0u)
                | (foreground && is_pressed(state, 'K') ? 0x2000u : 0u));
            if (options.smoke_player_dash) {
                jump_buttons = telemetry.frames >= 160u && telemetry.frames <= 162u ? 0x1000u
                    : (telemetry.frames == 165u ? 0x2000u : 0u);
            }
            const bool crouch = foreground && (is_pressed(state, 'S') || is_pressed(state, VK_DOWN));
            const bool up = foreground && (is_pressed(state, 'W') || is_pressed(state, VK_UP));
            constexpr double tick_seconds = 1.0 / 60.0;
            pending_player_time += options.smoke_frames != 0u
                ? tick_seconds : static_cast<double>(delta_seconds);
            while (pending_player_time >= tick_seconds) {
                pending_player_time -= tick_seconds;
                if (player_motion_stopped) continue;
                if (player->advance_ground_motion(direction, magnitude, jump_buttons, crouch, up)
                        == PlayerGroundMotionStatus::Applied) {
                    for (const auto cue : player->sound_requests()) audio->play(cue);
                    for (const auto& pickup : player->ring_pickups()) {
                        if (pickup.sound_requested) {
                            audio->play(pickup.sound_right ? SonicSoundCue::Ring1R : SonicSoundCue::Ring1L);
                        }
                    }
                } else {
                    player_motion_stopped = true;
                    audio->set_paused(true);
                    SetWindowTextW(state.status_caption,
                        L"Movement is unavailable for this state | Esc: close");
                }
            }
        } else {
            update_navigation(
                state,
                window.get(),
                delta_seconds,
                options.character_inspection ? 0.05f : 1.0f);
        }

        try {
            if (player) {
                const auto camera = player->camera_view(static_cast<std::int32_t>(device->width()),
                    static_cast<std::int32_t>(device->height()));
                renderer.set_ring_instances(player->ring_world_matrices(camera.roll_angle));
                renderer.set_player_effects(player->effects());
                renderer.render_player_scene(player->world_matrix(), camera, player->animation());
                telemetry.player_json = player->report_json();
                telemetry.audio_json = audio->report_json();
            } else {
                renderer.render(
                    state.horizontal_offset,
                    state.vertical_offset,
                    state.distance_factor,
                    delta_seconds);
            }
            const bool is_last_smoke_frame = options.smoke_frames != 0u
                && telemetry.frames + 1u == options.smoke_frames;
            if (is_last_smoke_frame && !player && !telemetry.readback.attempted) {
                telemetry.readback.attempted = true;
                try {
                    telemetry.readback = summarize_readback(device->readback_current_target());
                } catch (const std::exception& error) {
                    telemetry.readback.available = false;
                    telemetry.readback.error = error.what();
                }
            }
            device->present();
        } catch (const sonic4ep2::d3d9::D3d9Error& error) {
            if (is_recoverable_device_error(error)) {
                if (audio) audio->set_paused(true);
                previous_tick = std::chrono::steady_clock::now();
                wait_for_messages(50u);
                continue;
            }
            throw;
        }

        ++telemetry.frames;
        if (options.smoke_frames != 0u && telemetry.frames == options.smoke_frames) {
            emit_success_report(options, identity, telemetry, renderer);
            return 0;
        }
    }

    if (options.selftest_window && !telemetry.selftest.completed) {
        throw std::runtime_error("The native window closed before its self-test completed.");
    }
    if (options.smoke_frames != 0u) {
        throw std::runtime_error("The diagnostic smoke run ended before all requested frames rendered and presented.");
    }
    emit_success_report(options, identity, telemetry, renderer);
    return 0;
}

void report_failure(
    const Options& options,
    bool options_parsed,
    bool raw_hidden_requested,
    const RunIdentity& identity,
    const RunTelemetry& telemetry,
    std::string_view message) {
    try {
        std::cerr << "Sonic-Decomp: " << message << '\n';
    } catch (...) {
    }

    if (options_parsed && options.report_path.has_value()) {
        try {
            write_report(*options.report_path, build_failure_report(identity, telemetry, message));
        } catch (const std::exception& report_error) {
            try {
                std::cerr << "Sonic-Decomp: unable to write failure report: " << report_error.what() << '\n';
            } catch (...) {
            }
        } catch (...) {
        }
    }

    const bool suppress_message_box = (options_parsed && options.hidden && options.smoke_frames != 0u)
        || (!options_parsed && raw_hidden_requested);
    if (!suppress_message_box) {
        const std::wstring wide_message = utf8_to_wide(message);
        MessageBoxW(nullptr, wide_message.c_str(), kWindowTitle, MB_OK | MB_ICONERROR);
    }
}

}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    Options options;
    RunIdentity identity;
    RunTelemetry telemetry;
    bool options_parsed = false;
    const bool raw_hidden_requested = command_line_requests_hidden();

    try {
        identity = make_identity();
        options = parse_command_line();
        options_parsed = true;
        const std::filesystem::path data_root = resolve_data_root(options, identity);
        return run_application(instance, options, identity, data_root, telemetry);
    } catch (const std::exception& error) {
        report_failure(options, options_parsed, raw_hidden_requested, identity, telemetry, error.what());
    } catch (...) {
        report_failure(options, options_parsed, raw_hidden_requested, identity, telemetry, "Unexpected native host failure.");
    }
    return 1;
}
