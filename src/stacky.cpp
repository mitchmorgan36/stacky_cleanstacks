/**************************************************************************************************
 * System libs
 **************************************************************************************************/
#include <windows.h>
#include <Shlobj.h>
#include <Shobjidl.h>
#include <wincodec.h>
#include <Tlhelp32.h>
#pragma comment(lib, "Comctl32.lib")

/**************************************************************************************************
 * Standard libs
 **************************************************************************************************/
#include <cstdio>
#include <vector>
#include <string>

#include "resource.h" // for version info

/**************************************************************************************************
 * Simple types and constants
 **************************************************************************************************/
typedef wchar_t                 Char;
typedef unsigned char           Byte;
typedef __time64_t              Time;
typedef std::wstring            String;
typedef std::vector<String>     StringList;

const String CACHE_FILE_NAME    = L"!stacky.cache";
const String STACKY_EXEC_NAME   = L"stacky.exe";
const Char*	 STACKY_WINDOW_NAME = L"stacky";
const Char*	 DIR_SEP            = L"\\";

enum {
	WM_BASE                     = WM_USER + 100,
	WM_OPEN_TARGET_FOLDER       = WM_BASE + 1,
	WM_MENU_ITEM                = WM_BASE + 2,

	APP_EXIT_DELAY              = 3 * 1000,

    ERR_PATH_MISSING            = 401,
    ERR_PATH_INVALID            = 402,
    ERR_PARAM_UNKNOWN           = 403,
};

/**************************************************************************************************
 * The meat
 **************************************************************************************************/

struct Util {

    static String rtrim(const String& target, const String& trim) {
	    size_t cutoff_pos = target.size() - trim.size();
	    return target.rfind(trim) == cutoff_pos ? target.substr(0, cutoff_pos) : target;
    }
    static String ltrim(const String& target, const String& trim) {
	    size_t cutoff_pos = trim.size();
        return target.find(trim) != String::npos ? target.substr(cutoff_pos) : target;
    }
    static String trim(const String& target, const String& trim) {
        return rtrim(ltrim(target, trim), trim);
    }
    static String quote(const String& target) {
	    return L"\"" + target + L"\"";
    }
    static String escape_mnemonics(const String& target) {
        String escaped;
        for (size_t i = 0; i < target.size(); i++) {
            escaped += target[i];
            if (target[i] == L'&') {
                escaped += L'&';
            }
        }
        return escaped;
    }
    static void kill_other_stackies() {
        PROCESSENTRY32 entry = { 0 };
	    entry.dwSize = sizeof(PROCESSENTRY32);
        BOOL found = false;
	    HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPALL, 0);
	    do {
		    found = ::Process32Next(snapshot, &entry);
		    if (entry.th32ProcessID != ::GetCurrentProcessId() && ::lstrcmpiW(entry.szExeFile, STACKY_EXEC_NAME.c_str()) == 0) {
			    HANDLE hOtherStacky = ::OpenProcess(PROCESS_ALL_ACCESS, FALSE, entry.th32ProcessID);
			    if (hOtherStacky) {
				    ::TerminateProcess(hOtherStacky, 0);
				    ::CloseHandle(hOtherStacky);
			    }
			    else {
                    Util::msg(L"Failed to open another stacky.exe process. Kill stacky.exe manually.");
			    }
		    }
	    } while(found);
	    ::CloseHandle(snapshot);
    }
    static Time get_modified(const String& file_path) {
        struct _stat buf;
        return _wstat(file_path.c_str(), &buf) ? 0 : buf.st_mtime;
    }
    static int parse_cmd_line(const String& cmd_line, String& stack_path, String& opts) {
        stack_path = cmd_line;
        if (stack_path.size() < 1) {
            return ERR_PATH_MISSING;
        }
        DWORD attrs = ::GetFileAttributes(trim(stack_path, L"\"").c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            return ERR_PATH_INVALID;
        }
        return 0;
    }
    static void msgt(const String& title, const Char* format, ...) {
	    static Char msgBuf[4096] = { 0 };
	    va_list arglist;
	    va_start(arglist, format);
        vswprintf_s(msgBuf, sizeof(msgBuf) / sizeof(msgBuf[0]), format, arglist);
	    va_end(arglist);
        ::MessageBox(0, msgBuf, title.c_str(), MB_OK | MB_ICONINFORMATION);
    }
    static void msg(const Char* format, ...) {
	    static Char msgBuf[4096] = { 0 };
	    va_list arglist;
	    va_start(arglist, format);
        vswprintf_s(msgBuf, sizeof(msgBuf) / sizeof(msgBuf[0]), format, arglist);
	    va_end(arglist);
        ::MessageBox(0, msgBuf, L"Stacky", MB_OK | MB_ICONINFORMATION);
    }
    static bool has_case_insensitive_suffix(const String& target, const String& suffix) {
        if (target.size() < suffix.size()) {
            return false;
        }
        return ::_wcsicmp(target.c_str() + target.size() - suffix.size(), suffix.c_str()) == 0;
    }
    static bool has_case_insensitive_prefix(const String& target, const String& prefix) {
        return target.size() >= prefix.size() && ::_wcsnicmp(target.c_str(), prefix.c_str(), prefix.size()) == 0;
    }
    static bool get_env_string(const Char* name, String& value) {
        value.clear();
        DWORD required = ::GetEnvironmentVariable(name, 0, 0);
        if (!required) {
            return false;
        }
        std::vector<Char> buffer(required);
        if (!::GetEnvironmentVariable(name, &buffer[0], required)) {
            return false;
        }
        value.assign(&buffer[0]);
        return !value.empty();
    }
};

struct Buffer {
    size_t  capacity, size;
    Byte*   data;

    Buffer() : capacity(0), size(0), data(0) {}
    ~Buffer() { }

    void free() {
        if (data) {
            delete [] data;
            data = 0;
        }
        capacity = size = 0;
    }
    bool load(const String& str, bool append_null) {
        load(str.c_str(), str.size() * sizeof(Char));
        if (append_null) {
            Char str_end[1] = { 0 };
            load(str_end, sizeof(Char));
        }
        return true;
    }
    bool load(const void* src, size_t src_size) {
        grow(src_size + size);
        memcpy(data + size, src, src_size);
        size += src_size;
        return true;
    }
    bool load(const String& file_path) {
        FileWrap f(file_path, L"rb");
        if (!f.is_open()) {
            return false;
        }
        size_t file_size = f.size();
        if (!file_size) {
            return false;
        }
        grow(file_size + size);
        f.read(data + size, file_size);
        size += file_size;
        return true;
    }
    bool save(const String& file_path) {
        FileWrap f(file_path, L"wb");
        if (!f.is_open()) {
            return false;
        }
        f.write(data, size);
        return true;
    }

private:
    struct FileWrap {
        FILE* f;
        FileWrap(const String& path, const String& mode)    { _wfopen_s(&f, path.c_str(), mode.c_str()); }
        ~FileWrap()                                         { f && fclose(f); f = 0; }
        bool    is_open()                                   { return f != 0; }
        size_t  write(Byte* data, size_t size)              { return fwrite(data, 1, size, f); }
        size_t  read(Byte* data, size_t size)               { return fread(data, 1, size, f); }
        size_t  size() { 
            fseek(f, 0L, SEEK_END);
            size_t file_size = ftell(f);
            fseek(f, 0L, SEEK_SET); 
            return file_size;
        }
    };
    void grow(size_t new_capacity) {
        if (capacity >= new_capacity) {
            return;
        }
        size_t old_size = size;
        Byte* new_data = new Byte[new_capacity];
        size && memcpy(new_data, data, capacity);
        free();
        data = new_data;
        capacity = new_capacity;
        size = old_size;
    }
};

struct Bmp {

    BITMAPFILEHEADER    file_header;
    BITMAPINFOHEADER    info_header;
    Buffer              bits;
    HBITMAP             hBmp;

    Bmp() : hBmp(0) { 
        memset(&file_header, 0, sizeof(BITMAPFILEHEADER));
        memset(&info_header, 0, sizeof(BITMAPINFOHEADER));
    }

    void close() {
        ::DeleteObject(hBmp);
        bits.free();
        hBmp = 0;
        memset(&file_header, 0, sizeof(BITMAPFILEHEADER));
        memset(&info_header, 0, sizeof(BITMAPINFOHEADER));
    }
    int total_size() {
        return file_header.bfSize;
    }
    int bits_size() {
        return file_header.bfSize - sizeof(BITMAPINFOHEADER) - sizeof(BITMAPFILEHEADER);
    }
    bool load_bits_and_headers(Byte* bytes) {
        close();
        int pos = 0;
        memcpy(&file_header, bytes + pos, sizeof(BITMAPFILEHEADER));
        pos += sizeof(BITMAPFILEHEADER);
        memcpy(&info_header, bytes + pos, sizeof(BITMAPINFOHEADER));
        pos += sizeof(BITMAPINFOHEADER);
        return fill_bitmap(bytes + pos, bits_size());
    }
    bool load_bits_only(Byte* bytes, int bits_size, int width, int height) {
        close();
        memset(&file_header, 0, sizeof(BITMAPFILEHEADER));
        file_header.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + bits_size;
        file_header.bfType = 0x4d42;
        file_header.bfOffBits = 0x36;
        info_header = create_info_header(width, height);
        return fill_bitmap(bytes, bits_size);
    }
    bool serialize(Buffer& buffer) {
        buffer.load(&file_header, sizeof(BITMAPFILEHEADER));
        buffer.load(&info_header, sizeof(BITMAPINFOHEADER));
        buffer.load(bits.data, bits.size);
        return true;
    }
    static BITMAPINFOHEADER create_info_header(int width, int height) {
	    BITMAPINFOHEADER bmih = { 0 };
        bmih.biSize = sizeof(BITMAPINFOHEADER);
        bmih.biWidth = width;
        bmih.biHeight = height;
        bmih.biPlanes = 1;	
        bmih.biBitCount = 32;
        bmih.biCompression = BI_RGB;
        return bmih;
    }
    static bool convert_file_icon(const HICON icon, Bmp& bmp) {
        static IWICImagingFactory* img_factory = 0;
        if (!img_factory) {
            // In VS 2011 beta, clsid has to be changed to CLSID_WICImagingFactory1 (from CLSID_WICImagingFactory)
            if (!SUCCEEDED(::CoInitialize(0)) || !SUCCEEDED(::CoCreateInstance(CLSID_WICImagingFactory1, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&img_factory)))) {
                return false;
            }
        }
        IWICBitmap* pBitmap = 0;
        IWICFormatConverter* pConverter = 0;
        UINT cx = 0, cy = 0;
	    if (SUCCEEDED(img_factory->CreateBitmapFromHICON(icon, &pBitmap))) {
		    if (SUCCEEDED(img_factory->CreateFormatConverter(&pConverter))) {
			    if (SUCCEEDED(pConverter->Initialize(pBitmap, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, 0, 0.0f, WICBitmapPaletteTypeCustom))) {
				    if (SUCCEEDED(pConverter->GetSize(&cx, &cy))) {
						const UINT stride = cx * sizeof(DWORD);
						const UINT buf_size = cy * stride;
                        Byte* buf = new Byte[buf_size];
						pConverter->CopyPixels(0, stride, buf_size, buf);
                        bmp.load_bits_only(buf, buf_size, cx, -(int)cy);
                        delete [] buf;
				    }
			    }
			    pConverter->Release();
		    }
		    pBitmap->Release();
	    }
        return true;
    }
    static HICON extract_file_icon(const String& file_path) {
	    SHFILEINFOW file_info = { 0 };
	    HIMAGELIST hfi = (HIMAGELIST)::SHGetFileInfo(file_path.c_str(), 0, &file_info, sizeof(SHFILEINFOW), SHGFI_SYSICONINDEX);
	    return ::ImageList_GetIcon(hfi, file_info.iIcon, ILD_NORMAL);
    }

private:
    bool fill_bitmap(void* bytes, int byte_count) {
        Byte* buf = 0;
        if (!create_bitmap(info_header.biWidth, info_header.biHeight, (void**)(&buf), &hBmp)) {
            return false;
        }
        memcpy(buf, bytes, byte_count);
        return bits.load(bytes, byte_count);
    }
	static bool create_bitmap(int width, int height, void** bits, HBITMAP* phBmp) {
		BITMAPINFO bmi = { 0 };
		bmi.bmiHeader = create_info_header(width, height);
		*phBmp = ::CreateDIBSection(GetDC(0), &bmi, DIB_RGB_COLORS, bits, 0, 0);
		return *phBmp != 0;
	}
};

struct Cache {

    struct Item {
        String  name;
        Bmp     bmp;
        
        bool create(const String& file_name, const String& file_path) {
            if (!Bmp::convert_file_icon(Bmp::extract_file_icon(file_path), bmp)) {
                return false;
            }
            name = file_name;
            return true;
        }
        void serialize(Buffer& buffer) {
            buffer.load(name, true);
            bmp.serialize(buffer);
        }
        void unserialize(Buffer& buffer, size_t& pos) {
            name = (Char*)(buffer.data + pos);              pos += (name.size() + 1) * sizeof(Char);
            bmp.load_bits_and_headers(buffer.data + pos);   pos += bmp.total_size();
        }
    };


    std::vector<Item>   items;
    bool                was_rebuilt;

    Cache(const String& stack_path) : last_modified(0), was_rebuilt(false), scanned_last_modified(0) {
        base_dir = Util::trim(Util::rtrim(stack_path, DIR_SEP), L"\"") + DIR_SEP;
        cache_path = path(CACHE_FILE_NAME);
    }

    String path(const String& file = L"") const  { 
        return base_dir + file; 
    }
    bool scan() {
	    WIN32_FIND_DATA ffd = { 0 };
	    HANDLE hfind = FindFirstFile(path(L"*").c_str(), &ffd);
	    if (hfind == INVALID_HANDLE_VALUE) {
		    return false;
        }
	    do {
		    String filename = ffd.cFileName;
		    if (filename == L"." || filename == L".." || ffd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)
			    continue;
            scanned_items.push_back(filename);
            update_max_modified(filename);
	    }
	    while (FindNextFile(hfind, &ffd) != 0);
        ::FindClose(hfind);
	    return true;
    }
    bool load() {
        Buffer buffer;
        items.clear();

        buffer.load(cache_path);
        for (size_t pos = 0; pos < buffer.size; ) {
            Item item;
            item.unserialize(buffer, pos);
            items.push_back(item);
        }
        last_modified = Util::get_modified(cache_path);

        if (is_outdated()) {
            rebuild();
            was_rebuilt = true;
        }

        return true;
    }

private:
    String      base_dir;
    String      cache_path;
    Time        last_modified;
    StringList  scanned_items;
    Time        scanned_last_modified;

    bool rebuild() {
        Buffer buffer;
        items.clear();
        
        Item item;
        item.create(L"Open:  " + Util::rtrim(path(), DIR_SEP), path());
        item.serialize(buffer);
        items.push_back(item);
        for (size_t i = 0; i < scanned_items.size(); i++) {
            String file_name = scanned_items[i];
            item.create(file_name, path(file_name));
            item.serialize(buffer);
            items.push_back(item);
        }

        save(buffer);
        return true;
    }
    void save(Buffer buffer) {
        ::DeleteFile(cache_path.c_str());
        buffer.save(cache_path);
        ::SetFileAttributes(cache_path.c_str(), FILE_ATTRIBUTE_HIDDEN);
    }
    void update_max_modified(const String& filename) {
        Time ft = Util::get_modified(path(filename));
        scanned_last_modified = scanned_last_modified < ft ? ft : scanned_last_modified;
    }
    bool is_outdated() {
        if (scanned_last_modified > last_modified || items.size() < 1 || scanned_items.size() + 1 != items.size()) {
            return true;
        }
        for (size_t i = 0; i < scanned_items.size(); i++) if (scanned_items[i] != items[i + 1].name) {
            return true;
        }
        return false;
    }
};

/**************************************************************************************************
 * The app
 **************************************************************************************************/
struct App {

    App(Cache* c) : cache(c), window(0) { }

    bool init() {
        // Create window
	    Util::kill_other_stackies();
	    WNDCLASS wc = { 0 };
	    wc.style         = CS_HREDRAW | CS_VREDRAW;
	    wc.lpfnWndProc   = window_proc;
	    wc.hInstance     = ::GetModuleHandle(0);
	    wc.lpszClassName = STACKY_WINDOW_NAME;
	    if (!::RegisterClass(&wc)) {
            return false;
        }
	    window = ::CreateWindow(STACKY_WINDOW_NAME, STACKY_WINDOW_NAME, WS_OVERLAPPEDWINDOW, 0, 0, 0, 0, 0, 0, ::GetModuleHandle(0), 0);
        ::SetWindowLongPtr(window, GWLP_USERDATA, (LONG_PTR)cache);

        // Create window
        HMENU menu = ::CreatePopupMenu();
        int menu_pos = 0;
        int command_offset = 0;
        if (cache->was_rebuilt) {
            add_separator(menu, L"Stack cache rebuilt!");
            menu_pos++;
        }
        for (size_t i = 1; i < cache->items.size(); i++) {
            Cache::Item& item = cache->items[i];
            add_item(menu, menu_pos++, WM_MENU_ITEM + command_offset++, item);
        }
        show(menu);
        return true;
    }
    void run() {
	    for (MSG msg; ::GetMessage(&msg, 0, 0, 0); ) {
		    ::TranslateMessage(&msg);
		    ::DispatchMessage(&msg);
	    }
    }

private:
    HWND    window;
    Cache*  cache;

    bool add_separator(HMENU hMenu, const String& text) {
        return AppendMenu(hMenu, text.empty() ? MF_SEPARATOR : MF_GRAYED, 0, text.c_str()) == TRUE;
    }
    bool add_item(HMENU hMenu, int menu_pos, int command, const Cache::Item& item) {
        MENUITEMINFO mii = { sizeof(MENUITEMINFO) };
        mii.fMask = MIIM_BITMAP;
        mii.hbmpItem = item.bmp.hBmp;
        ::AppendMenu(hMenu, MF_STRING, command, Util::rtrim(item.name, L".lnk").c_str());
        return SUCCEEDED(::SetMenuItemInfo(hMenu, menu_pos, TRUE, &mii));
    }
    static const Char* describe_shell_error(INT_PTR shell_result) {
        switch (shell_result) {
            case 0:                     return L"Windows ran out of memory or resources.";
            case ERROR_FILE_NOT_FOUND:  return L"The selected item was not found.";
            case ERROR_PATH_NOT_FOUND:  return L"The selected path was not found.";
            case ERROR_BAD_FORMAT:      return L"The target is not a valid executable.";
            case SE_ERR_ACCESSDENIED:   return L"Windows denied access to the selected item.";
            case SE_ERR_ASSOCINCOMPLETE:return L"The file association for this item is incomplete.";
            case SE_ERR_DDEBUSY:        return L"The target application is busy.";
            case SE_ERR_DDEFAIL:        return L"The target application did not respond to the request.";
            case SE_ERR_DDETIMEOUT:     return L"The target application timed out.";
            case SE_ERR_DLLNOTFOUND:    return L"A required DLL was not found.";
            case SE_ERR_NOASSOC:        return L"Windows could not find an app association for the selected item.";
            case SE_ERR_OOM:            return L"Windows ran out of memory while opening the item.";
            case SE_ERR_SHARE:          return L"A sharing violation prevented the item from opening.";
            default:                    return L"Windows could not open the selected item.";
        }
    }
    static bool resolve_shortcut(const String& shortcut_path, String& resolved_path, String& arguments, String& working_dir) {
        resolved_path.clear();
        arguments.clear();
        working_dir.clear();

        HRESULT init_hr = ::CoInitialize(0);
        bool should_uninit = SUCCEEDED(init_hr);
        IShellLinkW* shell_link = 0;
        IPersistFile* persist_file = 0;
        bool success = false;
        WCHAR target_buf[MAX_PATH] = { 0 };
        WCHAR args_buf[1024] = { 0 };
        WCHAR workdir_buf[MAX_PATH] = { 0 };
        WIN32_FIND_DATA find_data = { 0 };

        if (SUCCEEDED(::CoCreateInstance(CLSID_ShellLink, 0, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&shell_link)) &&
            SUCCEEDED(shell_link->QueryInterface(IID_IPersistFile, (void**)&persist_file)) &&
            SUCCEEDED(persist_file->Load(shortcut_path.c_str(), STGM_READ)) &&
            SUCCEEDED(shell_link->GetPath(target_buf, MAX_PATH, &find_data, SLGP_RAWPATH))) {
            shell_link->GetArguments(args_buf, sizeof(args_buf) / sizeof(args_buf[0]));
            shell_link->GetWorkingDirectory(workdir_buf, sizeof(workdir_buf) / sizeof(workdir_buf[0]));
            resolved_path = target_buf;
            arguments = args_buf;
            working_dir = workdir_buf;
            success = !resolved_path.empty();
        }

        if (persist_file) {
            persist_file->Release();
        }
        if (shell_link) {
            shell_link->Release();
        }
        if (should_uninit) {
            ::CoUninitialize();
        }
        return success;
    }
    static void repair_resolved_target_path(String& resolved_path) {
        if (resolved_path.empty() || ::GetFileAttributes(resolved_path.c_str()) != INVALID_FILE_ATTRIBUTES) {
            return;
        }

        String program_files_x86, program_w6432;
        if (!Util::get_env_string(L"ProgramFiles(x86)", program_files_x86) ||
            !Util::get_env_string(L"ProgramW6432", program_w6432) ||
            !Util::has_case_insensitive_prefix(resolved_path, program_files_x86)) {
            return;
        }

        String candidate = program_w6432 + resolved_path.substr(program_files_x86.size());
        if (::GetFileAttributes(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
            resolved_path = candidate;
        }
    }
    static bool launch_path(const String& target_path) {
        String launch_target = target_path;
        String launch_args;
        String launch_working_dir;
        String display_path = Util::escape_mnemonics(target_path);

        if (Util::has_case_insensitive_suffix(target_path, L".lnk")) {
            if (!resolve_shortcut(target_path, launch_target, launch_args, launch_working_dir)) {
                Util::msgt(L"Stacky", L"Failed to resolve shortcut:\n%s", display_path.c_str());
                return false;
            }
            repair_resolved_target_path(launch_target);
        }

        DWORD attrs = ::GetFileAttributes(launch_target.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) {
            Util::msgt(
                L"Stacky",
                L"Resolved target does not exist for:\n%s\n\nResolved target:\n%s",
                display_path.c_str(),
                Util::escape_mnemonics(launch_target).c_str()
            );
            return false;
        }

        INT_PTR shell_result = (INT_PTR)::ShellExecute(
            0,
            0,
            launch_target.c_str(),
            launch_args.empty() ? 0 : launch_args.c_str(),
            launch_working_dir.empty() ? 0 : launch_working_dir.c_str(),
            SW_NORMAL
        );
        if (shell_result <= 32) {
            Util::msgt(
                L"Stacky",
                L"Failed to open:\n%s\n\nResolved target:\n%s\n\nShell error %ld: %s",
                display_path.c_str(),
                Util::escape_mnemonics(launch_target).c_str(),
                (long)shell_result,
                describe_shell_error(shell_result)
            );
            return false;
        }
        return true;
    }
    void show(HMENU menu) {
        RECT rWorkArea;
        POINT pos;
        ::SystemParametersInfo(SPI_GETWORKAREA, 0, &rWorkArea, 0);
        ::GetCursorPos(&pos);

        LONG pos_x = pos.x, pos_y = pos.y;
        if (pos_x < rWorkArea.left)         pos_x = rWorkArea.left - 1;
        else if (pos_x > rWorkArea.right)   pos_x = rWorkArea.right - 1;
        if (pos_y < rWorkArea.top)          pos_y = rWorkArea.top - 1;
        else if (pos_y > rWorkArea.bottom)  pos_y = rWorkArea.bottom - 1;

	    ::SetForegroundWindow(window);
	    ::TrackPopupMenuEx(menu, ::GetSystemMetrics(SM_MENUDROPALIGNMENT) | TPM_LEFTBUTTON, pos_x, pos_y, window, 0);
    }
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
	    switch (msg) {
		    case WM_COMMAND: {
                Cache* cache = (Cache*)::GetWindowLongPtr(hwnd, GWLP_USERDATA);
                UINT command = LOWORD(wparam);
                int item_idx = -1;
                if (command == WM_OPEN_TARGET_FOLDER) {
                    item_idx = 0;
                }
                else if (command >= WM_MENU_ITEM) {
                    item_idx = (int)(command - WM_MENU_ITEM) + 1;
                }
                if (item_idx >= 0 && item_idx < (int)cache->items.size()) {
                    String cmd = cache->path(item_idx == 0 ? L"" : cache->items[item_idx].name);
                    launch_path(cmd);
                }
            }
		    case WM_EXITMENULOOP:
                // WM_EXITMENULOOP is sent before WM_COMMAND, so the app termination has to be delayed.
                // This also allows to wait for the possible UAC prompt.
                ::SetTimer(hwnd, 0, APP_EXIT_DELAY, 0);
			    break;
            case WM_TIMER:
		        ::PostQuitMessage(0);
		        ::DestroyWindow(hwnd);
                break;
	    }
	    return ::DefWindowProc(hwnd, msg, wparam, lparam);
    }
};

/**************************************************************************************************
 * App entry point
 **************************************************************************************************/
int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPTSTR cmd_line, int) {
    String  stack_path, opts;
    int     cmd_line_error = Util::parse_cmd_line(cmd_line, stack_path, opts);
    String  err_title = String(L"Stacky v") + STACKY_VERSION_STR + L": ";
    String  err_msg = L"Path: " + stack_path;

    Cache   cache(stack_path);
    App     app(&cache);

    if (cmd_line_error == ERR_PATH_MISSING)         Util::msgt(err_title + L"Parameter missing", L"Pass path to the stack folder in the command line, for ex.: \n\n        stacky.exe D:\\Projects");
    else if (cmd_line_error == ERR_PATH_INVALID)    Util::msgt(err_title + L"Invalid parameter", (L"Path: " + stack_path + L" is not a valid directory").c_str());
    else if (!cache.scan())                         Util::msgt(err_title + L"Invalid path", err_msg.c_str());
    else if (!cache.load())                         Util::msgt(err_title + L"Failed to load stack cache", err_msg.c_str());
    else if (!app.init())                           Util::msgt(err_title + L"App init failed", err_msg.c_str());
	else                                            app.run();
	return 0;
}
