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
#include "stdafx.h"
#include "CWinx32App2.h"
#include "CExpSetup2.h"
#include "CDocFile4.h"
#include "CROIRect.h"

/* The following macro initializes COM for the default COINIT_MULTITHREADED model 
 * This needs to be done is each thread that can call the COM interfaces 
 * These threads are:
 * The thread that runs when the roper object is created (typically from st.cmd)
 * The roperTask thread that controls acquisition
 * The port driver thread that sets parameters */
#define INITIALIZE_COM CoInitializeEx(NULL, 0)
#define ERROR_MESSAGE_SIZE 256
/* The polling interval when checking to see if acquisition is complete */
#define ROPER_POLL_TIME .01

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
    NDArray *getData();
    asynStatus getStatus();
    asynStatus saveFile();
    
    /* Our data */
    epicsEventId startEventId;
    epicsEventId stopEventId;
    CWinx32App2 *pWinx32App;
    CExpSetup2  *pExpSetup;
    CDocFile4   *pDocFile;
    CROIRect    *pROIRect;
    char errorMessage[ERROR_MESSAGE_SIZE];
};

/* If we have any private driver parameters they begin with ADFirstDriverParam and should end
   with ADLastDriverParam, which is used for setting the size of the parameter library table */
typedef enum {
    RoperTemperature
        = ADFirstDriverParam,
    RoperComment1,
    RoperComment2,
    RoperComment3,
    RoperComment4,
    RoperComment5,
    ADLastDriverParam
} RoperParam_t;

static asynParamString_t RoperParamString[] = {
    {RoperTemperature,    "TEMPERATURE"},
    {RoperComment1,       "COMMENT1"},
    {RoperComment2,       "COMMENT2"},
    {RoperComment3,       "COMMENT3"},
    {RoperComment4,       "COMMENT4"},
    {RoperComment5,       "COMMENT5"}
};

#define NUM_ROPER_PARAMS (sizeof(RoperParamString)/sizeof(RoperParamString[0]))
    
asynStatus roper::saveFile() 
{
    char fullFileName[MAX_FILENAME_LEN];
    OLECHAR wideFullFileName[MAX_FILENAME_LEN];
    VARIANT varArg;
    BSTR fName;
    size_t len;
    asynStatus status=asynSuccess;
    const char *functionName="saveFile";
    
    VariantInit(&varArg);

    this->createFileName(MAX_FILENAME_LEN, fullFileName);
    mbstowcs_s(&len, wideFullFileName, MAX_FILENAME_LEN, fullFileName, MAX_FILENAME_LEN-1);
    varArg.vt = VT_BSTR;
    fName = SysAllocString(wideFullFileName);
    varArg.bstrVal = fName;
    try {
        this->pDocFile->SetParam(DM_FILENAME, &varArg);
        this->pDocFile->Save();
    }
    catch(CException *pEx) {
        pEx->GetErrorMessage(this->errorMessage, sizeof(this->errorMessage));
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
            "%s:%s: exception = %s\n", 
            driverName, functionName, this->errorMessage);
        pEx->Delete();
        status = asynError;
    }
    SysFreeString(fName);
    return(status);
}

NDArray *roper::getData() {

    NDArray *pArray = NULL;
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
        
    VariantInit(&varData);
    try {
        this->pDocFile->GetFrame(1, &varData);
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
                asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s:%s: unknown data type = %d\n", 
                    driverName, functionName, varType);
                return(NULL);
        }
        pArray = this->pNDArrayPool->alloc(nDims, dims, dataType, 0, NULL);
        pArray->getInfo(&arrayInfo);
        SafeArrayAccessData(pData, &pVarData);
        memcpy(pArray->pData, pVarData, arrayInfo.totalBytes);
        SafeArrayUnaccessData(pData);
        setIntegerParam(ADImageSize, arrayInfo.totalBytes);
        setIntegerParam(ADImageSizeX, dims[0]);
        setIntegerParam(ADImageSizeY, dims[1]);
        setIntegerParam(ADDataType, dataType);
    }
    catch(CException *pEx) {
        pEx->GetErrorMessage(this->errorMessage, sizeof(this->errorMessage));
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
            "%s:%s: exception = %s\n", 
            driverName, functionName, this->errorMessage);
        pEx->Delete();
        return(NULL);
    }
        
    return(pArray);
}

asynStatus roper::getStatus()
{
    short result;
    const char *functionName = "getStatus";
    VARIANT varResult;
    IDispatch pROIDispatch;
    double top, bottom, left, right;
    long minX, minY, sizeX, sizeY, binX, binY;
    
    try {
        varResult = pExpSetup->GetParam(EXP_REVERSE, &result);
        setIntegerParam(ADReverseX, varResult.lVal);
        varResult = pExpSetup->GetParam(EXP_FLIP, &result);
        setIntegerParam(ADReverseY, varResult.lVal);
        varResult = pExpSetup->GetParam(EXP_XDIMDET, &result);
        setIntegerParam(ADMaxSizeX, varResult.lVal);
        varResult = pExpSetup->GetParam(EXP_YDIMDET, &result);
        setIntegerParam(ADMaxSizeY, varResult.lVal);
        varResult = pExpSetup->GetParam(EXP_SEQUENTS, &result);
        setIntegerParam(ADNumImages, varResult.lVal);
        varResult = pExpSetup->GetParam(EXP_EXPOSURE, &result);
        setDoubleParam(ADAcquireTime, varResult.dblVal);
        pROIDispatch = pExpSetup->getROI(1);
        pROIRect->AttachDispatch(ROIDispatch);
        pROIRect->Get(&top, &left, &bottom, &right, &binX, &binY);
        minX = int(left);
        minY = int(top);
        sizeX = int(right)-int(left)+1;
        sizeY = int(bottom)-int(top)+1;
        setIntegerParam(ADMinX, minX);
        setIntegerParam(ADMinY, minY);
        setIntegerParam(ADSizeX, sizeX);
        setIntegerParam(ADSizeY, sizeX);
    }
    catch(CException *pEx) {
        pEx->GetErrorMessage(this->errorMessage, sizeof(this->errorMessage));
        printf("%s:%s: exception = %s\n", 
            driverName, functionName, this->errorMessage);
        pEx->Delete();
        return(asynError);
    }
    return(asynSuccess);
}

asynStatus roper::setROI() {
    int minX, minY, sizeX, sizeY, binX, binY, maxSizeX, maxSizeY;
    double ROILeft, ROIRight, ROITop, ROIBottom;
    asynStatus status;
    VARIANT  varArg;
    const char *functionName = "setROI";

    VariantInit(&varArg);
    status = getIntegerParam(ADMinX,  &minX);
    status = getIntegerParam(ADMinY,  &minY);
    status = getIntegerParam(ADSizeX, &sizeX);
    status = getIntegerParam(ADSizeY, &sizeY);
    status = getIntegerParam(ADBinX,  &binX);
    status = getIntegerParam(ADBinY,  &binY);
    status = getIntegerParam(ADMaxSizeX, &maxSizeX);
    status = getIntegerParam(ADMaxSizeY, &maxSizeY);
    if (minX < 1) {
        minX = 1;
        setIntegerParam(ADMinX, minX);
    }
    /* Make sure parameters are consistent, fix them if they are not */
    if (binX < 1) {
        binX = 1; 
        status = setIntegerParam(ADBinX, binX);
    }
    if (binY < 1) {
        binY = 1;
        status = setIntegerParam(ADBinY, binY);
    }
    if (minX < 1) {
        minX = 1; 
        status = setIntegerParam(ADMinX, minX);
    }
    if (minY < 1) {
        minY = 1; 
        status = setIntegerParam(ADMinY, minY);
    }
    /* The size must be a multiple of the binning or the controller can generate an error */
    if (sizeX < binX) sizeX = binX;    
    sizeX = (sizeX/binX) * binX;
    status = setIntegerParam(ADSizeX, sizeX);
    if (sizeY < binY) sizeY = binY;    
    sizeY = (sizeY/binY) * binY;
    status = setIntegerParam(ADSizeY, sizeY);
    if (minX > maxSizeX-binX) {
        minX = maxSizeX-binX; 
        status = setIntegerParam(ADMinX, minX);
    }
    if (minY > maxSizeY-binY) {
        minY = maxSizeY-binY; 
        status = setIntegerParam(ADMinY, minY);
    }
    if (minX+sizeX > maxSizeX) {
        sizeX = maxSizeX-minX; 
        status = setIntegerParam(ADSizeX, sizeX);
    }
    if (minY+sizeY > maxSizeY) {
        sizeY = maxSizeY-minY; 
        status = setIntegerParam(ADSizeY, sizeY);
    }

    ROILeft = minX;
    ROIRight = minX + sizeX - 1;
    ROITop = minY;
    ROIBottom = minY + sizeY - 1;
    
    try {
        this->pROIRect->Set(ROITop, ROILeft, ROIBottom, ROIRight, binX, binY);
        this->pExpSetup->ClearROIs();
        this->pExpSetup->SetROI(this->pROIRect->m_lpDispatch);
        varArg.vt = VT_I4;
        varArg.lVal = 1;
        this->pExpSetup->SetParam(EXP_USEROI, &varArg);
    }
    catch(CException *pEx) {
        pEx->GetErrorMessage(this->errorMessage, sizeof(this->errorMessage));
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
            "%s:%s: exception = %s\n", 
            driverName, functionName, this->errorMessage);
        pEx->Delete();
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
    int imageCounter;
    int numImages, numImagesCounter;
    int imageMode;
    int arrayCallbacks;
    int acquire, autoSave;
    NDArray *pImage;
    double acquireTime, acquirePeriod, delay;
    epicsTimeStamp startTime, endTime;
    double elapsedTime;
    const char *functionName = "roperTask";
    VARIANT varArg;
    IDispatch *pDocFileDispatch;
    HRESULT hr;
    short result;

    /* Initialize the COM system for this thread */
    hr = INITIALIZE_COM;
    if (hr == S_FALSE) {
        /* COM was already initialized for this thread */
        CoUninitialize();
    } else if (hr != S_OK) {
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
            "%s:%s: error initializing COM\n",
            driverName, functionName);
    }
    VariantInit(&varArg);

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
            getIntegerParam(ADAcquire, &acquire);
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

        try {
            /* Collect the frame(s) */
            /* Stop current exposure, if any */
            this->pExpSetup->Stop();
            this->pDocFile->Close();
            pDocFileDispatch = pExpSetup->Start2(&varArg);
            pDocFile->AttachDispatch(pDocFileDispatch);

            /* Wait for acquisition to complete, but allow acquire stop events to be handled */
            while (acquire) {
                status = epicsEventWaitWithTimeout(this->stopEventId, ROPER_POLL_TIME);
                if (status == epicsEventWaitOK) {
                    /* We got a stop event, abort acquisition */
                    this->pExpSetup->Stop();
                }
                varArg = pExpSetup->GetParam(EXP_RUNNING, &result);
                acquire = varArg.lVal;
            }
        }
        catch(CException *pEx) {
            pEx->GetErrorMessage(this->errorMessage, sizeof(this->errorMessage));
            asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: exception = %s\n", 
                driverName, functionName, this->errorMessage);
            pEx->Delete();
        }
        
        /* Close the shutter */
        setShutter(ADShutterClosed);
       /* Call the callbacks to update any changes */
        callParamCallbacks();
        
        /* Get the current parameters */
        getIntegerParam(ADAutoSave,         &autoSave);
        getIntegerParam(ADImageCounter,     &imageCounter);
        getIntegerParam(ADNumImages,        &numImages);
        getIntegerParam(ADNumImagesCounter, &numImagesCounter);
        getIntegerParam(ADImageMode,        &imageMode);
        getIntegerParam(ADArrayCallbacks,   &arrayCallbacks);
        imageCounter++;
        numImagesCounter++;
        setIntegerParam(ADImageCounter, imageCounter);
        setIntegerParam(ADNumImagesCounter, numImagesCounter);
        

        if (arrayCallbacks) {
            /* Get the data from the DocFile */
            pImage = this->getData();
            if (pImage)  {
                /* Put the frame number and time stamp into the buffer */
                pImage->uniqueId = imageCounter;
                pImage->timeStamp = startTime.secPastEpoch + startTime.nsec / 1.e9;
                /* Call the NDArray callback */
                /* Must release the lock here, or we can get into a deadlock, because we can
                 * block on the plugin lock, and the plugin can be calling us */
                epicsMutexUnlock(this->mutexId);
                asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
                     "%s:%s: calling imageData callback\n", driverName, functionName);
                doCallbacksGenericPointer(pImage, NDArrayData, 0);
                epicsMutexLock(this->mutexId);
                pImage->release();
            }
        }
        
        /* See if we should save the file */
        if (autoSave) this->saveFile();

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
                /* We set the status to indicate we are in the period delay */
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
    int currentlyAcquiring;
    asynStatus status = asynSuccess;
    VARIANT varArg;
    const char* functionName="writeInt32";

    VariantInit(&varArg);
    
    /* See if we are currently acquiring.  This must be done before the call to setIntegerParam below */
    getIntegerParam(ADAcquire, &currentlyAcquiring);
    
    /* Set the parameter and readback in the parameter library.  This may be overwritten when we read back the
     * status at the end, but that's OK */
    status = setIntegerParam(function, value);

    try {
        switch (function) {
        case ADAcquire:
            if (value && !currentlyAcquiring) {
                /* Send an event to wake up the Roper task.  
                 * It won't actually start generating new images until we release the lock below */
                epicsEventSignal(this->startEventId);
            } 
            if (!value && currentlyAcquiring) {
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
            varArg.vt = VT_I2;
            varArg.iVal = value;
            this->pExpSetup->SetParam(EXP_SEQUENTS, &varArg);
            break;
        case ADReverseX:
            varArg.vt = VT_I2;
            varArg.iVal = value;
            this->pExpSetup->SetParam(EXP_REVERSE, &varArg);
            break;
        case ADReverseY:
            varArg.vt = VT_I2;
            varArg.iVal = value;
            this->pExpSetup->SetParam(EXP_FLIP, &varArg);
            break;
        case ADShutterControl:
            setShutter(value);
            break;
        }
    }
    catch(CException *pEx) {
        pEx->GetErrorMessage(this->errorMessage, sizeof(this->errorMessage));
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
            "%s:%s: exception = %s\n", 
            driverName, functionName, this->errorMessage);
        pEx->Delete();
    }
    
    /* Read the actual state of the detector after this operation */
    this->getStatus();
    
    /* Do callbacks so higher layers see any changes */
    callParamCallbacks();
    
    if (status) 
        asynPrint(pasynUser, ASYN_TRACE_ERROR, 
              "%s:%s: error, status=%d function=%d, value=%d\n", 
              driverName, functionName, status, function, value);
    else        
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, 
              "%s:%s: function=%d, value=%d\n", 
              driverName, functionName, function, value);
    return status;
}


asynStatus roper::writeFloat64(asynUser *pasynUser, epicsFloat64 value)
{
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    VARIANT varArg;
    const char* functionName="writeInt32";

    VariantInit(&varArg);

    /* Set the parameter and readback in the parameter library.  This may be overwritten when we read back the
     * status at the end, but that's OK */
    status = setDoubleParam(function, value);

    /* Changing any of the following parameters requires recomputing the base image */
    try {
        switch (function) {
        case ADAcquireTime:
            varArg.vt = VT_R4;
            varArg.fltVal = (epicsFloat32)value;
            this->pExpSetup->SetParam(EXP_EXPOSURE, &varArg);
            break;
        case ADGain:
            break;
        }
    }
    catch(CException *pEx) {
        pEx->GetErrorMessage(this->errorMessage, sizeof(this->errorMessage));
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
            "%s:%s: exception = %s\n", 
            driverName, functionName, this->errorMessage);
        pEx->Delete();
    }

    /* Read the actual state of the detector after this operation */
    this->getStatus();

    /* Do callbacks so higher layers see any changes */
    callParamCallbacks();
    if (status) 
        asynPrint(pasynUser, ASYN_TRACE_ERROR, 
              "%s:%s, status=%d function=%d, value=%f\n", 
              driverName, functionName, status, function, value);
    else        
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, 
              "%s:%s: function=%d, value=%f\n", 
              driverName, functionName, function, value);
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
    HRESULT hr;

    /* Initialize the COM system for this thread */
    hr = INITIALIZE_COM;
    if (hr == S_FALSE) {
        /* COM was already initialized for this thread */
        CoUninitialize();
    } else if (hr != S_OK) {
        printf("%s:%s: error initializing COM\n",
            driverName, functionName);
    }

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
    VARIANT varResult;
    HRESULT hr;
    short result;
 
    /* Initialize the COM system for this thread */
    hr = INITIALIZE_COM;
    if (hr != S_OK) {
        printf("%s:%s: error initializing COM\n",
            driverName, functionName);
    }

    /* Create the COleDispatchDriver objects used to communicate with COM */
    VariantInit(&varResult);
    this->pWinx32App = new(CWinx32App2);
    this->pExpSetup  = new(CExpSetup2);
    this->pDocFile   = new(CDocFile4);
    this->pROIRect   = new(CROIRect);

    /* Connect to the WinX32App and ExpSetup COM objects */
    if (!pWinx32App->CreateDispatch("WinX32.Winx32App.2")) {
        printf("%s:%s: error creating WinX32App COM object\n",
            driverName, functionName);
        return;
    }
    if (!pExpSetup->CreateDispatch("WinX32.ExpSetup.2")) {
        printf("%s:%s: error creating ExpSetup COM object\n",
            driverName, functionName);
        return;
    }
    if (!pROIRect->CreateDispatch("WinX32.ROIRect")) {
        printf("%s:%s: error creating ROIRect COM object\n",
            driverName, functionName);
        return;
    }

    try {
        varResult = this->pExpSetup->GetParam(EXP_CONTROLLER_NAME, &result);
printf("EXP_CONTROLLER_NAME, vt=%d\n", varResult.vt);
    }
    catch(CException *pEx) {
        pEx->GetErrorMessage(this->errorMessage, sizeof(this->errorMessage));
        printf("%s:%s: exception = %s\n", 
            driverName, functionName, this->errorMessage);
        pEx->Delete();
    }

    /* Read the state of the detector */
    this->getStatus();

   /* Create the epicsEvents for signaling to the acquisition task when acquisition starts and stops */
    this->startEventId = epicsEventCreate(epicsEventEmpty);
    if (!this->startEventId) {
        printf("%s:%s: epicsEventCreate failure for start event\n", 
            driverName, functionName);
        return;
    }
    this->stopEventId = epicsEventCreate(epicsEventEmpty);
    if (!this->stopEventId) {
        printf("%s:%s: epicsEventCreate failure for stop event\n", 
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
        printf("%s:%s: epicsThreadCreate failure for Roper task\n", 
            driverName, functionName);
        return;
    }

    
}
