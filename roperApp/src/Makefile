TOP=../..
include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE

#======== DETECTOR LIBRARY ==============

ifeq (win32-x86, $(findstring win32-x86, $(T_A)))
  LIBRARY_IOC = roper
  LIB_SRCS += roper.cpp
endif

ifeq ($(STATIC_BUILD), NO)
USR_CXXFLAGS += -D_AFXDLL
endif

DBD += roperSupport.dbd

include $(ADCORE)/ADApp/commonLibraryMakefile

#=============================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

