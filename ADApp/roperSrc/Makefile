TOP=../..
include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#USR_CFLAGS += 
#USR_CXXFLAGS +=

LIBRARY_IOC_WIN32 = roper
LIBRARY_IOC_cygwin32 = roper
LIB_SRCS += XYDispDriver.cpp
LIB_SRCS += roper.cpp
LIB_SRCS += drvRoperEpics.c
LIB_LIBS += ADBase ADPlugin netCDF
LIB_LIBS += asyn
LIB_LIBS += $(EPICS_BASE_IOC_LIBS)

#==================================
PROD_IOC_WIN32 += roperApp
PROD_IOC_cygwin32 += roperApp
# <name>.dbd will be created from <name>Include.dbd
DBD += roperApp.dbd
DBD += roperSupport.dbd

# <name>_registerRecordDeviceDriver.cpp will be created from <name>.dbd
roperApp_SRCS_DEFAULT += roperApp_registerRecordDeviceDriver.cpp roperAppMain.cpp

# Add locally compiled object code
roperApp_LIBS += roper
roperApp_LIBS += ADPlugin ADBase netCDF
roperApp_LIBS += calc sscan autosave 
roperApp_LIBS += asyn
roperApp_LIBS += $(EPICS_BASE_IOC_LIBS)

roperApp_SYS_LIBS += ole32 oleaut32
roperApp_SYS_LIBS_cygwin32 += rpc

#=============================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

