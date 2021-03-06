/*
 * PROJECT:     ReactOS Zip Shell Extension
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Zip extraction
 * COPYRIGHT:   Copyright 2017 Mark Jansen (mark.jansen@reactos.org)
 */

#include "precomp.h"

class CZipExtract :
    public IZip
{
    CStringW m_Filename;
    CStringW m_Directory;
    bool m_DirectoryChanged;
    unzFile uf;
public:
    CZipExtract(PCWSTR Filename)
        :m_DirectoryChanged(false)
        ,uf(NULL)
    {
        m_Filename = Filename;
        m_Directory = m_Filename;
        PWSTR Dir = m_Directory.GetBuffer();
        PathRemoveExtensionW(Dir);
        m_Directory.ReleaseBuffer();
    }

    ~CZipExtract()
    {
        if (uf)
        {
            DPRINT1("WARNING: uf not closed!\n");
            Close();
        }
    }

    void Close()
    {
        if (uf)
            unzClose(uf);
        uf = NULL;
    }

    // *** IZip methods ***
    STDMETHODIMP QueryInterface(REFIID riid, void  **ppvObject)
    {
        if (riid == IID_IUnknown)
        {
            *ppvObject = this;
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef(void)
    {
        return 2;
    }
    STDMETHODIMP_(ULONG) Release(void)
    {
        return 1;
    }
    STDMETHODIMP_(unzFile) getZip()
    {
        return uf;
    }

    class CConfirmReplace : public CDialogImpl<CConfirmReplace>
    {
    private:
        CStringA m_Filename;
    public:
        enum DialogResult
        {
            Yes,
            YesToAll,
            No,
            Cancel
        };

        static DialogResult ShowDlg(HWND hDlg, PCSTR FullPath)
        {
            PCSTR Filename = PathFindFileNameA(FullPath);
            CConfirmReplace confirm(Filename);
            INT_PTR Result = confirm.DoModal(hDlg);
            switch (Result)
            {
            case IDYES: return Yes;
            case IDYESALL: return YesToAll;
            default:
            case IDNO: return No;
            case IDCANCEL: return Cancel;
            }
        }

        CConfirmReplace(const char* filename)
        {
            m_Filename = filename;
        }

        LRESULT OnInitDialog(UINT nMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
        {
            CenterWindow(GetParent());

            HICON hIcon = LoadIcon(NULL, IDI_EXCLAMATION);
            SendDlgItemMessage(IDC_EXCLAMATION_ICON, STM_SETICON, (WPARAM)hIcon);

            /* Our CString does not support FormatMessage yet */
            CStringA message(MAKEINTRESOURCE(IDS_OVERWRITEFILE_TEXT));
            CHeapPtr<CHAR, CLocalAllocator> formatted;

            DWORD_PTR args[2] =
            {
                (DWORD_PTR)m_Filename.GetString(),
                NULL
            };

            ::FormatMessageA(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_ARGUMENT_ARRAY,
                             message, 0, 0, (LPSTR)&formatted, 0, (va_list*)args);

            ::SetDlgItemTextA(m_hWnd, IDC_MESSAGE, formatted);
            return 0;
        }

        LRESULT OnButton(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
        {
            EndDialog(wID);
            return 0;
        }

    public:
        enum { IDD = IDD_CONFIRM_FILE_REPLACE };

        BEGIN_MSG_MAP(CConfirmReplace)
            MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
            COMMAND_ID_HANDLER(IDYES, OnButton)
            COMMAND_ID_HANDLER(IDYESALL, OnButton)
            COMMAND_ID_HANDLER(IDNO, OnButton)
            COMMAND_ID_HANDLER(IDCANCEL, OnButton)
        END_MSG_MAP()
    };


    class CExtractSettingsPage : public CPropertyPageImpl<CExtractSettingsPage>
    {
    private:
        CZipExtract* m_pExtract;

    public:
        CExtractSettingsPage(CZipExtract* extract)
            :CPropertyPageImpl<CExtractSettingsPage>(MAKEINTRESOURCE(IDS_WIZ_TITLE))
            ,m_pExtract(extract)
        {
            m_psp.pszHeaderTitle = MAKEINTRESOURCE(IDS_WIZ_DEST_TITLE);
            m_psp.pszHeaderSubTitle = MAKEINTRESOURCE(IDS_WIZ_DEST_SUBTITLE);
            m_psp.dwFlags |= PSP_USETITLE | PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE;
        }

        int OnSetActive()
        {
            SetDlgItemTextW(IDC_DIRECTORY, m_pExtract->m_Directory);
            m_pExtract->m_DirectoryChanged = false;
            ::EnableWindow(GetDlgItem(IDC_PASSWORD), FALSE);    /* Not supported for now */
            GetParent().CenterWindow(::GetDesktopWindow());
            return 0;
        }

        int OnWizardNext()
        {
            ::EnableWindow(GetDlgItem(IDC_BROWSE), FALSE);
            ::EnableWindow(GetDlgItem(IDC_DIRECTORY), FALSE);
            ::EnableWindow(GetDlgItem(IDC_PASSWORD), FALSE);

            if (m_pExtract->m_DirectoryChanged)
                UpdateDirectory();

            if (!m_pExtract->Extract(m_hWnd, GetDlgItem(IDC_PROGRESS)))
            {
                /* Extraction failed, do not go to the next page */
                SetWindowLongPtr(DWLP_MSGRESULT, -1);

                ::EnableWindow(GetDlgItem(IDC_BROWSE), TRUE);
                ::EnableWindow(GetDlgItem(IDC_DIRECTORY), TRUE);
                ::EnableWindow(GetDlgItem(IDC_PASSWORD), FALSE);    /* Not supported for now */

                return TRUE;
            }
            return 0;
        }

        struct browse_info
        {
            HWND hWnd;
            LPCWSTR Directory;
        };

        static INT CALLBACK s_BrowseCallbackProc(HWND hWnd, UINT uMsg, LPARAM lp, LPARAM pData)
        {
            if (uMsg == BFFM_INITIALIZED)
            {
                browse_info* info = (browse_info*)pData;
                CWindow dlg(hWnd);
                dlg.SendMessage(BFFM_SETSELECTION, TRUE, (LPARAM)info->Directory);
                dlg.CenterWindow(info->hWnd);
            }
            return 0;
        }

        LRESULT OnBrowse(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
        {
            BROWSEINFOW bi = { m_hWnd };
            WCHAR path[MAX_PATH];
            bi.pszDisplayName = path;
            bi.lpfn = s_BrowseCallbackProc;
            bi.ulFlags = BIF_NEWDIALOGSTYLE | BIF_RETURNFSANCESTORS | BIF_RETURNONLYFSDIRS;
            CStringW title(MAKEINTRESOURCEW(IDS_WIZ_BROWSE_TITLE));
            bi.lpszTitle = title;

            if (m_pExtract->m_DirectoryChanged)
                UpdateDirectory();

            browse_info info = { m_hWnd, m_pExtract->m_Directory.GetString() };
            bi.lParam = (LPARAM)&info;

            CComHeapPtr<ITEMIDLIST> pidl;
            pidl.Attach(SHBrowseForFolderW(&bi));

            WCHAR tmpPath[MAX_PATH];
            if (pidl && SHGetPathFromIDListW(pidl, tmpPath))
            {
                m_pExtract->m_Directory = tmpPath;
                SetDlgItemTextW(IDC_DIRECTORY, m_pExtract->m_Directory);
                m_pExtract->m_DirectoryChanged = false;
            }
            return 0;
        }

        LRESULT OnEnChangeDirectory(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
        {
            m_pExtract->m_DirectoryChanged = true;
            return 0;
        }

        LRESULT OnPassword(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
        {
            return 0;
        }

        void UpdateDirectory()
        {
            GetDlgItemText(IDC_DIRECTORY, m_pExtract->m_Directory);
            m_pExtract->m_DirectoryChanged = false;
        }

    public:
        enum { IDD = IDD_PROPPAGEDESTINATION };

        BEGIN_MSG_MAP(CCompleteSettingsPage)
            COMMAND_ID_HANDLER(IDC_BROWSE, OnBrowse)
            COMMAND_ID_HANDLER(IDC_PASSWORD, OnPassword)
            COMMAND_HANDLER(IDC_DIRECTORY, EN_CHANGE, OnEnChangeDirectory)
            CHAIN_MSG_MAP(CPropertyPageImpl<CExtractSettingsPage>)
        END_MSG_MAP()
    };


    class CCompleteSettingsPage : public CPropertyPageImpl<CCompleteSettingsPage>
    {
    private:
        CZipExtract* m_pExtract;

    public:
        CCompleteSettingsPage(CZipExtract* extract)
            :CPropertyPageImpl<CCompleteSettingsPage>(MAKEINTRESOURCE(IDS_WIZ_TITLE))
            , m_pExtract(extract)
        {
            m_psp.pszHeaderTitle = MAKEINTRESOURCE(IDS_WIZ_COMPL_TITLE);
            m_psp.pszHeaderSubTitle = MAKEINTRESOURCE(IDS_WIZ_COMPL_SUBTITLE);
            m_psp.dwFlags |= PSP_USETITLE | PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE;
        }


        int OnSetActive()
        {
            SetWizardButtons(PSWIZB_FINISH);
            CStringW Path = m_pExtract->m_Directory;
            PWSTR Ptr = Path.GetBuffer();
            RECT rc;
            ::GetWindowRect(GetDlgItem(IDC_DESTDIR), &rc);
            HDC dc = GetDC();
            PathCompactPathW(dc, Ptr, rc.right - rc.left);
            ReleaseDC(dc);
            Path.ReleaseBuffer();
            SetDlgItemTextW(IDC_DESTDIR, Path);
            CheckDlgButton(IDC_SHOW_EXTRACTED, BST_CHECKED);
            return 0;
        }
        BOOL OnWizardFinish()
        {
            if (IsDlgButtonChecked(IDC_SHOW_EXTRACTED) == BST_CHECKED)
            {
                ShellExecuteW(NULL, L"explore", m_pExtract->m_Directory, NULL, NULL, SW_SHOW);
            }
            return FALSE;
        }

    public:
        enum { IDD = IDD_PROPPAGECOMPLETE };

        BEGIN_MSG_MAP(CCompleteSettingsPage)
            CHAIN_MSG_MAP(CPropertyPageImpl<CCompleteSettingsPage>)
        END_MSG_MAP()
    };


    void runWizard()
    {
        PROPSHEETHEADERW psh = { sizeof(psh), 0 };
        psh.dwFlags = PSH_WIZARD97 | PSH_HEADER;
        psh.hInstance = _AtlBaseModule.GetResourceInstance();

        CExtractSettingsPage extractPage(this);
        CCompleteSettingsPage completePage(this);
        HPROPSHEETPAGE hpsp[] =
        {
            extractPage.Create(),
            completePage.Create()
        };

        psh.phpage = hpsp;
        psh.nPages = _countof(hpsp);

        PropertySheetW(&psh);
    }

    bool Extract(HWND hDlg, HWND hProgress)
    {
        unz_global_info64 gi;
        uf = unzOpen2_64(m_Filename.GetString(), &g_FFunc);
        int err = unzGetGlobalInfo64(uf, &gi);
        if (err != UNZ_OK)
        {
            DPRINT1("ERROR, unzGetGlobalInfo64: 0x%x\n", err);
            Close();
            return false;
        }

        CZipEnumerator zipEnum;
        if (!zipEnum.initialize(this))
        {
            DPRINT1("ERROR, zipEnum.initialize\n");
            Close();
            return false;
        }

        CWindow Progress(hProgress);
        Progress.SendMessage(PBM_SETRANGE32, 0, gi.number_entry);
        Progress.SendMessage(PBM_SETPOS, 0, 0);

        BYTE Buffer[2048];
        CStringA BaseDirectory = m_Directory;
        CStringA Name;
        unz_file_info64 Info;
        int CurrentFile = 0;
        bool bOverwriteAll = false;
        while (zipEnum.next(Name, Info))
        {
            bool is_dir = Name.GetLength() > 0 && Name[Name.GetLength()-1] == '/';

            char CombinedPath[MAX_PATH * 2] = { 0 };
            PathCombineA(CombinedPath, BaseDirectory, Name);
            CStringA FullPath = CombinedPath;
            FullPath.Replace('/', '\\');    /* SHPathPrepareForWriteA does not handle '/' */
            DWORD dwFlags = SHPPFW_DIRCREATE | (is_dir ? SHPPFW_NONE : SHPPFW_IGNOREFILENAME);
            HRESULT hr = SHPathPrepareForWriteA(hDlg, NULL, FullPath, dwFlags);
            if (FAILED_UNEXPECTEDLY(hr))
            {
                Close();
                return false;
            }
            CurrentFile++;
            if (is_dir)
                continue;

            const char* password = NULL;
            /* FIXME: Process password, if required and not specified, prompt the user */
            err = unzOpenCurrentFilePassword(uf, password);
            if (err != UNZ_OK)
            {
                DPRINT1("ERROR, unzOpenCurrentFilePassword: 0x%x\n", err);
                Close();
                return false;
            }

            HANDLE hFile = CreateFileA(FullPath, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hFile == INVALID_HANDLE_VALUE)
            {
                DWORD dwErr = GetLastError();
                if (dwErr == ERROR_FILE_EXISTS)
                {
                    bool bOverwrite = bOverwriteAll;
                    if (!bOverwriteAll)
                    {
                        CConfirmReplace::DialogResult Result = CConfirmReplace::ShowDlg(hDlg, FullPath);
                        switch (Result)
                        {
                        case CConfirmReplace::YesToAll:
                            bOverwriteAll = true;
                        case CConfirmReplace::Yes:
                            bOverwrite = true;
                            break;
                        case CConfirmReplace::No:
                            break;
                        case CConfirmReplace::Cancel:
                            unzCloseCurrentFile(uf);
                            Close();
                            return false;
                        }
                    }

                    if (bOverwrite)
                    {
                        hFile = CreateFileA(FullPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                        if (hFile == INVALID_HANDLE_VALUE)
                        {
                            dwErr = GetLastError();
                        }
                    }
                    else
                    {
                        unzCloseCurrentFile(uf);
                        continue;
                    }
                }
                if (hFile == INVALID_HANDLE_VALUE)
                {
                    unzCloseCurrentFile(uf);
                    DPRINT1("ERROR, CreateFileA: 0x%x (%s)\n", dwErr, bOverwriteAll ? "Y" : "N");
                    Close();
                    return false;
                }
            }

            do
            {
                err = unzReadCurrentFile(uf, Buffer, sizeof(Buffer));

                if (err < 0)
                {
                    DPRINT1("ERROR, unzReadCurrentFile: 0x%x\n", err);
                    break;
                }
                else if (err > 0)
                {
                    DWORD dwWritten;
                    if (!WriteFile(hFile, Buffer, err, &dwWritten, NULL))
                    {
                        DPRINT1("ERROR, WriteFile: 0x%x\n", GetLastError());
                        break;
                    }
                    if (dwWritten != (DWORD)err)
                    {
                        DPRINT1("ERROR, WriteFile: dwWritten:%d err:%d\n", dwWritten, err);
                        break;
                    }
                }

            } while (err > 0);

            /* Update Filetime */
            FILETIME LastAccessTime;
            GetFileTime(hFile, NULL, &LastAccessTime, NULL);
            FILETIME LocalFileTime;
            DosDateTimeToFileTime((WORD)(Info.dosDate >> 16), (WORD)Info.dosDate, &LocalFileTime);
            FILETIME FileTime;
            LocalFileTimeToFileTime(&LocalFileTime, &FileTime);
            SetFileTime(hFile, &FileTime, &LastAccessTime, &FileTime);

            /* Done.. */
            CloseHandle(hFile);

            if (err)
            {
                unzCloseCurrentFile(uf);
                DPRINT1("ERROR, unzReadCurrentFile2: 0x%x\n", err);
                Close();
                return false;
            }
            else
            {
                err = unzCloseCurrentFile(uf);
                if (err != UNZ_OK)
                {
                    DPRINT1("ERROR(non-fatal), unzCloseCurrentFile: 0x%x\n", err);
                }
            }
            Progress.SendMessage(PBM_SETPOS, CurrentFile, 0);
        }

        Close();
        return true;
    }
};


void _CZipExtract_runWizard(PCWSTR Filename)
{
    CZipExtract extractor(Filename);
    extractor.runWizard();
}

