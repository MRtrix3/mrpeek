#include <iostream>
#include <termios.h>
#include <unistd.h>

#include "exception.h"
#include "vt_control.h"
#include "debug.h"


namespace MR {
  namespace VT {

    namespace {
      struct termios orig_termios;
    }

    void enter_raw_mode ()
    {
      std::cout << CursorOff << MouseTrackingOn;
      std::cout.flush();

      // enable raw mode:
      struct termios raw;
      tcgetattr(STDIN_FILENO, &raw);
      orig_termios = raw;
      raw.c_iflag &= ~(ICRNL | IXON);
      raw.c_oflag &= ~(OPOST);
      raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
      raw.c_cc[VMIN] = 0;
      raw.c_cc[VTIME] = 0;
      tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    }


    void exit_raw_mode ()
    {
      tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
      std::cout << CursorOn << MouseTrackingOff << "\n";
      std::cout.flush();
    }

    int read_user_input (int& x, int& y)
    {
      int nread;
      char c = '\0';

      while ((nread = read (STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN)
          throw Exception ("error reading user input");
        return 0;
      }

      if (c == Escape) {
        char seq[5];
        if (read (STDIN_FILENO, &seq[0], 1) != 1) return Escape;
        if (read (STDIN_FILENO, &seq[1], 1) != 1) return Escape;
        if (seq[0] == '[') {
          if (seq[1] >= '0' && seq[1] <= '9') {
            if (read (STDIN_FILENO, &seq[2], 1) != 1) return Escape;
            if (seq[2] == '~') {
              switch (seq[1]) {
                case '1': return Home;
                case '3': return Delete;
                case '4': return End;
                case '5': return PageUp;
                case '6': return PageDown;
                case '7': return Home;
                case '8': return End;
                default: return Escape;
              }
            }
          } else {

            switch (seq[1]) {
              case 'A': return Up;
              case 'B': return Down;
              case 'C': return Right;
              case 'D': return Left;
              case 'Z': return ShiftTab;
              case 'H': return Home;
              case 'F': return End;
              case 'M': // mouse:
                        if (read (STDIN_FILENO, &seq[2], 1) != 1) return Escape;
                        if (read (STDIN_FILENO, &seq[3], 1) != 1) return Escape;
                        if (read (STDIN_FILENO, &seq[4], 1) != 1) return Escape;
                        x = seq[3];
                        y = seq[4];
                        switch (seq[2]) {
                          case ' ': return MouseLeft;
                          case '!': return MouseMiddle;
                          case '"': return MouseRight;
                          case '#': return MouseRelease;
                          case '`': return MouseWheelUp;
                          case 'a': return MouseWheelDown;
                          case '@': return MouseMoveLeft;
                          case 'A': return MouseMoveMiddle;
                          case 'B': return MouseMoveRight;
                          default: return Escape;
                        }



              default: return Escape;
            }
          }
        }
        else if (seq[0] == 'O') {
          switch (seq[1]) {
            case 'H': return Home;
            case 'F': return End;
            default: return Escape;
          }
        }
        return Escape;
      }

      return c;
    }


  }
};



