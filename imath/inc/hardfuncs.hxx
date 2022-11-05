/***************************************************************************
  hardfuncs.hxx  -  define hard-coded functions for eqc
                             -------------------
    begin                : Sun Oct 23 2022
    copyright            : (C) 2022 by Jan Rheinlaender
    email                : jrheinlaender@users.sourceforge.net
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef HARDFUNCS_H
#define HARDFUNCS_H

/**
 * @author Jan Rheinlaender
 * @short Define hard-coded functions for eqc
 */

#include <ginac/ginac.h>

namespace GiNaC {

// Note: The order here should correspond to the order of REGISTER_FUNCTION
// and VERY IMPORTANT round must be first, because of eval() using round_SERIAL::serial
DECLARE_FUNCTION_2P(round)
DECLARE_FUNCTION_2P(floor)
DECLARE_FUNCTION_2P(ceil)
DECLARE_FUNCTION_3P(sum)
DECLARE_FUNCTION_3P(mindex)
DECLARE_FUNCTION_3P(hadamard)
DECLARE_FUNCTION_1P(transpose)
DECLARE_FUNCTION_2P(vecprod)
DECLARE_FUNCTION_2P(scalprod)
DECLARE_FUNCTION_3P(ifelse)
DECLARE_FUNCTION_1P(vmax)
DECLARE_FUNCTION_1P(vmin)
DECLARE_FUNCTION_2P(concat)
// Matrix functions (as defined in GiNaC matrix.h). Note that diag_matrix() etc. are already declared in namespace GiNaC in matrix.h
DECLARE_FUNCTION_1P(diagmatrix)
DECLARE_FUNCTION_2P(identmatrix)
DECLARE_FUNCTION_2P(onesmatrix)
DECLARE_FUNCTION_5P(submatrix)
DECLARE_FUNCTION_3P(reducematrix)
DECLARE_FUNCTION_1P(determinant)
DECLARE_FUNCTION_1P(trace)
DECLARE_FUNCTION_2P(charpoly)
DECLARE_FUNCTION_1P(rank)
DECLARE_FUNCTION_3P(solvematrix)
DECLARE_FUNCTION_1P(invertmatrix)
DECLARE_FUNCTION_1P(matrixrows)
DECLARE_FUNCTION_1P(matrixcols)

}
#endif
