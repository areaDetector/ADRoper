// *************** file: ConsoleUtility.h *************************** 

#ifndef CONSOLEUTILITY_H 
#define CONSOLEUTILITY_H 

#include <wtypes.h> 

// create an ActiveX control object 
// return a pointer to the IDispatch interface if successful 
// return 0 otherwise 
IDispatch* CreateComDispatch(REFCLSID clsid,IDispatch** ppDisp); 

// call a method specified by its dispatch id with the given IDispatch pointer 
// return 1 if successful, otherwise return 0 
int CallStringMethodByID(IDispatch* pDisp, 
                         const DISPID dispid, 
                         const int nParamCount, 
                         const char* pInputParam[], 
                         VARIANT* pOutput);

// call a method specified by its name with the given IDispatch pointer 
// return 1 if successful, otherwise return 0 
int CallStringMethodByName(IDispatch* pDisp, 
                           const char* pName, 
                           const int nParamCount, 
                           const char* pInputParam[], 
                           VARIANT* pOutput);
#endif 

//*************** end: ConsoleUtility.h ************************ 


