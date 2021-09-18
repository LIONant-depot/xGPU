
#include "xcore.h"
#define NOMINMAX
#include <windows.h>

#include <string>
#include <shlobj.h>
#include <iostream>
#include <sstream>
#include <cstring>
#include <iostream>

#include "FolderBrowser.h"

namespace gbed_filebrowser
{
    // Referenced from
    // https://stackoverflow.com/questions/12034943/win32-select-directory-dialog-from-c-c

    static int CALLBACK BrowseCallbackProc(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData)
    {

        if (uMsg == BFFM_INITIALIZED)
        {
            std::string tmp = (const char *)lpData;
            std::cout << "path: " << tmp << std::endl;
            SendMessage(hwnd, BFFM_SETSELECTION, TRUE, lpData);
        }

        return 0;
    }

    std::filesystem::path BrowseFolder( const std::filesystem::path saved_path)
    {
        TCHAR path[512];

        const TCHAR * path_param = saved_path.c_str();

        BROWSEINFO bi = { 0 };
        bi.lpszTitle = TEXT("Browse for folder...");
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        bi.lpfn = BrowseCallbackProc;
        bi.lParam = (LPARAM)path_param;

        LPITEMIDLIST pidl = SHBrowseForFolder (&bi);

        if (pidl != 0)
        {
            //get the name of the folder and put it in path
            SHGetPathFromIDList (pidl, path);

            //free memory used
            IMalloc * imalloc = 0;
            if (SUCCEEDED(SHGetMalloc (&imalloc)))
            {
                imalloc->Free (pidl);
                imalloc->Release ();
            }

            return path;
        }

        return "";
    }
}