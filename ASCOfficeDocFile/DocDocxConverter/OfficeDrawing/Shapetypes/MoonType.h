﻿/*
 * (c) Copyright Ascensio System SIA 2010-2016
 *
 * This program is a free software product. You can redistribute it and/or
 * modify it under the terms of the GNU Affero General Public License (AGPL)
 * version 3 as published by the Free Software Foundation. In accordance with
 * Section 7(a) of the GNU AGPL its Section 15 shall be amended to the effect
 * that Ascensio System SIA expressly excludes the warranty of non-infringement
 * of any third-party rights.
 *
 * This program is distributed WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR  PURPOSE. For
 * details, see the GNU AGPL at: http://www.gnu.org/licenses/agpl-3.0.html
 *
 * You can contact Ascensio System SIA at Lubanas st. 125a-25, Riga, Latvia,
 * EU, LV-1021.
 *
 * The  interactive user interfaces in modified source and object code versions
 * of the Program must display Appropriate Legal Notices, as required under
 * Section 5 of the GNU AGPL version 3.
 *
 * Pursuant to Section 7(b) of the License you must retain the original Product
 * logo when distributing the program. Pursuant to Section 7(e) we decline to
 * grant you any rights under trademark law for use of our trademarks.
 *
 * All the Product's GUI elements, including illustrations and icon sets, as
 * well as technical writing content are licensed under the terms of the
 * Creative Commons Attribution-ShareAlike 4.0 International. See the License
 * terms at http://creativecommons.org/licenses/by-sa/4.0/legalcode
 *
 */
#pragma once

#include "../ShapeType.h"

namespace DocFileFormat
{
	class MoonType : public ShapeType
	{
	public:
		MoonType () : ShapeType(msosptMoon)
		{
			ShapeConcentricFill	=	true;
			Joins				=	miter;
			Path				=	_T("m21600,qx,10800,21600,21600wa@0@10@6@11,21600,21600,21600,xe");

			Formulas.push_back(_T("val #0"));
			Formulas.push_back(_T("sum 21600 0 #0"));
			Formulas.push_back(_T("prod #0 #0 @1"));
			Formulas.push_back(_T("prod 21600 21600 @1"));
			Formulas.push_back(_T("prod @3 2 1"));
			Formulas.push_back(_T("sum @4 0 @2"));
			Formulas.push_back(_T("sum @5 0 #0"));
			Formulas.push_back(_T("prod @5 1 2"));
			Formulas.push_back(_T("sum @7 0 #0"));
			Formulas.push_back(_T("prod @8 1 2"));
			Formulas.push_back(_T("sum 10800 0 @9"));
			Formulas.push_back(_T("sum @9 10800 0"));
			Formulas.push_back(_T("prod #0 9598 32768"));
			Formulas.push_back(_T("sum 21600 0 @12"));
			Formulas.push_back(_T("ellipse @13 21600 10800"));
			Formulas.push_back(_T("sum 10800 0 @14"));
			Formulas.push_back(_T("sum @14 10800 0"));

			AdjustmentValues	=	_T("10800");
			ConnectorLocations	=	_T("21600,0;0,10800;21600,21600;@0,10800");
			ConnectorAngles		=	_T("270,180,90,0");
			TextBoxRectangle	=	_T("@12,@15,@0,@16");

			Handle one;
			one.position		=	_T("#0,center");
			one.xrange			=	_T("0,18900"); 
			Handles.push_back (one);
		}
	};
}
