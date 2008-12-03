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
    asynStatus getData(NDArray *pArray);
    asynStatus testData();

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
    
asynStatus roper::getData(NDArray *pArray) {

    VARIANT *pStatus;
    VARIANT varData;
    SAFEARRAY *pData;
    int dim;
    LONG lbound, ubound;
    int dims[ND_ARRAY_MAX_DIMS];
    int nDims;
    VARTYPE varType;
    NDDataType_t dataType;
    NDArrayInfo arrayInfo;
    void HUGEP *pVarData;
    const char *functionName = "getData";
        
    new (CoInit);
    VariantInit(&varData);
    pStatus = this->pDocFile->InvokeMethod("GetFrame", 1, &varData);
    if (!pStatus) {
        printf("%s:%s error calling ActiveX interface to GetFrame\n",
            driverName, functionName);
        return(asynError);
    }
    pData = varData.parray;
    nDims = SafeArrayGetDim(pData);
    for (dim=0; dim<nDims; dim++) {
        SafeArrayGetLBound(pData, dim+1, &lbound);
        SafeArrayGetUBound(pData, dim+1, &ubound);
        dims[dim] = ubound - lbound + 1;
    }
    SafeArrayGetVartype(pData, &varType);
    switch (varType) {
        case VT_I2:
            dataType = NDInt16;
            break;
        case VT_I4:
            dataType = NDInt32;
            break;
        case VT_R4:
            dataType = NDFloat32;
            break;
        case VT_R8:
            dataType = NDFloat64;
            break;
        default:
            printf("%d:%s: unknown data type = %d\n", varType);
            return(asynError);
    }
    pArray = this->pNDArrayPool->alloc(nDims, dims, dataType, 0, NULL);
    pArray->getInfo(&arrayInfo);
    SafeArrayAccessData(pData, &pVarData);
    memcpy(pArray->pData, pVarData, arrayInfo.totalBytes);
    SafeArrayUnaccessData(pData);
    return(asynSuccess);
}
    
asynStatus roper::testData() {

    VARIANT *pStatus;
    const char *functionName = "testData";
    NDArray *pArray=NULL;
    XYDispDriver *pDocWindows = new(XYDispDriver);
    XYDispDriver *pDocWindow = new(XYDispDriver);
    IDispatch *pDocWindowDispatch;
    IDispatch *pDocFileDispatch;
    
    if (!pDocWindows->CreateObject("WinX32.DocWindows")) {
        printf("%s:%s error creating DocWindows COM object\n",
            driverName, functionName);
        return(asynError);
    }
    if (!pDocWindow->CreateObject("WinX32.DocWindow")) {
        printf("%s:%s error creating DocWindow COM object\n",
            driverName, functionName);
        return(asynError);
    }
    pStatus = pDocWindows->InvokeMethod("GetActive");
    if (!pStatus) {
        printf("%s:%s error calling ActiveX interface to GetActive\n",
            driverName, functionName);
        return(asynError);
    }
    pDocWindowDispatch = pStatus->pdispVal;
    if (!pDocWindow->Attach(pDocWindowDispatch)) {
        printf("%s:%s error attaching pDocWindowDispatch\n",
            driverName, functionName);
        return(asynError);
    }
    pStatus = pDocWindow->InvokeMethod("GetDocument");
    if (!pStatus) {
        printf("%s:%s error calling ActiveX interface to GetDocument\n",
            driverName, functionName);
        return(asynError);
    }
    pDocFileDispatch = pStatus->pdispVal;
    if (!this->pDocFile->Attach(pDocFileDispatch)) {
        printf("%s:%s error attaching pDocFileDispatch\n",
            driverName, functionName);
        return(asynError);
    }
    this->getData(pArray);
    return(asynSuccess);
}
    


asynStatus roper::setROI() {
    int minX, minY, sizeX, sizeY, binX, binY;
    double ROILeft, ROIRight, ROITop, ROIBottom;
    asynStatus status;
    VARIANT *pStatus, varParam;
    const char *functionName = "setROI";
    IDispatch *pROIRect;

     new (CoInit);
    VariantInit(&varParam);
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
        return(asynError);
    }
    pStatus = this->pExpSetup->InvokeMethod("ClearROIs");
    if (!pStatus) {
        printf("%s:%s error calling ActiveX interface to clear ROIs\n",
            driverName, functionName);
        return(asynError);
    }
    varParam.vt = VT_I4;
    varParam.lVal = 1;
    pStatus = this->pExpSetup->InvokeMethod("SetParam", EXP_USEROI, &varParam);
    if (!pStatus) {
        printf("%s:%s error calling ActiveX interface to set EXP_USEROI\n",
            driverName, functionName);
        //return(asynError);
    }
    pROIRect = this->pROIRect->GetDispatch();
    pStatus = this->pExpSetup->InvokeMethod("SetROI", pROIRect);
    if (!pStatus) {
        printf("%s:%s error calling ActiveX interface to set ROI 1\n",
            driverName, functionName);
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
    const char* functionName="writeInt32";

    VariantInit(&varParam);
    
    /* Set the parameter and readback in the parameter library.  This may be overwritten when we read back the
     * status at the end, but that's OK */
    status = setIntegerParam(function, value);

    /* For a real detector this is where the parameter is sent to the hardware */
    switch (function) {
    case ADAcquire:
        getIntegerParam(ADStatus, &adstatus);
        if (value) {
            /* Send an event to wake up the simulation task.  
             * It won't actually start generating new images until we release the lock below */
            pStatus = this->pExpSetup->InvokeMethod("Start2", &varParam);
            if (!pStatus) {
                printf("%s:%s error calling ActiveX interface to Start2\n",
                    driverName, functionName);
            status = asynError;
            }
            //epicsEventSignal(this->startEventId);
        } 
        if (!value) {
            /* This was a command to stop acquisition */
            /* Send the stop event */
            pStatus = this->pExpSetup->InvokeMethod("Stop");
            if (!pStatus) {
                printf("%s:%s error calling ActiveX interface to Stop\n",
                    driverName, functionName);
            status = asynError;
            }
            //epicsEventSignal(this->stopEventId);
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
        if (!pStatus) {
            printf("%s:%s error calling ActiveX interface to set EXP_SEQUENTS\n",
                driverName, functionName);
            status = asynError;
        }
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
    VARIANT *pVarOutput, varArg;
    short result;
 
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

    /* Create the XYDispDriver objects used to communicate with COM */
    new(CoInit);
    VariantInit(&varArg);
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
    if (!this->pDocFile->CreateObject("WinX32.DocFile")) {
        printf("%s:%s error creating DocFile COM object\n",
            driverName, functionName);
        return;
    }
    if (!this->pROIRect->CreateObject("WinX32.ROIRect")) {
        printf("%s:%s error creating ROIRect COM object\n",
            driverName, functionName);
        return;
    }
    
    pVarOutput = this->pExpSetup->InvokeMethod("GetParam", EXP_XDIMDET, &result);
    if (!pVarOutput) {
        printf("%s:%s error calling ActiveX interface to get ExpSetup X dimension\n",
            driverName, functionName);
        return;
    } 
    
    if (pVarOutput) printf("%s:%s EXP_XDIMDET = %d\n", 
        driverName, functionName, pVarOutput->iVal);
}
