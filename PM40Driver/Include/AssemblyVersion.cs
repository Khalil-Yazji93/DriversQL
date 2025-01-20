// -------------------------------------------------------------------------
// 
// PRODUCT:		    Expresso GUI
// MODULE NAME:     AssemblyVersion.cs
// 
// MODULE DESCRIPTION: 
// 
// Master Version file for C# programs
// 
// $Revision:  $
//
// ------------------------- CONFIDENTIAL ----------------------------------
// 
//              Copyright (c) 2017 by Northwest Logic, Inc.    
//                       All rights reserved. 
// 
// Trade Secret of Northwest Logic, Inc.  Do not disclose. 
// 
// Use of this source code in any form or means is permitted only 
// with a valid, written license agreement with Northwest Logic, Inc. 
// 
// Licensee shall keep all information contained herein confidential  
// and shall protect same in whole or in part from disclosure and  
// dissemination to all third parties. 
// 
// 
//                        Northwest Logic, Inc. 
//                  1100 NW Compton Drive, Suite 100 
//                      Beaverton, OR 97006, USA 
//   
//                        Ph:  +1 503 533 5800 
//                        Fax: +1 503 533 5900 
//                      E-Mail: info@nwlogic.com 
//                           www.nwlogic.com 
// 
// -------------------------------------------------------------------------

using System;
using System.Reflection;
using System.Runtime.CompilerServices;


//
// Version information for an assembly consists of the following four values:
//
//      Major Version
//      Minor Version
//      Build Number
//      Revision
//
// You can specify all the values or you can default the Revision and Build Numbers
// by using the '*' as shown below:

[assembly: AssemblyVersion(NWLogic.Versioning.SharedConstants.VersionStr)]
[assembly: AssemblyFileVersion(NWLogic.Versioning.SharedConstants.VersionStr)]

//
// General Information about an assembly is controlled through the following
// set of attributes. Change these attribute values to modify the information
// associated with an assembly.
//
[assembly: AssemblyCompany(NWLogic.Versioning.SharedConstants.CompanyStr)]
[assembly: AssemblyCopyright(NWLogic.Versioning.SharedConstants.CopyrightDateStr + " " +
    NWLogic.Versioning.SharedConstants.CompanyStr)]
[assembly: AssemblyProduct("Expresso PciExpress")]

namespace NWLogic.Versioning
{
    internal class SharedConstants
    {
        public const string VersionStr = "4.7.4.0";
        public const string CompanyStr = "Northwest Logic, Inc.";
        public const string CopyrightDateStr = "2005-2017";
    }
}
