// Copyright © 2010-2014 The CefSharp Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#pragma once

#include "Stdafx.h"
#include "BrowserSettings.h"
#include "MouseButtonType.h"
#include "PaintElementType.h"
#include "Internals/ClientAdapter.h"
#include "Internals/RenderClientAdapter.h"
#include "Internals/MCefRefPtr.h"
#include "Internals/StringVisitor.h"

using namespace CefSharp::Internals;
using namespace System::Diagnostics;
using namespace System::ServiceModel;
using namespace System::Threading::Tasks;

namespace CefSharp
{
    public ref class ManagedCefBrowserAdapter : public DisposableResource
    {
        MCefRefPtr<ClientAdapter> _clientAdapter;
        BrowserProcessServiceHost^ _browserProcessServiceHost;
        IWebBrowserInternal^ _webBrowserInternal;
        JavascriptObjectRepository^ _javaScriptObjectRepository;

    protected:
        virtual void DoDispose(bool isDisposing) override
        {
            CloseAllPopups(true);
            Close(true);

            _clientAdapter = nullptr;
            if (_browserProcessServiceHost != nullptr)
            {
                _browserProcessServiceHost->Close();
                _browserProcessServiceHost = nullptr;
            }

            _webBrowserInternal = nullptr;
            _javaScriptObjectRepository = nullptr;

            DisposableResource::DoDispose(isDisposing);
        };

    public:
        ManagedCefBrowserAdapter(IWebBrowserInternal^ webBrowserInternal, bool offScreenRendering)
        {
            if (offScreenRendering)
            {
                _clientAdapter = new RenderClientAdapter(webBrowserInternal,
                    gcnew Action<int>(this, &ManagedCefBrowserAdapter::OnAfterBrowserCreated));
            }
            else
            {
                _clientAdapter = new ClientAdapter(webBrowserInternal,
                    gcnew Action<int>(this, &ManagedCefBrowserAdapter::OnAfterBrowserCreated));
            }

            _webBrowserInternal = webBrowserInternal;
            _javaScriptObjectRepository = gcnew JavascriptObjectRepository();
        }

        void CreateOffscreenBrowser(IntPtr^ windowHandle, BrowserSettings^ browserSettings, String^ address)
        {
            auto hwnd = static_cast<HWND>(windowHandle->ToPointer());

            CefWindowInfo window;
            window.SetAsWindowless(hwnd, TRUE);
            CefString addressNative = StringUtils::ToNative(address);

            if (!CefBrowserHost::CreateBrowser(window, _clientAdapter.get(), addressNative,
                *(CefBrowserSettings*) browserSettings->_internalBrowserSettings, NULL))
            {
                throw gcnew InvalidOperationException( "Failed to create offscreen browser. Call Cef.Initialize() first." );
            }
        }

        void Close(bool forceClose)
        {
            auto cefHost = _clientAdapter->TryGetCefHost();

            if (cefHost != nullptr)
            {
                cefHost->CloseBrowser(forceClose);
            }
        }

        void CloseAllPopups(bool forceClose)
        {
            _clientAdapter->CloseAllPopups(forceClose);
        }

        void LoadUrl(String^ address)
        {
            auto cefFrame = _clientAdapter->TryGetCefMainFrame();

            if (cefFrame != nullptr)
            {
                cefFrame->LoadURL(StringUtils::ToNative(address));
            }
        }

        void OnAfterBrowserCreated(int browserId)
        {
            _browserProcessServiceHost = gcnew BrowserProcessServiceHost(_javaScriptObjectRepository, Process::GetCurrentProcess()->Id, browserId);
            _browserProcessServiceHost->Open();

            if(_webBrowserInternal != nullptr)
            {
                _webBrowserInternal->OnInitialized();
            }
        }

        void LoadHtml(String^ html, String^ url)
        {
            auto cefFrame = _clientAdapter->TryGetCefMainFrame();

            if (cefFrame != nullptr)
            {
                cefFrame->LoadString(StringUtils::ToNative(html), StringUtils::ToNative(url));
            }
        }

        void WasResized()
        {
            auto cefHost = _clientAdapter->TryGetCefHost();

            if (cefHost != nullptr)
            {
                cefHost->WasResized();
            }
        }

        void WasHidden(bool hidden)
        {
            auto cefHost = _clientAdapter->TryGetCefHost();

            if (cefHost != nullptr)
            {
                cefHost->WasHidden(hidden);
            }
        }

        void Invalidate(PaintElementType type)
        {
            auto cefHost = _clientAdapter->TryGetCefHost();

            if (cefHost != nullptr)
            {
                cefHost->Invalidate((CefBrowserHost::PaintElementType)type);
            }
        }

        void SendFocusEvent(bool isFocused)
        {
            auto cefHost = _clientAdapter->TryGetCefHost();

            if (cefHost != nullptr)
            {
                cefHost->SendFocusEvent(isFocused);
            }
        }

        void SetFocus(bool isFocused)
        {
            auto cefHost = _clientAdapter->TryGetCefHost();

            if (cefHost != nullptr)
            {
                cefHost->SetFocus(isFocused);
            }
        }

        bool SendKeyEvent(int message, int wParam, int lParam)
        {
            auto cefHost = _clientAdapter->TryGetCefHost();

            if (cefHost == nullptr)
            {
                return false;
            }

            CefKeyEvent keyEvent;
            keyEvent.windows_key_code = wParam;
            keyEvent.native_key_code = lParam;
            keyEvent.is_system_key = message == WM_SYSCHAR ||
                message == WM_SYSKEYDOWN ||
                message == WM_SYSKEYUP;

            if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN)
            {
                keyEvent.type = KEYEVENT_RAWKEYDOWN;
            }
            else if (message == WM_KEYUP || message == WM_SYSKEYUP)
            {
                keyEvent.type = KEYEVENT_KEYUP;
            }
            else
            {
                keyEvent.type = KEYEVENT_CHAR;
            }
            keyEvent.modifiers = GetCefKeyboardModifiers(wParam, lParam);

            cefHost->SendKeyEvent(keyEvent);

            return true;
        }

        bool isKeyDown(WPARAM wparam)
        {
            return (GetKeyState(wparam) & 0x8000) != 0;
        }

        //Code imported from
        //https://bitbucket.org/chromiumembedded/branches-2062-cef3/src/a073e92426b3967f1fc2f1d3fd7711d809eeb03a/tests/cefclient/cefclient_osr_widget_win.cpp?at=master#cl-361
        int GetCefKeyboardModifiers(WPARAM wparam, LPARAM lparam)
        {
            int modifiers = 0;
            if (isKeyDown(VK_SHIFT))
                modifiers |= EVENTFLAG_SHIFT_DOWN;
            if (isKeyDown(VK_CONTROL))
                modifiers |= EVENTFLAG_CONTROL_DOWN;
            if (isKeyDown(VK_MENU))
                modifiers |= EVENTFLAG_ALT_DOWN;

            // Low bit set from GetKeyState indicates "toggled".
            if (::GetKeyState(VK_NUMLOCK) & 1)
                modifiers |= EVENTFLAG_NUM_LOCK_ON;
            if (::GetKeyState(VK_CAPITAL) & 1)
                modifiers |= EVENTFLAG_CAPS_LOCK_ON;

            switch (wparam)
            {
                case VK_RETURN:
                    if ((lparam >> 16) & KF_EXTENDED)
                        modifiers |= EVENTFLAG_IS_KEY_PAD;
                    break;
                case VK_INSERT:
                case VK_DELETE:
                case VK_HOME:
                case VK_END:
                case VK_PRIOR:
                case VK_NEXT:
                case VK_UP:
                case VK_DOWN:
                case VK_LEFT:
                case VK_RIGHT:
                    if (!((lparam >> 16) & KF_EXTENDED))
                        modifiers |= EVENTFLAG_IS_KEY_PAD;
                    break;
                case VK_NUMLOCK:
                case VK_NUMPAD0:
                case VK_NUMPAD1:
                case VK_NUMPAD2:
                case VK_NUMPAD3:
                case VK_NUMPAD4:
                case VK_NUMPAD5:
                case VK_NUMPAD6:
                case VK_NUMPAD7:
                case VK_NUMPAD8:
                case VK_NUMPAD9:
                case VK_DIVIDE:
                case VK_MULTIPLY:
                case VK_SUBTRACT:
                case VK_ADD:
                case VK_DECIMAL:
                case VK_CLEAR:
                    modifiers |= EVENTFLAG_IS_KEY_PAD;
                    break;
                case VK_SHIFT:
                    if (isKeyDown(VK_LSHIFT))
                        modifiers |= EVENTFLAG_IS_LEFT;
                    else if (isKeyDown(VK_RSHIFT))
                        modifiers |= EVENTFLAG_IS_RIGHT;
                    break;
                case VK_CONTROL:
                    if (isKeyDown(VK_LCONTROL))
                        modifiers |= EVENTFLAG_IS_LEFT;
                    else if (isKeyDown(VK_RCONTROL))
                        modifiers |= EVENTFLAG_IS_RIGHT;
                    break;
                case VK_MENU:
                    if (isKeyDown(VK_LMENU))
                        modifiers |= EVENTFLAG_IS_LEFT;
                    else if (isKeyDown(VK_RMENU))
                        modifiers |= EVENTFLAG_IS_RIGHT;
                    break;
                case VK_LWIN:
                    modifiers |= EVENTFLAG_IS_LEFT;
                    break;
                case VK_RWIN:
                    modifiers |= EVENTFLAG_IS_RIGHT;
                    break;
            }
            return modifiers;
        }

        void OnMouseMove(int x, int y, bool mouseLeave, CefEventFlags modifiers)
        {
            auto cefHost = _clientAdapter->TryGetCefHost();

            if (cefHost != nullptr)
            {
                CefMouseEvent mouseEvent;
                mouseEvent.x = x;
                mouseEvent.y = y;

                mouseEvent.modifiers = (uint32)modifiers;

                cefHost->SendMouseMoveEvent(mouseEvent, mouseLeave);

                if (mouseLeave == true)
                {
                    _webBrowserInternal->SetTooltipText(nullptr);
                }
            }
        }

        void OnMouseButton(int x, int y, MouseButtonType mouseButtonType, bool mouseUp, int clickCount, CefEventFlags modifiers)
        {
            auto cefHost = _clientAdapter->TryGetCefHost();

            if (cefHost != nullptr)
            {
                CefMouseEvent mouseEvent;
                mouseEvent.x = x;
                mouseEvent.y = y;
                mouseEvent.modifiers = (uint32)modifiers;

                cefHost->SendMouseClickEvent(mouseEvent, (CefBrowserHost::MouseButtonType) mouseButtonType, mouseUp, clickCount);
            }
        }

        void OnMouseWheel(int x, int y, int deltaX, int deltaY)
        {
            auto cefHost = _clientAdapter->TryGetCefHost();

            if (cefHost != nullptr)
            {
                CefMouseEvent mouseEvent;
                mouseEvent.x = x;
                mouseEvent.y = y;

                cefHost->SendMouseWheelEvent(mouseEvent, deltaX, deltaY);
            }
        }

        void Stop()
        {
            auto cefBrowser = _clientAdapter->GetCefBrowser();

            if (cefBrowser != nullptr)
            {
                cefBrowser->StopLoad();
            }
        }

        void GoBack()
        {
            auto cefBrowser = _clientAdapter->GetCefBrowser();

            if (cefBrowser != nullptr)
            {
                cefBrowser->GoBack();
            }
        }

        void GoForward()
        {
            auto cefBrowser = _clientAdapter->GetCefBrowser();

            if (cefBrowser != nullptr)
            {
                cefBrowser->GoForward();
            }
        }

        void Print()
        {
            auto cefHost = _clientAdapter->TryGetCefHost();

            if (cefHost != nullptr)
            {
                cefHost->Print();
            }
        }

        void Find(int identifier, String^ searchText, bool forward, bool matchCase, bool findNext)
        {
            auto cefHost = _clientAdapter->TryGetCefHost();

            if (cefHost != nullptr)
            {
                cefHost->Find(identifier, StringUtils::ToNative(searchText), forward, matchCase, findNext);
            }
        }

        void StopFinding(bool clearSelection)
        {
            auto cefHost = _clientAdapter->TryGetCefHost();

            if (cefHost != nullptr)
            {
                cefHost->StopFinding(clearSelection);
            }
        }

        void Reload()
        {
            Reload(false);
        }

        void Reload(bool ignoreCache)
        {
            auto cefBrowser = _clientAdapter->GetCefBrowser();

            if (cefBrowser != nullptr)
            {
                if (ignoreCache)
                {
                    cefBrowser->ReloadIgnoreCache();
                }
                else
                {
                    cefBrowser->Reload();
                }
            }
        }

        void ViewSource()
        {
            auto cefFrame = _clientAdapter->TryGetCefMainFrame();

            if (cefFrame != nullptr)
            {
                cefFrame->ViewSource();
            }
        }

        void GetSource(IStringVisitor^ visitor)
        {
            auto cefFrame = _clientAdapter->TryGetCefMainFrame();

            if (cefFrame != nullptr)
            {
                auto stringVisitor = new StringVisitor(visitor);
                cefFrame->GetSource(stringVisitor);
            }
        }

        void GetText(IStringVisitor^ visitor)
        {
            auto cefFrame = _clientAdapter->TryGetCefMainFrame();

            if (cefFrame != nullptr)
            {
                auto stringVisitor = new StringVisitor(visitor);
                cefFrame->GetText(stringVisitor);
            }
        }

        void Cut()
        {
            auto cefFrame = _clientAdapter->TryGetCefMainFrame(); 

            if (cefFrame != nullptr)
            {
                cefFrame->Cut();
            }
        }

        void Copy()
        {
            auto cefFrame = _clientAdapter->TryGetCefMainFrame(); 

            if (cefFrame != nullptr)
            {
                cefFrame->Copy();
            }
        }

        void Paste()
        {
            auto cefFrame = _clientAdapter->TryGetCefMainFrame();

            if (cefFrame != nullptr)
            {
                cefFrame->Paste();
            }
        }

        void Delete()
        {
            auto cefFrame = _clientAdapter->TryGetCefMainFrame();

            if (cefFrame != nullptr)
            {
                cefFrame->Delete();
            }
        }

        void SelectAll()
        {
            auto cefFrame = _clientAdapter->TryGetCefMainFrame();

            if (cefFrame != nullptr)
            {
                cefFrame->SelectAll();
            }
        }

        void Undo()
        {
            auto cefFrame = _clientAdapter->TryGetCefMainFrame();

            if (cefFrame != nullptr)
            {
                cefFrame->Undo();
            }
        }

        void Redo()
        {
            auto cefFrame = _clientAdapter->TryGetCefMainFrame();

            if (cefFrame != nullptr)
            {
                cefFrame->Redo();
            }
        }

        void ExecuteScriptAsync(String^ script)
        {
            auto cefFrame = _clientAdapter->TryGetCefMainFrame();

            if (cefFrame != nullptr)
            {
                cefFrame->ExecuteJavaScript(StringUtils::ToNative(script), "about:blank", 0);
            }
        }

        Task<JavascriptResponse^>^ EvaluateScriptAsync(String^ script, Nullable<TimeSpan> timeout)
        {
            if (timeout.HasValue && timeout.Value.TotalMilliseconds > UInt32::MaxValue)
            {
                throw gcnew ArgumentOutOfRangeException("timeout", "Timeout greater than Maximum allowable value of " + UInt32::MaxValue);
            }

            auto browser = _clientAdapter->GetCefBrowser();


            if (_browserProcessServiceHost == nullptr && browser == nullptr)
            {
                return nullptr;
            }

            auto frame = browser->GetMainFrame();

            if (frame == nullptr)
            {
                return nullptr;
            }            

            return _browserProcessServiceHost->EvaluateScriptAsync(browser->GetIdentifier(), frame->GetIdentifier(), script, timeout);
        }

        double GetZoomLevel()
        {
            auto cefHost = _clientAdapter->TryGetCefHost();

            if (cefHost != nullptr)
            {
                return cefHost->GetZoomLevel();
            }

            return 0;
        }

        void SetZoomLevel(double zoomLevel)
        {
            auto cefHost = _clientAdapter->TryGetCefHost();

            if (cefHost != nullptr)
            {
                cefHost->SetZoomLevel(zoomLevel);
            }
        }

        void ShowDevTools()
        {
            _clientAdapter->ShowDevTools();
        }

        void CloseDevTools()
        {
            _clientAdapter->CloseDevTools();
        }

        void CreateBrowser(BrowserSettings^ browserSettings, IntPtr^ sourceHandle, String^ address)
        {
            HWND hwnd = static_cast<HWND>(sourceHandle->ToPointer());
            RECT rect;
            GetClientRect(hwnd, &rect);
            CefWindowInfo window;
            window.SetAsChild(hwnd, rect);
            CefString addressNative = StringUtils::ToNative(address);

            CefBrowserHost::CreateBrowser(window, _clientAdapter.get(), addressNative,
                *(CefBrowserSettings*)browserSettings->_internalBrowserSettings, NULL);
        }

        void Resize(int width, int height)
        {
            HWND browserHwnd = _clientAdapter->GetBrowserHwnd();
            if (browserHwnd) 
            {
                if (width == 0 && height == 0) 
                {
                    // For windowed browsers when the frame window is minimized set the
                    // browser window size to 0x0 to reduce resource usage.
                    SetWindowPos(browserHwnd, NULL, 0, 0, 0, 0, SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
                }
                else 
                {
                    SetWindowPos(browserHwnd, NULL, 0, 0, width, height, SWP_NOZORDER);
                }
            }
        }

        void NotifyMoveOrResizeStarted()
        {
            auto cefHost = _clientAdapter->TryGetCefHost();

            if (cefHost != nullptr)
            {
                cefHost->NotifyMoveOrResizeStarted();
            }
        }

        void RegisterJsObject(String^ name, Object^ object)
        {
            _javaScriptObjectRepository->Register(name, object);
        }

        void ReplaceMisspelling(String^ word)
        {
            auto cefHost = _clientAdapter->TryGetCefHost();

            if (cefHost != nullptr)
            {
                CefString wordNative = StringUtils::ToNative(word);
                cefHost->ReplaceMisspelling(wordNative);
            }
        }

        void AddWordToDictionary(String^ word)
        {
            auto cefHost = _clientAdapter->TryGetCefHost();

            if (cefHost != nullptr)
            {
                CefString wordNative = StringUtils::ToNative(word);
                cefHost->AddWordToDictionary(wordNative);
            }
        }
    };
}
