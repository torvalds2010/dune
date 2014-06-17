//***************************************************************************
// Copyright 2007-2014 Universidade do Porto - Faculdade de Engenharia      *
// Laboratório de Sistemas e Tecnologia Subaquática (LSTS)                  *
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
// https://www.lsts.pt/dune/licence.                                        *
//***************************************************************************
// Author: Ricardo Martins                                                  *
//***************************************************************************

#ifndef LCB_FILTERS_FACTORY_HPP_INCLUDED_
#define LCB_FILTERS_FACTORY_HPP_INCLUDED_

// DUNE headers.
#include <DUNE/DUNE.hpp>
using DUNE_NAMESPACES;

// SQLite headers.
#include <sqlite3/sqlite3.h>

// Local headers.
#include "Filter.hpp"
#define FILTER_INCLUDE
#include "Factory.def"
#undef FILTER_INCLUDE

namespace Transports
{
  namespace Leviathan
  {
    class Factory
    {
    public:
      static Filter*
      create(const std::string& name, sqlite3* db)
      {
#define FILTER(s, c) if (name == s) return new c(db);
#include "Factory.def"

        return NULL;
      }
    };
  }
}

#endif
