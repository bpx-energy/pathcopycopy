// PluginUtils.cpp
// (c) 2011-2019, Charles Lechasseur
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include <stdafx.h>
#include <PluginUtils.h>
#include <PathCopyCopyPluginsRegistry.h>
#include <PathCopyCopySettings.h>
#include <StringUtils.h>

#include <DefaultPlugin.h>

#include <memory>
#include <sstream>

#include <comutil.h>
#include <lm.h>


namespace
{
    const DWORD         INITIAL_BUFFER_SIZE     = 1024;     // Initial size of buffer used to fetch UNC name.
    const DWORD         MAX_REG_KEY_NAME_SIZE   = 255;      // Max size of a registry key's name.

    const std::wstring  SHARES_KEY_NAME     = L"SYSTEM\\CurrentControlSet\\Services\\Lanmanserver\\Shares"; // Name of key storing network shares
    const std::wstring  SHARE_PATH_VALUE    = L"Path=";     // Part of a share key's value containing the share path.
    const wchar_t       HIDDEN_SHARE_SUFFIX = L'$';         // Suffix used for hidden shares; we will not consider them unless specified.

    const std::wstring  HIDDEN_DRIVE_SHARES_REGEX   = L"^([A-Za-z])\\:((\\\\|/).*)$";   // Regex used to convert hidden drive shares.
    const std::wstring  HIDDEN_DRIVE_SHARES_FORMAT  = L"$1$$$2";                        // Format string used to convert hidden drive shares.

    const ULONG         REG_BUFFER_CHUNK_SIZE = 512;        // Size of chunks allocated to read the registry.

} // anonymous namespace

namespace PCC
{
    // Static members of PluginUtils
    std::mutex      PluginUtils::s_Lock;
    std::wstring    PluginUtils::s_ComputerName;
    bool            PluginUtils::s_HasComputerName = false;
    std::wregex     PluginUtils::s_HiddenDriveShareRegex(HIDDEN_DRIVE_SHARES_REGEX, std::regex_constants::ECMAScript);

    //
    // Determines if the given path points to a directory or file.
    //
    // @param p_Path Path to check.
    // @return true if path points to a directory.
    //
    bool PluginUtils::IsDirectory(const std::wstring& p_Path)
    {
        DWORD attribs = ::GetFileAttributesW(p_Path.c_str());
        return attribs != INVALID_FILE_ATTRIBUTES &&
               (attribs & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY;
    }

    //
    // Given a path to a file or folder, will return the path to its
    // parent folder. Ex:
    // C:\Foo\Bar.txt => C:\Foo
    //
    // @param p_rPath Complete path on input, parent's path on output.
    // @return true if we did find a parent path and returned it.
    //
    bool PluginUtils::ExtractFolderFromPath(std::wstring& p_rPath)
    {
        // Find the last delimiter in the path and truncate path
        // as appropriate to return only the parent's path.
        std::wstring::size_type lastDelimPos = p_rPath.find_last_of(L"/\\");
        bool found = (lastDelimPos != std::wstring::npos);
        if (found) {
            // We found a delimiter, clear everything after that
            // (and the delimiter as well). Exception: if we're left
            // with only a drive letter, keep the delimiter.
            if (lastDelimPos <= 2) {
                ++lastDelimPos;
            }
            p_rPath.erase(lastDelimPos);
        }
        return found;
    }

    //
    // Checks if the given path is a UNC path in the form
    // \\server\share[\...]
    //
    // @param p_FilePath Path to verify.
    // @return true if the path is a UNC path.
    //
    bool PluginUtils::IsUNCPath(const std::wstring& p_FilePath)
    {
        return p_FilePath.find(L"\\\\") == 0 && p_FilePath.find(L'\\', 2) > 2;
    }

    //
    // Checks if the given file resides on a mapped network drive.
    // If it does, returns its corresponding network path.
    // Ex: N:\Data\File.txt -> \\server\share\Data\File.txt
    //
    // @param p_rFilePath Local file path. Upon exit, will contain network path.
    // @return true if the file was on a mapped network drive and we fetched its network path.
    //
    bool PluginUtils::GetMappedDriveFilePath(std::wstring& p_rFilePath)
    {
        // WNetGetUniversalName allows us to get the network path
        // if file is on a mapped drive.
        bool converted = false;
        DWORD bufferSize = INITIAL_BUFFER_SIZE;
        std::unique_ptr<char[]> upBuffer;
        DWORD ret = ERROR_MORE_DATA;
        while (ret == ERROR_MORE_DATA) {
            upBuffer.reset(new char[bufferSize]);
            ret = ::WNetGetUniversalNameW(p_rFilePath.c_str(),
                                          UNIVERSAL_NAME_INFO_LEVEL,
                                          upBuffer.get(),
                                          &bufferSize);
        }
        if (ret == NO_ERROR) {
            // Got UNC path, return it.
            p_rFilePath.assign(reinterpret_cast<UNIVERSAL_NAME_INFO*>(upBuffer.get())->lpUniversalName);
            converted = true;
        }
        return converted;
    }

    //
    // Checks if the given file resides in a directory in a network share.
    // If it does, returns its corresponding network path.
    // Ex: C:\SharedDir\File.txt -> \\thiscomputer\SharedDir\File.txt
    //
    // @param p_rFilePath Local file path. Upon exit, will contain network path.
    // @param p_UseHiddenShares Whether to consider hidden shares when looking for valid shares.
    // @return true if the file was in a network share and we fetched its network path.
    //
    bool PluginUtils::GetNetworkShareFilePath(std::wstring& p_rFilePath,
                                              const bool p_UseHiddenShares)
    {
        bool converted = false;

        // Scan registry to see if we can find a share that contains this path.
        // Shares are stored in multi-string registry values in the Lanmanserver service keys.
        ATL::CRegKey shareKey;
        if (shareKey.Open(HKEY_LOCAL_MACHINE, SHARES_KEY_NAME.c_str(), KEY_READ) == ERROR_SUCCESS) {
            // Iterate registry values to check each share.
            wchar_t valueName[MAX_REG_KEY_NAME_SIZE + 1];
            std::wstring multiStringValue;
            std::wstring path;
            LONG ret = 0;
            DWORD i = 0;
            do {
                DWORD valueNameSize = MAX_REG_KEY_NAME_SIZE;
                DWORD valueType = 0;
                ret = ::RegEnumValue(shareKey, i, valueName, &valueNameSize, 0, &valueType, 0, 0);
                if (ret == ERROR_SUCCESS && valueType == REG_MULTI_SZ) {
                    // Get the multi-string values.
                    ULONG bufferSize = INITIAL_BUFFER_SIZE;
                    std::unique_ptr<wchar_t[]> buffer;
                    do {
                        buffer.reset(new wchar_t[bufferSize]);
                        ret = shareKey.QueryMultiStringValue(valueName, buffer.get(), &bufferSize);
                    } while (ret == ERROR_MORE_DATA);
                    if (ret == ERROR_SUCCESS) {
                        // Make sure this is not a hidden share (unless we use them).
                        if (valueNameSize != 0 && (p_UseHiddenShares || valueName[valueNameSize - 1] != HIDDEN_SHARE_SUFFIX)) {
                            // Find the "Path=" part of the mult-string. This contains the share path.
                            multiStringValue.assign(buffer.get(), bufferSize);
                            path = GetMultiStringLineBeginningWith(multiStringValue, SHARE_PATH_VALUE);
                            if (!path.empty()) {
                                // Check if our path is in that share.
                                if (p_rFilePath.find(path) == 0) {
                                    // Success: this is a share that contains our path.
                                    // Replace the start of the path with the computer and share name.
                                    std::wstringstream ss;
                                    ss << L"\\\\" << GetLocalComputerName() << L"\\" << valueName;
                                    if (*path.rbegin() == L'\\' || *path.rbegin() == L'/') {
                                        // The substr below will remove the terminator if the share path
                                        // ends with one (for example, for drives' administrative shares).
                                        // We'll have to add an extra one manually.
                                        ss << L'\\';
                                    }
                                    ss << p_rFilePath.substr(path.size());
                                    std::wstring newPath = ss.str();
                                    p_rFilePath = newPath;
                                    converted = true;
                                    break;
                                }
                            }
                        }
                    }
                }

                // Go to next share.
                ++i;
            } while (ret == ERROR_SUCCESS);
        }

        return converted;
    }

    //
    // Checks if the given file resides in a directory on a local drive.
    // If it does, returns its corresponding network path using a hidden drive share.
    // Ex: C:\Dir\File.txt -> \\thiscomputer\C$\Dir\File.txt
    //
    // @param p_rFilePath Local file path. Upon exit, will contain network path.
    // @return true if the file was on a local drive and we fetched its network path.
    //
    bool PluginUtils::GetHiddenDriveShareFilePath(std::wstring& p_rFilePath)
    {
        // Try to perform the replacement in one shot. If it works, the path is converted.
        bool converted = false;

        try {
            std::wstring replaced = std::regex_replace(p_rFilePath, s_HiddenDriveShareRegex, HIDDEN_DRIVE_SHARES_FORMAT);
            if (!replaced.empty()) {
                std::wstringstream ss;
                ss << L"\\\\" << GetLocalComputerName() << L"\\" << replaced;
                p_rFilePath = ss.str();
                converted = true;
            }
        } catch (const std::regex_error&) {
            // Didn't work.
        }

        return converted;
    }

    //
    // Replaces the hostname in the given UNC path with a
    // fully-qualified domain name (FQDN).
    //
    // @param p_rFilePath UNC path. Upon exit, host name will have been replaced.
    //
    void PluginUtils::ConvertUNCHostToFQDN(std::wstring& p_rFilePath)
    {
        // Find hostname in file path.
        std::wstring hostname, restOfPath;
        if (p_rFilePath.substr(0, 2) == L"\\\\") {
            auto withoutPrefix = p_rFilePath.substr(2);
            auto delimPos = withoutPrefix.find_first_of(L"\\/");
            if (delimPos != std::wstring::npos) {
                hostname = withoutPrefix.substr(0, delimPos);
                restOfPath = withoutPrefix.substr(delimPos);
            }
        }
        if (!hostname.empty()) {
            // First initialize Winsock if it's not already initialized in the process.
            WSADATA wsaData;
            if (::WSAStartup(MAKEWORD(2, 2), &wsaData) == 0) {
                // Try fetching info for the hostname
                hostent* pHostEnt = gethostbyname(_bstr_t(hostname.c_str()));
                if (pHostEnt != nullptr) {
                    // Rebuild the path by replacing the hostname with its FQDN
                    std::wstringstream wss;
                    wss << L"\\\\"
                        << _bstr_t(pHostEnt->h_name)
                        << restOfPath;
                    p_rFilePath = wss.str();
                }

                // Cleanup Winsock before returning.
                ::WSACleanup();
            }
        }
    }

    //
    // Returns the name of the local computer.
    //
    // @return Name of local computer.
    //
    const std::wstring& PluginUtils::GetLocalComputerName()
    {
        if (!s_HasComputerName) {
            std::lock_guard<std::mutex> lock(s_Lock);
            if (!s_HasComputerName) {
                DWORD length = MAX_COMPUTERNAME_LENGTH + 1;
                wchar_t name[MAX_COMPUTERNAME_LENGTH + 1];
                if (::GetComputerNameW(name, &length) != FALSE) {
                    if (::_wcslwr_s(name, length + 1) == 0) {
                        s_ComputerName = name;
                    }
                }
                s_HasComputerName = true;
            }
        }
        return s_ComputerName;
    }

    //
    // Reads the content of a string registry value and returns it in
    // a std::wstring so that it's easier to manage. Will take care of
    // reallocating buffers as needed.
    //
    // @param p_Key Key containing the value to read.
    // @param p_pValueName Name of value.
    // @param p_rValue Upon exit, will contain the value.
    // @return Error code, or ERROR_SUCCESS if all goes well.
    //
    long PluginUtils::ReadRegistryStringValue(const RegKey& p_Key,
                                              const wchar_t* const p_pValueName,
                                              std::wstring& p_rValue)
    {
        // Clear the content to assume value doesn't exist.
        p_rValue.clear();

        // Loop until we are able to read the value.
        long lRes = ERROR_MORE_DATA;
        ULONG curSize = 0;
        while (lRes == ERROR_MORE_DATA) {
            curSize += REG_BUFFER_CHUNK_SIZE;
            std::unique_ptr<wchar_t[]> upBuffer(new wchar_t[curSize]);
            DWORD valueType = REG_SZ;
            DWORD curSizeInBytes = curSize * sizeof(wchar_t);
            lRes = p_Key.QueryValue(p_pValueName, &valueType, upBuffer.get(), &curSizeInBytes);
            if (lRes == ERROR_SUCCESS) {
                // Make sure it is a string.
                if (valueType == REG_SZ && (curSizeInBytes % sizeof(wchar_t)) == 0) {
                    // Success, copy resulting string.
                    p_rValue.assign(upBuffer.get());
                } else {
                    lRes = ERROR_INVALID_DATATYPE;
                }
            }
        }

        return lRes;
    }

    //
    // Given a multi-line string read from a REG_MULTI_SZ registry value,
    // finds a line that begins with a given prefix and returns it.
    //
    // @param p_MultiStringValue The multi-string value.
    // @param p_Prefix Prefix of line to look for.
    // @return The entire matching line (excluding prefix), or an empty string if line is not found.
    //
    std::wstring PluginUtils::GetMultiStringLineBeginningWith(const std::wstring& p_MultiStringValue,
                                                              const std::wstring& p_Prefix)
    {
        std::wstring line;

        // Multi-line values contain embedded NULLs to separate the lines.
        std::wstring::size_type offset = 0;
        do {
            std::wstring::size_type endOffset = p_MultiStringValue.find_first_of(L'\0', offset);
            if (endOffset == offset) {
                // We're at the end of the value.
                break;
            }

            if (p_MultiStringValue.find(p_Prefix, offset) == offset) {
                // This is the line we're looking for.
                std::wstring::size_type prefixSize = p_Prefix.size();
                line = p_MultiStringValue.substr(offset + prefixSize, endOffset - offset - prefixSize);
                break;
            } else {
                // Not the line, go to next one.
                offset = endOffset + 1;
            }
        } while (offset < p_MultiStringValue.size());

        return line;
    }

    //
    // Converts a string containing a list of plugin unique identifiers
    // to a vector of GUID structs.
    //
    // @param p_rPluginIdsAsString String containing the plugin IDs. Upon return,
    //                             this string is unusable.
    // @param p_Separator Character used to separate the plugin IDs in the string.
    // @return Vector of plugin IDs as GUID structs.
    //
    GUIDV PluginUtils::StringToPluginIds(std::wstring& p_rPluginIdsAsString,
                                         const wchar_t p_Separator)
    {
        // Assume there are no plugin IDs.
        GUIDV vPluginIds;

        // First split the string.
        WStringV vStringParts;
        StringUtils::Split(p_rPluginIdsAsString, p_Separator, vStringParts);

        // Scan parts and convert to GUIDs.
        GUID onePluginId = { 0 };
        for (const std::wstring& stringPart : vStringParts) {
            if (SUCCEEDED(::CLSIDFromString(stringPart.c_str(), &onePluginId))) {
                vPluginIds.push_back(onePluginId);
            }
        }

        return vPluginIds;
    }

    //
    // Converts a string containing a list of unsigned integers to a vector.
    //
    // @param p_rUInt32sAsString String containing the integers. Upon return,
    //                           this string is unusable.
    // @param p_Separator Character used to separate the integers in the string.
    // @return Vector of unsigned integers.
    //
    UInt32V PluginUtils::StringToUInt32s(std::wstring& p_rUInt32sAsString,
                                         const wchar_t p_Separator)
    {
        // Assume there are no integers.
        UInt32V vUInt32s;

        // First split the string.
        WStringV vStringParts;
        StringUtils::Split(p_rUInt32sAsString, p_Separator, vStringParts);

        // Scan parts and convert to integers.
        for (const std::wstring& stringPart : vStringParts) {
            std::wistringstream wis(stringPart.c_str());
            uint32_t integer = 0;
            wis >> integer;
            vUInt32s.push_back(integer);
        }

        return vUInt32s;
    }

    //
    // Converts a list of plugin IDs to a string containing them.
    // This is the opposite of StringToPluginIds.
    //
    // @param p_vPluginIds List of plugin IDs to convert.
    // @param p_Separator Character used to separate the plugin IDs in the string.
    // @return String with merged plugin IDs.
    //
    std::wstring PluginUtils::PluginIdsToString(const GUIDV& p_vPluginIds,
                                                const wchar_t p_Separator)
    {
        wchar_t guidBuffer[40]; // See StringFromGUID2 in MSDN
        auto guidToString = [&](const GUID& p_GUID) -> std::wstring {
            return (::StringFromGUID2(p_GUID, guidBuffer, 40) != 0) ? guidBuffer : L"";
        };

        // Insert the first plugin ID without separator.
        std::wostringstream wos;
        if (!p_vPluginIds.empty()) {
            wos << guidToString(p_vPluginIds.front());

            // Insert the other elements with separators.
            GUIDV::const_iterator it, end = p_vPluginIds.cend();
            for (it = p_vPluginIds.cbegin() + 1; it != end; ++it) {
                wos << p_Separator << guidToString(*it);
            }
        }

        // Return resulting string.
        return wos.str();
    }

    //
    // Converts a list of unsigned integers to a string containing them.
    // This is the opposite of StringToUInts.
    //
    // @param p_vUInt32s List of unsigned integers to convert.
    // @param p_Separator Character used to separate the integers in the string.
    // @return String with merged unsigned integers.
    //
    std::wstring PluginUtils::UInt32sToString(const UInt32V& p_vUInt32s,
                                              const wchar_t p_Separator)
    {
        // Insert the first integer without separator.
        std::wostringstream wos;
        if (!p_vUInt32s.empty()) {
            wos << p_vUInt32s.front();

            // Insert the other elements with separators.
            UInt32V::const_iterator it, end = p_vUInt32s.cend();
            for (it = p_vUInt32s.cbegin() + 1; it != end; ++it) {
                wos << p_Separator << *it;
            }
        }

        // Return resulting string.
        return wos.str();
    }

    //
    // Checks in the Path Copy Copy settings if a specific plugin
    // is shown at all, whether in the main menu or in the submenu.
    //
    // @param p_Settings Object to access settings.
    // @param p_PluginId ID of plugin to look for in the submenu.
    // @return true if the plugin is displayed.
    //
    bool PluginUtils::IsPluginShown(const Settings& p_Settings,
                                    const GUID& p_PluginId)
    {
        // Assume it's shown.
        bool shown = true;

        // Get list of plugins in main menu and submenu from settings.
        GUIDV vPluginsInMainMenu, vPluginsInSubmenu;
        if (!p_Settings.GetMainMenuPluginDisplayOrder(vPluginsInMainMenu)) {
            // Not specified, use the default plugin.
            vPluginsInMainMenu.push_back(Plugins::DefaultPlugin().Id());
        }
        if (!p_Settings.GetSubmenuPluginDisplayOrder(vPluginsInSubmenu)) {
            // Not specified, use default plugins in default order.
            PluginSPV vspPlugins = PluginsRegistry::GetPluginsInDefaultOrder(&p_Settings, &p_Settings, false);
            for (const PluginSP& spPlugin : vspPlugins) {
                vPluginsInSubmenu.push_back(spPlugin->Id());
            }
        }

        // Scan lists to find our plugin.
        auto isOurPlugin = [&](const GUID& p_Id) -> bool {
            return ::IsEqualGUID(p_Id, p_PluginId) != FALSE;
        };
        if (std::find_if(vPluginsInMainMenu.cbegin(), vPluginsInMainMenu.cend(), isOurPlugin) == vPluginsInMainMenu.cend() &&
            std::find_if(vPluginsInSubmenu.cbegin(), vPluginsInSubmenu.cend(), isOurPlugin) == vPluginsInSubmenu.cend()) {

            // The plugins is not shown.
            shown = false;
        }

        return shown;
    }

} // namespace PCC
