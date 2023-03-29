# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the LibreOffice project.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

$(eval $(call gb_Package_Package,sm_res_imath,$(SRCDIR)/starmath/res/imath))

$(eval $(call gb_Package_add_files,sm_res_imath,$(LIBO_SHARE_FOLDER)/imath/references,\
	references/init.imath \
	references/units.imath \
	references/siunits.imath \
	references/siunits_abbrev.imath \
	references/siprefixes.imath \
	references/siprefixes_abbrev.imath \
	references/engunits.imath \
	references/engunits_abbrev.imath  \
	references/impunits.imath \
	references/impunits_abbrev.imath \
	references/substitutions.imath \
	examples/iMath-tour.odt \
	examples/Analysis.odt \
	examples/PartialDifferentiation.odt \
	examples/SymbolicIntegration.odt \
	examples/Laminar-boundary-layer-equations.odt \
	examples/Operators.odt \
))
# vim: set noet sw=4 ts=4:
