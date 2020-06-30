#include <iostream>
#include <termios.h>
#include <unistd.h>
#include <thread>
#include <poll.h>

#include "exception.h"
#include "vt_control.h"
#include "debug.h"
#include "mrtrix.h"


// implementation mostly gleaned from various sites, particularly:
// https://www.man7.org/linux/man-pages/man4/console_codes.4.html

// implementation currently ignores a lot of the possible control sequences#
// it is hoped that these would only be produced as output to the terminal, rather
// than received as input to the application...


namespace MR {
  namespace VT {

    namespace {
      struct termios orig_termios;
    }

    void enter_raw_mode ()
    {
      if (!isatty (STDOUT_FILENO))
        if (!freopen ("/dev/tty", "a", stdout))
          throw Exception ("failed to remap stdout to the terminal");

      if (!isatty (STDIN_FILENO))
        if (!freopen ("/dev/tty", "r", stdin))
          throw Exception ("failed to remap stdin to the terminal");

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



    void get_cursor_position (int& row, int& col)
    {
      std::cout << "\033[6n";
      std::cout.flush();

      int nread;
      char c = '\0';

      try {
        while ((nread = read (STDIN_FILENO, &c, 1)) != 1) {
          if (nread == -1 && errno != EAGAIN)
            throw 1;
          std::this_thread::sleep_for (std::chrono::milliseconds(10));
        }

        if (c != Escape)
          throw 1;

        std::string buf;
        while ((nread = read (STDIN_FILENO, &c, 1)) == 1) {
          if (c == '[' && buf.empty())
            continue;
          if (c == 'R')
            break;
          if (( c >= '0' && c <= '9') || c == ';')
            buf += c;
          else
            throw 1;
          if (buf.size() > 10)
            throw 1;
        }

        auto xy = split (buf, ";");
        if (xy.size() != 2)
          throw 1;

        row = to<int> (xy[0]);
        col = to<int> (xy[1]);
      }
      catch (int) {
        throw Exception ("unexpected response from terminal");
      }

    }






    void EventLoop::run ()
    {
      while (true) {
        param.clear();
        uint8_t c = next();

        if (c == Escape) esc();
        else if (c == 0x9B) CSI();
        else if (!callback (c, param))
          return;
      }
    }




    void EventLoop::esc ()
    {
      uint8_t c = next();
      if (c == '[')
        CSI();
      else if (c == ']')
        OSC();
      else
        throw Exception ("unexpected Esc control sequence");
    }



    void EventLoop::CSI ()
    {
      uint8_t c = next();
      if (c == '[') {
        next();
        return;
      }
      if (c == 'M') {
        mouse ();
        return;
      }
      // ignore initial question mark if encountered. Not sure whether this is
      // the right thing to do...
      if (c == '?')
        c = next();

      std::string buf;
      for (int n = 0; n < 16; ++n) {
        if (c >= '0' && c <= '9')
          buf.push_back (c);
        else if (c == ';') {
          param.push_back (buf.size() ? to<int>(buf) : 0);
          buf.clear();
        }
        else if (c == Escape) {
          param.clear();
          esc();
        }
        else if ((c >= 0x07 && c <= 0x0F) || c == 0x7F) { /* Control characters - ignore */ }
        else if (c == 0x9B) { // CSI
          param.clear();
          CSI();
        }
        else if (c == 0x18 || c == 0x1A) // abort
          return;
        else {
          if (buf.size())
            param.push_back (to<int>(buf));
          callback (CSImask | c, param);
          return;
        }
        c = next();
      }
      throw Exception ("unexpected input!");
    }


    void EventLoop::OSC ()
    {
      throw Exception ("unexpected OSC sequence");
    }




    void EventLoop::mouse ()
    {
      param.push_back (next()-0x20);
      param.push_back (next()-0x20);
      param.push_back (next()-0x20);

      callback (MouseEvent, param);
    }



    void EventLoop::fill_buffer ()
    {
      struct pollfd pfd;
      pfd.fd = STDIN_FILENO;
      pfd.events = POLLIN;

      current_char = 0;

      // if nothing on input stream, invoke idle event:
      poll (&pfd, 1, 0);
      if (!pfd.revents)
        callback (0, param);

      do {
        poll (&pfd, 1, -1);
        if (pfd.revents != POLLIN)
          throw Exception ("unexpected error on input stream");
        nread = read (STDIN_FILENO, buf, VT_READ_BUFSIZE);
        if (nread == -1 && errno != EAGAIN)
          throw Exception ("error reading user input");
      } while (nread == 0);
    }




  }
}



