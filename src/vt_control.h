#ifndef __VT_CODES_H__
#define __VT_CODES_H__

namespace MR {
  namespace VT {

    constexpr const char* ClearScreen = "\033[2J";
    constexpr const char* SaveScreen = "\033[?47h";
    constexpr const char* RestoreScreen = "\033[?47l";
    constexpr const char* CursorHome = "\033[H";
    constexpr const char* ClearLine = "\033[2K";
    constexpr const char* CarriageReturn = "\r";

    constexpr const char* SixelStart = "\033Pq$";
    constexpr const char* SixelStop = "\033\\";

    constexpr const char* CursorOff = "\033[?25l";
    constexpr const char* CursorOn = "\033[?25h";

    constexpr const char* TextUnderscore = "\033[4m";
    constexpr const char* TextForegroundYellow = "\033[33m";
    constexpr const char* TextReset = "\033[0m";

    constexpr const char* MouseTrackingOn = "\033[?1002h";
    constexpr const char* MouseTrackingOff = "\033[?1002l";

    constexpr char Escape = '\033';
    constexpr char CtrlC = '\x03';
    constexpr int Up = 0x0101;
    constexpr int Down = 0x0102;
    constexpr int Right = 0x0103;
    constexpr int Left = 0x0104;
    constexpr int PageUp = 0x0105;
    constexpr int PageDown = 0x0106;
    constexpr int Tab = 0x0009;
    constexpr int ShiftTab = 0x0109;
    constexpr int Home = 0x0110;
    constexpr int End = 0x0111;
    constexpr int Delete = 0x0112;
    constexpr int MouseLeft = 0x0201;
    constexpr int MouseMiddle = 0x0202;
    constexpr int MouseRight = 0x0203;
    constexpr int MouseWheelUp = 0x0204;
    constexpr int MouseWheelDown = 0x0205;
    constexpr int MouseRelease = 0x0206;
    constexpr int MouseMoveLeft = 0x0211;
    constexpr int MouseMoveMiddle = 0x0212;
    constexpr int MouseMoveRight = 0x0213;

    constexpr inline int Ctrl (int c) { return c & 0x1F; }

    inline void position_cursor_at (int row, int column)
    {
      std::cout << "\033[" << row << ";" << column << "H";
    }


    void enter_raw_mode();
    void exit_raw_mode();
    int read_user_input (int& x, int& y);


  }
};


#endif

