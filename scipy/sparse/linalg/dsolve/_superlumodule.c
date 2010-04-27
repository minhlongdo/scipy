/* -*-c-*-  */
/*
 * _superlu module
 *
 * Python interface to SuperLU decompositions.
 */

/* Copyright 1999 Travis Oliphant
 *
 * Permision to copy and modified this file is granted under
 * the revised BSD license. No warranty is expressed or IMPLIED
 */

#include <Python.h>
#include <setjmp.h>

#include "_superluobject.h"

extern jmp_buf _superlu_py_jmpbuf;

/*
 * Data-type dependent implementations for Xgssv and Xgstrf;
 *
 * These have to included from separate files because of SuperLU include
 * structure.
 */

static PyObject *
Py_gssv(PyObject *self, PyObject *args, PyObject *kwdict)
{
    PyObject *Py_B=NULL, *Py_X=NULL;
    PyArrayObject *nzvals=NULL;
    PyArrayObject *colind=NULL, *rowptr=NULL;
    int N, nnz;
    int info;
    int csc=0, permc_spec=2;
    int *perm_r=NULL, *perm_c=NULL;
    SuperMatrix A, B, L, U;
    superlu_options_t options;
    SuperLUStat_t stat;
    int type;

    static char *kwlist[] = {"N","nnz","nzvals","colind","rowptr","B", "csc",
                             "permc_spec",NULL};
    
    /* Get input arguments */
    if (!PyArg_ParseTupleAndKeywords(args, kwdict, "iiO!O!O!O|ii", kwlist,
                                     &N, &nnz, &PyArray_Type, &nzvals,
                                     &PyArray_Type, &colind, &PyArray_Type,
                                     &rowptr, &Py_B, &csc, &permc_spec)) {
        return NULL;
    }

    if (!_CHECK_INTEGER(colind) || !_CHECK_INTEGER(rowptr)) {
        PyErr_SetString(PyExc_TypeError,
                        "colind and rowptr must be of type cint");
        return NULL;
    }

    type = PyArray_TYPE(nzvals);
    if (!CHECK_SLU_TYPE(type)) {
        PyErr_SetString(PyExc_TypeError,
                        "nzvals is not of a type supported by SuperLU");
        return NULL;
    }

    /* Create Space for output */
    Py_X = PyArray_CopyFromObject(Py_B, type, 1, 2);
    if (Py_X == NULL) return NULL;

    if (csc) {
        if (NCFormat_from_spMatrix(&A, N, N, nnz, nzvals, colind, rowptr,
                                   type)) {
            Py_DECREF(Py_X);
            return NULL;
        }
    }
    else {
        if (NRFormat_from_spMatrix(&A, N, N, nnz, nzvals, colind, rowptr,
                                   type)) {
            Py_DECREF(Py_X);
            return NULL;
        }
    }
    
    if (DenseSuper_from_Numeric(&B, Py_X)) {
        Destroy_SuperMatrix_Store(&A);  
        Py_DECREF(Py_X);
        return NULL;
    }

    /* B and Py_X  share same data now but Py_X "owns" it */
    
    /* Setup options */
    
    if (setjmp(_superlu_py_jmpbuf)) {
        goto fail;
    }
    else {
        perm_c = intMalloc(N);
        perm_r = intMalloc(N);
        set_default_options(&options);
        options.ColPerm = superlu_module_getpermc(permc_spec);
        StatInit(&stat);

        /* Compute direct inverse of sparse Matrix */
        gssv(type, &options, &A, perm_c, perm_r, &L, &U, &B, &stat, &info);
    }
    
    SUPERLU_FREE(perm_r);
    SUPERLU_FREE(perm_c);
    Destroy_SuperMatrix_Store(&A);  /* holds just a pointer to the data */
    Destroy_SuperMatrix_Store(&B);
    Destroy_SuperNode_Matrix(&L);
    Destroy_CompCol_Matrix(&U);
    StatFree(&stat);
 
    return Py_BuildValue("Ni", Py_X, info);

fail:
    SUPERLU_FREE(perm_r);
    SUPERLU_FREE(perm_c);
    Destroy_SuperMatrix_Store(&A);  /* holds just a pointer to the data */
    Destroy_SuperMatrix_Store(&B);
    Destroy_SuperNode_Matrix(&L);
    Destroy_CompCol_Matrix(&U);
    StatFree(&stat);  
    Py_XDECREF(Py_X);
    return NULL;
}

static PyObject *
Py_gstrf(PyObject *self, PyObject *args, PyObject *keywds)
{
    /* default value for SuperLU parameters*/
    double diag_pivot_thresh = 1.0;
    int relax = 1;
    int panel_size = 10;
    int permc_spec = 2;
    int N, nnz;
    PyArrayObject *rowind, *colptr, *nzvals;
    SuperMatrix A;
    PyObject *result;
    int type;
  
    static char *kwlist[] = {"N","nnz","nzvals","rowind","colptr",
                             "permc_spec","diag_pivot_thresh",
                             "relax", "panel_size", NULL};

    int res = PyArg_ParseTupleAndKeywords(
        args, keywds, "iiO!O!O!|iddii", kwlist, 
        &N, &nnz,
        &PyArray_Type, &nzvals,
        &PyArray_Type, &rowind,
        &PyArray_Type, &colptr,
        &permc_spec,
        &diag_pivot_thresh,
        &relax,
        &panel_size);

    if (!res)
        return NULL;

    if (!_CHECK_INTEGER(colptr) || !_CHECK_INTEGER(rowind)) {
        PyErr_SetString(PyExc_TypeError,
                        "rowind and colptr must be of type cint");
        return NULL;
    }

    type = PyArray_TYPE(nzvals);
    if (!CHECK_SLU_TYPE(type)) {
        PyErr_SetString(PyExc_TypeError,
                        "nzvals is not of a type supported by SuperLU");
        return NULL;
    }

    if (NCFormat_from_spMatrix(&A, N, N, nnz, nzvals, rowind, colptr,
                               type)) {
        goto fail;
    }

    result = newSciPyLUObject(&A, diag_pivot_thresh, relax,
                              panel_size, permc_spec, type);
    if (result == NULL) {
        goto fail;
    }

    /* arrays of input matrix will not be freed */  
    Destroy_SuperMatrix_Store(&A);
    return result;
  
fail:
    /* arrays of input matrix will not be freed */
    Destroy_SuperMatrix_Store(&A); 
    return NULL;
}

static char gssv_doc[] = "Direct inversion of sparse matrix.\n\nX = gssv(A,B) solves A*X = B for X.";

static char gstrf_doc[] = "gstrf(A, ...)\n\
\n\
performs a factorization of the sparse matrix A=*(N,nnz,nzvals,rowind,colptr) and \n\
returns a factored_lu object.\n\
\n\
arguments\n\
---------\n\
\n\
Matrix to be factorized is represented as N,nnz,nzvals,rowind,colptr\n\
  as separate arguments.  This is compressed sparse column representation.\n\
\n\
N         number of rows and columns \n\
nnz       number of non-zero elements\n\
nzvals    non-zero values \n\
rowind    row-index for this column (same size as nzvals)\n\
colptr    index into rowind for first non-zero value in this column\n\
          size is (N+1).  Last value should be nnz. \n\
\n\
additional keyword arguments:\n\
-----------------------------\n\
permc_spec          specifies the matrix ordering used for the factorization\n\
                    0: natural ordering\n\
                    1: MMD applied to the structure of A^T * A\n\
                    2: MMD applied to the structure of A^T + A\n\
                    3: COLAMD, approximate minimum degree column ordering\n\
                    (default: 2)\n\
\n\
diag_pivot_thresh   threshhold for partial pivoting.\n\
                    0.0 <= diag_pivot_thresh <= 1.0\n\
                    0.0 corresponds to no pivoting\n\
                    1.0 corresponds to partial pivoting\n\
                    (default: 1.0)\n\
\n\
relax               to control degree of relaxing supernodes\n\
                    (default: 1)\n\
\n\
panel_size          a panel consist of at most panel_size consecutive columns.\n\
                    (default: 10)\n\
";


/*
 * Main SuperLU module
 */

static PyMethodDef SuperLU_Methods[] = {
    {"gssv", (PyCFunction)Py_gssv, METH_VARARGS|METH_KEYWORDS, gssv_doc},  
    {"gstrf", (PyCFunction)Py_gstrf, METH_VARARGS|METH_KEYWORDS, gstrf_doc},
    {NULL, NULL}
};

PyMODINIT_FUNC
init_superlu(void)
{
    PyObject *m, *d;

    SciPySuperLUType.ob_type = &PyType_Type;

    m = Py_InitModule("_superlu", SuperLU_Methods);
    d = PyModule_GetDict(m);

    PyDict_SetItemString(d, "SciPyLUType", (PyObject *)&SciPySuperLUType);

    import_array();
}