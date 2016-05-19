//***************************************************************************
// Copyright 2007-2016 OceanScan - Marine Systems & Technology, Lda         *
//***************************************************************************
// This file is part of DUNE: Unified Navigation Environment.               *
//                                                                          *
// Commercial Licence Usage                                                 *
// Licencees holding valid commercial DUNE licences may use this file in    *
// accordance with the commercial licence agreement provided with the       *
// Software or, alternatively, in accordance with the terms contained in a  *
// written agreement between you and Universidade do Porto. For licensing   *
// terms, conditions, and further information contact lsts@fe.up.pt.        *
//                                                                          *
// European Union Public Licence - EUPL v.1.1 Usage                         *
// Alternatively, this file may be used under the terms of the EUPL,        *
// Version 1.1 only (the "Licence"), appearing in the file LICENCE.md       *
// included in the packaging of this file. You may not use this work        *
// except in compliance with the Licence. Unless required by applicable     *
// law or agreed to in writing, software distributed under the Licence is   *
// distributed on an "AS IS" basis, WITHOUT WARRANTIES OR CONDITIONS OF     *
// ANY KIND, either express or implied. See the Licence for the specific    *
// language governing permissions and limitations at                        *
// http://ec.europa.eu/idabc/eupl.html.                                     *
//***************************************************************************
// Author: José Braga                                                       *
//***************************************************************************

#ifndef SENSORS_NORTEK_DVL_DRIVER_HPP_INCLUDED_
#define SENSORS_NORTEK_DVL_DRIVER_HPP_INCLUDED_

// ISO C++ 98 headers.
#include <string>

// DUNE headers.
#include <DUNE/DUNE.hpp>

namespace Sensors
{
  namespace NortekDVL
  {
    using DUNE_NAMESPACES;

    //! Break command
    static const std::string c_cmd_break = "K1W%!Q";
    //! Credentials
    static const std::string c_cmd_nortek = "nortek";

    //! Available power levels.
    enum PowerLevel
    {
      // minimum power level.
      PL_MIN,
      // medium power level.
      PL_MED,
      // maximum power level.
      PL_MAX
    };

    //! Device's available modes of operation.
    enum Modes
    {
      M_FIRMWARE = 0,
      M_MEASUREMENT = 1,
      M_COMMAND = 2,
      M_DATA = 4,
      M_CONFIRMATION = 5
    };

    //! Driver class to configure Nortek's DVL.
    class Driver
    {
    public:
      //! Constructor.
      //! @param[in] task parent task.
      //! @param[in] addr IPv4 address.
      //! @param[in] port tcp port.
      //! @param[in] rate sampling rate.
      Driver(Tasks::Task* task, const Address& addr, unsigned port, float rate):
        m_task(task),
        m_salinity(35.0),
        m_sampling_rate(5.0),
        m_cmd_mode(false)
      {
        m_sock.connect(addr, port);
        setSamplingRate(rate);
      }

      //! Destructor.
      ~Driver(void)
      {
        // Gracefully disconnect from device.
        sendCommand("POWERDOWN");
      }

      //! Device's setup sequence.
      //! @return true if command succeeded, false otherwise.
      bool
      setup(void)
      {
        if (!login())
          return false;

        if (!enterCommandMode())
          return false;

        if (!sendCommand("SETDEFAULT,ALL"))
          return false;

        if (!sendCommand("SETINST,LED=\"OFF\""))
          return false;

        if (!setTime())
          return false;

        if (!setDVL())
          return false;

        if (!save())
          return false;

        return start();
      }

      //! Update salinity.
      //! @param[in] value salinity value.
      void
      setSalinity(double value)
      {
        if (value < 0.0 || value > 50.0)
          return;

        m_salinity = value;
      }

      //! Set device's power level.
      //! @param[in] level power level range.
      //! @return true if configured successfully, false otherwise.
      bool
      setPowerLevel(PowerLevel level)
      {
        float power = 0.0;
        switch (level)
        {
          case (PL_MIN):
            power = -20.0;
            break;
          case (PL_MED):
            power = -10.0;
            break;
          case (PL_MAX):
            power = 0.0;
            break;
        }

        std::string cmd;
        cmd = String::str("SETBT,PL=%f", power);
        bool r = sendCommand(cmd);
        start();
        return r;
      }

    private:
      //! Login into device.
      //! @return true if login succeeded, false otherwise
      bool
      login(void)
      {
        replyLogin("Username: ");
        replyLogin("Password: ");

        if (readUntil("Command Interface\r\r\n", 2.0, true))
        {
          Delay::wait(1.0);
          return true;
        }

        return false;
      }

      //! Reply with credentials
      bool
      replyLogin(const std::string& reply)
      {
        if (readUntil(reply, 1.0, true))
          write(c_cmd_nortek);
        else
          return false;

        return true;
      }

      //! Start measuring.
      //! @return true if in measurement mode, false otherwise.
      bool
      start(void)
      {
        // go to measurement mode.
        if (sendCommand("START"))
        {
          m_cmd_mode = false;
          return true;
        }

        return false;
      }

      //! Stop measuring and enter command mode.
      //! @return true if back in command mode, false otherwise.
      bool
      enterCommandMode(void)
      {
        // no need to stop, not in measurement mode.
        if (m_cmd_mode)
          return true;

        if (!sendBreak())
          return false;

        Delay::wait(1.0);
        if (!sendCommand("MC", 2.0, true))
          return false;

        m_cmd_mode = true;
        return true;
      }

      //! Wake up device.
      //! @return true if break was received, false otherwise.
      bool
      sendBreak(void)
      {
        if (!sendCommand(c_cmd_break, true, 2.0))
        {
          if (sendCommand(c_cmd_break, true, 2.0))
            return true;
          else
            return false;
        }

        return true;
      }

      //! Set device's time.
      //! @return true if configured successfully, false otherwise.
      bool
      setTime(void)
      {
        Time::BrokenDown tm(Clock::getSinceEpoch(), true);
        std::string cmd;
        cmd = String::str("SETCLOCK,YEAR=%d,MONTH=%d,DAY=%d,"
                          "HOUR=%d,MINUTE=%d,SECOND=%d",
                          tm.year, tm.month, tm.day, tm.hour,
                          tm.minutes, tm.seconds);

        if (!sendCommand(cmd))
          return false;

        return true;
      }

      //! Set DVL parameters.
      //! @return true if configured successfully, false otherwise.
      bool
      setDVL(void)
      {
        std::string cmd;
        cmd = String::str("SETDVL,SR=%f,SA=%f",
                          m_sampling_rate, m_salinity);

        if (!sendCommand(cmd))
          return false;

        return true;
      }

      //! Save configuration.
      //! @return true if configured successfully, false otherwise.
      bool
      save(void)
      {
        if (!sendCommand("SAVE,ALL"))
        {
          sendCommand("GETERROR");
          return false;
        }

        return true;
      }

      //! Set sampling rate.
      //! @param[in] rate desired sampling rate.
      void
      setSamplingRate(float rate)
      {
        if (rate < 1.0 || rate > 8.0)
          return;

        m_sampling_rate = rate;
      }

      //! Send command and wait for reply.
      //! @param[in] cmd command to send.
      //! @param[in] ignore ignore if device is in measurement mode or not.
      //! @param[in] print send replies to output stream.
      //! @return true if command succeeded, false otherwise.
      bool
      sendCommand(const std::string& cmd, bool ignore = false, bool print = false)
      {
        // if in measurement mode, stop stream and go to command mode.
        if (!ignore)
        {
          if (!enterCommandMode())
            return false;
        }

        write(cmd);
        std::string reply("OK\r\n");
        return readUntil(reply, 1.0, print);
      }

      //! Write command.
      //! @param[in] cmd command to send.
      void
      write(const std::string& cmd)
      {
        std::string bfr(cmd + "\r\n");
        m_sock.write(bfr.c_str(), bfr.size());
        m_task->spew("sent: '%s'", sanitize(bfr).c_str());
      }

      //! Read input until a given sequence is received. Note that
      //! data after the sequence might be discarded.
      //! @param[in] sequence sequence to search.
      //! @param[in] timeout timeout in second.
      //! @param[in] print send replies to output stream.
      //! @return true if command succeeded, false otherwise.
      bool
      readUntil(const std::string& sequence, float timeout, bool print = false)
      {
        Counter<float> timer(timeout);
        char bfr[256] = {0};
        size_t offset = 0;

        while (!timer.overflow())
        {
          if (!Poll::poll(m_sock, timer.getRemaining()))
            break;

          offset += m_sock.read(bfr + offset, sizeof(bfr) - offset);
          if (offset > sizeof(bfr))
            break;

          if (String::endsWith(bfr, sequence))
          {
            if (print)
              m_task->spew("recv: '%s'", sanitize(bfr).c_str());

            return true;
          }
        }

        if (print)
          m_task->spew("recv: '%s' (does not end with: '%s')",
                       sanitize(bfr).c_str(), sanitize(sequence).c_str());

        return false;
      }

      //! Parent task.
      Tasks::Task* m_task;
      //! TCP socket.
      TCPSocket m_sock;
      //! Salinity value.
      double m_salinity;
      //! Sampling rate.
      float m_sampling_rate;
      //! Command Mode;
      bool m_cmd_mode;
    };
  }
}

#endif
