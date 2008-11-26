#include <stdio.h>
#include "comUtility.h" 

IDispatch* CreateComDispatch(REFCLSID clsid,IDispatch** ppDisp) 
{ 
    // initialize COM 
    static int init = 1;
    int i; 
    if(init) 
    { 
        if(CoInitialize(NULL)!=S_OK) return 0; 
        init = 0; 
    } 
    // create the control
    HRESULT hRet = CoCreateInstance(clsid, 
                                    NULL,  
                                    0x7, 
                                    IID_IDispatch, 
                                    (void**)(ppDisp) ); 
    // examine return value 
    if(hRet!=S_OK)
    {   
        *ppDisp = 0; 
    } 
    return *ppDisp; 
} 

// local helper function to convert char string to unicode string 
static unsigned short* GetBuffer(const char* input) 
{ 
    int nSize = 1; 
    if(input!=0) 
    { 
        nSize += strlen(input); 
    } 
    short* output = new short[nSize]; 
    for(int i=0;i<nSize-1;i++) 
    { 
        output[i] = input[i]; 
    } 
    output[nSize-1] = 0; 
    return (unsigned short*)output; 
} 

int CallStringMethodByID(IDispatch* pDisp, 
                         const DISPID dispid, 
                         const int nParamCount, 
                         const char* pInputParam[], 
                         VARIANT* pOutput)  
{ 
    HRESULT hRet; 
    // build parameter list 
    DISPPARAMS callParams = {NULL,NULL,0,0}; 
    if(nParamCount>0) 
    { 
        callParams.cArgs = nParamCount; 
        callParams.rgvarg = new VARIANTARG[nParamCount]; 
        unsigned short * output; 
        for(int i=0;i<nParamCount;i++) 
        { 
            output = GetBuffer(pInputParam[nParamCount-i-1]); 
            callParams.rgvarg[i].bstrVal = ::SysAllocString((const OLECHAR *)output); 
            callParams.rgvarg[i].vt = VT_BSTR; 
            delete []output; 
        } 
    } 

    // invoke the method 
    unsigned int nArgErr; 
    hRet = pDisp->Invoke(dispid, 
                        IID_NULL, 
                        LOCALE_SYSTEM_DEFAULT, 
                        DISPATCH_PROPERTYGET, 
                        //DISPATCH_METHOD, 
                        &callParams,
                        pOutput, 
                        NULL, 
                        &nArgErr); 

    // examine the return value 
    if(hRet!=S_OK) 
    {
        if(nParamCount>0) delete [](callParams.rgvarg); 
        return 0; 
    } 
    if(nParamCount>0) delete [](callParams.rgvarg); 
    return 1; 
} 

int CallStringMethodByName(IDispatch* pDisp, 
                           const char* pName, 
                           const int nParamCount, 
                           const char* pInputParam[], 
                           VARIANT* pOutput) 
{ 
    HRESULT hRet; 
    DISPID dispid; 
    unsigned short* pOLEName = GetBuffer(pName); 
    // get the id of the method 
    hRet = pDisp->GetIDsOfNames(IID_NULL, 
                                (LPOLESTR *)&pOLEName, 
                                1, 
                                LOCALE_SYSTEM_DEFAULT, 
                                &dispid); 

    delete []pOLEName; 
    if(hRet!=S_OK) 
    { 
        return 0; 
    } 
    // invoke the method by id 
    return CallStringMethodByID(pDisp, 
                                dispid, 
                                nParamCount, 
                                pInputParam, 
                                pOutput); 
} 
