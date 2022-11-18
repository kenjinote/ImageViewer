#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "WindowsCodecs.lib")
#pragma comment(lib, "SHLWAPI.LIB")

#include <windows.h>
#include <wincodec.h>
#include <shlwapi.h>
#include <d2d1.h>
#include <d2d1helper.h>

const FLOAT DEFAULT_DPI = 96.f;
TCHAR szClassName[] = TEXT("ImageViewer");
static IWICImagingFactory* m_pIWICFactory;
static ID2D1Factory* m_pD2DFactory;
static ID2D1HwndRenderTarget* m_pRT;
static ID2D1Bitmap* m_pD2DBitmap;
static IWICFormatConverter* m_pConvertedSourceBitmap;

template <typename T>
inline void SafeRelease(T*& p)
{
	if (NULL != p)
	{
		p->Release();
		p = NULL;
	}
}

HRESULT CreateDeviceResources(HWND hWnd)
{
	HRESULT hr = S_OK;

	if (!m_pRT)
	{
		RECT rc;
		hr = GetClientRect(hWnd, &rc) ? S_OK : E_FAIL;

		if (SUCCEEDED(hr))
		{
			// Create a D2D render target properties
			D2D1_RENDER_TARGET_PROPERTIES renderTargetProperties = D2D1::RenderTargetProperties();

			// Set the DPI to be the default system DPI to allow direct mapping
			// between image pixels and desktop pixels in different system DPI settings
			renderTargetProperties.dpiX = DEFAULT_DPI;
			renderTargetProperties.dpiY = DEFAULT_DPI;

			// Create a D2D render target
			D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);

			hr = m_pD2DFactory->CreateHwndRenderTarget(
				renderTargetProperties,
				D2D1::HwndRenderTargetProperties(hWnd, size),
				&m_pRT
			);
		}
	}

	return hr;
}

BOOL LocateImageFile(HWND hWnd, LPWSTR pszFileName, DWORD cchFileName)
{
	pszFileName[0] = L'\0';

	OPENFILENAME ofn;
	ZeroMemory(&ofn, sizeof(ofn));

	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hWnd;
	ofn.lpstrFilter = L"All Image Files\0"
		L"*.bmp;*.dib;*.wdp;*.mdp;*.hdp;*.gif;*.png;*.jpg;*.jpeg;*.tif;*.ico;*.webp\0"
		L"Windows Bitmap\0"               L"*.bmp;*.dib\0"
		L"High Definition Photo\0"        L"*.wdp;*.mdp;*.hdp\0"
		L"Graphics Interchange Format\0"  L"*.gif\0"
		L"Portable Network Graphics\0"    L"*.png\0"
		L"JPEG File Interchange Format\0" L"*.jpg;*.jpeg\0"
		L"Tiff File\0"                    L"*.tif\0"
		L"Icon\0"                         L"*.ico\0"
		L"All Files\0"                    L"*.*\0"
		L"\0";
	ofn.lpstrFile = pszFileName;
	ofn.nMaxFile = cchFileName;
	ofn.lpstrTitle = L"Open Image";
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

	// Display the Open dialog box. 
	return GetOpenFileName(&ofn);
}

HRESULT OpenImageFile(HWND hWnd, LPCWSTR lpszFilePath)
{
	HRESULT hr = S_OK;

	// Step 1: Create the open dialog box and locate the image file
	if (PathFileExists(lpszFilePath))
	{
		// Step 2: Decode the source image

		// Create a decoder
		IWICBitmapDecoder* pDecoder = NULL;

		hr = m_pIWICFactory->CreateDecoderFromFilename(
			lpszFilePath,                      // Image to be decoded
			NULL,                            // Do not prefer a particular vendor
			GENERIC_READ,                    // Desired read access to the file
			WICDecodeMetadataCacheOnDemand,  // Cache metadata when needed
			&pDecoder                        // Pointer to the decoder
		);

		// Retrieve the first frame of the image from the decoder
		IWICBitmapFrameDecode* pFrame = NULL;

		if (SUCCEEDED(hr))
		{
			hr = pDecoder->GetFrame(0, &pFrame);
		}

		//Step 3: Format convert the frame to 32bppPBGRA
		if (SUCCEEDED(hr))
		{
			SafeRelease(m_pConvertedSourceBitmap);
			hr = m_pIWICFactory->CreateFormatConverter(&m_pConvertedSourceBitmap);
		}

		if (SUCCEEDED(hr))
		{
			hr = m_pConvertedSourceBitmap->Initialize(
				pFrame,                          // Input bitmap to convert
				GUID_WICPixelFormat32bppPBGRA,   // Destination pixel format
				WICBitmapDitherTypeNone,         // Specified dither pattern
				NULL,                            // Specify a particular palette 
				0.f,                             // Alpha threshold
				WICBitmapPaletteTypeCustom       // Palette translation type
			);
		}

		//Step 4: Create render target and D2D bitmap from IWICBitmapSource
		if (SUCCEEDED(hr))
		{
			hr = CreateDeviceResources(hWnd);
		}

		if (SUCCEEDED(hr))
		{
			// Need to release the previous D2DBitmap if there is one
			SafeRelease(m_pD2DBitmap);
			hr = m_pRT->CreateBitmapFromWicBitmap(m_pConvertedSourceBitmap, NULL, &m_pD2DBitmap);
		}

		SafeRelease(pDecoder);
		SafeRelease(pFrame);

		InvalidateRect(hWnd, 0, TRUE);
	}

	return hr;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_CREATE:
	{
		HRESULT hr = S_OK;
		// Create WIC factory
		hr = CoCreateInstance(
			CLSID_WICImagingFactory,
			NULL,
			CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&m_pIWICFactory)
		);

		if (SUCCEEDED(hr))
		{
			// Create D2D factory
			hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_pD2DFactory);
		}
		DragAcceptFiles(hWnd, TRUE);
		break;
	}
	case WM_DROPFILES:
	{
		WCHAR szFilePath[MAX_PATH];
		UINT uFileNo = DragQueryFile((HDROP)wParam, 0xFFFFFFFF, NULL, 0);
		DragQueryFile((HDROP)wParam, 0, szFilePath, _countof(szFilePath));
		DragFinish((HDROP)wParam);
		OpenImageFile(hWnd, szFilePath);
		break;
	}
	case WM_SIZE:
	{
		D2D1_SIZE_U size = D2D1::SizeU(LOWORD(lParam), HIWORD(lParam));

		if (m_pRT)
		{
			// If we couldn't resize, release the device and we'll recreate it
			// during the next render pass.
			if (FAILED(m_pRT->Resize(size)))
			{
				SafeRelease(m_pRT);
				SafeRelease(m_pD2DBitmap);
			}
		}
		break;
	}
	case WM_PAINT:
	{
		HRESULT hr = S_OK;
		PAINTSTRUCT ps;

		if (BeginPaint(hWnd, &ps))
		{
			// Create render target if not yet created
			hr = CreateDeviceResources(hWnd);

			if (SUCCEEDED(hr) && !(m_pRT->CheckWindowState() & D2D1_WINDOW_STATE_OCCLUDED))
			{
				m_pRT->BeginDraw();

				m_pRT->SetTransform(D2D1::Matrix3x2F::Identity());

				// Clear the background
				m_pRT->Clear(D2D1::ColorF(D2D1::ColorF::White));

				// D2DBitmap may have been released due to device loss. 
				// If so, re-create it from the source bitmap
				if (m_pConvertedSourceBitmap && !m_pD2DBitmap)
				{
					m_pRT->CreateBitmapFromWicBitmap(m_pConvertedSourceBitmap, NULL, &m_pD2DBitmap);
				}

				D2D1_SIZE_F rtClientSize = m_pRT->GetSize();
				// Draws an image and scales it to the current window size
				if (m_pD2DBitmap && rtClientSize.width > 0 && rtClientSize.height > 0)
				{
					D2D1_SIZE_F rtBitmapSize = m_pD2DBitmap->GetSize();
					D2D1_RECT_F rectangle;
					if (rtBitmapSize.width / rtBitmapSize.height > rtClientSize.width / rtClientSize.height) {
						float zoom = rtClientSize.width / rtBitmapSize.width;
						float x = 0.0f;
						float y = (rtClientSize.height - zoom * rtBitmapSize.height) / 2.0F;
						rectangle = D2D1::RectF(x, y, x + rtClientSize.width, y + zoom * rtBitmapSize.height);
					}
					else {
						float zoom = rtClientSize.height / rtBitmapSize.height;
						float x = (rtClientSize.width - zoom * rtBitmapSize.width) / 2.0F;
						float y = 0.0f;
						rectangle = D2D1::RectF(x, y, x + zoom * rtBitmapSize.width, y + rtClientSize.height);
					}
					m_pRT->DrawBitmap(m_pD2DBitmap, rectangle);
				}

				hr = m_pRT->EndDraw();

				// In case of device loss, discard D2D render target and D2DBitmap
				// They will be re-create in the next rendering pass
				if (hr == D2DERR_RECREATE_TARGET)
				{
					SafeRelease(m_pD2DBitmap);
					SafeRelease(m_pRT);
					// Force a re-render
					hr = InvalidateRect(hWnd, NULL, TRUE) ? S_OK : E_FAIL;
				}
			}

			EndPaint(hWnd, &ps);
		}
		return SUCCEEDED(hr) ? 0 : 1;
	}
	case WM_DESTROY:
		SafeRelease(m_pD2DBitmap);
		SafeRelease(m_pConvertedSourceBitmap);
		SafeRelease(m_pIWICFactory);
		SafeRelease(m_pD2DFactory);
		SafeRelease(m_pRT);
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
	return 0;
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nShowCmd)
{
	HeapSetInformation(NULL, HeapEnableTerminationOnCorruption, NULL, 0);

	HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

	MSG msg;
	WNDCLASS wndclass = {
		CS_HREDRAW | CS_VREDRAW,
		WndProc,
		0,
		0,
		hInstance,
		0,
		LoadCursor(0,IDC_ARROW),
		(HBRUSH)(COLOR_WINDOW + 1),
		0,
		szClassName
	};
	RegisterClass(&wndclass);
	HWND hWnd = CreateWindow(
		szClassName,
		TEXT("ImageViewer"),
		WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
		CW_USEDEFAULT,
		0,
		CW_USEDEFAULT,
		0,
		0,
		0,
		hInstance,
		0
	);
	ShowWindow(hWnd, SW_SHOWDEFAULT);
	UpdateWindow(hWnd);
	while (GetMessage(&msg, 0, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	CoUninitialize();
	return (int)msg.wParam;
}
