// ShortUNCPathPlugin.cpp
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
#include <ShortUNCPathPlugin.h>
#include <PluginUtils.h>
#include <resource.h>

#include <assert.h>


namespace PCC
{
    namespace Plugins
    {
        // Plugin unique ID: {891F9E3E-0B70-49fd-A86C-53B21C6193C7}
        const GUID ShortUNCPathPlugin::ID = { 0x891f9e3e, 0xb70, 0x49fd, { 0xa8, 0x6c, 0x53, 0xb2, 0x1c, 0x61, 0x93, 0xc7 } };

        //
        // Constructor.
        //
        ShortUNCPathPlugin::ShortUNCPathPlugin()
            : LongUNCPathPlugin(IDS_SHORT_UNC_PATH_PLUGIN_DESCRIPTION, IDS_ANDROGYNOUS_UNC_PATH_PLUGIN_DESCRIPTION, IDS_SHORT_UNC_PATH_PLUGIN_HINT)
        {
        }

        //
        // Returns the plugin's unique identifier.
        //
        // @return Unique identifier.
        //
        const GUID& ShortUNCPathPlugin::Id() const
        {
            return ShortUNCPathPlugin::ID;
        }

        //
        // Returns the short UNC path of the specified file.
        //
        // @param p_File File path.
        // @return Short UNC path.
        //
        std::wstring ShortUNCPathPlugin::GetPath(const std::wstring& p_File) const
        {
            // First call inherited to get a long path.
            std::wstring path = LongUNCPathPlugin::GetPath(p_File);

            // Now ask for a short version and return it.
            if (!path.empty()) {
                wchar_t shortPath[MAX_PATH + 1];
                DWORD copied = ::GetShortPathNameW(path.c_str(),
                                                   shortPath,
                                                   sizeof(shortPath) / sizeof(wchar_t));
                if (copied != 0) {
                    path.assign(shortPath, copied);
                }
            }
            return path;
        }

        //
        // Determines if this plugin is androgynous. It is considered androgynous
        // if the long UNC path plugin is not shown according to settings.
        //
        // @return true to use androgynous description, false to use normal description.
        //
        bool ShortUNCPathPlugin::IsAndrogynous() const
        {
            assert(m_pSettings != nullptr);

            return m_pSettings != nullptr &&
                   m_pSettings->GetDropRedundantWords() &&
                   !PluginUtils::IsPluginShown(*m_pSettings, LongUNCPathPlugin::ID);
        }

    } // namespace Plugins

} // namespace PCC
