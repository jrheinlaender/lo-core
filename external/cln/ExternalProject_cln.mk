# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the LibreOffice project.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

$(eval $(call gb_ExternalProject_ExternalProject,cln))

$(eval $(call gb_ExternalProject_use_autoconf,cln,configure))
$(eval $(call gb_ExternalProject_use_autoconf,cln,build))

$(eval $(call gb_ExternalProject_register_targets,cln,\
	configure \
	build \
))

cln_CPPCLAGS="$(CPPFLAGS)"

# Note: --prefix is also written into pkg-config file cln.pc, which is then used by GiNaC
# Note: It would be desirable to build CLN (and GiNaC) as a DLL on Windows. But this proves very difficult. Some notes:
#       - Configure: --enabled-shared --disable-static
#       - Configure: The function func_cc_basename() returns "g++-wrapper" but it must return "cl" (could be patched)
#       - Configure: Add LDFLAGS="-no-undefined" to the call in this file
#       - Build: Compiling cl_asm.S and cl_asm_GF2.S results in no code, cl.exe does not create the files thus they are missing in the linking stage (removing them from Makefile.am/.in solves this)
#       - Libtool: nm cannot demangle the MSVC symbols, thus the .exp file is incomplete and linking to the resulting DLL results in lots of unresolved symbols
#       - Libtool: dlltool exists and is found by configure, but not used by libtool (instead nm is called)
#       - MS Link: Refuses to link the DLL as long as multiply defined and undefined symbols exist (this can be solved by two patches)
#       - MS Link: The syntax -Wl,-DLL,... is not understood, it must be -link -DLL ... (libtool could be patched)
# Note on patches:
# 1. dll_multiply_defined_symbols.patch (first time declared/defined inline, second time declared extern and defined in some .cc file)
# - remove macro ALL_cl_LF_OPERATIONS_SAME_PRECISION in src/float/lfloat/cl_LF.h
#   defines "inline const cl_LF operator- (const cl_LF& x, const cl_LF& y)"
#   cl_LF_hypot.cc executes this macro but also pulls in "extern const cl_LF operator- (const cl_LF& x, const cl_LF& y)" from lfloat.h
# - invalidate minusp methods in src/integer/elem/cl_I_minusp.cc, src/integer/elem/cl_I_zerop.cc, src/rational/elem/cl_RA_minusp.cc, src/rational/elem/cl_RA_zerop.cc
#   These methods are never used anyway!
# 2. dll_multiply_defined_symbols.patch (compiler hints at where they are defined but is missing the declaration)
# - declare them in GV_integer.h etc. etc.
#
$(call gb_ExternalProject_get_state_target,cln,build): $(call gb_ExternalProject_get_state_target,cln,configure)
	$(call gb_Trace_StartRange,cln,EXTERNAL)
	+$(call gb_ExternalProject_run,build,\
		cd src && $(MAKE) install \
	)
	$(call gb_Trace_EndRange,cln,EXTERNAL)

$(call gb_ExternalProject_get_state_target,cln,configure) :
	$(call gb_Trace_StartRange,cln,EXTERNAL)
ifeq ($(COM),MSC)
	$(call gb_ExternalProject_run,configure,\
		MAKE=$(MAKE) $(gb_RUN_CONFIGURE) ./configure \
			--build=$(BUILD_PLATFORM) \
			--host=$(HOST_PLATFORM) \
			--with-pic \
			--disable-shared --enable-static \
			--without-gmp \
			--prefix=$(call gb_UnpackedTarball_get_dir,cln)/instdir \
			--includedir=$(call gb_UnpackedTarball_get_dir,cln)/include \
			$(if $(cln_CPPFLAGS),CPPFLAGS='$(cln_CPPFLAGS)') \
			CPPFLAGS="$(CPPFLAGS) -MD -DNO_ASM -EHsc" \
			CXXFLAGS="$(CXXFLAGS) $(gb_EMSCRIPTEN_CPPFLAGS) $(if $(ENABLE_OPTIMIZED),$(gb_COMPILEROPTFLAGS),$(gb_COMPILERNOOPTFLAGS)) $(if $(debug),$(gb_DEBUGINFO_FLAGS))" \
	)
else
	$(call gb_ExternalProject_run,build,\
		$(gb_RUN_CONFIGURE) ./configure  \
			--with-pic \
			--enable-shared --disable-static \
			--without-gmp \
			--prefix=$(call gb_UnpackedTarball_get_dir,cln)/instdir --includedir=$(call gb_UnpackedTarball_get_dir,cln)/include \
			$(if $(CROSS_COMPILING),--build=$(BUILD_PLATFORM) --host=$(HOST_PLATFORM)) \
			$(if $(filter AIX,$(OS)),CFLAGS="-D_LINUX_SOURCE_COMPAT") \
			$(if $(cln_CPPFLAGS),CPPFLAGS='$(cln_CPPFLAGS)') \
			CXXFLAGS="$(CXXFLAGS) $(gb_EMSCRIPTEN_CPPFLAGS) $(if $(ENABLE_OPTIMIZED),$(gb_COMPILEROPTFLAGS),$(gb_COMPILERNOOPTFLAGS)) $(if $(debug),$(gb_DEBUGINFO_FLAGS))" \
		&& cd src && $(MAKE) install \
	)
endif
	$(call gb_Trace_EndRange,cln,EXTERNAL)

# vim: set noet sw=4 ts=4:
