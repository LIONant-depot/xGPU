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
#pragma once

namespace gbed_filebrowser
{
    std::filesystem::path BrowseFile( const std::filesystem::path src_path, bool bLoading, const wchar_t* pStr = L"" );
}
