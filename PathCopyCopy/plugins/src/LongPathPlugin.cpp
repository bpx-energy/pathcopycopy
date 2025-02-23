// LongPathPlugin.cpp
// (c) 2008-2019, Charles Lechasseur
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
#include <LongPathPlugin.h>
#include <PathCopyCopySettings.h>
#include <PluginUtils.h>
#include <resource.h>
#include <ShortPathPlugin.h>

#include <assert.h>


namespace PCC
{
    namespace Plugins
    {
        // Plugin unique ID: {331A3B60-AF49-44f4-B30D-56ADFF6D25E8}
        const GUID LongPathPlugin::ID = { 0x331a3b60, 0xaf49, 0x44f4, { 0xb3, 0xd, 0x56, 0xad, 0xff, 0x6d, 0x25, 0xe8 } };

        //
        // Constructor.
        //
        LongPathPlugin::LongPathPlugin()
            : AndrogynousInternalPlugin(IDS_LONG_PATH_PLUGIN_DESCRIPTION, IDS_ANDROGYNOUS_PATH_PLUGIN_DESCRIPTION, IDS_LONG_PATH_PLUGIN_HINT)
        {
        }

        //
        // Returns the plugin's unique identifier.
        //
        // @return Unique identifier.
        //
        const GUID& LongPathPlugin::Id() const
        {
            return ID;
        }

        //
        // Returns the long path of the specified file.
        //
        // @param p_File File path.
        // @return Long path.
        //
        std::wstring LongPathPlugin::GetPath(const std::wstring& p_File) const
        {
            assert(m_pSettings != nullptr);

            std::wstring path(p_File);
            if (!path.empty()) {
                wchar_t longPath[MAX_PATH + 1];
                DWORD copied = ::GetLongPathNameW(p_File.c_str(),
                                                  longPath,
                                                  sizeof(longPath) / sizeof(wchar_t));
                if (copied != 0) {
                    path.assign(longPath, copied);
                }

                // Append separator if needed.
                if (m_pSettings != nullptr && m_pSettings->GetAppendSeparatorForDirectories() && PluginUtils::IsDirectory(path)) {
                    path += L"\\";
                }
            }
            return path;
        }

        //
        // Protected constructor with custom description and help text resources.
        //
        // @param p_DescriptionStringResourceID ID of normal description resource.
        // @param p_AndrogynousDescriptionStringResourceID ID of androgynous description resource.
        // @param p_HelpTextStringResourceID ID of help text resource.
        //
        LongPathPlugin::LongPathPlugin(const unsigned short p_DescriptionStringResourceID,
                                       const unsigned short p_AndrogynousDescriptionStringResourceID,
                                       const unsigned short p_HelpTextStringResourceID)
            : AndrogynousInternalPlugin(p_DescriptionStringResourceID, p_AndrogynousDescriptionStringResourceID, p_HelpTextStringResourceID)
        {
        }

        //
        // Determines if this plugin is androgynous. It is considered androgynous
        // if the short path plugin is not shown according to settings.
        //
        // @return true to use androgynous description, false to use normal description.
        //
        bool LongPathPlugin::IsAndrogynous() const
        {
            assert(m_pSettings != nullptr);

            return m_pSettings != nullptr &&
                   m_pSettings->GetDropRedundantWords() &&
                   !PluginUtils::IsPluginShown(*m_pSettings, ShortPathPlugin::ID);
        }

    } // namespace Plugins

} // namespace PCC
