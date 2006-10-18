/*
 * OleUIPasteSpecial implementation
 *
 * Copyright 2006 Huw Davies
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#define COM_NO_WINDOWS_H
#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "wingdi.h"
#include "winuser.h"
#include "winnls.h"
#include "oledlg.h"

#include "oledlg_private.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(ole);

static const struct ps_flag
{
    DWORD flag;
    const char *name;
} ps_flags[] = {
#define PS_FLAG_ENTRY(p) {p, #p}
    PS_FLAG_ENTRY(PSF_SHOWHELP),
    PS_FLAG_ENTRY(PSF_SELECTPASTE),
    PS_FLAG_ENTRY(PSF_SELECTPASTELINK),
    PS_FLAG_ENTRY(PSF_CHECKDISPLAYASICON),
    PS_FLAG_ENTRY(PSF_DISABLEDISPLAYASICON),
    PS_FLAG_ENTRY(PSF_HIDECHANGEICON),
    PS_FLAG_ENTRY(PSF_STAYONCLIPBOARDCHANGE),
    PS_FLAG_ENTRY(PSF_NOREFRESHDATAOBJECT),
    {-1, NULL}
#undef PS_FLAG_ENTRY
};

static void dump_ps_flags(DWORD flags)
{
    char flagstr[1000] = "";

    const struct ps_flag *flag = ps_flags;
    for( ; flag->name; flag++) {
        if(flags & flag->flag) {
            strcat(flagstr, flag->name);
            strcat(flagstr, "|");
        }
    }
    TRACE("flags %08x %s\n", flags, flagstr);
}

static void dump_pastespecial(LPOLEUIPASTESPECIALW ps)
{
    UINT i;
    dump_ps_flags(ps->dwFlags);
    TRACE("hwnd %p caption %s hook %p custdata %lx\n",
          ps->hWndOwner, debugstr_w(ps->lpszCaption), ps->lpfnHook, ps->lCustData);
    if(IS_INTRESOURCE(ps->lpszTemplate))
        TRACE("hinst %p template %04x hresource %p\n", ps->hInstance, (WORD)(ULONG_PTR)ps->lpszTemplate, ps->hResource);
    else
        TRACE("hinst %p template %s hresource %p\n", ps->hInstance, debugstr_w(ps->lpszTemplate), ps->hResource);
    TRACE("dataobj %p arrpasteent %p cpasteent %d arrlinktype %p clinktype %d\n",
          ps->lpSrcDataObj, ps->arrPasteEntries, ps->cPasteEntries,
          ps->arrLinkTypes, ps->cLinkTypes);
    TRACE("cclsidex %d lpclsidex %p nselect %d flink %d hmetapict %p size(%d,%d)\n",
          ps->cClsidExclude, ps->lpClsidExclude, ps->nSelectedIndex, ps->fLink,
          ps->hMetaPict, ps->sizel.cx, ps->sizel.cy);
    for(i = 0; i < ps->cPasteEntries; i++)
    {
        TRACE("arrPasteEntries[%d]: cFormat %08x pTargetDevice %p dwAspect %d lindex %d tymed %d\n",
              i, ps->arrPasteEntries[i].fmtetc.cfFormat, ps->arrPasteEntries[i].fmtetc.ptd,
              ps->arrPasteEntries[i].fmtetc.dwAspect, ps->arrPasteEntries[i].fmtetc.lindex,
              ps->arrPasteEntries[i].fmtetc.tymed);
        TRACE("\tformat name %s result text %s flags %04x\n", debugstr_w(ps->arrPasteEntries[i].lpstrFormatName),
              debugstr_w(ps->arrPasteEntries[i].lpstrResultText), ps->arrPasteEntries[i].dwFlags);
    }
    for(i = 0; i < ps->cLinkTypes; i++)
        TRACE("arrLinkTypes[%d] %08x\n", i, ps->arrLinkTypes[i]);
    for(i = 0; i < ps->cClsidExclude; i++)
        TRACE("lpClsidExclude[%d] %s\n", i, debugstr_guid(&ps->lpClsidExclude[i]));

}

static inline WCHAR *strdupAtoW(const char *str)
{
    DWORD len;
    WCHAR *ret;
    if(!str) return NULL;
    len = MultiByteToWideChar(CP_ACP, 0, str, -1, NULL, 0);
    ret = HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
    MultiByteToWideChar(CP_ACP, 0, str, -1, ret, len);
    return ret;
}

static INT_PTR CALLBACK ps_dlg_proc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp)
{
    TRACE("(%p, %04x, %08x, %08lx)\n", hdlg, msg, wp, lp);
    switch(msg)
    {
    case WM_INITDIALOG:

        return TRUE; /* use default focus */

    case WM_COMMAND:
        switch(LOWORD(wp))
        {
        case IDOK:
        case IDCANCEL:
            EndDialog(hdlg, wp);
            return TRUE;
        }
        return FALSE;
    default:
        return FALSE;
    }

}

/***********************************************************************
 *           OleUIPasteSpecialA (OLEDLG.4)
 */
UINT WINAPI OleUIPasteSpecialA(LPOLEUIPASTESPECIALA psA)
{
    OLEUIPASTESPECIALW ps;
    UINT ret;
    TRACE("(%p)\n", psA);

    memcpy(&ps, psA, psA->cbStruct);

    ps.lpszCaption = strdupAtoW(psA->lpszCaption);
    if(!IS_INTRESOURCE(ps.lpszTemplate))
        ps.lpszTemplate = strdupAtoW(psA->lpszTemplate);

    if(psA->cPasteEntries > 0)
    {
        DWORD size = psA->cPasteEntries * sizeof(ps.arrPasteEntries[0]);
        UINT i;

        ps.arrPasteEntries = HeapAlloc(GetProcessHeap(), 0, size);
        memcpy(ps.arrPasteEntries, psA->arrPasteEntries, size);
        for(i = 0; i < psA->cPasteEntries; i++)
        {
            ps.arrPasteEntries[i].lpstrFormatName =
                strdupAtoW(psA->arrPasteEntries[i].lpstrFormatName);
            ps.arrPasteEntries[i].lpstrResultText =
                strdupAtoW(psA->arrPasteEntries[i].lpstrResultText);
        }
    }

    ret = OleUIPasteSpecialW(&ps);

    if(psA->cPasteEntries > 0)
    {
        UINT i;
        for(i = 0; i < psA->cPasteEntries; i++)
        {
            HeapFree(GetProcessHeap(), 0, (WCHAR*)ps.arrPasteEntries[i].lpstrFormatName);
            HeapFree(GetProcessHeap(), 0, (WCHAR*)ps.arrPasteEntries[i].lpstrResultText);
        }
        HeapFree(GetProcessHeap(), 0, ps.arrPasteEntries);
    }
    if(!IS_INTRESOURCE(ps.lpszTemplate))
        HeapFree(GetProcessHeap(), 0, (WCHAR*)ps.lpszTemplate);
    HeapFree(GetProcessHeap(), 0, (WCHAR*)ps.lpszCaption);

    /* Copy back the output fields */
    psA->dwFlags = ps.dwFlags;
    psA->lpSrcDataObj = ps.lpSrcDataObj;
    psA->nSelectedIndex = ps.nSelectedIndex;
    psA->fLink = ps.fLink;
    psA->hMetaPict = ps.hMetaPict;
    psA->sizel = ps.sizel;

    return ret;
}

/***********************************************************************
 *           OleUIPasteSpecialW (OLEDLG.22)
 */
UINT WINAPI OleUIPasteSpecialW(LPOLEUIPASTESPECIALW ps)
{
    LPCDLGTEMPLATEW dlg_templ = (LPCDLGTEMPLATEW)ps->hResource;

    TRACE("(%p)\n", ps);

    if(TRACE_ON(ole)) dump_pastespecial(ps);

    if(ps->hInstance || !ps->hResource)
    {
        HINSTANCE hInst = ps->hInstance ? ps->hInstance : OLEDLG_hInstance;
        const WCHAR *name = ps->hInstance ? ps->lpszTemplate : MAKEINTRESOURCEW(IDD_PASTESPECIAL4);
        HRSRC hrsrc;

        if(name == NULL) return OLEUI_ERR_LPSZTEMPLATEINVALID;
        hrsrc = FindResourceW(hInst, name, MAKEINTRESOURCEW(RT_DIALOG));
        if(!hrsrc) return OLEUI_ERR_FINDTEMPLATEFAILURE;
        dlg_templ = LoadResource(hInst, hrsrc);
        if(!dlg_templ) return OLEUI_ERR_LOADTEMPLATEFAILURE;
    }

    DialogBoxIndirectParamW(OLEDLG_hInstance, dlg_templ, ps->hWndOwner, ps_dlg_proc, (LPARAM)ps);

    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return OLEUI_FALSE;
}
