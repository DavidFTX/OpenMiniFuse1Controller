// minifuse_ctl - toggle INST / +48V / DIRECT MONITOR on an Arturia MiniFuse 1
//
// Talks straight to the Arturia (Thesycon-based) kernel driver with DeviceIoControl.
//
//   find   : SetupDi device interface {F607DF81-BCAD-4EED-AD96-B4D6F89D6F27}
//   check  : IOCTL 0x80882004  -> driver API version (expects 5 / 0x48)
//   ident  : IOCTL 0x808820C4  -> u32 VID, u32 PID, ...
//   read   : IOCTL 0x80882086  hdr{bRequest 0xB2, wValue 0,       wIndex 0}  -> 46-byte state block
//   write  : IOCTL 0x80882081  hdr{bRequest 0x22, wValue cs << 8, wIndex 0}  + u16 0/1
//
// The state block is an array of little-endian u16 indexed by the control selector (cs).
//
// Build (x64 Developer Command Prompt):  cl /EHsc /O2 /W4 minifuse_ctl.cpp setupapi.lib
// Usage:  minifuse1_ctl status
//         minifuse1_ctl <inst|48v|dm> <on|off|toggle>

#include <windows.h>
#include <setupapi.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "setupapi.lib")

// Device interface registered by arturiaminifuseusbaudio.sys
static const GUID kDriverInterface = {0xF607DF81, 0xBCAD, 0x4EED, {0xAD, 0x96, 0xB4, 0xD6, 0xF8, 0x9D, 0x6F, 0x27}};

// IOCTLs, from the TUSBAUDIO_* exports of arturiaminifuseusbaudioapi_x64.dll
constexpr DWORD kIoctlGetApiVersion = 0x80882004;     // METHOD_BUFFERED, fn 0x801
constexpr DWORD kIoctlGetDeviceProperties = 0x808820C4;  // METHOD_BUFFERED, fn 0x831
constexpr DWORD kIoctlControlRequestOut = 0x80882081;  // METHOD_IN_DIRECT,  fn 0x820 (AudioControlRequestSet)
constexpr DWORD kIoctlControlRequestIn = 0x80882086;   // METHOD_OUT_DIRECT, fn 0x821 (AudioControlRequestGet)

constexpr DWORD kExpectedApiMajor = 5;
constexpr DWORD kExpectedApiMinor = 0x48;

// 24-byte input header for the control-request IOCTLs. Built by the DLL at 0x180009e90 / 0x180009f80.
#pragma pack(push, 1)
struct ControlRequest {
    DWORD one0;        // always 1
    DWORD one1;        // always 1 for audio-control requests
    DWORD recipient;   // 1 = interface
    BYTE bRequest;
    BYTE pad[3];
    WORD wValue;       // (control selector << 8) | channel number
    WORD wIndex;       // entity id << 8 (driver fills the interface number)
    DWORD timeoutMs;
};
#pragma pack(pop)
static_assert(sizeof(ControlRequest) == 24, "header must be 24 bytes");

// Vendor protocol constants
constexpr BYTE kEntity = 0x00;
constexpr BYTE kReqSetParam = 0x22;   // hard-coded at VA 0x14072195a
constexpr BYTE kReqGetState = 0xB2;   // hard-coded at VA 0x140483cd2 (MiniFuse 1 class)
constexpr DWORD kStateLen = 46;
constexpr DWORD kTimeoutMs = 1000;    // 0x3E8, same as the app

constexpr DWORD kArturiaVid = 0x1C75;
constexpr DWORD kMiniFuse1Pid = 0xAF80;

struct Control {
    const char* name;
    const char* label;
    BYTE cs;  // control selector == word index in the state block
};
constexpr Control kControls[] = {
    {"inst", "INST",           0},
    {"48v",  "+48V",           4},
    {"dm",   "DIRECT MONITOR", 5},
};

static bool Ioctl(HANDLE h, DWORD code, void* in, DWORD inLen, void* out, DWORD outLen, DWORD* got = nullptr) {
    DWORD n = 0;
    if (!DeviceIoControl(h, code, in, inLen, out, outLen, &n, nullptr)) {
        std::fprintf(stderr, "IOCTL 0x%08lX failed: error %lu\n", code, GetLastError());
        return false;
    }
    if (got) *got = n;
    return true;
}

static std::vector<std::wstring> FindDevicePaths() {
    std::vector<std::wstring> paths;
    HDEVINFO set = SetupDiGetClassDevsW(&kDriverInterface, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return paths;
    SP_DEVICE_INTERFACE_DATA ifd = {sizeof(ifd)};
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &kDriverInterface, i, &ifd); ++i) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &ifd, nullptr, 0, &need, nullptr);
        std::vector<BYTE> buf(need);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buf.data());
        detail->cbSize = sizeof(*detail);
        if (SetupDiGetDeviceInterfaceDetailW(set, &ifd, detail, need, nullptr, nullptr))
            paths.emplace_back(detail->DevicePath);
    }
    SetupDiDestroyDeviceInfoList(set);
    return paths;
}

static HANDLE OpenMiniFuse1() {
    for (const auto& path : FindDevicePaths()) {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;

        DWORD ver[6] = {};
        DWORD props[260] = {};  // u32 vid, u32 pid, u32 bcdDevice, wchar serial[...]
        bool ok = Ioctl(h, kIoctlGetApiVersion, nullptr, 0, ver, sizeof(ver)) &&
                  Ioctl(h, kIoctlGetDeviceProperties, nullptr, 0, props, sizeof(props));
        if (ok && (ver[0] != kExpectedApiMajor || ver[1] != kExpectedApiMinor))
            std::fprintf(stderr, "warning: driver API %lu.%lu, tested with %lu.%lu\n", ver[0], ver[1],
                         kExpectedApiMajor, kExpectedApiMinor);
        if (ok && props[0] == kArturiaVid && props[1] == kMiniFuse1Pid) return h;
        if (ok) std::fprintf(stderr, "skipping VID %04lX PID %04lX\n", props[0], props[1]);
        CloseHandle(h);
    }
    return INVALID_HANDLE_VALUE;
}

static ControlRequest MakeRequest(BYTE bRequest, BYTE cs, BYTE cn) {
    ControlRequest r = {};
    r.one0 = 1;
    r.one1 = 1;
    r.recipient = 1;
    r.bRequest = bRequest;
    r.wValue = static_cast<WORD>((cs << 8) | cn);
    r.wIndex = static_cast<WORD>(kEntity << 8);
    r.timeoutMs = kTimeoutMs;
    return r;
}

static bool ReadState(HANDLE h, WORD (&words)[kStateLen / 2]) {
    ControlRequest r = MakeRequest(kReqGetState, 0, 0);
    BYTE buf[kStateLen] = {};
    if (!Ioctl(h, kIoctlControlRequestIn, &r, sizeof(r), buf, kStateLen)) return false;
    std::memcpy(words, buf, sizeof(buf));
    return true;
}

static bool WriteControl(HANDLE h, const Control& c, bool on) {
    ControlRequest r = MakeRequest(kReqSetParam, c.cs, 0);
    BYTE data[2] = {static_cast<BYTE>(on ? 1 : 0), 0};
    return Ioctl(h, kIoctlControlRequestOut, &r, sizeof(r), data, sizeof(data));
}

static void PrintStatus(const WORD (&w)[kStateLen / 2]) {
    for (const auto& c : kControls)
        std::printf("%-15s %s\n", c.label, w[c.cs] ? "ON" : "off");
}

static int Usage() {
    std::fprintf(stderr,
                 "usage: minifuse_ctl status\n"
                 "       minifuse_ctl <inst|48v|dm> <on|off|toggle>\n");
    return 2;
}

int main(int argc, char** argv) {
    if (argc < 2) return Usage();
    std::string cmd = argv[1];
    const Control* target = nullptr;
    int mode = -1;  // 0 off, 1 on, 2 toggle
    if (cmd != "status") {
        for (const auto& c : kControls)
            if (cmd == c.name) target = &c;
        if (!target || argc < 3) return Usage();
        std::string m = argv[2];
        mode = m == "off" ? 0 : m == "on" ? 1 : m == "toggle" ? 2 : -1;
        if (mode < 0) return Usage();
    }

    HANDLE h = OpenMiniFuse1();
    if (h == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "no MiniFuse 1 (VID 1C75 PID AF80) found - is the Arturia driver installed?\n");
        return 1;
    }

    int rc = 0;
    WORD w[kStateLen / 2] = {};
    if (!ReadState(h, w)) {
        rc = 1;
    } else if (!target) {
        PrintStatus(w);
    } else {
        bool on = mode == 2 ? w[target->cs] == 0 : mode == 1;
        if (!WriteControl(h, *target, on)) {
            rc = 1;
        } else {
            Sleep(100);  // let the device apply the change before reading back
            if (ReadState(h, w)) PrintStatus(w);
        }
    }
    CloseHandle(h);
    return rc;
}
