# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the LibreOffice project.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

$(eval $(call gb_ExternalProject_ExternalProject,ginac))

$(call gb_ExternalProject_use_external_project,ginac,cln)

$(eval $(call gb_ExternalProject_use_autoconf,ginac,configure))
$(eval $(call gb_ExternalProject_use_autoconf,ginac,build))

$(eval $(call gb_ExternalProject_register_targets,ginac,\
	configure \
	build \
))

ginac_CPPCLAGS=$(CPPFLAGS)

# Note: Make install is required to get a clean include file directory
# Note: It would be desirable to build GiNaC as a DLL on Windows. But this proves very difficult. Some notes:
#       General: See notes in ExternalProject_cln.mk
#       Configure: pkg-config does not recognize the DLL. Use CLN_CFLAGS="-I$(call gb_UnpackedTarball_get_dir,cln)/include/" CLN_LIBS="$(call gb_UnpackedTarball_get_dir,cln)/instdir/lib/libcln-6.dll"
$(call gb_ExternalProject_get_state_target,ginac,build): $(call gb_ExternalProject_get_state_target,ginac,configure)
	$(call gb_Trace_StartRange,ginac,EXTERNAL)
ifneq ($(COM),MSC)
	+$(call gb_ExternalProject_run,build,\
	cd ginac && $(MAKE) install \
	)
else
	+$(call gb_ExternalProject_run,build,\
	cd ginac && $(MAKE) install && \
	dlltool --export-all-symbols -z $(call gb_UnpackedTarball_get_dir,ginac)/instdir/libginac.def $(call gb_UnpackedTarball_get_dir,ginac)/ginac/*.obj \
	)
endif
	$(call gb_Trace_EndRange,ginac,EXTERNAL)

# Note: The setting of CPPFLAGS and CXXFLAGS is ignored by the ginac configure script?
# TODO: Handle linker warning LNK4102: export of deleting destructor 'public: virtual void * __ptr64 __cdecl GiNaC::spinidx::`scalar deleting destructor'(unsigned int) __ptr64'; image may not run correctly
$(call gb_ExternalProject_get_state_target,ginac,configure):
	$(call gb_Trace_StartRange,ginac,EXTERNAL)
ifeq ($(COM),MSC)
	$(call gb_ExternalProject_run,configure,\
		MAKE=$(MAKE) $(gb_RUN_CONFIGURE) ./configure \
			--build=$(BUILD_PLATFORM) \
			--host=$(HOST_PLATFORM) \
			--with-pic \
			--disable-shared --enable-static \
			PKG_CONFIG_PATH=$(call gb_UnpackedTarball_get_dir,cln) \
			--prefix=$(call gb_UnpackedTarball_get_dir,ginac)/instdir \
			$(if $(ginac_CPPFLAGS),CPPFLAGS='$(ginac_CPPFLAGS)') \
			CPPFLAGS="$(CPPFLAGS) -MD -EHsc -Zc:__cplusplus" \
			CXXFLAGS="$(CXXFLAGS) $(gb_EMSCRIPTEN_CPPFLAGS) $(if $(ENABLE_OPTIMIZED),$(gb_COMPILEROPTFLAGS),$(gb_COMPILERNOOPTFLAGS)) $(if $(debug),$(gb_DEBUGINFO_FLAGS))" \
	)
else
	$(call gb_ExternalProject_run,configure,\
		$(gb_RUN_CONFIGURE) ./configure \
			--with-pic \
			--enable-shared --disable-static \
			PKG_CONFIG_PATH=$(call gb_UnpackedTarball_get_dir,cln) \
			--prefix=$(call gb_UnpackedTarball_get_dir,ginac)/instdir \
			$(if $(CROSS_COMPILING),--build=$(BUILD_PLATFORM) --host=$(HOST_PLATFORM))\
			$(if $(filter AIX,$(OS)),CFLAGS="-D_LINUX_SOURCE_COMPAT") \
			$(if $(ginac_CPPFLAGS),CPPFLAGS='$(ginac_CPPFLAGS)') \
			CXXFLAGS="$(CXXFLAGS) $(gb_EMSCRIPTEN_CPPFLAGS) $(if $(ENABLE_OPTIMIZED),$(gb_COMPILEROPTFLAGS),$(gb_COMPILERNOOPTFLAGS)) $(if $(debug),$(gb_DEBUGINFO_FLAGS))" \
	)
endif
	$(call gb_Trace_EndRange,ginac,EXTERNAL)

# vim: set noet sw=4 ts=4:
