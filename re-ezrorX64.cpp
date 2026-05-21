#include <Windows.h>
#include <ShlObj.h>
#include <shobjidl.h>
#include <vector>
#include <string>
#include <ctime>
#include <algorithm>
#include <Shlwapi.h>
#include <stack>
#include <cwchar>
#include <cstring>
#include <fstream>
#include <map>
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#ifndef WINVER
#define WINVER 0x0501
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
using namespace std;
struct FEMTMapping {
    wstring appPath;
    wstring paramsTemplate;
};
map<wstring, FEMTMapping> g_femtMappings;
void LoadFEMTConfig();
struct RecycleItem {
    wstring originalPath;
    wstring storagePath;
    wstring displayName;
    FILETIME deleteTime;
    bool isFolder;
};
struct FileItem {
    wstring fullPath;
    wstring displayName;
    HICON hLargeIcon;
    HICON hSmallIcon;
    bool isSpecialItem;
    bool isFolder;
    int specialType;
    FileItem() : hLargeIcon(nullptr), hSmallIcon(nullptr), isSpecialItem(false), isFolder(false), specialType(0) {}
};
vector<RecycleItem> g_recycleBinItems;
wstring g_mainDir;
wstring g_recycleStorageDir;
vector<FileItem> g_mainFileItems;
int g_iconSize = 32;
int g_itemWidth = 120;
int g_itemHeight = 80;
UINT_PTR g_mainTimerID = 0;
COLORREF g_bgColor = RGB(245, 245, 245);
static COLORREF acrCustClr[16];
HINSTANCE g_hInstance;
int g_mainScrollOffset = 0;
size_t g_mainSelectedIndex = -1;
struct FolderWndState {
    vector<FileItem> items;
    wstring currentPath;
    stack<wstring> pathHistory;
    UINT_PTR timerID;
    int scrollOffset;
    size_t selectedIndex;
    FolderWndState() : timerID(0), scrollOffset(0), selectedIndex(-1) {}
};
const WCHAR g_mainWndClass[] = L"PE_Desktop_MainWnd";
const WCHAR g_folderWndClass[] = L"PE_Desktop_FolderWnd";
const WCHAR g_recycleStorageFolderName[] = L"RecycleBin_Storage";
wstring GetParentPath(const wstring& currentPath);
bool IsSystemSpecialItem(const wstring& path, const wstring& fileName) {
    const wstring specialNames[] = {
        L"$Recycle.Bin", L"System Volume Information", L"Documents and Settings",
        L"$Windows.~BT", L"$WinREAgent", L"pagefile.sys", L"hiberfil.sys",
        L"swapfile.sys", L"$Extend", L"Recovery", L"ProgramData", L"AppData",
        L"Local Settings", L"Temp", L"TEMP", L"FEMT.CF", L"RecycleBin_Storage"
    };
    for (const auto& name : specialNames) {
        if (_wcsicmp(fileName.c_str(), name.c_str()) == 0) {
            return true;
        }
    }
    wstring lowerPath = path;
    transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::towlower);
    if (lowerPath.find(L"system volume information") != wstring::npos ||
        lowerPath.find(L"documents and settings") != wstring::npos ||
        lowerPath.find(L"$recycle.bin") != wstring::npos) {
        return true;
    }
    return false;
}
HICON GetItemIcon(const wstring& path, bool isFolder, bool isLargeIcon, int specialType = 0) {
    if (specialType == 1) {
        SHFILEINFOW shFileInfo = {0};
        SHGetFileInfoW((LPCWSTR)&CLSID_MyComputer, FILE_ATTRIBUTE_DIRECTORY, &shFileInfo, sizeof(SHFILEINFOW),
                       SHGFI_ICON | (isLargeIcon ? SHGFI_LARGEICON : SHGFI_SMALLICON) | SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX);
        return shFileInfo.hIcon;
    } else if (specialType == 2) {
        SHFILEINFOW shFileInfo = {0};
        SHGetFileInfoW((LPCWSTR)&CLSID_RecycleBin, FILE_ATTRIBUTE_DIRECTORY, &shFileInfo, sizeof(SHFILEINFOW),
                       SHGFI_ICON | (isLargeIcon ? SHGFI_LARGEICON : SHGFI_SMALLICON) | SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX);
        return shFileInfo.hIcon;
    }
    SHFILEINFOW shFileInfo = {0};
    DWORD flags = SHGFI_ICON | (isLargeIcon ? SHGFI_LARGEICON : SHGFI_SMALLICON);
    DWORD fileAttr = isFolder ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    if (SHGetFileInfoW(path.c_str(), fileAttr, &shFileInfo, sizeof(SHFILEINFOW), flags)) {
        return shFileInfo.hIcon;
    }
    flags |= SHGFI_USEFILEATTRIBUTES;
    SHGetFileInfoW(path.c_str(), fileAttr, &shFileInfo, sizeof(SHFILEINFOW), flags);
    return shFileInfo.hIcon;
}
bool CreateRecycleStorageDir() {
    WCHAR szExePath[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, szExePath, MAX_PATH);
    WCHAR* pLastSlash = wcsrchr(szExePath, L'\\');
    if (!pLastSlash) return false;
    wstring exeDir = wstring(szExePath, pLastSlash - szExePath + 1);
    g_recycleStorageDir = exeDir + g_recycleStorageFolderName;
    DWORD attr = GetFileAttributesW(g_recycleStorageDir.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        return true;
    }
    if (CreateDirectoryW(g_recycleStorageDir.c_str(), nullptr)) {
        SetFileAttributesW(g_recycleStorageDir.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_DIRECTORY);
        return true;
    }
    MessageBoxW(nullptr, L"\u56de\u6536\u7ad9\u5b58\u50a8\u76ee\u5f55\u521b\u5efa\u5931\u8d25\uff01\u65e0\u6cd5\u4f7f\u7528\u56de\u6536\u7ad9\u529f\u80fd\u3002", L"\u9519\u8bef", MB_ICONERROR);
    return false;
}
wstring GenerateUniqueFileName(const wstring& baseName, const wstring& targetDir, bool isFolder) {
    wstring uniqueName = baseName;
    wstring fullPath = targetDir + L"\\" + uniqueName;
    int suffix = 1;
    while (GetFileAttributesW(fullPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        size_t dotPos = baseName.find_last_of(L".");
        if (dotPos != wstring::npos && !isFolder) {
            uniqueName = baseName.substr(0, dotPos) + L"_" + to_wstring(suffix) + baseName.substr(dotPos);
        } else {
            uniqueName = baseName + L"_" + to_wstring(suffix);
        }
        fullPath = targetDir + L"\\" + uniqueName;
        suffix++;
    }
    return uniqueName;
}
bool DeleteFolderRecursive(const wstring& folderPath) {
    wstring searchPath = folderPath + L"\\*.*";
    WIN32_FIND_DATAW findData = {0};
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        return RemoveDirectoryW(folderPath.c_str()) != 0;
    }
    do {
        if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0) continue;
        wstring itemPath = folderPath + L"\\" + findData.cFileName;
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!DeleteFolderRecursive(itemPath)) {
                FindClose(hFind);
                return false;
            }
        } else {
            if (!DeleteFileW(itemPath.c_str())) {
                FindClose(hFind);
                return false;
            }
        }
    } while (FindNextFileW(hFind, &findData));
    FindClose(hFind);
    return RemoveDirectoryW(folderPath.c_str()) != 0;
}
bool DeleteItemPermanently(const wstring& path, bool isFolder) {
    wstring fileName = path.substr(path.find_last_of(L"\\") + 1);
    int result = MessageBoxW(nullptr, (L"\u786e\u5b9a\u8981\u6c38\u4e45\u5220\u9664 \"" + fileName + L"\" \u5417\uff1f\n\u5220\u9664\u540e\u65e0\u6cd5\u6062\u590d\uff01").c_str(),
                             L"\u8b66\u544a\uff1a\u6c38\u4e45\u5220\u9664", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    if (result != IDYES) return false;
    if (isFolder) {
        return DeleteFolderRecursive(path);
    } else {
        return DeleteFileW(path.c_str()) != 0;
    }
}
bool ClearRecycleBin() {
    if (g_recycleBinItems.empty()) {
        MessageBoxW(nullptr, L"\u56de\u6536\u7ad9\u5df2\u4e3a\u7a7a\uff01", L"\u63d0\u793a", MB_ICONINFORMATION);
        return true;
    }
    int result = MessageBoxW(nullptr, L"\u786e\u5b9a\u8981\u6e05\u7a7a\u56de\u6536\u7ad9\u5417\uff1f\n\u6240\u6709\u9879\u76ee\u5c06\u88ab\u6c38\u4e45\u5220\u9664\uff0c\u65e0\u6cd5\u6062\u590d\uff01",
                             L"\u8b66\u544a\uff1a\u6279\u91cf\u6e05\u7a7a\u56de\u6536\u7ad9", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    if (result != IDYES) return false;
    for (auto it = std::begin(g_recycleBinItems); it != std::end(g_recycleBinItems); ++it) {
        DeleteItemPermanently(it->storagePath, it->isFolder);
    }
    g_recycleBinItems.clear();
    MessageBoxW(nullptr, L"\u56de\u6536\u7ad9\u5df2\u6e05\u7a7a\uff0c\u6240\u6709\u9879\u76ee\u5df2\u6c38\u4e45\u5220\u9664\u3002", L"\u63d0\u793a", MB_ICONINFORMATION);
    return true;
}
bool ResolveShortcut(const wstring& lnkPath, wstring& outTargetPath)
{
    outTargetPath.clear();
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr))
        return false;
    IShellLink* pShellLink = NULL;
    hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                          IID_IShellLink, (LPVOID*)&pShellLink);
    if (FAILED(hr))
    {
        CoUninitialize();
        return false;
    }
    IPersistFile* pPersistFile = NULL;
    hr = pShellLink->QueryInterface(IID_IPersistFile, (LPVOID*)&pPersistFile);
    if (SUCCEEDED(hr))
    {
        hr = pPersistFile->Load(lnkPath.c_str(), STGM_READ);
        if (SUCCEEDED(hr))
        {
            WCHAR szTarget[MAX_PATH] = {0};
            pShellLink->GetPath(szTarget, MAX_PATH, NULL, SLGP_UNCPRIORITY);
            outTargetPath = szTarget;
        }
        pPersistFile->Release();
    }
    pShellLink->Release();
    CoUninitialize();
    return !outTargetPath.empty();
}
bool RestoreRecycleItem(size_t itemIndex) {
    if (itemIndex >= g_recycleBinItems.size()) return false;
    RecycleItem& item = g_recycleBinItems[itemIndex];
    wstring originalDir = GetParentPath(item.originalPath);
    DWORD dirAttr = GetFileAttributesW(originalDir.c_str());
    if (dirAttr == INVALID_FILE_ATTRIBUTES || !(dirAttr & FILE_ATTRIBUTE_DIRECTORY)) {
        int res = MessageBoxW(nullptr,
            (L"\u539f\u59cb\u76ee\u5f55\u4e0d\u5b58\u5728\uff1a\n" + originalDir + L"\n\u662f\u5426\u81ea\u52a8\u521b\u5efa\u8be5\u76ee\u5f55\u5e76\u6062\u590d\u6587\u4ef6\uff1f").c_str(),
            L"\u6062\u590d\u63d0\u793a", MB_YESNO | MB_ICONQUESTION);
        if (res != IDYES) return false;
        WCHAR tempDir[MAX_PATH] = {0};
        wcscpy(tempDir, originalDir.c_str());
        WCHAR* p = wcschr(tempDir + 1, L'\\');
        while (p) {
            *p = L'\0';
            CreateDirectoryW(tempDir, nullptr);
            *p = L'\\';
            p = wcschr(p + 1, L'\\');
        }
        CreateDirectoryW(tempDir, nullptr);
    }
    wstring targetPath = item.originalPath;
    if (GetFileAttributesW(targetPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        wstring fileName = item.displayName;
        wstring uniqueFileName = GenerateUniqueFileName(fileName, originalDir, item.isFolder);
        targetPath = originalDir + L"\\" + uniqueFileName;
        MessageBoxW(nullptr,
            (L"\u539f\u59cb\u8def\u5f84\u5b58\u5728\u540c\u540d\u6587\u4ef6\uff0c\u5c06\u6062\u590d\u4e3a\uff1a\n" + uniqueFileName).c_str(),
            L"\u91cd\u540d\u63d0\u793a", MB_OK | MB_ICONINFORMATION);
    }
    BOOL restoreResult = FALSE;
    if (item.isFolder) {
        restoreResult = MoveFileW(item.storagePath.c_str(), targetPath.c_str());
    } else {
        restoreResult = MoveFileW(item.storagePath.c_str(), targetPath.c_str());
    }
    if (!restoreResult) {
        DWORD errorCode = GetLastError();
        wstring errorMsg = L"\u6062\u590d\u5931\u8d25\uff0c\u9519\u8bef\u7801\uff1a" + to_wstring(errorCode) + L"\n\u6587\u4ef6\uff1a" + item.displayName;
        MessageBoxW(nullptr, errorMsg.c_str(), L"\u9519\u8bef", MB_ICONERROR);
        return false;
    }
    g_recycleBinItems.erase(g_recycleBinItems.begin() + itemIndex);
    MessageBoxW(nullptr,
        (L"\"" + item.displayName + L"\" \u5df2\u6210\u529f\u6062\u590d\u3002").c_str(),
        L"\u6062\u590d\u6210\u529f", MB_ICONINFORMATION);
    return true;
}
bool DeleteSingleRecycleItem(size_t itemIndex) {
    if (itemIndex >= g_recycleBinItems.size()) return false;
    RecycleItem& item = g_recycleBinItems[itemIndex];
    bool deleteResult = DeleteItemPermanently(item.storagePath, item.isFolder);
    if (!deleteResult) {
        MessageBoxW(nullptr,
            (L"\u6c38\u4e45\u5220\u9664 \"" + item.displayName + L"\" \u5931\u8d25\u3002").c_str(),
            L"\u5220\u9664\u5931\u8d25", MB_ICONERROR);
        return false;
    }
    g_recycleBinItems.erase(g_recycleBinItems.begin() + itemIndex);
    return true;
}
bool MoveToRecycleBin(const wstring& originalPath) {
    if (!CreateRecycleStorageDir()) return false;
    DWORD attr = GetFileAttributesW(originalPath.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(nullptr, L"\u6587\u4ef6/\u6587\u4ef6\u5939\u4e0d\u5b58\u5728\uff01", L"\u9519\u8bef", MB_ICONERROR);
        return false;
    }
    bool isFolder = (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
    wstring fileName = originalPath.substr(originalPath.find_last_of(L"\\") + 1);
    wstring uniqueName = GenerateUniqueFileName(fileName, g_recycleStorageDir, isFolder);
    wstring storagePath = g_recycleStorageDir + L"\\" + uniqueName;
    BOOL moveResult = FALSE;
    if (isFolder) {
        moveResult = MoveFileW(originalPath.c_str(), storagePath.c_str());
    } else {
        moveResult = MoveFileW(originalPath.c_str(), storagePath.c_str());
    }
    if (!moveResult) {
        MessageBoxW(nullptr, L"\u79fb\u52a8\u5230\u56de\u6536\u7ad9\u5931\u8d25\uff01\u53ef\u80fd\u6587\u4ef6\u6b63\u5728\u88ab\u4f7f\u7528\u3002", L"\u9519\u8bef", MB_ICONERROR);
        return false;
    }
    RecycleItem recycleItem;
    recycleItem.originalPath = originalPath;
    recycleItem.storagePath = storagePath;
    recycleItem.displayName = fileName;
    GetFileTime((HANDLE)-1, nullptr, nullptr, &recycleItem.deleteTime);
    recycleItem.isFolder = isFolder;
    g_recycleBinItems.push_back(recycleItem);
    MessageBoxW(nullptr, (L"\"" + fileName + L"\" \u5df2\u79fb\u5230\u56de\u6536\u7ad9\u3002").c_str(), L"\u63d0\u793a", MB_ICONINFORMATION);
    return true;
}
void EnumDirItems(const wstring& dir, vector<FileItem>& itemList) {
    for (auto& item : itemList) {
        if (item.hLargeIcon) DestroyIcon(item.hLargeIcon);
        if (item.hSmallIcon) DestroyIcon(item.hSmallIcon);
    }
    itemList.clear();
    wstring fixedDir = dir;
    if (fixedDir.length() == 2 && fixedDir[1] == L':') {
        fixedDir += L"\\";
    } else if (!fixedDir.empty() && fixedDir.back() != L'\\') {
        fixedDir += L"\\";
    }
    DWORD attr = GetFileAttributesW(fixedDir.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        MessageBoxW(nullptr, (L"\u8def\u5f84\u65e0\u6548\u6216\u4e0d\u662f\u6587\u4ef6\u5939\uff1a" + fixedDir).c_str(), L"\u9519\u8bef", MB_ICONERROR);
        return;
    }
    wstring searchPath = fixedDir + L"*.*";
    WIN32_FIND_DATAW findData = {0};
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        DWORD errorCode = GetLastError();
        if (errorCode != 5) {
            wstring errorMsg = L"\u679a\u4e3e\u6587\u4ef6\u5939\u5931\u8d25\uff0c\u9519\u8bef\u7801\uff1a" + to_wstring(errorCode) + L"\n\u8def\u5f84\uff1a" + fixedDir;
            MessageBoxW(nullptr, errorMsg.c_str(), L"\u9519\u8bef", MB_ICONERROR);
        }
        return;
    }
    do {
        if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0) continue;
        
        if (IsSystemSpecialItem(fixedDir + findData.cFileName, findData.cFileName)) {
            continue;
        }
        FileItem item;
        item.displayName = findData.cFileName;
        item.fullPath = fixedDir + findData.cFileName;
        item.isSpecialItem = false;
        item.isFolder = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        item.specialType = 0;
        item.hLargeIcon = GetItemIcon(item.fullPath, item.isFolder, true);
        item.hSmallIcon = GetItemIcon(item.fullPath, item.isFolder, false);
        itemList.push_back(item);
    } while (FindNextFileW(hFind, &findData));
    FindClose(hFind);
}
void EnumMyComputerItems(vector<FileItem>& itemList) {
    for (auto& item : itemList) {
        if (item.hLargeIcon) DestroyIcon(item.hLargeIcon);
        if (item.hSmallIcon) DestroyIcon(item.hSmallIcon);
    }
    itemList.clear();
    WCHAR szDrives[MAX_PATH] = {0};
    GetLogicalDriveStringsW(MAX_PATH, szDrives);
    WCHAR* pDrive = szDrives;
    while (*pDrive) {
        FileItem item;
        item.displayName = pDrive;
        item.fullPath = pDrive;
        item.isSpecialItem = false;
        item.isFolder = true;
        item.specialType = 0;
        item.hLargeIcon = GetItemIcon(item.fullPath, true, true);
        item.hSmallIcon = GetItemIcon(item.fullPath, true, false);
        itemList.push_back(item);
        pDrive += wcslen(pDrive) + 1;
    }
}
void EnumRecycleBinItems(vector<FileItem>& itemList) {
    for (auto& item : itemList) {
        if (item.hLargeIcon) DestroyIcon(item.hLargeIcon);
        if (item.hSmallIcon) DestroyIcon(item.hSmallIcon);
    }
    itemList.clear();
    if (g_recycleBinItems.empty()) {
        FileItem emptyItem;
        emptyItem.displayName = L"\u56de\u6536\u7ad9\u4e3a\u7a7a";
        emptyItem.fullPath = L"";
        emptyItem.isSpecialItem = false;
        emptyItem.isFolder = false;
        emptyItem.specialType = 0;
        emptyItem.hLargeIcon = LoadIconW(nullptr, IDI_INFORMATION);
        emptyItem.hSmallIcon = LoadIconW(nullptr, IDI_INFORMATION);
        itemList.push_back(emptyItem);
        return;
    }
    for (auto it = std::begin(g_recycleBinItems); it != std::end(g_recycleBinItems); ++it) {
        FileItem item;
        item.displayName = it->displayName;
        item.fullPath = it->storagePath;
        item.isSpecialItem = false;
        item.isFolder = it->isFolder;
        item.specialType = 0;
        item.hLargeIcon = GetItemIcon(item.fullPath, item.isFolder, true);
        item.hSmallIcon = GetItemIcon(item.fullPath, item.isFolder, false);
        itemList.push_back(item);
    }
}
void AddSpecialItems(vector<FileItem>& itemList) {
    FileItem myComputerItem;
    myComputerItem.displayName = L"\u6b64\u7535\u8111";
    myComputerItem.fullPath = L"::MyComputer::";
    myComputerItem.isSpecialItem = true;
    myComputerItem.isFolder = false;
    myComputerItem.specialType = 1;
    myComputerItem.hLargeIcon = GetItemIcon(L"", true, true, 1);
    myComputerItem.hSmallIcon = GetItemIcon(L"", true, false, 1);
    itemList.insert(itemList.begin(), myComputerItem);
    FileItem recycleBinItem;
    recycleBinItem.displayName = L"\u56de\u6536\u7ad9";
    recycleBinItem.fullPath = L"::RecycleBin::";
    recycleBinItem.isSpecialItem = true;
    recycleBinItem.isFolder = false;
    recycleBinItem.specialType = 2;
    recycleBinItem.hLargeIcon = GetItemIcon(L"", true, true, 2);
    recycleBinItem.hSmallIcon = GetItemIcon(L"", true, false, 2);
    itemList.insert(itemList.begin() + 1, recycleBinItem);
}
class CRecycleDropTarget : public IDropTarget {
private:
    LONG m_refCount;
public:
    CRecycleDropTarget() : m_refCount(1) {}
    ~CRecycleDropTarget() {}
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) {
        if (riid == IID_IUnknown || riid == IID_IDropTarget) {
            *ppvObject = this;
            AddRef();
            return S_OK;
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() {
        return InterlockedIncrement(&m_refCount);
    }
    STDMETHODIMP_(ULONG) Release() {
        LONG refCount = InterlockedDecrement(&m_refCount);
        if (refCount == 0) delete this;
        return refCount;
    }
    STDMETHODIMP DragEnter(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) {
        *pdwEffect = DROPEFFECT_MOVE;
        return S_OK;
    }
    STDMETHODIMP DragOver(DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) {
        *pdwEffect = DROPEFFECT_MOVE;
        return S_OK;
    }
    STDMETHODIMP DragLeave() {
        return S_OK;
    }
    STDMETHODIMP Drop(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) {
        FORMATETC fmt = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM stgMedium = {0};
        if (FAILED(pDataObj->GetData(&fmt, &stgMedium))) {
            return E_FAIL;
        }
        HDROP hDrop = (HDROP)GlobalLock(stgMedium.hGlobal);
        if (!hDrop) {
            ReleaseStgMedium(&stgMedium);
            return E_FAIL;
        }
        UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < fileCount; ++i) {
            WCHAR szPath[MAX_PATH] = {0};
            DragQueryFileW(hDrop, i, szPath, MAX_PATH);
            MoveToRecycleBin(szPath);
        }
        GlobalUnlock(hDrop);
        ReleaseStgMedium(&stgMedium);
        *pdwEffect = DROPEFFECT_MOVE;
        return S_OK;
    }
};
IDropTarget* g_pDropTarget = nullptr;
HWND g_hDropTargetWnd = nullptr;
void RegisterDropTarget(HWND hWnd) {
    if (!g_pDropTarget) {
        g_pDropTarget = new CRecycleDropTarget();
    }
    g_hDropTargetWnd = hWnd;
    RegisterDragDrop(hWnd, g_pDropTarget);
}
void RevokeDropTarget() {
    if (g_pDropTarget && g_hDropTargetWnd) {
        RevokeDragDrop(g_hDropTargetWnd);
        g_pDropTarget->Release();
        g_pDropTarget = nullptr;
        g_hDropTargetWnd = nullptr;
    }
}
int CalcItemsTotalHeight(const vector<FileItem>& items, int clientWidth) {
    if (items.empty()) return 0;
    int cols = (clientWidth - 40) / (g_itemWidth + 10);
    if (cols <= 0) cols = 1;
    int rows = (items.size() + cols - 1) / cols;
    return rows * (g_itemHeight + 20) + 20 + 40;
}
bool IsFileExecutableOrOpenable(const wstring& filePath) {
    const wstring validExts[] = {
        L".exe", L".lnk", L".txt", L".doc", L".docx", L".xls", L".xlsx",
        L".ppt", L".pptx", L".pdf", L".jpg", L".png", L".gif", L".mp3",
        L".mp4", L".avi", L".zip", L".rar", L".7z", L".url", L".bat",
        L".cmd", L".html", L".htm", L".xml", L".json"
    };
    wstring ext = PathFindExtensionW(filePath.c_str());
    if (ext.empty()) return false;
    transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    for (const auto& e : validExts) {
        if (ext == e) {
            return true;
        }
    }
    return false;
}
size_t GetClickedItemIndex(const vector<FileItem>& items, const POINT& clickPt, int scrollOffset, int clientWidth) {
    if (items.empty()) return -1;
    int cols = (clientWidth - 40) / (g_itemWidth + 10);
    if (cols <= 0) cols = 1;
    POINT realPt = clickPt;
    realPt.y += scrollOffset;
    int x = 20, y = 20;
    for (size_t i = 0; i < items.size(); ++i) {
        RECT itemRect = {
            x, y,
            x + g_itemWidth,
            y + g_itemHeight
        };
        if (PtInRect(&itemRect, realPt)) {
            return i;
        }
        x += g_itemWidth + 10;
        if (x + g_itemWidth > clientWidth - 20) {
            x = 20;
            y += g_itemHeight + 20;
        }
    }
    return -1;
}
void AdjustPopupMenuPos(HWND hWnd, POINT& pt) {
    RECT wndRect;
    GetWindowRect(hWnd, &wndRect);
    
    int menuWidth = 200;
    int menuHeight = 150;
    
    if (pt.x + menuWidth > wndRect.right) {
        pt.x = wndRect.right - menuWidth - 10;
    }
    if (pt.y + menuHeight > wndRect.bottom) {
        pt.y = pt.y - menuHeight - 10;
    }
    
    if (pt.x < wndRect.left + 10) pt.x = wndRect.left + 10;
    if (pt.y < wndRect.top + 10) pt.y = wndRect.top + 10;
}
wstring GetParentPath(const wstring& currentPath) {
    wstring parentPath = currentPath;
    if (!parentPath.empty() && parentPath.back() == L'\\') {
        parentPath.pop_back();
    }
    size_t lastSlash = parentPath.find_last_of(L'\\');
    if (lastSlash == wstring::npos) {
        return currentPath;
    }
    parentPath = parentPath.substr(0, lastSlash + 1);
    return parentPath;
}
HWND CreateFolderWindow(HWND hParent, const wstring& title, const wstring& path) {
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int wndWidth = 800;
    int wndHeight = 600;
    int x = (screenWidth - wndWidth) / 2;
    int y = (screenHeight - wndHeight) / 2;
    HWND hFolderWnd = CreateWindowExW(
        0, g_folderWndClass, title.c_str(),
        WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_VSCROLL,
        x, y, wndWidth, wndHeight,
        hParent, nullptr, g_hInstance, (LPVOID)path.c_str()
    );
    if (!hFolderWnd) {
        MessageBoxW(nullptr, (L"\u521b\u5efa\u6587\u4ef6\u5939\u7a97\u53e3\u5931\u8d25\uff1a" + path).c_str(), L"\u9519\u8bef", MB_ICONERROR);
    }
    return hFolderWnd;
}
void LoadFEMTConfig() {
    WCHAR szExePath[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, szExePath, MAX_PATH);
    WCHAR* pLastSlash = wcsrchr(szExePath, L'\\');
    if (!pLastSlash) return;
    wstring exeDir(szExePath, pLastSlash - szExePath + 1);
    wstring configPathW = exeDir + L"FEMT.CF";
    HANDLE hFile = CreateFileW(configPathW.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;
    DWORD fileSize = GetFileSize(hFile, nullptr);
    if (fileSize == INVALID_FILE_SIZE || fileSize == 0) {
        CloseHandle(hFile);
        return;
    }
    char* fileBuffer = new char[fileSize + 1];
    DWORD readBytes = 0;
    ReadFile(hFile, fileBuffer, fileSize, &readBytes, nullptr);
    fileBuffer[readBytes] = '\0';
    CloseHandle(hFile);
    int bomOffset = 0;
    if ((unsigned char)fileBuffer[0] == 0xEF && (unsigned char)fileBuffer[1] == 0xBB && (unsigned char)fileBuffer[2] == 0xBF) {
        bomOffset = 3;
    }
    char* utf8Content = fileBuffer + bomOffset;
    int utf8Len = readBytes - bomOffset;
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, utf8Content, utf8Len, nullptr, 0);
    if (wideLen <= 0) {
        delete[] fileBuffer;
        return;
    }
    wstring content(wideLen, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8Content, utf8Len, &content[0], wideLen);
    delete[] fileBuffer;
    size_t lineStart = 0;
    size_t lineEnd = 0;
    size_t contentLen = content.length();
    while (lineStart < contentLen) {
        lineEnd = content.find(L'\n', lineStart);
        if (lineEnd == wstring::npos) lineEnd = contentLen;
        wstring line = content.substr(lineStart, lineEnd - lineStart);
        lineStart = lineEnd + 1;
        line.erase(remove(line.begin(), line.end(), L'\r'), line.end());
        if (line.empty()) continue;
        size_t quotePos = line.find(L'\"');
        if (quotePos == wstring::npos) continue;
        wstring ext = line.substr(0, quotePos);
        size_t startExt = ext.find_first_not_of(L" \t");
        size_t endExt = ext.find_last_not_of(L" \t");
        if (startExt == wstring::npos || endExt == wstring::npos) continue;
        ext = ext.substr(startExt, endExt - startExt + 1);
        transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        wstring cmdPart = line.substr(quotePos + 1);
        size_t endQuotePos = cmdPart.find_last_of(L'\"');
        if (endQuotePos == wstring::npos) continue;
        cmdPart = cmdPart.substr(0, endQuotePos);
        size_t firstSpace = cmdPart.find(L' ');
        FEMTMapping mapping;
        if (firstSpace == wstring::npos) {
            mapping.appPath = cmdPart;
            mapping.paramsTemplate = L"";
        } else {
            mapping.appPath = cmdPart.substr(0, firstSpace);
            mapping.paramsTemplate = cmdPart.substr(firstSpace + 1);
        }
        g_femtMappings[ext] = mapping;
    }
}
LRESULT CALLBACK FolderWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    FolderWndState* pState = (FolderWndState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    switch (uMsg) {
        case WM_CREATE: {
            CREATESTRUCTW* pCreate = (CREATESTRUCTW*)lParam;
            pState = new FolderWndState();
            pState->currentPath = (LPCWSTR)pCreate->lpCreateParams;
            if (!pState->currentPath.empty() && pState->currentPath != L"::MyComputer::" && pState->currentPath != L"::RecycleBin::") {
                pState->pathHistory.push(pState->currentPath);
            }
            if (pState->currentPath == L"::MyComputer::") {
                EnumMyComputerItems(pState->items);
                SetWindowTextW(hWnd, L"\u6b64\u7535\u8111");
            } else if (pState->currentPath == L"::RecycleBin::") {
                EnumRecycleBinItems(pState->items);
                SetWindowTextW(hWnd, L"\u56de\u6536\u7ad9");
            } else {
                EnumDirItems(pState->currentPath, pState->items);
                SetWindowTextW(hWnd, pState->currentPath.c_str());
            }
            pState->timerID = SetTimer(hWnd, 1, 1000, nullptr);
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pState);
            if (pState->currentPath == L"::RecycleBin::") {
                RegisterDropTarget(hWnd);
            }
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rcClient;
            GetClientRect(hWnd, &rcClient);
            HBRUSH hBgBrush = CreateSolidBrush(g_bgColor);
            FillRect(hdc, &rcClient, hBgBrush);
            DeleteObject(hBgBrush);
            int x = 20, y = 20 - pState->scrollOffset;
            for (size_t i = 0; i < pState->items.size(); ++i) {
                const FileItem& item = pState->items[i];
                RECT itemRect = {x, y, x + g_itemWidth, y + g_itemHeight};
                
                if (i == pState->selectedIndex) {
                    HBRUSH hSelBrush = CreateSolidBrush(RGB(200, 220, 255));
                    FillRect(hdc, &itemRect, hSelBrush);
                    DeleteObject(hSelBrush);
                }
                if (item.hLargeIcon) {
                    int iconX = x + (g_itemWidth - g_iconSize) / 2;
                    DrawIconEx(hdc, iconX, y, item.hLargeIcon, g_iconSize, g_iconSize, 0, nullptr, DI_NORMAL);
                }
                RECT textRect = {x, y + g_iconSize + 5, x + g_itemWidth, y + g_itemHeight};
                SetBkMode(hdc, TRANSPARENT);
                DrawTextW(hdc, item.displayName.c_str(), -1, &textRect, DT_CENTER | DT_WORDBREAK | DT_VCENTER);
                x += g_itemWidth + 10;
                if (x + g_itemWidth > rcClient.right - 20) {
                    x = 20;
                    y += g_itemHeight + 20;
                }
            }
            EndPaint(hWnd, &ps);
            return 0;
        }
        case WM_TIMER: {
            InvalidateRect(hWnd, nullptr, TRUE);
            return 0;
        }
        case WM_VSCROLL: {
            RECT rcClient;
            GetClientRect(hWnd, &rcClient);
            int scrollMax = CalcItemsTotalHeight(pState->items, rcClient.right) - rcClient.bottom;
            if (scrollMax < 0) scrollMax = 0;
            switch (LOWORD(wParam)) {
                case SB_LINEUP: pState->scrollOffset -= 20; break;
                case SB_LINEDOWN: pState->scrollOffset += 20; break;
                case SB_PAGEUP: pState->scrollOffset -= rcClient.bottom; break;
                case SB_PAGEDOWN: pState->scrollOffset += rcClient.bottom; break;
                case SB_THUMBPOSITION:
                case SB_THUMBTRACK: pState->scrollOffset = HIWORD(wParam); break;
                case SB_TOP: pState->scrollOffset = 0; break;
                case SB_BOTTOM: pState->scrollOffset = scrollMax; break;
            }
            if (pState->scrollOffset < 0) pState->scrollOffset = 0;
            if (pState->scrollOffset > scrollMax) pState->scrollOffset = scrollMax;
            SCROLLINFO si = {0};
            si.cbSize = sizeof(SCROLLINFO);
            si.fMask = SIF_ALL;
            si.nMin = 0;
            si.nMax = scrollMax;
            si.nPage = rcClient.bottom;
            si.nPos = pState->scrollOffset;
            SetScrollInfo(hWnd, SB_VERT, &si, TRUE);
            InvalidateRect(hWnd, nullptr, TRUE);
            return 0;
        }
        case WM_LBUTTONUP: {
            POINT pt = {LOWORD(lParam), HIWORD(lParam)};
            RECT rcClient;
            GetClientRect(hWnd, &rcClient);
            size_t selectedIndex = GetClickedItemIndex(pState->items, pt, pState->scrollOffset, rcClient.right);
            
            pState->selectedIndex = selectedIndex;
            InvalidateRect(hWnd, nullptr, TRUE);
            return 0;
        }
        case WM_LBUTTONDBLCLK: {
            POINT pt = {LOWORD(lParam), HIWORD(lParam)};
            RECT rcClient;
            GetClientRect(hWnd, &rcClient);
            size_t selectedIndex = GetClickedItemIndex(pState->items, pt, pState->scrollOffset, rcClient.right);
            if (selectedIndex != -1) {
                const FileItem& selectedItem = pState->items[selectedIndex];
                if (selectedItem.isFolder && !selectedItem.fullPath.empty()) {
                    pState->pathHistory.push(pState->currentPath);
                    
                    pState->scrollOffset = 0;
                    pState->currentPath = selectedItem.fullPath;
                    EnumDirItems(pState->currentPath, pState->items);
                    SetWindowTextW(hWnd, pState->currentPath.c_str());
                    pState->selectedIndex = -1;
                    InvalidateRect(hWnd, nullptr, TRUE);
                } else if (selectedItem.isSpecialItem) {
                    CreateFolderWindow(hWnd, selectedItem.displayName, selectedItem.fullPath);
                } else if (!selectedItem.fullPath.empty() && selectedItem.displayName != L"\u56de\u6536\u7ad9\u4e3a\u7a7a") {
                    wstring ext = PathFindExtensionW(selectedItem.fullPath.c_str());
                    if (!ext.empty() && ext[0] == L'.') ext = ext.substr(1);
                    transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
                    auto it = g_femtMappings.find(ext);
                    if (it != g_femtMappings.end()) {
                        wstring params = it->second.paramsTemplate;
                        // 完全删除"-i $.$"整个模板字符串，不做任何替换
                        size_t dashIPos = params.find(L"-i $.$");
                        if (dashIPos != wstring::npos) {
                            params.erase(dashIPos, 6);
                        }
                        // 其他占位符仍然正常处理
                        size_t placeholderPos = params.find(L"$.$");
                        if (placeholderPos != wstring::npos) {
                            params.replace(placeholderPos, 3, selectedItem.fullPath);
                        }
                        SHELLEXECUTEINFOW sei = {0};
                        sei.cbSize = sizeof(SHELLEXECUTEINFOW);
                        sei.lpFile = it->second.appPath.c_str();
                        sei.lpParameters = params.empty() ? nullptr : params.c_str();
                        sei.nShow = SW_SHOWNORMAL;
                        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
                        if (!ShellExecuteExW(&sei)) {
                            DWORD errorCode = GetLastError();
                            wstring errorMsg = L"\u6253\u5f00\u5931\u8d25\uff0c\u9519\u8bef\u7801\uff1a" + to_wstring(errorCode) + L"\n\u8def\u5f84\uff1a" + it->second.appPath;
                            MessageBoxW(NULL, errorMsg.c_str(), L"\u9519\u8bef", MB_ICONERROR);
                        }
                    } else if (IsFileExecutableOrOpenable(selectedItem.fullPath)) {
                        wstring openPath = selectedItem.fullPath;
                        wstring extCheck = PathFindExtensionW(openPath.c_str());
                        transform(extCheck.begin(), extCheck.end(), extCheck.begin(), ::towlower);
                        if (extCheck == L".lnk") {
                            wstring targetPath;
                            if (ResolveShortcut(openPath, targetPath)) {
                                openPath = targetPath;
                            } else {
                                MessageBoxW(NULL, L"\u65e0\u6cd5\u89e3\u6790\u8be5\u5feb\u6377\u65b9\u5f0f\uff08.lnk\uff09", L"\u63d0\u793a", MB_ICONWARNING);
                                return 0;
                            }
                        }
                        SHELLEXECUTEINFOW sei = {0};
                        sei.cbSize = sizeof(SHELLEXECUTEINFOW);
                        sei.lpFile = openPath.c_str();
                        sei.nShow = SW_SHOWNORMAL;
                        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
                        if (!ShellExecuteExW(&sei)) {
                            DWORD errorCode = GetLastError();
                            wstring errorMsg = L"\u6253\u5f00\u5931\u8d25\uff0c\u9519\u8bef\u7801\uff1a" + to_wstring(errorCode) + L"\n\u8def\u5f84\uff1a" + openPath;
                            MessageBoxW(NULL, errorMsg.c_str(), L"\u9519\u8bef", MB_ICONERROR);
                        }
                    } else {
                        MessageBoxW(nullptr, (L"\u65e0\u6cd5\u6253\u5f00\u6587\u4ef6\uff1a" + selectedItem.displayName + L"\n\u8be5\u6587\u4ef6\u7c7b\u578b\u65e0\u9ed8\u8ba4\u5173\u8054\u7a0b\u5e8f\u3002").c_str(), 
                                    L"\u63d0\u793a", MB_ICONINFORMATION);
                    }
                }
            }
            InvalidateRect(hWnd, nullptr, TRUE);
            return 0;
        }
        case WM_CONTEXTMENU: {
            POINT pt = {
                (SHORT)(LOWORD(lParam)),
                (SHORT)(HIWORD(lParam))
            };
            AdjustPopupMenuPos(hWnd, pt);
            HMENU hMenu = CreatePopupMenu();
            
            bool canGoBack = !pState->pathHistory.empty() && pState->pathHistory.size() > 1;
            AppendMenuW(hMenu, MF_STRING | (canGoBack ? 0 : MF_GRAYED), 1000, L"\u8fd4\u56de\u4e0a\u4e00\u7ea7");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            
            AppendMenuW(hMenu, MF_STRING, 1001, L"\u5237\u65b0");
            bool isValidItem = (pState->selectedIndex != -1) 
                              && !pState->items[pState->selectedIndex].isSpecialItem 
                              && !pState->items[pState->selectedIndex].fullPath.empty()
                              && pState->items[pState->selectedIndex].displayName != L"\u56de\u6536\u7ad9\u4e3a\u7a7a";
            if (isValidItem) {
                AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(hMenu, MF_STRING, 1004, L"\u79fb\u52a8\u5230\u56de\u6536\u7ad9");
            }
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu, MF_STRING, 1002, L"\u5173\u95ed\u7a97\u53e3");
            if (pState->currentPath == L"::RecycleBin::") {
                AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
                if (isValidItem) {
                    AppendMenuW(hMenu, MF_STRING, 1005, L"\u6062\u590d\u6b64\u9879\u76ee");
                    AppendMenuW(hMenu, MF_STRING, 1006, L"\u6c38\u4e45\u5220\u9664\u6b64\u9879\u76ee");
                }
                AppendMenuW(hMenu, MF_STRING, 1003, L"\u6e05\u7a7a\u56de\u6536\u7ad9");
            }
            int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, nullptr);
            switch (cmd) {
                case 1000: {
                    if (canGoBack) {
                        pState->pathHistory.pop();
                        wstring parentPath = pState->pathHistory.top();
                        
                        pState->scrollOffset = 0;
                        pState->currentPath = parentPath;
                        EnumDirItems(pState->currentPath, pState->items);
                        SetWindowTextW(hWnd, pState->currentPath.c_str());
                        pState->selectedIndex = -1;
                        InvalidateRect(hWnd, nullptr, TRUE);
                    }
                    break;
                }
                case 1001:
                    if (pState->currentPath == L"::MyComputer::") EnumMyComputerItems(pState->items);
                    else if (pState->currentPath == L"::RecycleBin::") EnumRecycleBinItems(pState->items);
                    else EnumDirItems(pState->currentPath, pState->items);
                    pState->selectedIndex = -1;
                    InvalidateRect(hWnd, nullptr, TRUE);
                    break;
                case 1002:
                    DestroyWindow(hWnd);
                    break;
                case 1003:
                    ClearRecycleBin();
                    EnumRecycleBinItems(pState->items);
                    pState->selectedIndex = -1;
                    InvalidateRect(hWnd, nullptr, TRUE);
                    break;
                case 1004:
                    if (isValidItem) {
                        const FileItem& selectedItem = pState->items[pState->selectedIndex];
                        MoveToRecycleBin(selectedItem.fullPath);
                        if (pState->currentPath == L"::MyComputer::") EnumMyComputerItems(pState->items);
                        else if (pState->currentPath == L"::RecycleBin::") EnumRecycleBinItems(pState->items);
                        else EnumDirItems(pState->currentPath, pState->items);
                        pState->selectedIndex = -1;
                        InvalidateRect(hWnd, nullptr, TRUE);
                    }
                    break;
                case 1005: {
                    if (isValidItem && pState->currentPath == L"::RecycleBin::") {
                        wstring targetStoragePath = pState->items[pState->selectedIndex].fullPath;
                        for (size_t i = 0; i < g_recycleBinItems.size(); ++i) {
                            if (g_recycleBinItems[i].storagePath == targetStoragePath) {
                                RestoreRecycleItem(i);
                                break;
                            }
                        }
                        EnumRecycleBinItems(pState->items);
                        pState->selectedIndex = -1;
                        InvalidateRect(hWnd, nullptr, TRUE);
                    }
                    break;
                }
                case 1006: {
                    if (isValidItem && pState->currentPath == L"::RecycleBin::") {
                        wstring targetStoragePath = pState->items[pState->selectedIndex].fullPath;
                        for (size_t i = 0; i < g_recycleBinItems.size(); ++i) {
                            if (g_recycleBinItems[i].storagePath == targetStoragePath) {
                                DeleteSingleRecycleItem(i);
                                break;
                            }
                        }
                        EnumRecycleBinItems(pState->items);
                        pState->selectedIndex = -1;
                        InvalidateRect(hWnd, nullptr, TRUE);
                    }
                    break;
                }
            }
            DestroyMenu(hMenu);
            return 0;
        }
        case WM_CLOSE: {
            KillTimer(hWnd, pState->timerID);
            for (auto& item : pState->items) {
                if (item.hLargeIcon) DestroyIcon(item.hLargeIcon);
                if (item.hSmallIcon) DestroyIcon(item.hSmallIcon);
            }
            delete pState;
            RevokeDropTarget();
            DestroyWindow(hWnd);
            return 0;
        }
        case WM_DESTROY: {
            return 0;
        }
        default:
            return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }
}
LRESULT CALLBACK MainWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            WCHAR szDesktopPath[MAX_PATH] = {0};
            bool isSystemDesktop = false;
            if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, szDesktopPath))) {
                g_mainDir = szDesktopPath;
                isSystemDesktop = true;
            } else {
                WCHAR szExePath[MAX_PATH] = {0};
                GetModuleFileNameW(nullptr, szExePath, MAX_PATH);
                WCHAR* pLastSlash = wcsrchr(szExePath, L'\\');
                if (pLastSlash) {
                    *pLastSlash = L'\0';
                    g_mainDir = szExePath;
                }
            }
            CreateRecycleStorageDir();
            LoadFEMTConfig();
            EnumDirItems(g_mainDir, g_mainFileItems);
            AddSpecialItems(g_mainFileItems);
            g_mainTimerID = SetTimer(hWnd, 1, 1000, nullptr);
            RegisterDropTarget(hWnd);
            wstring tipMsg = isSystemDesktop ? (L"\u5df2\u7ed1\u5b9a\u684c\u9762\u6587\u4ef6\u5939\uff1a" + g_mainDir) : (L"\u684c\u9762\u8def\u5f84\u83b7\u53d6\u5931\u8d25\uff0c\u7ed1\u5b9a\u7a0b\u5e8f\u76ee\u5f55\uff1a" + g_mainDir);
            MessageBoxW(nullptr, tipMsg.c_str(), L"\u521d\u59cb\u5316\u5b8c\u6210", MB_ICONINFORMATION);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rcClient;
            GetClientRect(hWnd, &rcClient);
            HBRUSH hBgBrush = CreateSolidBrush(g_bgColor);
            FillRect(hdc, &rcClient, hBgBrush);
            DeleteObject(hBgBrush);
            int x = 20, y = 20 - g_mainScrollOffset;
            for (size_t i = 0; i < g_mainFileItems.size(); ++i) {
                const FileItem& item = g_mainFileItems[i];
                RECT itemRect = {x, y, x + g_itemWidth, y + g_itemHeight};
                
                if (i == g_mainSelectedIndex) {
                    HBRUSH hSelBrush = CreateSolidBrush(RGB(200, 220, 255));
                    FillRect(hdc, &itemRect, hSelBrush);
                    DeleteObject(hSelBrush);
                }
                if (item.hLargeIcon) {
                    int iconX = x + (g_itemWidth - g_iconSize) / 2;
                    DrawIconEx(hdc, iconX, y, item.hLargeIcon, g_iconSize, g_iconSize, 0, nullptr, DI_NORMAL);
                }
                RECT textRect = {x, y + g_iconSize + 5, x + g_itemWidth, y + g_itemHeight};
                SetBkMode(hdc, TRANSPARENT);
                DrawTextW(hdc, item.displayName.c_str(), -1, &textRect, DT_CENTER | DT_WORDBREAK | DT_VCENTER);
                x += g_itemWidth + 10;
                if (x + g_itemWidth > rcClient.right - 20) {
                    x = 20;
                    y += g_itemHeight + 20;
                }
            }
            SYSTEMTIME st;
            GetLocalTime(&st);
            WCHAR timeStr[64] = {0};
            swprintf(timeStr, L"%04d-%02d-%02d %02d:%02d:%02d",
                st.wYear, st.wMonth, st.wDay,
                st.wHour, st.wMinute, st.wSecond);
            RECT timeRect;
            timeRect.left = 0;
            timeRect.top = rcClient.bottom - 40;
            timeRect.right = rcClient.right;
            timeRect.bottom = rcClient.bottom;
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(0, 0, 0));
            DrawTextW(hdc, timeStr, -1, &timeRect, DT_CENTER | DT_VCENTER);
            EndPaint(hWnd, &ps);
            return 0;
        }
        case WM_TIMER: {
            InvalidateRect(hWnd, nullptr, TRUE);
            return 0;
        }
        case WM_VSCROLL: {
            RECT rcClient;
            GetClientRect(hWnd, &rcClient);
            int scrollMax = CalcItemsTotalHeight(g_mainFileItems, rcClient.right) - rcClient.bottom;
            if (scrollMax < 0) scrollMax = 0;
            switch (LOWORD(wParam)) {
                case SB_LINEUP: g_mainScrollOffset -= 20; break;
                case SB_LINEDOWN: g_mainScrollOffset += 20; break;
                case SB_PAGEUP: g_mainScrollOffset -= rcClient.bottom; break;
                case SB_PAGEDOWN: g_mainScrollOffset += rcClient.bottom; break;
                case SB_THUMBPOSITION:
                case SB_THUMBTRACK: g_mainScrollOffset = HIWORD(wParam); break;
                case SB_TOP: g_mainScrollOffset = 0; break;
                case SB_BOTTOM: g_mainScrollOffset = scrollMax; break;
            }
            if (g_mainScrollOffset < 0) g_mainScrollOffset = 0;
            if (g_mainScrollOffset > scrollMax) g_mainScrollOffset = scrollMax;
            SCROLLINFO si = {0};
            si.cbSize = sizeof(SCROLLINFO);
            si.fMask = SIF_ALL;
            si.nMin = 0;
            si.nMax = scrollMax;
            si.nPage = rcClient.bottom;
            si.nPos = g_mainScrollOffset;
            SetScrollInfo(hWnd, SB_VERT, &si, TRUE);
            InvalidateRect(hWnd, nullptr, TRUE);
            return 0;
        }
        case WM_LBUTTONUP: {
            POINT pt = {LOWORD(lParam), HIWORD(lParam)};
            RECT rcClient;
            GetClientRect(hWnd, &rcClient);
            size_t selectedIndex = GetClickedItemIndex(g_mainFileItems, pt, g_mainScrollOffset, rcClient.right);
            g_mainSelectedIndex = selectedIndex;
            InvalidateRect(hWnd, nullptr, TRUE);
            return 0;
        }
        case WM_LBUTTONDBLCLK: {
            POINT pt = {LOWORD(lParam), HIWORD(lParam)};
            RECT rcClient;
            GetClientRect(hWnd, &rcClient);
            size_t selectedIndex = GetClickedItemIndex(g_mainFileItems, pt, g_mainScrollOffset, rcClient.right);
            if (selectedIndex != -1) {
                const FileItem& selectedItem = g_mainFileItems[selectedIndex];
                if (selectedItem.isFolder && !selectedItem.fullPath.empty()) {
                    CreateFolderWindow(hWnd, selectedItem.displayName, selectedItem.fullPath);
                    
                    g_mainSelectedIndex = -1;
                    InvalidateRect(hWnd, nullptr, TRUE);
                } else if (selectedItem.isSpecialItem) {
                    CreateFolderWindow(hWnd, selectedItem.displayName, selectedItem.fullPath);
                } else if (!selectedItem.fullPath.empty()) {
                    wstring ext = PathFindExtensionW(selectedItem.fullPath.c_str());
                    if (!ext.empty() && ext[0] == L'.') ext = ext.substr(1);
                    transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
                    auto it = g_femtMappings.find(ext);
                    if (it != g_femtMappings.end()) {
                        wstring params = it->second.paramsTemplate;
                        // 完全删除"-i $.$"整个模板字符串，不做任何替换
                        size_t dashIPos = params.find(L"-i $.$");
                        if (dashIPos != wstring::npos) {
                            params.erase(dashIPos, 6);
                        }
                        // 其他占位符仍然正常处理
                        size_t placeholderPos = params.find(L"$.$");
                        if (placeholderPos != wstring::npos) {
                            params.replace(placeholderPos, 3, selectedItem.fullPath);
                        }
                        SHELLEXECUTEINFOW sei = {0};
                        sei.cbSize = sizeof(SHELLEXECUTEINFOW);
                        sei.lpFile = it->second.appPath.c_str();
                        sei.lpParameters = params.empty() ? nullptr : params.c_str();
                        sei.nShow = SW_SHOWNORMAL;
                        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
                        if (!ShellExecuteExW(&sei)) {
                            DWORD errorCode = GetLastError();
                            wstring errorMsg = L"\u6253\u5f00\u5931\u8d25\uff0c\u9519\u8bef\u7801\uff1a" + to_wstring(errorCode) + L"\n\u8def\u5f84\uff1a" + it->second.appPath;
                            MessageBoxW(NULL, errorMsg.c_str(), L"\u9519\u8bef", MB_ICONERROR);
                        }
                    } else if (IsFileExecutableOrOpenable(selectedItem.fullPath)) {
                        wstring openPath = selectedItem.fullPath;
                        wstring extCheck = PathFindExtensionW(openPath.c_str());
                        transform(extCheck.begin(), extCheck.end(), extCheck.begin(), ::towlower);
                        if (extCheck == L".lnk") {
                            wstring targetPath;
                            if (ResolveShortcut(openPath, targetPath)) {
                                openPath = targetPath;
                            } else {
                                MessageBoxW(NULL, L"\u65e0\u6cd5\u89e3\u6790\u8be5\u5feb\u6377\u65b9\u5f0f\uff08.lnk\uff09", L"\u63d0\u793a", MB_ICONWARNING);
                                return 0;
                            }
                        }
                        SHELLEXECUTEINFOW sei = {0};
                        sei.cbSize = sizeof(SHELLEXECUTEINFOW);
                        sei.lpFile = openPath.c_str();
                        sei.nShow = SW_SHOWNORMAL;
                        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
                        if (!ShellExecuteExW(&sei)) {
                            DWORD errorCode = GetLastError();
                            wstring errorMsg = L"\u6253\u5f00\u5931\u8d25\uff0c\u9519\u8bef\u7801\uff1a" + to_wstring(errorCode) + L"\n\u8def\u5f84\uff1a" + openPath;
                            MessageBoxW(NULL, errorMsg.c_str(), L"\u9519\u8bef", MB_ICONERROR);
                        }
                    } else {
                        MessageBoxW(nullptr, (L"\u65e0\u6cd5\u6253\u5f00\u6587\u4ef6\uff1a" + selectedItem.displayName + L"\n\u8be5\u6587\u4ef6\u7c7b\u578b\u65e0\u9ed8\u8ba4\u5173\u8054\u7a0b\u5e8f\u3002").c_str(), 
                                    L"\u63d0\u793a", MB_ICONINFORMATION);
                    }
                }
            }
            InvalidateRect(hWnd, nullptr, TRUE);
            return 0;
        }
        case WM_CONTEXTMENU: {
            POINT pt = {
                (SHORT)(LOWORD(lParam)),
                (SHORT)(HIWORD(lParam))
            };
            AdjustPopupMenuPos(hWnd, pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, 2001, L"\u5237\u65b0\u684c\u9762");
            AppendMenuW(hMenu, MF_STRING, 2002, L"\u4fee\u6539\u80cc\u666f\u8272");
            bool isValidDesktopItem = (g_mainSelectedIndex != -1)
                                      && !g_mainFileItems[g_mainSelectedIndex].isSpecialItem
                                      && !g_mainFileItems[g_mainSelectedIndex].fullPath.empty();
            if (isValidDesktopItem) {
                AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(hMenu, MF_STRING, 2004, L"\u79fb\u52a8\u5230\u56de\u6536\u7ad9");
            }
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu, MF_STRING, 2003, L"\u9000\u51fa\u7a0b\u5e8f");
            int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, nullptr);
            switch (cmd) {
                case 2001:
                    g_mainScrollOffset = 0;
                    g_mainFileItems.clear();
                    EnumDirItems(g_mainDir, g_mainFileItems);
                    AddSpecialItems(g_mainFileItems);
                    g_mainSelectedIndex = -1;
                    InvalidateRect(hWnd, nullptr, TRUE);
                    break;
                case 2002: {
                    CHOOSECOLORW cc = {0};
                    cc.lStructSize = sizeof(CHOOSECOLORW);
                    cc.hwndOwner = hWnd;
                    cc.rgbResult = g_bgColor;
                    cc.lpCustColors = acrCustClr;
                    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
                    if (ChooseColorW(&cc)) {
                        g_bgColor = cc.rgbResult;
                        InvalidateRect(hWnd, nullptr, TRUE);
                    }
                    break;
                }
                case 2003:
                    DestroyWindow(hWnd);
                    break;
                case 2004:
                    if (isValidDesktopItem) {
                        const FileItem& selectedItem = g_mainFileItems[g_mainSelectedIndex];
                        MoveToRecycleBin(selectedItem.fullPath);
                        g_mainScrollOffset = 0;
                        g_mainFileItems.clear();
                        EnumDirItems(g_mainDir, g_mainFileItems);
                        AddSpecialItems(g_mainFileItems);
                        g_mainSelectedIndex = -1;
                        InvalidateRect(hWnd, nullptr, TRUE);
                    }
                    break;
            }
            DestroyMenu(hMenu);
            return 0;
        }
        case WM_CLOSE: {
            KillTimer(hWnd, g_mainTimerID);
            for (size_t i = 0; i < g_mainFileItems.size(); ++i) {
                if (g_mainFileItems[i].hLargeIcon) DestroyIcon(g_mainFileItems[i].hLargeIcon);
                if (g_mainFileItems[i].hSmallIcon) DestroyIcon(g_mainFileItems[i].hSmallIcon);
            }
            RevokeDropTarget();
            DestroyWindow(hWnd);
            return 0;
        }
        case WM_DESTROY: {
            PostQuitMessage(0);
            return 0;
        }
        default:
            return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }
}
bool RegisterWindowClasses() {
    WNDCLASSEXW wcexMain = {0};
    wcexMain.cbSize = sizeof(WNDCLASSEXW);
    wcexMain.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcexMain.lpfnWndProc = MainWndProc;
    wcexMain.cbClsExtra = 0;
    wcexMain.cbWndExtra = 0;
    wcexMain.hInstance = g_hInstance;
    wcexMain.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcexMain.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcexMain.hbrBackground = (HBRUSH)CreateSolidBrush(g_bgColor);
    wcexMain.lpszMenuName = nullptr;
    wcexMain.lpszClassName = g_mainWndClass;
    wcexMain.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
    if (!RegisterClassExW(&wcexMain)) {
        MessageBoxW(nullptr, L"\u4e3b\u7a97\u53e3\u7c7b\u6ce8\u518c\u5931\u8d25\uff01", L"\u9519\u8bef", MB_ICONERROR);
        return false;
    }
    WNDCLASSEXW wcexFolder = {0};
    wcexFolder.cbSize = sizeof(WNDCLASSEXW);
    wcexFolder.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcexFolder.lpfnWndProc = FolderWndProc;
    wcexFolder.cbClsExtra = 0;
    wcexFolder.cbWndExtra = 0;
    wcexFolder.hInstance = g_hInstance;
    wcexFolder.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcexFolder.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcexFolder.hbrBackground = (HBRUSH)CreateSolidBrush(g_bgColor);
    wcexFolder.lpszMenuName = nullptr;
    wcexFolder.lpszClassName = g_folderWndClass;
    wcexFolder.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
    if (!RegisterClassExW(&wcexFolder)) {
        MessageBoxW(nullptr, L"\u6587\u4ef6\u5939\u7a97\u53e3\u7c7b\u6ce8\u518c\u5931\u8d25\uff01", L"\u9519\u8bef", MB_ICONERROR);
        return false;
    }
    return true;
}
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    g_hInstance = hInstance;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (!RegisterWindowClasses()) {
        CoUninitialize();
        return 1;
    }
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    HWND hMainWnd = CreateWindowExW(
        0, g_mainWndClass, L"PE\u684c\u9762\u7ba1\u7406\u5668",
        WS_POPUP | WS_VISIBLE | WS_VSCROLL,
        0, 0, screenWidth, screenHeight,
        nullptr, nullptr, hInstance, nullptr
    );
    if (!hMainWnd) {
        MessageBoxW(nullptr, L"\u4e3b\u7a97\u53e3\u521b\u5efa\u5931\u8d25\uff01", L"\u9519\u8bef", MB_ICONERROR);
        CoUninitialize();
        return 1;
    }
    MSG msg = {0};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    CoUninitialize();
    return (int)msg.wParam;
}
