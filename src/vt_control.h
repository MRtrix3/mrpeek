#ifndef __VT_CODES_H__
#define __VT_CODES_H__

namespace MR {
  namespace VT {

    constexpr const char* SixelStart = "\033Pq$";
    constexpr const char* SixelStop = "\033\\";

    constexpr const char* CursorOff = "\033[?25l";
    constexpr const char* CursorOn = "\033[?25h";

    constexpr char Escape = '\033';
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

    constexpr inline int Ctrl (int c) { return c & 0x1F; }



    void enter_raw_mode();
    void exit_raw_mode();
    int read_user_input();


  }
};


#endif

