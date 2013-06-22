#ifdef _WIN32
#pragma warning(disable:4996) 
#pragma warning(disable:4127) 
#endif

#include "sphinx.h"
#include "sphinxutils.h"

#include "py_layer.h"

#if USE_PYTHON

#define LOC_CHECK(_hash,_key,_msg,_add) \
	if (!( _hash.Exists ( _key ) )) \
	{ \
	fprintf ( stdout, "ERROR: key '%s' not found " _msg "\n", _key, _add ); \
	return false; \
	}

CSphSource * SpawnSourcePython ( const CSphConfigSection & hSource, const char * sSourceName)
{
	assert ( hSource["type"]=="python" );

	LOC_CHECK ( hSource, "name", "in source '%s'.", sSourceName );
	
	CSphSource * pSrcPython = NULL;

	// FIXME: move the data source aware code -> pysource
	/*
	CSphSource_Python * pPySource = new CSphSource_Python ( sSourceName );
	if ( !pPySource->Setup ( hSource ) ) {
		if(pPySource->m_sError.Length())
			fprintf ( stdout, "ERROR: %s\n", pPySource->m_sError.cstr());
		SafeDelete ( pPySource );
	}
	pSrcPython = pPySource;
	*/

	return pSrcPython;
}

//////////////////////////////////////////////////////////////////////////
// get array of strings
#define LOC_GETAS(_sec, _arg,_key) \
	for ( CSphVariant * pVal = _sec(_key); pVal; pVal = pVal->m_pNext ) \
	_arg.Add ( pVal->cstr() );

// helper functions
#if USE_PYTHON

int init_python_layer_helpers()
{
	int nRet = 0;
	nRet = PyRun_SimpleString("import sys\nimport os\n");
	if(nRet) return nRet;
	//helper function to append path to env.
	nRet = PyRun_SimpleString("\n\
def __coreseek_set_python_path(sPath):\n\
	sPaths = [x.lower() for x in sys.path]\n\
	sPath = os.path.abspath(sPath)\n\
	if sPath not in sPaths:\n\
		sys.path.append(sPath)\n\
	#print sPaths\n\
\n");
	if(nRet) return nRet;
	// helper function to find data source
	nRet = PyRun_SimpleString("\n\
def __coreseek_find_pysource(sName): \n\
    pos = sName.find('.') \n\
    module_name = sName[:pos]\n\
    try:\n\
        exec ('%s=__import__(\"%s\")' % (module_name, module_name))\n\
        return eval(sName)\n\
    except ImportError, e:\n\
		print e\n\
		return None\n\
\n");
	return nRet;
}

#endif

bool	cftInitialize( const CSphConfigSection & hPython)
{
#if USE_PYTHON
	if (!Py_IsInitialized()) {
		Py_Initialize();
		//PyEval_InitThreads();

		if (!Py_IsInitialized()) {
			return false;
		}
		int nRet = init_python_layer_helpers();
		if(nRet != 0) {
			PyErr_Print();
			PyErr_Clear();
			return false;
		}
	}
	//init paths
	PyObject * main_module = NULL;
	//try //to disable -GX
	{
		
		CSphVector<CSphString>	m_dPyPaths;
		LOC_GETAS(hPython, m_dPyPaths, "path");
		///XXX: append system pre-defined path here.
		{
			main_module = PyImport_AddModule("__main__");  //+1
			//init paths
			PyObject* pFunc = PyObject_GetAttrString(main_module, "__coreseek_set_python_path");

			if(pFunc && PyCallable_Check(pFunc)){
				ARRAY_FOREACH ( i, m_dPyPaths )
				{
					PyObject* pArgsKey  = Py_BuildValue("(s)",m_dPyPaths[i].cstr() );
					PyObject* pResult = PyEval_CallObject(pFunc, pArgsKey);
					Py_XDECREF(pArgsKey);
					Py_XDECREF(pResult);
				}
			} // end if
			if (pFunc)
				Py_XDECREF(pFunc);
			//Py_XDECREF(main_module); //no needs to decrease refer to __main__ module, else will got a crash!
		}
	}/*
	catch (...) {
		PyErr_Print();
		PyErr_Clear(); //is function can be undefined
		Py_XDECREF(main_module);
		return false;
	}*/
	///XXX: hook the ext interface here.
	
	// initCsfHelper(); //the Csf 

	return true;
#endif
}

void			cftShutdown()
{

#if USE_PYTHON
		//FIXME: avoid the debug warning.
		if (Py_IsInitialized()) {
				//to avoid crash in release mode.
				Py_Finalize();
		}
#endif
		
}

int cftCreateObject(const char* dsName, PyObject** pp)
{
    PyObject* main_module = PyImport_AddModule("__main__");

    PyObject* m_pInstance = NULL;
    PyObject* pFunc = PyObject_GetAttrString(main_module, "__coreseek_find_pysource");
    PyObject* m_pTypeObj = NULL;
    if(pFunc && PyCallable_Check(pFunc)){
        PyObject* pArgsKey  = Py_BuildValue("(s)", dsName);
        m_pTypeObj = PyEval_CallObject(pFunc, pArgsKey);
        Py_XDECREF(pArgsKey);
    } // end if
    if (pFunc)
        Py_XDECREF(pFunc);

    if (m_pTypeObj == NULL || m_pTypeObj == Py_None) {
        sphDie("Can NOT found python class %s.\n", dsName);
        return 0;
    }

    if (!PyClass_Check(m_pTypeObj) && !PyType_Check(m_pTypeObj)) {
        Py_XDECREF(m_pTypeObj);
        sphDie("%s is NOT a Python class.\n", dsName);
        return -1; //not a valid type file
    }

    if(!m_pTypeObj||!PyCallable_Check(m_pTypeObj)){
        Py_XDECREF(m_pTypeObj);
        return  -2;
    }else{

        //PyObject* pargs  = Py_BuildValue("O", pConf); //+1
        PyObject* pArg  = Py_BuildValue("()");
        m_pInstance  = PyEval_CallObject(m_pTypeObj, pArg);
        if(!m_pInstance){
            PyErr_Print();
            Py_XDECREF(pArg);
            Py_XDECREF(m_pTypeObj);
            return -3; //source file error.
        }
        Py_XDECREF(pArg);
    }
    // output
    *pp = m_pInstance;

    Py_XDECREF(m_pTypeObj);
    return 0;
}

CSphPythonConfigParserHelper::CSphPythonConfigParserHelper(CSphConfigParser* p)
    :m_p(p), m_conf_classname("")
{
    if (p->m_tConf("python") && p->m_tConf["python"]("python") )
    {
#if USE_PYTHON
            CSphConfigSection & hPython =  p->m_tConf["python"]["python"];
            if(!cftInitialize(hPython))
                    sphDie ( "Python layer's initiation failed.");

            // create python object
            if( hPython("config_provider") )
                m_conf_classname = hPython["config_provider"];
#else
            sphDie ( "Python layer defined, but indexer does Not supports python. used --enbale-python to recompile.");
#endif
    }
}

bool  CSphPythonConfigParserHelper::LoadFromPython(){
    if(!m_conf_classname.IsEmpty()) {
        PyObject* m_pInstance;
        if(cftCreateObject(m_conf_classname.cstr(), &m_pInstance) == 0) {
           // to conf stuff.
        }
    }
    return true;
}

bool CSphPythonConfigParserHelper::AddSection ( const char * sType, const char * sSection )
{
    return m_p->AddSection(sType, sSection);
}

void CSphPythonConfigParserHelper::AddKey ( const char * sKey, char * sValue )
{
    return m_p->AddKey(sKey, sValue);
}

#endif //USE_PYTHON

// end of file.


