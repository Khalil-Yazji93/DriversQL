#pragma once
#define WIN9X_COMPAT_SPINLOCK
#include <ntddk.h>
#pragma warning(disable:4201)  // nameless struct/union warning
#pragma warning(disable:4214)
#include <stdarg.h>
#include <wdf.h>

#pragma warning(default:4201)

#include <initguid.h> // required for GUID definitions
#include <wdmguid.h>  // required for WMILIB_CONTEXT

#include "Reg9030.h"
#include "Public.h"
#include "Private.h"
#include "trace.h"

