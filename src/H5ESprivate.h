/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5.  The full HDF5 copyright notice, including     *
 * terms governing use, modification, and redistribution, is contained in    *
 * the COPYING file, which can be found at the root of the source code       *
 * distribution tree, or in https://support.hdfgroup.org/ftp/HDF5/releases.  *
 * If you do not have access to either file, you may request a copy from     *
 * help@hdfgroup.org.                                                        *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*-------------------------------------------------------------------------
 *
 * Created:		H5ESprivate.h
 *			Apr  6 2020
 *			Quincey Koziol <koziol@lbl.gov>
 *
 * Purpose:		Private header for library accessible event set routines.
 *
 *-------------------------------------------------------------------------
 */

#ifndef _H5ESprivate_H
#define _H5ESprivate_H

/* Include package's public header */
#include "H5ESpublic.h"         /* Event Sets                  */

/* Private headers needed by this file */
#include "H5VLprivate.h"        /* Virtual Object Layer        */


/**************************/
/* Library Private Macros */
/**************************/


/****************************/
/* Library Private Typedefs */
/****************************/

/* Typedef for event set objects */
typedef struct H5ES_t H5ES_t;


/*****************************/
/* Library-private Variables */
/*****************************/


/***************************************/
/* Library-private Function Prototypes */
/***************************************/
herr_t H5ES_insert(H5ES_t *es, H5VL_object_t *request);


#endif /* _H5ESprivate_H */
