//***************************************************************************
// Copyright 2007-2017 Universidade do Porto - Faculdade de Engenharia      *
// Laboratório de Sistemas e Tecnologia Subaquática (LSTS)                  *
//***************************************************************************
// This file is part of DUNE: Unified Navigation Environment.               *
//                                                                          *
// Commercial Licence Usage                                                 *
// Licencees holding valid commercial DUNE licences may use this file in    *
// accordance with the commercial licence agreement provided with the       *
// Software or, alternatively, in accordance with the terms contained in a  *
// written agreement between you and Faculdade de Engenharia da             *
// Universidade do Porto. For licensing terms, conditions, and further      *
// information contact lsts@fe.up.pt.                                       *
//                                                                          *
// Modified European Union Public Licence - EUPL v.1.1 Usage                *
// Alternatively, this file may be used under the terms of the Modified     *
// EUPL, Version 1.1 only (the "Licence"), appearing in the file LICENCE.md *
// included in the packaging of this file. You may not use this work        *
// except in compliance with the Licence. Unless required by applicable     *
// law or agreed to in writing, software distributed under the Licence is   *
// distributed on an "AS IS" basis, WITHOUT WARRANTIES OR CONDITIONS OF     *
// ANY KIND, either express or implied. See the Licence for the specific    *
// language governing permissions and limitations at                        *
// https://github.com/LSTS/dune/blob/master/LICENCE.md and                  *
// http://ec.europa.eu/idabc/eupl.html.                                     *
//***************************************************************************
// Author: PGonçalves                                                       *
//***************************************************************************

// DUNE headers.
#include <DUNE/DUNE.hpp>

// Local Headers.
#include "Driver.hpp"

namespace Power
{
  namespace PCTLv3
  {
    using DUNE_NAMESPACES;

    static const float c_delay_startup = 1.0f;
    static const uint8_t c_max_values_voltage = 16;
    static const uint8_t c_max_values_current = 3;
    static const float c_usart_timeout = 0.1f;

    struct Arguments
    {
      //! Serial port device.
      std::string uart_dev;
      //! Serial port baud rate.
      unsigned uart_baud;
      //! Input timeout.
      double input_timeout;
      //! Input number cell
      unsigned int number_cell;
      //! Time between each acquisition
      float sample_time;
      //! Scale conversion for A/Ah
      float scale_factor;
      //! Cell entity labels
      std::string cell_elabels[c_max_values_voltage - 1];
      //! Remaining Capacity entity label
      std::string rcap_elabel;
      //! Full Capacity entity label
      std::string fcap_elabel;
      //! State to dispatch Feul level
      bool dispatch_fuel_level;
      //! LED identifiers.
      std::vector<std::string> led_ids;
      //! Entities whose failures are critical.
      std::vector<std::string> critical;
    };

    struct Task: public DUNE::Tasks::Task
    {
      //! Serial port handle.
      SerialPort* m_uart;
      //! I/O Multiplexer.
      Poll m_poll;
      //! Task arguments
      Arguments m_args;
      //! Driver of BatMan
      DriverPCTLv3 *m_driver;
      //! Watchdog.
      Counter<double> m_wdog;
      //! Temperature message
      IMC::Temperature m_tmp;
      //! Voltage message
      IMC::Voltage m_volt[c_max_values_voltage];
      //! Voltage of batteries message
      IMC::Voltage m_bat_volt;
      //! Current message
      IMC::Current m_amp[c_max_values_current];
      //! Fuel Level message
      IMC::FuelLevel m_fuel;
      //! Buffer forEntityState
      char m_bufer_entity[128];
      //! Read timestamp.
      double m_tstamp;
      //! Flag to control state of shutdow;
      bool m_is_to_pwr_off;

      //! Constructor.
      //! @param[in] name task name.
      //! @param[in] ctx context.
      Task(const std::string& name, Tasks::Context& ctx):
        DUNE::Tasks::Task(name, ctx),
        m_uart(NULL),
        m_driver(0),
        m_tstamp(0),
        m_is_to_pwr_off(0)
      {
        param("Serial Port - Device", m_args.uart_dev)
        .defaultValue("")
        .description("Serial port device used to communicate with the sensor");

        param("Serial Port - Baud Rate", m_args.uart_baud)
        .defaultValue("38400")
        .description("Serial port baud rate");

        param("Input Timeout", m_args.input_timeout)
        .defaultValue("4.0")
        .minimumValue("1.0")
        .maximumValue("15.0")
        .units(Units::Second)
        .description("Amount of seconds to wait for data before reporting an error");

        param("Number of cells", m_args.number_cell)
        .defaultValue("7")
        .minimumValue("1")
        .maximumValue("15")
        .description("Number of cells to read.");

        param("Time between each acquisition", m_args.sample_time)
        .defaultValue("1.0")
        .minimumValue("1.0")
        .units(Units::Second)
        .description("Time between each acquisition.");

        param("Scale Factor A/Ah", m_args.scale_factor)
        .defaultValue("1")
        .description("Scale Factor A/Ah.");

        // Extract cell entity label
        for (uint8_t i = 1; i < c_max_values_voltage; ++i)
        {
          std::string option = String::str("Cell %u - Entity Label", i);
          param(option, m_args.cell_elabels[i - 1])
          .defaultValue("")
          .description("Cell Entity Label");
        }

        param("Remaining Capacity - Entity Label", m_args.rcap_elabel)
        .defaultValue("1")
        .description("Remaining Capacity A/Ah.");

        param("Full Capacity - Entity Label", m_args.fcap_elabel)
        .defaultValue("1")
        .description("Full Capacity A/Ah.");

        param("Dispatch Fuel Level", m_args.dispatch_fuel_level)
        .defaultValue("true")
        .description("Dispatch Fuel Level.");

        param("Identifiers", m_args.led_ids)
        .defaultValue("")
        .description("List of LED identifiers (Names, GPIO number, etc)");

        param("Critical Entities", m_args.critical)
        .defaultValue("")
        .description("Entity names whose failures are considered critical");

        bind<IMC::PowerChannelControl>(this);
        bind<IMC::SetLedBrightness>(this);
      }

      //! Reserve entity identifiers.
      void
      onEntityReservation(void)
      {
        for (uint8_t i = 0; i < m_args.number_cell; ++i)
        {

          if (m_args.cell_elabels[i].empty())
            continue;

          m_volt[i + 1].setSourceEntity(getEid(m_args.cell_elabels[i]));
        }

        m_bat_volt.setSourceEntity(getEid("Batteries"));
        m_amp[1].setSourceEntity(getEid(m_args.rcap_elabel));
        m_amp[2].setSourceEntity(getEid(m_args.fcap_elabel));
      }

      unsigned
      getEid(std::string label)
      {
        unsigned eid = 0;
        try
        {
          eid = resolveEntity(label);
        }
        catch (Entities::EntityDataBase::NonexistentLabel& e)
        {
          (void)e;
          eid = reserveEntity(label);
        }

        return eid;
      }

      //! Acquire resources.
      void
      onResourceAcquisition(void)
      {
        setEntityState(IMC::EntityState::ESTA_BOOT, Status::CODE_INIT);
        try
        {
          m_uart = new SerialPort(m_args.uart_dev, m_args.uart_baud);
          m_uart->setCanonicalInput(true);
          m_uart->flush();
          m_poll.add(*m_uart);
          m_driver = new DriverPCTLv3(this, m_uart, m_poll, m_args.number_cell);
          m_is_to_pwr_off = false;
        }
        catch (std::runtime_error& e)
        {
          throw RestartNeeded(e.what(), 10);
        }
      }

      //! Initialize resources.
      void
      onResourceInitialization(void)
      {
        m_driver->stopAcquisition();
        m_uart->flush();
        Delay::wait(c_delay_startup);

        if(!m_driver->getVersionFirmware())
          war(DTR("failed to get firmware version"));
        else
          inf("Firmware Version: %s", m_driver->getFirmwareVersion().c_str());

        if(m_args.sample_time >= m_args.input_timeout)
        {
          m_args.input_timeout++,
          war(DTR("Incorrect Timeout, setting to %.0f sec."), m_args.input_timeout);
        }

        if(!m_driver->initPCTLv3(m_args.number_cell, m_args.scale_factor, m_args.sample_time))
          throw RestartNeeded(DTR("failed to init PCTLv3"), 5, true);

        if(!m_driver->startAcquisition())
          throw RestartNeeded(DTR("failed to start acquisition"), 5, true);

        m_driver->turnOffAllLed();
        Delay::wait(c_delay_startup);
        debug("Init and Start OK");
        m_wdog.setTop(m_args.sample_time + m_args.input_timeout);
      }

      //! Release resources.
      void
      onResourceRelease(void)
      {
        if (m_uart != NULL)
        {
          m_poll.remove(*m_uart);
          Memory::clear(m_driver);
          Memory::clear(m_uart);
        }
      }

      void
      consume(const IMC::PowerChannelControl *msg)
      {
        war("aqui 1");
        if(msg->op == IMC::PowerChannelControl::PCC_OP_TURN_OFF)
        {
          war("turn off");
          m_is_to_pwr_off = true;
        }
      }

      void
      consume(const IMC::SetLedBrightness *msg)
      {
        if (msg->getSource() != getSystemId())
          return;

        for(uint8_t i = 0; i < 4; i++)
        {
          if (m_args.led_ids[i].compare(msg->name) == 0)
          {
            m_driver->updateBufferLed(i, msg->value);
            m_driver->updateLedState();
            break;
          }
        }
      }

      void
      dispatchData(void)
      {
        m_driver->resetStateNewData();
        std::memset(&m_bufer_entity, '\0', sizeof(m_bufer_entity));
        std::sprintf(m_bufer_entity, "H: %d %%, Volt: %.3f V, RCap: %.3f Ah", m_driver->m_pctlData.health,
            m_driver->m_pctlData.voltage, m_driver->m_pctlData.r_cap);
        setEntityState(IMC::EntityState::ESTA_NORMAL, Utils::String::str(DTR(m_bufer_entity)));

        m_volt[0].setTimeStamp(m_tstamp);
        m_volt[0].value = m_driver->m_pctlData.voltage;
        dispatch(m_volt[0], DF_KEEP_TIME);

        m_bat_volt.setTimeStamp(m_tstamp);
        m_bat_volt.value = m_driver->m_pctlData.voltage;
        dispatch(m_bat_volt, DF_KEEP_TIME);

        m_amp[0].setTimeStamp(m_tstamp);
        m_amp[0].value = m_driver->m_pctlData.current;
        dispatch(m_amp[0], DF_KEEP_TIME);

        for (uint8_t id = 1; id <= m_args.number_cell; ++id)
        {
          m_volt[id].setTimeStamp(m_tstamp);
          m_volt[id].value = m_driver->m_pctlData.cell_volt[id - 1];
          dispatch(m_volt[id], DF_KEEP_TIME);
        }

        m_tmp.setTimeStamp(m_tstamp);
        m_tmp.value = m_driver->m_pctlData.temperature;
        dispatch(m_tmp, DF_KEEP_TIME);

        m_amp[1].setTimeStamp(m_tstamp);
        m_amp[1].value = m_driver->m_pctlData.r_cap;
        dispatch(m_amp[1], DF_KEEP_TIME);

        m_amp[2].setTimeStamp(m_tstamp);
        m_amp[2].value = m_driver->m_pctlData.f_cap;
        dispatch(m_amp[2], DF_KEEP_TIME);

        if(m_args.dispatch_fuel_level)
        {
          m_fuel.setTimeStamp(m_tstamp);
          m_fuel.value = (m_driver->m_pctlData.r_cap * 100) / m_driver->m_pctlData.f_cap;
          m_fuel.confidence = 100;
          dispatch(m_fuel, DF_KEEP_TIME);
        }
      }

      //! Main loop.
      void
      onMain(void)
      {
        while (!stopping())
        {
          waitForMessages(0.15);

          if (m_wdog.overflow())
          {
            setEntityState(IMC::EntityState::ESTA_ERROR, Status::CODE_COM_ERROR);
              throw RestartNeeded(DTR(Status::getString(CODE_COM_ERROR)), 5);
          }

          if (!Poll::poll(*m_uart, c_usart_timeout))
            continue;

          if(m_driver->haveNewData())
          {
            m_tstamp = Clock::getSinceEpoch();
            dispatchData();
            m_wdog.reset();
          }

        }
        debug("Sending stop to PCTLv3");
        m_driver->turnOffAllLed();
        m_driver->stopAcquisition();
        Delay::wait(c_delay_startup);
        /*m_wdog.setTop(5);
        while(!m_is_to_pwr_off && !m_wdog.overflow())
        {
          waitForMessages(0.1);
        }*/

      }
    };
  }
}

DUNE_TASK