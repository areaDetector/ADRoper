/* roper.cpp
 *
 * This is a driver for Roper Scientific area detectors.
 *
 * Author: Mark Rivers
 *         University of Chicago
 *
 * Created: November 26, 2008
 *
 */
 
#include <stddef.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include <epicsTime.h>
#include <epicsThread.h>
#include <epicsEvent.h>
#include <epicsMutex.h>
#include <epicsString.h>
#include <epicsStdio.h>
#include <epicsMutex.h>
#include <cantProceed.h>

#include "ADStdDriverParams.h"
#include "NDArray.h"
#include "ADDriver.h"

#include "drvRoper.h"
#include "XYDispDriver.h"
#include "WinX32_EXP_CMD.h"
#include "WinX32_DM_CMD.h"


static const char *driverName = "drvRoper";

class roper : public ADDriver {
public:
    roper(const char *portName, const char *WinX32Name,
                int maxBuffers, size_t maxMemory);
                 
    /* These are the methods that we override from ADDriver */
    virtual asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value);
    virtual asynStatus writeFloat64(asynUser *pasynUser, epicsFloat64 value);
    virtual void setShutter(int open);
    virtual asynStatus drvUserCreate(asynUser *pasynUser, const char *drvInfo, 
                                     const char **pptypeName, size_t *psize);
    void report(FILE *fp, int details);
                                        
    void roperTask();
    asynStatus setROI();

    /* Our data */
    epicsEventId startEventId;
    epicsEventId stopEventId;
    XYDispDriver *pWinX32App;
    XYDispDriver *pExpSetup;
    XYDispDriver *pDocFile;
    XYDispDriver *pROIRect;
};

/* If we have any private driver parameters they begin with ADFirstDriverParam and should end
   with ADLastDriverParam, which is used for setting the size of the parameter library table */
typedef enum {
    RoperDummy 
        = ADFirstDriverParam,
    ADLastDriverParam
} RoperParam_t;

static asynParamString_t RoperParamString[] = {
    {RoperDummy,          "DUMMY"}
};

#define NUM_ROPER_PARAMS (sizeof(RoperParamString)/sizeof(RoperParamString[0]))

void printCOMError() {
    IErrorInfo *pErrInfo;
    HRESULT hr;
    BSTR *pSource=NULL, *pDescription=NULL;
    
    hr = ::GetErrorInfo(0, &pErrInfo);
    if (FAILED(hr)) {
        printf("GetErrorInfo failed\n");
        return;
    }
printf("printCOMError, GetErrorInfo OK, pErrInfo=%p\n", pErrInfo);
    if (!pErrInfo) return;
    hr = pErrInfo->GetSource(pSource);
    if (FAILED(hr)) {
        printf("GetSource failed\n");
        return;
    }
    hr = pErrInfo->GetDescription(pDescription);
    if (FAILED(hr)) {
        printf("GetDescription failed\n");
        return;
    }
    printf("COM error: source=%S, description=%S\n", pSource, pDescription);
}
    
asynStatus roper::setROI() {
    int minX, minY, sizeX, sizeY, binX, binY;
    double ROILeft, ROIRight, ROITop, ROIBottom;
    asynStatus status;
    VARIANT *pStatus, varParam;
    const char *functionName = "setROI";
    IDispatch *pROIRect;
    
    new (CoInit);
    status = getIntegerParam(ADMinX,  &minX);
    status = getIntegerParam(ADMinY,  &minY);
    status = getIntegerParam(ADSizeX, &sizeX);
    status = getIntegerParam(ADSizeY, &sizeY);
    status = getIntegerParam(ADBinX,  &binX);
    status = getIntegerParam(ADBinY,  &binY);
    ROILeft = minX;
    ROIRight = minX + sizeX;
    ROITop = minY;
    ROIBottom = minY + sizeY;
    
    pStatus = this->pROIRect->InvokeMethod("Set", ROITop, ROILeft, ROIBottom, ROIRight, binX, binY);
    if (!pStatus) {
        printf("%s:%s error calling ActiveX interface to set ROIRect\n",
            driverName, functionName);
        printCOMError();
        return(asynError);
    }
    pStatus = this->pExpSetup->InvokeMethod("ClearROIs");
    if (!pStatus) {
        printf("%s:%s error calling ActiveX interface to clear ROIs\n",
            driverName, functionName);
        printCOMError();
        return(asynError);
    }
    varParam.vt = VT_I4;
    varParam.lVal = 1;
    pStatus = this->pExpSetup->InvokeMethod("SetParam", EXP_USEROI, &varParam);
    if (!pStatus) {
        printf("%s:%s error calling ActiveX interface to set EXP_USEROI\n",
            driverName, functionName);
        printCOMError();
        return(asynError);
    }
    pROIRect = this->pROIRect->GetDispatch();
    pStatus = this->pExpSetup->InvokeMethod("SetROI", pROIRect);
    if (!pStatus) {
        printf("%s:%s error calling ActiveX interface to set ROI 1\n",
            driverName, functionName);
        printCOMError();
        return(asynError);
    }
    return(asynSuccess);
}

void roper::setShutter(int open)
{
    int shutterMode;
    
    getIntegerParam(ADShutterMode, &shutterMode);
    if (shutterMode == ADShutterModeDetector) {
        /* Simulate a shutter by just changing the status readback */
        setIntegerParam(ADShutterStatus, open);
    } else {
        /* For no shutter or EPICS shutter call the base class method */
        ADDriver::setShutter(open);
    }
}


static void roperTaskC(void *drvPvt)
{
    roper *pPvt = (roper *)drvPvt;
    
    pPvt->roperTask();
}

void roper::roperTask()
{
    /* This thread computes new image data and does the callbacks to send it to higher layers */
    int status = asynSuccess;
    int dataType;
    int imageSizeX, imageSizeY, imageSize;
    int imageCounter;
    int numImages, numImagesCounter;
    int imageMode;
    int arrayCallbacks;
    int acquire, autoSave;
    NDArray *pImage;
    double acquireTime, acquirePeriod, delay;
    epicsTimeStamp startTime, endTime;
    double elapsedTime;
    const char *functionName = "simTask";
    VARIANT *pStatus, varParam;

    epicsMutexLock(this->mutexId);
    /* Loop forever */
    while (1) {
        /* Is acquisition active? */
        getIntegerParam(ADAcquire, &acquire);
        
        /* If we are not acquiring then wait for a semaphore that is given when acquisition is started */
        if (!acquire) {
            setIntegerParam(ADStatus, ADStatusIdle);
            callParamCallbacks();
            /* Release the lock while we wait for an event that says acquire has started, then lock again */
            asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
                "%s:%s: waiting for acquire to start\n", driverName, functionName);
            epicsMutexUnlock(this->mutexId);
            status = epicsEventWait(this->startEventId);
            epicsMutexLock(this->mutexId);
            setIntegerParam(ADNumImagesCounter, 0);
        }
        
        /* We are acquiring. */
        /* Get the current time */
        epicsTimeGetCurrent(&startTime);
        
        /* Get the exposure parameters */
        getDoubleParam(ADAcquireTime, &acquireTime);
        getDoubleParam(ADAcquirePeriod, &acquirePeriod);
        
        setIntegerParam(ADStatus, ADStatusAcquire);
        
        /* Open the shutter */
        setShutter(ADShutterOpen);

        /* Call the callbacks to update any changes */
        callParamCallbacks();

        /* Collect the frame(s) */
        /* Stop current exposure, if any */
        pStatus = this->pExpSetup->InvokeMethod("Stop");
        pStatus = this->pDocFile->InvokeMethod("Close");
        pStatus = this->pExpSetup->InvokeMethod("Start2");
        /* The status returned should contain a pointer to the DocFile object */
       
        /* Close the shutter */
        setShutter(ADShutterClosed);
        setIntegerParam(ADStatus, ADStatusReadout);
        /* Call the callbacks to update any changes */
        callParamCallbacks();
        
        pImage = this->pArrays[0];
        
        /* Get the current parameters */
        getIntegerParam(ADImageSizeX, &imageSizeX);
        getIntegerParam(ADImageSizeY, &imageSizeY);
        getIntegerParam(ADImageSize,  &imageSize);
        getIntegerParam(ADDataType,   &dataType);
        getIntegerParam(ADAutoSave,   &autoSave);
        getIntegerParam(ADImageCounter, &imageCounter);
        getIntegerParam(ADNumImages, &numImages);
        getIntegerParam(ADNumImagesCounter, &numImagesCounter);
        getIntegerParam(ADImageMode, &imageMode);
        getIntegerParam(ADArrayCallbacks, &arrayCallbacks);
        imageCounter++;
        numImagesCounter++;
        setIntegerParam(ADImageCounter, imageCounter);
        setIntegerParam(ADNumImagesCounter, numImagesCounter);
        
        /* Put the frame number and time stamp into the buffer */
        pImage->uniqueId = imageCounter;
        pImage->timeStamp = startTime.secPastEpoch + startTime.nsec / 1.e9;

        if (arrayCallbacks) {
            /* Get the data from the DocFile */
                    
            /* Call the NDArray callback */
            /* Must release the lock here, or we can get into a deadlock, because we can
             * block on the plugin lock, and the plugin can be calling us */
            epicsMutexUnlock(this->mutexId);
            asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
                 "%s:%s: calling imageData callback\n", driverName, functionName);
            doCallbacksGenericPointer(pImage, NDArrayData, 0);
            epicsMutexLock(this->mutexId);
        }

        /* See if acquisition is done */
        if ((imageMode == ADImageSingle) ||
            ((imageMode == ADImageMultiple) && 
             (numImagesCounter >= numImages))) {
            setIntegerParam(ADAcquire, 0);
            asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
                  "%s:%s: acquisition completed\n", driverName, functionName);
        }
        
        /* Call the callbacks to update any changes */
        callParamCallbacks();
        getIntegerParam(ADAcquire, &acquire);
        
        /* If we are acquiring then sleep for the acquire period minus elapsed time. */
        if (acquire) {
            epicsTimeGetCurrent(&endTime);
            elapsedTime = epicsTimeDiffInSeconds(&endTime, &startTime);
            delay = acquirePeriod - elapsedTime;
            asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
                     "%s:%s: delay=%f\n",
                      driverName, functionName, delay);            
            if (delay >= 0.0) {
                /* We set the status to readOut to indicate we are in the period delay */
                setIntegerParam(ADStatus, ADStatusWaiting);
                callParamCallbacks();
                epicsMutexUnlock(this->mutexId);
                status = epicsEventWaitWithTimeout(this->stopEventId, delay);
                epicsMutexLock(this->mutexId);
            }
        }
    }
}


asynStatus roper::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
    int function = pasynUser->reason;
    int adstatus;
    asynStatus status = asynSuccess;
    VARIANT varParam;
    VARIANT *pStatus;

    varParam.wReserved1=0;
    varParam.wReserved2=0;
    varParam.wReserved3=0;
    
    /* Set the parameter and readback in the parameter library.  This may be overwritten when we read back the
     * status at the end, but that's OK */
    status = setIntegerParam(function, value);

    /* For a real detector this is where the parameter is sent to the hardware */
    switch (function) {
    case ADAcquire:
        getIntegerParam(ADStatus, &adstatus);
        if (value && (adstatus == ADStatusIdle)) {
            /* Send an event to wake up the simulation task.  
             * It won't actually start generating new images until we release the lock below */
            epicsEventSignal(this->startEventId);
        } 
        if (!value && (adstatus != ADStatusIdle)) {
            /* This was a command to stop acquisition */
            /* Send the stop event */
            epicsEventSignal(this->stopEventId);
        }
        break;
    case ADBinX:
    case ADBinY:
    case ADMinX:
    case ADMinY:
    case ADSizeX:
    case ADSizeY:
        this->setROI();
        break;
    case ADDataType:
        break;
    case ADNumImages:
        varParam.vt = VT_I2;
        varParam.iVal = value;
        pStatus = this->pExpSetup->InvokeMethod("SetParam", EXP_SEQUENTS, &varParam);
        break;
    case ADShutterControl:
        setShutter(value);
        break;
    }
    
    /* Do callbacks so higher layers see any changes */
    callParamCallbacks();
    
    if (status) 
        asynPrint(pasynUser, ASYN_TRACE_ERROR, 
              "%s:writeInt32 error, status=%d function=%d, value=%d\n", 
              driverName, status, function, value);
    else        
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, 
              "%s:writeInt32: function=%d, value=%d\n", 
              driverName, function, value);
    return status;
}


asynStatus roper::writeFloat64(asynUser *pasynUser, epicsFloat64 value)
{
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    VARIANT varParam;
    VARIANT *pStatus;

    /* Set the parameter and readback in the parameter library.  This may be overwritten when we read back the
     * status at the end, but that's OK */
    status = setDoubleParam(function, value);

    /* Changing any of the following parameters requires recomputing the base image */
    switch (function) {
    case ADAcquireTime:
        varParam.vt = VT_R4;
        varParam.fltVal = (epicsFloat32)value;
        pStatus = this->pExpSetup->InvokeMethod("SetParam", EXP_EXPOSURE, &varParam);
        break;
    case ADGain:
        break;
    }

    /* Do callbacks so higher layers see any changes */
    callParamCallbacks();
    if (status) 
        asynPrint(pasynUser, ASYN_TRACE_ERROR, 
              "%s:writeFloat64 error, status=%d function=%d, value=%f\n", 
              driverName, status, function, value);
    else        
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, 
              "%s:writeFloat64: function=%d, value=%f\n", 
              driverName, function, value);
    return status;
}


/* asynDrvUser routines */
asynStatus roper::drvUserCreate(asynUser *pasynUser,
                                      const char *drvInfo, 
                                      const char **pptypeName, size_t *psize)
{
    asynStatus status;
    int param;
    const char *functionName = "drvUserCreate";

    /* See if this is one of our standard parameters */
    status = findParam(RoperParamString, NUM_ROPER_PARAMS, 
                       drvInfo, &param);
                                
    if (status == asynSuccess) {
        pasynUser->reason = param;
        if (pptypeName) {
            *pptypeName = epicsStrDup(drvInfo);
        }
        if (psize) {
            *psize = sizeof(param);
        }
        asynPrint(pasynUser, ASYN_TRACE_FLOW,
                  "%s:%s: drvInfo=%s, param=%d\n", 
                  driverName, functionName, drvInfo, param);
        return(asynSuccess);
    }
    
    /* If not, then see if it is a base class parameter */
    status = ADDriver::drvUserCreate(pasynUser, drvInfo, pptypeName, psize);
    return(status);  
}
    
void roper::report(FILE *fp, int details)
{

    fprintf(fp, "Roper detector %s\n", this->portName);
    if (details > 0) {
        int nx, ny, dataType;
        getIntegerParam(ADSizeX, &nx);
        getIntegerParam(ADSizeY, &ny);
        getIntegerParam(ADDataType, &dataType);
        fprintf(fp, "  NX, NY:            %d  %d\n", nx, ny);
        fprintf(fp, "  Data type:         %d\n", dataType);
    }
    /* Invoke the base class method */
    ADDriver::report(fp, details);
}

extern "C" int roperConfig(const char *portName, const char *WinX32Name,
                                 int maxBuffers, size_t maxMemory)
{
    new roper(portName, WinX32Name, maxBuffers, maxMemory);
    return(asynSuccess);
}

roper::roper(const char *portName, const char *WinX32Name,
                         int maxBuffers, size_t maxMemory)

    : ADDriver(portName, 1, ADLastDriverParam, maxBuffers, maxMemory, 0, 0)

{
    int status = asynSuccess;
    const char *functionName = "roper";
    VARIANT *pVarOutput; 
 
    /* Create the XYDispDriver objects used to communicate with COM */
    new(CoInit);
    this->pWinX32App = new(XYDispDriver);
    this->pExpSetup  = new(XYDispDriver);
    this->pDocFile   = new(XYDispDriver);
    this->pROIRect   = new(XYDispDriver);
    /* Connect to the WinX32App and ExpSetup COM objects */
    if (!this->pWinX32App->CreateObject("WinX32.Winx32App.2")) {
        printf("%s:%s error creating WinX32App COM object\n",
            driverName, functionName);
        return;
    }
    if (!this->pExpSetup->CreateObject("WinX32.ExpSetup.2")) {
        printf("%s:%s error creating ExpSetup COM object\n",
            driverName, functionName);
        return;
    }
    if (!this->pExpSetup->CreateObject("WinX32.DocFile")) {
        printf("%s:%s error creating DocFile COM object\n",
            driverName, functionName);
        return;
    }
    if (!this->pROIRect->CreateObject("WinX32.ROIRect")) {
        printf("%s:%s error creating ROIRect COM object\n",
            driverName, functionName);
        return;
    }

    pVarOutput = this->pWinX32App->GetProperty("Version");
    if (!pVarOutput) {
        printf("%s:%s error calling ActiveX interface to get WinX32App version\n",
            driverName, functionName);
        printCOMError();
        return;
    } 
    printf("%s:%s WinX32App version = %S\n", 
        driverName, functionName, pVarOutput->pbstrVal);

    pVarOutput = this->pExpSetup->InvokeMethod("GetParam", EXP_XDIMDET);
    if (!pVarOutput) {
        printf("%s:%s error calling ActiveX interface to get ExpSetup X dimension\n",
            driverName, functionName);
        printCOMError();
        return;
    } 
printf("pVarOutput->vt=%d\n", pVarOutput->vt);
    if (pVarOutput) printf("%s:%s EXP_XDIMDET = %d\n", 
        driverName, functionName, pVarOutput->iVal);
    /* Create the epicsEvents for signaling to the acquisition task when acquisition starts and stops */
    this->startEventId = epicsEventCreate(epicsEventEmpty);
    if (!this->startEventId) {
        printf("%s:%s epicsEventCreate failure for start event\n", 
            driverName, functionName);
        return;
    }
    this->stopEventId = epicsEventCreate(epicsEventEmpty);
    if (!this->stopEventId) {
        printf("%s:%s epicsEventCreate failure for stop event\n", 
            driverName, functionName);
        return;
    }
    
    /* Set some default values for parameters */
    status =  setStringParam (ADManufacturer, "Roper Scientific");
    status |= setStringParam (ADModel, "Unknown");
    status |= setIntegerParam(ADImageMode, ADImageContinuous);
    status |= setDoubleParam (ADAcquireTime, .1);
    status |= setDoubleParam (ADAcquirePeriod, .5);
    status |= setIntegerParam(ADNumImages, 100);
    if (status) {
        printf("%s: unable to set camera parameters\n", functionName);
        return;
    }
    
    /* Create the thread that updates the images */
    status = (epicsThreadCreate("roperTask",
                                epicsThreadPriorityMedium,
                                epicsThreadGetStackSize(epicsThreadStackMedium),
                                (EPICSTHREADFUNC)roperTaskC,
                                this) == NULL);
    if (status) {
        printf("%s:%s epicsThreadCreate failure for Roper task\n", 
            driverName, functionName);
        return;
    }
}
