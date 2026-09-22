#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winver.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <cstdint>
#include <cwctype>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace
{
struct Configuration
{
    bool debug = false;
    const char* name = "Release";
};

struct PeMetadata
{
    WORD machine = 0;
    std::vector<std::string> importedModules;
};

using VersionFunction = const char*(__cdecl*)() noexcept;
using ApiVersionFunction = std::uint32_t(__cdecl*)() noexcept;

std::wstring lower(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
    return value;
}

std::string lowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return value;
}

std::string win32Error(DWORD error)
{
    std::ostringstream stream;
    stream << "Win32 error " << error;
    return stream.str();
}

bool readFileBytes(const std::wstring& path, std::vector<BYTE>& bytes, std::string& error)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        error = "cannot open DLL: " + win32Error(GetLastError());
        return false;
    }

    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(file, &fileSize) || fileSize.QuadPart <= 0
        || static_cast<ULONGLONG>(fileSize.QuadPart)
               > static_cast<ULONGLONG>(std::numeric_limits<DWORD>::max()))
    {
        error = "cannot read DLL size";
        CloseHandle(file);
        return false;
    }

    bytes.resize(static_cast<std::size_t>(fileSize.QuadPart));
    DWORD bytesRead = 0;
    const BOOL readSucceeded =
        ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &bytesRead, nullptr);
    const DWORD readError = readSucceeded ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!readSucceeded)
    {
        error = "cannot read DLL: " + win32Error(readError);
        return false;
    }
    if (bytesRead != bytes.size())
    {
        error = "cannot read complete DLL";
        return false;
    }
    return true;
}

bool hasRange(const std::vector<BYTE>& bytes, ULONGLONG offset, ULONGLONG size)
{
    return offset <= bytes.size() && size <= bytes.size() - offset;
}

template <typename T>
bool readStruct(const std::vector<BYTE>& bytes, ULONGLONG offset, T& value)
{
    if (!hasRange(bytes, offset, sizeof(T)))
        return false;
    std::memcpy(&value, bytes.data() + static_cast<std::size_t>(offset), sizeof(T));
    return true;
}

bool rvaToFileOffset(
    DWORD rva,
    std::size_t requiredSize,
    DWORD sizeOfHeaders,
    const std::vector<IMAGE_SECTION_HEADER>& sections,
    const std::vector<BYTE>& bytes,
    ULONGLONG& offset)
{
    if (rva < sizeOfHeaders)
    {
        offset = rva;
        return hasRange(bytes, offset, requiredSize);
    }

    for (const IMAGE_SECTION_HEADER& section : sections)
    {
        const ULONGLONG sectionStart = section.VirtualAddress;
        const ULONGLONG sectionSpan =
            std::max<ULONGLONG>(section.Misc.VirtualSize, section.SizeOfRawData);
        if (rva < sectionStart || static_cast<ULONGLONG>(rva) >= sectionStart + sectionSpan)
            continue;

        const ULONGLONG delta = static_cast<ULONGLONG>(rva) - sectionStart;
        if (delta + requiredSize > section.SizeOfRawData)
            return false;
        offset = static_cast<ULONGLONG>(section.PointerToRawData) + delta;
        return hasRange(bytes, offset, requiredSize);
    }
    return false;
}

bool readAsciiString(
    const std::vector<BYTE>& bytes,
    ULONGLONG offset,
    std::string& value)
{
    if (!hasRange(bytes, offset, 1))
        return false;

    value.clear();
    for (ULONGLONG index = offset; index < bytes.size(); ++index)
    {
        const BYTE character = bytes[static_cast<std::size_t>(index)];
        if (character == 0)
            return !value.empty();
        if (character > 0x7f)
            return false;
        value.push_back(static_cast<char>(character));
    }
    return false;
}

bool readPeMetadata(const std::wstring& path, PeMetadata& metadata, std::string& error)
{
    std::vector<BYTE> bytes;
    if (!readFileBytes(path, bytes, error))
        return false;

    IMAGE_DOS_HEADER dos{};
    if (!readStruct(bytes, 0, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0)
    {
        error = "invalid PE DOS header";
        return false;
    }

    const ULONGLONG ntOffset = static_cast<ULONGLONG>(dos.e_lfanew);
    DWORD signature = 0;
    IMAGE_FILE_HEADER fileHeader{};
    if (!readStruct(bytes, ntOffset, signature) || signature != IMAGE_NT_SIGNATURE
        || !readStruct(bytes, ntOffset + sizeof(signature), fileHeader))
    {
        error = "invalid PE signature or file header";
        return false;
    }

    const ULONGLONG optionalOffset =
        ntOffset + sizeof(signature) + sizeof(IMAGE_FILE_HEADER);
    if (fileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64))
    {
        error = "PE optional header is not a complete PE32+ header";
        return false;
    }

    IMAGE_OPTIONAL_HEADER64 optionalHeader{};
    if (!readStruct(bytes, optionalOffset, optionalHeader)
        || optionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        error = "PE image is not PE32+";
        return false;
    }

    const ULONGLONG sectionOffset = optionalOffset + fileHeader.SizeOfOptionalHeader;
    const ULONGLONG sectionBytes =
        static_cast<ULONGLONG>(fileHeader.NumberOfSections) * sizeof(IMAGE_SECTION_HEADER);
    if (!hasRange(bytes, sectionOffset, sectionBytes))
    {
        error = "invalid PE section table";
        return false;
    }

    std::vector<IMAGE_SECTION_HEADER> sections(fileHeader.NumberOfSections);
    for (std::size_t index = 0; index < sections.size(); ++index)
    {
        if (!readStruct(bytes, sectionOffset + index * sizeof(IMAGE_SECTION_HEADER),
                        sections[index]))
        {
            error = "invalid PE section header";
            return false;
        }
    }

    metadata.machine = fileHeader.Machine;
    metadata.importedModules.clear();
    if (optionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_IMPORT)
        return true;

    const IMAGE_DATA_DIRECTORY importDirectory =
        optionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (importDirectory.VirtualAddress == 0 || importDirectory.Size == 0)
        return true;

    const std::size_t descriptorCount =
        importDirectory.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR);
    if (descriptorCount == 0)
    {
        error = "invalid PE import directory";
        return false;
    }

    bool terminated = false;
    for (std::size_t index = 0; index < descriptorCount; ++index)
    {
        const ULONGLONG descriptorRva =
            static_cast<ULONGLONG>(importDirectory.VirtualAddress)
            + index * sizeof(IMAGE_IMPORT_DESCRIPTOR);
        if (descriptorRva > std::numeric_limits<DWORD>::max())
        {
            error = "invalid PE import descriptor RVA";
            return false;
        }

        ULONGLONG descriptorOffset = 0;
        if (!rvaToFileOffset(static_cast<DWORD>(descriptorRva),
                             sizeof(IMAGE_IMPORT_DESCRIPTOR),
                             optionalHeader.SizeOfHeaders,
                             sections,
                             bytes,
                             descriptorOffset))
        {
            error = "invalid PE import descriptor";
            return false;
        }

        IMAGE_IMPORT_DESCRIPTOR descriptor{};
        if (!readStruct(bytes, descriptorOffset, descriptor))
        {
            error = "invalid PE import descriptor";
            return false;
        }
        if (descriptor.OriginalFirstThunk == 0 && descriptor.TimeDateStamp == 0
            && descriptor.ForwarderChain == 0 && descriptor.Name == 0
            && descriptor.FirstThunk == 0)
        {
            terminated = true;
            break;
        }
        if (descriptor.Name == 0)
        {
            error = "PE import descriptor has no module name";
            return false;
        }

        ULONGLONG nameOffset = 0;
        if (!rvaToFileOffset(descriptor.Name, 1, optionalHeader.SizeOfHeaders,
                             sections, bytes, nameOffset))
        {
            error = "invalid PE import module name RVA";
            return false;
        }

        std::string name;
        if (!readAsciiString(bytes, nameOffset, name))
        {
            error = "invalid PE import module name";
            return false;
        }
        metadata.importedModules.push_back(lowerAscii(name));
    }

    if (!terminated)
    {
        error = "unterminated PE import directory";
        return false;
    }

    std::sort(metadata.importedModules.begin(), metadata.importedModules.end());
    metadata.importedModules.erase(
        std::unique(metadata.importedModules.begin(), metadata.importedModules.end()),
        metadata.importedModules.end());
    return true;
}

std::wstring fullPath(const std::wstring& path)
{
    const DWORD required = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (required == 0)
        return {};

    std::vector<wchar_t> buffer(required);
    const DWORD written = GetFullPathNameW(path.c_str(), required, buffer.data(), nullptr);
    if (written == 0 || written >= required)
        return {};
    return std::wstring(buffer.data(), written);
}

bool parseConfiguration(const std::wstring& value, Configuration& configuration)
{
    const std::wstring normalized = lower(value);
    if (normalized == L"debug")
    {
        configuration = {true, "Debug"};
        return true;
    }
    if (normalized == L"release")
    {
        configuration = {false, "Release"};
        return true;
    }
    return false;
}

bool parseFileVersion(
    const wchar_t* value,
    std::array<unsigned short, 4>& version)
{
    const std::wstring text(value);
    std::size_t position = 0;
    for (std::size_t index = 0; index < version.size(); ++index)
    {
        if (position >= text.size() || !std::iswdigit(text[position]))
            return false;

        unsigned long component = 0;
        while (position < text.size() && std::iswdigit(text[position]))
        {
            component = component * 10 + static_cast<unsigned long>(text[position] - L'0');
            if (component > std::numeric_limits<unsigned short>::max())
                return false;
            ++position;
        }
        version[index] = static_cast<unsigned short>(component);

        if (index + 1 < version.size())
        {
            if (position >= text.size() || text[position] != L'.')
                return false;
            ++position;
        }
    }
    return position == text.size();
}

bool parseApiVersion(const wchar_t* value, std::uint32_t& version)
{
    const std::wstring text(value);
    if (text.empty())
        return false;

    std::uint64_t parsed = 0;
    for (const wchar_t character : text)
    {
        if (character < L'0' || character > L'9')
            return false;
        const std::uint64_t digit = static_cast<std::uint64_t>(character - L'0');
        if (parsed > (std::numeric_limits<std::uint32_t>::max() - digit) / 10)
            return false;
        parsed = parsed * 10 + digit;
    }
    version = static_cast<std::uint32_t>(parsed);
    return true;
}

bool readFileVersion(const std::wstring& path, std::array<unsigned short, 4>& version)
{
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (size == 0)
        return false;

    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data()))
        return false;

    VS_FIXEDFILEINFO* info = nullptr;
    UINT infoSize = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<LPVOID*>(&info), &infoSize)
        || info == nullptr || infoSize < sizeof(VS_FIXEDFILEINFO)
        || info->dwSignature != VS_FFI_SIGNATURE)
    {
        return false;
    }

    version = {
        static_cast<unsigned short>(HIWORD(info->dwFileVersionMS)),
        static_cast<unsigned short>(LOWORD(info->dwFileVersionMS)),
        static_cast<unsigned short>(HIWORD(info->dwFileVersionLS)),
        static_cast<unsigned short>(LOWORD(info->dwFileVersionLS))};
    return true;
}

std::string versionText(const std::array<unsigned short, 4>& version)
{
    std::ostringstream stream;
    stream << version[0] << '.' << version[1] << '.' << version[2] << '.' << version[3];
    return stream.str();
}

std::string narrowAscii(const wchar_t* value)
{
    std::string result;
    while (*value != L'\0')
    {
        if (*value > 0x7f)
            return {};
        result.push_back(static_cast<char>(*value));
        ++value;
    }
    return result;
}

bool hasImport(const std::vector<std::string>& imports, const char* name)
{
    return std::binary_search(imports.begin(), imports.end(), lowerAscii(name));
}

bool checkCrtImports(
    const std::vector<std::string>& imports,
    Configuration configuration,
    std::string& error)
{
    const std::vector<const char*> required = configuration.debug
        ? std::vector<const char*>{"MSVCP140D.dll", "VCRUNTIME140D.dll", "ucrtbased.dll"}
        : std::vector<const char*>{"MSVCP140.dll", "VCRUNTIME140.dll"};
    const std::vector<const char*> forbidden = configuration.debug
        ? std::vector<const char*>{"MSVCP140.dll", "VCRUNTIME140.dll", "VCRUNTIME140_1.dll"}
        : std::vector<const char*>{"MSVCP140D.dll", "VCRUNTIME140D.dll",
                                   "VCRUNTIME140_1D.dll", "ucrtbased.dll"};

    for (const char* module : required)
    {
        if (!hasImport(imports, module))
        {
            error = "target DLL does not import required " + std::string(configuration.name)
                    + " CRT module " + module;
            return false;
        }
    }
    for (const char* module : forbidden)
    {
        if (hasImport(imports, module))
        {
            error = "target DLL imports opposite-configuration CRT module " + std::string(module);
            return false;
        }
    }
    return true;
}

int fail(const std::string& message)
{
    std::cout << "runtime_check,error," << message << '\n';
    return 1;
}
}

int wmain(int argc, wchar_t* argv[])
{
    if (argc != 6)
    {
        std::cerr
            << "usage: JaguarCoreRuntimeCheck <JaguarCore.dll> "
               "<expected-public-version> <expected-file-version> "
               "<expected-api-version> <Debug|Release>\n";
        return 2;
    }

    const std::wstring path = fullPath(argv[1]);
    if (path.empty())
        return fail("cannot resolve DLL path");

    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        return fail("DLL path does not identify a file");

    const std::string expectedPublicVersion = narrowAscii(argv[2]);
    if (expectedPublicVersion.empty())
        return fail("expected public version must be non-empty ASCII");

    std::array<unsigned short, 4> expectedFileVersion{};
    if (!parseFileVersion(argv[3], expectedFileVersion))
        return fail("expected file version must contain four numeric components");

    std::uint32_t expectedApiVersion = 0;
    if (!parseApiVersion(argv[4], expectedApiVersion))
        return fail("expected API version must be a non-negative 32-bit integer");

    Configuration configuration;
    if (!parseConfiguration(argv[5], configuration))
        return fail("configuration must be Debug or Release");

    PeMetadata metadata;
    std::string peError;
    if (!readPeMetadata(path, metadata, peError))
        return fail(peError);
    if (metadata.machine != IMAGE_FILE_MACHINE_AMD64)
        return fail("JaguarCore DLL is not an x64 binary");
    std::cout << "runtime_check,architecture,x64\n";

    std::array<unsigned short, 4> fileVersion{};
    if (!readFileVersion(path, fileVersion))
        return fail("DLL has no readable fixed file version resource");
    std::cout << "runtime_check,file_version," << versionText(fileVersion) << '\n';
    if (fileVersion != expectedFileVersion)
        return fail("fixed file version does not match expected version");

    std::string crtError;
    if (!checkCrtImports(metadata.importedModules, configuration, crtError))
        return fail(crtError);
    std::cout << "runtime_check,crt_imports," << configuration.name << ",dynamic,ok\n";

    HMODULE module = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (module == nullptr)
        return fail("LoadLibraryExW failed: " + win32Error(GetLastError()));

    const auto versionFunction = reinterpret_cast<VersionFunction>(
        GetProcAddress(module, "JaguarCoreVersion"));
    if (versionFunction == nullptr)
    {
        FreeLibrary(module);
        return fail("missing JaguarCoreVersion export");
    }

    const char* version = versionFunction();
    if (version == nullptr || version[0] == '\0')
    {
        FreeLibrary(module);
        return fail("JaguarCoreVersion returned an empty value");
    }
    std::cout << "runtime_check,export,JaguarCoreVersion,ok\n";
    std::cout << "runtime_check,jaguarcore_version," << version << '\n';

    if (expectedPublicVersion != version)
    {
        FreeLibrary(module);
        return fail("JaguarCoreVersion does not match expected version");
    }

    const auto apiVersionFunction = reinterpret_cast<ApiVersionFunction>(
        GetProcAddress(module, "JaguarCoreApiVersion"));
    if (apiVersionFunction == nullptr)
    {
        FreeLibrary(module);
        return fail("missing JaguarCoreApiVersion export");
    }

    const std::uint32_t apiVersion = apiVersionFunction();
    std::cout << "runtime_check,export,JaguarCoreApiVersion,ok\n";
    std::cout << "runtime_check,jaguarcore_api_version," << apiVersion << '\n';
    if (apiVersion != expectedApiVersion)
    {
        FreeLibrary(module);
        return fail("JaguarCoreApiVersion does not match expected version");
    }

    FreeLibrary(module);
    std::cout << "runtime_check,result,pass\n";
    return 0;
}
