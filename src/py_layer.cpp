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


// Manual write config reciever -> I'd NOT wanna took time on sip's linking..
#define RET_PYNONE	{ Py_INCREF(Py_None); return Py_None; }

PyObject * csfHelper_CSphPythonConfigParserHelper_AddSection(PyObject *, PyObject* args)  ;
PyObject * csfHelper_CSphPythonConfigParserHelper_AddKey(PyObject *, PyObject* args)  ;

typedef struct {
    PyObject_HEAD
    /* Type-specific fields go here. */
    CSphPythonConfigParserHelper* m_conf;
} csfHelper_CSphPythonConfigParserHelper;

static PyMethodDef Helper_methods[] = {
    //{"Numbers", Example_new_Numbers, METH_VARARGS},
    {"addSection", csfHelper_CSphPythonConfigParserHelper_AddSection, METH_VARARGS},
    {"addKey", csfHelper_CSphPythonConfigParserHelper_AddKey, METH_VARARGS},
    {NULL, NULL}
};

static PyTypeObject csfHelper_ConfigParserType = {
    PyObject_HEAD_INIT(NULL)
    0, /*ob_size*/
    "csfHelper.ToLower", /*tp_name*/
    sizeof(csfHelper_CSphPythonConfigParserHelper), /*tp_basicsize*/
    0, /*tp_itemsize*/
    0, /*tp_dealloc*/
    0, /*tp_print*/
    0, /*tp_getattr*/
    0, /*tp_setattr*/
    0, /*tp_compare*/
    0, /*tp_repr*/
    0, /*tp_as_number*/
    0, /*tp_as_sequence*/
    0, /*tp_as_mapping*/
    0, /*tp_hash */
    0, /*tp_call*/
    0, /*tp_str*/
    0, /*tp_getattro*/
    0, /*tp_setattro*/
    0, /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT, /*tp_flags*/
    "ToLower Helper", /* tp_doc */
    0, /*tp_traverse*/
    0, /*tp_clear*/
    0, /*tp_richcompare*/
    0, /*tp_weaklistoffset*/
    0, /*tp_iter*/
    0, /*tp_iternext*/
    Helper_methods, /*tp_methods*/
    0, /*tp_members*/
    0, /*tp_getset*/
    0, /*tp_base*/
    0, /*tp_dict*/
    0, /*tp_descr_get*/
    0, /*tp_descr_set*/
    0, /*tp_dictoffset*/
    0, /*tp_init*/
    0, /*tp_alloc*/
    0, /*tp_new*/
    0, /*tp_free*/
    0, /*tp_is_gc*/
    0, /*tp_bases*/
    0, /*tp_mro*/
    0, /*tp_cache*/
    0, /*tp_subclasses*/
    0, /*tp_weaklist*/
};

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

            csfHelper_CSphPythonConfigParserHelper *self;
            self = (csfHelper_CSphPythonConfigParserHelper *)PyType_GenericNew(&csfHelper_ConfigParserType, NULL, NULL);
            self->m_conf = this;
            {
                // call function
                PyObject*  m_pInstance_LoadConfig = PyObject_GetAttrString(m_pInstance, "loadConfig"); // +1
                if(!m_pInstance_LoadConfig || m_pInstance_LoadConfig == Py_None){
                    // BuildHits CAN '404 Not found.'
                    if(PyErr_Occurred())
                        PyErr_Clear();
                    sphWarn("[Python] config_provider assigned, but method `loadConfig(self, cong_writer)` not defined.");
                }else{
                    // call function
                    PyObject* pargs  = Py_BuildValue("(O)", self); //+1
                    PyObject* pResult = Py_None;
                    pResult = PyEval_CallObject(m_pInstance_LoadConfig, pargs);
                    if(!pResult && PyErr_Occurred()) {
                        PyErr_Print(); //report the error.
                        Py_XDECREF(pargs);
                    }
                    // this function should return pyNone | void...
                    Py_XDECREF(pResult);
                    Py_XDECREF(pargs);
                }
                Py_XDECREF(m_pInstance_LoadConfig);
            }
            Py_XDECREF(m_pInstance);
            //return (PyObject*)self;
            return true;
        }
    }
    return false;
}

bool CSphPythonConfigParserHelper::AddSection ( const char * sType, const char * sSection )
{
    return m_p->AddSection(sType, sSection);
}

void CSphPythonConfigParserHelper::AddKey ( const char * sKey, char * sValue )
{
    return m_p->AddKey(sKey, sValue);
}

PyObject * csfHelper_CSphPythonConfigParserHelper_AddSection(PyObject * pSelf, PyObject* args) {

    csfHelper_CSphPythonConfigParserHelper *self = (csfHelper_CSphPythonConfigParserHelper *)pSelf;

    char *sKey = NULL, *sSection = NULL;
    int ok = PyArg_ParseTuple( args, "ss", &sKey, &sSection);
    if(!ok) {
        return NULL;
    }
    //printf("s:%s\tn:%s\n",sKey, sSection);
    if ( self->m_conf->AddSection(sKey, sSection) )
        Py_RETURN_TRUE;
    Py_RETURN_FALSE;
}

PyObject * csfHelper_CSphPythonConfigParserHelper_AddKey(PyObject * pSelf, PyObject* args)  {
    csfHelper_CSphPythonConfigParserHelper *self = (csfHelper_CSphPythonConfigParserHelper *)pSelf;

    char* sKey = NULL, *sSection = NULL;
    int ok = PyArg_ParseTuple( args, "ss", &sKey, &sSection); // section should be value.
    if(!ok) {
        return NULL;
    }
    self->m_conf->AddKey(sKey, sSection);
    RET_PYNONE
}


// init & deinit

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
        // init modules
        {
            csfHelper_ConfigParserType.tp_new = PyType_GenericNew;
            if (PyType_Ready(&csfHelper_ConfigParserType) < 0)
                return false;
        // PyObject* m =
                Py_InitModule("csfHelper", Helper_methods);
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

#endif //USE_PYTHON

// end of file.


