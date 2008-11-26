/* drvRoperEpics.c
 *
 * This is the EPICS dependent code for the driver for the Roper area detector.
 * By making this separate file for the EPICS dependent code the driver itself
 * only needs libCom from EPICS for OS-independence.
 *
 * Author: Mark Rivers
 *         University of Chicago
 *
 * Created:  November 26, 2008
 *
 */
 
#include <iocsh.h>
#include <drvSup.h>
#include <epicsExport.h>

#include "drvRoper.h"


/* Code for iocsh registration */

/* roperConfig */
static const iocshArg roperConfigArg0 = {"Port name", iocshArgString};
static const iocshArg roperConfigArg1 = {"WinX32 name", iocshArgString};
static const iocshArg roperConfigArg2 = {"maxBuffers", iocshArgInt};
static const iocshArg roperConfigArg3 = {"maxMemory", iocshArgInt};
static const iocshArg * const roperConfigArgs[] =  {&roperConfigArg0,
                                                    &roperConfigArg1,
                                                    &roperConfigArg2,
                                                    &roperConfigArg3};
static const iocshFuncDef configRoper = {"roperConfig", 4, roperConfigArgs};
static void configRoperCallFunc(const iocshArgBuf *args)
{
    roperConfig(args[0].sval, args[1].sval, args[2].ival, args[3].ival);
}


static void roperRegister(void)
{

    iocshRegister(&configRoper, configRoperCallFunc);
}

epicsExportRegistrar(roperRegister);

