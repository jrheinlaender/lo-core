/* **************************************************************************
                          logging.hxx  - Loglevel for SAL messages
                             -------------------
    begin               : Sun Nov 27 17:00:00 CEST 2022
    copyright           : (C) 2022 by Jan Rheinlaender
    email               : jrheinlaender@users.sourceforge.net
 ***************************************************************************/

/* **************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify *
 *   it under the terms of the GNU General Public License as published by *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef _LOGGING_H
#define _LOGGING_H

#include <sal/log.hxx>
#include <officecfg/Office/iMath.hxx>

#define SAL_INFO_LEVEL(level, section, output) SAL_INFO_IF(level <= officecfg::Office::iMath::Miscellaneous::I_Debuglevel::get(), section, output)
#define SAL_WARN_LEVEL(level, section, output) SAL_WARN_IF(level <= officecfg::Office::iMath::Miscellaneous::I_Debuglevel::get(), section, output)

#endif
