// PathCopyCopyPlugin1a.h
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

#pragma once
#include "resource.h"       // main symbols

#include "TestPlugins_i.h"
#include <PathCopyCopy_i.h>


#if defined(_WIN32_WCE) && !defined(_CE_DCOM) && !defined(_CE_ALLOW_SINGLE_THREADED_OBJECTS_IN_MTA)
#error "Single-threaded COM objects are not properly supported on Windows CE platform, such as the Windows Mobile platforms that do not include full DCOM support. Define _CE_ALLOW_SINGLE_THREADED_OBJECTS_IN_MTA to force ATL to support creating single-thread COM object's and allow use of it's single-threaded COM object implementations. The threading model in your rgs file was set to 'Free' as that is the only threading model supported in non DCOM Windows CE platforms."
#endif



// CPathCopyCopyPlugin1a

class ATL_NO_VTABLE CPathCopyCopyPlugin1a :
	public ATL::CComObjectRootEx<ATL::CComSingleThreadModel>,
	public ATL::CComCoClass<CPathCopyCopyPlugin1a, &CLSID_PathCopyCopyPlugin1a>,
	public IPathCopyCopyPlugin1a,
    public IPathCopyCopyPlugin,
    public IPathCopyCopyPluginGroupInfo,
    public IPathCopyCopyPluginStateInfo,
    public IPathCopyCopyPluginIconInfo
{
public:
	CPathCopyCopyPlugin1a()
	{
	}

DECLARE_REGISTRY_RESOURCEID(IDR_PATHCOPYCOPYPLUGIN1A)

DECLARE_NOT_AGGREGATABLE(CPathCopyCopyPlugin1a)

BEGIN_COM_MAP(CPathCopyCopyPlugin1a)
	COM_INTERFACE_ENTRY(IPathCopyCopyPlugin1a)
    COM_INTERFACE_ENTRY(IPathCopyCopyPlugin)
    COM_INTERFACE_ENTRY(IPathCopyCopyPluginGroupInfo)
    COM_INTERFACE_ENTRY(IPathCopyCopyPluginStateInfo)
    COM_INTERFACE_ENTRY(IPathCopyCopyPluginIconInfo)
END_COM_MAP()



	DECLARE_PROTECT_FINAL_CONSTRUCT()

	HRESULT FinalConstruct()
	{
		return S_OK;
	}

	void FinalRelease()
	{
	}

    // IPathCopyCopyPlugin interface
    STDMETHOD(get_Description)(BSTR *p_ppDescription);
    STDMETHOD(get_HelpText)(BSTR *p_ppHelpText);
    STDMETHOD(GetPath)(BSTR p_pPath, BSTR *p_ppNewPath);

    // IPathCopyCopyPluginGroupInfo interface
    STDMETHOD(get_GroupId)(ULONG *p_pGroupId);
    STDMETHOD(get_GroupPosition)(ULONG *p_pPosition);

    // IPathCopyCopyPluginStateInfo
    STDMETHOD(Enabled)(BSTR p_pParentPath, BSTR p_pFile, VARIANT_BOOL *p_pEnabled);

    // IPathCopyCopyIconInfo
    STDMETHOD(get_IconFile)(BSTR *p_ppIconFile);
    STDMETHOD(get_UseDefaultIcon)(VARIANT_BOOL *p_pUseDefaultIcon);
};

OBJECT_ENTRY_AUTO(__uuidof(PathCopyCopyPlugin1a), CPathCopyCopyPlugin1a)
