/*	$OpenBSD: fld_type.c,v 1.4 1998/07/24 02:36:53 millert Exp $	*/

/****************************************************************************
 * Copyright (c) 1998 Free Software Foundation, Inc.                        *
 *                                                                          *
 * Permission is hereby granted, free of charge, to any person obtaining a  *
 * copy of this software and associated documentation files (the            *
 * "Software"), to deal in the Software without restriction, including      *
 * without limitation the rights to use, copy, modify, merge, publish,      *
 * distribute, distribute with modifications, sublicense, and/or sell       *
 * copies of the Software, and to permit persons to whom the Software is    *
 * furnished to do so, subject to the following conditions:                 *
 *                                                                          *
 * The above copyright notice and this permission notice shall be included  *
 * in all copies or substantial portions of the Software.                   *
 *                                                                          *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  *
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF               *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.   *
 * IN NO EVENT SHALL THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,   *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR    *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR    *
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.                               *
 *                                                                          *
 * Except as contained in this notice, the name(s) of the above copyright   *
 * holders shall not be used in advertising or otherwise to promote the     *
 * sale, use or other dealings in this Software without prior written       *
 * authorization.                                                           *
 ****************************************************************************/

/****************************************************************************
 *   Author: Juergen Pfeifer <Juergen.Pfeifer@T-Online.de> 1995,1997        *
 ****************************************************************************/

#include "form.priv.h"

MODULE_ID("$From: fld_type.c,v 1.7 1998/02/11 12:13:43 tom Exp $")

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  int set_field_type(FIELD *field, FIELDTYPE *type,...)
|   
|   Description   :  Associate the specified fieldtype with the field.
|                    Certain field types take additional arguments. Look
|                    at the spec of the field types !
|
|   Return Values :  E_OK           - success
|                    E_SYSTEM_ERROR - system error
+--------------------------------------------------------------------------*/
int set_field_type(FIELD *field,FIELDTYPE *type, ...)
{
  va_list ap;
  int res = E_SYSTEM_ERROR;
  int err = 0;

  va_start(ap,type);

  Normalize_Field(field);
  _nc_Free_Type(field);

  field->type = type;
  field->arg  = (void *)_nc_Make_Argument(field->type,&ap,&err);

  if (err)
    {
      _nc_Free_Argument(field->type,(TypeArgument *)(field->arg));
      field->type = (FIELDTYPE *)0;
      field->arg  = (void *)0;
    }
  else
    {
      res = E_OK;
      if (field->type) 
	field->type->ref++;
    }

  va_end(ap);
  RETURN(res);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  FIELDTYPE *field_type(const FIELD *field)
|   
|   Description   :  Retrieve the associated fieldtype for this field.
|
|   Return Values :  Pointer to fieldtype of NULL if none is defined.
+--------------------------------------------------------------------------*/
FIELDTYPE *field_type(const FIELD * field)
{
  return Normalize_Field(field)->type;
}

/* fld_type.c ends here */
