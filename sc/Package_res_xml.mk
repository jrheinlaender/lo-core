# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the LibreOffice project.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

$(eval $(call gb_Package_Package,sc_res_xml,$(SRCDIR)/sc/res/xml))

# The imath files here are a stupid hack because we can't figure out how to
# include starmath/Package_res_imath.xml into the Windows msi files
$(eval $(call gb_Package_add_files,sc_res_xml,$(LIBO_SHARE_FOLDER)/calc,\
	styles.xml \
	../../../starmath/res/imath/references/init.imath \
        ../../../starmath/res/imath/references/units.imath \
        ../../../starmath/res/imath/references/siunits.imath \
        ../../../starmath/res/imath/references/siunits_abbrev.imath \
        ../../../starmath/res/imath/references/siprefixes.imath \
        ../../../starmath/res/imath/references/siprefixes_abbrev.imath \
        ../../../starmath/res/imath/references/engunits.imath \
        ../../../starmath/res/imath/references/engunits_abbrev.imath  \
        ../../../starmath/res/imath/references/impunits.imath \
        ../../../starmath/res/imath/references/impunits_abbrev.imath \
        ../../../starmath/res/imath/references/substitutions.imath \
        ../../../starmath/res/imath/examples/iMath-tour.odt \
        ../../../starmath/res/imath/examples/Analysis.odt \
        ../../../starmath/res/imath/examples/PartialDifferentiation.odt \
        ../../../starmath/res/imath/examples/SymbolicIntegration.odt \
        ../../../starmath/res/imath/examples/Laminar-boundary-layer-equations.odt \
        ../../../starmath/res/imath/examples/Operators.odt \
))

# vim: set noet sw=4 ts=4:
