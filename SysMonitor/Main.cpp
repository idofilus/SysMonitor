#include <Windows.h>
#include <vector>
#include <bitset>
#include <iomanip> // setprecision
#include <sstream> // stringstream

#define WINDOW_TITLE "SysMonitor"
#define WINDOW_CLASS "SysMonitor"
#define WINDOW_BACKGROUND_COLOR RGB(30, 30, 30)
#define WINDOW_TEXT_COLOR RGB(245, 245, 245)
#define WINDOW_LIGHT_TEXT_COLOR RGB(175, 175, 175)
#define WINDOW_OPACITY 95

using namespace std;

namespace Colors
{
	static COLORREF red = RGB(0xf4, 0x43, 0x36);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

// https://stackoverflow.com/questions/4400747/converting-from-unsigned-long-long-to-float-with-round-to-nearest-even
const unsigned long long mask_bit_count = 31;
float ull_to_float(unsigned long long val)
{
	// How many bits are needed?
	int b = sizeof(unsigned long long) * CHAR_BIT - 1;
	for (; b >= 0; --b)
	{
		if (val & (1ull << b))
		{
			break;
		}
	}

	// If there are few enough significant bits, use normal cast and done.
	if (b < mask_bit_count)
	{
		return static_cast<float>(val & ~1ull);
	}

	// Save off the low-order useless bits:
	unsigned long long low_bits = val & ((1ull << (b - mask_bit_count)) - 1);

	// Now mask away those useless low bits:
	val &= ~((1ull << (b - mask_bit_count)) - 1);

	// Finally, decide how to round the new LSB:
	if (low_bits > ((1ull << (b - mask_bit_count)) / 2ull))
	{
		// Round up.
		val |= (1ull << (b - mask_bit_count));
	}
	else
	{
		// Round down.
		val &= ~(1ull << (b - mask_bit_count));
	}

	return static_cast<float>(val);
}

struct DriveSpaceInfo
{
	ULONGLONG total;
	ULONGLONG used;
	ULONGLONG free;

	static string get(ULONGLONG value)
	{
		stringstream stream;
		stream << fixed << setprecision(2);

		float fval = ull_to_float(value);

		if (value >= (1024 * 3)) // GB
			stream << (fval / 1024 / 1024 / 1024) << " GB";
		else if (value >= (1024 * 2)) // MB
			stream << (fval / 1024 / 1024) << " MB";
		else if (value >= (1024 * 1)) // KH
			stream << (fval / 1024) << " KB";
		else 
			stream << (fval) << " K";

		return stream.str();
	}
};

struct DriveInfo
{
	char letter;
	DriveSpaceInfo* space = new DriveSpaceInfo();

	void update()
	{
		// Space information
		ULARGE_INTEGER availableToCaller;
		ULARGE_INTEGER disk;
		ULARGE_INTEGER free;

		string drive = string(1, letter) + ":\\";

		GetDiskFreeSpaceExA(drive.c_str(), &availableToCaller, &disk, &free);

		space->total = disk.QuadPart;
		space->used = disk.QuadPart - free.QuadPart;
		space->free= free.QuadPart;
	}
};

struct SysInfo
{
	DWORD dwDrives;
	vector<DriveInfo*> drives = {};
};

shared_ptr<SysInfo> sysInfo;
HWND hWindow = NULL;

DWORD WINAPI BackgroundThread(LPVOID lpszReserved)
{
	while (true)
	{
		for (auto& drive : sysInfo->drives)
		{
			drive->update();
		}

		InvalidateRect(hWindow, 0, TRUE);

		Sleep(1000 * 30);
	}

	return 0;
}

struct Window
{
	int x;
	int y;
	RECT rect = { NULL };
	POINT p = { NULL };
	POINT dragStart = { NULL };

	Window(HWND hWnd)
	{
		if (GetWindowRect(hWnd, &rect))
		{
			x = rect.left;
			y = rect.top;
		}
	}

	void start(int x, int y)
	{
		dragStart.x = x;
		dragStart.y = y;
	}

	void update(int x, int y)
	{
		if (GetCursorPos(&p))
		{
			int offsetX = p.x - this->x;
			int offsetY = p.y - this->y;

			this->x += offsetX;
			this->y += offsetY;

			this->x -= dragStart.x;
			this->y -= dragStart.y;

			SetWindowPos(hWindow, HWND_TOPMOST, this->x, this->y, NULL, NULL, SWP_NOSIZE);
		}
	}
};

unique_ptr<Window> window;

namespace MouseMovement
{
	void OnDragStart(int x, int y)
	{
		window->start(x, y);
	}

	void OnDragMovement(int x, int y)
	{
		window->update(x, y);
	}
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	WNDCLASSEX wcex;
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPLICATION));
	wcex.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPLICATION));
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszMenuName = NULL;
	wcex.lpszClassName = WINDOW_CLASS;
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

	if (!RegisterClassEx(&wcex))
	{
		MessageBox(NULL, "Failed to register the window class", WINDOW_TITLE, MB_ICONERROR);
		return 1;
	}

	HWND hWnd = CreateWindow(WINDOW_CLASS, WINDOW_TITLE, WS_POPUPWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 350, 25, NULL, NULL, hInstance, NULL);

	if (!hWnd)
	{
		MessageBox(NULL, "Failed to create the window", WINDOW_TITLE, MB_ICONERROR);
		return 1;
	}

	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return (int)msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	HDC hdc;
	HFONT hFont, hOldFont;
	PAINTSTRUCT ps;

	switch (uMsg)
	{
		case WM_CREATE:
		{
			hWindow = hWnd;
			window = make_unique<Window>(hWindow);

			// Set window background color
			HBRUSH brush = CreateSolidBrush(WINDOW_BACKGROUND_COLOR);
			SetClassLongPtr(hWnd, GCLP_HBRBACKGROUND, (LONG)brush);

			// Set the window opacity 
			SetWindowLong(hWnd, GWL_EXSTYLE, GetWindowLong(hWnd, GWL_EXSTYLE) | WS_EX_LAYERED);
			SetLayeredWindowAttributes(hWnd, 0, (255 * WINDOW_OPACITY) / 100, LWA_ALPHA);

			sysInfo = make_shared<SysInfo>();
			sysInfo->dwDrives = GetLogicalDrives();
			bitset<32> drives(sysInfo->dwDrives);

			for (int i = 0, c = 0; i < 32 && c < drives.count(); i++)
			{
				if (sysInfo->dwDrives & (1 << i))
				{
					c++;

					DriveInfo* driveInfo = new DriveInfo();
					driveInfo->letter = i + 65;
					driveInfo->update();
					sysInfo->drives.push_back(driveInfo);
				}
			}

			CreateThread(NULL, NULL, BackgroundThread, NULL, NULL, NULL);

			break;
		}

		case WM_PAINT:
		{
			hdc = BeginPaint(hWnd, &ps);
			SetBkColor(hdc, WINDOW_BACKGROUND_COLOR);
			static long lfHeight = -MulDiv(10, GetDeviceCaps(hdc, LOGPIXELSY), 72);
			hFont = CreateFont(lfHeight, 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, DRAFT_QUALITY, VARIABLE_PITCH, TEXT("Arial"));
			hOldFont = (HFONT)SelectObject(hdc, hFont);

			static int textSize = 7;
			static int marginRight = 5;
			static int y = 4;

			int leftOffset = 10;

			for (auto drive : sysInfo->drives)
			{
				// Drive letter
				string letter = string(1, drive->letter) + ":";
				SetTextColor(hdc, WINDOW_LIGHT_TEXT_COLOR);
				TextOut(hdc, leftOffset, y, letter.c_str(), letter.size());
				leftOffset += letter.size() * textSize + marginRight;

				// Drive free space
				string free = DriveSpaceInfo::get(drive->space->free);

				// If less than 5% is free, make it red
				if (ull_to_float(drive->space->free) / drive->space->total < 0.05f)
					SetTextColor(hdc, Colors::red);
				else
					SetTextColor(hdc, WINDOW_TEXT_COLOR);

				TextOut(hdc, leftOffset, y, free.c_str(), free.size());
				leftOffset += free.size() * textSize + marginRight;

				leftOffset += (marginRight * 2);
			}

			SelectObject(hdc, hOldFont);
			DeleteObject(hFont);

			EndPaint(hWnd, &ps);
			break;
		}

		case WM_LBUTTONDOWN:
		{
			if (wParam == MK_LBUTTON)
				MouseMovement::OnDragStart(LOWORD(lParam), HIWORD(lParam));

			break;
		}

		case WM_MOUSEMOVE:
		{
			if (wParam == MK_LBUTTON)
				MouseMovement::OnDragMovement(LOWORD(lParam), HIWORD(lParam));

			break;
		}

		case WM_DESTROY:
		{
			PostQuitMessage(0);
			break;
		}

		default:
			return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}

	return 0;
}
