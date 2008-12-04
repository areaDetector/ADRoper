//#define _AFXDLL
#include "stdafx.h"
#include "CWinx32App2.h"
#include "CExpSetup2.h"
#include "CDocFile4.h"
#include "CROIRect.h"

int main(int argc, void **argv) 
{
    CWinx32App2 *pWinx32App = new(CWinx32App2);
    CExpSetup2  *pExpSetup  = new(CExpSetup2);
    CDocFile4   *pDocFile   = new(CDocFile4);
    CROIRect    *pROIRect   = new(CROIRect);
    IDispatch  *pDocFileDispatch;
    VARIANT varResult;
    VARIANT varArg;
    HRESULT hr;
    short result;
    CString version;
    SAFEARRAY *pData;
    short *pVarData;


    hr = CoInitialize(NULL);
    if (hr != S_OK) {
        printf("error calling CoInitialize\n");
    }
    /* Connect to the WinX32App and ExpSetup COM objects */
    if (!pWinx32App->CreateDispatch("WinX32.Winx32App.2")) {
        printf("error creating WinX32App COM object\n");
    }
    if (!pExpSetup->CreateDispatch("WinX32.ExpSetup.2")) {
        printf("error creating ExpSetup COM object\n");
    }
    if (!pROIRect->CreateDispatch("WinX32.ROIRect")) {
        printf("error creating ROIRect COM object\n");
    }

    version = pWinx32App->get_Version();
    printf("Version = %S\n", version);
   
    varResult = pExpSetup->GetParam(EXP_XDIMDET, &result);
    printf("XDIMDET = %d\n", varResult.lVal);

    pROIRect->Set(1, 1, 2, 2, 1, 1);
    pExpSetup->ClearROIs();
    pExpSetup->SetROI(pROIRect->m_lpDispatch);
    VariantInit(&varArg);
    varArg.vt = VT_I2;
    varArg.iVal = 1;
    pExpSetup->SetParam(EXP_USEROI, &varArg);

    pDocFileDispatch = pExpSetup->Start2(&varResult);
    Sleep(2000);
    pDocFile->AttachDispatch(pDocFileDispatch);
    VariantInit(&varArg);
    pDocFile->GetFrame(1, &varArg);
    pData = varArg.parray;
    SafeArrayAccessData(pData, (void **)&pVarData);
    printf("Data = %d, %d\n", pVarData[0], pVarData[1]);
    SafeArrayUnaccessData(pData);

    return(0);
}
