//----------------------------------------------------------------------------//
//  xCORE -- Copyright @ 2010 - 2016 LIONant LLC.                             //
//----------------------------------------------------------------------------//
// This source file is part of the LIONant core library and it is License     //
// under Apache License v2.0 with Runtime Library Exception.                  //
// You may not use this file except in compliance with the License.           //
// You may obtain a copy of the License at:                                   //
// http://www.apache.org/licenses/LICENSE-2.0                                 //
//                                                                            //
// The above copyright notice and this permission notice shall be included in //
// all copies or substantial portions of the Software.                        //
//                                                                            //
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR //
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   //
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL    //
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER //
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING    //
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER        //
// DEALINGS IN THE SOFTWARE.                                                  //
//----------------------------------------------------------------------------//
#define NOMINMAX
#include <windows.h>
#include <string>
#include <shlobj.h>
#include <iostream>
#include <sstream>
#include <cstring>
#include <iostream>

#include "xcore.h"
#include "FileBrowser.h"

namespace gbed_filebrowser
{
    // Referenced from
    // https://msdn.microsoft.com/en-us/library/windows/desktop/ms646829(v=vs.85).aspx#open_file

    std::filesystem::path BrowseFile( const std::filesystem::path src_path, bool bLoading, const wchar_t* pStr  )
    {
        std::array<wchar_t,260>  szFile;         // buffer for file name

        //
        // Initialize OPENFILENAME
        //
        OPENFILENAME        ofn;            // common dialog box structure
        ZeroMemory(&ofn, sizeof(ofn));
        ofn.lStructSize             = sizeof(ofn);
        ofn.hwndOwner               = nullptr;
        ofn.nMaxFile                = sizeof(szFile);
        ofn.lpstrFile               = szFile.data();

        if( src_path.empty() )
        {
            // Set lpstrFile[0] to '\0' so that GetOpenFileName does not 
            // use the contents of szFile to initialize itself.
            szFile[0] = 0;
        }
        else
        {
            std::wstring        Path{src_path};

            xcore::string::Copy( xcore::string::view{ szFile.data(), szFile.size() }, &Path[0] );

            if( src_path.has_filename() && src_path.has_extension() )
            {
                int Index = xcore::string::FindStr( szFile.data(), &std::wstring{ src_path.filename()}[0] );
                ofn.lpstrFile               = &szFile[Index];
            }
        }

        ofn.lpstrFilter             = pStr;
        ofn.nFilterIndex            = 1;
        ofn.lpstrFileTitle          = nullptr;
        ofn.nMaxFileTitle           = 0;
        ofn.lpstrInitialDir         = &szFile[0];

        if( bLoading )
        {
            ofn.Flags               = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
        }
        else
        {
            ofn.Flags               = OFN_PATHMUSTEXIST;
        }

        //
        // Display the Open dialog box. 
        //
        if ( GetOpenFileName(&ofn) == TRUE )
        {
            return ofn.lpstrFile;
        }

        return "";
    }
}