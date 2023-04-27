#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <random>

#define SCREEN_WIDTH 1920
#define SCREEN_HEIGHT 1080

// pixel value looks like this: 0x__RRGGBB
#define WHITE 0x00ffffff
#define BLACK 0x00000000

#define internal static
#define local_persist static
#define global_variable static

global_variable bool running;

global_variable BITMAPINFO bitmap_info;

// look, we have two buffers!
// we swap the buffer to pass to display for every frame
global_variable void *bitmap_memory[2] = {NULL, NULL};
// index for the buffer we will manipulate then display
global_variable int active_buf_idx = 0;

global_variable int bitmap_width;
global_variable int bitmap_height;
global_variable int bytes_per_pixel = 4;

// check if we did some initial population for the screen
global_variable bool initialized = 0;

// make it windowed fullscreen
// in case you cry about not being able to switch windows
void set_full_screen(int width, int height)
{
  DEVMODE dmSettings;

  memset(&dmSettings,0,sizeof(dmSettings));

  // get current display settings
  if (!EnumDisplaySettings(NULL,ENUM_CURRENT_SETTINGS,&dmSettings)) {
    MessageBox(NULL, "I can't get the current display settings :(", "Error", MB_OK);
    return;
  }

  dmSettings.dmPelsWidth  = width;
  dmSettings.dmPelsHeight  = height;

  // flags saying what fields we're changing in the display settings
  dmSettings.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
  
  int result = ChangeDisplaySettings(&dmSettings, CDS_FULLSCREEN);  

  if (result != DISP_CHANGE_SUCCESSFUL) {
    MessageBox(NULL, "can't change the display settings :(", "Error", MB_OK);
    PostQuitMessage(0);
  }
}

uint32_t get_current_state(int row, int col) {
  int pitch = bitmap_width * bytes_per_pixel;

  uint8_t *current_row = (uint8_t *)bitmap_memory[active_buf_idx ^ 1];

  current_row += pitch * row;

  uint32_t *pixel = (uint32_t *)current_row + col;

  return *pixel;
}

uint32_t get_alive_neighbours(int row, int col) {
  const int dr[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
  const int dc[8] = {-1, 0, 1, -1, 1, -1, 0, 1};

  uint32_t count = 0;

  for (int i = 0; i < 8; i++) {
    int nr = dr[i] + row;
    int nc = dc[i] + col;

    if (nr < 0 || nr >= bitmap_height)
      continue;
    if (nc < 0 || nc >= bitmap_width)
      continue;

    count += (get_current_state(nr, nc) == WHITE);
  }

  return count;
}

uint32_t get_next_state(int row, int col)
{
  uint32_t alive = get_current_state(row, col);
  uint32_t num_neighbours = get_alive_neighbours(row, col);

  // if not alive, be alive if you have 3 neighbours => reproduction
  if (!alive)
    return (num_neighbours == 3) ? WHITE : BLACK;

  // if alive, stay alive iff you have 2 or 3 neighbours => survive to next generation
  switch(num_neighbours) {
    case 2:
    case 3:
      return WHITE;
    default:
      // less than 2 is underpopulation, more than 3 is overpopulation
      return BLACK;
  }
}

internal void initialize_screen()
{
  int pitch = bitmap_width * bytes_per_pixel;
  uint8_t *row = (uint8_t *)bitmap_memory[active_buf_idx];

  // srand and rand shows some weird patterns on screen, so it's not enough
  // mersenne twister is a much better randomizer
  std::mt19937 mt(time(nullptr));

  for (int y = 0; y < bitmap_height; y++) {
    uint32_t *pixel = (uint32_t *)row;
    for (int x = 0; x < bitmap_width; x++) {
      // *pixel = (mt() % 2) ? WHITE : BLACK;
      *pixel = (mt() % 2) * WHITE;
      pixel++;
    }
    row += pitch;
  }
}

internal void render_screen()
{
  if (!initialized) {
    initialize_screen();
    initialized = true;
    return;
  }

  active_buf_idx ^= 1;
  int pitch = bitmap_width * bytes_per_pixel;
  uint8_t *row = (uint8_t *)bitmap_memory[active_buf_idx];

  for (int y = 0; y < bitmap_height; y++) {
    uint32_t *pixel = (uint32_t *)row;
    for (int x = 0; x < bitmap_width; x++) {
      *pixel = get_next_state(y, x);

      pixel++;
    }
    row += pitch;
  }
}

internal void ResizeDBISection(int width, int height)
{
  if (bitmap_memory[0]) {
    VirtualFree(bitmap_memory[0], 0, MEM_RELEASE);
    bitmap_memory[0] = NULL;
  }
  if (bitmap_memory[1]) {
    VirtualFree(bitmap_memory[1], 0, MEM_RELEASE);
    bitmap_memory[1] = NULL;
  }

  bitmap_width = width;
  bitmap_height = height;

  bitmap_info.bmiHeader.biSize = sizeof(bitmap_info.bmiHeader);
  bitmap_info.bmiHeader.biWidth = bitmap_width;
  bitmap_info.bmiHeader.biHeight = -bitmap_height;
  bitmap_info.bmiHeader.biPlanes = 1;
  bitmap_info.bmiHeader.biBitCount = 32; // 24-bit colour + 8 bits for alignment
  bitmap_info.bmiHeader.biCompression = BI_RGB;

  int bitmap_memory_size = bitmap_width * bitmap_height * bytes_per_pixel;
  bitmap_memory[0] =
    VirtualAlloc(0, bitmap_memory_size, MEM_COMMIT, PAGE_READWRITE);
  bitmap_memory[1] =
    VirtualAlloc(0, bitmap_memory_size, MEM_COMMIT, PAGE_READWRITE);

  render_screen();
}

internal void UpdateTerminalWindow(HDC device_context, RECT window_rect, LONG x,
                                   LONG y, LONG width, LONG height)
{
  int window_width = window_rect.right - window_rect.left;
  int window_height = window_rect.bottom - window_rect.top;

  // slap the bitmap on the screen
  StretchDIBits(device_context, 0, 0, bitmap_width, bitmap_height, 0, 0,
                window_width, window_height, bitmap_memory[active_buf_idx], &bitmap_info,
                DIB_RGB_COLORS, SRCCOPY);
}

LRESULT CALLBACK MainWindowCallback(HWND window, UINT message, WPARAM wParam,
                                    LPARAM lParam)
{
  LRESULT result = 0;
  switch (message) {
  case WM_SIZE: {
    RECT client_rect;
    GetClientRect(window, &client_rect);
    int width = client_rect.right - client_rect.left;
    int height = client_rect.bottom - client_rect.top;
    ResizeDBISection(width, height);
  } break;
  case WM_CLOSE: {
    running = false;
  } break;
  case WM_ACTIVATEAPP: {
    OutputDebugStringA("WM_ACTIVATEAPP\n");
  } break;
  case WM_DESTROY: {
    running = false;
  } break;
  case WM_PAINT: {
    // need to paint on the window
    PAINTSTRUCT paint;
    HDC device_context = BeginPaint(window, &paint);
    LONG x = paint.rcPaint.left;
    LONG y = paint.rcPaint.top;
    LONG height = paint.rcPaint.bottom - paint.rcPaint.top;
    LONG width = paint.rcPaint.right - paint.rcPaint.left;

    RECT client_rect;
    GetClientRect(window, &client_rect);

    UpdateTerminalWindow(device_context, client_rect, x, y, width, height);
    EndPaint(window, &paint);
  } break;
  default: {
    result = DefWindowProc(window, message, wParam, lParam);
  } break;
  }
  return result;
}

void MessageLoop(HWND window)
{
  running = true;

  // message loop
  while (running) {
    MSG message;
    if (PeekMessage(&message, 0, 0, 0, PM_REMOVE)) {
      if (message.message == WM_QUIT)
        running = false;

      TranslateMessage(&message);
      DispatchMessage(&message);
    }

    render_screen();
    HDC device_context = GetDC(window);
    RECT client_rect;
    GetClientRect(window, &client_rect);
    LONG width = client_rect.right - client_rect.left;
    LONG height = client_rect.bottom - client_rect.top;

    UpdateTerminalWindow(device_context, client_rect, 0, 0, width, height);
    ReleaseDC(window, device_context);
  }

  ShowCursor(true);
}

int CALLBACK WinMain(HINSTANCE Instance, HINSTANCE prevInstance,
                     LPSTR lpCmdLine, int nCmdShow)
{
  WNDCLASS window_class = {};
  window_class.style = CS_HREDRAW | CS_VREDRAW;
  window_class.lpfnWndProc = MainWindowCallback;
  window_class.hInstance = Instance;
  window_class.lpszClassName = "WeeeWindowClass";

  set_full_screen(SCREEN_WIDTH, SCREEN_HEIGHT);

  ShowCursor(false);

  if (RegisterClass(&window_class)) {
    HWND window_handle = CreateWindowEx(
      0, window_class.lpszClassName, "Weee",
      // windowed fullscreen so you don't cry as much by the cursor being goners
      WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_VISIBLE,
      CW_USEDEFAULT, CW_USEDEFAULT,
      SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0, Instance, 0);
    if (window_handle) {
      MessageLoop(window_handle);
    } else {
      // TODO(kako57): logging
    }
  } else {
    // TODO(kako57): logging
  }

  return 0;
}
