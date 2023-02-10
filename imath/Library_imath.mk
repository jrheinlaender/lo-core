# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the LibreOffice project.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

$(eval $(call gb_Library_Library,imath))

#$(eval $(call gb_Library_set_precompiled_header,imath,imath/inc/pch/precompiled_imath))

$(eval $(call gb_Library_set_include,imath,\
    -I$(SRCDIR)/imath/inc \
    -I$(WORKDIR)/YaccTarget/imath/source \
    $$(INCLUDE) \
))

$(eval $(call gb_Library_add_defs,imath,\
        -DIMATH_DLLIMPLEMENTATION \
	-DINSIDE_SM \
	-DOO_IS_AOO=0 \
        -DOO_MAJOR_VERSION=$(LIBO_VERSION_MAJOR) \
        -DOO_MINOR_VERSION=$(LIBO_VERSION_MINOR) \
	-DSAL_LOG_INFO=1 \
	-DSAL_LOG_WARN=1 \
))

ifeq ($(COM), MSC)
$(eval $(call gb_Library_add_ldflags,imath,\
	/DEF:$(call gb_UnpackedTarball_get_dir,ginac)/instdir/libginac.def \
))
endif

$(eval $(call gb_Library_use_externals,imath,\
	cln \
	ginac \
))

$(eval $(call gb_Library_use_libraries,imath,\
        comphelper \
        cppu \
        cppuhelper \
        sal \
))

$(eval $(call gb_Library_add_grammars,imath,\
        imath/source/smathparser \
))

$(call gb_YaccTarget_get_target,imath/source/smathparser) : T_YACCFLAGS := -d -osmathparser.cxx

$(eval $(call gb_Library_add_scanners,imath,\
        imath/source/smathlexer \
))

$(call gb_LexTarget_get_scanner_target,imath/source/smathlexer) : T_LEXFLAGS := -o smathlexer.cxx

$(eval $(call gb_Library_use_custom_headers,imath,\
        officecfg/registry \
))

$(eval $(call gb_Library_use_sdk_api,imath))

$(eval $(call gb_Library_add_exception_objects,imath,\
    imath/source/alignblock \
    imath/source/differential \
    imath/source/eqc \
    imath/source/equation \
    imath/source/exderivative \
    imath/source/expression \
    imath/source/extintegral \
    imath/source/extsymbol \
    imath/source/func \
    imath/source/funcmgr \
    imath/source/hardfuncs \
    imath/source/imathparse \
    imath/source/imathutils \
    imath/source/iFormulaLine \
    imath/source/iIterator \
    imath/source/msgdriver \
    imath/source/operands \
    imath/source/option \
    imath/source/printing \
    imath/source/settingsmanager \
    imath/source/stringex \
    imath/source/unit \
    imath/source/unitmgr \
    imath/source/utils \
))

# vim: set noet sw=4 ts=4:
